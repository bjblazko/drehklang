#include <unity.h>

#include <map>
#include <string>

#include "VisualizationSettings.h"

using drehklang::playback::KeyValueStore;
using drehklang::visualizer::Visualization;
using drehklang::visualizer::VisualizationSettings;

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
  void setU8(const std::string &key, uint8_t value) override { values[key] = value; }
  std::map<std::string, uint8_t> values;
};

}  // namespace

void test_defaults_analog_and_not_awake() {
  FakeStore store;
  VisualizationSettings settings(store);
  settings.begin();
  TEST_ASSERT_TRUE(settings.current() == Visualization::AnalogVu);
  TEST_ASSERT_FALSE(settings.keepAwake());
}

void test_next_wraps_and_is_stored() {
  FakeStore store;
  VisualizationSettings settings(store);
  settings.begin();
  settings.next();
  TEST_ASSERT_TRUE(settings.current() == Visualization::BarMeter);
  TEST_ASSERT_EQUAL_UINT8(1, store.values[VisualizationSettings::kKindKey]);
  settings.next();
  TEST_ASSERT_TRUE(settings.current() == Visualization::AnalogVu);

  VisualizationSettings reloaded(store);
  store.values[VisualizationSettings::kKindKey] = 1;
  reloaded.begin();
  TEST_ASSERT_TRUE(reloaded.current() == Visualization::BarMeter);
}

void test_unknown_stored_kind_reads_as_analog() {
  FakeStore store;
  store.values[VisualizationSettings::kKindKey] = 9;
  VisualizationSettings settings(store);
  settings.begin();
  TEST_ASSERT_TRUE(settings.current() == Visualization::AnalogVu);
}

void test_keep_awake_toggles_and_is_stored() {
  FakeStore store;
  VisualizationSettings settings(store);
  settings.begin();
  settings.toggleKeepAwake();
  TEST_ASSERT_TRUE(settings.keepAwake());
  TEST_ASSERT_EQUAL_UINT8(1, store.values[VisualizationSettings::kAwakeKey]);

  VisualizationSettings reloaded(store);
  reloaded.begin();
  TEST_ASSERT_TRUE(reloaded.keepAwake());
  reloaded.toggleKeepAwake();
  TEST_ASSERT_EQUAL_UINT8(0, store.values[VisualizationSettings::kAwakeKey]);
}

void test_holds_awake_only_while_playing() {
  FakeStore store;
  VisualizationSettings settings(store);
  settings.begin();
  TEST_ASSERT_FALSE(settings.holdsDisplayAwake(true));
  TEST_ASSERT_FALSE(settings.holdsDisplayAwake(false));
  settings.toggleKeepAwake();
  TEST_ASSERT_TRUE(settings.holdsDisplayAwake(true));
  TEST_ASSERT_FALSE(settings.holdsDisplayAwake(false));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_analog_and_not_awake);
  RUN_TEST(test_next_wraps_and_is_stored);
  RUN_TEST(test_unknown_stored_kind_reads_as_analog);
  RUN_TEST(test_keep_awake_toggles_and_is_stored);
  RUN_TEST(test_holds_awake_only_while_playing);
  return UNITY_END();
}
