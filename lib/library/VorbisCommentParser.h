#pragma once

#include "RawFile.h"
#include "TagResult.h"

namespace drehklang::library {

// Extracts ARTIST/ALBUM/TITLE/TRACKNUMBER from an OGG Vorbis comment
// header. Rather than fully parsing Ogg page framing, this scans the
// first 64KB of the file for the "\x03vorbis" comment-packet magic and
// reads the comment list that follows it directly -- correct for the
// overwhelming majority of real files, where the comment header (unlike
// large embedded cover art, which this project doesn't need to read)
// fits in a single Ogg page. A comment header split across page
// boundaries is a documented limitation, not silently mishandled: it
// simply won't be found, and TagReader's normal fallback applies.
class VorbisCommentParser {
 public:
  static TagResult parse(RawFile &file);
  // The comment list itself (vendor string, count, KEY=value entries),
  // which FLAC stores unchanged in its VORBIS_COMMENT block. A list cut
  // off by `size` yields the entries before the cut.
  static TagResult parseComments(const uint8_t *data, size_t size);
};

}  // namespace drehklang::library
