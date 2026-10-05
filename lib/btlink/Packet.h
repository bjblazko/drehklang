#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Crc16.h"

namespace drehklang::btlink {

// The link between the S3 and the ESP32-U4WDH (ADR 0027). Bump the version
// on any change to a packet's layout: HELLO carries it, and the S3 refuses
// a U4WDH firmware that speaks another one.
inline constexpr uint8_t kProtocolVersion = 1;
inline constexpr uint32_t kBaudRate = 3000000;
inline constexpr uint8_t kSync0 = 0xD5;
inline constexpr uint8_t kSync1 = 0x4B;
inline constexpr size_t kMaxPayload = 1100;
inline constexpr size_t kHeaderBytes = 6;  // Sync x2, type, seq, length x2.
inline constexpr size_t kCrcBytes = 2;
inline constexpr size_t kMaxPacketBytes = kHeaderBytes + kMaxPayload + kCrcBytes;

enum class PacketType : uint8_t {
  // Both ways.
  Hello = 0x01,
  // S3 -> U4WDH.
  Enable = 0x02,
  ScanStart = 0x03,
  ScanStop = 0x04,
  Pair = 0x05,
  Forget = 0x06,
  Audio = 0x10,
  // U4WDH -> S3.
  State = 0x41,
  ScanResult = 0x42,
  Button = 0x43,
  Stats = 0x44,
};

struct Packet {
  PacketType type = PacketType::Hello;
  uint8_t seq = 0;
  uint16_t length = 0;
  std::array<uint8_t, kMaxPayload> payload{};
};

// One packet into `out`, which has room for kHeaderBytes + length +
// kCrcBytes. Returns the bytes written; 0 when `length` is over the cap.
inline size_t encodePacket(PacketType type, uint8_t seq, const uint8_t *payload,
                           uint16_t length, uint8_t *out) {
  if (length > kMaxPayload) return 0;
  out[0] = kSync0;
  out[1] = kSync1;
  out[2] = static_cast<uint8_t>(type);
  out[3] = seq;
  out[4] = static_cast<uint8_t>(length & 0xFF);
  out[5] = static_cast<uint8_t>(length >> 8);
  for (uint16_t i = 0; i < length; ++i) out[kHeaderBytes + i] = payload[i];
  const uint16_t crc = crc16(out + 2, 4u + length);
  out[kHeaderBytes + length] = static_cast<uint8_t>(crc & 0xFF);
  out[kHeaderBytes + length + 1] = static_cast<uint8_t>(crc >> 8);
  return kHeaderBytes + length + kCrcBytes;
}

}  // namespace drehklang::btlink
