#pragma once

#include <atomic>
#include <cstdint>

namespace drehklang::btaudio {

// Interleaved 16-bit samples from one producer task to one consumer task,
// without a lock (ADR 0027). The storage is the caller's: PSRAM on the S3,
// internal RAM on the U4WDH. Capacity is in samples, a power of two.
// Attach the storage before either side runs.
class PcmRing {
 public:
  PcmRing() = default;
  PcmRing(int16_t *storage, uint32_t capacity) { attach(storage, capacity); }

  void attach(int16_t *storage, uint32_t capacity) {
    data_ = storage;
    capacity_ = storage != nullptr ? capacity : 0;
    mask_ = capacity_ - 1;
  }

  uint32_t capacity() const { return capacity_; }

  // Samples waiting. Safe from either side.
  uint32_t available() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }

  // Producer: all `count` samples, taken from source(i), or none of them.
  // A chunk is never split, so left and right never trade places.
  template <typename Source>
  bool write(uint32_t count, Source source) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    if (capacity_ - (head - tail) < count) return false;
    for (uint32_t i = 0; i < count; ++i) data_[(head + i) & mask_] = source(i);
    head_.store(head + count, std::memory_order_release);
    return true;
  }

  bool write(const int16_t *samples, uint32_t count) {
    return write(count, [samples](uint32_t i) { return samples[i]; });
  }

  // Consumer: up to `max` samples. Returns how many.
  uint32_t read(int16_t *dst, uint32_t max) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    uint32_t n = head_.load(std::memory_order_acquire) - tail;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) dst[i] = data_[(tail + i) & mask_];
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  // Consumer: drops everything waiting.
  void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  int16_t *data_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t mask_ = 0;
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
};

}  // namespace drehklang::btaudio
