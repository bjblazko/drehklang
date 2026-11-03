#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace drehklang::signal {

// Per-channel levels of the music, linear, 1.0 = full scale (ADR 0034).
// `rms` and `peak` cover everything since the previous read. `fresh`
// says a block arrived since then.
struct StereoLevels {
  float rms[2] = {0, 0};
  float peak[2] = {0, 0};
  bool fresh = false;
};

// Measures the decoder's interleaved stereo on the audio task and hands
// the result to the main loop. One producer, one consumer, no lock: the
// sums are floats kept as bits in atomics and added once per block, so a
// reader racing a writer sees at worst one block counted in the next read.
//
// Per sample there is only integer work (core 0 has a single-precision
// FPU and no time to spare, ADR 0031): the top 24 bits squared into a
// 64-bit sum, and the largest magnitude. The RMS covers every block since
// the last read. Unread for kIdleBlocks blocks, the meter stops measuring
// altogether -- nothing shows it -- and the next read throws away what it
// had, so an old peak never shows as new.
class StereoLevelMeter {
 public:
  // Decoder blocks are 256 frames: ~370 ms at 44.1 kHz, ~340 ms at 96 kHz.
  static constexpr uint32_t kIdleBlocks = 128;

  void noteBlock(const int32_t *interleaved, size_t words, float gain) {
    if (idleBlocks_.load(std::memory_order_relaxed) >= kIdleBlocks) return;
    idleBlocks_.fetch_add(1, std::memory_order_relaxed);
    const size_t frames = words / 2;
    if (frames == 0) return;
    int64_t sumSq[2] = {0, 0};
    uint32_t most[2] = {0, 0};
    for (size_t i = 0; i < frames; ++i) {
      for (int ch = 0; ch < 2; ++ch) {
        const int32_t v = interleaved[2 * i + ch] >> 8;  // 24 bits: the square fits 2^46.
        sumSq[ch] += static_cast<int64_t>(v) * v;
        const uint32_t magnitude = static_cast<uint32_t>(v < 0 ? -v : v);
        if (magnitude > most[ch]) most[ch] = magnitude;
      }
    }
    constexpr float kSquareScale = 1.0f / 70368744177664.0f;  // 2^-46
    constexpr float kScale = 1.0f / 8388608.0f;                // 2^-23
    for (int ch = 0; ch < 2; ++ch) {
      add(sumSq_[ch], static_cast<float>(sumSq[ch]) * kSquareScale * gain * gain);
      raise(peak_[ch], static_cast<float>(most[ch]) * kScale * gain);
    }
    frames_.fetch_add(static_cast<uint32_t>(frames), std::memory_order_release);
  }

  StereoLevels take() {
    const bool stopped = idleBlocks_.load(std::memory_order_relaxed) >= kIdleBlocks;
    const uint32_t frames = frames_.exchange(0, std::memory_order_acquire);
    StereoLevels out;
    for (int ch = 0; ch < 2; ++ch) {
      const float sumSq = toFloat(sumSq_[ch].exchange(0, std::memory_order_relaxed));
      const float peak = toFloat(peak_[ch].exchange(0, std::memory_order_relaxed));
      if (stopped || frames == 0) continue;
      out.rms[ch] = std::sqrt(sumSq / static_cast<float>(frames));
      out.peak[ch] = peak;
    }
    out.fresh = !stopped && frames != 0;
    idleBlocks_.store(0, std::memory_order_relaxed);
    return out;
  }

 private:
  static uint32_t toBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
  }
  static float toFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
  }
  static void add(std::atomic<uint32_t> &slot, float f) {
    uint32_t seen = slot.load(std::memory_order_relaxed);
    while (!slot.compare_exchange_weak(seen, toBits(toFloat(seen) + f),
                                       std::memory_order_relaxed)) {
    }
  }
  static void raise(std::atomic<uint32_t> &slot, float f) {
    uint32_t seen = slot.load(std::memory_order_relaxed);
    while (f > toFloat(seen) &&
           !slot.compare_exchange_weak(seen, toBits(f), std::memory_order_relaxed)) {
    }
  }

  std::atomic<uint32_t> sumSq_[2] = {};
  std::atomic<uint32_t> peak_[2] = {};
  std::atomic<uint32_t> frames_{0};
  std::atomic<uint32_t> idleBlocks_{0};
};

// Bridges frames with no decoder block in them: blocks are 256 frames,
// 5.8 ms at 44.1 kHz but 32 ms at 8 kHz, about one per meter frame, and
// the decoder delivers them in bursts. A stale read repeats the last
// fresh levels until kStaleMs without one, then reads silence.
class LevelHold {
 public:
  static constexpr uint32_t kStaleMs = 250;

  StereoLevels apply(const StereoLevels &in, uint32_t dtMs) {
    if (in.fresh) {
      last_ = in;
      staleMs_ = 0;
      return in;
    }
    staleMs_ += dtMs;
    if (staleMs_ > kStaleMs) last_ = StereoLevels{};
    StereoLevels out = last_;
    out.fresh = false;
    return out;
  }

 private:
  StereoLevels last_;
  uint32_t staleMs_ = 0;
};

}  // namespace drehklang::signal
