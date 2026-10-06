#include <unity.h>

#include <map>
#include <string>

#include "RotationSetting.h"

using drehklang::display::Point;
using drehklang::display::RotationSetting;
using drehklang::playback::KeyValueStore;

void setUp() {}
void tearDown() {}

namespace {

class FakeStore : public KeyValueStore {
 public:
  bool getU8(const std::string &key, uint8_t &out) override {
    auto it = values.find(key);
    if (it == values.end()) return false;
    out = it->second;
    return true;
  }
  void setU8(const std::string &key, uint8_t value) override {
    values[key] = value;
    writes++;
  }
  std::map<std::string, uint8_t> values;
  int writes = 0;
};

constexpr int16_t kSize = 360;

}  // namespace

void test_defaults_to_upright_and_ignores_garbage() {
  FakeStore store;
  RotationSetting rotation(store);
  rotation.begin();
  TEST_ASSERT_EQUAL_UINT8(0, rotation.quarterTurns());
  TEST_ASSERT_EQUAL_INT(0, rotation.clockwiseDegrees());

  store.values[RotationSetting::kKey] = 2;
  rotation.begin();
  TEST_ASSERT_EQUAL_INT(180, rotation.clockwiseDegrees());

  store.values[RotationSetting::kKey] = 7;
  rotation.begin();
  TEST_ASSERT_EQUAL_UINT8(0, rotation.quarterTurns());
}

void test_adjust_wraps_both_ways() {
  FakeStore store;
  RotationSetting rotation(store);
  rotation.begin();
  rotation.adjust(-1, 0);
  TEST_ASSERT_EQUAL_INT(270, rotation.clockwiseDegrees());
  rotation.adjust(2, 0);
  TEST_ASSERT_EQUAL_INT(90, rotation.clockwiseDegrees());
  rotation.adjust(7, 0);
  TEST_ASSERT_EQUAL_INT(0, rotation.clockwiseDegrees());
}

void test_saves_only_after_changes_settle() {
  FakeStore store;
  RotationSetting rotation(store);
  rotation.begin();
  rotation.adjust(1, 1000);
  rotation.adjust(1, 1500);
  rotation.tick(2400);
  TEST_ASSERT_EQUAL_INT(0, store.writes);
  rotation.tick(2500);
  TEST_ASSERT_EQUAL_INT(1, store.writes);
  TEST_ASSERT_EQUAL_UINT8(2, store.values[RotationSetting::kKey]);
  rotation.tick(9000);
  TEST_ASSERT_EQUAL_INT(1, store.writes);
}

void test_full_turn_back_to_the_start_does_not_save() {
  FakeStore store;
  RotationSetting rotation(store);
  rotation.begin();
  rotation.adjust(4, 0);
  rotation.tick(5000);
  TEST_ASSERT_EQUAL_INT(0, store.writes);
}

// Content turned clockwise by the setting: a touch on the panel's own
// top-left corner is, for the user, at the corner that the turn moved
// there.
void test_maps_panel_points_to_what_the_user_sees() {
  const Point topLeft{0, 0};
  const Point mid{180, 70};  // The calibration's top target.
  Point p = RotationSetting::toLogical(topLeft, 0, kSize);
  TEST_ASSERT_EQUAL_INT16(0, p.x);
  TEST_ASSERT_EQUAL_INT16(0, p.y);

  // 90 degrees clockwise: the panel's top-left is the user's bottom-left.
  p = RotationSetting::toLogical(topLeft, 1, kSize);
  TEST_ASSERT_EQUAL_INT16(0, p.x);
  TEST_ASSERT_EQUAL_INT16(kSize - 1, p.y);
  p = RotationSetting::toLogical(mid, 1, kSize);
  TEST_ASSERT_EQUAL_INT16(70, p.x);
  TEST_ASSERT_EQUAL_INT16(179, p.y);

  p = RotationSetting::toLogical(topLeft, 2, kSize);
  TEST_ASSERT_EQUAL_INT16(kSize - 1, p.x);
  TEST_ASSERT_EQUAL_INT16(kSize - 1, p.y);

  // 270: the panel's top-left is the user's top-right.
  p = RotationSetting::toLogical(topLeft, 3, kSize);
  TEST_ASSERT_EQUAL_INT16(kSize - 1, p.x);
  TEST_ASSERT_EQUAL_INT16(0, p.y);
}

void test_four_quarter_turns_are_the_identity() {
  Point p{123, 45};
  for (int i = 0; i < 4; ++i) p = RotationSetting::toLogical(p, 1, kSize);
  TEST_ASSERT_EQUAL_INT16(123, p.x);
  TEST_ASSERT_EQUAL_INT16(45, p.y);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_upright_and_ignores_garbage);
  RUN_TEST(test_adjust_wraps_both_ways);
  RUN_TEST(test_saves_only_after_changes_settle);
  RUN_TEST(test_full_turn_back_to_the_start_does_not_save);
  RUN_TEST(test_maps_panel_points_to_what_the_user_sees);
  RUN_TEST(test_four_quarter_turns_are_the_identity);
  return UNITY_END();
}
