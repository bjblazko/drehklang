#pragma once

#include "RawFile.h"
#include "TagResult.h"

namespace drehklang::library {

// Reads tags and the cover from a FLAC file's metadata blocks: the
// VORBIS_COMMENT block (the same KEY=value list Ogg Vorbis uses, parsed by
// VorbisCommentParser) and the first JPEG PICTURE block, a front cover
// winning over any other picture type. An ID3v2 tag some taggers put in
// front of "fLaC" is skipped. Durations come from the decoder library,
// which reads them exactly from STREAMINFO.
class FlacParser {
 public:
  static TagResult parse(RawFile &file);
};

}  // namespace drehklang::library
