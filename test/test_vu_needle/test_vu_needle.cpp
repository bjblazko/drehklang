#include <unity.h>

#include <cmath>

#include "VuNeedle.h"

namespace vu = drehklang::visualizer::vu;

void setUp() {}
void tearDown() {}

namespace {
constexpr float kZeroVuPosition = 1.0f / 1.4125375f;  // 0.70795
}

void test_target_at_zero_vu() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, kZeroVuPosition, vu::needleTarget(0.19953f, 0.19953f));
}

void test_target_averages_linear() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.35397f, vu::needleTarget(0.19953f, 0.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, vu::needleTarget(0.0f, 0.0f));
}

void test_rise_reaches_99_percent_in_300ms() {
  vu::NeedleBallistics needle;
  int reachedAt = -1;
  for (int ms = 1; ms <= 1000 && reachedAt < 0; ++ms) {
    needle.update(kZeroVuPosition, 1);
    if (needle.position() >= 0.99f * kZeroVuPosition) reachedAt = ms;
  }
  // The approved preview (omega 20) gets there in ~198 ms; the IEC 268-17
  // 300 ms is the upper bound.
  TEST_ASSERT_TRUE(reachedAt >= 180);
  TEST_ASSERT_TRUE(reachedAt <= 300);
}

void test_overshoot_between_1_and_2_percent() {
  vu::NeedleBallistics needle;
  float most = 0;
  for (int ms = 0; ms < 1000; ++ms) {
    needle.update(kZeroVuPosition, 1);
    most = std::fmax(most, needle.position());
  }
  TEST_ASSERT_TRUE(most > 1.01f * kZeroVuPosition);
  TEST_ASSERT_TRUE(most < 1.02f * kZeroVuPosition);
}

void test_pinned_at_stop() {
  vu::NeedleBallistics needle;
  for (int ms = 0; ms < 1000; ++ms) {
    needle.update(1.2f, 1);
    TEST_ASSERT_TRUE(needle.position() <= 1.06f);
  }
  TEST_ASSERT_TRUE(needle.position() >= 1.05f);
}

void test_long_stall_stays_finite() {
  vu::NeedleBallistics needle;
  needle.update(1.0f, 5000);
  TEST_ASSERT_TRUE(std::isfinite(needle.position()));
  TEST_ASSERT_TRUE(needle.position() >= 0.0f && needle.position() <= 1.06f);
  needle.update(0.0f, 5000);
  TEST_ASSERT_TRUE(std::isfinite(needle.position()));
  TEST_ASSERT_TRUE(needle.position() >= 0.0f && needle.position() <= 1.06f);
}

void test_falls_back_on_silence() {
  vu::NeedleBallistics needle;
  for (int ms = 0; ms < 1000; ++ms) needle.update(1.0f, 1);
  for (int ms = 0; ms < 600; ++ms) needle.update(0.0f, 1);
  TEST_ASSERT_TRUE(needle.position() < 0.01f);
}

void test_lamp_lights_and_fades() {
  vu::PeakLamp lamp;
  lamp.update(0.95f, 0.0f, 16);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, lamp.brightness());
  for (int ms = 0; ms < 175; ++ms) lamp.update(0, 0, 1);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, lamp.brightness());
  for (int ms = 0; ms < 200; ++ms) lamp.update(0, 0, 1);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, lamp.brightness());
}

void test_lamp_stays_dark_below_threshold() {
  vu::PeakLamp lamp;
  lamp.update(0.88f, 0.88f, 16);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, lamp.brightness());
}

namespace {
vu::NeedlePoint tipMid(float position) {
  const auto poly = vu::needlePolygon(position);
  return {(poly[1].x + poly[2].x) / 2, (poly[1].y + poly[2].y) / 2};
}
}  // namespace

void test_polygon_vertical_at_half() {
  const auto tip = tipMid(0.5f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, tip.x);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, tip.y);
}

void test_polygon_left_stop() {
  const auto tip = tipMid(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 55.15f, tip.x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 205.59f, tip.y);
}

void test_area_covers_shadow_and_clamps() {
  const auto area = vu::needleArea(0.5f);
  TEST_ASSERT_TRUE(area.x1 <= 177);
  TEST_ASSERT_TRUE(area.x2 >= 186);
  TEST_ASSERT_TRUE(area.y1 <= 147);
  TEST_ASSERT_TRUE(area.y2 >= 326 && area.y2 <= 359);
  TEST_ASSERT_TRUE(vu::needleArea(0.0f).x1 >= 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_target_at_zero_vu);
  RUN_TEST(test_target_averages_linear);
  RUN_TEST(test_rise_reaches_99_percent_in_300ms);
  RUN_TEST(test_overshoot_between_1_and_2_percent);
  RUN_TEST(test_pinned_at_stop);
  RUN_TEST(test_long_stall_stays_finite);
  RUN_TEST(test_falls_back_on_silence);
  RUN_TEST(test_lamp_lights_and_fades);
  RUN_TEST(test_lamp_stays_dark_below_threshold);
  RUN_TEST(test_polygon_vertical_at_half);
  RUN_TEST(test_polygon_left_stop);
  RUN_TEST(test_area_covers_shadow_and_clamps);
  return UNITY_END();
}
