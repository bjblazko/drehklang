#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "GraphicEqualizer.h"

using drehklang::signal::GraphicEqualizer;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRate = 48000;
constexpr double kPi = 3.141592653589793;
constexpr size_t kFrames = 16384;

// Interleaved stereo, full scale = 2^31, both channels the same sine.
std::vector<int32_t> sine(float hz, double amplitude, uint32_t rate = kRate) {
  std::vector<int32_t> s(kFrames * 2);
  for (size_t i = 0; i < kFrames; ++i) {
    const auto v = static_cast<int32_t>(
        std::lround(amplitude * 2147483647.0 * std::sin(2.0 * kPi * hz * i / rate)));
    s[2 * i] = v;
    s[2 * i + 1] = v;
  }
  return s;
}

// RMS of the left channel over the second half, once the filters settled.
float rmsDb(const std::vector<int32_t> &s) {
  double sum = 0;
  size_t n = 0;
  for (size_t i = kFrames / 2; i < kFrames; ++i, ++n) {
    const float v = s[2 * i] / 2147483648.0;
    sum += v * v;
  }
  return static_cast<float>(10.0 * std::log10(sum / n));
}

// The equalizer's gain at `hz`, in dB: output level over input level.
float gainAt(GraphicEqualizer &eq, float hz, uint32_t rate = kRate) {
  auto s = sine(hz, 0.1, rate);
  const float before = rmsDb(s);
  // In chunks, the way the decoder hands them over.
  for (size_t i = 0; i < s.size(); i += 512) eq.process(s.data() + i, 512, rate);
  return rmsDb(s) - before;
}
}  // namespace

void test_flat_leaves_every_sample_untouched() {
  GraphicEqualizer eq;
  auto s = sine(1000, 0.5);
  const auto original = s;
  eq.process(s.data(), s.size(), kRate);
  TEST_ASSERT_TRUE(eq.flat());
  TEST_ASSERT_TRUE(s == original);
}

void test_gains_are_held_to_twelve_db() {
  GraphicEqualizer eq;
  eq.setGain(0, 30);
  eq.setGain(6, -30);
  TEST_ASSERT_EQUAL_INT(12, eq.gain(0));
  TEST_ASSERT_EQUAL_INT(-12, eq.gain(6));
}

void test_a_band_lifts_its_frequency_against_the_rest() {
  GraphicEqualizer eq;
  eq.setGain(3, 6);  // 1 kHz.
  const float atBand = gainAt(eq, 1000);
  const float far = gainAt(eq, 100);
  TEST_ASSERT_FLOAT_WITHIN(1.0, 6.0, atBand - far);
  // Lifted, the rest is lowered instead of the band clipping -- by the
  // lift, not more.
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 0.0f, atBand);
}

void test_the_lowest_band_reaches_deep_bass_only() {
  GraphicEqualizer eq;
  eq.setGain(0, 12);  // 31 Hz.
  const float at31 = gainAt(eq, 31);
  const float at1k = gainAt(eq, 1000);
  const float at300 = gainAt(eq, 300);
  TEST_ASSERT_FLOAT_WITHIN(1.5, 12.0, at31 - at1k);
  TEST_ASSERT_FLOAT_WITHIN(1.5, 0.0, at300 - at1k);
}

void test_the_top_band_lifts_the_air() {
  GraphicEqualizer eq;
  eq.setGain(6, 6);  // 16 kHz shelf.
  TEST_ASSERT_TRUE(gainAt(eq, 19000) - gainAt(eq, 1000) > 4.5);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 0.0, gainAt(eq, 3000) - gainAt(eq, 1000));
}

void test_a_cut_cuts() {
  GraphicEqualizer eq;
  eq.setGain(4, -12);  // 3 kHz.
  TEST_ASSERT_FLOAT_WITHIN(1.5, -12.0, gainAt(eq, 3000) - gainAt(eq, 300));
}

void test_everything_lifted_still_does_not_clip() {
  GraphicEqualizer eq;
  for (size_t b = 0; b < GraphicEqualizer::kBands; ++b) eq.setGain(b, 12);
  for (float hz : {31.0f, 60.0f, 300.0f, 650.0f, 5000.0f, 12000.0f}) {
    auto s = sine(hz, 0.99);
    eq.process(s.data(), s.size(), kRate);
    int64_t peak = 0;
    for (size_t i = kFrames / 2; i < kFrames; ++i) {
      peak = std::max<int64_t>(peak, std::llabs(static_cast<int64_t>(s[2 * i])));
    }
    TEST_ASSERT_TRUE(peak < 2147483647LL);
  }
}

void test_a_band_above_half_the_rate_is_left_out() {
  // A 22.05 kHz audiobook has no 16 kHz: that band does nothing there,
  // and nothing turns into noise or NaN.
  constexpr uint32_t kLowRate = 22050;
  GraphicEqualizer eq;
  eq.setGain(6, 12);
  const float at5k = gainAt(eq, 5000, kLowRate);
  const float at1k = gainAt(eq, 1000, kLowRate);
  TEST_ASSERT_TRUE(std::isfinite(at5k));
  TEST_ASSERT_FLOAT_WITHIN(0.5, 0.0, at5k - at1k);
}

void test_back_to_flat_is_untouched_again() {
  GraphicEqualizer eq;
  eq.setGain(2, 5);
  auto s = sine(300, 0.3);
  eq.process(s.data(), s.size(), kRate);
  eq.setGain(2, 0);
  auto t = sine(300, 0.3);
  const auto original = t;
  eq.process(t.data(), t.size(), kRate);
  TEST_ASSERT_TRUE(t == original);
}

void test_headroom_is_the_lift_and_nothing_when_flat() {
  GraphicEqualizer eq;
  auto s = sine(1000, 0.1);
  eq.process(s.data(), s.size(), kRate);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, eq.headroom());
  eq.setGain(0, 12);  // Everything else is lowered by ~12 dB.
  eq.process(s.data(), s.size(), kRate);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 12.0f, 20.0f * std::log10(eq.headroom()));
}

void test_with_the_headroom_given_back_the_rest_keeps_its_level() {
  // At every volume (user, 2026-10-09): signal::PeakLimiter gives all of
  // the headroom back after the volume and only dips for a peak.
  GraphicEqualizer eq;
  eq.setGain(0, 12);
  const float at1k = gainAt(eq, 1000);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 0.0f, at1k + 20.0f * std::log10(eq.headroom()));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_flat_leaves_every_sample_untouched);
  RUN_TEST(test_gains_are_held_to_twelve_db);
  RUN_TEST(test_a_band_lifts_its_frequency_against_the_rest);
  RUN_TEST(test_the_lowest_band_reaches_deep_bass_only);
  RUN_TEST(test_the_top_band_lifts_the_air);
  RUN_TEST(test_a_cut_cuts);
  RUN_TEST(test_everything_lifted_still_does_not_clip);
  RUN_TEST(test_a_band_above_half_the_rate_is_left_out);
  RUN_TEST(test_back_to_flat_is_untouched_again);
  RUN_TEST(test_headroom_is_the_lift_and_nothing_when_flat);
  RUN_TEST(test_with_the_headroom_given_back_the_rest_keeps_its_level);
  return UNITY_END();
}
