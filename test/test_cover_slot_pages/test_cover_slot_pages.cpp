#include <unity.h>

#include <map>
#include <string>

#include "CoverSlotPages.h"

using drehklang::playback::KeyValueStore;
using drehklang::visualizer::CoverSlotPages;
using drehklang::visualizer::SlotView;

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

void test_shows_the_cover_first_when_nothing_is_stored() {
  FakeStore store;
  CoverSlotPages pages(store);
  pages.begin();
  TEST_ASSERT_EQUAL(SlotView::Cover, pages.shown(true));
  TEST_ASSERT_EQUAL_size_t(4, pages.count(true));
  TEST_ASSERT_EQUAL_size_t(0, pages.shownIndex(true));
}

void test_without_a_cover_there_is_no_cover_page() {
  FakeStore store;
  CoverSlotPages pages(store);
  pages.begin();
  TEST_ASSERT_EQUAL_size_t(3, pages.count(false));
  TEST_ASSERT_EQUAL(SlotView::DotMatrix, pages.shown(false));
  TEST_ASSERT_EQUAL_size_t(0, pages.shownIndex(false));
}

void test_takes_over_the_old_spectrum_switch() {
  FakeStore store;
  store.values[CoverSlotPages::kLegacyKey] = 1;
  CoverSlotPages pages(store);
  pages.begin();
  TEST_ASSERT_EQUAL(SlotView::DotMatrix, pages.shown(true));

  store.values[CoverSlotPages::kLegacyKey] = 0;
  pages.begin();
  TEST_ASSERT_EQUAL(SlotView::Cover, pages.shown(true));
}

void test_a_stored_view_wins_and_garbage_is_ignored() {
  FakeStore store;
  store.values[CoverSlotPages::kLegacyKey] = 1;
  store.values[CoverSlotPages::kKey] = static_cast<uint8_t>(SlotView::Spectrum);
  CoverSlotPages pages(store);
  pages.begin();
  TEST_ASSERT_EQUAL(SlotView::Spectrum, pages.shown(true));
  TEST_ASSERT_EQUAL_size_t(3, pages.shownIndex(true));
  TEST_ASSERT_EQUAL_size_t(2, pages.shownIndex(false));

  store.values[CoverSlotPages::kKey] = 9;
  pages.begin();
  TEST_ASSERT_EQUAL(SlotView::DotMatrix, pages.shown(true));
}

void test_swipes_step_through_the_pages_without_wrapping_and_are_kept() {
  FakeStore store;
  CoverSlotPages pages(store);
  pages.begin();
  TEST_ASSERT_FALSE(pages.step(-1, true));
  TEST_ASSERT_TRUE(pages.step(1, true));
  TEST_ASSERT_EQUAL(SlotView::DotMatrix, pages.shown(true));
  TEST_ASSERT_TRUE(pages.step(1, true));
  TEST_ASSERT_TRUE(pages.step(1, true));
  TEST_ASSERT_EQUAL(SlotView::Spectrum, pages.shown(true));
  TEST_ASSERT_FALSE(pages.step(1, true));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SlotView::Spectrum),
                          store.values[CoverSlotPages::kKey]);
}

void test_swiping_where_there_is_no_cover_starts_from_the_dot_matrix() {
  FakeStore store;
  CoverSlotPages pages(store);
  pages.begin();
  // Shown as the dot matrix here, but swiping on from it goes to the scope.
  TEST_ASSERT_TRUE(pages.step(1, false));
  TEST_ASSERT_EQUAL(SlotView::Scope, pages.shown(false));
  // Back to the first page of an album without a cover, then one with.
  TEST_ASSERT_TRUE(pages.step(-1, false));
  TEST_ASSERT_FALSE(pages.step(-1, false));
  TEST_ASSERT_EQUAL(SlotView::DotMatrix, pages.shown(true));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_shows_the_cover_first_when_nothing_is_stored);
  RUN_TEST(test_without_a_cover_there_is_no_cover_page);
  RUN_TEST(test_takes_over_the_old_spectrum_switch);
  RUN_TEST(test_a_stored_view_wins_and_garbage_is_ignored);
  RUN_TEST(test_swipes_step_through_the_pages_without_wrapping_and_are_kept);
  RUN_TEST(test_swiping_where_there_is_no_cover_starts_from_the_dot_matrix);
  return UNITY_END();
}
