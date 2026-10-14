#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace drehklang::dictaphone {

// The dictaphone's files (ADR 0032): plain PCM WAV, which the decoder
// library and any computer play without help. Written with a placeholder
// header whose sizes are patched when recording stops; read back by
// RecordingPlayer, which trusts the file's length over a size of zero.
inline constexpr size_t kWavHeaderBytes = 44;

struct WavFormat {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;

  uint16_t blockAlign() const { return static_cast<uint16_t>(channels * bitsPerSample / 8); }
  uint32_t byteRate() const { return sampleRate * blockAlign(); }
};

struct WavInfo {
  bool valid = false;
  WavFormat format;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;

  uint32_t durationMs() const {
    const uint32_t rate = format.byteRate();
    return rate ? static_cast<uint32_t>(static_cast<uint64_t>(dataBytes) * 1000 / rate) : 0;
  }
};

namespace wav_detail {

inline void put16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
inline void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace wav_detail

// The canonical 44-byte header: RIFF, a 16-byte "fmt " chunk, "data".
inline void writeWavHeader(uint8_t *out, const WavFormat &format, uint32_t dataBytes) {
  using namespace wav_detail;
  std::memcpy(out, "RIFF", 4);
  put32(out + 4, 36 + dataBytes);
  std::memcpy(out + 8, "WAVEfmt ", 8);
  put32(out + 16, 16);
  put16(out + 20, 1);  // PCM
  put16(out + 22, format.channels);
  put32(out + 24, format.sampleRate);
  put32(out + 28, format.byteRate());
  put16(out + 32, format.blockAlign());
  put16(out + 34, format.bitsPerSample);
  std::memcpy(out + 36, "data", 4);
  put32(out + 40, dataBytes);
}

// Walks the chunks in the file's first `count` bytes to "data". Uncompressed
// PCM only. A data size of zero, or one past the end of the file, is taken
// from `fileSize` instead, in whole frames.
inline WavInfo readWavHeader(const uint8_t *bytes, size_t count, uint32_t fileSize) {
  using namespace wav_detail;
  WavInfo info;
  if (count < 12 || std::memcmp(bytes, "RIFF", 4) != 0 ||
      std::memcmp(bytes + 8, "WAVE", 4) != 0) {
    return info;
  }
  bool haveFormat = false;
  size_t at = 12;
  while (at + 8 <= count) {
    const uint32_t size = get32(bytes + at + 4);
    const uint8_t *body = bytes + at + 8;
    if (std::memcmp(bytes + at, "fmt ", 4) == 0 && at + 8 + 16 <= count) {
      if (get16(body) != 1) return info;
      info.format.channels = get16(body + 2);
      info.format.sampleRate = get32(body + 4);
      info.format.bitsPerSample = get16(body + 14);
      haveFormat = info.format.blockAlign() > 0 && info.format.sampleRate > 0;
    } else if (std::memcmp(bytes + at, "data", 4) == 0) {
      if (!haveFormat) return info;
      info.dataOffset = static_cast<uint32_t>(at + 8);
      const uint32_t available = fileSize > info.dataOffset ? fileSize - info.dataOffset : 0;
      uint32_t data = size == 0 || size > available ? available : size;
      data -= data % info.format.blockAlign();
      info.dataBytes = data;
      info.valid = true;
      return info;
    }
    at += 8 + size + (size & 1);  // Chunks are padded to even sizes.
  }
  return info;
}

}  // namespace drehklang::dictaphone
