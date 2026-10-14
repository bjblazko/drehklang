#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Packet.h"

namespace drehklang::btlink {

using Address = std::array<uint8_t, 6>;

// What the U4WDH's Bluetooth side is doing (STATE).
enum class LinkState : uint8_t { Off = 0, Idle = 1, Connecting = 2, Connected = 3 };

// A headphone button (BUTTON). Play and Pause stay apart: headphones send
// the one they think is due, and a toggle would undo a press they repeat.
enum class ButtonCode : uint8_t { Play = 1, Pause = 2 };

inline constexpr size_t kMaxNameBytes = 32;
// AUDIO: the sample rate (u32 LE), then this many stereo frames of int16 LE.
inline constexpr size_t kAudioFramesPerPacket = 256;
inline constexpr size_t kAudioHeaderBytes = 4;
inline constexpr size_t kAudioPayloadBytes = kAudioHeaderBytes + kAudioFramesPerPacket * 4;

inline void putU32(uint8_t *out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint32_t getU32(const uint8_t *in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

// How many bytes of `text` fit in `maxBytes` without cutting a UTF-8
// character in half.
inline size_t utf8Prefix(const std::string &text, size_t maxBytes) {
  if (text.size() <= maxBytes) return text.size();
  size_t n = maxBytes;
  while (n > 0 && (static_cast<uint8_t>(text[n]) & 0xC0) == 0x80) --n;
  return n;
}

// A name as Bluetooth reports it (EIR or remote name, up to 248 bytes,
// not terminated) into `out`, which has room for kMaxNameBytes + 1: cut at
// a character boundary and terminated. A raw cut at 32 bytes could end in
// half an emoji, and the half would then travel and be stored.
inline void copyName(const uint8_t *raw, size_t length, char *out) {
  const std::string name(reinterpret_cast<const char *>(raw), length);
  const size_t n = utf8Prefix(name, kMaxNameBytes);
  for (size_t i = 0; i < n; ++i) out[i] = name[i];
  out[n] = '\0';
}

namespace detail {

inline size_t putName(uint8_t *out, const std::string &name) {
  const size_t n = utf8Prefix(name, kMaxNameBytes);
  out[0] = static_cast<uint8_t>(n);
  for (size_t i = 0; i < n; ++i) out[1 + i] = static_cast<uint8_t>(name[i]);
  return 1 + n;
}

// False when the name is longer than allowed or runs past the payload.
inline bool getName(const Packet &packet, size_t at, std::string &name) {
  if (at >= packet.length) return false;
  const size_t n = packet.payload[at];
  if (n > kMaxNameBytes || at + 1 + n > packet.length) return false;
  name.assign(reinterpret_cast<const char *>(&packet.payload[at + 1]), n);
  return true;
}

inline void putAddress(uint8_t *out, const Address &address) {
  std::copy(address.begin(), address.end(), out);
}

inline void getAddress(const uint8_t *in, Address &address) {
  std::copy(in, in + address.size(), address.begin());
}

}  // namespace detail

// Remembered headphones (ADR 0027): the U4WDH keeps this many bonds and
// tries them in turn.
inline constexpr size_t kMaxPeers = 4;

struct Peer {
  Address address{};
  std::string name;
};

// STATE: link u8, flags u8 (bit 0 scanning), active i8 (the peer the link
// is with or being made with, -1 none), count u8, then count x (address,
// name), the most recently used first.
struct StateMessage {
  LinkState link = LinkState::Off;
  bool scanning = false;
  int8_t active = -1;
  std::vector<Peer> peers;
};

inline size_t encodeState(const StateMessage &m, uint8_t *out) {
  out[0] = static_cast<uint8_t>(m.link);
  out[1] = static_cast<uint8_t>(m.scanning ? 1 : 0);
  out[2] = static_cast<uint8_t>(m.active);
  const size_t count = std::min(m.peers.size(), kMaxPeers);
  out[3] = static_cast<uint8_t>(count);
  size_t at = 4;
  for (size_t i = 0; i < count; ++i) {
    detail::putAddress(out + at, m.peers[i].address);
    at += 6;
    at += detail::putName(out + at, m.peers[i].name);
  }
  return at;
}

inline bool decodeState(const Packet &p, StateMessage &m) {
  if (p.type != PacketType::State || p.length < 4 || p.payload[0] > 3) return false;
  const size_t count = p.payload[3];
  const auto active = static_cast<int8_t>(p.payload[2]);
  if (count > kMaxPeers || active < -1 || active >= static_cast<int>(count)) return false;
  m.link = static_cast<LinkState>(p.payload[0]);
  m.scanning = (p.payload[1] & 1) != 0;
  m.active = active;
  m.peers.assign(count, Peer{});
  size_t at = 4;
  for (auto &peer : m.peers) {
    if (at + 6 > p.length) return false;
    detail::getAddress(&p.payload[at], peer.address);
    at += 6;
    if (!detail::getName(p, at, peer.name)) return false;
    at += 1 + peer.name.size();
  }
  return true;
}

// SELECT and FORGET: one remembered peer's address.
inline size_t encodeAddress(const Address &address, uint8_t *out) {
  detail::putAddress(out, address);
  return 6;
}

inline bool decodeAddress(const Packet &p, PacketType type, Address &address) {
  if (p.type != type || p.length < 6) return false;
  detail::getAddress(&p.payload[0], address);
  return true;
}

// SCAN_RESULT: address, RSSI i8, name.
struct ScanResultMessage {
  Address address{};
  int8_t rssi = 0;
  std::string name;
};

inline size_t encodeScanResult(const ScanResultMessage &m, uint8_t *out) {
  detail::putAddress(out, m.address);
  out[6] = static_cast<uint8_t>(m.rssi);
  return 7 + detail::putName(out + 7, m.name);
}

inline bool decodeScanResult(const Packet &p, ScanResultMessage &m) {
  if (p.type != PacketType::ScanResult || p.length < 8) return false;
  detail::getAddress(&p.payload[0], m.address);
  m.rssi = static_cast<int8_t>(p.payload[6]);
  return detail::getName(p, 7, m.name);
}

// PAIR: address, name (so the U4WDH can report it in STATE afterwards).
struct PairMessage {
  Address address{};
  std::string name;
};

inline size_t encodePair(const PairMessage &m, uint8_t *out) {
  detail::putAddress(out, m.address);
  return 6 + detail::putName(out + 6, m.name);
}

inline bool decodePair(const Packet &p, PairMessage &m) {
  if (p.type != PacketType::Pair || p.length < 7) return false;
  detail::getAddress(&p.payload[0], m.address);
  return detail::getName(p, 6, m.name);
}

// STATS: three u32 LE counters, every 5 s.
struct StatsMessage {
  uint32_t underruns = 0;
  uint32_t crcErrors = 0;
  uint32_t lostPackets = 0;
};

inline size_t encodeStats(const StatsMessage &m, uint8_t *out) {
  putU32(out, m.underruns);
  putU32(out + 4, m.crcErrors);
  putU32(out + 8, m.lostPackets);
  return 12;
}

inline bool decodeStats(const Packet &p, StatsMessage &m) {
  if (p.type != PacketType::Stats || p.length < 12) return false;
  m.underruns = getU32(&p.payload[0]);
  m.crcErrors = getU32(&p.payload[4]);
  m.lostPackets = getU32(&p.payload[8]);
  return true;
}

}  // namespace drehklang::btlink
