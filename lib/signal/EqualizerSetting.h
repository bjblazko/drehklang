#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "GraphicEqualizer.h"
#include "KeyValueStore.h"

namespace drehklang::signal {

// Settings > Equalizer (ADR 0029): which band is selected, what the knob
// and a drag do to it, and keeping the gains -- like brightness, once
// they have settled rather than per detent. The gains themselves live in
// the GraphicEqualizer the audio task runs. Pure logic over a store and
// an explicit clock.
class EqualizerSetting {
 public:
  static constexpr uint32_t kSaveDebounceMs = 1000;

  EqualizerSetting(playback::KeyValueStore &store, GraphicEqualizer &eq)
      : store_(store), eq_(eq) {}

  // Stored as gain + 12 under "eq0".."eq6"; anything out of range is 0.
  void begin() {
    for (size_t b = 0; b < GraphicEqualizer::kBands; ++b) {
      uint8_t stored = kOffset;
      const bool ok = store_.getU8(key(b), stored) && stored <= 2 * kOffset;
      saved_[b] = ok ? stored - kOffset : 0;
      eq_.setGain(b, saved_[b]);
    }
  }

  size_t selected() const { return selected_; }
  void select(size_t band) {
    if (band < GraphicEqualizer::kBands) selected_ = band;
  }

  // The knob: one detent is 1 dB on the selected band.
  void adjust(int delta, uint32_t nowMs) {
    set(selected_, eq_.gain(selected_) + delta, nowMs);
  }

  // A drag on a band: sets it and selects it.
  void set(size_t band, int db, uint32_t nowMs) {
    if (band >= GraphicEqualizer::kBands) return;
    selected_ = band;
    eq_.setGain(band, db);
    lastChangeMs_ = nowMs;
  }

  void makeFlat(uint32_t nowMs) {
    for (size_t b = 0; b < GraphicEqualizer::kBands; ++b) eq_.setGain(b, 0);
    lastChangeMs_ = nowMs;
  }

  int gain(size_t band) const { return eq_.gain(band); }
  bool flat() const { return eq_.flat(); }

  // Call every loop(); writes the bands that changed once nothing has
  // changed for kSaveDebounceMs.
  void tick(uint32_t nowMs) {
    if (nowMs - lastChangeMs_ < kSaveDebounceMs) return;
    for (size_t b = 0; b < GraphicEqualizer::kBands; ++b) {
      const int now = eq_.gain(b);
      if (now == saved_[b]) continue;
      store_.setU8(key(b), static_cast<uint8_t>(now + kOffset));
      saved_[b] = now;
    }
  }

 private:
  static constexpr int kOffset = GraphicEqualizer::kMaxGainDb;

  static std::string key(size_t band) { return "eq" + std::to_string(band); }

  playback::KeyValueStore &store_;
  GraphicEqualizer &eq_;
  std::array<int, GraphicEqualizer::kBands> saved_{};
  size_t selected_ = 0;
  uint32_t lastChangeMs_ = 0;
};

}  // namespace drehklang::signal
