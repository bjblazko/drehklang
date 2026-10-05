#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace drehklang::resume {

// Up to ADR 0026 an Ogg track's stored position was a sample index from
// Drehklang's own Vorbis decoder; the decoder library reads it as a byte
// offset, which lands somewhere else in the file. A record written before
// then therefore keeps the track but starts it from its beginning.
inline void forgetLegacyOggPosition(const std::string &trackPath, uint32_t &filePosition,
                                    uint32_t &elapsedSeconds) {
  const auto dot = trackPath.find_last_of('.');
  if (dot == std::string::npos) return;
  std::string ext = trackPath.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (ext != "ogg" && ext != "oga") return;
  filePosition = 0;
  elapsedSeconds = 0;
}

}  // namespace drehklang::resume
