#pragma once

#include <cstdint>

namespace drehklang::navigation {

// Which main-menu destinations the user has chosen to show (ADR 0018) --
// one bit per entry in the menu table, 1 = shown.
//
// Pure logic, no storage and no LVGL: ScreenManager reads and writes the
// mask as two NVS bytes (one until the ninth entry, ADR 0032), and this
// decides what the mask means. That
// keeps the two rules worth getting right -- an entry that may never be
// hidden, and never ending up with an empty menu -- host-testable.
class MenuVisibility {
 public:
  static constexpr int kMaxEntries = 16;
  // 1: an entry hidden by default was added (Video, 2026-10-13).
  // 2: the mask outgrew its byte (Dictaphone, ADR 0032).
  static constexpr uint8_t kMaskVersion = 2;

  // `pinnedMask` marks entries the user may not hide: Settings, which is
  // the only way back to the screen that does the hiding.
  // `hiddenByDefault` marks entries that start hidden: Video, which needs
  // specially converted files (ADR 0031).
  MenuVisibility(int entryCount, uint16_t pinnedMask, uint16_t hiddenByDefault = 0)
      : entryCount_(entryCount < 0 ? 0
                    : entryCount > kMaxEntries ? kMaxEntries
                                               : entryCount),
        pinnedMask_(pinnedMask),
        hiddenByDefault_(hiddenByDefault),
        mask_(defaultMask()) {}

  // Everything shown but the entries hidden by default.
  uint16_t defaultMask() const { return static_cast<uint16_t>(0xFFFF & ~hiddenByDefault_); }

  uint16_t mask() const { return mask_; }
  void setMask(uint16_t mask) { mask_ = mask; }

  // A mask stored at `storedVersion`. Masks from before version 1 have
  // every unused bit set (the old default was 0xFF), which would show an
  // entry added since as if the user had chosen it. Masks from before
  // version 2 were one byte: entries past it take their default.
  void loadStored(uint16_t stored, uint8_t storedVersion) {
    if (storedVersion < 1) stored = static_cast<uint16_t>(stored & ~hiddenByDefault_);
    if (storedVersion < 2) {
      stored = static_cast<uint16_t>((stored & 0x00FF) | (defaultMask() & 0xFF00));
    }
    mask_ = stored;
  }

  bool pinned(int entryIndex) const {
    return inRange(entryIndex) && (pinnedMask_ & bitFor(entryIndex)) != 0;
  }

  bool visible(int entryIndex) const {
    if (!inRange(entryIndex)) return false;
    return pinned(entryIndex) || (mask_ & bitFor(entryIndex)) != 0;
  }

  int visibleCount() const {
    int count = 0;
    for (int i = 0; i < entryCount_; ++i) {
      if (visible(i)) ++count;
    }
    return count;
  }

  // Why a toggle was refused, so the UI can say which rule it hit rather
  // than leaving the row looking broken.
  enum class ToggleResult { Toggled, Pinned, WouldEmptyMenu };

  ToggleResult toggle(int entryIndex) {
    if (!inRange(entryIndex)) return ToggleResult::Pinned;
    if (pinned(entryIndex)) return ToggleResult::Pinned;
    // Belt and braces next to the pinned rule: a menu with nothing in it
    // would strand the user on a blank Home with no way anywhere.
    if (visible(entryIndex) && visibleCount() <= 1) {
      return ToggleResult::WouldEmptyMenu;
    }
    mask_ ^= bitFor(entryIndex);
    return ToggleResult::Toggled;
  }

 private:
  bool inRange(int entryIndex) const {
    return entryIndex >= 0 && entryIndex < entryCount_;
  }
  // Not named bit(): Arduino.h defines a bit() macro, which would eat any
  // call to it in a translation unit that includes both.
  static uint16_t bitFor(int entryIndex) {
    return static_cast<uint16_t>(1u << entryIndex);
  }

  int entryCount_;
  uint16_t pinnedMask_;
  uint16_t hiddenByDefault_;
  uint16_t mask_;
};

}  // namespace drehklang::navigation
