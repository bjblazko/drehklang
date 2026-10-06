#include <unity.h>

#include <cstdint>
#include <cstdlib>

#include "CircuitGame.h"
#include "CircuitRecords.h"
#include "CircuitTrack.h"
#include "CircuitTracks.h"

using drehklang::games::CircuitGame;
using drehklang::games::CircuitRecords;
using drehklang::games::CircuitTrack;
using drehklang::games::Scenery;
using drehklang::games::TrackDef;
using drehklang::games::TrackSection;
using Phase = CircuitGame::Phase;
using EndReason = CircuitGame::EndReason;
using Sound = CircuitGame::Sound;

void setUp() {}
void tearDown() {}

namespace {

// Test tracks: one thing each, so a rule fails on its own.
constexpr TrackSection kStraight[] = {{1200, 0, 0, Scenery::None, 0, 0}};
constexpr TrackDef kStraightTrack{"STRAIGHT", kStraight, 1, 600, 60000, 20000, 0, 0};
constexpr TrackDef kStraightWithTraffic{"TRAFFIC", kStraight, 1, 600, 60000, 20000, 0, 6};

constexpr TrackSection kBend[] = {{1200, 4, 0, Scenery::None, 0, 0}};
constexpr TrackDef kBendTrack{"BEND", kBend, 1, 600, 60000, 20000, 0, 0};

// Palms on both sides, every segment: whoever leaves the road hits one.
constexpr TrackSection kLined[] = {{1200, 0, 0, Scenery::Palm, 1, 0}};
constexpr TrackDef kLinedTrack{"LINED", kLined, 1, 600, 60000, 20000, 0, 0};

constexpr TrackSection kShort[] = {{100, 0, 0, Scenery::None, 0, 0}};
constexpr TrackDef kShortTrack{"SHORT", kShort, 1, 50, 60000, 5000, 0, 0};

// Advances in ~5 ms loop steps, the rate the device ticks at.
uint32_t drive(CircuitGame &game, uint32_t fromMs, uint32_t forMs) {
  uint32_t now = fromMs;
  while (now < fromMs + forMs && game.phase() != Phase::Over) {
    now += 5;
    game.tick(now);
  }
  return now;
}

// Selects a track and runs the countdown out: returns the time of GO.
uint32_t race(CircuitGame &game, const TrackDef &track) {
  game.select(track);
  game.startCountdown(0);
  return drive(game, 0, CircuitGame::kCountdownMs);
}

int countSounds(CircuitGame &game, Sound wanted) {
  int seen = 0;
  for (Sound s = game.takeSound(); s != Sound::None; s = game.takeSound()) {
    if (s == wanted) ++seen;
  }
  return seen;
}

}  // namespace

// --- The track ---------------------------------------------------------

void test_track_length_is_its_segments() {
  CircuitTrack track(kStraightTrack);
  TEST_ASSERT_EQUAL_INT32(1200, track.segmentCount());
  TEST_ASSERT_EQUAL_INT32(1200 * CircuitTrack::kSegmentLength, track.length());
}

void test_track_curve_eases_in_and_out() {
  CircuitTrack track(kBendTrack);
  const int32_t entry = track.segment(0).curve;
  const int32_t middle = track.segment(600).curve;
  const int32_t exit = track.segment(1199).curve;
  TEST_ASSERT_TRUE(middle > 0);
  TEST_ASSERT_TRUE(entry < middle);
  TEST_ASSERT_TRUE(exit < middle);
}

void test_track_segment_index_wraps() {
  CircuitTrack track(kLinedTrack);
  TEST_ASSERT_EQUAL(track.segment(5).scenery, track.segment(1205).scenery);
  TEST_ASSERT_EQUAL(track.segment(-1).scenery, track.segment(1199).scenery);
}

void test_every_shipped_track_is_a_closed_lap_of_reasonable_length() {
  for (int i = 0; i < drehklang::games::kTrackCount; ++i) {
    CircuitTrack track(drehklang::games::kTracks[i]);
    TEST_ASSERT_GREATER_THAN(600, track.segmentCount());
    TEST_ASSERT_LESS_THAN(4000, track.segmentCount());
    TEST_ASSERT_TRUE(drehklang::games::kTracks[i].checkpoint > 0);
    TEST_ASSERT_TRUE(drehklang::games::kTracks[i].checkpoint < track.segmentCount());
  }
}

// --- Countdown and throttle --------------------------------------------

void test_nothing_moves_before_go() {
  CircuitGame game;
  game.select(kStraightTrack);
  game.startCountdown(0);
  drive(game, 0, CircuitGame::kCountdownMs - 50);
  TEST_ASSERT_EQUAL(Phase::Countdown, game.phase());
  TEST_ASSERT_EQUAL_INT32(0, game.speed());
}

void test_countdown_beeps_three_times_then_go() {
  CircuitGame game;
  game.select(kStraightTrack);
  game.startCountdown(0);
  drive(game, 0, CircuitGame::kCountdownMs + 10);
  int beeps = 0;
  int gos = 0;
  for (Sound s = game.takeSound(); s != Sound::None; s = game.takeSound()) {
    if (s == Sound::Beep) ++beeps;
    if (s == Sound::Go) ++gos;
  }
  TEST_ASSERT_EQUAL(3, beeps);
  TEST_ASSERT_EQUAL(1, gos);
  TEST_ASSERT_EQUAL(Phase::Race, game.phase());
}

void test_throttle_is_full_from_go() {
  CircuitGame game;
  const uint32_t go = race(game, kStraightTrack);
  drive(game, go, 1000);
  const int32_t after1s = game.speed();
  drive(game, go + 1000, 4000);
  TEST_ASSERT_GREATER_THAN(0, after1s);
  TEST_ASSERT_GREATER_THAN(after1s, game.speed());
  TEST_ASSERT_LESS_OR_EQUAL(CircuitGame::kTopSpeed, game.speed());
}

void test_braking_slows_and_letting_go_speeds_up_again() {
  CircuitGame game;
  uint32_t now = race(game, kStraightTrack);
  now = drive(game, now, 6000);
  const int32_t cruising = game.speed();
  game.setBraking(true);
  now = drive(game, now, 1000);
  const int32_t braked = game.speed();
  game.setBraking(false);
  drive(game, now, 1000);
  TEST_ASSERT_LESS_THAN(cruising / 2, braked);
  TEST_ASSERT_GREATER_THAN(braked, game.speed());
}

void test_gears_rise_with_speed() {
  CircuitGame game;
  uint32_t now = race(game, kStraightTrack);
  now = drive(game, now, 300);
  const int low = game.gear();
  drive(game, now, 8000);
  TEST_ASSERT_EQUAL(1, low);
  TEST_ASSERT_EQUAL(CircuitGame::kGears, game.gear());
}

// --- Steering ----------------------------------------------------------

void test_dead_zone_ignores_two_detents_of_jitter() {
  // The knob is not always exact (user, 2026-10-06): +-2 is straight.
  TEST_ASSERT_EQUAL(0, CircuitGame::effectiveSteering(2));
  TEST_ASSERT_EQUAL(0, CircuitGame::effectiveSteering(-2));
  TEST_ASSERT_EQUAL(1, CircuitGame::effectiveSteering(3));
  TEST_ASSERT_EQUAL(-1, CircuitGame::effectiveSteering(-3));

  CircuitGame game;
  uint32_t now = race(game, kStraightTrack);
  game.steer(2);
  drive(game, now, 3000);
  TEST_ASSERT_EQUAL_INT32(0, game.x());
}

void test_steering_beyond_the_dead_zone_moves_the_car() {
  CircuitGame game;
  uint32_t now = race(game, kStraightTrack);
  now = drive(game, now, 2000);
  game.steer(5);
  drive(game, now, 500);
  TEST_ASSERT_GREATER_THAN(100, game.x());
}

void test_steering_is_clamped_and_stays_where_it_is_left() {
  CircuitGame game;
  race(game, kStraightTrack);
  game.steer(50);
  TEST_ASSERT_EQUAL(CircuitGame::kMaxSteering, game.steering());
  game.steer(-3);
  TEST_ASSERT_EQUAL(CircuitGame::kMaxSteering - 3, game.steering());
}

void test_steering_already_works_in_the_countdown() {
  CircuitGame game;
  game.select(kStraightTrack);
  game.startCountdown(0);
  game.steer(4);
  TEST_ASSERT_EQUAL(4, game.steering());
}

void test_a_bend_pushes_an_unsteered_car_outward() {
  // Positive curve bends right; the car drifts left (negative x).
  CircuitGame game;
  uint32_t now = race(game, kBendTrack);
  drive(game, now, 8000);  // The bend eases in over its first quarter.
  TEST_ASSERT_LESS_THAN(-200, game.x());
}

void test_steering_into_a_bend_holds_the_line() {
  CircuitGame free;
  CircuitGame held;
  uint32_t a = race(free, kBendTrack);
  uint32_t b = race(held, kBendTrack);
  held.steer(5);
  drive(free, a, 8000);
  drive(held, b, 8000);
  TEST_ASSERT_LESS_THAN(std::abs(free.x()), std::abs(held.x()));
}

// --- Off the road, traffic, scenery ------------------------------------

void test_off_road_caps_speed_and_costs_damage() {
  CircuitGame game;
  uint32_t now = race(game, kStraightTrack);
  game.steer(CircuitGame::kMaxSteering);
  now = drive(game, now, 3000);
  game.steer(-CircuitGame::kMaxSteering);  // Back to straight.
  TEST_ASSERT_TRUE(game.offRoad());
  drive(game, now, 3000);
  TEST_ASSERT_LESS_OR_EQUAL(CircuitGame::kOffRoadTopSpeed, game.speed());
  TEST_ASSERT_GREATER_THAN(0, game.damage());
  TEST_ASSERT_LESS_THAN(50, game.damage());
}

void test_hitting_scenery_is_a_crash() {
  CircuitGame game;
  uint32_t now = race(game, kLinedTrack);
  now = drive(game, now, 2000);
  game.steer(CircuitGame::kMaxSteering);
  drive(game, now, 4000);
  TEST_ASSERT_GREATER_OR_EQUAL(1, countSounds(game, Sound::Crash));
  TEST_ASSERT_GREATER_OR_EQUAL(CircuitGame::kCrashDamage, game.damage());
}

void test_a_crash_stops_the_car_and_puts_it_back_on_the_road() {
  CircuitGame game;
  uint32_t now = race(game, kLinedTrack);
  now = drive(game, now, 2000);
  game.steer(CircuitGame::kMaxSteering);
  while (!game.crashed() && now < 20000) now = drive(game, now, 5);
  TEST_ASSERT_TRUE(game.crashed());
  TEST_ASSERT_EQUAL_INT32(0, game.speed());
  TEST_ASSERT_EQUAL(0, game.steering());
  drive(game, now, CircuitGame::kCrashRecoveryMs + 20);
  TEST_ASSERT_FALSE(game.crashed());
  TEST_ASSERT_FALSE(game.offRoad());
}

void test_running_into_a_car_bumps_and_damages() {
  CircuitGame game;
  uint32_t now = race(game, kStraightWithTraffic);
  drive(game, now, 40000);
  TEST_ASSERT_GREATER_OR_EQUAL(1, game.bumps());
  TEST_ASSERT_GREATER_OR_EQUAL(CircuitGame::kBumpDamage, game.damage());
}

void test_a_stall_cannot_pass_through_a_car() {
  // A 500 ms loop stall at top speed covers more than a car's length; the
  // sub-steps must still find the collision.
  CircuitGame stalled;
  CircuitGame smooth;
  uint32_t a = race(stalled, kStraightWithTraffic);
  uint32_t b = race(smooth, kStraightWithTraffic);
  for (uint32_t t = 0; t < 40000; t += 500) {
    stalled.tick(a + t);
  }
  drive(smooth, b, 40000);
  TEST_ASSERT_GREATER_OR_EQUAL(1, stalled.bumps());
}

void test_computer_cars_stay_on_the_road() {
  CircuitGame game;
  uint32_t now = race(game, kStraightWithTraffic);
  for (int step = 0; step < 100; ++step) {
    now = drive(game, now, 300);
    for (int i = 0; i < game.carCount(); ++i) {
      TEST_ASSERT_LESS_OR_EQUAL(CircuitTrack::kRoadHalfWidth, std::abs(game.car(i).x));
    }
  }
}

// --- Time, laps, endings -----------------------------------------------

void test_running_out_of_time_ends_the_race() {
  CircuitGame game;
  uint32_t now = race(game, kStraightTrack);
  game.setBraking(true);  // Never reaches a checkpoint.
  drive(game, now, kStraightTrack.startTimeMs + 100);
  TEST_ASSERT_EQUAL(Phase::Over, game.phase());
  TEST_ASSERT_EQUAL(EndReason::TimeUp, game.endReason());
  TEST_ASSERT_EQUAL_UINT32(0, game.timeLeftMs());
}

void test_full_damage_wrecks_the_car() {
  CircuitGame game;
  uint32_t now = race(game, kLinedTrack);
  for (int attempt = 0; attempt < 20 && game.phase() == Phase::Race; ++attempt) {
    game.steer(CircuitGame::kMaxSteering);
    now = drive(game, now, 4000);
  }
  TEST_ASSERT_EQUAL(Phase::Over, game.phase());
  TEST_ASSERT_EQUAL(EndReason::Wrecked, game.endReason());
  TEST_ASSERT_EQUAL(CircuitGame::kWrecked, game.damage());
}

void test_checkpoint_and_lap_add_time_and_record_a_lap() {
  CircuitGame game;
  uint32_t now = race(game, kShortTrack);
  const uint32_t before = game.timeLeftMs();
  now = drive(game, now, 8000);
  TEST_ASSERT_GREATER_OR_EQUAL(1, game.lapsCompleted());
  TEST_ASSERT_GREATER_THAN(0, game.bestLapMs());
  TEST_ASSERT_GREATER_THAN(before - 8000, game.timeLeftMs());
  int checkpoints = 0;
  int laps = 0;
  for (Sound s = game.takeSound(); s != Sound::None; s = game.takeSound()) {
    if (s == Sound::Checkpoint) ++checkpoints;
    if (s == Sound::Lap) ++laps;
  }
  TEST_ASSERT_GREATER_OR_EQUAL(1, checkpoints);
  TEST_ASSERT_GREATER_OR_EQUAL(1, laps);
}

void test_a_drive_replays_identically() {
  CircuitGame a;
  CircuitGame b;
  uint32_t ta = race(a, drehklang::games::kTracks[0]);
  uint32_t tb = race(b, drehklang::games::kTracks[0]);
  for (int step = 0; step < 40; ++step) {
    a.steer(step % 3 - 1);
    b.steer(step % 3 - 1);
    ta = drive(a, ta, 250);
    tb = drive(b, tb, 250);
  }
  TEST_ASSERT_EQUAL_INT32(a.z(), b.z());
  TEST_ASSERT_EQUAL_INT32(a.x(), b.x());
  TEST_ASSERT_EQUAL_INT32(a.damage(), b.damage());
}

void test_select_after_a_race_starts_clean() {
  CircuitGame game;
  uint32_t now = race(game, kStraightTrack);
  game.steer(7);
  drive(game, now, 3000);
  game.select(kStraightTrack);
  TEST_ASSERT_EQUAL(Phase::Select, game.phase());
  TEST_ASSERT_EQUAL_INT32(0, game.speed());
  TEST_ASSERT_EQUAL(0, game.steering());
  TEST_ASSERT_EQUAL(0, game.damage());
}

// --- Records -------------------------------------------------------------

void test_records_start_empty_and_keep_the_best() {
  CircuitRecords records;
  TEST_ASSERT_EQUAL_UINT32(0, records.best(0));
  TEST_ASSERT_TRUE(records.offer(0, 41000));
  TEST_ASSERT_FALSE(records.offer(0, 45000));
  TEST_ASSERT_TRUE(records.offer(0, 39500));
  TEST_ASSERT_EQUAL_UINT32(39500, records.best(0));
  TEST_ASSERT_EQUAL_UINT32(0, records.best(1));
  TEST_ASSERT_FALSE(records.offer(0, 0));  // No lap is not a record.
}

void test_lap_time_formats_as_minutes_seconds_tenths() {
  char text[12];
  CircuitRecords::format(0, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("--:--.-", text);
  CircuitRecords::format(83460, text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("1:23.4", text);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_track_length_is_its_segments);
  RUN_TEST(test_track_curve_eases_in_and_out);
  RUN_TEST(test_track_segment_index_wraps);
  RUN_TEST(test_every_shipped_track_is_a_closed_lap_of_reasonable_length);
  RUN_TEST(test_nothing_moves_before_go);
  RUN_TEST(test_countdown_beeps_three_times_then_go);
  RUN_TEST(test_throttle_is_full_from_go);
  RUN_TEST(test_braking_slows_and_letting_go_speeds_up_again);
  RUN_TEST(test_gears_rise_with_speed);
  RUN_TEST(test_dead_zone_ignores_two_detents_of_jitter);
  RUN_TEST(test_steering_beyond_the_dead_zone_moves_the_car);
  RUN_TEST(test_steering_is_clamped_and_stays_where_it_is_left);
  RUN_TEST(test_steering_already_works_in_the_countdown);
  RUN_TEST(test_a_bend_pushes_an_unsteered_car_outward);
  RUN_TEST(test_steering_into_a_bend_holds_the_line);
  RUN_TEST(test_off_road_caps_speed_and_costs_damage);
  RUN_TEST(test_hitting_scenery_is_a_crash);
  RUN_TEST(test_a_crash_stops_the_car_and_puts_it_back_on_the_road);
  RUN_TEST(test_running_into_a_car_bumps_and_damages);
  RUN_TEST(test_a_stall_cannot_pass_through_a_car);
  RUN_TEST(test_computer_cars_stay_on_the_road);
  RUN_TEST(test_running_out_of_time_ends_the_race);
  RUN_TEST(test_full_damage_wrecks_the_car);
  RUN_TEST(test_checkpoint_and_lap_add_time_and_record_a_lap);
  RUN_TEST(test_a_drive_replays_identically);
  RUN_TEST(test_select_after_a_race_starts_clean);
  RUN_TEST(test_records_start_empty_and_keep_the_best);
  RUN_TEST(test_lap_time_formats_as_minutes_seconds_tenths);
  return UNITY_END();
}
