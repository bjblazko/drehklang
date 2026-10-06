#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace drehklang::signal {

// A seven-band graphic equalizer for the music (ADR 0029), run on the
// decoder's samples before the volume: interleaved stereo, full-scale
// 32-bit.
//
// - Bells from 31 Hz to 8 kHz, about 1.6 octaves apart (Q 0.9), and a
//   high shelf for the top band: a bell that close to half the sample rate
//   is squeezed out of shape, and "air" is a shelf anyway. The bottom is a
//   bell, not a shelf, so nothing below hearing is lifted.
// - Never clips: everything is lowered by the curve's highest point,
//   found on a fine grid (headroom()), and given back after the volume as
//   far as the volume leaves room (makeup()). At a listening volume the
//   music keeps its level; only near full volume does a lift lower the
//   rest, because there is no room left.
// - A band at or above 0.45 x the sample rate is left out (a 22.05 kHz
//   audiobook has no 16 kHz).
// - Flat costs nothing: the samples are not touched at all.
//
// Gains are set from the UI thread and picked up by process() on the
// audio task, which works out the filters itself when they or the rate
// changed -- no lock on the hot path.
class GraphicEqualizer {
 public:
  static constexpr size_t kBands = 7;
  static constexpr std::array<float, kBands> kFrequenciesHz = {31.0f,   100.0f,  300.0f, 1000.0f,
                                                               3000.0f, 8000.0f, 16000.0f};
  static constexpr int kMaxGainDb = 12;

  void setGain(size_t band, int db) {
    if (band >= kBands) return;
    gains_[band].store(static_cast<int8_t>(std::clamp(db, -kMaxGainDb, kMaxGainDb)),
                       std::memory_order_relaxed);
    version_.fetch_add(1, std::memory_order_release);
  }

  int gain(size_t band) const {
    return band < kBands ? gains_[band].load(std::memory_order_relaxed) : 0;
  }

  bool flat() const {
    for (const auto &g : gains_) {
      if (g.load(std::memory_order_relaxed) != 0) return false;
    }
    return true;
  }

  // How far process() lowered everything, as a linear factor >= 1: the
  // curve's highest point. Read from any task; set by process().
  float headroom() const { return headroom_.load(std::memory_order_relaxed); }

  // What to multiply by after a volume of `volumeLinear` (0..1): the
  // headroom back, but never past what keeps full scale at full scale.
  float makeup(float volumeLinear) const {
    if (volumeLinear <= 0.0f) return 1.0f;
    return std::max(1.0f, std::min(headroom(), 1.0f / volumeLinear));
  }

  // `words` interleaved left/right samples, in place, at `sampleRate`.
  void process(int32_t *samples, size_t words, uint32_t sampleRate) {
    const uint32_t version = version_.load(std::memory_order_acquire);
    if (version != preparedVersion_ || sampleRate != preparedRate_) {
      prepare(version, sampleRate);
    }
    if (active_ == 0) return;
    words &= ~static_cast<size_t>(1);
    for (size_t done = 0; done < words; done += kBlockWords) {
      const size_t n = std::min(kBlockWords, words - done);
      processBlock(samples + done, n);
    }
  }

 private:
  static constexpr float kBellQ = 0.9f;
  static constexpr float kHighestUsable = 0.45f;  // Of the sample rate.
  static constexpr size_t kGridPoints = 256;
  // Worked through a block at a time, one band after another, with each
  // band's coefficients and history held in registers: 12% of a core at
  // 44.1 kHz with every band on took one band per sample (measured
  // 2026-10-06).
  static constexpr size_t kBlockWords = 256;

  struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    // Per channel: x[n-1], x[n-2], y[n-1], y[n-2].
    std::array<std::array<float, 4>, 2> state{};

    // Runs over an interleaved stereo block, in place. Both channels in
    // one loop: each output waits on the one before it, and two
    // independent chains side by side hide that wait.
    void run(float *x, size_t frames) {
      float lx1 = state[0][0], lx2 = state[0][1], ly1 = state[0][2], ly2 = state[0][3];
      float rx1 = state[1][0], rx2 = state[1][1], ry1 = state[1][2], ry2 = state[1][3];
      const float c0 = b0, c1 = b1, c2 = b2, d1 = a1, d2 = a2;
      for (size_t i = 0; i < frames; ++i) {
        const float l = x[2 * i];
        const float r = x[2 * i + 1];
        const float ly = c0 * l + c1 * lx1 + c2 * lx2 - d1 * ly1 - d2 * ly2;
        const float ry = c0 * r + c1 * rx1 + c2 * rx2 - d1 * ry1 - d2 * ry2;
        lx2 = lx1;
        lx1 = l;
        ly2 = ly1;
        ly1 = ly;
        rx2 = rx1;
        rx1 = r;
        ry2 = ry1;
        ry1 = ry;
        x[2 * i] = ly;
        x[2 * i + 1] = ry;
      }
      state[0] = {lx1, lx2, ly1, ly2};
      state[1] = {rx1, rx2, ry1, ry2};
    }

    // |H|^2 at a frequency, given phi = sin^2(w/2) there. The RBJ
    // cookbook's factored form: the expanded one in cos(w) cancels to
    // rounding noise near DC in float, and put +12 dB at 31 Hz at +21.6
    // (found 2026-10-06, the music came out far too quiet).
    float magnitudeSquared(float phi) const {
      const float bs = b0 + b1 + b2;
      const float as = 1.0f + a1 + a2;
      const float num =
          bs * bs - 4.0f * (b0 * b1 + 4.0f * b0 * b2 + b1 * b2) * phi + 16.0f * b0 * b2 * phi * phi;
      const float den =
          as * as - 4.0f * (a1 + 4.0f * a2 + a1 * a2) * phi + 16.0f * a2 * phi * phi;
      return num / den;
    }
  };

  void processBlock(int32_t *samples, size_t words) {
    constexpr float kIn = 1.0f / 2147483648.0f;
    // A member, not on the stack: the decode task's stack is the library's.
    auto &block = block_;
    const float in = kIn * preGain_;
    for (size_t i = 0; i < words; ++i) block[i] = static_cast<float>(samples[i]) * in;
    const size_t frames = words / 2;
    for (size_t i = 0; i < active_; ++i) {
      filters_[order_[i]].run(block.data(), frames);
    }
    for (size_t i = 0; i < words; ++i) samples[i] = toSample(block[i]);
  }

  // RBJ audio EQ cookbook, normalised by a0.
  static Biquad bell(float hz, float rate, int db) {
    const float a = std::pow(10.0f, static_cast<float>(db) / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * hz / rate;
    const float alpha = std::sin(w0) / (2.0f * kBellQ);
    const float c = std::cos(w0);
    const float a0 = 1.0f + alpha / a;
    Biquad f;
    f.b0 = (1.0f + alpha * a) / a0;
    f.b1 = -2.0f * c / a0;
    f.b2 = (1.0f - alpha * a) / a0;
    f.a1 = -2.0f * c / a0;
    f.a2 = (1.0f - alpha / a) / a0;
    return f;
  }

  // Shelf slope 1: as steep as a shelf gets without overshoot.
  static Biquad highShelf(float hz, float rate, int db) {
    const float a = std::pow(10.0f, static_cast<float>(db) / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * hz / rate;
    const float c = std::cos(w0);
    const float k = 2.0f * std::sqrt(a) * std::sin(w0) / 2.0f * std::sqrt(2.0f);
    const float a0 = (a + 1.0f) - (a - 1.0f) * c + k;
    Biquad f;
    f.b0 = a * ((a + 1.0f) + (a - 1.0f) * c + k) / a0;
    f.b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * c) / a0;
    f.b2 = a * ((a + 1.0f) + (a - 1.0f) * c - k) / a0;
    f.a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * c) / a0;
    f.a2 = ((a + 1.0f) - (a - 1.0f) * c - k) / a0;
    return f;
  }

  void prepare(uint32_t version, uint32_t sampleRate) {
    std::array<bool, kBands> wasRunning{};
    for (size_t i = 0; i < active_; ++i) wasRunning[order_[i]] = true;
    preparedVersion_ = version;
    preparedRate_ = sampleRate;
    active_ = 0;
    const auto rate = static_cast<float>(sampleRate);
    for (size_t b = 0; b < kBands && sampleRate != 0; ++b) {
      const int db = gains_[b].load(std::memory_order_relaxed);
      const float hz = kFrequenciesHz[b];
      if (db == 0 || hz >= kHighestUsable * rate) continue;
      Biquad next = b + 1 == kBands ? highShelf(hz, rate, db) : bell(hz, rate, db);
      // A band that was already running keeps its history, so a knob turn
      // does not click; one starting fresh starts from silence.
      if (wasRunning[b]) next.state = filters_[b].state;
      filters_[b] = next;
      order_[active_++] = b;
    }
    const float peak = active_ == 0 ? 1.0f : std::max(1.0f, peakMagnitude(rate));
    preGain_ = 1.0f / peak;
    headroom_.store(peak, std::memory_order_relaxed);
  }

  // The curve's highest point: a log grid from 20 Hz, plus the band
  // centres themselves, where the peaks are.
  float peakMagnitude(float rate) const {
    const float top = std::min(20000.0f, kHighestUsable * rate);
    float peak = 0.0f;
    auto at = [&](float hz) {
      const float half = std::sin(static_cast<float>(M_PI) * hz / rate);
      const float phi = half * half;
      float m = 1.0f;
      for (size_t i = 0; i < active_; ++i) m *= filters_[order_[i]].magnitudeSquared(phi);
      peak = std::max(peak, m);
    };
    for (size_t i = 0; i < kGridPoints; ++i) {
      at(20.0f * std::pow(top / 20.0f, static_cast<float>(i) / (kGridPoints - 1)));
    }
    for (float hz : kFrequenciesHz) {
      if (hz < top) at(hz);
    }
    // A hair of margin for peaks between grid points.
    return std::sqrt(peak) * 1.01f;
  }

  static int32_t toSample(float x) {
    const float scaled = x * 2147483648.0f;
    if (scaled >= 2147483647.0f) return INT32_MAX;
    if (scaled <= -2147483648.0f) return INT32_MIN;
    return static_cast<int32_t>(scaled);
  }

  std::array<std::atomic<int8_t>, kBands> gains_{};
  std::atomic<uint32_t> version_{0};
  // The audio task's own: what process() last worked out.
  uint32_t preparedVersion_ = 0;
  uint32_t preparedRate_ = 0;
  // By band; order_ lists the active_ bands that run.
  std::array<Biquad, kBands> filters_{};
  std::array<size_t, kBands> order_{};
  size_t active_ = 0;
  float preGain_ = 1.0f;
  std::atomic<float> headroom_{1.0f};
  std::array<float, kBlockWords> block_{};
};

}  // namespace drehklang::signal
