#include "CoverArtCache.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <new>

namespace drehklang::library {

namespace {

std::string toLower(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return out;
}

// Sizes the buffer a cover is read into. A picture can be larger than the
// memory left (a 1.35 MB cover during a rescan, 2026-11-06), and an
// uncaught std::bad_alloc aborts the firmware: such a cover is skipped.
bool sizeBuffer(std::vector<uint8_t> *out, size_t size) {
  try {
    out->resize(size);
    return true;
  } catch (const std::bad_alloc &) {
    out->clear();
    out->shrink_to_fit();
    return false;
  }
}

// Finds a cover.jpg/cover.jpeg (any casing) among an album folder's
// immediate children, if any.
bool findFolderCoverName(DirectoryReader &dirReader,
                          const std::string &albumFolderPath,
                          std::string *coverName) {
  for (const auto &entry : dirReader.listChildren(albumFolderPath)) {
    if (entry.isDirectory) continue;
    std::string lower = toLower(entry.name);
    if (lower == "cover.jpg" || lower == "cover.jpeg") {
      *coverName = entry.name;
      return true;
    }
  }
  return false;
}

void decodeAndWrite(const uint8_t *jpegBytes, size_t jpegLen,
                     const std::string &albumFolderPath, JpegDecoder &decoder,
                     CoverWriter &writer) {
  std::vector<uint16_t> pixels;
  if (!decoder.decodeSquare(jpegBytes, jpegLen, CoverArtCache::kCoverSize,
                             &pixels)) {
    return;
  }
  writer.writeCover(albumFolderPath, CoverArtCache::kCoverSize, pixels);
}

}  // namespace

std::string CoverArtCache::cacheFileNameFor(const std::string &albumFolderPath) {
  // FNV-1a, hex-encoded -- simple, deterministic, and collision-resistant
  // enough for the handful of album folders a personal music library
  // actually has.
  uint32_t hash = 2166136261u;
  for (unsigned char c : albumFolderPath) {
    hash ^= c;
    hash *= 16777619u;
  }
  char hex[9];
  std::snprintf(hex, sizeof(hex), "%08x", hash);
  return std::string(hex);
}

std::string CoverArtCache::albumFolderPathFor(
    const std::string &trackFilePath) {
  auto lastSlash = trackFilePath.find_last_of('/');
  if (lastSlash == std::string::npos) return "";
  return trackFilePath.substr(0, lastSlash);
}

bool CoverArtCache::readCoverJpeg(const std::string &albumFolderPath,
                                  RawFile &firstTrackFile, const TagResult &firstTrackTags,
                                  DirectoryReader &dirReader, FileOpener &opener,
                                  std::vector<uint8_t> *out) {
  if (firstTrackTags.picture.present) {
    if (!sizeBuffer(out, firstTrackTags.picture.length)) return false;
    if (firstTrackFile.seek(firstTrackTags.picture.offset) &&
        firstTrackFile.read(out->data(), out->size()) == out->size()) {
      return true;
    }
  }

  std::string coverName;
  if (!findFolderCoverName(dirReader, albumFolderPath, &coverName)) {
    out->clear();
    return false;
  }
  auto coverFile = opener.open(albumFolderPath + "/" + coverName);
  if (!coverFile) {
    out->clear();
    return false;
  }
  if (!sizeBuffer(out, coverFile->size())) return false;
  if (coverFile->read(out->data(), out->size()) != out->size()) {
    out->clear();
    return false;
  }
  return true;
}

void CoverArtCache::ensureCoverCached(const std::string &albumFolderPath,
                                       RawFile &firstTrackFile,
                                       const TagResult &firstTrackTags,
                                       DirectoryReader &dirReader,
                                       FileOpener &opener, JpegDecoder &decoder,
                                       CoverWriter &writer) {
  std::vector<uint8_t> jpegBytes;
  if (!readCoverJpeg(albumFolderPath, firstTrackFile, firstTrackTags, dirReader, opener,
                     &jpegBytes)) {
    return;
  }
  decodeAndWrite(jpegBytes.data(), jpegBytes.size(), albumFolderPath, decoder, writer);
}

}  // namespace drehklang::library
