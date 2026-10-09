#pragma once

#include <cstdint>

#include "CircuitGame.h"
#include "CircuitTrack.h"

namespace drehklang::games {

// Everything one frame of Circuit shows, copied out of the game (ADR
// 0030). The render task on core 0 reads only this, never the game the
// main loop is ticking, so the two never share mutable state. The track
// itself is constexpr data and safe to read from anywhere.
struct CircuitScene {
  struct Car {
    int32_t z;
    int32_t x;
    uint8_t livery;
  };

  const CircuitTrack *track = nullptr;
  int trackIndex = 0;  // palette, name and record
  CircuitGame::Phase phase = CircuitGame::Phase::Select;
  CircuitGame::EndReason endReason = CircuitGame::EndReason::None;

  int32_t z = 0;
  int32_t x = 0;
  int32_t speed = 0;
  int32_t heading = 0;
  int steering = 0;  // the capped angle, as drawn
  int wheel = 0;     // where the wheel is, uncapped: the rim marker
  bool braking = false;
  bool offRoad = false;
  int32_t crashLeftMs = 0;

  int carCount = 0;
  Car cars[CircuitTraffic::kMaxCars] = {};

  uint32_t timeLeftMs = 0;
  uint32_t lapTimeMs = 0;
  uint32_t bestLapMs = 0;   // this race
  uint32_t recordMs = 0;    // this track, ever
  int lap = 1;
  int damage = 0;
  uint32_t countdownMs = 0;
  bool newRecord = false;
  // Counts frames, for blinking and the off-road shake.
  uint32_t frame = 0;
  // Time since the last checkpoint or lap, for "EXTENDED TIME".
  uint32_t sinceBonusMs = UINT32_MAX;

  static CircuitScene capture(const CircuitGame &game) {
    CircuitScene scene;
    scene.track = &game.track();
    scene.phase = game.phase();
    scene.endReason = game.endReason();
    scene.z = game.z();
    scene.x = game.x();
    scene.speed = game.speed();
    scene.heading = game.heading();
    scene.steering = game.steeringAngle();
    scene.wheel = game.steering();
    scene.braking = game.braking() && game.phase() == CircuitGame::Phase::Race;
    scene.offRoad = game.offRoad();
    scene.crashLeftMs = game.crashLeftMs();
    scene.carCount = game.carCount();
    for (int i = 0; i < scene.carCount; ++i) {
      const CircuitCar &car = game.car(i);
      scene.cars[i] = Car{car.z, car.x, car.livery};
    }
    scene.timeLeftMs = game.timeLeftMs();
    scene.lapTimeMs = game.lapTimeMs();
    scene.bestLapMs = game.bestLapMs();
    scene.lap = game.lapsCompleted() + 1;
    scene.damage = game.damage();
    scene.countdownMs = game.countdownElapsedMs();
    return scene;
  }
};

}  // namespace drehklang::games
