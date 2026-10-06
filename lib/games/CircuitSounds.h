#pragma once

#include <cstdint>

#include "ChipVoices.h"
#include "CircuitGame.h"
#include "ToneGenerator.h"

namespace drehklang::games {

// Circuit's sounds (ADR 0030), for the three-voice chip. Pitches and
// levels are first guesses, to be tuned by ear (`CHIP` over serial).
namespace circuit_sound {

using playback::ChipEffect;

// Held voices sit well under the effects: they never stop, and a level
// chosen to cut through for 24 ms is exhausting held for a minute (the
// same reasoning as Gravity's thrust).
constexpr int16_t kEngineLevel = playback::ToneGenerator::kAmplitude / 3;
constexpr int16_t kRumbleLevel = playback::ToneGenerator::kAmplitude / 4;
constexpr int16_t kSquealLevel = playback::ToneGenerator::kAmplitude / 5;
constexpr uint16_t kIdleHz = 55;
constexpr uint16_t kRumbleClockHz = 900;
constexpr uint16_t kSquealClockHz = 7000;

// The engine note: it climbs through each gear and drops back on the
// change up, a little higher every gear.
constexpr uint16_t engineHz(int gear, int revs) {
  return static_cast<uint16_t>(kIdleHz + (gear - 1) * 12 + revs * 150 / 1000);
}

struct Held {
  uint16_t engineHz;
  int16_t engineLevel;
  uint16_t noiseHz;
  int16_t noiseLevel;
  bool operator==(const Held &other) const {
    return engineHz == other.engineHz && engineLevel == other.engineLevel &&
           noiseHz == other.noiseHz && noiseLevel == other.noiseLevel;
  }
  bool operator!=(const Held &other) const { return !(*this == other); }
};

// What should be sounding right now. Idling through the countdown,
// silent once the race is over or while the wreck burns.
inline Held heldFor(const CircuitGame &game) {
  Held held{0, 0, 0, 0};
  const auto phase = game.phase();
  if (phase == CircuitGame::Phase::Countdown) {
    held.engineHz = kIdleHz;
    held.engineLevel = kEngineLevel;
  }
  if (phase != CircuitGame::Phase::Race || game.crashed()) return held;
  held.engineHz = engineHz(game.gear(), game.revs());
  held.engineLevel = kEngineLevel;
  if (game.offRoad() && game.speed() > 0) {
    held.noiseHz = kRumbleClockHz;
    held.noiseLevel = kRumbleLevel;
  } else if (game.squealing()) {
    held.noiseHz = kSquealClockHz;
    held.noiseLevel = kSquealLevel;
  }
  return held;
}

constexpr int16_t kLoud = playback::ToneGenerator::kAmplitude;

// The one-shots. {start Hz, end Hz, ms, level, noise}
constexpr ChipEffect effectFor(CircuitGame::Sound sound) {
  switch (sound) {
    case CircuitGame::Sound::Beep: return ChipEffect{660, 660, 160, kLoud, false};
    case CircuitGame::Sound::Go: return ChipEffect{1320, 1320, 520, kLoud, false};
    case CircuitGame::Sound::Checkpoint: return ChipEffect{880, 1320, 260, kLoud, false};
    case CircuitGame::Sound::Lap: return ChipEffect{660, 1760, 420, kLoud, false};
    case CircuitGame::Sound::Bump: return ChipEffect{320, 80, 200, kLoud, false};
    case CircuitGame::Sound::Crash: return ChipEffect{2400, 150, 1000, kLoud * 3 / 2, true};
    case CircuitGame::Sound::Over: return ChipEffect{800, 60, 1400, kLoud, false};
    default: return ChipEffect{0, 0, 0, 0, false};
  }
}

}  // namespace circuit_sound

}  // namespace drehklang::games
