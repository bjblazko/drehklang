#include <unity.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <vector>

#include "StereoLevels.h"

using drehklang::signal::LevelHold;
using drehklang::signal::StereoLevelMeter;
using drehklang::signal::StereoLevels;

void setUp() {}
void tearDown() {}

namespace {

constexpr double kFull = 2147483648.0;  // 2^31

// Interleaved stereo, `frames` long, from two per-frame functions.
template <typename L, typename R>
std::vector<int32_t> stereo(size_t frames, L left, R right) {
  std::vector<int32_t> out;
  out.reserve(frames * 2);
  for (size_t i = 0; i < frames; ++i) {
    out.push_back(left(i));
    out.push_back(right(i));
  }
  return out;
}

int32_t sineAt(size_t i, double amplitude) {
  const double s = std::sin(2.0 * M_PI * 1000.0 * static_cast<double>(i) / 48000.0);
  return static_cast<int32_t>(s * amplitude * kFull);
}

int32_t constantAt(double value) { return static_cast<int32_t>(value * (kFull - 1)); }

}  // namespace

void test_full_scale_square_on_left_only() {
  StereoLevelMeter meter;
  auto block = stereo(
      64, [](size_t i) { return i % 2 ? INT32_MAX : -INT32_MAX; }, [](size_t) { return 0; });
  meter.noteBlock(block.data(), block.size(), 1.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_TRUE(levels.fresh);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, levels.rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, levels.rms[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, levels.peak[0]);
}

void test_sine_half_scale_rms() {
  StereoLevelMeter meter;
  auto block = stereo(
      4800, [](size_t i) { return sineAt(i, 0.5); }, [](size_t i) { return sineAt(i, 0.5); });
  meter.noteBlock(block.data(), block.size(), 1.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.35355f, levels.rms[0]);
  TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.35355f, levels.rms[1]);
}

void test_gain_scales_levels() {
  StereoLevelMeter meter;
  auto block = stereo(
      4800, [](size_t i) { return sineAt(i, 0.5); }, [](size_t i) { return sineAt(i, 0.5); });
  meter.noteBlock(block.data(), block.size(), 2.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_FLOAT_WITHIN(4e-3f, 0.7071f, levels.rms[0]);
  TEST_ASSERT_FLOAT_WITHIN(4e-3f, 1.0f, levels.peak[0]);
}

void test_identical_channels_read_alike() {
  StereoLevelMeter meter;
  auto block = stereo(
      1000, [](size_t i) { return sineAt(i, 0.3); }, [](size_t i) { return sineAt(i, 0.3); });
  meter.noteBlock(block.data(), block.size(), 1.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_EQUAL_FLOAT(levels.rms[0], levels.rms[1]);
  TEST_ASSERT_EQUAL_FLOAT(levels.peak[0], levels.peak[1]);
}

void test_peak_is_max_since_take_and_resets() {
  StereoLevelMeter meter;
  auto loud = stereo(
      16, [](size_t) { return constantAt(0.8); }, [](size_t) { return constantAt(0.8); });
  auto quiet = stereo(
      16, [](size_t) { return constantAt(0.3); }, [](size_t) { return constantAt(0.3); });
  meter.noteBlock(loud.data(), loud.size(), 1.0f);
  meter.noteBlock(quiet.data(), quiet.size(), 1.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.8f, levels.peak[0]);

  StereoLevels again = meter.take();
  TEST_ASSERT_FALSE(again.fresh);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, again.peak[0]);
}

// The RMS covers every block since the last read, not only the latest:
// one 5.8 ms block per frame swings with the phase of a bass note.
void test_rms_covers_every_block_since_take() {
  StereoLevelMeter meter;
  auto loud = stereo(
      16, [](size_t) { return constantAt(0.8); }, [](size_t) { return constantAt(0.8); });
  auto quiet = stereo(
      16, [](size_t) { return constantAt(0.3); }, [](size_t) { return constantAt(0.3); });
  meter.noteBlock(loud.data(), loud.size(), 1.0f);
  meter.noteBlock(quiet.data(), quiet.size(), 1.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.60415f, levels.rms[0]);  // sqrt((.64 + .09) / 2)
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.60415f, levels.rms[1]);
}

// Unread for a while, the meter stops measuring (nothing is shown), and
// the first read after that discards what it had: no stale peak flashes
// when the view opens or the display wakes.
void test_unread_meter_stops_and_discards_stale_levels() {
  StereoLevelMeter meter;
  auto loud = stereo(
      16, [](size_t) { return constantAt(0.9); }, [](size_t) { return constantAt(0.9); });
  auto quiet = stereo(
      16, [](size_t) { return constantAt(0.3); }, [](size_t) { return constantAt(0.3); });
  for (uint32_t i = 0; i < StereoLevelMeter::kIdleBlocks + 5; ++i) {
    meter.noteBlock(loud.data(), loud.size(), 1.0f);
  }
  StereoLevels stale = meter.take();
  TEST_ASSERT_FALSE(stale.fresh);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, stale.peak[0]);

  meter.noteBlock(quiet.data(), quiet.size(), 1.0f);
  StereoLevels now = meter.take();
  TEST_ASSERT_TRUE(now.fresh);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, now.peak[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.3f, now.rms[0]);
}

void test_most_negative_sample_reads_as_full_scale() {
  StereoLevelMeter meter;
  int32_t words[2] = {INT32_MIN, INT32_MIN};
  meter.noteBlock(words, 2, 1.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, levels.peak[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, levels.rms[0]);
}

void test_odd_word_is_ignored() {
  StereoLevelMeter meter;
  int32_t words[3] = {constantAt(0.5), constantAt(0.25), constantAt(1.0)};
  meter.noteBlock(words, 3, 1.0f);
  StereoLevels levels = meter.take();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, levels.rms[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.25f, levels.rms[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, levels.peak[0]);
}

namespace {
StereoLevels freshAt(float value) {
  StereoLevels l;
  l.rms[0] = l.rms[1] = l.peak[0] = l.peak[1] = value;
  l.fresh = true;
  return l;
}
}  // namespace

void test_hold_keeps_last_fresh_until_stale() {
  LevelHold hold;
  hold.apply(freshAt(0.5f), 16);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, hold.apply(StereoLevels{}, 100).rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, hold.apply(StereoLevels{}, 100).rms[0]);
  StereoLevels gone = hold.apply(StereoLevels{}, 60);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, gone.rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, gone.peak[1]);
}

void test_hold_survives_slow_blocks() {
  LevelHold hold;
  hold.apply(freshAt(0.4f), 16);
  TEST_ASSERT_EQUAL_FLOAT(0.4f, hold.apply(StereoLevels{}, 200).rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.6f, hold.apply(freshAt(0.6f), 16).rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.6f, hold.apply(StereoLevels{}, 200).rms[1]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_full_scale_square_on_left_only);
  RUN_TEST(test_sine_half_scale_rms);
  RUN_TEST(test_gain_scales_levels);
  RUN_TEST(test_identical_channels_read_alike);
  RUN_TEST(test_peak_is_max_since_take_and_resets);
  RUN_TEST(test_rms_covers_every_block_since_take);
  RUN_TEST(test_unread_meter_stops_and_discards_stale_levels);
  RUN_TEST(test_most_negative_sample_reads_as_full_scale);
  RUN_TEST(test_odd_word_is_ignored);
  RUN_TEST(test_hold_keeps_last_fresh_until_stale);
  RUN_TEST(test_hold_survives_slow_blocks);
  return UNITY_END();
}
