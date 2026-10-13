#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "AviReader.h"
#include "RawFile.h"
#include "VideoResume.h"
#include "VideoShelf.h"

using drehklang::video::AviReader;
using drehklang::video::filmTitles;

void setUp() {}
void tearDown() {}

namespace {

class MemoryFile : public drehklang::library::RawFile {
 public:
  explicit MemoryFile(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  size_t size() const override { return bytes_.size(); }
  bool seek(size_t position) override {
    if (position > bytes_.size()) return false;
    position_ = position;
    return true;
  }
  size_t read(uint8_t *buf, size_t n) override {
    const size_t count = std::min(n, bytes_.size() - position_);
    std::memcpy(buf, bytes_.data() + position_, count);
    position_ += count;
    return count;
  }

 private:
  std::vector<uint8_t> bytes_;
  size_t position_ = 0;
};

void putU32(std::vector<uint8_t> &out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void putChunk(std::vector<uint8_t> &out, const char *id, const std::vector<uint8_t> &body) {
  out.insert(out.end(), id, id + 4);
  putU32(out, static_cast<uint32_t>(body.size()));
  out.insert(out.end(), body.begin(), body.end());
  if (body.size() & 1) out.push_back(0);
}

// What ffmpeg writes for a picture-only film: hdrl, movi with one 00dc
// chunk per frame, idx1 with offsets from the 'movi' fourcc. Frame i is
// `sizes[i]` bytes of the value i.
std::vector<uint8_t> makeAvi(const std::vector<uint32_t> &sizes) {
  std::vector<uint8_t> movi = {'m', 'o', 'v', 'i'};
  std::vector<uint8_t> index;
  for (size_t i = 0; i < sizes.size(); ++i) {
    index.insert(index.end(), {'0', '0', 'd', 'c'});
    putU32(index, 16);
    putU32(index, static_cast<uint32_t>(movi.size()));
    putU32(index, sizes[i]);
    putChunk(movi, "00dc", std::vector<uint8_t>(sizes[i], static_cast<uint8_t>(i)));
  }
  std::vector<uint8_t> riff = {'A', 'V', 'I', ' '};
  putChunk(riff, "LIST", {'h', 'd', 'r', 'l'});
  putChunk(riff, "JUNK", std::vector<uint8_t>(6, 0));
  putChunk(riff, "LIST", movi);
  putChunk(riff, "idx1", index);
  std::vector<uint8_t> file;
  putChunk(file, "RIFF", riff);
  return file;
}

}  // namespace

void test_frames_come_in_order_with_their_numbers() {
  MemoryFile file(makeAvi({5, 3, 4}));
  AviReader avi;
  TEST_ASSERT_TRUE(avi.open(file));
  TEST_ASSERT_EQUAL_UINT32(3, avi.frameCount());
  uint8_t buf[16];
  uint32_t index = 99;
  TEST_ASSERT_EQUAL(5, avi.nextFrame(buf, sizeof(buf), index));
  TEST_ASSERT_EQUAL_UINT32(0, index);
  TEST_ASSERT_EQUAL(3, avi.nextFrame(buf, sizeof(buf), index));
  TEST_ASSERT_EQUAL_UINT32(1, index);
  TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
  TEST_ASSERT_EQUAL(4, avi.nextFrame(buf, sizeof(buf), index));
  TEST_ASSERT_EQUAL(0, avi.nextFrame(buf, sizeof(buf), index));
}

void test_seeking_lands_on_the_frame_the_index_names() {
  MemoryFile file(makeAvi({5, 3, 4, 7}));
  AviReader avi;
  TEST_ASSERT_TRUE(avi.open(file));
  TEST_ASSERT_TRUE(avi.seekFrame(2));
  uint8_t buf[16];
  uint32_t index = 0;
  TEST_ASSERT_EQUAL(4, avi.nextFrame(buf, sizeof(buf), index));
  TEST_ASSERT_EQUAL_UINT32(2, index);
  TEST_ASSERT_EQUAL_UINT8(2, buf[0]);
  TEST_ASSERT_FALSE(avi.seekFrame(4));
}

void test_a_frame_too_large_for_the_buffer_is_refused() {
  MemoryFile file(makeAvi({20}));
  AviReader avi;
  TEST_ASSERT_TRUE(avi.open(file));
  uint8_t buf[16];
  uint32_t index = 0;
  TEST_ASSERT_EQUAL(0, avi.nextFrame(buf, sizeof(buf), index));
}

void test_a_file_longer_than_its_riff_is_refused() {
  std::vector<uint8_t> bytes = makeAvi({4});
  bytes.insert(bytes.end(), {'R', 'I', 'F', 'F', 0, 0, 0, 0});  // OpenDML's next part.
  MemoryFile file(bytes);
  AviReader avi;
  TEST_ASSERT_FALSE(avi.open(file));
}

void test_not_an_avi_is_refused() {
  MemoryFile file({'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'});
  AviReader avi;
  TEST_ASSERT_FALSE(avi.open(file));
}

void test_a_film_needs_both_halves() {
  const std::vector<std::string> films =
      filmTitles({"Ein Film (1985).avi", "Ein Film (1985).m4a",
                  "Alone.avi", "Sound only.m4a", "._Ghost.avi", "._Ghost.m4a", "Ghost.AVI",
                  "ghost.m4a", "notes.txt"});
  TEST_ASSERT_EQUAL(2, films.size());
  TEST_ASSERT_EQUAL_STRING("Ein Film (1985)", films[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Ghost", films[1].c_str());
}

class FakeReader : public drehklang::library::DirectoryReader {
 public:
  std::string asked;
  std::vector<drehklang::library::FolderEntry> listChildren(const std::string &path) override {
    asked = path;
    return {{"Film.avi", false}, {"Film.m4a", false}, {"Old.avi", true}, {"Old.m4a", false}};
  }
};

void test_the_shelf_reads_the_videos_folder_and_skips_folders() {
  FakeReader reader;
  const std::vector<std::string> films = drehklang::video::filmTitlesIn(reader);
  TEST_ASSERT_EQUAL_STRING("/Videos", reader.asked.c_str());
  TEST_ASSERT_EQUAL(1, films.size());
  TEST_ASSERT_EQUAL_STRING("Film", films[0].c_str());
}

void test_resume_keys_fit_nvs_and_differ_per_title() {
  const std::string a = drehklang::video::soundKey("Ein Film (1985)");
  const std::string b = drehklang::video::soundKey("Ein Film II (1989)");
  TEST_ASSERT_TRUE(a.size() <= 15);
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_TRUE(drehklang::video::frameKey("x") != drehklang::video::soundKey("x"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_frames_come_in_order_with_their_numbers);
  RUN_TEST(test_seeking_lands_on_the_frame_the_index_names);
  RUN_TEST(test_a_frame_too_large_for_the_buffer_is_refused);
  RUN_TEST(test_a_file_longer_than_its_riff_is_refused);
  RUN_TEST(test_not_an_avi_is_refused);
  RUN_TEST(test_a_film_needs_both_halves);
  RUN_TEST(test_the_shelf_reads_the_videos_folder_and_skips_folders);
  RUN_TEST(test_resume_keys_fit_nvs_and_differ_per_title);
  return UNITY_END();
}
