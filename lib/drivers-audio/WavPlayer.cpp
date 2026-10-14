#include "WavPlayer.h"

#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include "AudioGain.h"
#include "AudioHooks.h"
#include "AudioOutputStage.h"
#include "AudioTap.h"
#include "Esp32AudioI2SDriver.h"

namespace drehklang::drivers {

namespace {
constexpr uint32_t kTaskStackBytes = 4096;
// The stack in PSRAM: the dictaphone's three tasks idle almost always, and
// internal RAM is what the music player's I2S buffers need (AGENTS.md).
constexpr UBaseType_t kStackCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kAudioCore = 0;
constexpr TickType_t kWriteTimeout = pdMS_TO_TICKS(50);
constexpr TickType_t kIdleDelay = pdMS_TO_TICKS(10);
// After every chunk, outside the lock: the write blocks while holding
// mutex_, and without a pause the task took it straight back -- rewind()
// on the main loop starved for seconds, the UI froze and the Bluetooth
// link timed out (2026-10-14). The DMA buffers hold ~90 ms.
constexpr TickType_t kYield = 1;
// Enough to find "data" behind a LIST chunk another tool may have added.
constexpr size_t kHeaderProbeBytes = 512;

struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }
  SemaphoreHandle_t m_;
};
}  // namespace

void WavPlayer::begin() {
  mutex_ = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCoreWithCaps(&WavPlayer::taskTrampoline, "wavplayer", kTaskStackBytes, this,
                                  kTaskPriority, nullptr, kAudioCore, kStackCaps);
}

bool WavPlayer::open(const std::string &path) {
  close();
  Lock lock(mutex_);
  fs::File file = SD_MMC.open(path.c_str(), FILE_READ);
  if (!file) return false;
  uint8_t probe[kHeaderProbeBytes];
  const size_t got = file.read(probe, sizeof(probe));
  const dictaphone::WavInfo info =
      dictaphone::readWavHeader(probe, got, static_cast<uint32_t>(file.size()));
  // What the recorder writes: 16-bit mono.
  if (!info.valid || info.format.channels != 1 || info.format.bitsPerSample != 16 ||
      !file.seek(info.dataOffset)) {
    file.close();
    return false;
  }
  file_ = file;
  path_ = path;
  info_ = info;
  position_.store(0);
  return true;
}

void WavPlayer::play() {
  {
    Lock lock(mutex_);
    if (!file_) return;
    if (position_.load() >= info_.dataBytes) {
      file_.seek(info_.dataOffset);
      position_.store(0);
    }
  }
  // Before taking mutex_ again: claiming closes the previous owner's
  // channel, which takes that class's own lock.
  dac_.claim(*this);
  Lock lock(mutex_);
  if (channel_ == nullptr && !openChannel()) return;
  playing_.store(true);
}

void WavPlayer::close() {
  playing_.store(false);
  Lock lock(mutex_);
  closeChannel();
  if (file_) file_.close();
  path_.clear();
  info_ = dictaphone::WavInfo{};
  position_.store(0);
}

void WavPlayer::rewind(uint32_t ms) {
  Lock lock(mutex_);
  if (!file_) return;
  const uint32_t back = static_cast<uint32_t>(
      static_cast<uint64_t>(ms) * info_.format.byteRate() / 1000);
  uint32_t target = std::min(position_.load(), info_.dataBytes);
  target = target > back ? target - back : 0;
  target -= target % info_.format.blockAlign();
  if (file_.seek(info_.dataOffset + target)) position_.store(target);
}

bool WavPlayer::remove(const std::string &path) {
  bool wasOpen = false;
  {
    Lock lock(mutex_);
    wasOpen = path == path_;
  }
  if (wasOpen) close();
  return SD_MMC.remove(path.c_str());
}

size_t WavPlayer::readRecent(int16_t *dst, size_t maxSamples) {
  return audioOutputStage()
      .readRecentSamples(dst, maxSamples, info_.format.sampleRate)
      .count;
}

void WavPlayer::releaseDac() {
  playing_.store(false);
  Lock lock(mutex_);
  closeChannel();
}

// 16-bit stereo on the PCM5100A's pins, as ToneOutput opens it; the mono
// recording goes to both sides.
bool WavPlayer::openChannel() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;  // Silence, not the last buffer, while paused.
  if (i2s_new_channel(&chan, &channel_, nullptr) != ESP_OK) {
    channel_ = nullptr;
    return false;
  }
  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(info_.format.sampleRate);
  std.slot_cfg =
      I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  std.gpio_cfg.bclk = static_cast<gpio_num_t>(kAudioBclkPin);
  std.gpio_cfg.ws = static_cast<gpio_num_t>(kAudioLrcPin);
  std.gpio_cfg.dout = static_cast<gpio_num_t>(kAudioDoutPin);
  std.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(channel_, &std) != ESP_OK ||
      i2s_channel_enable(channel_) != ESP_OK) {
    i2s_del_channel(channel_);
    channel_ = nullptr;
    return false;
  }
  return true;
}

void WavPlayer::closeChannel() {
  if (channel_ == nullptr) return;
  i2s_channel_disable(channel_);
  i2s_del_channel(channel_);
  channel_ = nullptr;
}

void WavPlayer::taskTrampoline(void *self) { static_cast<WavPlayer *>(self)->taskLoop(); }

void WavPlayer::taskLoop() {
  for (;;) {
    if (!playing_.load()) {
      vTaskDelay(kIdleDelay);
      continue;
    }
    // At the end it stops by itself; play() starts it over.
    if (!playChunk()) playing_.store(false);
    vTaskDelay(kYield);
  }
}

bool WavPlayer::playChunk() {
  auto &stage = audioOutputStage();
  Lock lock(mutex_);
  if (channel_ == nullptr || !file_) return false;
  const uint32_t left = info_.dataBytes - std::min(position_.load(), info_.dataBytes);
  const size_t want = std::min<size_t>(sizeof(mono_), left);
  const size_t got = file_.read(reinterpret_cast<uint8_t *>(mono_), want);
  const size_t frames = got / sizeof(int16_t);
  if (frames == 0) return false;
  for (size_t i = 0; i < frames; ++i) {
    stage.noteMonoSample(mono_[i]);
    const int16_t sample =
        playback::AudioGain::applyVolume(mono_[i], stage.volumeStep(), stage.outputGain());
    stereo_[i * 2] = sample;
    stereo_[i * 2 + 1] = sample;
  }
  position_.fetch_add(static_cast<uint32_t>(frames * sizeof(int16_t)));
  size_t written = 0;
  if (i2s_channel_write(channel_, stereo_, frames * 2 * sizeof(int16_t), &written,
                        kWriteTimeout) != ESP_OK) {
    return false;
  }
  // Bluetooth gets what the jack gets (ADR 0027).
  if (auto *tap = bluetoothTap()) {
    tap->pushStereo16(stereo_, frames * 2, info_.format.sampleRate);
  }
  return true;
}

}  // namespace drehklang::drivers
