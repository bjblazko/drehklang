#pragma once

#include <cstdint>

#include "CircuitTrack.h"

namespace drehklang::games {

// One computer car (ADR 0030): traffic to pass, not a rival. It holds
// its own speed in its own lane and now and then drifts to another.
struct CircuitCar {
  int32_t z = 0;        // world units along the lap, 0..length
  int32_t x = 0;        // world units from the centre line
  int32_t speed = 0;    // units per second
  int32_t targetX = 0;  // the lane it is drifting to
  int32_t laneTimerMs = 0;
  int32_t zRemainder = 0;
  uint8_t livery = 0;
};

// Circuit's traffic. Pure and seeded, so a race replays exactly.
class CircuitTraffic {
 public:
  static constexpr int kMaxCars = 8;
  static constexpr int kLaneCount = 3;
  static constexpr int32_t kLanes[kLaneCount] = {-550, 0, 550};
  // Sideways, when changing lanes: a drift, not a swerve.
  static constexpr int32_t kLaneChangeSpeed = 400;

  // Spreads `count` cars around the lap, the first one close ahead in the
  // centre lane so the race has something to pass straight away. Speeds
  // are 60..85 % of `topSpeed`.
  void reset(int count, int32_t trackLength, int32_t topSpeed, uint32_t seed) {
    count_ = count > kMaxCars ? kMaxCars : count;
    seed_ = seed;
    for (int i = 0; i < count_; ++i) {
      CircuitCar &car = cars_[i];
      car = CircuitCar{};
      car.z = (i == 0) ? 3000 : static_cast<int32_t>(
                                    static_cast<int64_t>(trackLength) * i / count_);
      const int lane = (i == 0) ? 1 : static_cast<int>(next() % kLaneCount);
      car.x = car.targetX = kLanes[lane];
      car.speed = topSpeed * (60 + static_cast<int32_t>(next() % 26)) / 100;
      car.laneTimerMs = nextLaneTimer();
      car.livery = static_cast<uint8_t>(i % 4);
    }
  }

  void advance(int32_t ms, int32_t trackLength) {
    for (int i = 0; i < count_; ++i) advanceCar(cars_[i], ms, trackLength);
  }

  int count() const { return count_; }
  const CircuitCar &car(int i) const { return cars_[i]; }
  CircuitCar &car(int i) { return cars_[i]; }

  // Sends a car to a lane other than the one it is in: it has to get out
  // of the way of the player's car.
  void changeLane(CircuitCar &car) {
    const int lane = laneOf(car.targetX);
    car.targetX = kLanes[(lane + 1 + static_cast<int>(next() % (kLaneCount - 1))) % kLaneCount];
    car.laneTimerMs = nextLaneTimer();
  }

 private:
  static int laneOf(int32_t x) {
    for (int i = 0; i < kLaneCount; ++i) {
      if (kLanes[i] == x) return i;
    }
    return 1;
  }

  void advanceCar(CircuitCar &car, int32_t ms, int32_t trackLength) {
    const int32_t travelled = car.speed * ms + car.zRemainder;
    car.z += travelled / 1000;
    car.zRemainder = travelled % 1000;
    if (car.z >= trackLength) car.z -= trackLength;

    const int32_t step = kLaneChangeSpeed * ms / 1000;
    if (car.x < car.targetX) car.x = car.x + step > car.targetX ? car.targetX : car.x + step;
    if (car.x > car.targetX) car.x = car.x - step < car.targetX ? car.targetX : car.x - step;

    car.laneTimerMs -= ms;
    if (car.laneTimerMs <= 0) changeLane(car);
  }

  // A plain LCG: the numbers only need to look unplanned, and replay.
  uint32_t next() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return seed_ >> 8;
  }

  int32_t nextLaneTimer() { return 3000 + static_cast<int32_t>(next() % 5000); }

  CircuitCar cars_[kMaxCars];
  int count_ = 0;
  uint32_t seed_ = 1;
};

}  // namespace drehklang::games
