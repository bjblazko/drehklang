#include "ToneOutput.h"

#include <Arduino.h>
#include <freertos/task.h>

#include "AudioGain.h"
#include "AudioHooks.h"
#include "AudioOutputStage.h"
#include "AudioTap.h"
#include "Esp32AudioI2SDriver.h"

namespace drehklang::drivers {

namespace {
constexpr uint32_t kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kAudioCore = 0;
constexpr TickType_t kWriteTimeout = pdMS_TO_TICKS(50);

// RAII take/give of mutex_.
struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }
  SemaphoreHandle_t m_;
};
}  // namespace

void ToneOutput::begin() {
  mutex_ = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(&ToneOutput::taskTrampoline, "tones", kTaskStackBytes,
                          this, kTaskPriority, nullptr, kAudioCore);
}

void ToneOutput::blip(uint16_t frequencyHz, uint16_t durationMs) {
  claimDac(kBlipSampleRate);
  tone_.trigger(frequencyHz, durationMs);
}

void ToneOutput::noise(uint16_t clockHz, uint16_t durationMs, int16_t level) {
  claimDac(kBlipSampleRate);
  tone_.triggerNoise(clockHz, durationMs, level);
}

void ToneOutput::start() {
  claimDac(signal::kGeneratorSampleRate);
  control_.setRunning(true);
}

size_t ToneOutput::readRecent(int16_t *dst, size_t maxSamples) {
  return audioOutputStage()
      .readRecentSamples(dst, maxSamples, signal::kGeneratorSampleRate)
      .count;
}

void ToneOutput::claimDac(uint32_t rate) {
  dac_.claim(*this);
  Lock lock(mutex_);
  if (channel_ != nullptr && channelRate_ == rate) return;
  closeChannel();
  openChannel(rate);
}

void ToneOutput::releaseDac() {
  Lock lock(mutex_);
  closeChannel();
}

// 16-bit stereo, the PCM5100A's I2S pins from Esp32AudioI2SDriver.h. The
// DAC derives its clocks from BCLK, so it follows a rate change by itself.
bool ToneOutput::openChannel(uint32_t rate) {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;  // Silence, not the last buffer, when we stop writing.
  if (i2s_new_channel(&chan, &channel_, nullptr) != ESP_OK) {
    channel_ = nullptr;
    return false;
  }
  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO);
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
  channelRate_ = rate;
  return true;
}

void ToneOutput::closeChannel() {
  if (channel_ == nullptr) return;
  i2s_channel_disable(channel_);
  i2s_del_channel(channel_);
  channel_ = nullptr;
  channelRate_ = 0;
}

void ToneOutput::taskTrampoline(void *self) {
  static_cast<ToneOutput *>(self)->taskLoop();
}

void ToneOutput::taskLoop() {
  for (;;) {
    // The generator keeps writing through its fade-out after stop(), so
    // the last thing the DAC hears is silence rather than a cut.
    const bool generating = control_.running() || !oscillator_.idle();
    if (!generating && !tone_.active()) {
      vTaskDelay(pdMS_TO_TICKS(kPollMs));
      continue;
    }
    const uint32_t rate = generating ? signal::kGeneratorSampleRate : kBlipSampleRate;
    if (!writeChunk(rate)) vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

// One chunk of whichever voice is sounding. False when there is no channel
// at this rate to write to (the player owns the DAC, or the rate is about
// to change) -- the sound is then dropped, not queued.
bool ToneOutput::writeChunk(uint32_t rate) {
  auto &stage = audioOutputStage();
  if (rate == signal::kGeneratorSampleRate) {
    oscillator_.setParams(control_.snapshot());
    if (control_.running()) {
      oscillator_.start();
    } else {
      oscillator_.stop();
    }
    // Unscaled: the generator's level is stated in dBFS (ADR 0024).
    oscillator_.render(mono_, kChunkFrames, rate);
#ifdef DREHKLANG_GENERATOR_DEBUG
    logGenerator(rate);
#endif
  } else {
    for (size_t i = 0; i < kChunkFrames; ++i) {
      mono_[i] = playback::AudioGain::applyVolume(tone_.nextSample(rate),
                                                  stage.volumeStep(), stage.outputGain());
    }
  }
  for (size_t i = 0; i < kChunkFrames; ++i) {
    chunk_[i * 2] = mono_[i];
    chunk_[i * 2 + 1] = mono_[i];
    stage.noteMonoSample(mono_[i]);
  }
  Lock lock(mutex_);
  if (channel_ == nullptr || channelRate_ != rate) return false;
  size_t written = 0;
  if (i2s_channel_write(channel_, chunk_, sizeof(chunk_), &written, kWriteTimeout) != ESP_OK) {
    return false;
  }
  // Bluetooth gets what the jack gets (ADR 0027).
  if (auto *tap = bluetoothTap()) tap->pushStereo16(chunk_, kChunkFrames * 2, rate);
  return true;
}

#ifdef DREHKLANG_GENERATOR_DEBUG
// Once a second: the pitch and peak of what was actually written, measured
// from the samples rather than taken from the settings -- the check that
// the grid, the rate and the level all agree (ADR 0024).
void ToneOutput::logGenerator(uint32_t rate) {
  for (size_t i = 0; i < kChunkFrames; ++i) {
    const int16_t s = mono_[i];
    if (debugLast_ < 0 && s >= 0) ++debugCrossings_;
    debugLast_ = s;
    const int16_t magnitude = static_cast<int16_t>(s < 0 ? -s : s);
    if (magnitude > debugPeak_) debugPeak_ = magnitude;
  }
  debugSamples_ += kChunkFrames;
  if (debugSamples_ < rate) return;
  const float seconds = static_cast<float>(debugSamples_) / rate;
  const float dbfs = debugPeak_ > 0 ? 20.0f * log10f(debugPeak_ / 32767.0f) : -99.0f;
  // Never Serial.printf() from here: USBCDC::write() waits without a
  // timeout whenever the host is not draining the port (AGENTS.md), and
  // this is the audio task -- it tripped the task watchdog during a
  // SCREENSHOT transfer (2026-09-18). A line that does not fit is dropped.
  char line[80];
  const int length = snprintf(line, sizeof(line),
                              "[generator] %.1f Hz, peak %d (%.1f dBFS) at %lu Hz\n",
                              debugCrossings_ / seconds, debugPeak_, dbfs,
                              static_cast<unsigned long>(rate));
  if (length > 0 && Serial.availableForWrite() >= length) {
    Serial.write(reinterpret_cast<const uint8_t *>(line), length);
  }
  debugSamples_ = 0;
  debugCrossings_ = 0;
  debugPeak_ = 0;
}
#endif

}  // namespace drehklang::drivers
