#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "Crc16.h"
#include "Messages.h"
#include "Packet.h"
#include "PacketReader.h"

using namespace drehklang::btlink;

namespace {

std::vector<uint8_t> encoded(PacketType type, uint8_t seq, const uint8_t *payload,
                             uint16_t length) {
  std::vector<uint8_t> out(kMaxPacketBytes);
  out.resize(encodePacket(type, seq, payload, length, out.data()));
  return out;
}

// Feeds every byte; returns how many packets completed.
int feedAll(PacketReader &reader, const std::vector<uint8_t> &bytes) {
  int packets = 0;
  for (uint8_t b : bytes) packets += reader.feed(b) ? 1 : 0;
  return packets;
}

std::vector<uint8_t> statePacket(uint8_t seq, const StateMessage &m) {
  uint8_t payload[kMaxPayload];
  return encoded(PacketType::State, seq, payload,
                 static_cast<uint16_t>(encodeState(m, payload)));
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_crc_matches_the_ccitt_false_check_value() {
  const char *text = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(reinterpret_cast<const uint8_t *>(text), 9));
}

void test_state_round_trips_through_the_reader() {
  StateMessage sent;
  sent.link = LinkState::Connected;
  sent.scanning = true;
  sent.paired = true;
  sent.address = {1, 2, 3, 4, 5, 6};
  sent.name = "WH-1000XM4";
  PacketReader reader;
  TEST_ASSERT_EQUAL(1, feedAll(reader, statePacket(7, sent)));
  StateMessage got;
  TEST_ASSERT_TRUE(decodeState(reader.packet(), got));
  TEST_ASSERT_EQUAL(7, reader.packet().seq);
  TEST_ASSERT_EQUAL(static_cast<int>(LinkState::Connected), static_cast<int>(got.link));
  TEST_ASSERT_TRUE(got.scanning);
  TEST_ASSERT_TRUE(got.paired);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sent.address.data(), got.address.data(), 6);
  TEST_ASSERT_EQUAL_STRING("WH-1000XM4", got.name.c_str());
}

void test_a_wrong_crc_is_rejected() {
  const uint8_t payload[] = {kProtocolVersion};
  auto bytes = encoded(PacketType::Hello, 0, payload, 1);
  bytes[kHeaderBytes] ^= 0xFF;
  PacketReader reader;
  TEST_ASSERT_EQUAL(0, feedAll(reader, bytes));
  TEST_ASSERT_EQUAL(1, reader.crcErrors());
}

void test_garbage_before_a_packet_is_skipped() {
  const uint8_t payload[] = {kProtocolVersion};
  std::vector<uint8_t> bytes = {0x00, 0xD5, 0x12, 0xD5, 0xD5, 0x99};
  const auto packet = encoded(PacketType::Hello, 3, payload, 1);
  bytes.insert(bytes.end(), packet.begin(), packet.end());
  PacketReader reader;
  TEST_ASSERT_EQUAL(1, feedAll(reader, bytes));
  TEST_ASSERT_EQUAL(static_cast<int>(PacketType::Hello), static_cast<int>(reader.packet().type));
}

void test_the_reader_recovers_after_a_packet_cut_short() {
  const uint8_t payload[] = {kProtocolVersion};
  auto cut = encoded(PacketType::Hello, 1, payload, 1);
  cut.resize(cut.size() - 3);
  const auto a = encoded(PacketType::Hello, 2, payload, 1);
  const auto b = encoded(PacketType::Hello, 3, payload, 1);
  const auto c = encoded(PacketType::Hello, 4, payload, 1);
  std::vector<uint8_t> bytes = cut;
  for (const auto *p : {&a, &b, &c}) bytes.insert(bytes.end(), p->begin(), p->end());
  PacketReader reader;
  TEST_ASSERT_TRUE(feedAll(reader, bytes) >= 1);
  TEST_ASSERT_EQUAL(4, reader.packet().seq);
}

void test_sequence_gaps_count_as_lost_across_the_wrap() {
  const uint8_t payload[] = {kProtocolVersion};
  PacketReader reader;
  for (uint8_t seq : {254, 255, 0, 3}) {
    feedAll(reader, encoded(PacketType::Hello, seq, payload, 1));
  }
  TEST_ASSERT_EQUAL(2, reader.lostPackets());
}

void test_a_length_past_the_cap_is_rejected() {
  // A header claiming kMaxPayload + 1 bytes, as line noise might.
  const std::vector<uint8_t> bytes = {kSync0, kSync1, 0x01, 0x00,
                                      static_cast<uint8_t>((kMaxPayload + 1) & 0xFF),
                                      static_cast<uint8_t>((kMaxPayload + 1) >> 8)};
  PacketReader reader;
  TEST_ASSERT_EQUAL(0, feedAll(reader, bytes));
  TEST_ASSERT_EQUAL(1, reader.crcErrors());
}

void test_long_name_is_cut_at_a_character_boundary() {
  StateMessage sent;
  sent.name = std::string(31, 'a') + "\xC3\xBC" + "tail";  // 31 + ü + more.
  PacketReader reader;
  feedAll(reader, statePacket(0, sent));
  StateMessage got;
  TEST_ASSERT_TRUE(decodeState(reader.packet(), got));
  TEST_ASSERT_EQUAL(31, got.name.size());
}

void test_scan_result_pair_and_stats_round_trip() {
  uint8_t payload[kMaxPayload];
  PacketReader reader;

  ScanResultMessage scan;
  scan.address = {9, 8, 7, 6, 5, 4};
  scan.rssi = -61;
  scan.name = "Kopfh\xC3\xB6rer";
  feedAll(reader, encoded(PacketType::ScanResult, 0, payload,
                          static_cast<uint16_t>(encodeScanResult(scan, payload))));
  ScanResultMessage gotScan;
  TEST_ASSERT_TRUE(decodeScanResult(reader.packet(), gotScan));
  TEST_ASSERT_EQUAL(-61, gotScan.rssi);
  TEST_ASSERT_EQUAL_STRING("Kopfh\xC3\xB6rer", gotScan.name.c_str());

  PairMessage pair;
  pair.address = {1, 1, 2, 3, 5, 8};
  pair.name = "Buds";
  feedAll(reader, encoded(PacketType::Pair, 1, payload,
                          static_cast<uint16_t>(encodePair(pair, payload))));
  PairMessage gotPair;
  TEST_ASSERT_TRUE(decodePair(reader.packet(), gotPair));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pair.address.data(), gotPair.address.data(), 6);
  TEST_ASSERT_EQUAL_STRING("Buds", gotPair.name.c_str());

  StatsMessage stats{4000000000u, 2, 3};
  feedAll(reader, encoded(PacketType::Stats, 2, payload,
                          static_cast<uint16_t>(encodeStats(stats, payload))));
  StatsMessage gotStats;
  TEST_ASSERT_TRUE(decodeStats(reader.packet(), gotStats));
  TEST_ASSERT_EQUAL_UINT32(4000000000u, gotStats.underruns);
  TEST_ASSERT_EQUAL_UINT32(2, gotStats.crcErrors);
  TEST_ASSERT_EQUAL_UINT32(3, gotStats.lostPackets);
}

void test_a_name_running_past_the_payload_is_rejected() {
  StateMessage sent;
  sent.name = "abc";
  PacketReader reader;
  auto bytes = statePacket(0, sent);
  feedAll(reader, bytes);
  Packet truncated = reader.packet();
  truncated.length = static_cast<uint16_t>(truncated.length - 1);
  StateMessage got;
  TEST_ASSERT_FALSE(decodeState(truncated, got));
}

void test_copy_name_cuts_a_raw_name_at_a_character_boundary() {
  // A 40-byte name as Bluetooth reports it: 30 ASCII bytes, then a 4-byte
  // emoji straddling the 32-byte limit.
  const std::string raw = std::string(30, 'k') + "\xF0\x9F\x8E\xA7" + " Pro Max";
  char out[kMaxNameBytes + 1];
  copyName(reinterpret_cast<const uint8_t *>(raw.data()), raw.size(), out);
  TEST_ASSERT_EQUAL(30, std::strlen(out));
  TEST_ASSERT_EQUAL_STRING(std::string(30, 'k').c_str(), out);
}

void test_copy_name_keeps_a_short_name_whole() {
  const char raw[] = "Buds";
  char out[kMaxNameBytes + 1];
  copyName(reinterpret_cast<const uint8_t *>(raw), 4, out);
  TEST_ASSERT_EQUAL_STRING("Buds", out);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc_matches_the_ccitt_false_check_value);
  RUN_TEST(test_state_round_trips_through_the_reader);
  RUN_TEST(test_a_wrong_crc_is_rejected);
  RUN_TEST(test_garbage_before_a_packet_is_skipped);
  RUN_TEST(test_the_reader_recovers_after_a_packet_cut_short);
  RUN_TEST(test_sequence_gaps_count_as_lost_across_the_wrap);
  RUN_TEST(test_a_length_past_the_cap_is_rejected);
  RUN_TEST(test_long_name_is_cut_at_a_character_boundary);
  RUN_TEST(test_scan_result_pair_and_stats_round_trip);
  RUN_TEST(test_a_name_running_past_the_payload_is_rejected);
  RUN_TEST(test_copy_name_cuts_a_raw_name_at_a_character_boundary);
  RUN_TEST(test_copy_name_keeps_a_short_name_whole);
  return UNITY_END();
}
