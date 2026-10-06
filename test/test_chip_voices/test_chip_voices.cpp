#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "ChipVoices.h"

using drehklang::playback::ChipEffect;
using drehklang::playback::ChipVoices;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kRate = 22050;

// Sign changes over `samples`, which is twice the pitch for a square or
// pulse wave over one second.
int countCrossings(ChipVoices &chip, uint32_t samples) {
  int crossings = 0;
  int16_t last = chip.nextSample(kRate);
  for (uint32_t i = 1; i < samples; ++i) {
    const int16_t s = chip.nextSample(kRate);
    if ((s > 0) != (last > 0) && s != 0 && last != 0) ++crossings;
    last = s;
  }
  return crossings;
}

void settle(ChipVoices &chip, uint32_t samples) {
  for (uint32_t i = 0; i < samples; ++i) chip.nextSample(kRate);
}

}  // namespace

void test_silent_until_asked() {
  ChipVoices chip;
  TEST_ASSERT_FALSE(chip.active());
  for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL_INT16(0, chip.nextSample(kRate));
}

void test_engine_sounds_at_its_pitch() {
  ChipVoices chip;
  chip.setEngine(200, 4000);
  settle(chip, kRate);  // Past the glide from silence.
  const int crossings = countCrossings(chip, kRate);
  TEST_ASSERT_INT_WITHIN(8, 400, crossings);
}

void test_engine_glides_without_restarting() {
  // A pitch change mid-cycle must not reset the phase: a restart would
  // flip the wave back high at once, every time it is asked.
  ChipVoices chip;
  chip.setEngine(100, 4000);
  settle(chip, 1000);
  int16_t last = chip.nextSample(kRate);
  chip.setEngine(180, 4000);
  int switches = 0;
  for (int i = 0; i < 200; ++i) {
    const int16_t s = chip.nextSample(kRate);
    if ((s > 0) != (last > 0)) ++switches;
    last = s;
  }
  // 200 samples at 100..180 Hz is at most ~4 half cycles, so at most a
  // handful of edges -- a restart storm would show dozens.
  TEST_ASSERT_LESS_OR_EQUAL(6, switches);
}

void test_effect_decays_to_silence_within_its_duration() {
  ChipVoices chip;
  chip.play(ChipEffect{880, 880, 100, 6000, false});
  TEST_ASSERT_TRUE(chip.active());
  settle(chip, kRate * 100 / 1000 + 10);
  TEST_ASSERT_FALSE(chip.active());
  TEST_ASSERT_EQUAL_INT16(0, chip.nextSample(kRate));
}

void test_effect_starts_loud_and_ends_quiet() {
  ChipVoices chip;
  chip.play(ChipEffect{440, 440, 200, 6000, false});
  int16_t early = 0;
  for (int i = 0; i < 200; ++i) early = std::max<int16_t>(early, std::abs(chip.nextSample(kRate)));
  settle(chip, kRate * 150 / 1000);
  int16_t late = 0;
  for (int i = 0; i < 200; ++i) late = std::max<int16_t>(late, std::abs(chip.nextSample(kRate)));
  TEST_ASSERT_GREATER_THAN(5000, early);
  TEST_ASSERT_LESS_THAN(early / 2, late);
}

void test_sweep_ends_near_its_target() {
  ChipVoices chip;
  chip.play(ChipEffect{1000, 200, 1000, 6000, false});
  settle(chip, kRate * 900 / 1000);
  // The last 100 ms sit near 200..280 Hz: count crossings over 50 ms.
  const int crossings = countCrossings(chip, kRate / 20);
  TEST_ASSERT_INT_WITHIN(6, 22, crossings);
}

void test_full_mix_never_wraps() {
  ChipVoices chip;
  chip.setEngine(150, 32767);
  chip.setNoise(3000, 32767);
  chip.play(ChipEffect{500, 500, 1000, 32767, false});
  for (int i = 0; i < 5000; ++i) {
    const int16_t s = chip.nextSample(kRate);
    TEST_ASSERT_TRUE(s >= -32767 && s <= 32767);
  }
}

void test_silence_stops_everything_at_once() {
  ChipVoices chip;
  chip.setEngine(150, 4000);
  chip.setNoise(3000, 4000);
  chip.play(ChipEffect{500, 500, 1000, 6000, true});
  settle(chip, 100);
  chip.silence();
  TEST_ASSERT_FALSE(chip.active());
  TEST_ASSERT_EQUAL_INT16(0, chip.nextSample(kRate));
}

void test_noise_is_not_a_tone() {
  ChipVoices chip;
  chip.setNoise(4000, 4000);
  settle(chip, 100);
  // An LFSR at 4 kHz changes sign irregularly: far fewer crossings than
  // a 4 kHz square (8000/s), but plenty.
  const int crossings = countCrossings(chip, kRate);
  TEST_ASSERT_GREATER_THAN(500, crossings);
  TEST_ASSERT_LESS_THAN(7000, crossings);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_silent_until_asked);
  RUN_TEST(test_engine_sounds_at_its_pitch);
  RUN_TEST(test_engine_glides_without_restarting);
  RUN_TEST(test_effect_decays_to_silence_within_its_duration);
  RUN_TEST(test_effect_starts_loud_and_ends_quiet);
  RUN_TEST(test_sweep_ends_near_its_target);
  RUN_TEST(test_full_mix_never_wraps);
  RUN_TEST(test_silence_stops_everything_at_once);
  RUN_TEST(test_noise_is_not_a_tone);
  return UNITY_END();
}
