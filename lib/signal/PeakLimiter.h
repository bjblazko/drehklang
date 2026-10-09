#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace drehklang::signal {

// Gives the equalizer's headroom back after the volume, all of it (ADR
// 0029): the music the equalizer did not lift keeps exactly its level at
// every volume step. Where a lifted band would then pass full scale, the
// gain dips just enough for that peak and comes back over ~80 ms -- a
// peak limiter, so nothing ever clips.
//
// The first version gave back only as much as the volume step left room
// for, assuming full-scale music in the lifted band all the time; at a
// listening volume that made the rest of the music 4-8 dB quieter
// whenever a band was lifted (user, 2026-10-09).
//
// Interleaved stereo, both channels by the same gain so the image never
// shifts. Decode task only.
class PeakLimiter {
 public:
  // -0.18 dBFS: a little short of full scale, for the float rounding.
  static constexpr int32_t kCeiling = 2104533974;
  static constexpr float kReleaseSeconds = 0.08f;

  void process(int32_t *samples, size_t words, float gain, uint32_t sampleRate) {
    if (gain <= 1.0f && current_ >= 1.0f && current_ <= 1.0f) return;
    if (sampleRate == 0) return;
    const float release = 1.0f - std::exp(-1.0f / (kReleaseSeconds * sampleRate));
    for (size_t i = 0; i + 1 < words; i += 2) {
      // Back toward the full gain, gently; a lower target is taken at once.
      current_ = current_ < gain ? current_ + (gain - current_) * release : gain;
      const float peak = static_cast<float>(
          std::max(std::llabs(samples[i]), std::llabs(samples[i + 1])));
      if (peak * current_ > kCeiling) current_ = kCeiling / peak;
      samples[i] = scale(samples[i]);
      samples[i + 1] = scale(samples[i + 1]);
    }
  }

  float gain() const { return current_; }

 private:
  int32_t scale(int32_t sample) const {
    const float v = static_cast<float>(sample) * current_;
    if (v >= kCeiling) return kCeiling;
    if (v <= -kCeiling) return -kCeiling;
    return static_cast<int32_t>(v);
  }

  float current_ = 1.0f;
};

}  // namespace drehklang::signal
