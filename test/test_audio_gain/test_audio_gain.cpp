#include <unity.h>

#include <cmath>
#include <cstdint>

#include "AudioGain.h"

using drehklang::playback::AudioGain;

void setUp() {}
void tearDown() {}

void test_output_gain_scales_a_full_scale_sample() {
  // After the library's volume: only the sleep-timer fade applies.
  TEST_ASSERT_EQUAL_INT32(1 << 28, AudioGain::applyOutputGain(1 << 28, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT32(1 << 27, AudioGain::applyOutputGain(1 << 28, AudioGain::kUnityOutputGain / 2));
  TEST_ASSERT_EQUAL_INT32(0, AudioGain::applyOutputGain(1 << 28, 0));
  TEST_ASSERT_EQUAL_INT32(-(1 << 27), AudioGain::applyOutputGain(-(1 << 28), AudioGain::kUnityOutputGain / 2));
}

void test_output_gain_never_overflows_or_amplifies() {
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, AudioGain::applyOutputGain(INT32_MAX, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, AudioGain::applyOutputGain(INT32_MIN, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT32(1000, AudioGain::applyOutputGain(1000, 60000));
}

void test_volume_curve_is_unity_at_the_top_and_follows_the_table() {
  // The library calls the curve with t = volume / 21.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, AudioGain::volumeCurveDb(1.0f));
  for (uint8_t step = 1; step <= AudioGain::kMaxVolumeStep; ++step) {
    const float t = static_cast<float>(step) / AudioGain::kMaxVolumeStep;
    const float linear = std::pow(10.0f, AudioGain::volumeCurveDb(t) / 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, AudioGain::kVolumeTable[step] / 64.0f, linear);
  }
}

void test_volume_curve_rises_with_every_step() {
  for (uint8_t step = 2; step <= AudioGain::kMaxVolumeStep; ++step) {
    TEST_ASSERT_TRUE(AudioGain::volumeCurveDb(static_cast<float>(step) / AudioGain::kMaxVolumeStep) >
                     AudioGain::volumeCurveDb(static_cast<float>(step - 1) / AudioGain::kMaxVolumeStep));
  }
}

void test_volume_curve_clamps_out_of_range_input() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, AudioGain::volumeCurveDb(2.0f));
  // Below the first step it stays finite rather than taking log10(0).
  TEST_ASSERT_TRUE(std::isfinite(AudioGain::volumeCurveDb(-1.0f)));
}

void test_blip_volume_step_21_is_unity_and_step_0_is_silence() {
  TEST_ASSERT_EQUAL_INT16(
      1000, AudioGain::applyVolume(1000, AudioGain::kMaxVolumeStep, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT16(0, AudioGain::applyVolume(1000, 0, AudioGain::kUnityOutputGain));
  // Out-of-range steps clamp to the loudest entry, never index past it.
  TEST_ASSERT_EQUAL_INT16(1000, AudioGain::applyVolume(1000, 200, AudioGain::kUnityOutputGain));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_output_gain_scales_a_full_scale_sample);
  RUN_TEST(test_output_gain_never_overflows_or_amplifies);
  RUN_TEST(test_volume_curve_is_unity_at_the_top_and_follows_the_table);
  RUN_TEST(test_volume_curve_rises_with_every_step);
  RUN_TEST(test_volume_curve_clamps_out_of_range_input);
  RUN_TEST(test_blip_volume_step_21_is_unity_and_step_0_is_silence);
  return UNITY_END();
}
