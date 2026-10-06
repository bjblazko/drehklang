#pragma once

#include <cstdint>

namespace drehklang::games {

// What stands beside Circuit's road (ADR 0030). Every kind is solid:
// leaving the road into one is a crash.
enum class Scenery : uint8_t { None, Palm, Rock, Cactus, Lamp, Sign, Tower };

// One stretch of a track: `segments` road segments with the same bend,
// hill and roadside. curve is -6..6 (positive bends right); hill is the
// steepest climb, in kHillStep units per segment, at the stretch's start
// -- it flattens over the crest and descends back to where it began, so a
// negative hill is a dip. Scenery stands every `spacing` segments on
// `side` -1 (left), 1 (right) or 0 (both).
struct TrackSection {
  uint16_t segments;
  int8_t curve;
  int8_t hill;
  Scenery scenery;
  uint8_t spacing;
  int8_t side;
};

// A whole track. A lap starts and ends on segment 0; `checkpoint` is the
// one in between. Crossing either adds bonusMs. `palette` picks the
// colours (CircuitSprites.h) and `cars` how much traffic there is.
struct TrackDef {
  const char *name;
  const TrackSection *sections;
  uint8_t sectionCount;
  uint16_t checkpoint;
  uint32_t startTimeMs;
  uint32_t bonusMs;
  uint8_t palette;
  uint8_t cars;
};

// A track as the game and the renderer read it: one segment at a time,
// bends and hills eased in and out of each stretch so neither ever
// changes in a single step. Pure, like the rest of the game.
class CircuitTrack {
 public:
  // World units. The road is 2 * kRoadHalfWidth across; a segment is
  // kSegmentLength long (the classic proportions, roughly a car length).
  static constexpr int32_t kSegmentLength = 200;
  static constexpr int32_t kRoadHalfWidth = 1000;
  // curve and slope come in 1/256.
  static constexpr int32_t kOne = 256;
  static constexpr int32_t kHillStep = 8;

  struct Segment {
    int32_t curve;  // -6*256..6*256
    int32_t slope;  // height change to the next segment, 1/256 unit
    Scenery scenery;
    int8_t side;
  };

  CircuitTrack() = default;
  explicit CircuitTrack(const TrackDef &def) : def_(&def) {
    for (uint8_t i = 0; i < def.sectionCount; ++i) count_ += def.sections[i].segments;
  }

  const TrackDef &def() const { return *def_; }
  int32_t segmentCount() const { return count_; }
  int32_t length() const { return count_ * kSegmentLength; }

  // Any index: wraps around the lap either way.
  int32_t wrap(int32_t index) const {
    index %= count_;
    return index < 0 ? index + count_ : index;
  }

  Segment segment(int32_t index) const {
    int32_t i = wrap(index);
    for (uint8_t s = 0; s < def_->sectionCount; ++s) {
      const TrackSection &section = def_->sections[s];
      if (i < section.segments) return describe(section, i);
      i -= section.segments;
    }
    return Segment{0, 0, Scenery::None, 0};
  }

  // How far from the centre line a kind of scenery stands, in world
  // units. Signs closest, towers furthest: the nearer something is, the
  // more it punishes a wide line.
  static int32_t sceneryOffset(Scenery kind) {
    switch (kind) {
      case Scenery::Sign: return 1350;
      case Scenery::Lamp: return 1400;
      case Scenery::Tower: return 2200;
      default: return 1600;
    }
  }

 private:
  // A ramp over the first and last quarter of a stretch, flat in the
  // middle: 0..kOne.
  static int32_t ease(int32_t at, int32_t length) {
    const int32_t quarter = length / 4 > 0 ? length / 4 : 1;
    if (at < quarter) return at * kOne / quarter;
    if (at >= length - quarter) return (length - 1 - at) * kOne / quarter;
    return kOne;
  }

  // The slope falls linearly from +kOne to -kOne across the stretch, so
  // the road's height is a parabolic hump that ends where it started.
  static int32_t hump(int32_t at, int32_t length) {
    const int32_t t = at * kOne / length;  // 0..kOne
    return kOne - 2 * t;
  }

  static Segment describe(const TrackSection &section, int32_t at) {
    Segment segment{};
    segment.curve = section.curve * ease(at, section.segments);
    segment.slope = section.hill * kHillStep * hump(at, section.segments);
    const bool placed = section.scenery != Scenery::None && section.spacing > 0 &&
                        at % section.spacing == 0;
    segment.scenery = placed ? section.scenery : Scenery::None;
    segment.side = section.side;
    return segment;
  }

  const TrackDef *def_ = nullptr;
  int32_t count_ = 0;
};

}  // namespace drehklang::games
