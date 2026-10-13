#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "HapticFeedback.h"

using drehklang::haptics::Feel;
using drehklang::haptics::HapticFeedback;

void setUp() {}
void tearDown() {}

namespace {

class FakeStore : public drehklang::playback::KeyValueStore {
 public:
  std::map<std::string, uint8_t> values;
  bool getU8(const std::string &key, uint8_t &out) override {
    auto it = values.find(key);
    if (it == values.end()) return false;
    out = it->second;
    return true;
  }
  void setU8(const std::string &key, uint8_t value) override { values[key] = value; }
};

class FakeSink : public drehklang::haptics::HapticSink {
 public:
  std::vector<uint8_t> played;
  void play(uint8_t effect) override { played.push_back(effect); }
};

}  // namespace

void test_on_by_default_and_each_feel_has_its_effect() {
  FakeStore store;
  FakeSink sink;
  HapticFeedback haptics(store);
  haptics.begin();
  haptics.attach(sink);
  TEST_ASSERT_TRUE(haptics.enabled());
  haptics.feel(Feel::Tap, 1000);
  haptics.feel(Feel::Refusal, 2000);
  haptics.feel(Feel::Lock, 3000);
  TEST_ASSERT_EQUAL(3, sink.played.size());
  TEST_ASSERT_EQUAL_UINT8(24, sink.played[0]);
  TEST_ASSERT_EQUAL_UINT8(10, sink.played[1]);
  TEST_ASSERT_EQUAL_UINT8(7, sink.played[2]);
  haptics.feel(Feel::Hit, 4000);
  haptics.feel(Feel::Crash, 5000);
  TEST_ASSERT_EQUAL_UINT8(4, sink.played[3]);
  TEST_ASSERT_EQUAL_UINT8(1, sink.played[4]);
}

// LVGL reports the click after its handler refused: the refusal must win.
void test_a_tap_right_after_a_refusal_is_dropped() {
  FakeStore store;
  FakeSink sink;
  HapticFeedback haptics(store);
  haptics.attach(sink);
  haptics.feel(Feel::Refusal, 5000);
  haptics.feel(Feel::Tap, 5010);
  TEST_ASSERT_EQUAL(1, sink.played.size());
  haptics.feel(Feel::Tap, 5000 + HapticFeedback::kTapHoldoffMs);
  TEST_ASSERT_EQUAL(2, sink.played.size());
}

void test_switched_off_it_stays_quiet_and_remembers() {
  FakeStore store;
  FakeSink sink;
  {
    HapticFeedback haptics(store);
    haptics.setEnabled(false);
  }
  HapticFeedback haptics(store);
  haptics.begin();
  haptics.attach(sink);
  TEST_ASSERT_FALSE(haptics.enabled());
  haptics.feel(Feel::Tap, 100);
  haptics.feel(Feel::Lock, 900);
  TEST_ASSERT_EQUAL(0, sink.played.size());
}

void test_without_a_motor_nothing_happens() {
  FakeStore store;
  HapticFeedback haptics(store);
  haptics.feel(Feel::Tap, 100);  // Must not crash.
  TEST_PASS();
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_on_by_default_and_each_feel_has_its_effect);
  RUN_TEST(test_a_tap_right_after_a_refusal_is_dropped);
  RUN_TEST(test_switched_off_it_stays_quiet_and_remembers);
  RUN_TEST(test_without_a_motor_nothing_happens);
  return UNITY_END();
}
