#include <unity.h>

#include "BarMeter.h"

namespace bars = drehklang::visualizer::bars;

void setUp() {}
void tearDown() {}

void test_scale_points() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, bars::scalePosition(-50));
  TEST_ASSERT_EQUAL_FLOAT(0.25f, bars::scalePosition(-30));
  TEST_ASSERT_EQUAL_FLOAT(0.42f, bars::scalePosition(-20));
  TEST_ASSERT_EQUAL_FLOAT(0.75f, bars::scalePosition(-6));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, bars::scalePosition(0));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, bars::scalePosition(-60));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, bars::scalePosition(6));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.335f, bars::scalePosition(-25));
}

void test_red_from_27() { TEST_ASSERT_EQUAL_INT(27, bars::redFromSegment()); }

void test_instant_attack() {
  bars::BarMeter meter;
  meter.update(1.0f, 16);
  TEST_ASSERT_EQUAL_INT(32, meter.litSegments());
  TEST_ASSERT_EQUAL_INT(31, meter.holdSegment());
}

void test_release_rate() {
  bars::BarMeter meter;
  meter.update(1.0f, 16);
  for (int t = 0; t < 1000; t += 16) meter.update(0.0f, 16);
  TEST_ASSERT_TRUE(meter.litSegments() >= 14 && meter.litSegments() <= 15);
}

void test_hold_stays_1500ms_then_falls() {
  bars::BarMeter meter;
  meter.update(1.0f, 10);
  int t = 0;
  for (; t < 1490; t += 10) {
    meter.update(0.0f, 10);
    TEST_ASSERT_TRUE(meter.holdSegment() >= meter.litSegments() - 1);
  }
  TEST_ASSERT_EQUAL_INT(31, meter.holdSegment());
  for (; t < 1700; t += 10) {
    meter.update(0.0f, 10);
    TEST_ASSERT_TRUE(meter.holdSegment() >= meter.litSegments() - 1);
  }
  TEST_ASSERT_TRUE(meter.holdSegment() < 31);
}

void test_over_full_scale_clamps() {
  bars::BarMeter meter;
  meter.update(2.0f, 16);
  TEST_ASSERT_EQUAL_INT(32, meter.litSegments());
  TEST_ASSERT_EQUAL_INT(31, meter.holdSegment());
}

void test_silence_is_dark() {
  bars::BarMeter meter;
  meter.update(0.0f, 16);
  TEST_ASSERT_EQUAL_INT(0, meter.litSegments());
  TEST_ASSERT_EQUAL_INT(-1, meter.holdSegment());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_scale_points);
  RUN_TEST(test_red_from_27);
  RUN_TEST(test_instant_attack);
  RUN_TEST(test_release_rate);
  RUN_TEST(test_hold_stays_1500ms_then_falls);
  RUN_TEST(test_over_full_scale_clamps);
  RUN_TEST(test_silence_is_dark);
  return UNITY_END();
}
