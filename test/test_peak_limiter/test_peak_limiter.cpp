#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "PeakLimiter.h"

using drehklang::signal::PeakLimiter;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kRate = 44100;

// Interleaved stereo sine at `level` of full scale.
std::vector<int32_t> sine(float hz, float level, int frames) {
  std::vector<int32_t> out(frames * 2);
  for (int i = 0; i < frames; ++i) {
    const float v = level * 2147483647.0f * std::sin(2.0f * 3.14159265f * hz * i / kRate);
    out[2 * i] = out[2 * i + 1] = static_cast<int32_t>(v);
  }
  return out;
}

int32_t peakOf(const std::vector<int32_t> &s, size_t from = 0) {
  int64_t peak = 0;
  for (size_t i = from; i < s.size(); ++i) peak = std::max<int64_t>(peak, std::llabs(s[i]));
  return static_cast<int32_t>(peak);
}

}  // namespace

void test_unity_gain_leaves_samples_untouched() {
  PeakLimiter limiter;
  auto samples = sine(440, 0.9f, 4000);
  const auto original = samples;
  limiter.process(samples.data(), samples.size(), 1.0f, kRate);
  TEST_ASSERT_TRUE(samples == original);
}

void test_quiet_music_gets_the_whole_gain() {
  // The equalizer's headroom given back in full: the music that was not
  // lifted is exactly as loud as without the equalizer (user, 2026-10-09).
  // Measured once settled: a lift fades in over the release time rather
  // than jumping.
  PeakLimiter limiter;
  auto samples = sine(1000, 0.1f, kRate / 2);
  const int32_t before = peakOf(samples);
  limiter.process(samples.data(), samples.size(), 4.0f, kRate);
  TEST_ASSERT_INT_WITHIN(before / 100, before * 4, peakOf(samples, samples.size() / 2));
}

void test_nothing_ever_passes_the_ceiling() {
  PeakLimiter limiter;
  auto samples = sine(60, 0.9f, 20000);
  limiter.process(samples.data(), samples.size(), 4.0f, kRate);
  TEST_ASSERT_LESS_OR_EQUAL(PeakLimiter::kCeiling, peakOf(samples));
}

void test_the_gain_comes_back_after_a_loud_passage() {
  PeakLimiter limiter;
  auto loud = sine(60, 0.9f, kRate / 2);
  limiter.process(loud.data(), loud.size(), 4.0f, kRate);
  // Half a second of quiet music afterwards: back to the full gain.
  auto quiet = sine(1000, 0.1f, kRate / 2);
  const int32_t before = peakOf(quiet);
  limiter.process(quiet.data(), quiet.size(), 4.0f, kRate);
  TEST_ASSERT_INT_WITHIN(before / 20, before * 4, peakOf(quiet, quiet.size() / 2));
}

void test_both_channels_are_limited_together() {
  // A loud left must not shift the stereo image by limiting left alone.
  PeakLimiter limiter;
  std::vector<int32_t> samples(2000);
  for (size_t i = 0; i < samples.size(); i += 2) {
    samples[i] = 1800000000;  // left: loud
    samples[i + 1] = 100000000;  // right: quiet
  }
  limiter.process(samples.data(), samples.size(), 4.0f, kRate);
  const double ratio = static_cast<double>(samples[1000]) / samples[1001];
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 18.0f, static_cast<float>(ratio));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_unity_gain_leaves_samples_untouched);
  RUN_TEST(test_quiet_music_gets_the_whole_gain);
  RUN_TEST(test_nothing_ever_passes_the_ceiling);
  RUN_TEST(test_the_gain_comes_back_after_a_loud_passage);
  RUN_TEST(test_both_channels_are_limited_together);
  return UNITY_END();
}
