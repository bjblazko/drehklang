#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "MicConditioner.h"
#include "RecordingShelf.h"
#include "WavHeader.h"

using namespace drehklang::dictaphone;

void setUp() {}
void tearDown() {}

namespace {

uint32_t le32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

constexpr WavFormat kVoice{16000, 1, 16};

std::vector<drehklang::library::FolderEntry> files(const std::vector<std::string> &names) {
  std::vector<drehklang::library::FolderEntry> out;
  for (const auto &name : names) out.push_back({name, false, 0});
  return out;
}

}  // namespace

// --- WavHeader ---

void test_header_describes_16k_mono_pcm() {
  uint8_t h[kWavHeaderBytes];
  writeWavHeader(h, kVoice, 32000);
  TEST_ASSERT_EQUAL_MEMORY("RIFF", h, 4);
  TEST_ASSERT_EQUAL_UINT32(36 + 32000, le32(h + 4));
  TEST_ASSERT_EQUAL_MEMORY("WAVEfmt ", h + 8, 8);
  TEST_ASSERT_EQUAL_UINT32(16, le32(h + 16));
  TEST_ASSERT_EQUAL_UINT16(1, le16(h + 20));  // PCM
  TEST_ASSERT_EQUAL_UINT16(1, le16(h + 22));
  TEST_ASSERT_EQUAL_UINT32(16000, le32(h + 24));
  TEST_ASSERT_EQUAL_UINT32(32000, le32(h + 28));  // byte rate
  TEST_ASSERT_EQUAL_UINT16(2, le16(h + 32));      // block align
  TEST_ASSERT_EQUAL_UINT16(16, le16(h + 34));
  TEST_ASSERT_EQUAL_MEMORY("data", h + 36, 4);
  TEST_ASSERT_EQUAL_UINT32(32000, le32(h + 40));
}

void test_reading_back_a_written_header() {
  uint8_t h[kWavHeaderBytes];
  writeWavHeader(h, kVoice, 64000);
  const WavInfo info = readWavHeader(h, sizeof(h), kWavHeaderBytes + 64000);
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(16000, info.format.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(1, info.format.channels);
  TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(64000, info.dataBytes);
  TEST_ASSERT_EQUAL_UINT32(2000, info.durationMs());
}

// A reset while recording leaves the placeholder's zero sizes behind.
void test_zero_data_size_is_taken_from_the_file_length() {
  uint8_t h[kWavHeaderBytes];
  writeWavHeader(h, kVoice, 0);
  const WavInfo info = readWavHeader(h, sizeof(h), kWavHeaderBytes + 48001);
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(48000, info.dataBytes);  // whole frames only
}

void test_data_size_past_the_end_is_clamped_to_the_file() {
  uint8_t h[kWavHeaderBytes];
  writeWavHeader(h, kVoice, 100000);
  const WavInfo info = readWavHeader(h, sizeof(h), kWavHeaderBytes + 1000);
  TEST_ASSERT_EQUAL_UINT32(1000, info.dataBytes);
}

void test_chunks_before_data_are_skipped() {
  // A WAV saved by another tool may carry a LIST chunk before "data".
  std::vector<uint8_t> bytes(kWavHeaderBytes);
  writeWavHeader(bytes.data(), kVoice, 10);
  const uint8_t list[] = {'L', 'I', 'S', 'T', 4, 0, 0, 0, 'I', 'N', 'F', 'O'};
  bytes.insert(bytes.begin() + 36, std::begin(list), std::end(list));
  const WavInfo info = readWavHeader(bytes.data(), bytes.size(), bytes.size() + 10);
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(56, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(10, info.dataBytes);
}

void test_not_a_wav_is_invalid() {
  uint8_t h[kWavHeaderBytes] = {};
  std::memcpy(h, "ID3", 3);
  TEST_ASSERT_FALSE(readWavHeader(h, sizeof(h), 1000).valid);
}

void test_compressed_wav_is_invalid() {
  uint8_t h[kWavHeaderBytes];
  writeWavHeader(h, kVoice, 10);
  h[20] = 2;  // ADPCM
  TEST_ASSERT_FALSE(readWavHeader(h, sizeof(h), 54).valid);
}

// --- RecordingShelf ---

void test_recording_numbers_are_read_from_names() {
  TEST_ASSERT_EQUAL_INT(1, recordingNumber("REC0001.wav"));
  TEST_ASSERT_EQUAL_INT(42, recordingNumber("rec0042.WAV"));
  TEST_ASSERT_EQUAL_INT(12345, recordingNumber("REC12345.wav"));
  TEST_ASSERT_EQUAL_INT(-1, recordingNumber("REC.wav"));
  TEST_ASSERT_EQUAL_INT(-1, recordingNumber("REC00a1.wav"));
  TEST_ASSERT_EQUAL_INT(-1, recordingNumber("Song.wav"));
  TEST_ASSERT_EQUAL_INT(-1, recordingNumber("REC0001.mp3"));
  TEST_ASSERT_EQUAL_INT(-1, recordingNumber("._REC0001.wav"));
}

void test_recordings_list_newest_first() {
  const auto list = recordings(files({"REC0002.wav", "notes.txt", "REC0010.wav", "REC0001.wav"}));
  TEST_ASSERT_EQUAL(3, list.size());
  TEST_ASSERT_EQUAL_INT(10, list[0].number);
  TEST_ASSERT_EQUAL_STRING("REC0010.wav", list[0].fileName.c_str());
  TEST_ASSERT_EQUAL_INT(2, list[1].number);
  TEST_ASSERT_EQUAL_INT(1, list[2].number);
}

void test_next_number_follows_the_highest() {
  TEST_ASSERT_EQUAL_INT(1, nextRecordingNumber({}));
  // A deleted number in the middle is not reused.
  TEST_ASSERT_EQUAL_INT(8, nextRecordingNumber(recordings(files({"REC0001.wav", "REC0007.wav"}))));
}

void test_names_and_titles() {
  TEST_ASSERT_EQUAL_STRING("REC0003.wav", recordingFileName(3).c_str());
  TEST_ASSERT_EQUAL_STRING("REC10000.wav", recordingFileName(10000).c_str());
  TEST_ASSERT_EQUAL_STRING("/Recordings/REC0003.wav", recordingPath("REC0003.wav").c_str());
  TEST_ASSERT_EQUAL_STRING("Recording 3", recordingTitle(3).c_str());
}

// The list shows each recording's length, from the size the directory
// listing already has -- no file is opened for it.
void test_recording_keeps_its_file_size() {
  const auto list = recordings({{"REC0001.wav", false, 44 + 64000}, {"REC0002", true, 0}});
  TEST_ASSERT_EQUAL(1, list.size());
  TEST_ASSERT_EQUAL_UINT32(44 + 64000, list[0].bytes);
}

void test_length_comes_from_the_file_size() {
  TEST_ASSERT_EQUAL_UINT32(2000, recordingDurationMs(kWavHeaderBytes + 64000));
  TEST_ASSERT_EQUAL_UINT32(0, recordingDurationMs(kWavHeaderBytes));
  TEST_ASSERT_EQUAL_UINT32(0, recordingDurationMs(10));
}

void test_durations_are_formatted_like_a_clock() {
  char text[16];
  formatDuration(text, sizeof(text), 0);
  TEST_ASSERT_EQUAL_STRING("0:00", text);
  formatDuration(text, sizeof(text), 65999);
  TEST_ASSERT_EQUAL_STRING("1:05", text);
  formatDuration(text, sizeof(text), 3723000);
  TEST_ASSERT_EQUAL_STRING("1:02:03", text);
}

// --- MicConditioner ---

void test_dc_offset_is_removed() {
  MicConditioner mic(8.0f);
  std::vector<int16_t> in(16000, 1375);
  std::vector<int16_t> out(in.size());
  mic.process(in.data(), out.data(), in.size());
  // After a second the offset is gone.
  TEST_ASSERT_INT_WITHIN(2, 0, out.back());
}

void test_first_block_starts_without_a_jump() {
  MicConditioner mic(8.0f);
  std::vector<int16_t> in(64, 1375);
  std::vector<int16_t> out(in.size());
  mic.process(in.data(), out.data(), in.size());
  TEST_ASSERT_INT_WITHIN(2, 0, out[0]);
}

void test_speech_band_is_amplified_by_the_gain() {
  MicConditioner mic(8.0f);
  std::vector<int16_t> in(16000);
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] = static_cast<int16_t>(1375 + 1000 * std::sin(2 * M_PI * 1000 * i / 16000.0));
  }
  std::vector<int16_t> out(in.size());
  mic.process(in.data(), out.data(), in.size());
  int peak = 0;
  for (size_t i = 8000; i < out.size(); ++i) peak = std::max(peak, std::abs(out[i]));
  TEST_ASSERT_INT_WITHIN(200, 8000, peak);
}

void test_loud_input_clips_instead_of_wrapping() {
  MicConditioner mic(8.0f);
  std::vector<int16_t> in(1600);
  for (size_t i = 0; i < in.size(); ++i) in[i] = (i / 8) % 2 ? 20000 : -20000;
  std::vector<int16_t> out(in.size());
  mic.process(in.data(), out.data(), in.size());
  for (size_t i = 0; i < out.size(); ++i) {
    if (in[i] > 0 && i % 8 == 0) TEST_ASSERT_TRUE(out[i] > 0);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header_describes_16k_mono_pcm);
  RUN_TEST(test_reading_back_a_written_header);
  RUN_TEST(test_zero_data_size_is_taken_from_the_file_length);
  RUN_TEST(test_data_size_past_the_end_is_clamped_to_the_file);
  RUN_TEST(test_chunks_before_data_are_skipped);
  RUN_TEST(test_not_a_wav_is_invalid);
  RUN_TEST(test_compressed_wav_is_invalid);
  RUN_TEST(test_recording_numbers_are_read_from_names);
  RUN_TEST(test_recordings_list_newest_first);
  RUN_TEST(test_next_number_follows_the_highest);
  RUN_TEST(test_names_and_titles);
  RUN_TEST(test_recording_keeps_its_file_size);
  RUN_TEST(test_length_comes_from_the_file_size);
  RUN_TEST(test_durations_are_formatted_like_a_clock);
  RUN_TEST(test_dc_offset_is_removed);
  RUN_TEST(test_first_block_starts_without_a_jump);
  RUN_TEST(test_speech_band_is_amplified_by_the_gain);
  RUN_TEST(test_loud_input_clips_instead_of_wrapping);
  return UNITY_END();
}
