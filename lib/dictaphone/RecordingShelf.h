#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "FolderBrowser.h"
#include "WavHeader.h"

namespace drehklang::dictaphone {

// The recordings in /Recordings (ADR 0032). There is no clock, so they are
// numbered: REC0001.wav, shown as "Recording 1". The next number is the
// highest on the card plus one, so nothing is overwritten and nothing has
// to be stored.
inline constexpr const char *kRecordingFolder = "/Recordings";
// What the recorder writes: 16 kHz, mono, 16 bit (ADR 0032).
inline constexpr WavFormat kRecordingFormat{16000, 1, 16};

struct Recording {
  int number = 0;
  std::string fileName;
  uint32_t bytes = 0;  // The file's size.
};

// A recording's length from its file size alone: the recorder writes the
// canonical header and kRecordingFormat, so the list needs to open no
// file for it.
inline uint32_t recordingDurationMs(uint32_t fileBytes) {
  if (fileBytes <= kWavHeaderBytes) return 0;
  return static_cast<uint32_t>(static_cast<uint64_t>(fileBytes - kWavHeaderBytes) * 1000 /
                               kRecordingFormat.byteRate());
}

// m:ss, or h:mm:ss from an hour on.
inline void formatDuration(char *out, size_t size, uint32_t ms) {
  const unsigned seconds = ms / 1000;
  if (seconds >= 3600) {
    snprintf(out, size, "%u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60);
  } else {
    snprintf(out, size, "%u:%02u", seconds / 60, seconds % 60);
  }
}

// The number in "REC<digits>.wav" (any case), or -1 for anything else --
// macOS's "._" sidecars included.
inline int recordingNumber(const std::string &name) {
  constexpr size_t kPrefix = 3;  // "REC"
  constexpr size_t kSuffix = 4;  // ".wav"
  if (name.size() <= kPrefix + kSuffix) return -1;
  auto lower = [](const std::string &text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
  };
  if (lower(name.substr(0, kPrefix)) != "rec") return -1;
  if (lower(name.substr(name.size() - kSuffix)) != ".wav") return -1;
  const std::string digits = name.substr(kPrefix, name.size() - kPrefix - kSuffix);
  if (digits.size() > 8) return -1;
  for (char c : digits) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
  }
  return std::stoi(digits);
}

// The recordings among a folder's entries, newest (highest number) first.
inline std::vector<Recording> recordings(const std::vector<library::FolderEntry> &entries) {
  std::vector<Recording> out;
  for (const library::FolderEntry &entry : entries) {
    if (entry.isDirectory) continue;
    const int number = recordingNumber(entry.name);
    if (number >= 0) out.push_back({number, entry.name, entry.size});
  }
  std::sort(out.begin(), out.end(),
            [](const Recording &a, const Recording &b) { return a.number > b.number; });
  return out;
}

inline int nextRecordingNumber(const std::vector<Recording> &existing) {
  int highest = 0;
  for (const Recording &recording : existing) highest = std::max(highest, recording.number);
  return highest + 1;
}

inline std::string recordingFileName(int number) {
  char name[24];
  snprintf(name, sizeof(name), "REC%04d.wav", number);
  return name;
}

inline std::string recordingPath(const std::string &fileName) {
  return std::string(kRecordingFolder) + "/" + fileName;
}

inline std::string recordingTitle(int number) { return "Recording " + std::to_string(number); }

// The recordings on the card, through the reader the Folder view uses.
inline std::vector<Recording> recordingsIn(library::DirectoryReader &reader) {
  return recordings(reader.listChildren(kRecordingFolder));
}

}  // namespace drehklang::dictaphone
