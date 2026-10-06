#pragma once

#include <cstdint>

#include "KeyValueStore.h"

namespace drehklang::display {

struct Point {
  int16_t x;
  int16_t y;
};

// How far the picture is turned clockwise, in quarter turns (Settings >
// Rotation). Set with the knob, applied live, and persisted like
// brightness: once it has settled, not per detent. The panel turns the
// picture itself (its MADCTL register); touch comes in in the panel's own
// frame and is turned to match with toLogical(). Pure logic over an
// explicit clock.
class RotationSetting {
 public:
  static constexpr char kKey[] = "rotation";
  static constexpr uint32_t kSaveDebounceMs = 1000;

  explicit RotationSetting(playback::KeyValueStore &store) : store_(store) {}

  // Loads the stored turn; anything out of range is upright.
  void begin() {
    uint8_t stored = 0;
    turns_ = store_.getU8(kKey, stored) && stored < 4 ? stored : 0;
    savedTurns_ = turns_;
  }

  void adjust(int delta, uint32_t nowMs) {
    turns_ = static_cast<uint8_t>(((turns_ + delta) % 4 + 4) % 4);
    lastChangeMs_ = nowMs;
  }

  // Call every loop(); persists the turn once unchanged for
  // kSaveDebounceMs, and only if it differs from what is stored.
  void tick(uint32_t nowMs) {
    if (turns_ == savedTurns_ || nowMs - lastChangeMs_ < kSaveDebounceMs) return;
    store_.setU8(kKey, turns_);
    savedTurns_ = turns_;
  }

  uint8_t quarterTurns() const { return turns_; }
  int clockwiseDegrees() const { return turns_ * 90; }

  // A point in the panel's own frame, as the user sees it with the
  // picture turned `turns` quarter turns clockwise on a `size`-square
  // panel.
  static Point toLogical(Point panel, uint8_t turns, int16_t size) {
    const int16_t last = static_cast<int16_t>(size - 1);
    switch (turns & 3) {
      case 1:
        return {panel.y, static_cast<int16_t>(last - panel.x)};
      case 2:
        return {static_cast<int16_t>(last - panel.x), static_cast<int16_t>(last - panel.y)};
      case 3:
        return {static_cast<int16_t>(last - panel.y), panel.x};
      default:
        return panel;
    }
  }

 private:
  playback::KeyValueStore &store_;
  uint8_t turns_ = 0;
  uint8_t savedTurns_ = 0;
  uint32_t lastChangeMs_ = 0;
};

}  // namespace drehklang::display
