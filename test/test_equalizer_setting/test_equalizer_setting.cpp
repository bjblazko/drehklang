#include <unity.h>

#include <map>
#include <string>

#include "EqualizerSetting.h"
#include "GraphicEqualizer.h"

using drehklang::playback::KeyValueStore;
using drehklang::signal::EqualizerSetting;
using drehklang::signal::GraphicEqualizer;

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
}  // namespace

void test_starts_flat_with_the_lowest_band_selected() {
  FakeStore store;
  GraphicEqualizer eq;
  EqualizerSetting setting(store, eq);
  setting.begin();
  TEST_ASSERT_TRUE(setting.flat());
  TEST_ASSERT_EQUAL_size_t(0, setting.selected());
}

void test_loads_stored_gains_into_the_equalizer() {
  FakeStore store;
  store.values["eq1"] = 12 + 5;
  store.values["eq6"] = 12 - 3;
  store.values["eq3"] = 200;  // Garbage: flat.
  GraphicEqualizer eq;
  EqualizerSetting setting(store, eq);
  setting.begin();
  TEST_ASSERT_EQUAL_INT(5, eq.gain(1));
  TEST_ASSERT_EQUAL_INT(-3, eq.gain(6));
  TEST_ASSERT_EQUAL_INT(0, eq.gain(3));
}

void test_the_knob_sets_the_selected_band() {
  FakeStore store;
  GraphicEqualizer eq;
  EqualizerSetting setting(store, eq);
  setting.begin();
  setting.select(4);
  setting.adjust(3, 0);
  setting.adjust(20, 0);
  TEST_ASSERT_EQUAL_INT(12, eq.gain(4));
  setting.select(9);  // Out of range: ignored.
  TEST_ASSERT_EQUAL_size_t(4, setting.selected());
}

void test_dragging_sets_a_band_and_selects_it() {
  FakeStore store;
  GraphicEqualizer eq;
  EqualizerSetting setting(store, eq);
  setting.begin();
  setting.set(2, -7, 0);
  TEST_ASSERT_EQUAL_INT(-7, eq.gain(2));
  TEST_ASSERT_EQUAL_size_t(2, setting.selected());
}

void test_flat_resets_every_band() {
  FakeStore store;
  GraphicEqualizer eq;
  EqualizerSetting setting(store, eq);
  setting.begin();
  setting.set(0, 6, 0);
  setting.set(5, -4, 0);
  setting.makeFlat(0);
  TEST_ASSERT_TRUE(setting.flat());
  TEST_ASSERT_TRUE(eq.flat());
}

void test_saves_changed_bands_once_settled() {
  FakeStore store;
  GraphicEqualizer eq;
  EqualizerSetting setting(store, eq);
  setting.begin();
  setting.select(1);
  setting.adjust(1, 1000);
  setting.adjust(1, 1500);
  setting.tick(2400);
  TEST_ASSERT_EQUAL_INT(0, store.writes);
  setting.tick(2500);
  TEST_ASSERT_EQUAL_INT(1, store.writes);
  TEST_ASSERT_EQUAL_UINT8(12 + 2, store.values["eq1"]);
  setting.tick(9000);
  TEST_ASSERT_EQUAL_INT(1, store.writes);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_flat_with_the_lowest_band_selected);
  RUN_TEST(test_loads_stored_gains_into_the_equalizer);
  RUN_TEST(test_the_knob_sets_the_selected_band);
  RUN_TEST(test_dragging_sets_a_band_and_selects_it);
  RUN_TEST(test_flat_resets_every_band);
  RUN_TEST(test_saves_changed_bands_once_settled);
  return UNITY_END();
}
