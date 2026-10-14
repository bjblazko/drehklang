#pragma once

#include <FS.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "PcmRing.h"
#include "WavHeader.h"

namespace drehklang::drivers {

// Writes the dictaphone's samples to a WAV file on its own task (ADR 0032),
// so a slow SD write never holds up the microphone: the microphone's task
// fills ring(), this task drains it in 4 KB writes. The header is written
// with zero sizes first and patched when the recording is finished; a
// write that fails finishes it early and keeps what reached the card.
class SdWavWriter {
 public:
  // Allocates the ring (PSRAM) and starts the task. Call once.
  void begin();

  // Main loop: creates the file and its folder. False when the file could
  // not be created or a previous recording is still being finished.
  bool start(const std::string &path, const dictaphone::WavFormat &format);
  // Main loop: whatever is in the ring is still written, then the header
  // is patched and the file closed.
  void finish();

  // True from start() until the file is closed.
  bool busy() const { return state_.load() != State::Idle; }
  // True while new samples are wanted -- not once finishing has begun.
  bool accepting() const { return state_.load() == State::Recording; }
  // The microphone's side. Only written while accepting().
  btaudio::PcmRing &ring() { return ring_; }
  // True once after a write failed.
  bool takeFailure() { return failed_.exchange(false); }

 private:
  enum class State : uint8_t { Idle, Recording, Finishing };
  // 4 s at 16 kHz: room for the card's slowest moments (a FAT update can
  // take a few hundred ms).
  static constexpr uint32_t kRingSamples = 65536;
  static constexpr uint32_t kBlockSamples = 2048;  // 4 KB per write

  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);
  bool writeBlock(uint32_t count);
  void close();

  btaudio::PcmRing ring_;
  int16_t *block_ = nullptr;
  fs::File file_;
  dictaphone::WavFormat format_;
  uint32_t dataBytes_ = 0;
  std::atomic<State> state_{State::Idle};
  std::atomic<bool> failed_{false};
};

}  // namespace drehklang::drivers
