#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace drehklang::signal {

// Per-channel levels of the music, linear, 1.0 = full scale (ADR 0034).
// `rms` is the latest decoder block's, `peak` the largest sample since
// the previous read. `fresh` says a block arrived since then.
struct StereoLevels {
  float rms[2] = {0, 0};
  float peak[2] = {0, 0};
  bool fresh = false;
};

// Measures the decoder's interleaved stereo on the audio task and hands
// the result to the main loop. One producer, one consumer, no lock: the
// values are floats kept as bits in atomics, so a reader racing a writer
// sees at worst one channel a block newer than the other.
class StereoLevelMeter {
 public:
  void noteBlock(const int32_t *interleaved, size_t words, float gain) {
    const size_t frames = words / 2;
    if (frames == 0) return;
    double sumSq[2] = {0, 0};
    float peak[2] = {0, 0};
    const double scale = static_cast<double>(gain) / 2147483648.0;
    for (size_t i = 0; i < frames; ++i) {
      for (int ch = 0; ch < 2; ++ch) {
        const double v = static_cast<double>(interleaved[2 * i + ch]) * scale;
        sumSq[ch] += v * v;
        const float a = static_cast<float>(std::fabs(v));
        if (a > peak[ch]) peak[ch] = a;
      }
    }
    for (int ch = 0; ch < 2; ++ch) {
      store(rms_[ch], static_cast<float>(std::sqrt(sumSq[ch] / static_cast<double>(frames))));
      raise(peak_[ch], peak[ch]);
    }
    blocks_.fetch_add(1, std::memory_order_release);
  }

  StereoLevels take() {
    StereoLevels out;
    const uint32_t blocks = blocks_.load(std::memory_order_acquire);
    out.fresh = blocks != lastBlocks_;
    lastBlocks_ = blocks;
    for (int ch = 0; ch < 2; ++ch) {
      out.rms[ch] = load(rms_[ch]);
      out.peak[ch] = toFloat(peak_[ch].exchange(0, std::memory_order_acq_rel));
    }
    if (!out.fresh) out.rms[0] = out.rms[1] = 0;
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
  static void store(std::atomic<uint32_t> &slot, float f) {
    slot.store(toBits(f), std::memory_order_relaxed);
  }
  static float load(const std::atomic<uint32_t> &slot) {
    return toFloat(slot.load(std::memory_order_relaxed));
  }
  static void raise(std::atomic<uint32_t> &slot, float f) {
    uint32_t seen = slot.load(std::memory_order_relaxed);
    while (f > toFloat(seen) &&
           !slot.compare_exchange_weak(seen, toBits(f), std::memory_order_relaxed)) {
    }
  }

  std::atomic<uint32_t> rms_[2] = {};
  std::atomic<uint32_t> peak_[2] = {};
  std::atomic<uint32_t> blocks_{0};
  uint32_t lastBlocks_ = 0;  // Consumer side only.
};

// Bridges the gaps between decoder blocks: they come every ~26 ms at
// 44.1 kHz but up to ~150 ms apart for a low-rate audiobook, while the
// meters draw at ~30 fps. A stale read repeats the last fresh levels until
// kStaleMs without one, then reads silence.
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
