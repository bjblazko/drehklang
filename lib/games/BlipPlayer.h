#pragma once

#include <cstdint>

#include "ChipVoices.h"

namespace drehklang::games {

// How a game asks for a sound (ADR 0022). Deliberately this small: a game
// knows a pitch and a length, and nothing about I2S, tasks or whether
// music happens to be playing -- that is the driver's problem
// (docs/coding-guidelines.md's hardware/logic separation).
class BlipPlayer {
 public:
  virtual ~BlipPlayer() = default;
  virtual void blip(uint16_t frequencyHz, uint16_t durationMs) = 0;
  // Noise rather than a tone, for an engine rather than a blip (ADR
  // 0023). durationMs == 0 holds it until silence() -- thrust lasts as
  // long as the finger does, which no fixed duration can express.
  virtual void noise(uint16_t clockHz, uint16_t durationMs, int16_t level) = 0;
  // Circuit's three-voice chip (ADR 0030), mixed over the blips. The
  // engine and noise voices are held at whatever was last set (level 0
  // is off); an effect is a one-shot.
  virtual void chipEngine(uint16_t hz, int16_t level) = 0;
  virtual void chipNoise(uint16_t clockHz, int16_t level) = 0;
  virtual void chipEffect(const playback::ChipEffect &effect) = 0;
  // Stops anything sounding, chip included: leaving the game should not
  // trail a beep, and letting go of the throttle should not trail an
  // engine.
  virtual void silence() = 0;
};

}  // namespace drehklang::games
