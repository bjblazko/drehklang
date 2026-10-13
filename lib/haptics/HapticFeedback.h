#pragma once

#include <cstdint>

#include "KeyValueStore.h"

namespace drehklang::haptics {

// What the board's vibration motor says (2026-10-13). Three feels, chosen
// by hand on the device from the DRV2605L's LRA library:
enum class Feel : uint8_t {
  Tap,      // Anything a tap set off. "Sharp Tick 1" (24).
  Refusal,  // Something a tap could not do. "Double Click" (10).
  Lock,     // Locked or unlocked. "Soft Bump" (7).
  // In the games: a ball on the paddle, a touchdown, a bump. "Sharp
  // Click" (4).
  Hit,
  // In the games: a point, a crash. "Strong Click" (1).
  Crash,
};

inline uint8_t effectFor(Feel feel) {
  switch (feel) {
    case Feel::Tap:
      return 24;
    case Feel::Refusal:
      return 10;
    case Feel::Lock:
      return 7;
    case Feel::Hit:
      return 4;
    case Feel::Crash:
      return 1;
  }
  return 0;
}

// Plays one of the motor driver's library effects.
class HapticSink {
 public:
  virtual ~HapticSink() = default;
  virtual void play(uint8_t effect) = 0;
};

// The policy: Settings > Haptic feedback (on by default) and which feel
// wins. LVGL reports a click only after the click's handler ran, so the
// tap's tick would cut short the refusal that handler just played; a tap
// right after a stronger feel is dropped. Pure logic, host-tested.
class HapticFeedback {
 public:
  static constexpr uint32_t kTapHoldoffMs = 150;
  static constexpr char kEnabledKey[] = "haptic";

  explicit HapticFeedback(playback::KeyValueStore &store) : store_(store) {}

  void begin() {
    uint8_t stored = 1;
    if (store_.getU8(kEnabledKey, stored)) enabled_ = stored != 0;
  }
  void attach(HapticSink &sink) { sink_ = &sink; }

  bool enabled() const { return enabled_; }
  void setEnabled(bool enabled) {
    enabled_ = enabled;
    store_.setU8(kEnabledKey, enabled ? 1 : 0);
  }

  void feel(Feel feel, uint32_t nowMs) {
    if (!enabled_ || !sink_) return;
    if (feel == Feel::Tap && strongAtMs_ != 0 &&
        static_cast<int32_t>(nowMs - strongAtMs_) < static_cast<int32_t>(kTapHoldoffMs)) {
      return;
    }
    if (feel != Feel::Tap) strongAtMs_ = nowMs == 0 ? 1 : nowMs;
    sink_->play(effectFor(feel));
  }

 private:
  playback::KeyValueStore &store_;
  HapticSink *sink_ = nullptr;
  bool enabled_ = true;
  uint32_t strongAtMs_ = 0;
};

}  // namespace drehklang::haptics
