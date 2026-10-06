#pragma once

#include <cstdint>
#include <cstdlib>

#include "CircuitTrack.h"
#include "CircuitTraffic.h"

namespace drehklang::games {

// Circuit (ADR 0030): a lap against the clock, after the early-80s arcade
// racers. Checkpoints buy time, traffic is in the way, and the car only
// takes so much -- the race ends when the clock or the car gives out.
//
// The throttle is always open once the lights go: touching and turning at
// the same time is awkward on this device, so the screen is only a brake
// and the knob only a wheel (user, 2026-10-06).
//
// Pure logic over an explicit clock like the other games: no LVGL, no
// Arduino, integer world units, tick() in fixed sub-steps, and every
// integration carries its remainder (ADR 0023).
class CircuitGame {
 public:
  enum class Phase : uint8_t { Select, Countdown, Race, Over };
  enum class EndReason : uint8_t { None, TimeUp, Wrecked };
  // One-shots only. The engine, the rumble off the road and the tyres are
  // states the screen reads (ADR 0023's lesson about held sounds).
  enum class Sound : uint8_t { None, Beep, Go, Checkpoint, Lap, Bump, Crash, Over };

  // Units per second. The top speed covers 60 segments a second.
  static constexpr int32_t kTopSpeed = 12000;
  static constexpr int32_t kOffRoadTopSpeed = 4800;
  static constexpr int32_t kAcceleration = 4000;  // at a standstill
  static constexpr int32_t kBraking = 9000;
  static constexpr int32_t kOffRoadDrag = 8000;
  static constexpr int kGears = 4;

  // Steering, in knob detents. The innermost kDeadZone either side count
  // as straight: the knob is not always exact, and a click of jitter must
  // not steer (user, 2026-10-06).
  static constexpr int kMaxSteering = 12;
  static constexpr int kDeadZone = 2;
  // Sideways speed per effective detent at top speed, and the push out of
  // a curve per unit of curve at top speed. First guesses: Table Tennis's
  // knob took three goes on the device (ADR 0022).
  static constexpr int32_t kSteerPerDetent = 150;
  static constexpr int32_t kCentrifugal = 170;
  static constexpr int32_t kSquealPush = 220;
  static constexpr int32_t kMaxX = 2500;

  // Damage is 0..kWrecked.
  static constexpr int kWrecked = 100;
  static constexpr int kBumpDamage = 12;
  static constexpr int kCrashDamage = 30;
  static constexpr int kOffRoadDamagePerSecond = 3;
  static constexpr int32_t kCrashRecoveryMs = 1500;

  // Collision sizes in world units.
  static constexpr int32_t kCarHalfWidth = 300;
  static constexpr int32_t kCarLength = 300;
  static constexpr int32_t kSceneryHalfWidth = 200;

  static constexpr uint32_t kCountdownMs = 3000;
  static constexpr uint32_t kSubStepMs = 8;
  // A stall longer than this is lost rather than simulated, but anything
  // up to it is sub-stepped, so no car is passed through.
  static constexpr uint32_t kMaxStepMs = 500;

  // The steering the car actually follows, dead zone taken out.
  static int effectiveSteering(int detents) {
    if (detents > kDeadZone) return detents - kDeadZone;
    if (detents < -kDeadZone) return detents + kDeadZone;
    return 0;
  }

  // Back to the start of `track`, nothing moving.
  void select(const TrackDef &track) {
    track_ = CircuitTrack(track);
    traffic_.reset(track.cars, track_.length(), kTopSpeed, 0x5EED0000u + track.palette);
    phase_ = Phase::Select;
    endReason_ = EndReason::None;
    z_ = x_ = speed_ = 0;
    zRemainder_ = xRemainder_ = speedRemainder_ = 0;
    steering_ = 0;
    braking_ = false;
    damageMilli_ = 0;
    crashLeftMs_ = 0;
    bumps_ = 0;
    timeLeftMs_ = track.startTimeMs;
    raceMs_ = lapStartMs_ = bestLapMs_ = 0;
    laps_ = 0;
    squealing_ = false;
    soundCount_ = 0;
  }

  void startCountdown(uint32_t nowMs) {
    if (phase_ != Phase::Select) return;
    phase_ = Phase::Countdown;
    countdownStartMs_ = lastTickMs_ = nowMs;
    beeps_ = 0;
  }

  // Signed detents, clockwise steers right. The angle stays where it is
  // left, like a wheel. Works in the countdown too: a control that does
  // nothing until later reads as broken (ADR 0023).
  void steer(int detents) {
    if (phase_ != Phase::Countdown && phase_ != Phase::Race) return;
    int next = steering_ + detents;
    if (next > kMaxSteering) next = kMaxSteering;
    if (next < -kMaxSteering) next = -kMaxSteering;
    steering_ = next;
  }

  void setBraking(bool on) { braking_ = on; }

  void tick(uint32_t nowMs) {
    uint32_t dtMs = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;
    if (phase_ == Phase::Countdown) tickCountdown(nowMs);
    if (phase_ != Phase::Race) return;
    if (dtMs > kMaxStepMs) dtMs = kMaxStepMs;
    while (dtMs > 0 && phase_ == Phase::Race) {
      const uint32_t slice = dtMs > kSubStepMs ? kSubStepMs : dtMs;
      advance(static_cast<int32_t>(slice));
      dtMs -= slice;
    }
  }

  Sound takeSound() {
    if (soundCount_ == 0) return Sound::None;
    const Sound sound = sounds_[0];
    for (uint8_t i = 1; i < soundCount_; ++i) sounds_[i - 1] = sounds_[i];
    --soundCount_;
    return sound;
  }

  Phase phase() const { return phase_; }
  EndReason endReason() const { return endReason_; }
  const CircuitTrack &track() const { return track_; }
  int32_t z() const { return z_; }
  int32_t x() const { return x_; }
  int32_t speed() const { return speed_; }
  int steering() const { return steering_; }
  bool braking() const { return braking_; }
  int damage() const { return damageMilli_ / 1000; }
  bool crashed() const { return crashLeftMs_ > 0; }
  int32_t crashLeftMs() const { return crashLeftMs_; }
  int bumps() const { return bumps_; }
  uint32_t timeLeftMs() const { return timeLeftMs_; }
  uint32_t countdownElapsedMs() const { return lastTickMs_ - countdownStartMs_; }
  int lapsCompleted() const { return laps_; }
  uint32_t lapTimeMs() const { return raceMs_ - lapStartMs_; }
  uint32_t bestLapMs() const { return bestLapMs_; }
  bool offRoad() const { return std::abs(x_) > CircuitTrack::kRoadHalfWidth; }
  bool squealing() const { return squealing_; }
  int carCount() const { return traffic_.count(); }
  const CircuitCar &car(int i) const { return traffic_.car(i); }

  // 1..kGears, from speed alone: the gearbox is automatic.
  int gear() const {
    const int g = 1 + static_cast<int>(static_cast<int64_t>(speed_) * kGears / (kTopSpeed + 1));
    return g > kGears ? kGears : g;
  }

  // 0..1000 within the current gear: what the engine note follows.
  int revs() const {
    const int32_t band = kTopSpeed / kGears;
    const int32_t within = speed_ - (gear() - 1) * band;
    const int32_t r = within * 1000 / band;
    return r > 1000 ? 1000 : r;
  }

 private:
  void tickCountdown(uint32_t nowMs) {
    const uint32_t elapsed = nowMs - countdownStartMs_;
    while (beeps_ < 3 && elapsed >= static_cast<uint32_t>(beeps_) * 1000u) {
      ++beeps_;
      push(Sound::Beep);
    }
    if (elapsed >= kCountdownMs) {
      push(Sound::Go);
      phase_ = Phase::Race;
    }
  }

  void advance(int32_t ms) {
    traffic_.advance(ms, track_.length());
    raceMs_ += static_cast<uint32_t>(ms);
    if (crashLeftMs_ > 0) {
      crashLeftMs_ -= ms;
      if (crashLeftMs_ < 0) crashLeftMs_ = 0;
    } else {
      drive(ms);
    }
    keepTrafficBehind();
    countDown(ms);
  }

  void drive(int32_t ms) {
    updateSpeed(ms);
    steerAndDrift(ms);
    const int32_t before = z_ / CircuitTrack::kSegmentLength;
    move(ms);
    const int32_t after = z_ / CircuitTrack::kSegmentLength;
    if (after != before) crossSegment(after);
    if (offRoad() && speed_ > 0) damageMilli_ += kOffRoadDamagePerSecond * ms;
    checkTraffic();
    if (damage() >= kWrecked) end(EndReason::Wrecked);
  }

  void updateSpeed(int32_t ms) {
    const int32_t top = offRoad() ? kOffRoadTopSpeed : kTopSpeed;
    int32_t rate = 0;  // units per second per second
    if (braking_) {
      rate = -kBraking;
    } else if (speed_ > top) {
      rate = -kOffRoadDrag;
    } else {
      rate = static_cast<int32_t>(static_cast<int64_t>(kAcceleration) * (top - speed_) / top);
    }
    const int32_t delta = rate * ms + speedRemainder_;
    speed_ += delta / 1000;
    speedRemainder_ = delta % 1000;
    if (speed_ < 0) speed_ = speedRemainder_ = 0;
    if (!braking_ && rate < 0 && speed_ < top) speed_ = top;
  }

  void steerAndDrift(int32_t ms) {
    const CircuitTrack::Segment here = track_.segment(z_ / CircuitTrack::kSegmentLength);
    const int64_t v = speed_;
    const int64_t steer = effectiveSteering(steering_) * kSteerPerDetent * v / kTopSpeed;
    const int64_t push = static_cast<int64_t>(here.curve) * kCentrifugal * v / kTopSpeed * v /
                         kTopSpeed / CircuitTrack::kOne;
    squealing_ = std::llabs(push) > kSquealPush;
    const int32_t delta = static_cast<int32_t>(steer - push) * ms + xRemainder_;
    x_ += delta / 1000;
    xRemainder_ = delta % 1000;
    if (x_ > kMaxX) x_ = kMaxX;
    if (x_ < -kMaxX) x_ = -kMaxX;
  }

  void move(int32_t ms) {
    const int32_t travelled = speed_ * ms + zRemainder_;
    z_ += travelled / 1000;
    zRemainder_ = travelled % 1000;
  }

  // At most one segment per slice: kTopSpeed * kSubStepMs is under a
  // segment's length.
  void crossSegment(int32_t after) {
    if (z_ >= track_.length()) {
      z_ -= track_.length();
      completeLap();
      after = 0;
    }
    if (after == track_.def().checkpoint) {
      timeLeftMs_ += track_.def().bonusMs;
      push(Sound::Checkpoint);
    }
    checkScenery(track_.segment(after));
  }

  void completeLap() {
    const uint32_t lap = raceMs_ - lapStartMs_;
    lapStartMs_ = raceMs_;
    ++laps_;
    if (bestLapMs_ == 0 || lap < bestLapMs_) bestLapMs_ = lap;
    timeLeftMs_ += track_.def().bonusMs;
    push(Sound::Lap);
  }

  void checkScenery(const CircuitTrack::Segment &segment) {
    if (segment.scenery == Scenery::None) return;
    const int32_t offset = CircuitTrack::sceneryOffset(segment.scenery);
    const bool left = segment.side <= 0 && hits(-offset);
    const bool right = segment.side >= 0 && hits(offset);
    if (left || right) crash();
  }

  bool hits(int32_t objectX) const {
    return std::abs(x_ - objectX) < kCarHalfWidth + kSceneryHalfWidth;
  }

  void crash() {
    speed_ = speedRemainder_ = 0;
    steering_ = 0;
    xRemainder_ = 0;
    const int32_t back = CircuitTrack::kRoadHalfWidth - kCarHalfWidth;
    x_ = x_ < 0 ? -back : back;
    crashLeftMs_ = kCrashRecoveryMs;
    damageMilli_ += kCrashDamage * 1000;
    push(Sound::Crash);
  }

  int32_t ahead(const CircuitCar &car) const {
    const int32_t dz = car.z - z_;
    return dz < 0 ? dz + track_.length() : dz;
  }

  bool sameLane(const CircuitCar &car) const {
    return std::abs(car.x - x_) < 2 * kCarHalfWidth;
  }

  // Running into the back of a car: it costs speed and some of the car.
  void checkTraffic() {
    for (int i = 0; i < traffic_.count(); ++i) {
      const CircuitCar &car = traffic_.car(i);
      if (ahead(car) >= kCarLength || !sameLane(car) || speed_ <= car.speed) continue;
      speed_ = car.speed * 3 / 4;
      speedRemainder_ = 0;
      x_ += (x_ >= car.x ? 1 : -1) * 150;
      damageMilli_ += kBumpDamage * 1000;
      ++bumps_;
      push(Sound::Bump);
    }
  }

  // A computer car catching the player from behind does not drive
  // through: it waits behind and moves over.
  void keepTrafficBehind() {
    for (int i = 0; i < traffic_.count(); ++i) {
      CircuitCar &car = traffic_.car(i);
      const int32_t behind = track_.length() - ahead(car);
      if (behind >= kCarLength || !sameLane(car)) continue;
      car.z = z_ - kCarLength;
      if (car.z < 0) car.z += track_.length();
      if (car.x == car.targetX) traffic_.changeLane(car);
    }
  }

  void countDown(int32_t ms) {
    const uint32_t step = static_cast<uint32_t>(ms);
    timeLeftMs_ = timeLeftMs_ > step ? timeLeftMs_ - step : 0;
    if (timeLeftMs_ == 0) end(EndReason::TimeUp);
  }

  void end(EndReason reason) {
    if (phase_ != Phase::Race) return;
    phase_ = Phase::Over;
    endReason_ = reason;
    if (reason == EndReason::Wrecked) damageMilli_ = kWrecked * 1000;
    speed_ = 0;
    squealing_ = false;
    push(Sound::Over);
  }

  void push(Sound sound) {
    if (soundCount_ < kMaxSounds) sounds_[soundCount_++] = sound;
  }

  static constexpr uint8_t kMaxSounds = 8;

  CircuitTrack track_;
  CircuitTraffic traffic_;
  Phase phase_ = Phase::Select;
  EndReason endReason_ = EndReason::None;
  int32_t z_ = 0;
  int32_t x_ = 0;
  int32_t speed_ = 0;
  int32_t zRemainder_ = 0;
  int32_t xRemainder_ = 0;
  int32_t speedRemainder_ = 0;
  int steering_ = 0;
  bool braking_ = false;
  bool squealing_ = false;
  int32_t damageMilli_ = 0;
  int32_t crashLeftMs_ = 0;
  int bumps_ = 0;
  uint32_t timeLeftMs_ = 0;
  uint32_t raceMs_ = 0;
  uint32_t lapStartMs_ = 0;
  uint32_t bestLapMs_ = 0;
  int laps_ = 0;
  uint32_t countdownStartMs_ = 0;
  uint32_t lastTickMs_ = 0;
  int beeps_ = 0;
  Sound sounds_[kMaxSounds] = {};
  uint8_t soundCount_ = 0;
};

}  // namespace drehklang::games
