#pragma once

#include <cstddef>
#include <cstdint>

#include "Crc16.h"
#include "Packet.h"

namespace drehklang::btlink {

// Turns the link's byte stream back into packets, one byte at a time.
// After a bad checksum or an impossible length it hunts for the next sync
// word; nothing is retransmitted. A gap in the sequence numbers counts as
// lost packets (ADR 0027).
class PacketReader {
 public:
  // True when this byte completed a packet with a good checksum, now in
  // packet() until the next one completes.
  bool feed(uint8_t byte) {
    switch (stage_) {
      case Stage::Sync0:
        if (byte == kSync0) stage_ = Stage::Sync1;
        return false;
      case Stage::Sync1:
        if (byte == kSync1) {
          stage_ = Stage::Header;
          got_ = 0;
        } else if (byte != kSync0) {
          stage_ = Stage::Sync0;
        }
        return false;
      case Stage::Header:
        header_[got_++] = byte;
        if (got_ < sizeof(header_)) return false;
        length_ = static_cast<uint16_t>(header_[2] | (header_[3] << 8));
        if (length_ > kMaxPayload) {
          ++crcErrors_;
          stage_ = Stage::Sync0;
          return false;
        }
        got_ = 0;
        stage_ = length_ == 0 ? Stage::Crc : Stage::Payload;
        return false;
      case Stage::Payload:
        packet_.payload[got_++] = byte;
        if (got_ == length_) {
          got_ = 0;
          stage_ = Stage::Crc;
        }
        return false;
      case Stage::Crc:
        crc_[got_++] = byte;
        if (got_ < sizeof(crc_)) return false;
        stage_ = Stage::Sync0;
        return finish();
    }
    return false;
  }

  const Packet &packet() const { return packet_; }
  uint32_t crcErrors() const { return crcErrors_; }
  uint32_t lostPackets() const { return lost_; }

 private:
  enum class Stage : uint8_t { Sync0, Sync1, Header, Payload, Crc };

  bool finish() {
    uint16_t crc = crc16(header_, sizeof(header_));
    crc = crc16(packet_.payload.data(), length_, crc);
    if (crc != static_cast<uint16_t>(crc_[0] | (crc_[1] << 8))) {
      ++crcErrors_;
      return false;
    }
    const uint8_t seq = header_[1];
    if (seen_) lost_ += static_cast<uint8_t>(seq - lastSeq_ - 1);
    seen_ = true;
    lastSeq_ = seq;
    packet_.type = static_cast<PacketType>(header_[0]);
    packet_.seq = seq;
    packet_.length = length_;
    return true;
  }

  Stage stage_ = Stage::Sync0;
  uint8_t header_[4] = {};  // Type, seq, length x2.
  uint8_t crc_[2] = {};
  size_t got_ = 0;
  uint16_t length_ = 0;
  Packet packet_;
  bool seen_ = false;
  uint8_t lastSeq_ = 0;
  uint32_t crcErrors_ = 0;
  uint32_t lost_ = 0;
};

}  // namespace drehklang::btlink
