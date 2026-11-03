#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace drehklang::visualizer::vu {

// The analog VU meter (ADR 0034): one needle for L+R, as in the approved
// preview (docs/superpowers/specs/2026-11-03-vu-meter-preview.html).

constexpr float kRefDbfs = -14.0f;           // 0 VU
constexpr float kFullScaleVu = 1.4125375f;   // +3 VU, 10^(3/20), the scale's end
constexpr float kPeakThreshold = 0.8912509f;  // -1 dBFS, 10^(-1/20)

inline float zeroVuLinear() { return std::pow(10.0f, kRefDbfs / 20.0f); }

// Needle position (0 = left stop, 1 = +3 VU) for the RMS of both channels:
// a VU meter reads voltage, so the channels are averaged linearly. Past
// the scale's end the mechanical stop holds the needle (NeedleBallistics).
inline float needleTarget(float rmsL, float rmsR) {
  const float position = (rmsL + rmsR) / 2.0f / zeroVuLinear() / kFullScaleVu;
  return std::clamp(position, 0.0f, 1.2f);
}

// A moving coil: second order, ~300 ms to 99 %, ~1.5 % overshoot, and
// stops at both ends that the needle bounces off a little.
class NeedleBallistics {
 public:
  static constexpr float kOmega = 20.0f;  // rad/s
  static constexpr float kZeta = 0.8f;
  static constexpr float kStop = 1.06f;

  void update(float target, uint32_t dtMs) {
    const uint32_t steps = std::min<uint32_t>(dtMs, 100);  // 1 ms substeps
    for (uint32_t i = 0; i < steps; ++i) {
      const float acc = kOmega * kOmega * (target - position_) - 2.0f * kZeta * kOmega * velocity_;
      velocity_ += acc * 0.001f;
      position_ += velocity_ * 0.001f;
      if (position_ < 0.0f) {
        position_ = 0.0f;
        velocity_ = -velocity_ * 0.15f;
      }
      if (position_ > kStop) {
        position_ = kStop;
        velocity_ = -velocity_ * 0.2f;
      }
    }
  }

  float position() const { return position_; }
  float velocity() const { return velocity_; }

 private:
  float position_ = 0.0f;
  float velocity_ = 0.0f;
};

// The red PEAK lamp: full while either channel's sample peak is above
// -1 dBFS, then fading out over 350 ms.
class PeakLamp {
 public:
  static constexpr uint32_t kFadeMs = 350;

  void update(float peakL, float peakR, uint32_t dtMs) {
    if (peakL > kPeakThreshold || peakR > kPeakThreshold) {
      brightness_ = 1.0f;
      return;
    }
    brightness_ = std::max(0.0f, brightness_ - static_cast<float>(dtMs) / kFadeMs);
  }

  float brightness() const { return brightness_; }

 private:
  float brightness_ = 0.0f;
};

// Needle geometry in screen pixels (360 x 360).
struct NeedlePoint {
  float x, y;
};
struct PixelArea {
  int16_t x1, y1, x2, y2;  // Inclusive.
};

constexpr float kPivotX = 180.0f;
constexpr float kPivotY = 318.0f;
constexpr float kTipRadius = 168.0f;
constexpr float kSpanRad = 48.0f * 3.14159265f / 180.0f;
constexpr float kBaseHalfWidth = 1.8f;
constexpr float kTipHalfWidth = 0.5f;
constexpr int kShadowDx = 3;
constexpr int kShadowDy = 5;

// Angle from vertical, clockwise.
inline float needleAngle(float position) { return -kSpanRad + 2.0f * kSpanRad * position; }

// A thin tapered needle: pivot-n, tip-n, tip+n, pivot+n.
inline std::array<NeedlePoint, 4> needlePolygon(float position) {
  const float a = needleAngle(position);
  const float nx = std::cos(a), ny = std::sin(a);
  const float tx = kPivotX + kTipRadius * std::sin(a);
  const float ty = kPivotY - kTipRadius * std::cos(a);
  return {{{kPivotX - nx * kBaseHalfWidth, kPivotY - ny * kBaseHalfWidth},
           {tx - nx * kTipHalfWidth, ty - ny * kTipHalfWidth},
           {tx + nx * kTipHalfWidth, ty + ny * kTipHalfWidth},
           {kPivotX + nx * kBaseHalfWidth, kPivotY + ny * kBaseHalfWidth}}};
}

// What to redraw for a needle at `position`: the needle, its shadow and
// a margin for anti-aliasing.
inline PixelArea needleArea(float position) {
  constexpr int kMargin = 3;
  const auto poly = needlePolygon(position);
  float x1 = poly[0].x, x2 = poly[0].x, y1 = poly[0].y, y2 = poly[0].y;
  for (const auto &p : poly) {
    x1 = std::min(x1, p.x);
    x2 = std::max(x2, p.x);
    y1 = std::min(y1, p.y);
    y2 = std::max(y2, p.y);
  }
  auto clampPx = [](float v) { return static_cast<int16_t>(std::clamp(v, 0.0f, 359.0f)); };
  return {clampPx(std::floor(x1) - kMargin), clampPx(std::floor(y1) - kMargin),
          clampPx(std::ceil(x2) + kShadowDx + kMargin),
          clampPx(std::ceil(y2) + kShadowDy + kMargin)};
}

}  // namespace drehklang::visualizer::vu
