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
