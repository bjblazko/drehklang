#include <unity.h>

#include <climits>
#include <cstdint>

#include "AudioTap.h"
#include "PcmRing.h"

using drehklang::bluetooth::AudioTap;
using drehklang::btaudio::PcmRing;

void setUp() {}
void tearDown() {}

void test_nothing_is_copied_without_headphones() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  AudioTap tap(ring);
  const int32_t samples[] = {1 << 16, 2 << 16};
  tap.pushStereo32(samples, 2, 44100);
  TEST_ASSERT_EQUAL(0, ring.available());
}

void test_keeps_the_top_16_bits() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  AudioTap tap(ring);
  tap.setForwarding(true);
  const int32_t samples[] = {0x12345678, -1, INT32_MIN, INT32_MAX};
  tap.pushStereo32(samples, 4, 48000);
  int16_t out[4];
  TEST_ASSERT_EQUAL(4, ring.read(out, 4));
  TEST_ASSERT_EQUAL(0x1234, out[0]);
  TEST_ASSERT_EQUAL(-1, out[1]);
  TEST_ASSERT_EQUAL(-32768, out[2]);
  TEST_ASSERT_EQUAL(32767, out[3]);
  TEST_ASSERT_EQUAL_UINT32(48000, tap.rate());
}

void test_a_full_ring_drops_the_whole_chunk() {
  int16_t storage[4];
  PcmRing ring(storage, 4);
  AudioTap tap(ring);
  tap.setForwarding(true);
  const int16_t samples[] = {1, 2, 3, 4, 5, 6};
  tap.pushStereo16(samples, 4, 22050);
  tap.pushStereo16(samples, 2, 22050);
  TEST_ASSERT_EQUAL(4, ring.available());
  TEST_ASSERT_EQUAL_UINT32(1, tap.dropped());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_copied_without_headphones);
  RUN_TEST(test_keeps_the_top_16_bits);
  RUN_TEST(test_a_full_ring_drops_the_whole_chunk);
  return UNITY_END();
}
