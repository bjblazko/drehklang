#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "PcmRing.h"

namespace drehklang::btaudio {

// Turns the S3's stream -- at its source's rate, clocked by the S3's DAC --
// into 44.1 kHz stereo at the pace Bluetooth asks for it (ADR 0027).
//
// Linear interpolation between neighbouring frames. The step is trimmed by
// how full the ring is: above half, a little faster; below, a little
// slower. That absorbs the drift between the two clocks without dropping
// or repeating frames. A ring that runs dry plays silence until it is half
// full again, so a stall is one clean gap rather than a crackle.
//
// No low-pass before downsampling: content above 22 kHz in a 96 kHz file
// aliases. Music has little energy there, so this stays simple.
class AdaptiveResampler {
 public:
  static constexpr uint32_t kOutputRate = 44100;
  // Fill error (relative to half the ring) that trims nothing: one 256-frame
  // packet is ~6 % of the U4WDH's 8192-frame ring, and each arrival must not
  // wobble the pitch.
  static constexpr float kDeadband = 0.1f;
  static constexpr float kGain = 0.01f;
  // 2000 ppm: ten times the worst crystal pair.
  static constexpr float kMaxTrim = 0.002f;
  static constexpr float kSmoothing = 0.01f;

  void reset(uint32_t inputRate) {
    inputRate_ = inputRate;
    baseStep_ = static_cast<float>(inputRate) / kOutputRate;
    phase_ = 0.0f;
    primed_ = false;
    playing_ = false;
    fillError_ = 0.0f;
    trim_ = 0.0f;
  }

  uint32_t inputRate() const { return inputRate_; }
  float step() const { return baseStep_ * (1.0f + trim_); }
  bool playing() const { return playing_; }
  uint32_t underruns() const { return underruns_; }

  // Fills `frames` interleaved stereo frames into `out`.
  void render(PcmRing &ring, int16_t *out, size_t frames) {
    const uint32_t half = ring.capacity() / 2;
    if (!playing_) {
      if (ring.available() < half) {
        silence(out, frames);
        return;
      }
      playing_ = true;
    }
    updateTrim(ring.available(), half);
    const float stepNow = step();
    for (size_t i = 0; i < frames; ++i) {
      if (!advance(ring)) {
        ++underruns_;
        playing_ = false;
        primed_ = false;
        phase_ = 0.0f;
        silence(out + i * 2, frames - i);
        return;
      }
      out[i * 2] = lerp(cur_[0], next_[0]);
      out[i * 2 + 1] = lerp(cur_[1], next_[1]);
      phase_ += stepNow;
    }
  }

 private:
  // Makes cur_/next_ the frames either side of phase_. False when the ring
  // has run dry.
  bool advance(PcmRing &ring) {
    if (!primed_) {
      if (!readFrame(ring, cur_) || !readFrame(ring, next_)) return false;
      primed_ = true;
      phase_ = 0.0f;
    }
    while (phase_ >= 1.0f) {
      cur_[0] = next_[0];
      cur_[1] = next_[1];
      if (!readFrame(ring, next_)) return false;
      phase_ -= 1.0f;
    }
    return true;
  }

  static bool readFrame(PcmRing &ring, int16_t *frame) { return ring.read(frame, 2) == 2; }

  int16_t lerp(int16_t a, int16_t b) const {
    const float v = a + (b - a) * phase_;
    return static_cast<int16_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
  }

  void updateTrim(uint32_t available, uint32_t half) {
    const float error = (static_cast<float>(available) - half) / half;
    fillError_ += (error - fillError_) * kSmoothing;
    const float excess = std::fabs(fillError_) - kDeadband;
    float trim = excess > 0.0f ? std::copysign(excess * kGain, fillError_) : 0.0f;
    if (trim > kMaxTrim) trim = kMaxTrim;
    if (trim < -kMaxTrim) trim = -kMaxTrim;
    trim_ = trim;
  }

  static void silence(int16_t *out, size_t frames) {
    for (size_t i = 0; i < frames * 2; ++i) out[i] = 0;
  }

  uint32_t inputRate_ = kOutputRate;
  float baseStep_ = 1.0f;
  float phase_ = 0.0f;
  float fillError_ = 0.0f;
  float trim_ = 0.0f;
  int16_t cur_[2] = {};
  int16_t next_[2] = {};
  bool primed_ = false;
  bool playing_ = false;
  uint32_t underruns_ = 0;
};

}  // namespace drehklang::btaudio
