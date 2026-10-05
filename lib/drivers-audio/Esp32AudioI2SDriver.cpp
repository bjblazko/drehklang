#include "Esp32AudioI2SDriver.h"

#include <Arduino.h>
#include <Audio.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>
#include <new>

#include "AudioGain.h"
#include "AudioHooks.h"
#include "AudioOutputStage.h"
#include "Mp3Duration.h"
#include "Mp4Parser.h"
#include "SdRawFile.h"

namespace drehklang::drivers {

namespace {
using playback::AudioGain;

constexpr uint32_t kTaskStackBytes = 8192;
// Above Arduino's loopTask (1): keeping the input buffer filled is
// latency-sensitive. Core 0, away from LVGL's flush on core 1 (ADR 0006).
constexpr UBaseType_t kTaskPriority = 3;
constexpr BaseType_t kAudioCore = 0;

// RAII take/give of the driver's mutex.
struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }
  SemaphoreHandle_t m_;
};

std::string lowerExtension(const std::string &path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = path.substr(dot + 1);
  for (char &c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return ext;
}

#ifdef AUDIO_LOG
// The library's messages, for diagnosing playback. Called on its decode
// task, so never a blocking Serial write (AGENTS.md): a line that does not
// fit is dropped.
void logAudioMessage(Audio::msg_t message) {
  if (message.msg == nullptr) return;
  char line[160];
  const int length = snprintf(line, sizeof(line), "[audio] %s: %s\n",
                              Audio::eventStr[message.e], message.msg);
  if (length > 0 && Serial.availableForWrite() >= length) {
    Serial.write(reinterpret_cast<const uint8_t *>(line), length);
  }
}
#endif
}  // namespace

Esp32AudioI2SDriver::Esp32AudioI2SDriver(playback::DacArbiter &dac) : dac_(dac) {}

Esp32AudioI2SDriver::~Esp32AudioI2SDriver() = default;

void Esp32AudioI2SDriver::InternalRamDeleter::operator()(Audio *audio) const {
  audio->~Audio();
  heap_caps_free(audio);
}

void Esp32AudioI2SDriver::begin() {
  mutex_ = xSemaphoreCreateMutex();
#ifdef AUDIO_LOG
  Audio::audio_info_callback = logAudioMessage;
#endif
  xTaskCreatePinnedToCore(&Esp32AudioI2SDriver::taskTrampoline, "audio", kTaskStackBytes,
                          this, kTaskPriority, nullptr, kAudioCore);
}

bool Esp32AudioI2SDriver::playFileAt(const std::string &path, uint32_t position) {
  // Before taking mutex_: claiming may close the tone output's channel,
  // which takes that class's own lock.
  dac_.claim(*this);
  timing_ = readTrackTiming(path);
  Lock lock(mutex_);
  return openTrack(path, position);
}

// Caller holds mutex_ and owns the DAC.
bool Esp32AudioI2SDriver::openTrack(const std::string &path, uint32_t position) {
  ensureAudio();
  if (!audio_) return false;  // No internal RAM for it; the caller stops.
  audio_->stopSong();
  path_ = path;
  paused_ = false;
  parked_ = false;
  pendingSeek_ = position > 0 ? static_cast<int64_t>(position) : -1;
  setHoldOutput(pendingSeek_ >= 0);
  const bool ok = audio_->connecttoFS(SD_MMC, path.c_str());
  if (!ok) {
    path_.clear();
    pendingSeek_ = -1;
    setHoldOutput(false);
  }
  return ok;
}

// Caller holds mutex_. Creating Audio opens its I2S channel and starts its
// decode task; its destructor undoes both (releaseDac()).
void Esp32AudioI2SDriver::ensureAudio() {
  if (audio_) return;
  // Zeroed, like the global `Audio audio;` the library is written for:
  // its constructor leaves members such as the M4A header state
  // uninitialised, and garbage there made every M4A skip to a nonsense
  // offset and end at once (2026-10-05).
  void *memory = heap_caps_calloc(1, sizeof(Audio), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (memory == nullptr) return;
  audio_.reset(new (memory) Audio());
  audio_->setPinout(kAudioBclkPin, kAudioLrcPin, kAudioDoutPin);
  audio_->setVolumeCurve(&AudioGain::volumeCurveDb);
  audio_->setVolume(volume_);
}

void Esp32AudioI2SDriver::releaseDac() {
  Lock lock(mutex_);
  if (!audio_) return;
  if (!path_.empty()) {
    parkedPosition_ = heardPosition();
    parked_ = true;
  }
  pendingSeek_ = -1;
  setHoldOutput(false);
  audio_.reset();
}

void Esp32AudioI2SDriver::pause() {
  Lock lock(mutex_);
  if (parked_ || !audio_ || paused_) return;
  audio_->pauseResume();
  paused_ = true;
}

void Esp32AudioI2SDriver::resume() {
  if (parked_) {
    dac_.claim(*this);
    Lock lock(mutex_);
    openTrack(path_, parkedPosition_);
    return;
  }
  Lock lock(mutex_);
  if (!audio_ || !paused_) return;
  audio_->pauseResume();
  paused_ = false;
}

void Esp32AudioI2SDriver::stop() {
  Lock lock(mutex_);
  if (audio_) audio_->stopSong();
  path_.clear();
  paused_ = false;
  parked_ = false;
  pendingSeek_ = -1;
  setHoldOutput(false);
}

void Esp32AudioI2SDriver::setVolume(uint8_t volume) {
  Lock lock(mutex_);
  volume_ = volume;
  if (audio_) audio_->setVolume(volume);
  audioOutputStage().setVolumeStep(volume);
}

void Esp32AudioI2SDriver::setOutputGain(uint16_t gain) {
  audioOutputStage().setOutputGain(gain);
}

// A parked track still counts as running: it is only waiting for the DAC,
// and the main loop must not take it for finished.
bool Esp32AudioI2SDriver::isRunning() {
  Lock lock(mutex_);
  if (parked_) return true;
  return audio_ && audio_->isRunning();
}

uint32_t Esp32AudioI2SDriver::filePosition() {
  Lock lock(mutex_);
  return heardPosition();
}

// Where the listener is, for resume (ADR 0012). The library already
// subtracts what is still in its input buffer.
uint32_t Esp32AudioI2SDriver::heardPosition() {
  if (parked_) return parkedPosition_;
  if (pendingSeek_ >= 0) return static_cast<uint32_t>(pendingSeek_);
  return audio_ ? audio_->getAudioFilePosition() : 0;
}

// The library's own seek takes whole seconds -- too coarse for 2× cue
// (ADR 0013) -- and converts them with its own bitrate guess, which is
// wrong for VBR. So convert ms to bytes here, with the exact duration
// where the file states one, and seek by byte; the library re-aligns to a
// frame boundary itself.
bool Esp32AudioI2SDriver::seekByMs(int32_t deltaMs) {
  Lock lock(mutex_);
  if (parked_ || !audio_ || pendingSeek_ >= 0) return false;
  int64_t start = timing_.dataStart;
  // An M4A's moov atom (tags, cover, sample tables) can sit after the
  // audio, so the file size overstates the audio data.
  int64_t end = timing_.dataEnd != 0 ? timing_.dataEnd : audio_->getFileSize();
  const uint32_t avgBitrate =
      timing_.durationSeconds != 0 && end > start
          ? static_cast<uint32_t>((end - start) * 8 / timing_.durationSeconds)
          : audio_->getBitRate();
  if (avgBitrate == 0 || end <= 0) return false;
  const int64_t bytes = static_cast<int64_t>(deltaMs) * avgBitrate / 8000;
  // The library clamps a target before its audio data to the data start.
  const int64_t target =
      std::clamp<int64_t>(static_cast<int64_t>(audio_->getAudioFilePosition()) + bytes, start, end - 1);
  return audio_->setAudioFilePosition(static_cast<uint32_t>(target));
}

// The exact duration from the MP3's VBR header or the M4A's movie header
// when there is one; otherwise the library's estimate.
uint32_t Esp32AudioI2SDriver::durationSeconds() {
  Lock lock(mutex_);
  if (timing_.durationSeconds != 0) return timing_.durationSeconds;
  return audio_ ? audio_->getAudioFileDuration() : 0;
}

playback::SampleWindow Esp32AudioI2SDriver::readRecentSamples(int16_t *dst,
                                                              size_t maxSamples) {
  // Without mutex_: taking it at frame rate would wait on the loop task,
  // and a stale rate for one frame after a track change is harmless.
  return audioOutputStage().readRecentSamples(dst, maxSamples,
                                              sampleRate_.load(std::memory_order_relaxed));
}

// One extra open per track start, before the decoder opens the file; not
// under mutex_: it's a separate handle, and SD_MMC serializes card access
// itself.
Esp32AudioI2SDriver::TrackTiming Esp32AudioI2SDriver::readTrackTiming(const std::string &path) {
  TrackTiming timing;
  const std::string ext = lowerExtension(path);
  if (ext != "mp3" && ext != "m4a") return timing;
  fs::File file = SD_MMC.open(path.c_str());
  if (!file) return timing;
  SdRawFile raw(std::move(file));
  if (ext == "mp3") {
    timing.durationSeconds = library::Mp3Duration::readSeconds(raw);
    return timing;
  }
  library::Mp4Info info = library::Mp4Parser::parse(raw);
  timing.durationSeconds = (info.durationMs + 500) / 1000;
  timing.dataStart = info.mdatStart;
  timing.dataEnd = info.mdatEnd;
  return timing;
}

// A resume position can only be applied once the decoder is past the
// file's headers: the library needs the bitrate and the audio data range,
// and Vorbis needs its setup header, or it never decodes (an Ogg resumed
// after the tone generator raced through the file in silence,
// 2026-10-05). The first decoded samples prove both; until then the
// track's opening frames stay off the DAC.
void Esp32AudioI2SDriver::applyPendingSeek() {
  if (pendingSeek_ < 0 || !audio_->isRunning() || !decoderProducedSinceHold()) return;
  if (!audio_->setAudioFilePosition(static_cast<uint32_t>(pendingSeek_))) return;
  pendingSeek_ = -1;
  setHoldOutput(false);
}

void Esp32AudioI2SDriver::taskTrampoline(void *self) {
  static_cast<Esp32AudioI2SDriver *>(self)->taskLoop();
}

void Esp32AudioI2SDriver::taskLoop() {
  for (;;) {
    {
      Lock lock(mutex_);
      if (audio_) {
        audio_->loop();
        applyPendingSeek();
        // Idle, the library still reports a rate nothing is clocked at.
        if (audio_->isRunning()) sampleRate_.store(audio_->getSampleRate());
      }
    }
    // Yields to the idle task (feeds core 0's watchdog) between calls;
    // the library's own task keeps the DMA fed meanwhile.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace drehklang::drivers
