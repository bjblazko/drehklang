#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "CircuitRecords.h"
#include "CircuitScene.h"
#include "CircuitTracks.h"

namespace drehklang::games {

// What Circuit writes over the picture (ADR 0030): the time, lap, damage
// and speed in the sky and beside the car, and each phase's few words.
// Drawn by the renderer in its own pixel font rather than as LVGL labels
// -- the panel belongs to the game while it runs, and Montserrat would
// announce a modern UI.
//
// Placed for the round screen: nothing wide near the top or bottom edge,
// where the circle is narrow (ux-guidelines §7).
class CircuitHud {
 public:
  static constexpr uint16_t kWhite = 0xFFFF;
  static constexpr uint16_t kYellow = 0xFF08;
  static constexpr uint16_t kRed = 0xFA08;
  static constexpr uint16_t kGreen = 0x6E8C;
  static constexpr uint16_t kDim = 0x4209;
  static constexpr uint16_t kMid = 0x8410;

  struct Text {
    int16_t centerX;
    int16_t top;
    uint8_t scale;
    uint16_t color;
    char text[22];
  };
  struct Rect {
    int16_t x, y, w, h;
    uint16_t color;
  };

  static constexpr int kMaxTexts = 10;
  static constexpr int kMaxRects = 16;

  void layout(const CircuitScene &scene) {
    textCount_ = rectCount_ = 0;
    switch (scene.phase) {
      case CircuitGame::Phase::Select:
        layoutSelect(scene);
        break;
      case CircuitGame::Phase::Countdown:
        layoutGauges(scene);
        layoutCountdown(scene);
        break;
      case CircuitGame::Phase::Race:
        layoutGauges(scene);
        layoutRace(scene);
        break;
      case CircuitGame::Phase::Over:
        layoutOver(scene);
        break;
    }
  }

  int textCount() const { return textCount_; }
  const Text &text(int i) const { return texts_[i]; }
  int rectCount() const { return rectCount_; }
  const Rect &rect(int i) const { return rects_[i]; }

 private:
  static bool blink(const CircuitScene &scene) { return (scene.frame / 8) % 2 == 0; }

  // All of it in the sky: the start gantry fills the road below.
  void layoutSelect(const CircuitScene &scene) {
    add(180, 46, 4, kWhite, kTracks[scene.trackIndex].name);
    char lap[12];
    CircuitRecords::format(scene.recordMs, lap, sizeof(lap));
    addf(180, 82, 2, kYellow, "BEST %s", lap);
    add(180, 104, 2, kWhite, "< TURN TO CHOOSE >");
    if (blink(scene)) add(180, 126, 2, kWhite, "TAP TO START");
    add(180, 148, 1, kWhite, "SWIPE RIGHT TO LEAVE");
  }

  void layoutCountdown(const CircuitScene &scene) {
    const uint32_t left = 3 - scene.countdownMs / 1000;
    addf(180, 104, 8, kYellow, "%u", static_cast<unsigned>(left > 0 ? left : 1));
    add(180, 196, 2, kWhite, "TURN TO STEER");
    add(180, 216, 2, kWhite, "TOUCH TO BRAKE");
  }

  void layoutRace(const CircuitScene &scene) {
    if (scene.countdownMs < CircuitGame::kCountdownMs + 800) {
      add(180, 112, 6, kYellow, "GO!");
    } else if (scene.sinceBonusMs < 1600 && blink(scene)) {
      add(180, 112, 2, kYellow, "EXTENDED TIME");
    }
  }

  void layoutOver(const CircuitScene &scene) {
    const bool wrecked = scene.endReason == CircuitGame::EndReason::Wrecked;
    add(180, 96, 4, wrecked ? kRed : kYellow, wrecked ? "WRECKED" : "TIME UP");
    addf(180, 140, 2, kWhite, "LAPS %d", scene.lap - 1);
    char lap[12];
    CircuitRecords::format(scene.bestLapMs, lap, sizeof(lap));
    addf(180, 162, 2, kWhite, "BEST %s", lap);
    if (scene.newRecord && blink(scene)) add(180, 184, 2, kYellow, "NEW RECORD!");
    add(180, 214, 2, kWhite, "TAP TO GO ON");
  }

  // Time large at the top, lap below it, damage as a bar above them;
  // speed and the wheel beside the car.
  void layoutGauges(const CircuitScene &scene) {
    const uint32_t seconds = (scene.timeLeftMs + 999) / 1000;
    uint16_t timeColour = kWhite;
    if (seconds <= 10) timeColour = kYellow;
    if (seconds <= 5 && !blink(scene)) timeColour = kRed;
    addf(180, 40, 4, timeColour, "%u", static_cast<unsigned>(seconds));
    char lap[12];
    CircuitRecords::format(scene.lapTimeMs, lap, sizeof(lap));
    addf(180, 74, 2, kWhite, "LAP %d %s", scene.lap, lap);

    layoutDamage(scene.damage);
    addf(88, 300, 2, kWhite, "%d",
         static_cast<int>(scene.speed * kTopKmh / CircuitGame::kTopSpeed));
    add(88, 318, 1, kWhite, "KM/H");
    layoutWheel(scene.steering);
  }

  void layoutDamage(int damage) {
    constexpr int kSegments = 10;
    constexpr int kSegmentW = 10;
    constexpr int kGap = 2;
    const int left = 180 - (kSegments * (kSegmentW + kGap) - kGap) / 2;
    const int worn = (damage + 9) / 10;
    for (int i = 0; i < kSegments; ++i) {
      uint16_t colour = kDim;
      if (i < worn) colour = i < 5 ? kGreen : (i < 8 ? kYellow : kRed);
      addRect(left + i * (kSegmentW + kGap), 26, kSegmentW, 6, colour);
    }
  }

  // A strip with the dead zone shaded and a marker where the wheel is:
  // the knob has no centre you can feel, so the screen shows it.
  void layoutWheel(int steering) {
    constexpr int kPerDetent = 2;
    constexpr int kCentre = 274;
    const int half = CircuitGame::kMaxSteering * kPerDetent;
    addRect(kCentre - half, 308, 2 * half + 2, 6, kDim);
    addRect(kCentre - CircuitGame::kDeadZone * kPerDetent, 308,
            2 * CircuitGame::kDeadZone * kPerDetent + 2, 6, kMid);
    addRect(kCentre + steering * kPerDetent, 304, 2, 14, kWhite);
  }

  void add(int centerX, int top, int scale, uint16_t colour, const char *text) {
    if (textCount_ >= kMaxTexts) return;
    Text &t = texts_[textCount_++];
    t.centerX = static_cast<int16_t>(centerX);
    t.top = static_cast<int16_t>(top);
    t.scale = static_cast<uint8_t>(scale);
    t.color = colour;
    std::snprintf(t.text, sizeof(t.text), "%s", text);
  }

  template <typename... Args>
  void addf(int centerX, int top, int scale, uint16_t colour, const char *format, Args... args) {
    char text[22];
    std::snprintf(text, sizeof(text), format, args...);
    add(centerX, top, scale, colour, text);
  }

  void addRect(int x, int y, int w, int h, uint16_t colour) {
    if (rectCount_ >= kMaxRects) return;
    rects_[rectCount_++] = Rect{static_cast<int16_t>(x), static_cast<int16_t>(y),
                                static_cast<int16_t>(w), static_cast<int16_t>(h), colour};
  }

  static constexpr int32_t kTopKmh = 300;

  Text texts_[kMaxTexts];
  Rect rects_[kMaxRects];
  int textCount_ = 0;
  int rectCount_ = 0;
};

}  // namespace drehklang::games
