#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "RawFile.h"

namespace drehklang::video {

// Reads the picture half of a film (ADR 0031): a RIFF AVI holding one
// Motion-JPEG stream and nothing else, with its idx1 index -- what
// scripts/convert-video.sh writes. Pure logic over library::RawFile, so it
// runs against in-memory files on the host.
//
// Only a single-RIFF file is accepted: ffmpeg switches to OpenDML (more
// RIFF parts, no complete idx1) past 1 GB, which the script avoids.
class AviReader {
 public:
  bool open(library::RawFile &file) {
    file_ = &file;
    frameCount_ = 0;
    char id[4];
    uint32_t riffSize = 0;
    if (!readHeader(0, id, riffSize) || !is(id, "RIFF")) return false;
    if (static_cast<size_t>(riffSize) + 8 != file.size()) return false;  // OpenDML.
    if (!readFourcc(8, id) || !is(id, "AVI ")) return false;
    return findChunks(12, static_cast<size_t>(riffSize) + 8) && moviStart_ != 0 &&
           frameCount_ > 0;
  }

  uint32_t frameCount() const { return frameCount_; }

  // Moves to `frame`; the next nextFrame() returns it. False past the end
  // or when the index does not point at a picture.
  bool seekFrame(uint32_t frame) {
    if (frame >= frameCount_) return false;
    uint8_t entry[16];
    if (!file_->seek(idx1Start_ + static_cast<size_t>(frame) * 16) ||
        file_->read(entry, 16) != 16) {
      return false;
    }
    if (!isPicture(reinterpret_cast<const char *>(entry))) return false;
    const size_t chunk = moviFourcc_ + u32(entry + 8);
    if (chunk + 8 > moviEnd_ || !file_->seek(chunk)) return false;
    position_ = chunk;
    nextIndex_ = frame;
    return true;
  }

  // The next picture's JPEG bytes into `buf` (up to `capacity`); its size,
  // or 0 at the end of the film or for a picture larger than `capacity`
  // (which is skipped). `index` is the frame number it carries.
  size_t nextFrame(uint8_t *buf, size_t capacity, uint32_t &index) {
    while (position_ + 8 <= moviEnd_) {
      char id[4];
      uint32_t size = 0;
      if (!readHeader(position_, id, size)) return 0;
      const size_t body = position_ + 8;
      position_ = body + size + (size & 1);
      if (!isPicture(id)) continue;
      index = nextIndex_++;
      if (size > capacity) return 0;
      if (!file_->seek(body) || file_->read(buf, size) != size) return 0;
      return size;
    }
    return 0;
  }

 private:
  static bool is(const char *id, const char *fourcc) { return std::memcmp(id, fourcc, 4) == 0; }
  // "00dc" or "00db": stream 0, compressed or uncompressed picture.
  static bool isPicture(const char *id) {
    return id[0] == '0' && id[1] == '0' && id[2] == 'd' && (id[3] == 'c' || id[3] == 'b');
  }
  static uint32_t u32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
  }

  bool readFourcc(size_t at, char *id) {
    return file_->seek(at) && file_->read(reinterpret_cast<uint8_t *>(id), 4) == 4;
  }

  bool readHeader(size_t at, char *id, uint32_t &size) {
    uint8_t header[8];
    if (!file_->seek(at) || file_->read(header, 8) != 8) return false;
    std::memcpy(id, header, 4);
    size = u32(header + 4);
    return true;
  }

  // The top-level chunks: the 'movi' list and the idx1 index after it.
  bool findChunks(size_t at, size_t end) {
    while (at + 8 <= end) {
      char id[4];
      uint32_t size = 0;
      if (!readHeader(at, id, size)) return false;
      const size_t body = at + 8;
      if (body + size > end) return false;
      char type[4] = {};
      if (is(id, "LIST") && readFourcc(body, type) && is(type, "movi")) {
        moviFourcc_ = body;
        moviStart_ = body + 4;
        moviEnd_ = body + size;
      } else if (is(id, "idx1")) {
        idx1Start_ = body;
        frameCount_ = size / 16;
      }
      at = body + size + (size & 1);
    }
    position_ = moviStart_;
    nextIndex_ = 0;
    return true;
  }

  library::RawFile *file_ = nullptr;
  size_t moviFourcc_ = 0;  // idx1 offsets count from here.
  size_t moviStart_ = 0;
  size_t moviEnd_ = 0;
  size_t idx1Start_ = 0;
  uint32_t frameCount_ = 0;
  size_t position_ = 0;
  uint32_t nextIndex_ = 0;
};

}  // namespace drehklang::video
