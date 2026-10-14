#include "SdWavWriter.h"

#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include "RecordingShelf.h"

namespace drehklang::drivers {

namespace {
constexpr uint32_t kTaskStackBytes = 4096;
// The stack in PSRAM: the dictaphone's three tasks idle almost always, and
// internal RAM is what the music player's I2S buffers need (AGENTS.md).
constexpr UBaseType_t kStackCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
// Below the microphone's task (prio 2): a write may block for a while,
// the microphone must not.
constexpr UBaseType_t kTaskPriority = 1;
constexpr BaseType_t kCore = 0;
constexpr TickType_t kIdleDelay = pdMS_TO_TICKS(20);
}  // namespace

void SdWavWriter::begin() {
  auto *storage = static_cast<int16_t *>(
      heap_caps_malloc(kRingSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  ring_.attach(storage, kRingSamples);
  block_ = static_cast<int16_t *>(
      heap_caps_malloc(kBlockSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  xTaskCreatePinnedToCoreWithCaps(&SdWavWriter::taskTrampoline, "wavwriter", kTaskStackBytes, this,
                                  kTaskPriority, nullptr, kCore, kStackCaps);
}

bool SdWavWriter::start(const std::string &path, const dictaphone::WavFormat &format) {
  if (busy() || ring_.capacity() == 0 || block_ == nullptr) return false;
  // FATFS will not create a file in a folder that does not exist (AGENTS.md).
  SD_MMC.mkdir(dictaphone::kRecordingFolder);
  file_ = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file_) return false;
  uint8_t header[dictaphone::kWavHeaderBytes];
  dictaphone::writeWavHeader(header, format, 0);
  if (file_.write(header, sizeof(header)) != sizeof(header)) {
    file_.close();
    SD_MMC.remove(path.c_str());
    return false;
  }
  format_ = format;
  dataBytes_ = 0;
  ring_.clear();
  failed_.store(false);
  state_.store(State::Recording);
  return true;
}

void SdWavWriter::finish() {
  State expected = State::Recording;
  state_.compare_exchange_strong(expected, State::Finishing);
}

void SdWavWriter::taskTrampoline(void *self) { static_cast<SdWavWriter *>(self)->taskLoop(); }

void SdWavWriter::taskLoop() {
  for (;;) {
    const State state = state_.load();
    if (state == State::Idle) {
      vTaskDelay(kIdleDelay);
      continue;
    }
    const uint32_t waiting = ring_.available();
    if (waiting >= kBlockSamples || (state == State::Finishing && waiting > 0)) {
      if (!writeBlock(std::min(waiting, kBlockSamples))) {
        failed_.store(true);
        close();
      }
      continue;
    }
    if (state == State::Finishing) {
      close();
      continue;
    }
    vTaskDelay(kIdleDelay);
  }
}

bool SdWavWriter::writeBlock(uint32_t count) {
  const uint32_t got = ring_.read(block_, count);
  const size_t bytes = got * sizeof(int16_t);
  const size_t written = file_.write(reinterpret_cast<const uint8_t *>(block_), bytes);
  dataBytes_ += written - written % sizeof(int16_t);
  return written == bytes;
}

// Patches the header with what reached the card. Also after a failed
// write: whatever is there is still a playable recording.
void SdWavWriter::close() {
  uint8_t header[dictaphone::kWavHeaderBytes];
  dictaphone::writeWavHeader(header, format_, dataBytes_);
  if (file_.seek(0)) file_.write(header, sizeof(header));
  file_.close();
  state_.store(State::Idle);
}

}  // namespace drehklang::drivers
