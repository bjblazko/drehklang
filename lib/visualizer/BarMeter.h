#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace drehklang::visualizer::bars {

// The FL bar meter (ADR 0034): per channel, dBFS sample peaks on 32
// segments, as in the approved preview
// (docs/superpowers/specs/2026-11-03-vu-meter-preview.html).

constexpr int kSegments = 32;

// Scale points, dBFS -> position along the bar.
struct ScalePoint {
  float db, position;
};
constexpr ScalePoint kScale[] = {{-50, 0.0f}, {-30, 0.25f}, {-20, 0.42f}, {-10, 0.64f},
                                 {-6, 0.75f},  {-3, 0.85f},  {0, 1.0f}};

inline float scalePosition(float dbfs) {
  if (!(dbfs > kScale[0].db)) return 0.0f;  // Also -inf and NaN.
  for (size_t i = 1; i < sizeof(kScale) / sizeof(kScale[0]); ++i) {
    if (dbfs <= kScale[i].db) {
      const ScalePoint &a = kScale[i - 1], &b = kScale[i];
      return a.position + (b.position - a.position) * (dbfs - a.db) / (b.db - a.db);
    }
  }
  return 1.0f;
}

// The first red segment: from -3 dB up.
inline int redFromSegment() { return static_cast<int>(std::floor(scalePosition(-3) * kSegments)); }

// One channel: instant attack, a steady fall, and a peak held for 1.5 s
// before it falls too, never below the bar.
class BarMeter {
 public:
  static constexpr float kReleasePerS = 0.55f;
  static constexpr uint32_t kHoldMs = 1500;
  static constexpr float kHoldFallPerS = 0.4f;

  void update(float peakLinear, uint32_t dtMs) {
    const float dt = static_cast<float>(dtMs) / 1000.0f;
    const float p = peakLinear > 0.0f ? scalePosition(20.0f * std::log10(peakLinear)) : 0.0f;
    level_ = p >= level_ ? p : std::max(p, level_ - dt * kReleasePerS);
    if (p >= hold_) {
      hold_ = p;
      holdLeftMs_ = static_cast<int32_t>(kHoldMs);
    } else {
      holdLeftMs_ -= static_cast<int32_t>(dtMs);
      if (holdLeftMs_ <= 0) hold_ = std::max(level_, hold_ - dt * kHoldFallPerS);
    }
  }

  int litSegments() const { return static_cast<int>(std::lround(level_ * kSegments)); }

  // -1 when nothing is held.
  int holdSegment() const {
    if (hold_ <= 0.0f) return -1;
    return std::min(kSegments - 1, static_cast<int>(std::lround(hold_ * kSegments)) - 1);
  }

 private:
  float level_ = 0.0f;
  float hold_ = 0.0f;
  int32_t holdLeftMs_ = 0;
};

}  // namespace drehklang::visualizer::bars
