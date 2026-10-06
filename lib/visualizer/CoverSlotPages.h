#pragma once

#include <cstddef>
#include <cstdint>

#include "KeyValueStore.h"

namespace drehklang::visualizer {

// What Now Playing's cover slot shows (ADR 0028). Values are stored.
enum class SlotView : uint8_t { Cover, DotMatrix, Scope, Spectrum };

// The cover slot's pages, swiped through like the tone generator's band:
// the cover (only when the album has one), the dot matrix, and the tone
// generator's scope and spectrum over the music. The page swiped to is
// kept. Pure logic over a store.
class CoverSlotPages {
 public:
  static constexpr char kKey[] = "npView";
  // Before ADR 0028: 1 = spectrum (the dot matrix), 0 = cover.
  static constexpr char kLegacyKey[] = "npSpectrum";

  explicit CoverSlotPages(playback::KeyValueStore &store) : store_(store) {}

  void begin() {
    uint8_t stored = 0;
    if (store_.getU8(kKey, stored)) {
      preferred_ = stored <= kLast ? static_cast<SlotView>(stored) : SlotView::DotMatrix;
    } else {
      preferred_ = store_.getU8(kLegacyKey, stored) && stored != 0 ? SlotView::DotMatrix
                                                                   : SlotView::Cover;
    }
  }

  size_t count(bool hasCover) const { return hasCover ? kLast + 1 : kLast; }

  // The kept page, or the dot matrix where it asks for a missing cover.
  SlotView shown(bool hasCover) const {
    return preferred_ == SlotView::Cover && !hasCover ? SlotView::DotMatrix : preferred_;
  }

  size_t shownIndex(bool hasCover) const {
    const auto view = static_cast<size_t>(shown(hasCover));
    return hasCover ? view : view - 1;
  }

  // One page on (+1, a swipe right to left) or back (-1), no further than
  // the first or last. Returns whether the page changed; keeps it if so.
  bool step(int direction, bool hasCover) {
    const size_t index = shownIndex(hasCover);
    if (direction > 0 && index + 1 >= count(hasCover)) return false;
    if (direction < 0 && index == 0) return false;
    const size_t next = direction > 0 ? index + 1 : index - 1;
    preferred_ = static_cast<SlotView>(hasCover ? next : next + 1);
    store_.setU8(kKey, static_cast<uint8_t>(preferred_));
    return true;
  }

 private:
  static constexpr uint8_t kLast = static_cast<uint8_t>(SlotView::Spectrum);

  playback::KeyValueStore &store_;
  SlotView preferred_ = SlotView::Cover;
};

}  // namespace drehklang::visualizer
