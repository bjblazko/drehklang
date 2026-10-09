#pragma once

#include "CircuitTrack.h"

namespace drehklang::games {

// Circuit's three tracks (ADR 0030). Each is a table: a fourth is one more
// table and one more row in kTracks. Lap lengths are ~1400-1700 segments,
// about half a minute at speed; start time and bonus are first guesses
// meant to be tuned on the device.
//
// Signs stand every 3x, palms and cacti every 2x the distance they did at
// first: the roadside was too busy to read (user, 2026-10-09).
//
// Columns: segments, curve (-6..6, + is right), hill (steepest climb,
// units per segment), scenery, every n segments, side (-1 left, 1 right,
// 0 both).

// Coast: fast and open, long sweepers along the sea, one crest.
constexpr TrackSection kCoastSections[] = {
    {120, 0, 0, Scenery::Sign, 36, 0},
    {160, 2, 0, Scenery::Palm, 16, -1},
    {100, 0, 3, Scenery::Palm, 20, 0},
    {140, -3, 0, Scenery::Palm, 16, 1},
    {120, 0, -2, Scenery::Lamp, 10, 0},
    {100, 4, 0, Scenery::Sign, 18, -1},
    {160, 0, 4, Scenery::Palm, 18, 0},
    {130, -2, 0, Scenery::Palm, 16, 1},
    {90, -5, 0, Scenery::Sign, 15, 1},
    {150, 0, 0, Scenery::Lamp, 10, 0},
    {140, 3, -3, Scenery::Palm, 16, -1},
    {110, 0, 0, Scenery::Sign, 36, 0},
};

// Desert: long straights, then hard corners that punish carried speed.
constexpr TrackSection kDesertSections[] = {
    {180, 0, 0, Scenery::Sign, 45, 0},
    {80, 5, 0, Scenery::Rock, 6, -1},
    {200, 0, 2, Scenery::Cactus, 18, 0},
    {90, -5, 0, Scenery::Rock, 6, 1},
    {120, 0, 5, Scenery::Cactus, 20, 0},
    {100, 3, -4, Scenery::Rock, 7, -1},
    {220, 0, 0, Scenery::Cactus, 22, 0},
    {80, -6, 0, Scenery::Sign, 15, 1},
    {140, 0, -3, Scenery::Cactus, 18, 0},
    {110, 4, 0, Scenery::Rock, 7, -1},
    {130, 0, 0, Scenery::Sign, 42, 0},
};

// Night: the city. Narrow-feeling turns between lamps, short straights.
constexpr TrackSection kNightSections[] = {
    {100, 0, 0, Scenery::Lamp, 6, 0},
    {110, -3, 0, Scenery::Tower, 10, 1},
    {90, 4, 0, Scenery::Lamp, 5, -1},
    {130, 0, 3, Scenery::Lamp, 6, 0},
    {100, -4, 0, Scenery::Sign, 15, 1},
    {120, 2, -3, Scenery::Tower, 9, 0},
    {140, 0, 0, Scenery::Lamp, 6, 0},
    {80, 5, 0, Scenery::Sign, 12, -1},
    {110, -2, 2, Scenery::Lamp, 6, 0},
    {120, 0, 0, Scenery::Tower, 10, 0},
    {90, -5, 0, Scenery::Sign, 12, 1},
    {150, 3, 0, Scenery::Lamp, 6, 0},
    {100, 0, 0, Scenery::Lamp, 6, 0},
};

template <typename T, int N>
constexpr uint8_t countOf(const T (&)[N]) {
  return static_cast<uint8_t>(N);
}

// The palette indices match CircuitSprites.h's kPalettes.
constexpr TrackDef kTracks[] = {
    {"COAST", kCoastSections, countOf(kCoastSections), 760, 42000, 18000, 0, 6},
    {"DESERT", kDesertSections, countOf(kDesertSections), 720, 42000, 18000, 1, 6},
    {"NIGHT", kNightSections, countOf(kNightSections), 700, 42000, 18000, 2, 6},
};

constexpr int kTrackCount = static_cast<int>(sizeof(kTracks) / sizeof(kTracks[0]));

}  // namespace drehklang::games
