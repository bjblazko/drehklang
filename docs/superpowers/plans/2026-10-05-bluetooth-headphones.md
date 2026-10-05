# Bluetooth headphones Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Everything Drehklang plays also reaches paired Bluetooth headphones, through a new firmware on the board's second chip (ESP32-U4WDH) fed over the shared UART, while the jack keeps playing.

**Architecture:** The S3 copies what the DAC's current owner plays (player or tone output) into a PSRAM ring (`AudioTap`), and a link task sends it as CRC'd packets over UART1 at 3 Mbaud (`BtLink`). The U4WDH buffers the stream, resamples it adaptively to 44.1 kHz (`HeadphoneStream`), and hands it to ESP-IDF's A2DP source, which encodes SBC. A main-loop state machine on the S3 (`BtController`) handles handshake, state, pairing and the Play/Pause button; Settings > Bluetooth is an ordinary list.

**Tech Stack:** C++17, PlatformIO with pioarduino 55.03.312-1 (Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5), ESP-IDF UART driver and Bluedroid (A2DP source, AVRCP target, GAP), LVGL 8.4, Unity host tests (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-10-05-bluetooth-headphones-design.md` (Task 1 brings it in line with the throughput spike run on 2026-10-05).

## Global Constraints

- S3 link pins: **TX GPIO48, RX GPIO38** (measured 2026-10-05; device.md had them swapped). U4WDH link pins: TX IO18, RX IO23. UART1 on the S3, UART2 on the U4WDH.
- Link speed `btlink::kBaudRate = 3000000`, 8N1, no flow control. Both ends call `uart_set_rx_full_threshold(port, 64)`: at the default 120 of 128 the U4WDH lost ~5 % of packets at 3 Mbaud; at 64 a 10-minute saturated run in both directions had zero errors.
- `btlink::kProtocolVersion = 1`. Packet: `0xD5 0x4B | type u8 | seq u8 | length u16 LE | payload | CRC16-CCITT-FALSE LE` over type..payload.
- AUDIO payload: sample rate u32 LE, then 256 stereo frames of int16 LE (1028 bytes).
- Never `Serial.printf()` from an audio or link task (AGENTS.md). Only the main loop prints.
- UI copy is English and exactly as written in this plan. Colours only from `lib/ui/Theme.h`. No new widgets.
- `CollectionId`, `ScreenKind` and `kMenuEntries` are append-only (AGENTS.md).
- Pure logic lives in header-only libraries under `lib/` and is host-tested; drivers are not.
- Verification commands: `pio test -e native`, `pio run -e esp32-s3`, `pio run -d bt -e esp32-bt`.
- Commit after every task, messages ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Work on branch `bluetooth-headphones-spec`.

## Review Focus

1. **A file at a rate other than 44.1/48/22.05 kHz** (32 kHz MP3, 96 kHz FLAC) must still reach the headphones at the right pitch. Rate travels as a u32, and `HeadphoneStream` resets the resampler on any change (Task 4, `test_rate_change_resets_and_keeps_pitch`).
2. **Headphones that send Play while already playing** must not pause. Play and Pause stay apart, so a repeated press does nothing (Task 6, `test_play_while_playing_does_nothing`).
3. **The U4WDH restarting while the S3 runs** (brown-out, reflash) must come back without a reboot of the S3. A STATE that disagrees with the enabled setting resends ENABLE (Task 6, `test_state_disagreeing_with_enabled_resends_enable`).
4. **A device name longer than 32 bytes with umlauts or emoji** must arrive as valid UTF-8 (Task 2, `test_long_name_is_cut_at_a_character_boundary`).
5. **The same headphones reported twice during a scan** (updated RSSI or a name that arrives later) must show once (Task 6, `test_scan_result_for_known_address_updates_in_place`).

---

## File Structure

| File | Responsibility |
|---|---|
| `lib/btlink/Crc16.h` | CRC-16/CCITT-FALSE |
| `lib/btlink/Packet.h` | Constants, `PacketType`, `Packet`, `encodePacket()` |
| `lib/btlink/PacketReader.h` | Byte-at-a-time decoder with resync and loss counting |
| `lib/btlink/Messages.h` | Payload layouts of every message, UTF-8-safe names |
| `lib/btaudio/PcmRing.h` | Lock-free SPSC ring of int16 samples on caller storage |
| `lib/btaudio/AdaptiveResampler.h` | Linear resampler to 44.1 kHz, fill-trimmed, prebuffer and underrun handling |
| `lib/btaudio/HeadphoneStream.h` | U4WDH side: packet → ring, rate changes, render for A2DP |
| `lib/bluetooth/AudioTap.h` | S3 side: DAC owner's samples → ring, gated, never blocks |
| `lib/bluetooth/BtController.h` | S3 main-loop state machine: handshake, state, scan list, pairing, events |
| `lib/bluetooth/HeadphoneButton.h` | What Play or Pause from the headphones does where |
| `lib/drivers-bluetooth/BtLink.{h,cpp}` | S3 UART1 driver and link task |
| `lib/drivers-audio/AudioHooks.{h,cpp}` | Gains the tap and the decoder rate |
| `lib/drivers-audio/Esp32AudioI2SDriver.cpp` | Publishes the decoder rate |
| `lib/drivers-audio/ToneOutput.cpp` | Copies each written chunk to the tap |
| `src/main.cpp` | Wiring, the `BT` serial command |
| `lib/navigation/ScreenId.h` | `Bluetooth`, `BluetoothSearch` appended |
| `lib/ui/ScreenManager.h`, `ScreenManager.cpp`, `ScreenManagerMenu.cpp` | Settings row values from the table; list dispatch by row id |
| `lib/ui/ScreenManagerBluetooth.cpp` | The two Bluetooth screens, messages, headphone button |
| `lib/ui/BatteryIndicator.h` | Bluetooth glyph on the lock screen |
| `bt/platformio.ini` | The U4WDH firmware project |
| `bt/src/main.cpp` | U4WDH wiring and loop: link messages ↔ Bluetooth |
| `bt/src/LinkUart.{h,cpp}` | U4WDH UART2 driver, receive task, send |
| `bt/src/BtSource.{h,cpp}` | Bluedroid: GAP scan and pairing, A2DP source, AVRCP target |
| `bt/src/PeerStore.{h,cpp}` | The paired headphones in NVS |
| `scripts/flash-bt-mcu.sh` | Flash `bt/` after checking that the CH340 reaches the ESP32 |
| `test/test_btlink/`, `test_pcm_ring/`, `test_adaptive_resampler/`, `test_bt_audio_tap/`, `test_bt_controller/`, `test_headphone_button/` | Host tests |
| `docs/adr/0027-bluetooth-headphones.md`, `device.md`, `AGENTS.md`, `docs/design/ux-guidelines.md`, `THIRD-PARTY.md`, `lib/about/Credits.h` | Docs and credits |

---

### Task 1: Record the throughput spike

The spike (throwaway sketches, not in the repo) ran on 2026-10-05:

- **Pins.** S3 GPIO38 carries the U4WDH's TX, so the S3 transmits on GPIO48.
- **Clean run.** At 3 Mbaud (S3 asked 3 000 000, U4WDH got 3 004 694), with the RX threshold at 64, 10 minutes of both directions saturated at ~301 KB/s gave 0 CRC errors, 0 lost packets and 0 FIFO overflows: 174 413 packets one way and 186 515 the other.
- **Default threshold.** At the default threshold the U4WDH received ~5 % bad packets.
- **Port open resets the U4WDH.** Opening the CH340 port with pyserial's defaults holds the U4WDH in reset (RTS).
- **Both orientations.** The link works with the cable either way round.
- **Chip.** esptool reports `ESP32-U4WDH (revision v3.1)`, dual core, 240 MHz.

**Files:**
- Modify: `device.md` (the "Inter-MCU UART" table row and "The second chip and the audio switch")
- Modify: `AGENTS.md` (new gotcha entry)
- Modify: `docs/superpowers/specs/2026-10-05-bluetooth-headphones-design.md`

- [ ] **Step 1: Fix device.md.** Replace the table row text `TX 38, RX 48 (schematic ESP32S3_TX/ESP32S3_RX; 43/44 are the S3's own UART0, an earlier mistake here)` with:

```markdown
| | Inter-MCU UART (to the ESP32-U4WDH) | **TX 48, RX 38** (measured 2026-10-05: the U4WDH's TX arrives on GPIO38; the schematic's `ESP32S3_TX`/`ESP32S3_RX` are named from the other chip's side. 43/44 are the S3's own UART0) |
```

In "The second chip and the audio switch", replace the bullet starting "**The chips share a UART:**" with:

```markdown
- **The chips share a UART:** ESP32 IO18 (TX) to S3 GPIO38 (RX), and S3
  GPIO48 (TX) to ESP32 IO23 (RX). There are no flow-control lines.
  Measured 2026-10-05 with a pin probe and a throughput test: 3 Mbaud
  both ways, saturated for 10 minutes, no errors -- once both receivers
  lower their RX FIFO threshold to 64 bytes
  (`uart_set_rx_full_threshold`). At the driver's default (120 of 128
  bytes) the ESP32 dropped bytes at 3 Mbaud. The link works with the
  USB-C cable either way round.
- **The ESP32-U4WDH is dual core, 240 MHz** (esptool: "ESP32-U4WDH
  (revision v3.1)", 2026-10-05).
```

- [ ] **Step 2: Add the AGENTS.md entry** after the "The DAC has one owner at a time" bullet:

```markdown
- **Talking to the second chip (ESP32-U4WDH).** Its UART0 is the CH340
  side of the cable (flip the USB-C plug; `/dev/cu.usbserial-*`). Opening
  that port with pyserial's defaults asserts RTS and **holds the chip in
  reset** -- set `dtr = rts = False` before `open()`, or the log stays
  empty. The link between the chips is S3 GPIO48 (TX) / GPIO38 (RX) at
  3 Mbaud, and both ends must lower the RX FIFO threshold to 64 or bytes
  get lost (device.md, ADR 0027). `scripts/flash-bt-mcu.sh` flashes it.
```

- [ ] **Step 3: Bring the spec in line.** Edit the spec:
  - In "Why", swap the pin lines to `S3 GPIO48 (TX) → U4WDH IO23 (RX)` and `U4WDH IO18 (TX) → S3 GPIO38 (RX)`, and add the sentence "Measured on 2026-10-05: 3 Mbaud works both ways once both receivers lower their RX FIFO threshold to 64 bytes."
  - In the protocol section, make the AUDIO payload "sample rate u32 LE, then 256 stereo frames, 16-bit LE (1028 bytes)" instead of the rate code, so any source rate (32 kHz MP3, 96 kHz FLAC) gets through.
  - Make BUTTON "button u8: 1 = Play, 2 = Pause (kept apart: a toggle would undo a press the headphones repeat)".
  - In `BtController`'s states, replace `Scanning` with `Starting` (the first 3 s before any answer). Add that scanning is a flag beside the state, because a scan can run while connected.
  - Remove "value `confirm` green when on" from the Bluetooth screen sketch. The `On`/`Off` value is plain text like Settings > Main menu's.
  - Replace "With nothing found, the screen shows `No headphones found. Is pairing mode on?` under `Search again`" with "With nothing found, a message says `None found. Pairing mode on?`".
  - In `BtLink`, give GPIO48 as TX and GPIO38 as RX, and add "RX FIFO threshold 64".
  - In "Verification on the device", mark step 1 as done on 2026-10-05 and give the result.
  - Add a step 0: "With the U4WDH running anything but its factory image, music still reaches the jack. The U4WDH's IO32 drives the DAC's XSMT; if the jack is silent, the BT firmware must drive IO32 high."

- [ ] **Step 4: Commit**

```bash
git add device.md AGENTS.md docs/superpowers/specs/2026-10-05-bluetooth-headphones-design.md
git commit -m "Record the inter-chip UART measurements

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The link protocol (`lib/btlink/`)

**Files:**
- Create: `lib/btlink/Crc16.h`, `lib/btlink/Packet.h`, `lib/btlink/PacketReader.h`, `lib/btlink/Messages.h`
- Test: `test/test_btlink/test_btlink.cpp`

**Interfaces:**
- Produces: `btlink::crc16(const uint8_t*, size_t, uint16_t = 0xFFFF)`, `btlink::kProtocolVersion`, `kBaudRate`, `kMaxPayload`, `kMaxPacketBytes`, `enum class PacketType`, `struct Packet {type, seq, length, payload}`, `size_t encodePacket(PacketType, uint8_t seq, const uint8_t*, uint16_t, uint8_t *out)`, `class PacketReader { bool feed(uint8_t); const Packet &packet() const; uint32_t crcErrors() const; uint32_t lostPackets() const; }`, `using Address = std::array<uint8_t,6>`, `enum class LinkState`, `enum class ButtonCode`, `kMaxNameBytes`, `kAudioFramesPerPacket`, `kAudioHeaderBytes`, `kAudioPayloadBytes`, `StateMessage`, `ScanResultMessage`, `PairMessage`, `StatsMessage` with `encodeX(const X&, uint8_t*) -> size_t` and `decodeX(const Packet&, X&) -> bool`, `putU32`/`getU32`, `utf8Prefix(const std::string&, size_t)`.

- [ ] **Step 1: Write the failing test** `test/test_btlink/test_btlink.cpp`:

```cpp
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
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_btlink`
Expected: FAIL, `Crc16.h: No such file or directory`.

- [ ] **Step 3: Write `lib/btlink/Crc16.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace drehklang::btlink {

// CRC-16/CCITT-FALSE (polynomial 0x1021, start 0xFFFF), the link's
// checksum (ADR 0027). "123456789" gives 0x29B1. Bitwise rather than a
// table: at 300 KB/s it costs the U4WDH about 4 % of one core.
inline uint16_t crc16(const uint8_t *data, size_t length, uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i] << 8);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace drehklang::btlink
```

- [ ] **Step 4: Write `lib/btlink/Packet.h`**

```cpp
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
```

- [ ] **Step 5: Write `lib/btlink/PacketReader.h`**

```cpp
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
```

- [ ] **Step 6: Write `lib/btlink/Messages.h`**

```cpp
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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

// STATE: link u8, flags u8 (bit 0 scanning, bit 1 paired), the paired
// address, the paired name.
struct StateMessage {
  LinkState link = LinkState::Off;
  bool scanning = false;
  bool paired = false;
  Address address{};
  std::string name;
};

inline size_t encodeState(const StateMessage &m, uint8_t *out) {
  out[0] = static_cast<uint8_t>(m.link);
  out[1] = static_cast<uint8_t>((m.scanning ? 1 : 0) | (m.paired ? 2 : 0));
  detail::putAddress(out + 2, m.address);
  return 8 + detail::putName(out + 8, m.name);
}

inline bool decodeState(const Packet &p, StateMessage &m) {
  if (p.type != PacketType::State || p.length < 9 || p.payload[0] > 3) return false;
  m.link = static_cast<LinkState>(p.payload[0]);
  m.scanning = (p.payload[1] & 1) != 0;
  m.paired = (p.payload[1] & 2) != 0;
  detail::getAddress(&p.payload[2], m.address);
  return detail::getName(p, 8, m.name);
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
```

- [ ] **Step 7: Run the tests to see them pass**

Run: `pio test -e native -f test_btlink`
Expected: 10 tests, PASS.

- [ ] **Step 8: Commit**

```bash
git add lib/btlink test/test_btlink
git commit -m "Add the inter-chip link protocol for Bluetooth

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The sample ring (`lib/btaudio/PcmRing.h`)

**Files:**
- Create: `lib/btaudio/PcmRing.h`
- Test: `test/test_pcm_ring/test_pcm_ring.cpp`

**Interfaces:**
- Produces: `class btaudio::PcmRing { PcmRing(); PcmRing(int16_t*, uint32_t); void attach(int16_t*, uint32_t); uint32_t capacity() const; uint32_t available() const; template<class Source> bool write(uint32_t count, Source); bool write(const int16_t*, uint32_t); uint32_t read(int16_t*, uint32_t); void clear(); }`

- [ ] **Step 1: Write the failing test**

```cpp
#include <unity.h>

#include <cstdint>

#include "PcmRing.h"

using drehklang::btaudio::PcmRing;

void setUp() {}
void tearDown() {}

void test_reads_back_in_order() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  const int16_t in[] = {1, 2, 3, 4};
  TEST_ASSERT_TRUE(ring.write(in, 4));
  TEST_ASSERT_EQUAL(4, ring.available());
  int16_t out[4] = {};
  TEST_ASSERT_EQUAL(4, ring.read(out, 4));
  TEST_ASSERT_EQUAL_INT16_ARRAY(in, out, 4);
  TEST_ASSERT_EQUAL(0, ring.available());
}

void test_a_chunk_that_does_not_fit_is_not_written_at_all() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  const int16_t six[] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_TRUE(ring.write(six, 6));
  const int16_t four[] = {7, 8, 9, 10};
  TEST_ASSERT_FALSE(ring.write(four, 4));
  TEST_ASSERT_EQUAL(6, ring.available());
}

void test_wraps_around_the_end() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  int16_t scratch[8];
  const int16_t first[] = {1, 2, 3, 4, 5, 6};
  ring.write(first, 6);
  ring.read(scratch, 6);
  const int16_t second[] = {10, 11, 12, 13, 14, 15};
  TEST_ASSERT_TRUE(ring.write(second, 6));
  TEST_ASSERT_EQUAL(6, ring.read(scratch, 8));
  TEST_ASSERT_EQUAL_INT16_ARRAY(second, scratch, 6);
}

void test_clear_drops_everything_waiting() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  const int16_t in[] = {1, 2};
  ring.write(in, 2);
  ring.clear();
  TEST_ASSERT_EQUAL(0, ring.available());
}

void test_an_unattached_ring_takes_nothing() {
  PcmRing ring;
  const int16_t in[] = {1, 2};
  TEST_ASSERT_FALSE(ring.write(in, 2));
  int16_t storage[4];
  ring.attach(storage, 4);
  TEST_ASSERT_TRUE(ring.write(in, 2));
}

void test_write_with_a_source_converts_each_sample() {
  int16_t storage[4];
  PcmRing ring(storage, 4);
  TEST_ASSERT_TRUE(ring.write(2, [](uint32_t i) { return static_cast<int16_t>(100 + i); }));
  int16_t out[2];
  ring.read(out, 2);
  TEST_ASSERT_EQUAL(100, out[0]);
  TEST_ASSERT_EQUAL(101, out[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_back_in_order);
  RUN_TEST(test_a_chunk_that_does_not_fit_is_not_written_at_all);
  RUN_TEST(test_wraps_around_the_end);
  RUN_TEST(test_clear_drops_everything_waiting);
  RUN_TEST(test_an_unattached_ring_takes_nothing);
  RUN_TEST(test_write_with_a_source_converts_each_sample);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_pcm_ring`
Expected: FAIL, `PcmRing.h: No such file or directory`.

- [ ] **Step 3: Write `lib/btaudio/PcmRing.h`**

```cpp
#pragma once

#include <atomic>
#include <cstdint>

namespace drehklang::btaudio {

// Interleaved 16-bit samples from one producer task to one consumer task,
// without a lock (ADR 0027). The storage is the caller's: PSRAM on the S3,
// internal RAM on the U4WDH. Capacity is in samples, a power of two.
// Attach the storage before either side runs.
class PcmRing {
 public:
  PcmRing() = default;
  PcmRing(int16_t *storage, uint32_t capacity) { attach(storage, capacity); }

  void attach(int16_t *storage, uint32_t capacity) {
    data_ = storage;
    capacity_ = storage != nullptr ? capacity : 0;
    mask_ = capacity_ - 1;
  }

  uint32_t capacity() const { return capacity_; }

  // Samples waiting. Safe from either side.
  uint32_t available() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }

  // Producer: all `count` samples, taken from source(i), or none of them.
  // A chunk is never split, so left and right never trade places.
  template <typename Source>
  bool write(uint32_t count, Source source) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    if (capacity_ - (head - tail) < count) return false;
    for (uint32_t i = 0; i < count; ++i) data_[(head + i) & mask_] = source(i);
    head_.store(head + count, std::memory_order_release);
    return true;
  }

  bool write(const int16_t *samples, uint32_t count) {
    return write(count, [samples](uint32_t i) { return samples[i]; });
  }

  // Consumer: up to `max` samples. Returns how many.
  uint32_t read(int16_t *dst, uint32_t max) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    uint32_t n = head_.load(std::memory_order_acquire) - tail;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) dst[i] = data_[(tail + i) & mask_];
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  // Consumer: drops everything waiting.
  void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  int16_t *data_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t mask_ = 0;
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
};

}  // namespace drehklang::btaudio
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `pio test -e native -f test_pcm_ring`
Expected: 6 tests, PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/btaudio/PcmRing.h test/test_pcm_ring
git commit -m "Add a lock-free sample ring for the Bluetooth stream

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Resampler and headphone stream (`lib/btaudio/`)

**Files:**
- Create: `lib/btaudio/AdaptiveResampler.h`, `lib/btaudio/HeadphoneStream.h`
- Test: `test/test_adaptive_resampler/test_adaptive_resampler.cpp`

**Interfaces:**
- Consumes: `btaudio::PcmRing` (Task 3), `btlink::getU32` is not needed here.
- Produces: `class AdaptiveResampler { static constexpr uint32_t kOutputRate = 44100; void reset(uint32_t); uint32_t inputRate() const; float step() const; bool playing() const; uint32_t underruns() const; void render(PcmRing&, int16_t*, size_t frames); }`, `class HeadphoneStream { HeadphoneStream(int16_t*, uint32_t capacity); bool push(uint32_t rate, const uint8_t *pcmLe, uint32_t samples); void render(int16_t*, size_t frames); uint32_t inputRate() const; uint32_t underruns() const; uint32_t dropped() const; uint32_t buffered() const; }`

- [ ] **Step 1: Write the failing test**

```cpp
#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "AdaptiveResampler.h"
#include "HeadphoneStream.h"
#include "PcmRing.h"

using drehklang::btaudio::AdaptiveResampler;
using drehklang::btaudio::HeadphoneStream;
using drehklang::btaudio::PcmRing;

namespace {

constexpr uint32_t kCapacity = 16384;  // The U4WDH's ring: 8192 frames.

// Stereo frames with left = right = a running counter, so order is visible.
struct Producer {
  int16_t next = 0;
  bool push(PcmRing &ring, uint32_t frames) {
    return ring.write(frames * 2, [this](uint32_t i) {
      const int16_t v = next;
      if (i % 2 == 1) ++next;
      return v;
    });
  }
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_waits_for_half_a_ring_before_playing() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  producer.push(ring, 1000);
  AdaptiveResampler resampler;
  std::vector<int16_t> out(512 * 2, 7);
  resampler.render(ring, out.data(), 512);
  TEST_ASSERT_FALSE(resampler.playing());
  for (int16_t s : out) TEST_ASSERT_EQUAL(0, s);
}

void test_the_same_rate_is_bit_exact() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  producer.push(ring, kCapacity / 4);  // Exactly half full, in frames.
  AdaptiveResampler resampler;
  std::vector<int16_t> out(256 * 2);
  int16_t expected = 0;
  for (int block = 0; block < 100; ++block) {
    resampler.render(ring, out.data(), 256);
    producer.push(ring, 256);
    for (size_t i = 0; i < 256; ++i) {
      TEST_ASSERT_EQUAL(expected, out[i * 2]);
      TEST_ASSERT_EQUAL(expected, out[i * 2 + 1]);
      ++expected;
    }
  }
}

void test_48k_to_44k_steps_by_the_ratio_and_keeps_the_fill() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  AdaptiveResampler resampler;
  resampler.reset(48000);
  producer.push(ring, kCapacity / 4);
  std::vector<int16_t> out(441 * 2);
  resampler.render(ring, out.data(), 441);
  const uint32_t startFill = ring.available();
  // Ten seconds: 480 frames in and 441 out every 10 ms.
  for (int step = 0; step < 1000; ++step) {
    producer.push(ring, 480);
    resampler.render(ring, out.data(), 441);
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 48000.0f / 44100.0f, resampler.step());
  TEST_ASSERT_INT_WITHIN(64 * 2, startFill, ring.available());
}

void drift(double ppm) {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  AdaptiveResampler resampler;
  std::vector<int16_t> out(441 * 2);
  double owed = 0;
  uint32_t lowest = kCapacity, highest = 0;
  // Ten simulated minutes in 10 ms steps; the S3 sends 256-frame packets.
  for (int step = 0; step < 60000; ++step) {
    owed += 441.0 * (1.0 + ppm * 1e-6);
    while (owed >= 256) {
      TEST_ASSERT_TRUE_MESSAGE(producer.push(ring, 256), "ring overran");
      owed -= 256;
    }
    resampler.render(ring, out.data(), 441);
    if (step > 1000) {
      lowest = std::min(lowest, ring.available());
      highest = std::max(highest, ring.available());
    }
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, resampler.underruns(), "ring ran dry");
  TEST_ASSERT_GREATER_THAN(kCapacity / 4, lowest);
  TEST_ASSERT_LESS_THAN(kCapacity * 3 / 4, highest);
}

void test_a_faster_s3_clock_is_absorbed() { drift(200); }
void test_a_slower_s3_clock_is_absorbed() { drift(-200); }

void test_an_empty_ring_plays_silence_then_refills() {
  std::vector<int16_t> storage(kCapacity);
  PcmRing ring(storage.data(), kCapacity);
  Producer producer;
  AdaptiveResampler resampler;
  producer.push(ring, kCapacity / 4);
  std::vector<int16_t> out(kCapacity);  // Enough for every frame and more.
  resampler.render(ring, out.data(), kCapacity / 4 + 100);
  TEST_ASSERT_EQUAL(1, resampler.underruns());
  TEST_ASSERT_FALSE(resampler.playing());
  TEST_ASSERT_EQUAL(0, out[(kCapacity / 4 + 50) * 2]);
  producer.push(ring, kCapacity / 4);
  resampler.render(ring, out.data(), 10);
  TEST_ASSERT_TRUE(resampler.playing());
}

void test_rate_change_resets_and_keeps_pitch() {
  std::vector<int16_t> storage(kCapacity);
  HeadphoneStream stream(storage.data(), kCapacity);
  std::vector<uint8_t> pcm(256 * 4, 0);
  stream.push(44100, pcm.data(), 512);
  std::vector<int16_t> out(256 * 2);
  stream.render(out.data(), 256);
  TEST_ASSERT_EQUAL_UINT32(44100, stream.inputRate());
  stream.push(32000, pcm.data(), 512);
  stream.render(out.data(), 256);
  TEST_ASSERT_EQUAL_UINT32(32000, stream.inputRate());
  TEST_ASSERT_EQUAL(0, stream.buffered());
}

void test_stream_decodes_little_endian_samples() {
  std::vector<int16_t> storage(kCapacity);
  HeadphoneStream stream(storage.data(), kCapacity);
  const uint8_t pcm[] = {0x34, 0x12, 0xFF, 0xFF};  // 0x1234, -1.
  TEST_ASSERT_TRUE(stream.push(44100, pcm, 2));
  TEST_ASSERT_EQUAL(2, stream.buffered());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_waits_for_half_a_ring_before_playing);
  RUN_TEST(test_the_same_rate_is_bit_exact);
  RUN_TEST(test_48k_to_44k_steps_by_the_ratio_and_keeps_the_fill);
  RUN_TEST(test_a_faster_s3_clock_is_absorbed);
  RUN_TEST(test_a_slower_s3_clock_is_absorbed);
  RUN_TEST(test_an_empty_ring_plays_silence_then_refills);
  RUN_TEST(test_rate_change_resets_and_keeps_pitch);
  RUN_TEST(test_stream_decodes_little_endian_samples);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_adaptive_resampler`
Expected: FAIL, `AdaptiveResampler.h: No such file or directory`.

- [ ] **Step 3: Write `lib/btaudio/AdaptiveResampler.h`**

```cpp
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "PcmRing.h"

namespace drehklang::btaudio {

// Turns the S3's stream -- at its source's rate, clocked by the S3's DAC --
// into 44.1 kHz stereo at the pace Bluetooth asks for it (ADR 0027).
//
// Linear interpolation between neighbouring frames. The step is trimmed by
// how full the ring is: above half, a little faster; below, a little
// slower. That absorbs the drift between the two clocks without dropping
// or repeating frames. A ring that runs dry plays silence until it is half
// full again, so a stall is one clean gap rather than a crackle.
//
// No low-pass before downsampling: content above 22 kHz in a 96 kHz file
// aliases. Music has little energy there, so this stays simple.
class AdaptiveResampler {
 public:
  static constexpr uint32_t kOutputRate = 44100;
  // Fill error (relative to half the ring) that trims nothing: one 256-frame
  // packet is ~6 % of the U4WDH's 8192-frame ring, and each arrival must not
  // wobble the pitch.
  static constexpr float kDeadband = 0.1f;
  static constexpr float kGain = 0.01f;
  // 2000 ppm: ten times the worst crystal pair.
  static constexpr float kMaxTrim = 0.002f;
  static constexpr float kSmoothing = 0.01f;

  void reset(uint32_t inputRate) {
    inputRate_ = inputRate;
    baseStep_ = static_cast<float>(inputRate) / kOutputRate;
    phase_ = 0.0f;
    primed_ = false;
    playing_ = false;
    fillError_ = 0.0f;
    trim_ = 0.0f;
  }

  uint32_t inputRate() const { return inputRate_; }
  float step() const { return baseStep_ * (1.0f + trim_); }
  bool playing() const { return playing_; }
  uint32_t underruns() const { return underruns_; }

  // Fills `frames` interleaved stereo frames into `out`.
  void render(PcmRing &ring, int16_t *out, size_t frames) {
    const uint32_t half = ring.capacity() / 2;
    if (!playing_) {
      if (ring.available() < half) {
        silence(out, frames);
        return;
      }
      playing_ = true;
    }
    updateTrim(ring.available(), half);
    const float stepNow = step();
    for (size_t i = 0; i < frames; ++i) {
      if (!advance(ring)) {
        ++underruns_;
        playing_ = false;
        primed_ = false;
        phase_ = 0.0f;
        silence(out + i * 2, frames - i);
        return;
      }
      out[i * 2] = lerp(cur_[0], next_[0]);
      out[i * 2 + 1] = lerp(cur_[1], next_[1]);
      phase_ += stepNow;
    }
  }

 private:
  // Makes cur_/next_ the frames either side of phase_. False when the ring
  // has run dry.
  bool advance(PcmRing &ring) {
    if (!primed_) {
      if (!readFrame(ring, cur_) || !readFrame(ring, next_)) return false;
      primed_ = true;
      phase_ = 0.0f;
    }
    while (phase_ >= 1.0f) {
      cur_[0] = next_[0];
      cur_[1] = next_[1];
      if (!readFrame(ring, next_)) return false;
      phase_ -= 1.0f;
    }
    return true;
  }

  static bool readFrame(PcmRing &ring, int16_t *frame) { return ring.read(frame, 2) == 2; }

  int16_t lerp(int16_t a, int16_t b) const {
    const float v = a + (b - a) * phase_;
    return static_cast<int16_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
  }

  void updateTrim(uint32_t available, uint32_t half) {
    const float error = (static_cast<float>(available) - half) / half;
    fillError_ += (error - fillError_) * kSmoothing;
    const float excess = std::fabs(fillError_) - kDeadband;
    float trim = excess > 0.0f ? std::copysign(excess * kGain, fillError_) : 0.0f;
    if (trim > kMaxTrim) trim = kMaxTrim;
    if (trim < -kMaxTrim) trim = -kMaxTrim;
    trim_ = trim;
  }

  static void silence(int16_t *out, size_t frames) {
    for (size_t i = 0; i < frames * 2; ++i) out[i] = 0;
  }

  uint32_t inputRate_ = kOutputRate;
  float baseStep_ = 1.0f;
  float phase_ = 0.0f;
  float fillError_ = 0.0f;
  float trim_ = 0.0f;
  int16_t cur_[2] = {};
  int16_t next_[2] = {};
  bool primed_ = false;
  bool playing_ = false;
  uint32_t underruns_ = 0;
};

}  // namespace drehklang::btaudio
```

Note on `test_the_same_rate_is_bit_exact`: the ring starts exactly half full and is topped up by what is consumed, so the fill error stays inside the deadband. The step is then exactly 1.0, `phase_` is 0.0 at every output, and each output is `cur_` unchanged.

- [ ] **Step 4: Write `lib/btaudio/HeadphoneStream.h`**

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AdaptiveResampler.h"
#include "PcmRing.h"

namespace drehklang::btaudio {

// The U4WDH's side of the stream (ADR 0027): the link task pushes each
// AUDIO packet, and Bluetooth's data callback renders 44.1 kHz from it. A
// new source rate -- the next track, or the tone output taking the DAC --
// drops what is buffered and restarts the resampler at that rate.
class HeadphoneStream {
 public:
  HeadphoneStream(int16_t *storage, uint32_t capacity) : ring_(storage, capacity) {}

  // Link task: `samples` interleaved int16 little-endian samples at `rate`.
  // False when the ring had no room; the packet is then dropped.
  bool push(uint32_t rate, const uint8_t *pcm, uint32_t samples) {
    if (rate == 0) return false;
    rate_.store(rate, std::memory_order_relaxed);
    const bool written = ring_.write(samples, [pcm](uint32_t i) {
      return static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
    });
    if (!written) dropped_.fetch_add(1, std::memory_order_relaxed);
    return written;
  }

  // Bluetooth's data callback: `frames` stereo frames at 44.1 kHz.
  void render(int16_t *out, size_t frames) {
    const uint32_t rate = rate_.load(std::memory_order_relaxed);
    if (rate != resampler_.inputRate()) {
      ring_.clear();
      resampler_.reset(rate);
    }
    resampler_.render(ring_, out, frames);
  }

  uint32_t inputRate() const { return resampler_.inputRate(); }
  uint32_t underruns() const { return resampler_.underruns(); }
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  uint32_t buffered() const { return ring_.available(); }

 private:
  PcmRing ring_;
  AdaptiveResampler resampler_;
  std::atomic<uint32_t> rate_{AdaptiveResampler::kOutputRate};
  std::atomic<uint32_t> dropped_{0};
};

}  // namespace drehklang::btaudio
```

- [ ] **Step 5: Run the tests to see them pass**

Run: `pio test -e native -f test_adaptive_resampler`
Expected: 8 tests, PASS. If a drift test fails on the fill bounds, raise `kGain` to 0.02 and re-run. Do not widen the test's bounds.

- [ ] **Step 6: Commit**

```bash
git add lib/btaudio test/test_adaptive_resampler
git commit -m "Add the U4WDH's adaptive resampler and headphone stream

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The tap on the S3 (`lib/bluetooth/AudioTap.h`)

**Files:**
- Create: `lib/bluetooth/AudioTap.h`
- Test: `test/test_bt_audio_tap/test_bt_audio_tap.cpp`

**Interfaces:**
- Consumes: `btaudio::PcmRing` (Task 3).
- Produces: `class bluetooth::AudioTap { explicit AudioTap(btaudio::PcmRing&); void setForwarding(bool); bool forwarding() const; void pushStereo32(const int32_t*, size_t words, uint32_t rate); void pushStereo16(const int16_t*, size_t words, uint32_t rate); uint32_t rate() const; uint32_t dropped() const; }`

- [ ] **Step 1: Write the failing test**

```cpp
#include <unity.h>

#include <climits>
#include <cstdint>

#include "AudioTap.h"
#include "PcmRing.h"

using drehklang::bluetooth::AudioTap;
using drehklang::btaudio::PcmRing;

void setUp() {}
void tearDown() {}

void test_nothing_is_copied_without_headphones() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  AudioTap tap(ring);
  const int32_t samples[] = {1 << 16, 2 << 16};
  tap.pushStereo32(samples, 2, 44100);
  TEST_ASSERT_EQUAL(0, ring.available());
}

void test_keeps_the_top_16_bits() {
  int16_t storage[8];
  PcmRing ring(storage, 8);
  AudioTap tap(ring);
  tap.setForwarding(true);
  const int32_t samples[] = {0x12345678, -1, INT32_MIN, INT32_MAX};
  tap.pushStereo32(samples, 4, 48000);
  int16_t out[4];
  TEST_ASSERT_EQUAL(4, ring.read(out, 4));
  TEST_ASSERT_EQUAL(0x1234, out[0]);
  TEST_ASSERT_EQUAL(-1, out[1]);
  TEST_ASSERT_EQUAL(-32768, out[2]);
  TEST_ASSERT_EQUAL(32767, out[3]);
  TEST_ASSERT_EQUAL_UINT32(48000, tap.rate());
}

void test_a_full_ring_drops_the_whole_chunk() {
  int16_t storage[4];
  PcmRing ring(storage, 4);
  AudioTap tap(ring);
  tap.setForwarding(true);
  const int16_t samples[] = {1, 2, 3, 4, 5, 6};
  tap.pushStereo16(samples, 4, 22050);
  tap.pushStereo16(samples, 2, 22050);
  TEST_ASSERT_EQUAL(4, ring.available());
  TEST_ASSERT_EQUAL_UINT32(1, tap.dropped());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_copied_without_headphones);
  RUN_TEST(test_keeps_the_top_16_bits);
  RUN_TEST(test_a_full_ring_drops_the_whole_chunk);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_bt_audio_tap`
Expected: FAIL, `AudioTap.h: No such file or directory`.

- [ ] **Step 3: Write `lib/bluetooth/AudioTap.h`**

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "PcmRing.h"

namespace drehklang::bluetooth {

// Where the DAC's current owner -- the player or the tone output -- hands
// Bluetooth a copy of what it plays (ADR 0027). The jack keeps playing;
// this only copies. Both callers are audio tasks, so it never blocks:
// without headphones it returns at once, and a full ring drops the chunk.
class AudioTap {
 public:
  explicit AudioTap(btaudio::PcmRing &ring) : ring_(ring) {}

  // Main loop: whether headphones are connected.
  void setForwarding(bool on) { forwarding_.store(on, std::memory_order_relaxed); }
  bool forwarding() const { return forwarding_.load(std::memory_order_relaxed); }

  // ESP32-audioI2S's samples after the volume: interleaved, full-scale
  // 32-bit. `words` counts samples, not frames.
  void pushStereo32(const int32_t *samples, size_t words, uint32_t rate) {
    if (!forwarding()) return;
    rate_.store(rate, std::memory_order_relaxed);
    note(ring_.write(static_cast<uint32_t>(words), [samples](uint32_t i) {
      return static_cast<int16_t>(samples[i] >> 16);
    }));
  }

  // The tone output's chunks, already 16-bit.
  void pushStereo16(const int16_t *samples, size_t words, uint32_t rate) {
    if (!forwarding()) return;
    rate_.store(rate, std::memory_order_relaxed);
    note(ring_.write(samples, static_cast<uint32_t>(words)));
  }

  // The rate of what was pushed last; the link puts it in every packet.
  uint32_t rate() const { return rate_.load(std::memory_order_relaxed); }
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  void note(bool written) {
    if (!written) dropped_.fetch_add(1, std::memory_order_relaxed);
  }

  btaudio::PcmRing &ring_;
  std::atomic<bool> forwarding_{false};
  std::atomic<uint32_t> rate_{44100};
  std::atomic<uint32_t> dropped_{0};
};

}  // namespace drehklang::bluetooth
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `pio test -e native -f test_bt_audio_tap`
Expected: 3 tests, PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/bluetooth/AudioTap.h test/test_bt_audio_tap
git commit -m "Add the S3's audio tap for Bluetooth

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Controller and headphone button (`lib/bluetooth/`)

**Files:**
- Create: `lib/bluetooth/BtController.h`, `lib/bluetooth/HeadphoneButton.h`
- Test: `test/test_bt_controller/test_bt_controller.cpp`, `test/test_headphone_button/test_headphone_button.cpp`

**Interfaces:**
- Consumes: `btlink::*` (Task 2), `playback::KeyValueStore` (`lib/playback/KeyValueStore.h`: `getU8(key, out)`, `setU8(key, value)`).
- Produces:
  - `class bluetooth::PacketSender { virtual void send(btlink::PacketType, const uint8_t*, uint16_t) = 0; }`
  - `enum class BtState { Starting, FirmwareMissing, FirmwareOutdated, NotAnswering, Off, Idle, Connecting, Connected }`
  - `enum class BtEvent { Connected, Disconnected, PairFailed, Play, Pause }`
  - `struct ScanEntry { btlink::Address address; int8_t rssi; std::string name; }`
  - `class BtController` with `begin(uint32_t)`, `tick(uint32_t)`, `onPacket(const btlink::Packet&, uint32_t)`, `state()`, `enabled()`, `setEnabled(bool)`, `scanning()`, `startScan()`, `stopScan()`, `scanResults()`, `pair(const ScanEntry&, uint32_t)`, `reconnect(uint32_t)`, `forget()`, `paired()`, `pairedName()`, `pairingName()`, `forwardAudio()`, `takeEvent() -> std::optional<BtEvent>`, `revision()`, `stats()`
  - `enum class ButtonAction { None, TogglePlayer, StartTone, StopTone }`, `struct ButtonContext { bool inGame, onTones, toneRunning, hasTrack, playing; }`, `ButtonAction actionFor(BtEvent, const ButtonContext&)`

- [ ] **Step 1: Write the failing controller test** `test/test_bt_controller/test_bt_controller.cpp`:

```cpp
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "BtController.h"
#include "KeyValueStore.h"
#include "Messages.h"
#include "Packet.h"

using namespace drehklang;
using bluetooth::BtController;
using bluetooth::BtEvent;
using bluetooth::BtState;
using btlink::Packet;
using btlink::PacketType;

namespace {

class FakeStore : public playback::KeyValueStore {
 public:
  bool getU8(const std::string &key, uint8_t &out) override {
    auto it = values.find(key);
    if (it == values.end()) return false;
    out = it->second;
    return true;
  }
  void setU8(const std::string &key, uint8_t value) override { values[key] = value; }
  std::map<std::string, uint8_t> values;
};

struct Sent {
  PacketType type;
  std::vector<uint8_t> payload;
};

class FakeLink : public bluetooth::PacketSender {
 public:
  void send(PacketType type, const uint8_t *payload, uint16_t length) override {
    sent.push_back({type, std::vector<uint8_t>(payload, payload + length)});
  }
  int count(PacketType type) const {
    int n = 0;
    for (const auto &s : sent) n += s.type == type ? 1 : 0;
    return n;
  }
  std::vector<Sent> sent;
};

Packet hello(uint8_t version) {
  Packet p;
  p.type = PacketType::Hello;
  p.length = 1;
  p.payload[0] = version;
  return p;
}

Packet state(btlink::LinkState link, bool scanning = false, bool paired = false,
             const std::string &name = "") {
  btlink::StateMessage m;
  m.link = link;
  m.scanning = scanning;
  m.paired = paired;
  m.address = {1, 2, 3, 4, 5, 6};
  m.name = name;
  Packet p;
  p.type = PacketType::State;
  p.length = static_cast<uint16_t>(btlink::encodeState(m, p.payload.data()));
  return p;
}

Packet scanResult(uint8_t last, int8_t rssi, const std::string &name) {
  btlink::ScanResultMessage m;
  m.address = {9, 9, 9, 9, 9, last};
  m.rssi = rssi;
  m.name = name;
  Packet p;
  p.type = PacketType::ScanResult;
  p.length = static_cast<uint16_t>(btlink::encodeScanResult(m, p.payload.data()));
  return p;
}

Packet button(btlink::ButtonCode code) {
  Packet p;
  p.type = PacketType::Button;
  p.length = 1;
  p.payload[0] = static_cast<uint8_t>(code);
  return p;
}

struct Fixture {
  FakeStore store;
  FakeLink link;
  BtController controller{link, store};
  // Handshake done, enabled, idle.
  void answer(uint32_t nowMs = 100) {
    store.values[BtController::kEnabledKey] = 1;
    controller.begin(0);
    controller.onPacket(hello(btlink::kProtocolVersion), nowMs);
    controller.onPacket(state(btlink::LinkState::Idle), nowMs);
  }
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_starts_by_saying_hello() {
  Fixture f;
  f.controller.begin(0);
  TEST_ASSERT_EQUAL(1, f.link.count(PacketType::Hello));
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::Starting), static_cast<int>(f.controller.state()));
  f.controller.tick(500);
  TEST_ASSERT_EQUAL(2, f.link.count(PacketType::Hello));
}

void test_no_answer_in_3_s_means_firmware_missing() {
  Fixture f;
  f.controller.begin(0);
  f.controller.tick(2999);
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::Starting), static_cast<int>(f.controller.state()));
  f.controller.tick(3000);
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::FirmwareMissing),
                    static_cast<int>(f.controller.state()));
}

void test_another_version_means_firmware_outdated() {
  Fixture f;
  f.controller.begin(0);
  f.controller.onPacket(hello(btlink::kProtocolVersion + 1), 10);
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::FirmwareOutdated),
                    static_cast<int>(f.controller.state()));
}

void test_handshake_sends_the_stored_enable() {
  Fixture f;
  f.answer();
  TEST_ASSERT_EQUAL(1, f.link.count(PacketType::Enable));
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::Idle), static_cast<int>(f.controller.state()));
}

void test_disabled_by_default() {
  Fixture f;
  f.controller.begin(0);
  TEST_ASSERT_FALSE(f.controller.enabled());
  f.controller.onPacket(hello(btlink::kProtocolVersion), 10);
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::Off), static_cast<int>(f.controller.state()));
}

void test_silence_after_answering_means_not_answering() {
  Fixture f;
  f.answer(100);
  f.controller.onPacket(state(btlink::LinkState::Connected), 200);
  f.controller.tick(2199);
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::Connected), static_cast<int>(f.controller.state()));
  f.controller.tick(2200);
  TEST_ASSERT_EQUAL(static_cast<int>(BtState::NotAnswering),
                    static_cast<int>(f.controller.state()));
  TEST_ASSERT_FALSE(f.controller.forwardAudio());
  // Connected, then Disconnected.
  f.controller.takeEvent();
  auto event = f.controller.takeEvent();
  TEST_ASSERT_TRUE(event.has_value());
  TEST_ASSERT_EQUAL(static_cast<int>(BtEvent::Disconnected), static_cast<int>(*event));
  const int hellos = f.link.count(PacketType::Hello);
  f.controller.tick(2700);
  TEST_ASSERT_EQUAL(hellos + 1, f.link.count(PacketType::Hello));
}

void test_connecting_forwards_audio_and_reports_it() {
  Fixture f;
  f.answer();
  TEST_ASSERT_FALSE(f.controller.forwardAudio());
  f.controller.onPacket(state(btlink::LinkState::Connected, false, true, "Buds"), 300);
  TEST_ASSERT_TRUE(f.controller.forwardAudio());
  TEST_ASSERT_EQUAL_STRING("Buds", f.controller.pairedName().c_str());
  auto event = f.controller.takeEvent();
  TEST_ASSERT_TRUE(event.has_value());
  TEST_ASSERT_EQUAL(static_cast<int>(BtEvent::Connected), static_cast<int>(*event));
  TEST_ASSERT_FALSE(f.controller.takeEvent().has_value());
}

void test_state_disagreeing_with_enabled_resends_enable() {
  Fixture f;
  f.answer();
  const int enables = f.link.count(PacketType::Enable);
  // The U4WDH restarted and came back up off.
  f.controller.onPacket(state(btlink::LinkState::Off), 500);
  TEST_ASSERT_EQUAL(enables + 1, f.link.count(PacketType::Enable));
}

void test_pairing_that_does_not_connect_in_15_s_fails() {
  Fixture f;
  f.answer();
  bluetooth::ScanEntry entry;
  entry.address = {7, 7, 7, 7, 7, 7};
  entry.name = "Buds";
  f.controller.pair(entry, 1000);
  TEST_ASSERT_EQUAL(1, f.link.count(PacketType::Pair));
  f.controller.onPacket(state(btlink::LinkState::Connecting), 15000);
  f.controller.tick(15999);
  TEST_ASSERT_FALSE(f.controller.takeEvent().has_value());
  f.controller.onPacket(state(btlink::LinkState::Connecting), 16000);
  f.controller.tick(16000);
  auto event = f.controller.takeEvent();
  TEST_ASSERT_TRUE(event.has_value());
  TEST_ASSERT_EQUAL(static_cast<int>(BtEvent::PairFailed), static_cast<int>(*event));
  TEST_ASSERT_EQUAL_STRING("Buds", f.controller.pairingName().c_str());
}

void test_scan_results_are_sorted_by_signal() {
  Fixture f;
  f.answer();
  f.controller.startScan();
  TEST_ASSERT_EQUAL(1, f.link.count(PacketType::ScanStart));
  TEST_ASSERT_TRUE(f.controller.scanning());
  f.controller.onPacket(scanResult(1, -80, "Far"), 400);
  f.controller.onPacket(scanResult(2, -40, "Near"), 400);
  const auto &results = f.controller.scanResults();
  TEST_ASSERT_EQUAL(2, results.size());
  TEST_ASSERT_EQUAL_STRING("Near", results[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Far", results[1].name.c_str());
}

void test_scan_result_for_known_address_updates_in_place() {
  Fixture f;
  f.answer();
  f.controller.startScan();
  f.controller.onPacket(scanResult(1, -80, "58:2A:BD:5E:55:3C"), 400);
  f.controller.onPacket(scanResult(1, -50, "Buds"), 400);
  const auto &results = f.controller.scanResults();
  TEST_ASSERT_EQUAL(1, results.size());
  TEST_ASSERT_EQUAL_STRING("Buds", results[0].name.c_str());
  TEST_ASSERT_EQUAL(-50, results[0].rssi);
}

void test_buttons_become_events() {
  Fixture f;
  f.answer();
  f.controller.onPacket(button(btlink::ButtonCode::Pause), 400);
  auto event = f.controller.takeEvent();
  TEST_ASSERT_TRUE(event.has_value());
  TEST_ASSERT_EQUAL(static_cast<int>(BtEvent::Pause), static_cast<int>(*event));
}

void test_enabling_is_stored_and_sent() {
  Fixture f;
  f.controller.begin(0);
  f.controller.onPacket(hello(btlink::kProtocolVersion), 10);
  const uint32_t revision = f.controller.revision();
  f.controller.setEnabled(true);
  TEST_ASSERT_EQUAL(1, f.store.values[BtController::kEnabledKey]);
  TEST_ASSERT_EQUAL(static_cast<int>(PacketType::Enable), static_cast<int>(f.link.sent.back().type));
  TEST_ASSERT_EQUAL(1, f.link.sent.back().payload[0]);
  TEST_ASSERT_NOT_EQUAL(revision, f.controller.revision());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_by_saying_hello);
  RUN_TEST(test_no_answer_in_3_s_means_firmware_missing);
  RUN_TEST(test_another_version_means_firmware_outdated);
  RUN_TEST(test_handshake_sends_the_stored_enable);
  RUN_TEST(test_disabled_by_default);
  RUN_TEST(test_silence_after_answering_means_not_answering);
  RUN_TEST(test_connecting_forwards_audio_and_reports_it);
  RUN_TEST(test_state_disagreeing_with_enabled_resends_enable);
  RUN_TEST(test_pairing_that_does_not_connect_in_15_s_fails);
  RUN_TEST(test_scan_results_are_sorted_by_signal);
  RUN_TEST(test_scan_result_for_known_address_updates_in_place);
  RUN_TEST(test_buttons_become_events);
  RUN_TEST(test_enabling_is_stored_and_sent);
  return UNITY_END();
}
```

- [ ] **Step 2: Write the failing routing test** `test/test_headphone_button/test_headphone_button.cpp`:

```cpp
#include <unity.h>

#include "HeadphoneButton.h"

using namespace drehklang::bluetooth;

void setUp() {}
void tearDown() {}

void test_pause_while_playing_toggles_the_player() {
  ButtonContext c;
  c.hasTrack = true;
  c.playing = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::TogglePlayer),
                    static_cast<int>(actionFor(BtEvent::Pause, c)));
}

void test_play_while_playing_does_nothing() {
  ButtonContext c;
  c.hasTrack = true;
  c.playing = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_play_while_paused_toggles_the_player() {
  ButtonContext c;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::TogglePlayer),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_nothing_loaded_does_nothing() {
  ButtonContext c;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_tones_start_and_stop() {
  ButtonContext c;
  c.onTones = true;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::StartTone),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
  c.toneRunning = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::StopTone),
                    static_cast<int>(actionFor(BtEvent::Pause, c)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_games_ignore_the_button() {
  ButtonContext c;
  c.inGame = true;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_connection_events_are_not_buttons() {
  ButtonContext c;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Connected, c)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pause_while_playing_toggles_the_player);
  RUN_TEST(test_play_while_playing_does_nothing);
  RUN_TEST(test_play_while_paused_toggles_the_player);
  RUN_TEST(test_nothing_loaded_does_nothing);
  RUN_TEST(test_tones_start_and_stop);
  RUN_TEST(test_games_ignore_the_button);
  RUN_TEST(test_connection_events_are_not_buttons);
  return UNITY_END();
}
```

- [ ] **Step 3: Run both to see them fail**

Run: `pio test -e native -f test_bt_controller -f test_headphone_button`
Expected: FAIL, `BtController.h: No such file or directory`.

- [ ] **Step 4: Write `lib/bluetooth/BtController.h`**

```cpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "KeyValueStore.h"
#include "Messages.h"
#include "Packet.h"

namespace drehklang::bluetooth {

// The link as the controller sees it. BtLink (lib/drivers-bluetooth/)
// queues these for its task; tests record them.
class PacketSender {
 public:
  virtual ~PacketSender() = default;
  virtual void send(btlink::PacketType type, const uint8_t *payload, uint16_t length) = 0;
};

enum class BtState : uint8_t {
  Starting,          // No answer yet, within the first 3 s.
  FirmwareMissing,   // No answer at all: the factory image, or nothing.
  FirmwareOutdated,  // Answers with another protocol version.
  NotAnswering,      // Answered once, silent for 2 s since.
  Off,
  Idle,
  Connecting,
  Connected,
};

enum class BtEvent : uint8_t { Connected, Disconnected, PairFailed, Play, Pause };

struct ScanEntry {
  btlink::Address address{};
  int8_t rssi = 0;
  std::string name;
};

// The S3's view of the ESP32-U4WDH's Bluetooth side (ADR 0027), on the
// main loop. Says hello until the other chip answers, mirrors its STATE,
// keeps the scan list, and turns what happened into events for the UI.
// Scanning is a flag beside the state: a scan can run while connected.
class BtController {
 public:
  static constexpr uint32_t kHelloIntervalMs = 500;
  static constexpr uint32_t kAnswerTimeoutMs = 3000;
  static constexpr uint32_t kHeartbeatTimeoutMs = 2000;
  static constexpr uint32_t kPairTimeoutMs = 15000;
  // NVS keys are limited to 15 characters.
  static constexpr char kEnabledKey[] = "btOn";

  BtController(PacketSender &link, playback::KeyValueStore &store)
      : link_(link), store_(store) {}

  // Off until the user turns it on: a radio nobody asked for stays quiet.
  void begin(uint32_t nowMs) {
    uint8_t on = 0;
    if (store_.getU8(kEnabledKey, on)) enabled_ = on != 0;
    startedMs_ = nowMs;
    lastHelloMs_ = nowMs;
    sendHello();
  }

  void tick(uint32_t nowMs) {
    if (!answered_) {
      if (state_ == BtState::Starting && nowMs - startedMs_ >= kAnswerTimeoutMs) {
        setState(BtState::FirmwareMissing);
      }
      if (nowMs - lastHelloMs_ >= kHelloIntervalMs) {
        lastHelloMs_ = nowMs;
        sendHello();
      }
      return;
    }
    if (nowMs - lastPacketMs_ >= kHeartbeatTimeoutMs) {
      answered_ = false;
      pairing_ = false;
      setScanning(false);
      setState(BtState::NotAnswering);
      lastHelloMs_ = nowMs;
      return;
    }
    if (pairing_ && nowMs - pairingSinceMs_ >= kPairTimeoutMs) {
      pairing_ = false;
      events_.push_back(BtEvent::PairFailed);
    }
  }

  void onPacket(const btlink::Packet &packet, uint32_t nowMs) {
    lastPacketMs_ = nowMs;
    if (packet.type == btlink::PacketType::Hello) {
      onHello(packet);
      return;
    }
    if (!answered_) return;
    switch (packet.type) {
      case btlink::PacketType::State:
        onState(packet);
        break;
      case btlink::PacketType::ScanResult:
        onScanResult(packet);
        break;
      case btlink::PacketType::Button:
        onButton(packet);
        break;
      case btlink::PacketType::Stats:
        btlink::decodeStats(packet, stats_);
        break;
      default:
        break;
    }
  }

  BtState state() const { return state_; }
  bool enabled() const { return enabled_; }

  void setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    store_.setU8(kEnabledKey, on ? 1 : 0);
    ++revision_;
    if (answered_) sendEnable();
  }

  bool scanning() const { return scanning_; }
  const std::vector<ScanEntry> &scanResults() const { return results_; }

  void startScan() {
    results_.clear();
    setScanning(true);
    link_.send(btlink::PacketType::ScanStart, nullptr, 0);
  }

  void stopScan() { link_.send(btlink::PacketType::ScanStop, nullptr, 0); }

  void pair(const ScanEntry &entry, uint32_t nowMs) {
    btlink::PairMessage message;
    message.address = entry.address;
    message.name = entry.name;
    uint8_t payload[btlink::kMaxPayload];
    link_.send(btlink::PacketType::Pair, payload,
               static_cast<uint16_t>(btlink::encodePair(message, payload)));
    pairing_ = true;
    pairingSinceMs_ = nowMs;
    pairingName_ = entry.name;
  }

  // The paired headphones' row: try now rather than at the next 10 s retry.
  void reconnect(uint32_t nowMs) {
    if (!paired_) return;
    ScanEntry entry;
    entry.address = pairedAddress_;
    entry.name = pairedName_;
    pair(entry, nowMs);
  }

  void forget() { link_.send(btlink::PacketType::Forget, nullptr, 0); }

  bool paired() const { return paired_; }
  const std::string &pairedName() const { return pairedName_; }
  // Whom the last pair() was for, for "Could not connect to ...".
  const std::string &pairingName() const { return pairingName_; }

  bool forwardAudio() const { return state_ == BtState::Connected; }

  std::optional<BtEvent> takeEvent() {
    if (events_.empty()) return std::nullopt;
    const BtEvent event = events_.front();
    events_.pop_front();
    return event;
  }

  // Changes whenever something the UI shows changes.
  uint32_t revision() const { return revision_; }
  const btlink::StatsMessage &stats() const { return stats_; }

 private:
  void sendHello() {
    const uint8_t payload[] = {btlink::kProtocolVersion};
    link_.send(btlink::PacketType::Hello, payload, sizeof(payload));
  }

  void sendEnable() {
    const uint8_t payload[] = {static_cast<uint8_t>(enabled_ ? 1 : 0)};
    link_.send(btlink::PacketType::Enable, payload, sizeof(payload));
  }

  void onHello(const btlink::Packet &packet) {
    if (packet.length < 1 || packet.payload[0] != btlink::kProtocolVersion) {
      answered_ = false;
      setState(BtState::FirmwareOutdated);
      return;
    }
    const bool wasAnswered = answered_;
    answered_ = true;
    if (!wasAnswered) setState(enabled_ ? BtState::Idle : BtState::Off);
    sendEnable();
  }

  void onState(const btlink::Packet &packet) {
    btlink::StateMessage m;
    if (!btlink::decodeState(packet, m)) return;
    // The U4WDH starts up off: after a restart of its own it has forgotten.
    if ((m.link == btlink::LinkState::Off) == enabled_) sendEnable();
    if (paired_ != m.paired || pairedName_ != m.name || pairedAddress_ != m.address) {
      paired_ = m.paired;
      pairedName_ = m.name;
      pairedAddress_ = m.address;
      ++revision_;
    }
    setScanning(m.scanning);
    setState(stateFor(m.link));
  }

  void onScanResult(const btlink::Packet &packet) {
    btlink::ScanResultMessage m;
    if (!btlink::decodeScanResult(packet, m)) return;
    auto it = std::find_if(results_.begin(), results_.end(),
                           [&](const ScanEntry &e) { return e.address == m.address; });
    if (it == results_.end()) {
      results_.push_back({m.address, m.rssi, m.name});
    } else {
      it->rssi = m.rssi;
      it->name = m.name;
    }
    std::stable_sort(results_.begin(), results_.end(),
                     [](const ScanEntry &a, const ScanEntry &b) { return a.rssi > b.rssi; });
    ++revision_;
  }

  void onButton(const btlink::Packet &packet) {
    if (packet.length < 1) return;
    if (packet.payload[0] == static_cast<uint8_t>(btlink::ButtonCode::Play)) {
      events_.push_back(BtEvent::Play);
    } else if (packet.payload[0] == static_cast<uint8_t>(btlink::ButtonCode::Pause)) {
      events_.push_back(BtEvent::Pause);
    }
  }

  static BtState stateFor(btlink::LinkState link) {
    switch (link) {
      case btlink::LinkState::Idle:
        return BtState::Idle;
      case btlink::LinkState::Connecting:
        return BtState::Connecting;
      case btlink::LinkState::Connected:
        return BtState::Connected;
      case btlink::LinkState::Off:
      default:
        return BtState::Off;
    }
  }

  void setState(BtState next) {
    if (next == state_) return;
    if (state_ == BtState::Connected) events_.push_back(BtEvent::Disconnected);
    if (next == BtState::Connected) {
      events_.push_back(BtEvent::Connected);
      pairing_ = false;
    }
    state_ = next;
    ++revision_;
  }

  void setScanning(bool scanning) {
    if (scanning == scanning_) return;
    scanning_ = scanning;
    ++revision_;
  }

  PacketSender &link_;
  playback::KeyValueStore &store_;
  BtState state_ = BtState::Starting;
  bool enabled_ = false;
  bool answered_ = false;
  bool scanning_ = false;
  bool paired_ = false;
  bool pairing_ = false;
  btlink::Address pairedAddress_{};
  std::string pairedName_;
  std::string pairingName_;
  std::vector<ScanEntry> results_;
  std::deque<BtEvent> events_;
  btlink::StatsMessage stats_;
  uint32_t startedMs_ = 0;
  uint32_t lastHelloMs_ = 0;
  uint32_t lastPacketMs_ = 0;
  uint32_t pairingSinceMs_ = 0;
  uint32_t revision_ = 0;
};

}  // namespace drehklang::bluetooth
```

- [ ] **Step 5: Write `lib/bluetooth/HeadphoneButton.h`**

```cpp
#pragma once

#include "BtController.h"

namespace drehklang::bluetooth {

enum class ButtonAction : uint8_t { None, TogglePlayer, StartTone, StopTone };

// What the screen is doing when a headphone button arrives.
struct ButtonContext {
  bool inGame = false;
  bool onTones = false;
  bool toneRunning = false;
  bool hasTrack = false;  // Playing, paused, parked or cued.
  bool playing = false;
};

// Play/Pause from the headphones (ADR 0027): the player's own button in the
// music, start/stop on Tones, nothing in a game. Play and Pause are not a
// toggle -- headphones repeat the one they think is due, and a toggle would
// undo it.
inline ButtonAction actionFor(BtEvent event, const ButtonContext &c) {
  if (event != BtEvent::Play && event != BtEvent::Pause) return ButtonAction::None;
  const bool play = event == BtEvent::Play;
  if (c.inGame) return ButtonAction::None;
  if (c.onTones) {
    if (play && !c.toneRunning) return ButtonAction::StartTone;
    if (!play && c.toneRunning) return ButtonAction::StopTone;
    return ButtonAction::None;
  }
  if (!c.hasTrack || play == c.playing) return ButtonAction::None;
  return ButtonAction::TogglePlayer;
}

}  // namespace drehklang::bluetooth
```

- [ ] **Step 6: Run the tests to see them pass**

Run: `pio test -e native -f test_bt_controller -f test_headphone_button`
Expected: 13 and 7 tests, PASS.

- [ ] **Step 7: Run the whole native suite**

Run: `pio test -e native`
Expected: every suite PASS.

- [ ] **Step 8: Commit**

```bash
git add lib/bluetooth test/test_bt_controller test/test_headphone_button
git commit -m "Add the Bluetooth controller and headphone button routing

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The S3 side of the link (driver, hooks, wiring)

**Files:**
- Create: `lib/drivers-bluetooth/BtLink.h`, `lib/drivers-bluetooth/BtLink.cpp`
- Modify: `lib/drivers-audio/AudioHooks.h`, `lib/drivers-audio/AudioHooks.cpp`, `lib/drivers-audio/Esp32AudioI2SDriver.cpp` (`taskLoop()`, ~line 283), `lib/drivers-audio/ToneOutput.cpp` (`writeChunk()`, end), `src/main.cpp`

**Interfaces:**
- Consumes: `AudioTap` (Task 5), `PcmRing` (Task 3), `BtController`, `PacketSender` (Task 6), `btlink::*` (Task 2).
- Produces: `drivers::BtLink : bluetooth::PacketSender { BtLink(btaudio::PcmRing&, bluetooth::AudioTap&); void begin(); bool receive(btlink::Packet&); }`, `drivers::setBluetoothTap(bluetooth::AudioTap*)`, `drivers::bluetoothTap()`, `drivers::setDecoderRate(uint32_t)`, `drivers::decoderRate()`. In `src/main.cpp`: globals `g_btRing`, `g_btTap`, `g_btLink`, `g_btController`.

- [ ] **Step 1: Write `lib/drivers-bluetooth/BtLink.h`**

```cpp
#pragma once

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstddef>
#include <cstdint>

#include "AudioTap.h"
#include "BtController.h"
#include "Messages.h"
#include "Packet.h"
#include "PacketReader.h"
#include "PcmRing.h"

namespace drehklang::drivers {

// The S3's end of the link to the ESP32-U4WDH (ADR 0027): UART1 at 3 Mbaud,
// TX GPIO48, RX GPIO38 (measured; the schematic names them from the other
// chip's side). Its own task on core 0 sends the tap's samples and queued
// control packets, and queues what arrives for the main loop. It never
// prints (AGENTS.md).
class BtLink : public bluetooth::PacketSender {
 public:
  BtLink(btaudio::PcmRing &ring, bluetooth::AudioTap &tap) : ring_(ring), tap_(tap) {}

  // Installs the UART and starts the task. Call once, after PSRAM is up.
  void begin();

  // Main loop. A full queue drops the packet: HELLO repeats itself, and
  // anything else the user can do again.
  void send(btlink::PacketType type, const uint8_t *payload, uint16_t length) override;

  // Main loop: the next control packet received, if any.
  bool receive(btlink::Packet &out);

 private:
  static constexpr size_t kMaxControlPayload = 64;
  struct Control {
    btlink::PacketType type;
    uint8_t length;
    uint8_t payload[kMaxControlPayload];
  };

  bool pumpReceived();
  bool pumpControl();
  bool pumpAudio();
  void write(btlink::PacketType type, const uint8_t *payload, uint16_t length);
  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);

  btaudio::PcmRing &ring_;
  bluetooth::AudioTap &tap_;
  QueueHandle_t toLink_ = nullptr;
  QueueHandle_t fromLink_ = nullptr;
  btlink::PacketReader reader_;
  uint8_t txSeq_ = 0;
  // In PSRAM (begin()): ~3 KB the internal heap does not have to spare.
  int16_t *pcm_ = nullptr;
  uint8_t *payload_ = nullptr;
  uint8_t *frame_ = nullptr;
};

}  // namespace drehklang::drivers
```

- [ ] **Step 2: Write `lib/drivers-bluetooth/BtLink.cpp`**

```cpp
#include "BtLink.h"

#include <esp_heap_caps.h>
#include <freertos/task.h>

#include <cstring>

namespace drehklang::drivers {

namespace {
constexpr uart_port_t kPort = UART_NUM_1;
constexpr int kTxPin = 48;
constexpr int kRxPin = 38;
constexpr int kRxBufferBytes = 1024;
constexpr int kTxBufferBytes = 2048;
// At the driver's default (120 of 128 bytes) the ESP32 side lost bytes at
// 3 Mbaud; 64 leaves the interrupt ~210 us (spike, 2026-10-05).
constexpr uint8_t kRxFullThreshold = 64;
constexpr size_t kQueueDepth = 16;
constexpr uint32_t kTaskStackBytes = 3072;
constexpr UBaseType_t kTaskPriority = 3;
constexpr BaseType_t kLinkCore = 0;
constexpr uint32_t kPacketSamples = btlink::kAudioFramesPerPacket * 2;
}  // namespace

void BtLink::begin() {
  uart_config_t config = {};
  config.baud_rate = static_cast<int>(btlink::kBaudRate);
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  uart_driver_install(kPort, kRxBufferBytes, kTxBufferBytes, 0, nullptr, 0);
  uart_param_config(kPort, &config);
  uart_set_pin(kPort, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_set_rx_full_threshold(kPort, kRxFullThreshold);

  pcm_ = static_cast<int16_t *>(heap_caps_malloc(kPacketSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  payload_ = static_cast<uint8_t *>(heap_caps_malloc(btlink::kAudioPayloadBytes, MALLOC_CAP_SPIRAM));
  frame_ = static_cast<uint8_t *>(heap_caps_malloc(btlink::kMaxPacketBytes, MALLOC_CAP_SPIRAM));
  toLink_ = xQueueCreate(kQueueDepth, sizeof(Control));
  fromLink_ = xQueueCreate(kQueueDepth, sizeof(Control));
  xTaskCreatePinnedToCore(&BtLink::taskTrampoline, "btlink", kTaskStackBytes, this,
                          kTaskPriority, nullptr, kLinkCore);
}

void BtLink::send(btlink::PacketType type, const uint8_t *payload, uint16_t length) {
  if (toLink_ == nullptr || length > kMaxControlPayload) return;
  Control control{type, static_cast<uint8_t>(length), {}};
  if (length > 0) std::memcpy(control.payload, payload, length);
  xQueueSend(toLink_, &control, 0);
}

bool BtLink::receive(btlink::Packet &out) {
  Control control;
  if (fromLink_ == nullptr || xQueueReceive(fromLink_, &control, 0) != pdTRUE) return false;
  out.type = control.type;
  out.length = control.length;
  std::memcpy(out.payload.data(), control.payload, control.length);
  return true;
}

void BtLink::taskTrampoline(void *self) { static_cast<BtLink *>(self)->taskLoop(); }

void BtLink::taskLoop() {
  for (;;) {
    // All three every round: one finding work must not starve the others.
    const bool received = pumpReceived();
    const bool controlled = pumpControl();
    const bool streamed = pumpAudio();
    if (!received && !controlled && !streamed) vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool BtLink::pumpReceived() {
  uint8_t bytes[128];
  const int n = uart_read_bytes(kPort, bytes, sizeof(bytes), 0);
  for (int i = 0; i < n; ++i) {
    if (!reader_.feed(bytes[i])) continue;
    const btlink::Packet &packet = reader_.packet();
    if (packet.length > kMaxControlPayload) continue;
    Control control{packet.type, static_cast<uint8_t>(packet.length), {}};
    std::memcpy(control.payload, packet.payload.data(), packet.length);
    xQueueSend(fromLink_, &control, 0);
  }
  return n > 0;
}

bool BtLink::pumpControl() {
  Control control;
  if (xQueueReceive(toLink_, &control, 0) != pdTRUE) return false;
  write(control.type, control.payload, control.length);
  return true;
}

bool BtLink::pumpAudio() {
  if (!tap_.forwarding()) {
    ring_.clear();
    return false;
  }
  if (ring_.available() < kPacketSamples) return false;
  ring_.read(pcm_, kPacketSamples);
  btlink::putU32(payload_, tap_.rate());
  for (uint32_t i = 0; i < kPacketSamples; ++i) {
    const auto s = static_cast<uint16_t>(pcm_[i]);
    payload_[btlink::kAudioHeaderBytes + i * 2] = static_cast<uint8_t>(s & 0xFF);
    payload_[btlink::kAudioHeaderBytes + i * 2 + 1] = static_cast<uint8_t>(s >> 8);
  }
  write(btlink::PacketType::Audio, payload_, btlink::kAudioPayloadBytes);
  return true;
}

// Blocks while the UART's TX buffer is full: at 3 Mbaud a packet drains in
// ~3.4 ms, which is what paces this task.
void BtLink::write(btlink::PacketType type, const uint8_t *payload, uint16_t length) {
  const size_t n = btlink::encodePacket(type, txSeq_++, payload, length, frame_);
  if (n > 0) uart_write_bytes(kPort, frame_, n);
}

}  // namespace drehklang::drivers
```

- [ ] **Step 3: Give the hooks the tap.** In `lib/drivers-audio/AudioHooks.h`, add before the closing namespace:

```cpp
// Bluetooth headphones (ADR 0027): where audio_process_i2s copies what it
// plays, or nullptr. Set once in setup(), before anything plays.
void setBluetoothTap(bluetooth::AudioTap *tap);
bluetooth::AudioTap *bluetoothTap();

// The decoder's current sample rate, for the tap. Set by
// Esp32AudioI2SDriver's loop task.
void setDecoderRate(uint32_t rate);
uint32_t decoderRate();
```

Add above `namespace drehklang::drivers {` in that header:

```cpp
#include <cstdint>

namespace drehklang::bluetooth {
class AudioTap;
}
```

In `lib/drivers-audio/AudioHooks.cpp`, add `#include "AudioTap.h"` after `#include "AudioOutputStage.h"`, and inside the anonymous namespace:

```cpp
std::atomic<drehklang::bluetooth::AudioTap *> g_bluetoothTap{nullptr};
std::atomic<uint32_t> g_decoderRate{44100};
```

After `bool holdingOutput() ...` add:

```cpp
void setBluetoothTap(bluetooth::AudioTap *tap) {
  g_bluetoothTap.store(tap, std::memory_order_release);
}

bluetooth::AudioTap *bluetoothTap() { return g_bluetoothTap.load(std::memory_order_acquire); }

void setDecoderRate(uint32_t rate) { g_decoderRate.store(rate, std::memory_order_relaxed); }

uint32_t decoderRate() { return g_decoderRate.load(std::memory_order_relaxed); }
```

Replace the whole `audio_process_i2s` with:

```cpp
// After the volume: the sleep timer's fade (ADR 0015), silence while a
// resume position is still being applied, and a copy for Bluetooth
// headphones -- exactly what the jack gets (ADR 0027).
void audio_process_i2s(int32_t *samples, int16_t words, bool *continueI2S) {
  *continueI2S = true;
  if (drehklang::drivers::holdingOutput()) {
    std::fill(samples, samples + words, 0);
    return;
  }
  const uint16_t gain = drehklang::drivers::audioOutputStage().outputGain();
  if (gain < drehklang::playback::AudioGain::kUnityOutputGain) {
    for (int16_t i = 0; i < words; ++i) {
      samples[i] = drehklang::playback::AudioGain::applyOutputGain(samples[i], gain);
    }
  }
  if (auto *tap = drehklang::drivers::bluetoothTap()) {
    tap->pushStereo32(samples, static_cast<size_t>(words), drehklang::drivers::decoderRate());
  }
}
```

- [ ] **Step 4: Publish the decoder rate.** In `Esp32AudioI2SDriver::taskLoop()` (`lib/drivers-audio/Esp32AudioI2SDriver.cpp`), replace

```cpp
        if (audio_->isRunning()) sampleRate_.store(audio_->getSampleRate());
```

with

```cpp
        if (audio_->isRunning()) {
          const uint32_t rate = audio_->getSampleRate();
          sampleRate_.store(rate);
          setDecoderRate(rate);  // For the Bluetooth tap (ADR 0027).
        }
```

Add `#include "AudioHooks.h"` to that file if it is not there yet.

- [ ] **Step 5: Copy tones and blips.** In `lib/drivers-audio/ToneOutput.cpp`, add `#include "AudioHooks.h"` and `#include "AudioTap.h"`. Replace the last three lines of `writeChunk()`:

```cpp
  size_t written = 0;
  return i2s_channel_write(channel_, chunk_, sizeof(chunk_), &written, kWriteTimeout) == ESP_OK;
```

with

```cpp
  size_t written = 0;
  if (i2s_channel_write(channel_, chunk_, sizeof(chunk_), &written, kWriteTimeout) != ESP_OK) {
    return false;
  }
  // Bluetooth gets what the jack gets (ADR 0027).
  if (auto *tap = bluetoothTap()) tap->pushStereo16(chunk_, kChunkFrames * 2, rate);
  return true;
```

- [ ] **Step 6: Wire it in `src/main.cpp`.**

Includes, next to the others:

```cpp
#include "AudioHooks.h"
#include "AudioTap.h"
#include "BtController.h"
#include "BtLink.h"
#include "PcmRing.h"
```

Globals, after `drehklang::drivers::ToneOutput g_toneOutput(g_dac);`:

```cpp
// Bluetooth headphones (ADR 0027). The ring's storage comes from PSRAM in
// setup() -- PSRAM is not up yet while globals are constructed.
constexpr uint32_t kBtRingSamples = 8192;  // 16 KB, ~85 ms at 48 kHz.
drehklang::btaudio::PcmRing g_btRing;
drehklang::bluetooth::AudioTap g_btTap(g_btRing);
drehklang::drivers::BtLink g_btLink(g_btRing, g_btTap);
drehklang::bluetooth::BtController g_btController(g_btLink, g_nvsStore);
```

(`g_nvsStore` is declared above `g_toneOutput`. Keep this block after both.)

In `setup()`, directly after `g_toneOutput.begin();`:

```cpp
  g_btRing.attach(static_cast<int16_t *>(heap_caps_calloc(
                      kBtRingSamples, sizeof(int16_t), MALLOC_CAP_SPIRAM)),
                  kBtRingSamples);
  drehklang::drivers::setBluetoothTap(&g_btTap);
  g_btLink.begin();
  g_btController.begin(millis());
```

In `loop()`, directly before `g_playback.tick(now);` (~line 801):

```cpp
  static drehklang::btlink::Packet btPacket;
  while (g_btLink.receive(btPacket)) g_btController.onPacket(btPacket, now);
  g_btController.tick(now);
  g_btTap.setForwarding(g_btController.forwardAudio());
```

Serial command, after the `BLIP` branch (~line 540):

```cpp
        } else if (strcmp(buf, "BT") == 0) {
          // The Bluetooth link at a glance (ADR 0027), for checking on the
          // device without the UI.
          const auto &stats = g_btController.stats();
          Serial.printf("[bt] state %d enabled %d scanning %d paired '%s' results %u "
                        "tap rate %lu dropped %lu | U4WDH underruns %lu crc %lu lost %lu\n",
                        static_cast<int>(g_btController.state()), g_btController.enabled(),
                        g_btController.scanning(), g_btController.pairedName().c_str(),
                        static_cast<unsigned>(g_btController.scanResults().size()),
                        static_cast<unsigned long>(g_btTap.rate()),
                        static_cast<unsigned long>(g_btTap.dropped()),
                        static_cast<unsigned long>(stats.underruns),
                        static_cast<unsigned long>(stats.crcErrors),
                        static_cast<unsigned long>(stats.lostPackets));
```

Match the surrounding branch style (the existing branches use `sscanf`; `strcmp` needs `<cstring>`, which `main.cpp` may already include. Check, and add it if not).

- [ ] **Step 7: Build the firmware**

Run: `pio run -e esp32-s3`
Expected: SUCCESS. If `uart_set_rx_full_threshold` or `UART_SCLK_DEFAULT` are missing, include `<driver/uart.h>` exactly as written. Both exist in ESP-IDF 5.5 and compiled in the spike.

- [ ] **Step 8: Check the weak hooks still link strong**

Run: `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm -C .pio/build/esp32-s3/firmware.elf | grep audio_process`
Expected: both `audio_process_raw_samples` and `audio_process_i2s` with `T`, not `W` (AGENTS.md). If the toolchain directory differs, find it with `ls ~/.platformio/packages | grep xtensa`.

- [ ] **Step 9: Run the native suite**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add lib/drivers-bluetooth lib/drivers-audio src/main.cpp
git commit -m "Send what the DAC plays to the Bluetooth chip

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The U4WDH firmware (`bt/`) and its flash script

**Files:**
- Create: `bt/platformio.ini`, `bt/src/main.cpp`, `bt/src/LinkUart.h`, `bt/src/LinkUart.cpp`, `bt/src/BtSource.h`, `bt/src/BtSource.cpp`, `bt/src/PeerStore.h`, `bt/src/PeerStore.cpp`, `scripts/flash-bt-mcu.sh`
- Modify: `.gitignore` (add `bt/.pio/` if `.pio` is not already ignored everywhere)

**Interfaces:**
- Consumes: `btlink::*` (Task 2), `btaudio::HeadphoneStream` (Task 4) through `lib_extra_dirs = ../lib`.
- Produces: nothing other tasks link against. The firmware speaks the protocol in Task 2.

- [ ] **Step 1: Write `bt/platformio.ini`**

```ini
; Drehklang's Bluetooth firmware for the board's second chip, the
; ESP32-U4WDH (ADR 0027): A2DP source and AVRCP target, fed PCM by the S3
; over the shared UART. Flashed through the CH340 with the USB-C plug the
; other way round -- scripts/flash-bt-mcu.sh.
;
; A project of its own rather than a second env in ../platformio.ini:
; PlatformIO has one src_dir per project. The protocol and the stream come
; from ../lib (btlink, btaudio), shared with the S3 and the host tests.

[env:esp32-bt]
; The same platform as the S3, pinned to the same release asset.
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip
board = esp32dev
framework = arduino
lib_extra_dirs = ../lib
lib_ldf_mode = chain
build_flags =
    -std=gnu++17
build_unflags =
    -std=gnu++11
monitor_speed = 115200
monitor_dtr = 0
monitor_rts = 0
```

- [ ] **Step 2: Write `bt/src/PeerStore.h` and `.cpp`**

```cpp
// PeerStore.h
#pragma once

#include <string>

#include "Messages.h"

namespace drehklang::bt {

// The one paired pair of headphones, in this chip's own NVS. The bonding
// keys are Bluedroid's own business; this keeps whom to reconnect to.
class PeerStore {
 public:
  void begin();
  bool has() const { return has_; }
  const btlink::Address &address() const { return address_; }
  const std::string &name() const { return name_; }
  void save(const btlink::Address &address, const std::string &name);
  void clear();

 private:
  bool has_ = false;
  btlink::Address address_{};
  std::string name_;
};

}  // namespace drehklang::bt
```

```cpp
// PeerStore.cpp
#include "PeerStore.h"

#include <Preferences.h>

namespace drehklang::bt {

namespace {
constexpr char kNamespace[] = "btpeer";
}

void PeerStore::begin() {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  has_ = prefs.getBytes("addr", address_.data(), address_.size()) == address_.size();
  name_ = prefs.getString("name", "").c_str();
  prefs.end();
}

void PeerStore::save(const btlink::Address &address, const std::string &name) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putBytes("addr", address.data(), address.size());
  prefs.putString("name", name.c_str());
  prefs.end();
  has_ = true;
  address_ = address;
  name_ = name;
}

void PeerStore::clear() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.clear();
  prefs.end();
  has_ = false;
  address_ = {};
  name_.clear();
}

}  // namespace drehklang::bt
```

- [ ] **Step 3: Write `bt/src/LinkUart.h` and `.cpp`**

```cpp
// LinkUart.h
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <cstdint>

#include "HeadphoneStream.h"
#include "Packet.h"
#include "PacketReader.h"

namespace drehklang::bt {

// This chip's end of the link (ADR 0027): UART2 at 3 Mbaud, TX IO18, RX
// IO23. A receive task on core 1 puts AUDIO straight into the stream and
// queues everything else for loop().
class LinkUart {
 public:
  explicit LinkUart(btaudio::HeadphoneStream &stream) : stream_(stream) {}
  void begin();
  // Any task. Serialised by a mutex.
  void send(btlink::PacketType type, const uint8_t *payload, uint16_t length);
  // loop(): the next control packet, if any.
  bool receive(btlink::Packet &out);
  uint32_t crcErrors() const { return reader_.crcErrors(); }
  uint32_t lostPackets() const { return reader_.lostPackets(); }

 private:
  static constexpr size_t kMaxControlPayload = 64;
  struct Control {
    btlink::PacketType type;
    uint8_t length;
    uint8_t payload[kMaxControlPayload];
  };
  [[noreturn]] void receiveLoop();
  static void trampoline(void *self);

  btaudio::HeadphoneStream &stream_;
  btlink::PacketReader reader_;
  QueueHandle_t control_ = nullptr;
  SemaphoreHandle_t sendMutex_ = nullptr;
  uint8_t txSeq_ = 0;
  uint8_t frame_[btlink::kMaxPacketBytes] = {};
};

}  // namespace drehklang::bt
```

```cpp
// LinkUart.cpp
#include "LinkUart.h"

#include <driver/uart.h>
#include <freertos/task.h>

#include <cstring>

#include "Messages.h"

namespace drehklang::bt {

namespace {
constexpr uart_port_t kPort = UART_NUM_2;
constexpr int kTxPin = 18;
constexpr int kRxPin = 23;
constexpr int kRxBufferBytes = 8192;
constexpr int kTxBufferBytes = 1024;
// The default 120 of 128 bytes lost packets at 3 Mbaud on this chip; 64
// gave none in 10 minutes (spike, 2026-10-05).
constexpr uint8_t kRxFullThreshold = 64;
constexpr size_t kQueueDepth = 16;
}  // namespace

void LinkUart::begin() {
  uart_config_t config = {};
  config.baud_rate = static_cast<int>(btlink::kBaudRate);
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  uart_driver_install(kPort, kRxBufferBytes, kTxBufferBytes, 0, nullptr, 0);
  uart_param_config(kPort, &config);
  uart_set_pin(kPort, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_set_rx_full_threshold(kPort, kRxFullThreshold);
  control_ = xQueueCreate(kQueueDepth, sizeof(Control));
  sendMutex_ = xSemaphoreCreateMutex();
  // Core 1: Bluedroid and the controller run on core 0.
  xTaskCreatePinnedToCore(&LinkUart::trampoline, "link", 4096, this, 10, nullptr, 1);
}

void LinkUart::send(btlink::PacketType type, const uint8_t *payload, uint16_t length) {
  xSemaphoreTake(sendMutex_, portMAX_DELAY);
  const size_t n = btlink::encodePacket(type, txSeq_++, payload, length, frame_);
  if (n > 0) uart_write_bytes(kPort, frame_, n);
  xSemaphoreGive(sendMutex_);
}

bool LinkUart::receive(btlink::Packet &out) {
  Control control;
  if (xQueueReceive(control_, &control, 0) != pdTRUE) return false;
  out.type = control.type;
  out.length = control.length;
  std::memcpy(out.payload.data(), control.payload, control.length);
  return true;
}

void LinkUart::trampoline(void *self) { static_cast<LinkUart *>(self)->receiveLoop(); }

void LinkUart::receiveLoop() {
  uint8_t bytes[512];
  for (;;) {
    const int n = uart_read_bytes(kPort, bytes, sizeof(bytes), pdMS_TO_TICKS(20));
    for (int i = 0; i < n; ++i) {
      if (!reader_.feed(bytes[i])) continue;
      const btlink::Packet &packet = reader_.packet();
      if (packet.type == btlink::PacketType::Audio) {
        if (packet.length < btlink::kAudioHeaderBytes) continue;
        stream_.push(btlink::getU32(packet.payload.data()),
                     packet.payload.data() + btlink::kAudioHeaderBytes,
                     (packet.length - btlink::kAudioHeaderBytes) / 2);
        continue;
      }
      if (packet.length > kMaxControlPayload) continue;
      Control control{packet.type, static_cast<uint8_t>(packet.length), {}};
      std::memcpy(control.payload, packet.payload.data(), packet.length);
      xQueueSend(control_, &control, 0);
    }
  }
}

}  // namespace drehklang::bt
```

- [ ] **Step 4: Write `bt/src/BtSource.h`**

```cpp
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "HeadphoneStream.h"
#include "Messages.h"

namespace drehklang::bt {

// What Bluedroid's callbacks report, handed to loop() through a queue so no
// callback ever sends on the link or calls back into the stack.
struct BtSourceEvent {
  enum class Kind : uint8_t { Found, Button, Connected, Disconnected } kind;
  btlink::Address address{};
  int8_t rssi = 0;
  btlink::ButtonCode button = btlink::ButtonCode::Play;
  char name[btlink::kMaxNameBytes + 1] = {};
};

// ESP-IDF's Bluedroid as an A2DP source with an AVRCP target (ADR 0027).
// Bluedroid encodes SBC itself from the PCM the data callback renders out
// of the HeadphoneStream.
class BtSource {
 public:
  explicit BtSource(btaudio::HeadphoneStream &stream);
  void begin();
  bool nextEvent(BtSourceEvent &out);

  void startScan();
  void stopScan();
  void connect(const btlink::Address &address);
  void disconnect();
  void removeBond(const btlink::Address &address);
  // After a connection: check the stream endpoint and start streaming.
  void startMedia();

  btlink::LinkState link() const { return link_.load(); }
  bool scanning() const { return scanning_.load(); }

 private:
  static void onGap(int event, void *param);
  static void onA2dp(int event, void *param);
  static void onAvrcTarget(int event, void *param);
  static int32_t onData(uint8_t *data, int32_t length);
  void post(const BtSourceEvent &event);

  btaudio::HeadphoneStream &stream_;
  QueueHandle_t events_ = nullptr;
  std::atomic<btlink::LinkState> link_{btlink::LinkState::Idle};
  std::atomic<bool> scanning_{false};
  btlink::Address connected_{};
};

}  // namespace drehklang::bt
```

- [ ] **Step 5: Write `bt/src/BtSource.cpp`**

```cpp
#include "BtSource.h"

#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>

#include <cstdio>
#include <cstring>

namespace drehklang::bt {

namespace {
BtSource *g_self = nullptr;
// 8 x 1.28 s: the 10 s the spec gives a scan.
constexpr uint8_t kInquiryLength = 8;

void toAddress(const uint8_t *bda, btlink::Address &out) { std::memcpy(out.data(), bda, 6); }

// The name from a discovery result: the full or short EIR name, then the
// BDNAME property, then the address.
void nameOf(esp_bt_gap_cb_param_t *param, char *out, size_t size) {
  out[0] = '\0';
  for (int i = 0; i < param->disc_res.num_prop; ++i) {
    esp_bt_gap_dev_prop_t &prop = param->disc_res.prop[i];
    if (prop.type == ESP_BT_GAP_DEV_PROP_EIR) {
      uint8_t length = 0;
      auto *eir = static_cast<uint8_t *>(prop.val);
      uint8_t *name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &length);
      if (name == nullptr) {
        name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &length);
      }
      if (name != nullptr) {
        const size_t n = length < size - 1 ? length : size - 1;
        std::memcpy(out, name, n);
        out[n] = '\0';
        return;
      }
    } else if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME) {
      snprintf(out, size, "%.*s", prop.len, static_cast<char *>(prop.val));
      return;
    }
  }
  const uint8_t *a = param->disc_res.bda;
  snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}
}  // namespace

// Keeps Arduino from releasing the Classic BT controller's memory before
// setup() (esp32-hal-misc.c). BLE is not used; its memory is released.
extern "C" bool btClassicInUse() { return true; }

BtSource::BtSource(btaudio::HeadphoneStream &stream) : stream_(stream) { g_self = this; }

void BtSource::begin() {
  events_ = xQueueCreate(16, sizeof(BtSourceEvent));
  esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  controller.mode = ESP_BT_MODE_CLASSIC_BT;
  esp_bt_controller_init(&controller);
  esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
  esp_bluedroid_config_t stack = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  esp_bluedroid_init_with_cfg(&stack);
  esp_bluedroid_enable();

  esp_bt_gap_set_device_name("Drehklang");
  esp_bt_gap_register_callback(
      [](esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t *p) { onGap(e, p); });
  // Headphones confirm nothing on a screen: "just works" pairing.
  esp_bt_io_cap_t ioCap = ESP_BT_IO_CAP_NONE;
  esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &ioCap, sizeof(ioCap));

  // AVRCP before A2DP, as ESP-IDF asks.
  esp_avrc_ct_init();
  esp_avrc_tg_register_callback(
      [](esp_avrc_tg_cb_event_t e, esp_avrc_tg_cb_param_t *p) { onAvrcTarget(e, p); });
  esp_avrc_tg_init();
  esp_avrc_psth_bit_mask_t commands = {};
  esp_avrc_tg_get_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_ALLOWED_CMD, &commands);
  esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &commands);

  esp_a2d_register_callback([](esp_a2d_cb_event_t e, esp_a2d_cb_param_t *p) { onA2dp(e, p); });
  esp_a2d_source_register_data_callback(&BtSource::onData);
  esp_a2d_source_init();

  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
}

bool BtSource::nextEvent(BtSourceEvent &out) {
  return xQueueReceive(events_, &out, 0) == pdTRUE;
}

void BtSource::startScan() {
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquiryLength, 0);
}

void BtSource::stopScan() { esp_bt_gap_cancel_discovery(); }

void BtSource::connect(const btlink::Address &address) {
  btlink::Address copy = address;
  link_.store(btlink::LinkState::Connecting);
  esp_a2d_source_connect(copy.data());
}

void BtSource::disconnect() {
  if (link_.load() == btlink::LinkState::Idle) return;
  esp_a2d_source_disconnect(connected_.data());
}

void BtSource::removeBond(const btlink::Address &address) {
  btlink::Address copy = address;
  esp_bt_gap_remove_bond_device(copy.data());
}

void BtSource::startMedia() { esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY); }

void BtSource::post(const BtSourceEvent &event) { xQueueSend(events_, &event, 0); }

void BtSource::onGap(int event, void *raw) {
  auto *param = static_cast<esp_bt_gap_cb_param_t *>(raw);
  switch (static_cast<esp_bt_gap_cb_event_t>(event)) {
    case ESP_BT_GAP_DISC_RES_EVT: {
      uint32_t cod = 0;
      int8_t rssi = -127;
      for (int i = 0; i < param->disc_res.num_prop; ++i) {
        const auto &prop = param->disc_res.prop[i];
        if (prop.type == ESP_BT_GAP_DEV_PROP_COD) cod = *static_cast<uint32_t *>(prop.val);
        if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI) rssi = *static_cast<int8_t *>(prop.val);
      }
      // Headphones and speakers only: the list is for something to listen with.
      if (!esp_bt_gap_is_valid_cod(cod) ||
          esp_bt_gap_get_cod_major_dev(cod) != ESP_BT_COD_MAJOR_DEV_AV) {
        return;
      }
      BtSourceEvent found{BtSourceEvent::Kind::Found};
      toAddress(param->disc_res.bda, found.address);
      found.rssi = rssi;
      nameOf(param, found.name, sizeof(found.name));
      g_self->post(found);
      break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
      g_self->scanning_.store(param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED);
      break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
      // Legacy pairing: the PIN headphones nearly always use.
      esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
      esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
      break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
      esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
      break;
    default:
      break;
  }
}

void BtSource::onA2dp(int event, void *raw) {
  auto *param = static_cast<esp_a2d_cb_param_t *>(raw);
  switch (static_cast<esp_a2d_cb_event_t>(event)) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
      const auto state = param->conn_stat.state;
      if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        toAddress(param->conn_stat.remote_bda, g_self->connected_);
        g_self->link_.store(btlink::LinkState::Connected);
        BtSourceEvent connected{BtSourceEvent::Kind::Connected};
        g_self->post(connected);
      } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        g_self->link_.store(btlink::LinkState::Idle);
        BtSourceEvent gone{BtSourceEvent::Kind::Disconnected};
        g_self->post(gone);
      } else if (state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
        g_self->link_.store(btlink::LinkState::Connecting);
      }
      break;
    }
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
      // The stream stays started while connected: some headphones switch
      // themselves off after a few minutes of a suspended stream.
      if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
          param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
      }
      break;
    default:
      break;
  }
}

void BtSource::onAvrcTarget(int event, void *raw) {
  auto *param = static_cast<esp_avrc_tg_cb_param_t *>(raw);
  if (static_cast<esp_avrc_tg_cb_event_t>(event) != ESP_AVRC_TG_PASSTHROUGH_CMD_EVT) return;
  if (param->psth_cmd.key_state != ESP_AVRC_PT_CMD_STATE_PRESSED) return;
  BtSourceEvent pressed{BtSourceEvent::Kind::Button};
  switch (param->psth_cmd.key_code) {
    case ESP_AVRC_PT_CMD_PLAY:
      pressed.button = btlink::ButtonCode::Play;
      break;
    case ESP_AVRC_PT_CMD_PAUSE:
    case ESP_AVRC_PT_CMD_STOP:
      pressed.button = btlink::ButtonCode::Pause;
      break;
    default:
      return;
  }
  g_self->post(pressed);
}

// Bluedroid asks for `length` bytes of 44.1 kHz 16-bit stereo PCM.
int32_t BtSource::onData(uint8_t *data, int32_t length) {
  if (data == nullptr || length <= 0) return 0;
  const size_t frames = static_cast<size_t>(length) / 4;
  g_self->stream_.render(reinterpret_cast<int16_t *>(data), frames);
  return static_cast<int32_t>(frames * 4);
}

}  // namespace drehklang::bt
```

The callbacks' first parameters are typed `int`/`void *` in the class so that `BtSource.h` needs no Bluedroid headers. The lambdas in `begin()` convert. If the compiler rejects a lambda-to-function-pointer conversion, declare `onGap` etc. with the exact Bluedroid signatures in `BtSource.cpp`'s anonymous namespace instead and register those.

- [ ] **Step 6: Write `bt/src/main.cpp`**

```cpp
// Drehklang's Bluetooth firmware for the ESP32-U4WDH (ADR 0027). Talks to
// the S3 over the shared UART, and to headphones as an A2DP source.
// Serial (UART0, the CH340) is for logs only -- and only from loop().

#include <Arduino.h>

#include "BtSource.h"
#include "HeadphoneStream.h"
#include "LinkUart.h"
#include "Messages.h"
#include "Packet.h"
#include "PeerStore.h"

using namespace drehklang;

namespace {

// 16384 samples, 32 KB: ~186 ms at 44.1 kHz. Internal RAM -- this chip has no PSRAM.
constexpr uint32_t kRingSamples = 16384;
int16_t g_ringStorage[kRingSamples];

btaudio::HeadphoneStream g_stream(g_ringStorage, kRingSamples);
bt::LinkUart g_link(g_stream);
bt::BtSource g_source(g_stream);
bt::PeerStore g_peer;

constexpr uint32_t kStateIntervalMs = 1000;
constexpr uint32_t kStatsIntervalMs = 5000;
constexpr uint32_t kReconnectIntervalMs = 10000;

bool g_enabled = false;
uint32_t g_lastStateMs = 0;
uint32_t g_lastStatsMs = 0;
uint32_t g_lastConnectMs = 0;

void sendState() {
  btlink::StateMessage m;
  m.link = g_enabled ? g_source.link() : btlink::LinkState::Off;
  m.scanning = g_source.scanning();
  m.paired = g_peer.has();
  m.address = g_peer.address();
  m.name = g_peer.name();
  uint8_t payload[btlink::kMaxPayload];
  g_link.send(btlink::PacketType::State, payload,
              static_cast<uint16_t>(btlink::encodeState(m, payload)));
}

void sendStats() {
  btlink::StatsMessage m{g_stream.underruns(), g_link.crcErrors(), g_link.lostPackets()};
  uint8_t payload[12];
  g_link.send(btlink::PacketType::Stats, payload,
              static_cast<uint16_t>(btlink::encodeStats(m, payload)));
}

void connectToPeer(uint32_t nowMs) {
  g_lastConnectMs = nowMs;
  g_source.connect(g_peer.address());
}

void onPair(const btlink::Packet &packet, uint32_t nowMs) {
  btlink::PairMessage m;
  if (!btlink::decodePair(packet, m)) return;
  g_source.stopScan();
  if (g_peer.has() && g_peer.address() != m.address) {
    g_source.disconnect();
    g_source.removeBond(g_peer.address());
  }
  g_peer.save(m.address, m.name);
  if (g_enabled) connectToPeer(nowMs);
}

void onControl(const btlink::Packet &packet, uint32_t nowMs) {
  switch (packet.type) {
    case btlink::PacketType::Hello: {
      const uint8_t payload[] = {btlink::kProtocolVersion};
      g_link.send(btlink::PacketType::Hello, payload, sizeof(payload));
      break;
    }
    case btlink::PacketType::Enable:
      g_enabled = packet.length > 0 && packet.payload[0] != 0;
      if (!g_enabled) {
        g_source.stopScan();
        g_source.disconnect();
      } else if (g_peer.has() && g_source.link() == btlink::LinkState::Idle) {
        connectToPeer(nowMs);
      }
      break;
    case btlink::PacketType::ScanStart:
      if (g_enabled) g_source.startScan();
      break;
    case btlink::PacketType::ScanStop:
      g_source.stopScan();
      break;
    case btlink::PacketType::Pair:
      onPair(packet, nowMs);
      break;
    case btlink::PacketType::Forget:
      g_source.disconnect();
      if (g_peer.has()) g_source.removeBond(g_peer.address());
      g_peer.clear();
      break;
    default:
      break;
  }
  sendState();
}

void onSourceEvent(const bt::BtSourceEvent &event) {
  switch (event.kind) {
    case bt::BtSourceEvent::Kind::Found: {
      btlink::ScanResultMessage m{event.address, event.rssi, event.name};
      uint8_t payload[btlink::kMaxPayload];
      g_link.send(btlink::PacketType::ScanResult, payload,
                  static_cast<uint16_t>(btlink::encodeScanResult(m, payload)));
      break;
    }
    case bt::BtSourceEvent::Kind::Button: {
      const uint8_t payload[] = {static_cast<uint8_t>(event.button)};
      g_link.send(btlink::PacketType::Button, payload, sizeof(payload));
      break;
    }
    case bt::BtSourceEvent::Kind::Connected:
      g_source.startMedia();
      Serial.println("[bt] connected");
      sendState();
      break;
    case bt::BtSourceEvent::Kind::Disconnected:
      Serial.println("[bt] disconnected");
      sendState();
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  g_peer.begin();
  g_link.begin();
  g_source.begin();
  Serial.printf("[bt] Drehklang BT firmware, protocol %u, peer %s\n",
                btlink::kProtocolVersion, g_peer.has() ? g_peer.name().c_str() : "none");
}

void loop() {
  const uint32_t now = millis();
  static btlink::Packet packet;
  while (g_link.receive(packet)) onControl(packet, now);
  bt::BtSourceEvent event;
  while (g_source.nextEvent(event)) onSourceEvent(event);
  if (now - g_lastStateMs >= kStateIntervalMs) {
    g_lastStateMs = now;
    sendState();
  }
  if (now - g_lastStatsMs >= kStatsIntervalMs) {
    g_lastStatsMs = now;
    sendStats();
    Serial.printf("[bt] link %d underruns %lu crc %lu lost %lu dropped %lu\n",
                  static_cast<int>(g_source.link()), static_cast<unsigned long>(g_stream.underruns()),
                  static_cast<unsigned long>(g_link.crcErrors()),
                  static_cast<unsigned long>(g_link.lostPackets()),
                  static_cast<unsigned long>(g_stream.dropped()));
  }
  if (g_enabled && g_peer.has() && g_source.link() == btlink::LinkState::Idle &&
      now - g_lastConnectMs >= kReconnectIntervalMs) {
    connectToPeer(now);
  }
  delay(5);
}
```

`btlink::ScanResultMessage m{event.address, event.rssi, event.name}` uses aggregate initialisation with a `char[]` converting to `std::string`, which is fine.

- [ ] **Step 7: Build it**

Run: `pio run -d bt -e esp32-bt`
Expected: SUCCESS. Typical fixes if it does not build:
- the ESP-IDF 5.5 name of a field (`param->disc_st_chg.state`, `param->media_ctrl_stat.cmd`): look it up in `~/.platformio/packages/framework-arduinoespressif32-libs/esp32/include/bt/host/bluedroid/api/include/api/` and use that header's exact field;
- `esp_a2d_source_register_data_callback`: it is declared in `esp_a2dp_legacy_api.h`, which `esp_a2dp_api.h` includes.

- [ ] **Step 8: Write `scripts/flash-bt-mcu.sh`**

```bash
#!/usr/bin/env bash
# Flashes the Bluetooth firmware (bt/) to the board's SECOND MCU, the
# ESP32-U4WDH (ADR 0027). It is reached through the CH340 with the USB-C
# plug turned so the CH340 shows up (/dev/cu.usbserial-*), not the S3's
# native USB port. Checks with esptool that an ESP32 -- not the S3 --
# answers there before writing anything.
#
# Back to the factory image: hardware-backups/restore.sh secondary
#
# Usage: flash-bt-mcu.sh [--port /dev/cu.XXXX]
set -euo pipefail
cd "$(dirname "$0")/.."

PORT=""
if [[ "${1:-}" == "--port" ]]; then
  PORT="$2"
fi

if [[ -z "$PORT" ]]; then
  PORT="$(pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
for d in json.load(sys.stdin):
    if "1A86:7523" in d.get("hwid", "").upper():
        print(d["port"])
        break
' || true)"
fi

if [[ -z "$PORT" ]]; then
  echo "The CH340 is not connected. Turn the USB-C plug over and try again." >&2
  exit 1
fi

PYTHON="$HOME/.platformio/penv/bin/python"
CHIP="$("$PYTHON" -m esptool --port "$PORT" chip_id 2>&1 | grep -E '^Chip (type|is)' || true)"
echo "$CHIP"
if [[ "$CHIP" != *ESP32-U4WDH* && "$CHIP" != *"ESP32-D"* ]]; then
  echo "That port does not reach the ESP32-U4WDH. Turn the USB-C plug over and try again." >&2
  exit 1
fi

echo "== Flashing bt/ (ESP32-U4WDH) via $PORT =="
pio run -d bt -e esp32-bt -t upload --upload-port "$PORT"
echo "== Flash OK =="
```

Then `chmod +x scripts/flash-bt-mcu.sh`.

- [ ] **Step 9: Flash and look at it** (needs the board; ask the user to turn the plug so the CH340 shows)

Run: `scripts/flash-bt-mcu.sh`, then read the log with RTS released:

```bash
~/.platformio/penv/bin/python - <<'EOF'
import serial, time
s = serial.Serial()
s.port = sorted(__import__('glob').glob('/dev/cu.usbserial-*'))[0]
s.baudrate = 115200; s.timeout = 1; s.dtr = False; s.rts = False
s.open()
end = time.time() + 15
while time.time() < end:
    line = s.readline()
    if line: print(line.decode(errors='replace').rstrip())
EOF
```

Expected: `[bt] Drehklang BT firmware, protocol 1, peer none`, then a `[bt] link 1 ...` line every 5 s, or `link 0` while the S3 has not enabled it. No `Guru Meditation` and no reboot loop.

- [ ] **Step 10: Commit**

```bash
git add bt scripts/flash-bt-mcu.sh .gitignore
git commit -m "Add the Bluetooth firmware for the ESP32-U4WDH

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Settings > Bluetooth, messages, button, lock-screen glyph

**Files:**
- Modify: `lib/navigation/ScreenId.h` (append two kinds after `About`)
- Modify: `lib/ui/ScreenManager.h` (SettingsRow gets a value; Bluetooth members)
- Modify: `lib/ui/ScreenManagerMenu.cpp` (`kSettingsRows`)
- Modify: `lib/ui/ScreenManager.cpp` (row values, item ids, list dispatch, captions, empty text)
- Create: `lib/ui/ScreenManagerBluetooth.cpp`
- Modify: `lib/ui/BatteryIndicator.h`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `BtController`, `BtState`, `BtEvent`, `ScanEntry` (Task 6), `actionFor`/`ButtonContext`/`ButtonAction` (Task 6), `signal::ToneSession::{start,stop,running}`, `playback::PlaybackStateMachine::{state,hasQueue,togglePlayPause}`, `ui_widgets::MessageArea::show(text, anchor, nowMs, durationMs, scope)`.
- Produces: `ScreenManager::setBluetooth(bluetooth::BtController&)`, `ScreenManager::tickBluetooth(uint32_t)`, `BatteryIndicator::setBluetoothConnected(bool)`.

- [ ] **Step 1: Append the screen kinds.** In `lib/navigation/ScreenId.h`, after `About,`:

```cpp
  // Settings > Bluetooth and its search (ADR 0027). Appended like
  // everything above; past NowPlaying, so a resume never lands on them.
  Bluetooth,
  BluetoothSearch,
```

- [ ] **Step 2: Give Settings rows an optional value.** In `lib/ui/ScreenManager.h`, replace the `SettingsRow` struct and count with:

```cpp
  // One Settings row. A table rather than a chain of index comparisons,
  // so adding a row never silently renumbers the ones after it. `value`
  // is the plain text the row ends in, or nullptr for none.
  struct SettingsRow {
    const char *label;
    void (*open)(ScreenManager &self);
    std::string (*value)(const ScreenManager &self) = nullptr;
  };
  static constexpr int kSettingsRowCount = 7;
```

In the public section, after `setScopeSource(...)`:

```cpp
  // Bluetooth headphones (ADR 0027). Optional: without a controller
  // Settings shows no Bluetooth row content and the button does nothing.
  void setBluetooth(bluetooth::BtController &bt) { bluetooth_ = &bt; }

  // Messages for connect/disconnect, the headphone button, the search's
  // lifetime, and redraws when what the Bluetooth screens show changed.
  // Call every loop().
  void tickBluetooth(uint32_t nowMs);
```

In the private section, after the tone generator block:

```cpp
  // ScreenManagerBluetooth.cpp (ADR 0027).
  void appendBluetoothRows(std::vector<std::pair<std::string, int>> &items) const;
  void appendBluetoothSearchRows(std::vector<std::pair<std::string, int>> &items) const;
  std::string bluetoothRowValue(int itemId) const;
  std::string bluetoothSettingsValue() const;
  const char *bluetoothEmptyText() const;
  std::string bluetoothSearchCaption() const;
  void onBluetoothRow(int itemId);
  void onBluetoothSearchRow(int itemId);
  void openBluetoothSearch();
  void showBluetoothEvent(bluetooth::BtEvent event, uint32_t nowMs);
  void onHeadphoneButton(bluetooth::BtEvent event);
  bool touchHeld() const;
  // Bluetooth screen row ids. The rows come and go with the state, so a
  // click is dispatched by id, never by position.
  static constexpr int kBtToggleRow = 0;
  static constexpr int kBtDeviceRow = 1;
  static constexpr int kBtFindRow = 2;
  static constexpr int kBtForgetRow = 3;
  // The search's first row; results use their index.
  static constexpr int kBtSearchAgainItemId = -4;
  bluetooth::BtController *bluetooth_ = nullptr;
  uint32_t shownBtRevision_ = 0;
  bool shownBtScanning_ = false;
  bool btSearchOpen_ = false;
```

Add `int itemId = 0;` to `struct ItemContext` after `int index;`. Add `#include "BtController.h"` to the includes.

- [ ] **Step 3: Rows.** In `lib/ui/ScreenManagerMenu.cpp`, replace the `kSettingsRows` initialiser's Brightness entry so it carries its value, and insert Bluetooth before About:

```cpp
        {"Brightness",
         [](ScreenManager &self) {
           self.tabs_.activeStack().push(Screen{ScreenKind::Brightness, {}});
           self.render();
         },
         [](const ScreenManager &self) {
           return std::to_string(self.brightness_.percent()) + "%";
         }},
```

```cpp
        {"USB drive", [](ScreenManager &self) { self.startUsbDrive(); }},
        {"Bluetooth",
         [](ScreenManager &self) {
           self.tabs_.activeStack().push(Screen{ScreenKind::Bluetooth, {}});
           self.render();
         },
         [](const ScreenManager &self) { return self.bluetoothSettingsValue(); }},
        {"About", [](ScreenManager &self) { self.openAbout(); }},
```

- [ ] **Step 4: Render and dispatch** in `lib/ui/ScreenManager.cpp`:

(a) In the list's `switch (current.kind)` that fills `items` (after `case ScreenKind::MenuVisibility:`):

```cpp
      case ScreenKind::Bluetooth:
        appendBluetoothRows(items);
        break;
      case ScreenKind::BluetoothSearch:
        appendBluetoothSearchRows(items);
        break;
```

(b) Replace the Settings value branch

```cpp
    } else if (current.kind == ScreenKind::Settings && items[i].second == 0) {
      snprintf(valueText, sizeof(valueText), "%u%%",
               static_cast<unsigned>(brightness_.percent()));
      secondary = valueText;
    }
```

with

```cpp
    } else if (current.kind == ScreenKind::Settings &&
               kSettingsRows[items[i].second].value != nullptr) {
      // A row's current value, as its row in the table says (Brightness's
      // percentage, Bluetooth's headphones).
      secondaryText = kSettingsRows[items[i].second].value(*this);
      secondary = secondaryText.c_str();
    } else if (current.kind == ScreenKind::Bluetooth) {
      secondaryText = bluetoothRowValue(items[i].second);
      if (!secondaryText.empty()) secondary = secondaryText.c_str();
    }
```

Remove `char valueText[8] = {0};` if nothing else uses it (compile to check).

(c) After `ctx->isBrowseAxis = ...;` add `ctx->itemId = items[i].second;`.

(d) In `onListItemClicked`'s switch, after `case ScreenKind::MenuVisibility:`:

```cpp
    case ScreenKind::Bluetooth:
      self->onBluetoothRow(ctx->itemId);
      break;
    case ScreenKind::BluetoothSearch:
      self->onBluetoothSearchRow(ctx->itemId);
      break;
```

(e) In `captionTextFor()`:

```cpp
    case ScreenKind::Bluetooth:
      return "Bluetooth";
    case ScreenKind::BluetoothSearch:
      return bluetoothSearchCaption();
```

(f) At the top of `emptyListText()`, before the Folder line:

```cpp
  if (screen.kind == ScreenKind::Bluetooth) return bluetoothEmptyText();
```

- [ ] **Step 5: Write `lib/ui/ScreenManagerBluetooth.cpp`**

```cpp
// Settings > Bluetooth and its search (ADR 0027), the headphone button, and
// the messages for connecting and disconnecting. Kept apart from the music
// screens like the tone generator's file.

#include <lvgl.h>

#include <string>

#include "HeadphoneButton.h"
#include "ScreenManager.h"

namespace drehklang::ui {

using bluetooth::BtEvent;
using bluetooth::BtState;
using navigation::Screen;
using navigation::ScreenKind;

namespace {
constexpr ui_widgets::MessageAnchor kCentre{drivers::kLcdHorRes / 2, drivers::kLcdVerRes / 2};

bool answering(BtState state) {
  return state != BtState::Starting && state != BtState::FirmwareMissing &&
         state != BtState::FirmwareOutdated && state != BtState::NotAnswering;
}
}  // namespace

std::string ScreenManager::bluetoothSettingsValue() const {
  if (bluetooth_ == nullptr) return {};
  switch (bluetooth_->state()) {
    case BtState::Starting:
      return "\xE2\x80\xA6";  // "…": nothing known yet.
    case BtState::FirmwareMissing:
      return "Firmware missing";
    case BtState::FirmwareOutdated:
      return "Firmware outdated";
    case BtState::NotAnswering:
      return "Not answering";
    case BtState::Off:
      return "Off";
    case BtState::Connected:
      return bluetooth_->pairedName();
    case BtState::Idle:
    case BtState::Connecting:
    default:
      return "Not connected";
  }
}

const char *ScreenManager::bluetoothEmptyText() const {
  if (bluetooth_ == nullptr) return "Nothing here";
  switch (bluetooth_->state()) {
    case BtState::FirmwareMissing:
      return "Bluetooth chip has no\nDrehklang firmware.\nscripts/flash-bt-mcu.sh";
    case BtState::FirmwareOutdated:
      return "Bluetooth chip firmware\nis outdated.\nscripts/flash-bt-mcu.sh";
    case BtState::NotAnswering:
      return "Bluetooth chip\nnot answering.";
    default:
      return "\xE2\x80\xA6";
  }
}

void ScreenManager::appendBluetoothRows(std::vector<std::pair<std::string, int>> &items) const {
  if (bluetooth_ == nullptr || !answering(bluetooth_->state())) return;
  items.emplace_back("Bluetooth", kBtToggleRow);
  if (!bluetooth_->enabled()) return;
  if (bluetooth_->paired()) items.emplace_back(bluetooth_->pairedName(), kBtDeviceRow);
  items.emplace_back("Find headphones", kBtFindRow);
  if (bluetooth_->paired()) items.emplace_back("Forget headphones", kBtForgetRow);
}

std::string ScreenManager::bluetoothRowValue(int itemId) const {
  if (bluetooth_ == nullptr) return {};
  if (itemId == kBtToggleRow) return bluetooth_->enabled() ? "On" : "Off";
  if (itemId != kBtDeviceRow) return {};
  switch (bluetooth_->state()) {
    case BtState::Connected:
      return "Connected";
    case BtState::Connecting:
      return "Connecting\xE2\x80\xA6";
    default:
      return "Not in range";
  }
}

void ScreenManager::onBluetoothRow(int itemId) {
  if (bluetooth_ == nullptr) return;
  switch (itemId) {
    case kBtToggleRow:
      bluetooth_->setEnabled(!bluetooth_->enabled());
      break;
    case kBtDeviceRow:
      if (bluetooth_->state() != BtState::Connected) bluetooth_->reconnect(millis());
      break;
    case kBtFindRow:
      openBluetoothSearch();
      return;
    case kBtForgetRow:
      bluetooth_->forget();
      break;
    default:
      return;
  }
  render();
}

void ScreenManager::openBluetoothSearch() {
  tabs_.activeStack().push(Screen{ScreenKind::BluetoothSearch, {}});
  bluetooth_->startScan();
  btSearchOpen_ = true;
  shownBtScanning_ = true;
  render();
}

void ScreenManager::appendBluetoothSearchRows(
    std::vector<std::pair<std::string, int>> &items) const {
  if (bluetooth_ == nullptr) return;
  items.emplace_back("Search again", kBtSearchAgainItemId);
  const auto &results = bluetooth_->scanResults();
  for (size_t i = 0; i < results.size(); ++i) {
    items.emplace_back(results[i].name, static_cast<int>(i));
  }
}

std::string ScreenManager::bluetoothSearchCaption() const {
  if (bluetooth_ == nullptr || bluetooth_->scanning()) return "Searching\xE2\x80\xA6";
  return std::to_string(bluetooth_->scanResults().size()) + " found";
}

void ScreenManager::onBluetoothSearchRow(int itemId) {
  if (bluetooth_ == nullptr) return;
  if (itemId == kBtSearchAgainItemId) {
    // Greyed out by doing nothing while one runs: a second scan would
    // only restart the list under the finger.
    if (bluetooth_->scanning()) return;
    bluetooth_->startScan();
    shownBtScanning_ = true;
    render();
    return;
  }
  const auto &results = bluetooth_->scanResults();
  if (itemId < 0 || static_cast<size_t>(itemId) >= results.size()) return;
  const auto entry = results[static_cast<size_t>(itemId)];
  bluetooth_->stopScan();
  bluetooth_->pair(entry, millis());
  btSearchOpen_ = false;
  tabs_.activeStack().pop();
  render();
  messages_.show(("Connecting to " + entry.name).c_str(), kCentre, millis(),
                 ui_widgets::MessageArea::kDefaultDurationMs, messaging::MessageScope::System);
}

void ScreenManager::showBluetoothEvent(BtEvent event, uint32_t nowMs) {
  std::string text;
  switch (event) {
    case BtEvent::Connected:
      text = "Headphones connected";
      break;
    case BtEvent::Disconnected:
      text = "Headphones disconnected";
      break;
    case BtEvent::PairFailed:
      text = "Could not connect to " + bluetooth_->pairingName();
      break;
    default:
      onHeadphoneButton(event);
      return;
  }
  const auto anchor = tabs_.activeStack().current().kind == ScreenKind::NowPlaying
                          ? kNowPlayingMessageAnchor
                          : kCentre;
  messages_.show(text.c_str(), anchor, nowMs, ui_widgets::MessageArea::kDefaultDurationMs,
                 messaging::MessageScope::System);
}

void ScreenManager::onHeadphoneButton(BtEvent event) {
  const ScreenKind kind = tabs_.activeStack().current().kind;
  bluetooth::ButtonContext context;
  context.inGame = kind == ScreenKind::TableTennis || kind == ScreenKind::Gravity;
  context.onTones = kind == ScreenKind::ToneGenerator && toneSession_ != nullptr;
  context.toneRunning = toneSession_ != nullptr && toneSession_->running();
  context.hasTrack = playback_.hasQueue();
  context.playing = playback_.state() == playback::PlaybackState::Playing;
  switch (bluetooth::actionFor(event, context)) {
    case bluetooth::ButtonAction::TogglePlayer:
      playback_.togglePlayPause(millis());
      render();
      break;
    case bluetooth::ButtonAction::StartTone:
      toneSession_->start();
      break;
    case bluetooth::ButtonAction::StopTone:
      toneSession_->stop();
      break;
    case bluetooth::ButtonAction::None:
      break;
  }
}

// A press must not lose its object to a redraw: LVGL would never deliver
// the release (see applyCaptionChip()).
bool ScreenManager::touchHeld() const {
  lv_indev_t *indev = lv_indev_get_next(nullptr);
  return indev != nullptr && indev->proc.state == LV_INDEV_STATE_PRESSED;
}

void ScreenManager::tickBluetooth(uint32_t nowMs) {
  if (bluetooth_ == nullptr) return;
  while (auto event = bluetooth_->takeEvent()) showBluetoothEvent(*event, nowMs);

  const ScreenKind kind = tabs_.activeStack().current().kind;
  if (btSearchOpen_ && kind != ScreenKind::BluetoothSearch) {
    bluetooth_->stopScan();
    btSearchOpen_ = false;
  }
  if (kind == ScreenKind::BluetoothSearch && shownBtScanning_ && !bluetooth_->scanning()) {
    shownBtScanning_ = false;
    if (bluetooth_->scanResults().empty()) {
      messages_.show("None found. Pairing mode on?", kCentre, nowMs);
    }
  }

  if (bluetooth_->revision() == shownBtRevision_) return;
  const bool showsBluetooth = kind == ScreenKind::Settings || kind == ScreenKind::Bluetooth ||
                              kind == ScreenKind::BluetoothSearch;
  if (!showsBluetooth) {
    shownBtRevision_ = bluetooth_->revision();
    return;
  }
  if (renderedKind_ != kind || touchHeld()) return;
  shownBtRevision_ = bluetooth_->revision();
  render();
}

}  // namespace drehklang::ui
```

Check two names against the code before compiling. `kDefaultDurationMs` is the `MessageArea::show` default (`grep -n kDefaultDurationMs lib/ui-widgets/MessageArea.h`). `kNowPlayingMessageAnchor` is a static member of `ScreenManager`. If either differs, use the actual name. `indev->proc.state` is LVGL 8's field; it is reachable because `lv_conf.h` exposes the struct.

- [ ] **Step 6: Lock-screen glyph.** In `lib/ui/BatteryIndicator.h`, add a public method after `setLocked`:

```cpp
  // A quiet Bluetooth glyph beside the battery on the lock screen while
  // headphones are connected (ADR 0027) -- status shown where it is looked
  // for, nowhere else (ux-guidelines §6).
  void setBluetoothConnected(bool connected) {
    if (connected == bluetooth_) return;
    bluetooth_ = connected;
    render();
  }
```

Add the member `bool bluetooth_ = false;`. In `render()`, change the two locked texts:

```cpp
      lv_label_set_text(label_, bluetooth_ ? LV_SYMBOL_BLUETOOTH "  " LV_SYMBOL_CHARGE "  Charging"
                                           : LV_SYMBOL_CHARGE "  Charging");
```

and

```cpp
    char text[32];
    snprintf(text, sizeof(text), "%s%s  %d%%", locked_ && bluetooth_ ? LV_SYMBOL_BLUETOOTH "  " : "",
             symbol, percent);
```

`LV_SYMBOL_BLUETOOTH` (U+F293, 62099) is already in the text fonts' Font Awesome range (`scripts/generate-text-fonts.sh`), so no font is regenerated.

- [ ] **Step 7: Wire it in `src/main.cpp`.** Next to `g_screenManager.setToneSession(g_toneSession);`:

```cpp
    g_screenManager.setBluetooth(g_btController);
```

In `loop()`, after the `g_btTap.setForwarding(...)` line from Task 7:

```cpp
  g_screenManager.tickBluetooth(now);
  g_batteryIndicator.setBluetoothConnected(g_btController.forwardAudio());
```

- [ ] **Step 8: Build and test**

Run: `pio run -e esp32-s3 && pio test -e native`
Expected: SUCCESS and PASS. `test_navigation` and `test_resume` must still pass: the new kinds are appended past `NowPlaying`, so resume records drop them.

- [ ] **Step 9: Review the UI against huepattl-rams-design.** Load the `huepattl-rams-design` skill and run its review (`references/review.md`) on the two Bluetooth screens and the lock-screen glyph, with `docs/design/ux-guidelines.md` taking precedence. Fix what it finds, then rebuild.

- [ ] **Step 10: Commit**

```bash
git add lib/navigation/ScreenId.h lib/ui src/main.cpp
git commit -m "Add Settings > Bluetooth, its search, and the headphone button

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: ADR 0027, design guidelines, credits

**Files:**
- Create: `docs/adr/0027-bluetooth-headphones.md`
- Modify: `docs/design/ux-guidelines.md` (§5 Settings bullet, §6 status bullet)
- Modify: `lib/about/Credits.h`, `THIRD-PARTY.md`
- Modify: `docs/adr/0026-arduino-esp32-3-and-upstream-audioi2s.md` (roadmap item 1: "Done in ADR 0027")
- Test: `test/test_credits` (existing)

- [ ] **Step 1: Write ADR 0027.** Follow the shape of ADR 0026: Status, Context, Decision, Consequences. Content, each point taken from the spec and this plan:

  - **Status:** Accepted, 2026-10-05. Amends ADR 0026: Bluetooth is a tap, not a DAC owner.
  - **Context:**
    - Only the U4WDH has Classic BT.
    - The chips share a UART with no flow control.
    - The user wanted everything audible on the headphones, the jack in parallel, one volume, Play/Pause only, and one paired device.
  - **Decision:**
    - Bluetooth is a tap behind the DAC owner. The section states why: the same hook work and the same RAM, and the DAC paces the S3, so the link needs no back-pressure.
    - The link: 3 Mbaud on GPIO48/38, the RX threshold of 64 with the spike's numbers, and the packet format.
    - The U4WDH resamples adaptively to 44.1 kHz, and Bluedroid encodes SBC.
    - The `bt/` sub-project, and why it is a project of its own.
    - The UI: Settings > Bluetooth and the lock-screen glyph.
  - **Consequences:**
    - The jack runs ~150 ms ahead of the headphones.
    - The internal RAM the S3 side uses.
    - A U4WDH with the factory image shows "Firmware missing".
    - The legal check before the first binary (SBC, the Bluetooth trademark and SIG qualification).
    - No low-pass before downsampling, so a 96 kHz file can alias.
    - An underrun is also counted every time the S3 pauses.
  - Leave a short "Verified on the device" section for Task 11 to fill.

- [ ] **Step 2: Update ux-guidelines.** In §5's Settings bullet, after the About sentence, add:

```markdown
  Bluetooth (ADR 0027) is a row like the others, ending in its state or
  the connected headphones' name; its screen is a list -- the switch, the
  paired headphones with their state, Find headphones, Forget headphones
  -- and when the Bluetooth chip has no Drehklang firmware it says so
  instead of offering a switch that would do nothing.
```

In §6, after the "Status is shown only when it matters" bullet, add:

```markdown
- **Bluetooth status follows the battery's rule.** Connecting and
  disconnecting each show a message; on the lock screen a quiet
  Bluetooth glyph sits beside the battery while headphones are
  connected. There is no permanent icon anywhere else (ADR 0027).
```

- [ ] **Step 3: Credit Bluedroid.** In `lib/about/Credits.h`, after the ESP-IDF entry:

```cpp
    {"Bluedroid", "", "Bluetooth and SBC on the second chip, inside ESP-IDF", "Apache-2.0",
     "github.com/espressif/esp-idf", nullptr, nullptr},
```

In `THIRD-PARTY.md`'s table, after the ESP-IDF row:

```markdown
| Bluedroid, bundled with ESP-IDF | Bluetooth stack and SBC encoder of the ESP32-U4WDH firmware (`bt/`) | Apache-2.0 | |
```

The table column must contain the credit name `Bluedroid` exactly; check `test_credits` for how it matches (`grep -n "THIRD-PARTY" test/test_credits/test_credits.cpp`).

- [ ] **Step 4: Run the credits test**

Run: `pio test -e native -f test_credits`
Expected: PASS.

- [ ] **Step 5: Mark ADR 0026's roadmap item.** Under "Where this leaves the roadmap", item 1, append: "Designed and built in ADR 0027."

- [ ] **Step 6: Commit**

```bash
git add docs lib/about/Credits.h THIRD-PARTY.md
git commit -m "Record Bluetooth headphones in ADR 0027 and the design guidelines

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Verify on the device

Needs the board, real headphones and the person holding them. Ask before each step that needs a hand (turning the plug, pairing mode, listening). Record the results in ADR 0027's "Verified on the device" section, in the order found, and commit.

- [ ] **Step 0: Jack without the factory image.** Flash the S3 (`scripts/flash-primary-mcu.sh`) with the U4WDH on the Drehklang BT firmware. Play a track and ask whether the jack plays. If it is silent, the factory image drove XSMT (U4WDH IO32). In that case, add `pinMode(32, OUTPUT); digitalWrite(32, HIGH);` at the top of `bt/src/main.cpp`'s `setup()` with a comment, reflash, re-check, and note it in device.md.
- [ ] **Step 1: Handshake.** Send `BT` over serial: state 4 (Off) or 5 (Idle). Then flash the factory image to the U4WDH (`hardware-backups/restore.sh secondary`). After a reboot, Settings shows `Firmware missing`. Reflash the BT firmware.
- [ ] **Step 2: Pair.** Turn Bluetooth on, open Find headphones with the headphones in pairing mode, and tap them. Check that `Connecting to …` and then `Headphones connected` appear.
- [ ] **Step 3: 60 minutes of music** with the jack connected. Send `BT` every ~10 minutes. U4WDH underruns must not grow after the start except at pauses, `crc` and `lost` must stay at 0, and the tap's `dropped` must stay at 0. Ask whether anything was heard dropping out.
- [ ] **Step 4: Rates.** Play a 48 kHz file and a 44.1 kHz one, open the tone generator (48 kHz) and play a game (22.05 kHz). Ask whether each plays at the right pitch on the headphones.
- [ ] **Step 5: Reconnect.** Switch the headphones off and on. `Headphones disconnected` should appear, then within ~10 s `Headphones connected`, with music still on the jack in between.
- [ ] **Step 6: Button.** Play/Pause on the headphones in Now Playing, in Tones and in a game.
- [ ] **Step 7: Heap.** `INFO` while playing over Bluetooth. Note internal free heap against ADR 0026's ~31 KB.
- [ ] **Step 8: Commit** ADR 0027's verification notes (and any fix found on the way, each in its own commit with its own test where it is host-testable).

---

## Self-review notes

- **Spec coverage.** Every spec section is covered:
  - tap: Tasks 5 and 7
  - protocol: Task 2
  - resampler: Task 4
  - controller: Task 6
  - U4WDH firmware: Task 8
  - UI and status: Task 9
  - flash script: Task 8
  - docs and credits: Tasks 1 and 10
  - device verification: Task 11
  - legal check before release: noted in ADR 0027, Task 10
- **Deliberate changes from the spec,** made in Task 1:
  - rate as a u32 instead of a code
  - Play and Pause instead of a toggle
  - `Starting` state, scanning as a flag
  - plain `On` value
  - the empty-search message
  - swapped pins
- **Not covered by host tests:**
  - UART and Bluedroid glue: device only, in Task 11
  - LVGL screens: build, then the person holding the device checks them in Task 11, since `SCREENSHOT` is unreliable (AGENTS.md)
