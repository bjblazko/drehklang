#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace drehklang::video {

// Where a film was left (ADR 0031): the sound's byte offset, which the
// audio driver resumes from, and the picture shown with it. Two NVS u32
// values per film, keyed by a hash of its title -- NVS keys are at most
// 15 characters, titles are not.
struct VideoResume {
  uint32_t soundPosition = 0;
  uint32_t frame = 0;

  bool atStart() const { return soundPosition == 0; }
};

// FNV-1a, 32 bit: short, stable across builds, no table.
inline uint32_t titleHash(const std::string &title) {
  uint32_t hash = 2166136261u;
  for (unsigned char c : title) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash;
}

// "vs" + 8 hex digits for the sound, "vf" + 8 for the frame.
inline std::string resumeKey(char kind, const std::string &title) {
  char key[12];
  std::snprintf(key, sizeof(key), "v%c%08lx", kind, static_cast<unsigned long>(titleHash(title)));
  return key;
}

inline std::string soundKey(const std::string &title) { return resumeKey('s', title); }
inline std::string frameKey(const std::string &title) { return resumeKey('f', title); }

}  // namespace drehklang::video
