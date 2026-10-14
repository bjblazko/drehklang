#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace drehklang::coverview {

// The full-screen cover's background (ADR 0033): the cover itself, blurred
// until only its colours are left, behind the sharp copy. Pure RGB565
// arithmetic so it is host-tested; run on the 96 px cached cover, where a
// strong blur costs next to nothing and its low resolution cannot show.

// The mean of each channel.
inline uint16_t averageColor(const uint16_t *pixels, size_t count) {
  if (count == 0) return 0;
  uint64_t r = 0;
  uint64_t g = 0;
  uint64_t b = 0;
  for (size_t i = 0; i < count; ++i) {
    r += pixels[i] >> 11;
    g += (pixels[i] >> 5) & 0x3F;
    b += pixels[i] & 0x1F;
  }
  return static_cast<uint16_t>(((r / count) << 11) | ((g / count) << 5) | (b / count));
}

namespace backdrop_detail {

// One pass along a line of `count` pixels, `stride` apart: each becomes the
// mean of its 2*radius+1 neighbours, the line's ends repeated past it.
inline void blurLine(uint16_t *line, size_t count, size_t stride, int radius,
                     std::vector<uint16_t> &scratch) {
  scratch.resize(count);
  for (size_t i = 0; i < count; ++i) scratch[i] = line[i * stride];
  const int last = static_cast<int>(count) - 1;
  const int window = 2 * radius + 1;
  auto at = [&](int i) { return scratch[std::clamp(i, 0, last)]; };
  uint32_t r = 0;
  uint32_t g = 0;
  uint32_t b = 0;
  for (int i = -radius; i <= radius; ++i) {
    const uint16_t p = at(i);
    r += p >> 11;
    g += (p >> 5) & 0x3F;
    b += p & 0x1F;
  }
  for (int i = 0; i <= last; ++i) {
    line[static_cast<size_t>(i) * stride] =
        static_cast<uint16_t>(((r / window) << 11) | ((g / window) << 5) | (b / window));
    const uint16_t out = at(i - radius);
    const uint16_t in = at(i + radius + 1);
    r += (in >> 11) - (out >> 11);
    g += ((in >> 5) & 0x3F) - ((out >> 5) & 0x3F);
    b += (in & 0x1F) - (out & 0x1F);
  }
}

}  // namespace backdrop_detail

// A box blur of a size x size image, in place. Three passes come close to
// a Gaussian.
inline void boxBlur(uint16_t *pixels, uint16_t size, int radius, int passes) {
  std::vector<uint16_t> scratch;
  for (int pass = 0; pass < passes; ++pass) {
    for (uint16_t y = 0; y < size; ++y) {
      backdrop_detail::blurLine(pixels + static_cast<size_t>(y) * size, size, 1, radius,
                                scratch);
    }
    for (uint16_t x = 0; x < size; ++x) {
      backdrop_detail::blurLine(pixels + x, size, size, radius, scratch);
    }
  }
}

// Bilinear enlargement of a srcSize square to dstSize, in 8-bit fixed
// point, for the backdrop, where the result is soft anyway: half of
// SquareResampler's ~190 ms for 360 px on the device (2026-10-14). A
// centred square of holeSize is left as it is -- the sharp cover hides it,
// which halves the time again.
inline void enlarge(const uint16_t *src, uint16_t srcSize, uint16_t dstSize,
                    std::vector<uint16_t> *out, uint16_t holeSize = 0) {
  const uint16_t holeFrom = static_cast<uint16_t>((dstSize - holeSize) / 2);
  const uint16_t holeTo = static_cast<uint16_t>(holeFrom + holeSize);
  struct Tap {
    uint16_t a;
    uint16_t b;
    uint16_t weight;  // Of b, out of 256.
  };
  std::vector<Tap> taps(dstSize);
  for (uint16_t i = 0; i < dstSize; ++i) {
    // The source position of the output pixel's centre, in 1/256ths.
    const int32_t pos = (2 * i + 1) * srcSize * 256 / (2 * dstSize) - 128;
    const int32_t clamped = std::clamp<int32_t>(pos, 0, (srcSize - 1) * 256);
    const auto a = static_cast<uint16_t>(clamped >> 8);
    taps[i] = {a, static_cast<uint16_t>(std::min<int32_t>(a + 1, srcSize - 1)),
               static_cast<uint16_t>(clamped & 0xFF)};
  }
  out->resize(static_cast<size_t>(dstSize) * dstSize);
  auto mix = [](uint16_t p, uint16_t q, uint32_t w) {
    const uint32_t r = ((p >> 11) * (256 - w) + (q >> 11) * w);
    const uint32_t g = (((p >> 5) & 0x3F) * (256 - w) + ((q >> 5) & 0x3F) * w);
    const uint32_t b = ((p & 0x1F) * (256 - w) + (q & 0x1F) * w);
    return std::array<uint32_t, 3>{r, g, b};  // Each times 256.
  };
  for (uint16_t y = 0; y < dstSize; ++y) {
    const Tap &ty = taps[y];
    const uint16_t *rowA = src + static_cast<size_t>(ty.a) * srcSize;
    const uint16_t *rowB = src + static_cast<size_t>(ty.b) * srcSize;
    uint16_t *dst = out->data() + static_cast<size_t>(y) * dstSize;
    const bool rowHasHole = holeSize > 0 && y >= holeFrom && y < holeTo;
    for (uint16_t x = 0; x < dstSize; ++x) {
      if (rowHasHole && x == holeFrom) {
        x = holeTo - 1;
        continue;
      }
      const Tap &tx = taps[x];
      const auto top = mix(rowA[tx.a], rowA[tx.b], tx.weight);
      const auto bottom = mix(rowB[tx.a], rowB[tx.b], tx.weight);
      uint32_t c[3];
      for (int k = 0; k < 3; ++k) {
        c[k] = (top[k] * (256 - ty.weight) + bottom[k] * ty.weight + 32768) >> 16;
      }
      dst[x] = static_cast<uint16_t>((c[0] << 11) | (c[1] << 5) | c[2]);
    }
  }
}

}  // namespace drehklang::coverview
