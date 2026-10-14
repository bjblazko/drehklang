#include <unity.h>

#include <cstdint>
#include <vector>

#include "CoverBackdrop.h"

using namespace drehklang::coverview;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

}  // namespace

void test_average_of_one_colour_is_that_colour() {
  const std::vector<uint16_t> px(16, rgb565(200, 100, 40));
  TEST_ASSERT_EQUAL_HEX16(rgb565(200, 100, 40), averageColor(px.data(), px.size()));
}

void test_average_mixes_channels_separately() {
  // Half pure red, half pure blue: half of each, no green.
  std::vector<uint16_t> px(8, rgb565(248, 0, 0));
  px.insert(px.end(), 8, rgb565(0, 0, 248));
  const uint16_t avg = averageColor(px.data(), px.size());
  TEST_ASSERT_EQUAL_UINT16(15, avg >> 11);         // 31 / 2
  TEST_ASSERT_EQUAL_UINT16(0, (avg >> 5) & 0x3F);
  TEST_ASSERT_EQUAL_UINT16(15, avg & 0x1F);
}

void test_average_of_nothing_is_black() { TEST_ASSERT_EQUAL_HEX16(0, averageColor(nullptr, 0)); }

void test_blur_leaves_a_flat_image_flat() {
  std::vector<uint16_t> px(32 * 32, rgb565(80, 160, 240));
  boxBlur(px.data(), 32, 4, 3);
  for (uint16_t p : px) TEST_ASSERT_EQUAL_HEX16(rgb565(80, 160, 240), p);
}

void test_blur_spreads_a_bright_spot() {
  std::vector<uint16_t> px(32 * 32, 0);
  px[16 * 32 + 16] = 0xFFFF;
  boxBlur(px.data(), 32, 2, 1);
  // The spot is dimmer, and its neighbours within the radius lit.
  TEST_ASSERT_TRUE((px[16 * 32 + 16] >> 11) < 31);
  TEST_ASSERT_TRUE((px[16 * 32 + 18] >> 11) > 0);
  TEST_ASSERT_TRUE((px[18 * 32 + 16] >> 11) > 0);
  // Outside it, nothing.
  TEST_ASSERT_EQUAL_HEX16(0, px[16 * 32 + 22]);
}

void test_blur_keeps_the_edges_from_darkening() {
  // Clamped at the borders, not padded with black.
  std::vector<uint16_t> px(16 * 16, rgb565(248, 248, 248));
  boxBlur(px.data(), 16, 6, 3);
  TEST_ASSERT_EQUAL_HEX16(rgb565(248, 248, 248), px[0]);
}

void test_enlarging_a_flat_image_keeps_it_flat() {
  const std::vector<uint16_t> src(8 * 8, rgb565(80, 160, 240));
  std::vector<uint16_t> out;
  enlarge(src.data(), 8, 30, &out);
  TEST_ASSERT_EQUAL(30 * 30, out.size());
  for (uint16_t p : out) TEST_ASSERT_EQUAL_HEX16(rgb565(80, 160, 240), p);
}

void test_enlarging_blends_between_neighbours() {
  // Black on the left half, white on the right: the middle of the output
  // is between the two, its edges are not.
  std::vector<uint16_t> src(4 * 4, 0);
  for (int y = 0; y < 4; ++y) src[y * 4 + 2] = src[y * 4 + 3] = 0xFFFF;
  std::vector<uint16_t> out;
  enlarge(src.data(), 4, 40, &out);
  TEST_ASSERT_EQUAL_HEX16(0, out[20 * 40 + 0]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, out[20 * 40 + 39]);
  const uint16_t mid = out[20 * 40 + 20] >> 11;
  TEST_ASSERT_TRUE(mid > 0 && mid < 31);
}

// The sharp cover hides the middle; nothing is computed there.
void test_enlarging_leaves_the_hole_alone() {
  const std::vector<uint16_t> src(8 * 8, rgb565(80, 160, 240));
  std::vector<uint16_t> out;
  enlarge(src.data(), 8, 30, &out, 10);
  TEST_ASSERT_EQUAL_HEX16(rgb565(80, 160, 240), out[0]);
  TEST_ASSERT_EQUAL_HEX16(rgb565(80, 160, 240), out[9 * 30 + 15]);
  TEST_ASSERT_EQUAL_HEX16(0, out[15 * 30 + 15]);
  TEST_ASSERT_EQUAL_HEX16(0, out[10 * 30 + 10]);
  TEST_ASSERT_EQUAL_HEX16(rgb565(80, 160, 240), out[20 * 30 + 20]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_average_of_one_colour_is_that_colour);
  RUN_TEST(test_average_mixes_channels_separately);
  RUN_TEST(test_average_of_nothing_is_black);
  RUN_TEST(test_blur_leaves_a_flat_image_flat);
  RUN_TEST(test_blur_spreads_a_bright_spot);
  RUN_TEST(test_blur_keeps_the_edges_from_darkening);
  RUN_TEST(test_enlarging_a_flat_image_keeps_it_flat);
  RUN_TEST(test_enlarging_blends_between_neighbours);
  RUN_TEST(test_enlarging_leaves_the_hole_alone);
  return UNITY_END();
}
