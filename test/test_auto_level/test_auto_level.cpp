#include <unity.h>

#include <cmath>

#include "AutoLevel.h"

using drehklang::signal::StereoLevels;
using drehklang::visualizer::AutoLevel;

void setUp() {}
void tearDown() {}

namespace {

float fromDb(float db) { return std::pow(10.0f, db / 20.0f); }
float toDb(float linear) { return 20.0f * std::log10(linear); }

StereoLevels levels(float rmsDb, float peakDb) {
  StereoLevels in;
  in.rms[0] = in.rms[1] = fromDb(rmsDb);
  in.peak[0] = in.peak[1] = fromDb(peakDb);
  in.fresh = true;
  return in;
}

StereoLevels silence() {
  StereoLevels in;
  in.fresh = true;
  return in;
}

// Frames of 33 ms, as the view draws them.
StereoLevels play(AutoLevel &level, const StereoLevels &in, uint32_t ms) {
  StereoLevels out;
  for (uint32_t t = 0; t < ms; t += 33) out = level.apply(in, 33);
  return out;
}

}  // namespace

// "Big Music": loud passages at RMS -6 dBFS sit 8 dB past 0 VU.
void test_a_loud_track_is_brought_to_zero_vu() {
  AutoLevel level;
  level.startTrack();
  const StereoLevels out = play(level, levels(-6.0f, -0.5f), 8000);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, AutoLevel::kRmsTargetDb, toDb(out.rms[0]));
  TEST_ASSERT_FLOAT_WITHIN(0.3f, AutoLevel::kRmsTargetDb, toDb(out.rms[1]));
}

void test_peaks_land_just_below_the_red() {
  AutoLevel level;
  level.startTrack();
  const StereoLevels out = play(level, levels(-6.0f, -0.5f), 8000);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, AutoLevel::kPeakTargetDb, toDb(out.peak[0]));
}

void test_a_quiet_track_is_raised_but_no_more_than_the_limit() {
  AutoLevel level;
  level.startTrack();
  const StereoLevels out = play(level, levels(-45.0f, -35.0f), 8000);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, -45.0f + AutoLevel::kMaxGainDb, toDb(out.rms[0]));
}

// Fixed for the track once learned: the dynamics within it stay visible.
void test_a_louder_chorus_after_learning_shows_louder() {
  AutoLevel level;
  level.startTrack();
  play(level, levels(-20.0f, -10.0f), 8000);
  const StereoLevels out = play(level, levels(-14.0f, -4.0f), 2000);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, AutoLevel::kRmsTargetDb + 6.0f, toDb(out.rms[0]));
}

// A quiet intro does not use up the learning time.
void test_silence_does_not_count_as_learning() {
  AutoLevel level;
  level.startTrack();
  play(level, silence(), 10000);
  const StereoLevels out = play(level, levels(-6.0f, -0.5f), 8000);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, AutoLevel::kRmsTargetDb, toDb(out.rms[0]));
}

// The next track starts at the last one's level, then learns its own.
void test_the_next_track_starts_at_the_last_level_then_learns() {
  AutoLevel level;
  level.startTrack();
  play(level, levels(-6.0f, -0.5f), 8000);
  level.startTrack();
  const StereoLevels early = play(level, levels(-20.0f, -10.0f), 330);
  // The last track's gain: -6 dBFS to the target.
  TEST_ASSERT_FLOAT_WITHIN(0.3f, -20.0f + (AutoLevel::kRmsTargetDb + 6.0f), toDb(early.rms[0]));
  const StereoLevels late = play(level, levels(-20.0f, -10.0f), 8000);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, AutoLevel::kRmsTargetDb, toDb(late.rms[0]));
}

// One snare hit is not the track's level.
void test_a_single_loud_frame_does_not_set_the_level() {
  AutoLevel level;
  level.startTrack();
  play(level, levels(-20.0f, -10.0f), 3000);
  play(level, levels(-2.0f, -0.1f), 33);
  const StereoLevels out = play(level, levels(-20.0f, -10.0f), 5000);
  TEST_ASSERT_TRUE(toDb(out.rms[0]) > AutoLevel::kRmsTargetDb - 3.0f);
}

void test_silence_stays_silent() {
  AutoLevel level;
  level.startTrack();
  const StereoLevels out = play(level, silence(), 1000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, out.rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, out.peak[0]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_loud_track_is_brought_to_zero_vu);
  RUN_TEST(test_peaks_land_just_below_the_red);
  RUN_TEST(test_a_quiet_track_is_raised_but_no_more_than_the_limit);
  RUN_TEST(test_a_louder_chorus_after_learning_shows_louder);
  RUN_TEST(test_silence_does_not_count_as_learning);
  RUN_TEST(test_the_next_track_starts_at_the_last_level_then_learns);
  RUN_TEST(test_a_single_loud_frame_does_not_set_the_level);
  RUN_TEST(test_silence_stays_silent);
  return UNITY_END();
}
