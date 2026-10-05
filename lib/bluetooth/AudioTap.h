#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "PcmRing.h"

namespace drehklang::bluetooth {

// Where the DAC's current owner -- the player or the tone output -- hands
// Bluetooth a copy of what it plays (ADR 0027). The jack keeps playing;
// this only copies. Both callers are audio tasks, so it never blocks:
// without headphones it returns at once, and a full ring drops the chunk.
class AudioTap {
 public:
  explicit AudioTap(btaudio::PcmRing &ring) : ring_(ring) {}

  // Main loop: whether headphones are connected.
  void setForwarding(bool on) { forwarding_.store(on, std::memory_order_relaxed); }
  bool forwarding() const { return forwarding_.load(std::memory_order_relaxed); }

  // Above this the link cannot keep up: 3 Mbaud carries ~74 kHz of stereo.
  static constexpr uint32_t kMaxLinkRate = 48000;

  // ESP32-audioI2S's samples after the volume: interleaved, full-scale
  // 32-bit. `words` counts samples, not frames. 88.2 and 96 kHz go out at
  // half the rate, each pair of frames averaged -- a crude low-pass, and
  // enough for headphones that get SBC at 44.1 kHz in the end.
  void pushStereo32(const int32_t *samples, size_t words, uint32_t rate) {
    if (!forwarding()) return;
    if (rate > kMaxLinkRate) {
      pushHalved(samples, words, rate / 2);
      return;
    }
    rate_.store(rate, std::memory_order_relaxed);
    note(ring_.write(static_cast<uint32_t>(words), [samples](uint32_t i) {
      return static_cast<int16_t>(samples[i] >> 16);
    }));
  }

  // The tone output's chunks, already 16-bit.
  void pushStereo16(const int16_t *samples, size_t words, uint32_t rate) {
    if (!forwarding()) return;
    rate_.store(rate, std::memory_order_relaxed);
    note(ring_.write(samples, static_cast<uint32_t>(words)));
  }

  // The rate of what was pushed last; the link puts it in every packet.
  uint32_t rate() const { return rate_.load(std::memory_order_relaxed); }
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  // Frames 2n and 2n + 1 become one: sample i of the output is the mean
  // of the same channel in both.
  void pushHalved(const int32_t *samples, size_t words, uint32_t rate) {
    rate_.store(rate, std::memory_order_relaxed);
    const auto out = static_cast<uint32_t>(words / 4 * 2);
    note(ring_.write(out, [samples](uint32_t i) {
      const size_t first = (i / 2) * 4 + i % 2;
      const int64_t sum = static_cast<int64_t>(samples[first]) + samples[first + 2];
      return static_cast<int16_t>(sum >> 17);
    }));
  }

  void note(bool written) {
    if (!written) dropped_.fetch_add(1, std::memory_order_relaxed);
  }

  btaudio::PcmRing &ring_;
  std::atomic<bool> forwarding_{false};
  std::atomic<uint32_t> rate_{44100};
  std::atomic<uint32_t> dropped_{0};
};

}  // namespace drehklang::bluetooth
