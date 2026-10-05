#include <unity.h>

#include <cstdint>

#include "PcmRing.h"

using drehklang::btaudio::PcmRing;

void setUp() {}
void tearDown() {}

void test_reads_back_in_order() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  const int16_t in[] = {1, 2, 3, 4};
  TEST_ASSERT_TRUE(ring.write(in, 4));
  TEST_ASSERT_EQUAL(4, ring.available());
  int16_t out[4] = {};
  TEST_ASSERT_EQUAL(4, ring.read(out, 4));
  TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 4);
  TEST_ASSERT_EQUAL(0, ring.available());
}

void test_a_chunk_that_does_not_fit_is_not_written_at_all() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  const int16_t six[] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_TRUE(ring.write(six, 6));
  const int16_t four[] = {7, 8, 9, 10};
  TEST_ASSERT_FALSE(ring.write(four, 4));
  TEST_ASSERT_EQUAL(6, ring.available());
}

void test_wraps_around_the_end() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  int16_t scratch[8];
  const int16_t first[] = {1, 2, 3, 4, 5, 6};
  ring.write(first, 6);
  ring.read(scratch, 6);
  const int16_t second[] = {10, 11, 12, 13, 14, 15};
  TEST_ASSERT_TRUE(ring.write(second, 6));
  TEST_ASSERT_EQUAL(6, ring.read(scratch, 8));
  TEST_ASSERT_EQUAL_INT16_ARRAY(second, scratch, 6);
}

void test_clear_drops_everything_waiting() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  const int16_t in[] = {1, 2};
  ring.write(in, 2);
  ring.clear();
  TEST_ASSERT_EQUAL(0, ring.available());
}

void test_an_unattached_ring_takes_nothing() {
  PcmRing ring;
  const int16_t in[] = {1, 2};
  TEST_ASSERT_FALSE(ring.write(in, 2));
  int16_t storage[4];
  ring.attach(storage, 4);
  TEST_ASSERT_TRUE(ring.write(in, 2));
}

void test_write_with_a_source_converts_each_sample() {
  int16_t storage[4];
  PcmRing ring(storage, 4);
  TEST_ASSERT_TRUE(ring.write(2, [](uint32_t i) { return static_cast<int16_t>(100 + i); }));
  int16_t out[2];
  ring.read(out, 2);
  TEST_ASSERT_EQUAL(100, out[0]);
  TEST_ASSERT_EQUAL(101, out[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_back_in_order);
  RUN_TEST(test_a_chunk_that_does_not_fit_is_not_written_at_all);
  RUN_TEST(test_wraps_around_the_end);
  RUN_TEST(test_clear_drops_everything_waiting);
  RUN_TEST(test_an_unattached_ring_takes_nothing);
  RUN_TEST(test_write_with_a_source_converts_each_sample);
  return UNITY_END();
}
