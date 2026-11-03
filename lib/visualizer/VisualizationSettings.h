#pragma once

#include <cstdint>

#include "KeyValueStore.h"

namespace drehklang::visualizer {

// What the full-screen visualization shows (ADR 0034). Stored, so
// append-only.
enum class Visualization : uint8_t { AnalogVu, BarMeter };

// The visualization last chosen, and whether the view keeps the display
// awake. Both survive a reboot. Pure logic over a store.
class VisualizationSettings {
 public:
  static constexpr char kKindKey[] = "vizKind";
  static constexpr char kAwakeKey[] = "vizAwake";

  explicit VisualizationSettings(playback::KeyValueStore &store) : store_(store) {}

  void begin() {
    uint8_t stored = 0;
    current_ = store_.getU8(kKindKey, stored) && stored <= kLast ? static_cast<Visualization>(stored)
                                                                 : Visualization::AnalogVu;
    keepAwake_ = store_.getU8(kAwakeKey, stored) && stored != 0;
  }

  Visualization current() const { return current_; }

  // The next one, wrapping round after the last.
  void next() {
    const uint8_t index = static_cast<uint8_t>(current_);
    current_ = static_cast<Visualization>(index >= kLast ? 0 : index + 1);
    store_.setU8(kKindKey, static_cast<uint8_t>(current_));
  }

  bool keepAwake() const { return keepAwake_; }

  void toggleKeepAwake() {
    keepAwake_ = !keepAwake_;
    store_.setU8(kAwakeKey, keepAwake_ ? 1 : 0);
  }

  // Only while music plays: a paused player goes dark as usual.
  bool holdsDisplayAwake(bool playing) const { return keepAwake_ && playing; }

 private:
  static constexpr uint8_t kLast = static_cast<uint8_t>(Visualization::BarMeter);

  playback::KeyValueStore &store_;
  Visualization current_ = Visualization::AnalogVu;
  bool keepAwake_ = false;
};

}  // namespace drehklang::visualizer
