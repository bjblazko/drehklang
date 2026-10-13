#include <unity.h>

#include <cstring>
#include <vector>

#include "FakeRawFile.h"
#include "FlacParser.h"
#include "Id3v2Parser.h"
#include "RiffInfoParser.h"
#include "TagReader.h"
#include "VorbisCommentParser.h"

using drehklang::library::FlacParser;
using drehklang::library::Id3v2Parser;
using drehklang::library::RiffInfoParser;
using drehklang::library::TagReader;
using drehklang::library::TagResult;
using drehklang::library::VorbisCommentParser;

void setUp() {}
void tearDown() {}

namespace {

void appendU32BE(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void appendSynchsafe(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 21) & 0x7F));
  out.push_back(static_cast<uint8_t>((v >> 14) & 0x7F));
  out.push_back(static_cast<uint8_t>((v >> 7) & 0x7F));
  out.push_back(static_cast<uint8_t>(v & 0x7F));
}

void appendFrameV3(std::vector<uint8_t> &frames, const char *id,
                    const std::string &text) {
  std::vector<uint8_t> data;
  data.push_back(0);  // encoding: ISO-8859-1
  data.insert(data.end(), text.begin(), text.end());

  frames.insert(frames.end(), id, id + 4);
  appendU32BE(frames, static_cast<uint32_t>(data.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), data.begin(), data.end());
}

std::vector<uint8_t> buildId3v2Mp3(const std::vector<
    std::pair<const char *, std::string>> &fields) {
  std::vector<uint8_t> frames;
  for (auto &f : fields) {
    appendFrameV3(frames, f.first, f.second);
  }

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  // A little "audio" payload after the tag, like a real file.
  for (int i = 0; i < 16; ++i) file.push_back(0xAB);
  return file;
}

std::vector<uint8_t> buildId3v1Trailer(const std::string &title,
                                        const std::string &artist,
                                        const std::string &album) {
  std::vector<uint8_t> file(200, 0);  // fake "audio" body
  std::vector<uint8_t> tag(128, 0);
  tag[0] = 'T';
  tag[1] = 'A';
  tag[2] = 'G';
  std::memcpy(&tag[3], title.data(), title.size());
  std::memcpy(&tag[33], artist.data(), artist.size());
  std::memcpy(&tag[63], album.data(), album.size());
  tag[127] = 255;  // No genre, the way a tagger writes it.
  file.insert(file.end(), tag.begin(), tag.end());
  return file;
}

}  // namespace

void test_id3v2_parses_title_artist_album_track() {
  auto file = buildId3v2Mp3({{"TIT2", "Song Title"},
                              {"TPE1", "The Artist"},
                              {"TALB", "The Album"},
                              {"TRCK", "3/12"},
                              {"TPOS", "2/5"}});
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("Song Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("The Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("The Album", result.album.c_str());
  TEST_ASSERT_EQUAL_UINT16(3, result.trackNumber);
  TEST_ASSERT_EQUAL_UINT16(2, result.discNumber);
}

// UTF-16 with a BOM (encoding byte 1) is the shape every non-ASCII frame
// takes in the user's real library: BOM + code units, no null
// terminator required by this helper (the parser stops at one if
// present).
void appendFrameV3Utf16(std::vector<uint8_t> &frames, const char *id,
                         std::initializer_list<uint16_t> units) {
  std::vector<uint8_t> data;
  data.push_back(1);  // encoding: UTF-16 with BOM
  data.push_back(0xFF);
  data.push_back(0xFE);  // BOM: little-endian
  for (uint16_t u : units) {
    data.push_back(static_cast<uint8_t>(u & 0xFF));
    data.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
  }

  frames.insert(frames.end(), id, id + 4);
  appendU32BE(frames, static_cast<uint32_t>(data.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), data.begin(), data.end());
}

void appendFrameV3Latin1Byte(std::vector<uint8_t> &frames, const char *id,
                              const std::string &asciiPrefix, uint8_t rawByte,
                              const std::string &asciiSuffix) {
  std::vector<uint8_t> data;
  data.push_back(0);  // encoding: ISO-8859-1
  data.insert(data.end(), asciiPrefix.begin(), asciiPrefix.end());
  data.push_back(rawByte);
  data.insert(data.end(), asciiSuffix.begin(), asciiSuffix.end());

  frames.insert(frames.end(), id, id + 4);
  appendU32BE(frames, static_cast<uint32_t>(data.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), data.begin(), data.end());
}

void test_id3v2_decodes_utf16_bom_umlaut_to_utf8() {
  // "Björk" as UTF-16LE code units, matching the real-world shape: every
  // non-ASCII ID3v2 text frame in the user's library uses encoding 1
  // (UTF-16 with BOM).
  std::vector<uint8_t> frames;
  appendFrameV3Utf16(frames, "TPE1", {'B', 'j', 0x00F6, 'r', 'k'});

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  // UTF-8 for "Björk": B j 0xC3 0xB6 r k.
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", result.artist.c_str());
}

void test_id3v2_decodes_iso8859_1_byte_to_utf8() {
  // Encoding 0 (ISO-8859-1): a raw 0xF6 byte ('ö') must come out as the
  // two-byte UTF-8 sequence 0xC3 0xB6, not the raw byte (which would be
  // invalid UTF-8 on its own).
  std::vector<uint8_t> frames;
  appendFrameV3Latin1Byte(frames, "TALB", "Bj", 0xF6, "rk");

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", result.album.c_str());
}

void test_id3v2_missing_tag_returns_not_found() {
  std::vector<uint8_t> file(64, 0xAB);  // no "ID3" header, no v1 trailer
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_id3v2_falls_back_to_v1_trailer() {
  auto file = buildId3v1Trailer("V1 Title", "V1 Artist", "V1 Album");
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("V1 Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("V1 Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("V1 Album", result.album.c_str());
}

void test_id3v2_extracts_embedded_jpeg_picture() {
  std::vector<uint8_t> apicData;
  apicData.push_back(0);  // text encoding: Latin-1
  const char *mime = "image/jpeg";
  apicData.insert(apicData.end(), mime, mime + std::strlen(mime) + 1);
  apicData.push_back(0x03);  // picture type: front cover
  apicData.push_back(0);     // description: empty, null-terminated
  const std::vector<uint8_t> jpegBytes = {0xFF, 0xD8, 0xFF, 0xAA, 0xBB, 0xCC};
  apicData.insert(apicData.end(), jpegBytes.begin(), jpegBytes.end());

  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, static_cast<uint32_t>(apicData.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), apicData.begin(), apicData.end());

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.picture.present);
  TEST_ASSERT_EQUAL_UINT32(jpegBytes.size(), result.picture.length);

  std::vector<uint8_t> extracted(result.picture.length);
  TEST_ASSERT_TRUE(raw.seek(result.picture.offset));
  TEST_ASSERT_EQUAL_UINT32(extracted.size(),
                            raw.read(extracted.data(), extracted.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(jpegBytes.data(), extracted.data(),
                                 jpegBytes.size());
}

void test_id3v2_ignores_non_jpeg_picture() {
  std::vector<uint8_t> apicData;
  apicData.push_back(0);
  const char *mime = "image/png";
  apicData.insert(apicData.end(), mime, mime + std::strlen(mime) + 1);
  apicData.push_back(0x03);
  apicData.push_back(0);
  const std::vector<uint8_t> pngBytes = {0x89, 'P', 'N', 'G'};
  apicData.insert(apicData.end(), pngBytes.begin(), pngBytes.end());

  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, static_cast<uint32_t>(apicData.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), apicData.begin(), apicData.end());

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_FALSE(result.picture.present);
}

void test_id3v2_apic_only_no_text_frames_still_found() {
  std::vector<uint8_t> apicData;
  apicData.push_back(0);
  const char *mime = "image/jpeg";
  apicData.insert(apicData.end(), mime, mime + std::strlen(mime) + 1);
  apicData.push_back(0x03);
  apicData.push_back(0);
  const std::vector<uint8_t> jpegBytes = {0xFF, 0xD8, 0xFF};
  apicData.insert(apicData.end(), jpegBytes.begin(), jpegBytes.end());

  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, static_cast<uint32_t>(apicData.size()));
  frames.push_back(0);
  frames.push_back(0);
  frames.insert(frames.end(), apicData.begin(), apicData.end());

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_TRUE(result.picture.present);
}

void test_id3v2_truncated_apic_frame_does_not_crash() {
  std::vector<uint8_t> frames;
  frames.insert(frames.end(), {'A', 'P', 'I', 'C'});
  appendU32BE(frames, 100000);  // bogus, way past the tag
  frames.push_back(0);
  frames.push_back(0);
  frames.push_back(0);  // encoding byte only, no real payload

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_FALSE(result.picture.present);
}

void test_id3v2_truncated_frame_does_not_crash_and_yields_no_field() {
  // A frame header claiming a size far larger than the tag actually has.
  std::vector<uint8_t> frames;
  const char id[4] = {'T', 'I', 'T', '2'};
  frames.insert(frames.end(), id, id + 4);
  appendU32BE(frames, 100000);  // bogus, way past the tag
  frames.push_back(0);
  frames.push_back(0);
  frames.push_back(0);  // encoding byte only, no real payload

  std::vector<uint8_t> file;
  file.insert(file.end(), {'I', 'D', '3', 3, 0, 0});
  appendSynchsafe(file, static_cast<uint32_t>(frames.size()));
  file.insert(file.end(), frames.begin(), frames.end());

  FakeRawFile raw(file);
  TagResult result = Id3v2Parser::parse(raw);

  // Should not crash; no usable frame was read so it falls through to
  // (absent) ID3v1 -- not found.
  TEST_ASSERT_FALSE(result.found);
}

void test_vorbis_comment_parses_fields() {
  std::vector<uint8_t> buf;
  buf.push_back(0x03);
  const char *vorbis = "vorbis";
  buf.insert(buf.end(), vorbis, vorbis + 6);

  auto appendU32LE = [&](uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  auto appendComment = [&](const std::string &s) {
    appendU32LE(static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
  };

  std::string vendor = "Drehklang test encoder";
  appendU32LE(static_cast<uint32_t>(vendor.size()));
  buf.insert(buf.end(), vendor.begin(), vendor.end());

  appendU32LE(6);  // comment count
  appendComment("ARTIST=Vorbis Artist");
  appendComment("ALBUM=Vorbis Album");
  appendComment("TITLE=Vorbis Title");
  appendComment("TRACKNUMBER=7");
  appendComment("DISCNUMBER=2/3");
  appendComment("GENRE=Krautrock");

  FakeRawFile raw(buf);
  TagResult result = VorbisCommentParser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("Vorbis Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Vorbis Album", result.album.c_str());
  TEST_ASSERT_EQUAL_STRING("Vorbis Title", result.title.c_str());
  TEST_ASSERT_EQUAL_UINT16(7, result.trackNumber);
  TEST_ASSERT_EQUAL_UINT16(2, result.discNumber);
  TEST_ASSERT_EQUAL_STRING("Krautrock", result.genre.c_str());
}

void test_vorbis_comment_missing_magic_returns_not_found() {
  std::vector<uint8_t> buf(128, 0x00);
  FakeRawFile raw(buf);

  TagResult result = VorbisCommentParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_vorbis_comment_empty_list_returns_not_found() {
  std::vector<uint8_t> buf;
  buf.push_back(0x03);
  const char *vorbis = "vorbis";
  buf.insert(buf.end(), vorbis, vorbis + 6);
  auto appendU32LE = [&](uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  };
  std::string vendor = "enc";
  appendU32LE(static_cast<uint32_t>(vendor.size()));
  buf.insert(buf.end(), vendor.begin(), vendor.end());
  appendU32LE(0);  // zero comments

  FakeRawFile raw(buf);
  TagResult result = VorbisCommentParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_riff_info_parses_fields() {
  std::vector<uint8_t> info;
  const char infoTag[4] = {'I', 'N', 'F', 'O'};
  info.insert(info.end(), infoTag, infoTag + 4);

  auto appendSub = [&](const char *id, const std::string &value) {
    info.insert(info.end(), id, id + 4);
    uint32_t size = static_cast<uint32_t>(value.size());
    info.push_back(static_cast<uint8_t>(size & 0xFF));
    info.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
    info.push_back(static_cast<uint8_t>((size >> 16) & 0xFF));
    info.push_back(static_cast<uint8_t>((size >> 24) & 0xFF));
    info.insert(info.end(), value.begin(), value.end());
    if (size % 2 != 0) info.push_back(0);
  };
  appendSub("INAM", "WAV Title");
  appendSub("IART", "WAV Artist");
  appendSub("IPRD", "WAV Album");
  appendSub("IGNR", "Field Recording");

  std::vector<uint8_t> file;
  file.insert(file.end(), {'R', 'I', 'F', 'F'});
  uint32_t riffSize = 4 + 8 + static_cast<uint32_t>(info.size());
  file.push_back(static_cast<uint8_t>(riffSize & 0xFF));
  file.push_back(static_cast<uint8_t>((riffSize >> 8) & 0xFF));
  file.push_back(static_cast<uint8_t>((riffSize >> 16) & 0xFF));
  file.push_back(static_cast<uint8_t>((riffSize >> 24) & 0xFF));
  file.insert(file.end(), {'W', 'A', 'V', 'E'});

  file.insert(file.end(), {'L', 'I', 'S', 'T'});
  uint32_t listSize = static_cast<uint32_t>(info.size());
  file.push_back(static_cast<uint8_t>(listSize & 0xFF));
  file.push_back(static_cast<uint8_t>((listSize >> 8) & 0xFF));
  file.push_back(static_cast<uint8_t>((listSize >> 16) & 0xFF));
  file.push_back(static_cast<uint8_t>((listSize >> 24) & 0xFF));
  file.insert(file.end(), info.begin(), info.end());

  FakeRawFile raw(file);
  TagResult result = RiffInfoParser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("WAV Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("WAV Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("WAV Album", result.album.c_str());
  TEST_ASSERT_EQUAL_STRING("Field Recording", result.genre.c_str());
}

void test_riff_info_no_info_chunk_returns_not_found() {
  std::vector<uint8_t> file;
  file.insert(file.end(), {'R', 'I', 'F', 'F'});
  uint32_t riffSize = 4;
  file.push_back(static_cast<uint8_t>(riffSize & 0xFF));
  file.push_back(0);
  file.push_back(0);
  file.push_back(0);
  file.insert(file.end(), {'W', 'A', 'V', 'E'});

  FakeRawFile raw(file);
  TagResult result = RiffInfoParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_tag_reader_falls_back_to_unknown_and_filename() {
  std::vector<uint8_t> file(32, 0x00);  // no valid tag of any kind
  FakeRawFile raw(file);

  TagResult result = TagReader::read(raw, "/Music/some track.mp3");

  TEST_ASSERT_EQUAL_STRING("Unknown Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Unknown Album", result.album.c_str());
  TEST_ASSERT_EQUAL_STRING("some track", result.title.c_str());
}

void test_tag_reader_uses_id3_when_present_for_mp3() {
  auto file = buildId3v2Mp3({{"TIT2", "Real Title"}, {"TPE1", "Real Artist"}});
  FakeRawFile raw(file);

  TagResult result = TagReader::read(raw, "/Music/whatever.mp3");

  TEST_ASSERT_EQUAL_STRING("Real Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Real Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Unknown Album", result.album.c_str());
}

void test_id3v2_tcon_takes_plain_genre_text() {
  auto file = buildId3v2Mp3({{"TIT2", "Song"}, {"TCON", "Neue Deutsche Welle"}});
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_EQUAL_STRING("Neue Deutsche Welle", result.genre.c_str());
}

void test_id3v2_tcon_resolves_the_numbered_forms() {
  // "(17)" and a bare "17" are the same genre by two spellings.
  for (const char *text : {"(17)", "17"}) {
    auto file = buildId3v2Mp3({{"TIT2", "Song"}, {"TCON", text}});
    FakeRawFile raw(file);
    TEST_ASSERT_EQUAL_STRING("Rock", Id3v2Parser::parse(raw).genre.c_str());
  }
}

void test_id3v2_tcon_refining_text_wins_over_the_number() {
  auto file = buildId3v2Mp3({{"TIT2", "Song"}, {"TCON", "(17)Hard Rock"}});
  FakeRawFile raw(file);

  TEST_ASSERT_EQUAL_STRING("Hard Rock", Id3v2Parser::parse(raw).genre.c_str());
}

void test_id3v2_tcon_remix_marker_and_unknown_number_yield_no_genre() {
  for (const char *text : {"(RX)", "(240)"}) {
    auto file = buildId3v2Mp3({{"TIT2", "Song"}, {"TCON", text}});
    FakeRawFile raw(file);
    TEST_ASSERT_TRUE(Id3v2Parser::parse(raw).genre.empty());
  }
}

void test_id3v1_trailer_reads_the_genre_byte() {
  auto file = buildId3v1Trailer("Title", "Artist", "Album");
  file[file.size() - 1] = 9;  // Metal.
  FakeRawFile raw(file);

  TEST_ASSERT_EQUAL_STRING("Metal", Id3v2Parser::parse(raw).genre.c_str());
}

void test_id3v1_trailer_unset_genre_byte_yields_nothing() {
  auto file = buildId3v1Trailer("Title", "Artist", "Album");
  FakeRawFile raw(file);

  TEST_ASSERT_TRUE(Id3v2Parser::parse(raw).genre.empty());
}

void test_id3v2_tpe2_stands_in_for_missing_tpe1() {
  auto file = buildId3v2Mp3({{"TIT2", "Song"}, {"TPE2", "Album Artist"}});
  FakeRawFile raw(file);

  TagResult result = Id3v2Parser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("Album Artist", result.artist.c_str());
}

void test_id3v2_tpe1_wins_over_tpe2() {
  // Either order: a compilation's tracks keep their own artists.
  auto before = buildId3v2Mp3({{"TPE2", "Various"}, {"TPE1", "Track Artist"}});
  auto after = buildId3v2Mp3({{"TPE1", "Track Artist"}, {"TPE2", "Various"}});
  FakeRawFile rawBefore(before);
  FakeRawFile rawAfter(after);

  TEST_ASSERT_EQUAL_STRING("Track Artist", Id3v2Parser::parse(rawBefore).artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Track Artist", Id3v2Parser::parse(rawAfter).artist.c_str());
}

void appendU32LE(std::vector<uint8_t> &out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

std::vector<uint8_t> flacCommentBlock(const std::vector<std::string> &comments) {
  std::vector<uint8_t> out;
  const std::string vendor = "reference libFLAC 1.4.3";
  appendU32LE(out, static_cast<uint32_t>(vendor.size()));
  out.insert(out.end(), vendor.begin(), vendor.end());
  appendU32LE(out, static_cast<uint32_t>(comments.size()));
  for (const std::string &c : comments) {
    appendU32LE(out, static_cast<uint32_t>(c.size()));
    out.insert(out.end(), c.begin(), c.end());
  }
  return out;
}

std::vector<uint8_t> flacPictureBlock(uint32_t type, const std::string &mime,
                                      const std::vector<uint8_t> &image) {
  std::vector<uint8_t> out;
  appendU32BE(out, type);
  appendU32BE(out, static_cast<uint32_t>(mime.size()));
  out.insert(out.end(), mime.begin(), mime.end());
  const std::string description = "Cover";
  appendU32BE(out, static_cast<uint32_t>(description.size()));
  out.insert(out.end(), description.begin(), description.end());
  for (uint32_t field : {600u, 600u, 24u, 0u}) appendU32BE(out, field);
  appendU32BE(out, static_cast<uint32_t>(image.size()));
  out.insert(out.end(), image.begin(), image.end());
  return out;
}

// "fLaC", a STREAMINFO block, the given blocks (the last one flagged),
// then a little "audio".
std::vector<uint8_t> buildFlac(
    const std::vector<std::pair<uint8_t, std::vector<uint8_t>>> &blocks) {
  std::vector<uint8_t> file = {'f', 'L', 'a', 'C'};
  auto appendBlock = [&](uint8_t type, const std::vector<uint8_t> &data, bool last) {
    file.push_back(static_cast<uint8_t>(type | (last ? 0x80 : 0)));
    file.push_back(static_cast<uint8_t>((data.size() >> 16) & 0xFF));
    file.push_back(static_cast<uint8_t>((data.size() >> 8) & 0xFF));
    file.push_back(static_cast<uint8_t>(data.size() & 0xFF));
    file.insert(file.end(), data.begin(), data.end());
  };
  appendBlock(0, std::vector<uint8_t>(34, 0), blocks.empty());
  for (size_t i = 0; i < blocks.size(); ++i) {
    appendBlock(blocks[i].first, blocks[i].second, i + 1 == blocks.size());
  }
  for (int i = 0; i < 16; ++i) file.push_back(0xAB);
  return file;
}

const std::vector<uint8_t> kJpegA = {0xFF, 0xD8, 0xFF, 0xE0, 'A'};
const std::vector<uint8_t> kJpegB = {0xFF, 0xD8, 0xFF, 0xE0, 'B', 'B'};

void test_flac_reads_the_vorbis_comment_block() {
  auto file = buildFlac({{4, flacCommentBlock({"ARTIST=Flac Artist", "album=Flac Album",
                                                "TITLE=Flac Title", "TRACKNUMBER=3/12",
                                                "DISCNUMBER=2", "DATE=2001-05-04",
                                                "GENRE=Electronic"})}});
  FakeRawFile raw(file);

  TagResult result = FlacParser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_EQUAL_STRING("Flac Artist", result.artist.c_str());
  TEST_ASSERT_EQUAL_STRING("Flac Album", result.album.c_str());
  TEST_ASSERT_EQUAL_STRING("Flac Title", result.title.c_str());
  TEST_ASSERT_EQUAL_UINT16(3, result.trackNumber);
  TEST_ASSERT_EQUAL_UINT16(2, result.discNumber);
  TEST_ASSERT_EQUAL_UINT16(2001, result.year);
  TEST_ASSERT_EQUAL_STRING("Electronic", result.genre.c_str());
  TEST_ASSERT_FALSE(result.picture.present);
}

void test_flac_skips_a_leading_id3v2_tag() {
  auto flac = buildFlac({{4, flacCommentBlock({"TITLE=After ID3"})}});
  std::vector<uint8_t> file = {'I', 'D', '3', 4, 0, 0};
  appendSynchsafe(file, 20);
  file.insert(file.end(), 20, 0);
  file.insert(file.end(), flac.begin(), flac.end());
  FakeRawFile raw(file);

  TEST_ASSERT_EQUAL_STRING("After ID3", FlacParser::parse(raw).title.c_str());
}

void test_flac_prefers_the_front_cover_jpeg() {
  auto file = buildFlac({{6, flacPictureBlock(4, "image/jpeg", kJpegA)},  // Back.
                         {6, flacPictureBlock(3, "image/jpeg", kJpegB)},  // Front.
                         {4, flacCommentBlock({"TITLE=Covered"})}});
  FakeRawFile raw(file);

  TagResult result = FlacParser::parse(raw);

  TEST_ASSERT_EQUAL_STRING("Covered", result.title.c_str());
  TEST_ASSERT_TRUE(result.picture.present);
  TEST_ASSERT_EQUAL_UINT32(kJpegB.size(), result.picture.length);
  TEST_ASSERT_EQUAL_UINT8(0xFF, file[result.picture.offset]);
  TEST_ASSERT_EQUAL_UINT8('B', file[result.picture.offset + 4]);
}

void test_flac_takes_another_jpeg_when_there_is_no_front_cover() {
  auto file = buildFlac({{6, flacPictureBlock(0, "image/jpeg", kJpegA)}});
  FakeRawFile raw(file);

  TagResult result = FlacParser::parse(raw);

  TEST_ASSERT_TRUE(result.found);
  TEST_ASSERT_TRUE(result.picture.present);
  TEST_ASSERT_EQUAL_UINT8('A', file[result.picture.offset + 4]);
}

void test_flac_ignores_a_png_picture() {
  const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A};
  auto file = buildFlac({{6, flacPictureBlock(3, "image/png", png)}});
  FakeRawFile raw(file);

  TagResult result = FlacParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
  TEST_ASSERT_FALSE(result.picture.present);
}

void test_flac_without_magic_returns_not_found() {
  std::vector<uint8_t> file(64, 0x00);
  FakeRawFile raw(file);

  TEST_ASSERT_FALSE(FlacParser::parse(raw).found);
}

void test_flac_block_longer_than_the_file_does_not_crash() {
  auto file = buildFlac({{4, flacCommentBlock({"TITLE=Cut"})}});
  file.resize(file.size() - 30);  // The comment block now runs past the end.
  FakeRawFile raw(file);

  TagResult result = FlacParser::parse(raw);

  TEST_ASSERT_FALSE(result.found);
}

void test_flac_picture_with_lying_lengths_does_not_crash() {
  auto block = flacPictureBlock(3, "image/jpeg", kJpegA);
  block[4] = 0xFF;  // MIME length now far past the block.
  auto file = buildFlac({{6, block}, {4, flacCommentBlock({"TITLE=Still read"})}});
  FakeRawFile raw(file);

  TagResult result = FlacParser::parse(raw);

  TEST_ASSERT_EQUAL_STRING("Still read", result.title.c_str());
  TEST_ASSERT_FALSE(result.picture.present);
}

void test_tag_reader_uses_the_flac_parser_for_flac() {
  auto file = buildFlac({{4, flacCommentBlock({"ARTIST=Real Artist", "TITLE=Real Title"})}});
  FakeRawFile raw(file);

  TagResult result = TagReader::read(raw, "/Music/whatever.flac");

  TEST_ASSERT_EQUAL_STRING("Real Title", result.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Real Artist", result.artist.c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_id3v2_parses_title_artist_album_track);
  RUN_TEST(test_id3v2_decodes_utf16_bom_umlaut_to_utf8);
  RUN_TEST(test_id3v2_decodes_iso8859_1_byte_to_utf8);
  RUN_TEST(test_id3v2_missing_tag_returns_not_found);
  RUN_TEST(test_id3v2_falls_back_to_v1_trailer);
  RUN_TEST(test_id3v2_tcon_takes_plain_genre_text);
  RUN_TEST(test_id3v2_tcon_resolves_the_numbered_forms);
  RUN_TEST(test_id3v2_tcon_refining_text_wins_over_the_number);
  RUN_TEST(test_id3v2_tcon_remix_marker_and_unknown_number_yield_no_genre);
  RUN_TEST(test_id3v1_trailer_reads_the_genre_byte);
  RUN_TEST(test_id3v1_trailer_unset_genre_byte_yields_nothing);
  RUN_TEST(test_id3v2_truncated_frame_does_not_crash_and_yields_no_field);
  RUN_TEST(test_id3v2_extracts_embedded_jpeg_picture);
  RUN_TEST(test_id3v2_ignores_non_jpeg_picture);
  RUN_TEST(test_id3v2_apic_only_no_text_frames_still_found);
  RUN_TEST(test_id3v2_truncated_apic_frame_does_not_crash);
  RUN_TEST(test_vorbis_comment_parses_fields);
  RUN_TEST(test_vorbis_comment_missing_magic_returns_not_found);
  RUN_TEST(test_vorbis_comment_empty_list_returns_not_found);
  RUN_TEST(test_riff_info_parses_fields);
  RUN_TEST(test_riff_info_no_info_chunk_returns_not_found);
  RUN_TEST(test_tag_reader_falls_back_to_unknown_and_filename);
  RUN_TEST(test_tag_reader_uses_id3_when_present_for_mp3);
  RUN_TEST(test_id3v2_tpe2_stands_in_for_missing_tpe1);
  RUN_TEST(test_id3v2_tpe1_wins_over_tpe2);
  RUN_TEST(test_flac_reads_the_vorbis_comment_block);
  RUN_TEST(test_flac_skips_a_leading_id3v2_tag);
  RUN_TEST(test_flac_prefers_the_front_cover_jpeg);
  RUN_TEST(test_flac_takes_another_jpeg_when_there_is_no_front_cover);
  RUN_TEST(test_flac_ignores_a_png_picture);
  RUN_TEST(test_flac_without_magic_returns_not_found);
  RUN_TEST(test_flac_block_longer_than_the_file_does_not_crash);
  RUN_TEST(test_flac_picture_with_lying_lengths_does_not_crash);
  RUN_TEST(test_tag_reader_uses_the_flac_parser_for_flac);
  return UNITY_END();
}
