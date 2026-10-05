#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AdaptiveResampler.h"
#include "PcmRing.h"

namespace drehklang::btaudio {

// The U4WDH's side of the stream (ADR 0027): the link task pushes each
// AUDIO packet, and Bluetooth's data callback renders 44.1 kHz from it. A
// new source rate -- the next track, or the tone output taking the DAC --
// drops what is buffered and restarts the resampler at that rate.
class HeadphoneStream {
 public:
  HeadphoneStream(int16_t *storage, uint32_t capacity) : ring_(storage, capacity) {}

  // Link task: `samples` interleaved int16 little-endian samples at `rate`.
  // False when the ring had no room; the packet is then dropped.
  bool push(uint32_t rate, const uint8_t *pcm, uint32_t samples) {
    if (rate == 0) return false;
    rate_.store(rate, std::memory_order_relaxed);
    const bool written = ring_.write(samples, [pcm](uint32_t i) {
      return static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
    });
    if (!written) dropped_.fetch_add(1, std::memory_order_relaxed);
    return written;
  }

  // Bluetooth's data callback: `frames` stereo frames at 44.1 kHz.
  void render(int16_t *out, size_t frames) {
    const uint32_t rate = rate_.load(std::memory_order_relaxed);
    if (rate != resampler_.inputRate()) {
      ring_.clear();
      resampler_.reset(rate);
    }
    resampler_.render(ring_, out, frames);
  }

  uint32_t inputRate() const { return resampler_.inputRate(); }
  uint32_t underruns() const { return resampler_.underruns(); }
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  uint32_t buffered() const { return ring_.available(); }

 private:
  PcmRing ring_;
  AdaptiveResampler resampler_;
  std::atomic<uint32_t> rate_{AdaptiveResampler::kOutputRate};
  std::atomic<uint32_t> dropped_{0};
};

}  // namespace drehklang::btaudio
