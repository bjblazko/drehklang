#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "KeyValueStore.h"

namespace drehklang::games {

// Circuit's best laps, one per track (ADR 0030), kept across reboots.
// Zero means no lap yet. Written only when a record falls, so NVS sees a
// handful of writes a year rather than one per race.
class CircuitRecords {
 public:
  static constexpr int kTracks = 3;

  void load(playback::KeyValueStore &store) {
    for (int i = 0; i < kTracks; ++i) {
      uint32_t value = 0;
      best_[i] = store.getU32(key(i), value) ? value : 0;
    }
  }

  void save(playback::KeyValueStore &store, int track) const {
    if (valid(track)) store.setU32(key(track), best_[track]);
  }

  uint32_t best(int track) const { return valid(track) ? best_[track] : 0; }

  // True when lapMs beats the record (or is the first), which it then is.
  bool offer(int track, uint32_t lapMs) {
    if (!valid(track) || lapMs == 0) return false;
    if (best_[track] != 0 && lapMs >= best_[track]) return false;
    best_[track] = lapMs;
    return true;
  }

  // "1:23.4", or "--:--.-" for no lap at all.
  static void format(uint32_t ms, char *out, size_t size) {
    if (ms == 0) {
      std::snprintf(out, size, "--:--.-");
      return;
    }
    std::snprintf(out, size, "%u:%02u.%u", static_cast<unsigned>(ms / 60000),
                  static_cast<unsigned>(ms / 1000 % 60),
                  static_cast<unsigned>(ms / 100 % 10));
  }

 private:
  static bool valid(int track) { return track >= 0 && track < kTracks; }
  static std::string key(int track) { return "circuitBest" + std::to_string(track); }

  uint32_t best_[kTracks] = {};
};

}  // namespace drehklang::games
