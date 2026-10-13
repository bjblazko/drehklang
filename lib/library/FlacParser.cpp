#include "FlacParser.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "VorbisCommentParser.h"

namespace drehklang::library {

namespace {

constexpr uint8_t kVorbisComment = 4;
constexpr uint8_t kPicture = 6;
constexpr uint8_t kInvalidBlock = 127;
constexpr uint32_t kFrontCover = 3;
// Real files carry a handful of blocks; a corrupt chain stops here.
constexpr int kMaxBlocks = 64;
// Like the Ogg scan window: the text tags fit easily, and a list cut off
// here still yields the entries before the cut.
constexpr size_t kCommentLimit = 64 * 1024;

uint32_t readU32BE(const uint8_t *b) {
  return (static_cast<uint32_t>(b[0]) << 24) |
         (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

// Where "fLaC" should start: after an ID3v2 tag, if one leads the file.
size_t streamStart(RawFile &file) {
  uint8_t h[10];
  if (!file.seek(0) || file.read(h, 10) != 10 || std::memcmp(h, "ID3", 3) != 0) {
    return 0;
  }
  size_t size = (static_cast<size_t>(h[6] & 0x7F) << 21) |
                (static_cast<size_t>(h[7] & 0x7F) << 14) |
                (static_cast<size_t>(h[8] & 0x7F) << 7) |
                static_cast<size_t>(h[9] & 0x7F);
  return 10 + size + ((h[5] & 0x10) ? 10 : 0);  // Footer flag.
}

// A PICTURE block: type, MIME, description, four dimension fields, then
// the image. Sets `picture` to the image if it is a JPEG.
bool readPicture(RawFile &file, size_t start, size_t length,
                 uint32_t *pictureType, EmbeddedPicture *picture) {
  const size_t end = start + length;
  size_t pos = start;
  uint8_t b[8];
  auto skipField = [&](size_t extra) -> bool {
    // A 32-bit length followed by that many bytes, plus `extra` fixed ones.
    if (end - pos < 4 || !file.seek(pos) || file.read(b, 4) != 4) return false;
    pos += 4;
    const uint32_t fieldLength = readU32BE(b);
    if (fieldLength > end - pos || extra > end - pos - fieldLength) return false;
    pos += fieldLength + extra;
    return true;
  };
  if (length < 4 || !file.seek(pos) || file.read(b, 4) != 4) return false;
  *pictureType = readU32BE(b);
  pos += 4;
  // MIME, then the description and width/height/depth/colours.
  if (!skipField(0) || !skipField(16)) return false;
  if (end - pos < 4 || !file.seek(pos) || file.read(b, 4) != 4) return false;
  pos += 4;
  const uint32_t imageLength = readU32BE(b);
  // The MIME type may also be "-->" with a URL as the data; the JPEG
  // magic tells both apart, as for untyped MP4 covers.
  if (imageLength < 2 || imageLength > end - pos || !file.seek(pos) ||
      file.read(b, 2) != 2 || b[0] != 0xFF || b[1] != 0xD8) {
    return false;
  }
  picture->present = true;
  picture->offset = pos;
  picture->length = imageLength;
  return true;
}

}  // namespace

TagResult FlacParser::parse(RawFile &file) {
  TagResult result;
  size_t pos = streamStart(file);
  uint8_t header[4];
  if (pos > file.size() || file.size() - pos < 4 || !file.seek(pos) ||
      file.read(header, 4) != 4 || std::memcmp(header, "fLaC", 4) != 0) {
    return result;
  }
  pos += 4;

  bool haveFront = false;
  for (int i = 0; i < kMaxBlocks; ++i) {
    if (file.size() - pos < 4 || !file.seek(pos) || file.read(header, 4) != 4) {
      break;
    }
    const bool last = (header[0] & 0x80) != 0;
    const uint8_t type = header[0] & 0x7F;
    const size_t length = (static_cast<size_t>(header[1]) << 16) |
                          (static_cast<size_t>(header[2]) << 8) | header[3];
    const size_t dataStart = pos + 4;
    if (type == kInvalidBlock || length > file.size() - dataStart) break;

    if (type == kVorbisComment) {
      std::vector<uint8_t> data(std::min(length, kCommentLimit));
      if (file.seek(dataStart) && file.read(data.data(), data.size()) == data.size()) {
        const EmbeddedPicture picture = result.picture;
        result = VorbisCommentParser::parseComments(data.data(), data.size());
        result.picture = picture;
      }
    } else if (type == kPicture && !haveFront) {
      uint32_t pictureType = 0;
      EmbeddedPicture picture;
      if (readPicture(file, dataStart, length, &pictureType, &picture) &&
          (!result.picture.present || pictureType == kFrontCover)) {
        result.picture = picture;
        haveFront = pictureType == kFrontCover;
      }
    }

    if (last) break;
    pos = dataStart + length;
  }

  result.found = result.found || result.picture.present;
  return result;
}

}  // namespace drehklang::library
