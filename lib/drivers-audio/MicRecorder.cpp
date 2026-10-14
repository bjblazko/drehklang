#include "MicRecorder.h"

#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include "AudioOutputStage.h"
#include "RecordingShelf.h"

namespace drehklang::drivers {

namespace {
constexpr uint32_t kTaskStackBytes = 4096;
// The stack in PSRAM: the dictaphone's three tasks idle almost always, and
// internal RAM is what the music player's I2S buffers need (AGENTS.md).
constexpr UBaseType_t kStackCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
// Like the tone task: above the WAV writer, so a slow card never makes
// the microphone's DMA overrun.
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kAudioCore = 0;
constexpr TickType_t kReadTimeout = pdMS_TO_TICKS(50);
constexpr TickType_t kIdleDelay = pdMS_TO_TICKS(10);
// After every block, outside the lock. The read blocks for most of a block
// while holding mutex_; without a pause the task took it straight back,
// and the main loop waiting in close() on the other core starved for
// seconds -- the UI froze and the Bluetooth link timed out (2026-10-14).
// The DMA buffers hold ~90 ms, so a tick costs nothing.
constexpr TickType_t kYield = 1;
constexpr gpio_num_t kMicClkPin = GPIO_NUM_45;
constexpr gpio_num_t kMicDataPin = GPIO_NUM_46;

struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }
  SemaphoreHandle_t m_;
};
}  // namespace

void MicRecorder::begin() {
  mutex_ = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCoreWithCaps(&MicRecorder::taskTrampoline, "mic", kTaskStackBytes, this,
                                  kTaskPriority, nullptr, kAudioCore, kStackCaps);
}

bool MicRecorder::listen() {
  // Before taking mutex_: claiming closes the previous owner's channel,
  // which takes that class's own lock.
  dac_.claim(*this);
  Lock lock(mutex_);
  return channel_ != nullptr || openChannel();
}

void MicRecorder::close() {
  stopRecording();
  Lock lock(mutex_);
  closeChannel();
}

bool MicRecorder::record(const std::string &path) {
  {
    Lock lock(mutex_);
    if (channel_ == nullptr) return false;
  }
  if (!writer_.start(path, dictaphone::kRecordingFormat)) return false;
  recordedSamples_.store(0);
  paused_.store(false);
  recording_.store(true);
  return true;
}

void MicRecorder::stopRecording() {
  paused_.store(false);
  if (!recording_.exchange(false)) return;
  writer_.finish();
}

bool MicRecorder::takeWriteFailure() {
  if (!writer_.takeFailure()) return false;
  recording_.store(false);
  return true;
}

size_t MicRecorder::readRecent(int16_t *dst, size_t maxSamples) {
  return audioOutputStage().readRecentSamples(dst, maxSamples, kSampleRate).count;
}

void MicRecorder::releaseDac() {
  stopRecording();
  Lock lock(mutex_);
  closeChannel();
}

// This board's microphone answers on the left PDM slot only; the right
// one reads a constant (spike, 2026-10-14). The S3's PDM-to-PCM filter
// gives 16-bit samples at kSampleRate.
bool MicRecorder::openChannel() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chan, nullptr, &channel_) != ESP_OK) {
    channel_ = nullptr;
    return false;
  }
  i2s_pdm_rx_config_t pdm = {};
  pdm.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRate);
  pdm.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  pdm.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
  pdm.gpio_cfg.clk = kMicClkPin;
  pdm.gpio_cfg.din = kMicDataPin;
  if (i2s_channel_init_pdm_rx_mode(channel_, &pdm) != ESP_OK ||
      i2s_channel_enable(channel_) != ESP_OK) {
    i2s_del_channel(channel_);
    channel_ = nullptr;
    return false;
  }
  conditioner_.reset();
  return true;
}

void MicRecorder::closeChannel() {
  if (channel_ == nullptr) return;
  i2s_channel_disable(channel_);
  i2s_del_channel(channel_);
  channel_ = nullptr;
}

void MicRecorder::taskTrampoline(void *self) { static_cast<MicRecorder *>(self)->taskLoop(); }

void MicRecorder::taskLoop() {
  auto &stage = audioOutputStage();
  for (;;) {
    size_t count = 0;
    {
      Lock lock(mutex_);
      if (channel_ != nullptr) {
        size_t bytes = 0;
        if (i2s_channel_read(channel_, raw_, sizeof(raw_), &bytes, kReadTimeout) == ESP_OK) {
          count = bytes / sizeof(int16_t);
          conditioner_.process(raw_, clean_, count);
        }
      }
    }
    if (count == 0) {
      vTaskDelay(kIdleDelay);
      continue;
    }
    for (size_t i = 0; i < count; ++i) stage.noteMonoSample(clean_[i]);
    if (recording_.load() && !paused_.load() && writer_.accepting()) {
      // A full ring (the card stalled for 4 s) drops the block rather than
      // waiting: the time keeps running, so the gap stays honest.
      writer_.ring().write(clean_, static_cast<uint32_t>(count));
      recordedSamples_.fetch_add(static_cast<uint32_t>(count));
    }
    vTaskDelay(kYield);
  }
}

}  // namespace drehklang::drivers
