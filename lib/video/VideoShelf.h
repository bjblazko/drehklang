#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "FolderBrowser.h"

namespace drehklang::video {

// The films in /Videos (ADR 0031): a film is a pair, <title>.avi for the
// picture and <title>.m4a for the sound, shown once, by its title. A file
// without its partner is not a film; macOS's "._" sidecars are not either.
inline constexpr const char *kVideoFolder = "/Videos";

inline std::string lowered(const std::string &text) {
  std::string out = text;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

// The title of `name` if it ends in `extension` (lower case, with the
// dot), else empty.
inline std::string titleWith(const std::string &name, const std::string &extension) {
  if (name.size() <= extension.size()) return {};
  if (lowered(name.substr(name.size() - extension.size())) != extension) return {};
  return name.substr(0, name.size() - extension.size());
}

// Titles with both halves, sorted case-insensitively.
inline std::vector<std::string> filmTitles(const std::vector<std::string> &fileNames) {
  std::vector<std::string> pictures;
  std::vector<std::string> sounds;
  for (const std::string &name : fileNames) {
    if (name.rfind("._", 0) == 0) continue;
    std::string title = titleWith(name, ".avi");
    if (!title.empty()) pictures.push_back(title);
    title = titleWith(name, ".m4a");
    if (!title.empty()) sounds.push_back(lowered(title));
  }
  std::vector<std::string> films;
  for (const std::string &title : pictures) {
    if (std::find(sounds.begin(), sounds.end(), lowered(title)) != sounds.end()) {
      films.push_back(title);
    }
  }
  std::sort(films.begin(), films.end(), [](const std::string &a, const std::string &b) {
    return lowered(a) < lowered(b);
  });
  return films;
}

// The films in kVideoFolder, through the reader the Folder view uses.
inline std::vector<std::string> filmTitlesIn(library::DirectoryReader &reader) {
  std::vector<std::string> names;
  for (const library::FolderEntry &entry : reader.listChildren(kVideoFolder)) {
    if (!entry.isDirectory) names.push_back(entry.name);
  }
  return filmTitles(names);
}

}  // namespace drehklang::video
