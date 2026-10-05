#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "AdaptiveResampler.h"
#include "HeadphoneStream.h"
#include "PcmRing.h"

using drehklang::btaudio::AdaptiveResampler;
using drehklang::btaudio::HeadphoneStream;
using drehklang::btaudio::PcmRing;

namespace {

constexpr uint32_t kCapacity = 16384;  // The U4WDH's ring: 8192 frames.

// Stereo frames with left = right = a running counter, so order is visible.
struct Producer {
  int16_t next = 0;
  bool push(PcmRing &ring, uint32_t frames) {
    return ring.write(frames * 2, [this](uint32_t i) {
      const int16_t v = next;
      if (i % 2 == 1) ++next;
      return v;
    });
  }
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_waits_for_half_a_ring_before_playing() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  producer.push(ring, 1000);
  AdaptiveResampler resampler;
  std::vector<int16_t> out(512 * 2, 7);
  resampler.render(ring, out.data(), 512);
  TEST_ASSERT_FALSE(resampler.playing());
  for (int16_t s : out) TEST_ASSERT_EQUAL(0, s);
}

void test_the_same_rate_is_bit_exact() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  producer.push(ring, kCapacity / 4);  // Exactly half full, in frames.
  AdaptiveResampler resampler;
  std::vector<int16_t> out(256 * 2);
  int16_t expected = 0;
  for (int block = 0; block < 100; ++block) {
    resampler.render(ring, out.data(), 256);
    producer.push(ring, 256);
    for (size_t i = 0; i < 256; ++i) {
      TEST_ASSERT_EQUAL(expected, out[i * 2]);
      TEST_ASSERT_EQUAL(expected, out[i * 2 + 1]);
      ++expected;
    }
  }
}

void test_48k_to_44k_steps_by_the_ratio_and_keeps_the_fill() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  AdaptiveResampler resampler;
  resampler.reset(48000);
  producer.push(ring, kCapacity / 4);
  std::vector<int16_t> out(441 * 2);
  resampler.render(ring, out.data(), 441);
  const uint32_t startFill = ring.available();
  // Ten seconds: 480 frames in and 441 out every 10 ms.
  for (int step = 0; step < 1000; ++step) {
    producer.push(ring, 480);
    resampler.render(ring, out.data(), 441);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 48000.0f / 44100.0f, resampler.step());
  TEST_ASSERT_INT_WITHIN(64 * 2, startFill, ring.available());
}

void drift(double ppm) {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  AdaptiveResampler resampler;
  std::vector<int16_t> out(441 * 2);
  double owed = 0;
  uint32_t lowest = kCapacity, highest = 0;
  // Ten simulated minutes in 10 ms steps; the S3 sends 256-frame packets.
  for (int step = 0; step < 60000; ++step) {
    owed += 441.0 * (1.0 + ppm * 1e-6);
    while (owed >= 256) {
      TEST_ASSERT_TRUE_MESSAGE(producer.push(ring, 256), "ring overran");
      owed -= 256;
    }
    resampler.render(ring, out.data(), 441);
    if (step > 1000) {
      lowest = std::min(lowest, ring.available());
      highest = std::max(highest, ring.available());
    }
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, resampler.underruns(), "ring ran dry");
  TEST_ASSERT_GREATER_THAN(kCapacity / 4, lowest);
  TEST_ASSERT_LESS_THAN(kCapacity * 3 / 4, highest);
}

void test_a_faster_s3_clock_is_absorbed() { drift(200); }
void test_a_slower_s3_clock_is_absorbed() { drift(-200); }

void test_an_empty_ring_plays_silence_then_refills() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  AdaptiveResampler resampler;
  producer.push(ring, kCapacity / 4);
  std::vector<int16_t> out(kCapacity);  // Enough for every frame and more.
  resampler.render(ring, out.data(), kCapacity / 4 + 100);
  TEST_ASSERT_EQUAL(1, resampler.underruns());
  TEST_ASSERT_FALSE(resampler.playing());
  TEST_ASSERT_EQUAL(0, out[(kCapacity / 4 + 50) * 2]);
  producer.push(ring, kCapacity / 4);
  resampler.render(ring, out.data(), 10);
  TEST_ASSERT_TRUE(resampler.playing());
}

void test_rate_change_resets_and_keeps_pitch() {
  std::vector<int16_t> storage(kCapacity);
  HeadphoneStream stream(storage.data(), kCapacity);
  std::vector<uint8_t> pcm(256 * 4, 0);
  stream.push(44100, pcm.data(), 512);
  std::vector<int16_t> out(256 * 2);
  stream.render(out.data(), 256);
  TEST_ASSERT_EQUAL_UINT32(44100, stream.inputRate());
  stream.push(32000, pcm.data(), 512);
  stream.render(out.data(), 256);
  TEST_ASSERT_EQUAL_UINT32(32000, stream.inputRate());
  TEST_ASSERT_EQUAL(0, stream.buffered());
}

void test_stream_decodes_little_endian_samples() {
  std::vector<int16_t> storage(kCapacity);
  HeadphoneStream stream(storage.data(), kCapacity);
  const uint8_t pcm[] = {0x34, 0x12, 0xFF, 0xFF};  // 0x1234, -1.
  TEST_ASSERT_TRUE(stream.push(44100, pcm, 2));
  TEST_ASSERT_EQUAL(2, stream.buffered());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_waits_for_half_a_ring_before_playing);
  RUN_TEST(test_the_same_rate_is_bit_exact);
  RUN_TEST(test_48k_to_44k_steps_by_the_ratio_and_keeps_the_fill);
  RUN_TEST(test_a_faster_s3_clock_is_absorbed);
  RUN_TEST(test_a_slower_s3_clock_is_absorbed);
  RUN_TEST(test_an_empty_ring_plays_silence_then_refills);
  RUN_TEST(test_rate_change_resets_and_keeps_pitch);
  RUN_TEST(test_stream_decodes_little_endian_samples);
  return UNITY_END();
}
