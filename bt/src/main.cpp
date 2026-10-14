// Drehklang's Bluetooth firmware for the ESP32-U4WDH (ADR 0027). Talks to
// the S3 over the shared UART, and to headphones as an A2DP source.
// Serial (UART0, the CH340) is for logs only -- and only from loop().

#include <Arduino.h>

#include <cstdarg>
#include <cstring>

#include "BtSource.h"
#include "ConnectPlan.h"
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
bt::PeerStore g_peers;
btlink::ConnectPlan g_plan;
// Whom the link is with, or is being made with; meaningless while Idle.
btlink::Address g_target{};

// IO32 drives the PCM5100A's XSMT (soft mute, active low). Left floating,
// the DAC stays muted and the jack is silent -- the factory image drove it
// high (found on the device 2026-10-05: no sound with this firmware, with
// either S3 build).
constexpr int kDacUnmutePin = 32;

constexpr uint32_t kStateIntervalMs = 1000;
constexpr uint32_t kStatsIntervalMs = 5000;
// A connection attempt Bluedroid never reports back on is given up after
// this, so the retry can start again (found 2026-10-05: a pairing that
// stayed "Connecting" until the chip restarted). Well past Bluedroid's own
// timeouts (authentication takes up to ~30 s): at 20 s it cut into a
// pairing still in progress, and the second connect left A2DP stuck.
constexpr uint32_t kConnectTimeoutMs = 60000;
// After a disconnect, before connecting again ourselves. Connecting in the
// same moment, while the old link was still being torn down, left A2DP
// stuck in "connecting" after a failed pairing (seen on the device
// 2026-10-06 with Marshall Major V). The pause also leaves room for
// headphones switched back on to connect to us themselves.
constexpr uint32_t kAfterDisconnectMs = 3000;
// Headphones that come back by themselves (their baseband link up while we
// sit idle) set up A2DP next. A connect of ours in between collided with
// theirs and hung in "connecting" for 28 s (BT log, 2026-10-14).
constexpr uint32_t kAfterTheirLinkMs = 8000;
// After removing a bond, before pairing afresh. Bluedroid removes it
// asynchronously (GAP event 14); the connect used to start in the same
// millisecond, and every such first pairing sat 30 s in authentication
// before failing (BT log, 2026-10-14). Whether that is the cause is not
// proven; the wait costs nothing.
constexpr uint32_t kAfterBondRemovalMs = 1000;

bool g_enabled = false;
uint32_t g_lastStateMs = 0;
uint32_t g_lastStatsMs = 0;
// When we may next connect to the peer ourselves.
uint32_t g_nextConnectMs = 0;

bool linkIdle() { return g_source.link() == btlink::LinkState::Idle; }

std::string nameOf(const btlink::Address &address) {
  const int at = g_peers.list().indexOf(address);
  return at < 0 ? std::string("?") : g_peers.list().peers()[static_cast<size_t>(at)].name;
}

void sendState() {
  btlink::StateMessage m;
  m.link = g_enabled ? g_source.link() : btlink::LinkState::Off;
  m.scanning = g_source.scanning();
  m.peers = g_peers.list().peers();
  m.active = linkIdle() ? -1 : static_cast<int8_t>(g_peers.list().indexOf(g_target));
  uint8_t payload[btlink::kMaxPayload];
  g_link.send(btlink::PacketType::State, payload,
              static_cast<uint16_t>(btlink::encodeState(m, payload)));
}

// A line for this chip's serial log and, as a LOG packet, for the S3's
// Bluetooth log on the SD card (2026-10-13). The S3 drops control packets
// over 64 bytes, so the line is cut there.
void report(const char *format, ...) {
  char line[64];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length < 0) return;
  Serial.printf("[bt] %s\n", line);
  g_link.send(btlink::PacketType::Log, reinterpret_cast<const uint8_t *>(line),
              static_cast<uint16_t>(strnlen(line, sizeof(line))));
}

void sendStats() {
  btlink::StatsMessage m{g_stream.underruns(), g_link.crcErrors(), g_link.lostPackets()};
  uint8_t payload[12];
  g_link.send(btlink::PacketType::Stats, payload,
              static_cast<uint16_t>(btlink::encodeStats(m, payload)));
}

// Not before `delayMs` from now. Compared as a signed difference, so it
// holds across millis() wrapping.
void postponeConnect(uint32_t delayMs) {
  const uint32_t at = millis() + delayMs;
  if (static_cast<int32_t>(at - g_nextConnectMs) > 0) g_nextConnectMs = at;
}

// The next remembered headphones in turn, or the ones the user picked
// (btlink::ConnectPlan).
void connectNext(uint32_t nowMs) {
  const auto attempt = g_plan.next(g_peers.list(), nowMs);
  if (!attempt) return;
  g_nextConnectMs = nowMs + attempt->pauseMs;
  g_target = attempt->address;
  report("connecting to %s", nameOf(g_target).c_str());
  g_source.connect(g_target);
}

// Over to the headphones the user asked for. Whatever link there is goes
// first, and the connect waits until it is gone: connecting into a link
// still being torn down left A2DP stuck in "connecting" -- after a failed
// pairing (2026-10-06) and on every switch to other headphones (BT log,
// 2026-10-14, AirPods and Major V).
void switchTo(const btlink::Address &address, uint32_t settleMs) {
  const uint32_t now = millis();
  g_plan.want(address, now);
  // Already with them, or on the way: a second tap changes nothing.
  if (!linkIdle() && g_target == address) return;
  g_nextConnectMs = now + settleMs;
  if (g_source.link() == btlink::LinkState::Connected) {
    report("switching to %s", nameOf(address).c_str());
    // Its DISCONNECTED postpones the connect by kAfterDisconnectMs.
    g_source.disconnect();
  } else if (g_source.link() == btlink::LinkState::Connecting) {
    report("switching to %s", nameOf(address).c_str());
    g_source.cancelConnect();
    postponeConnect(kAfterDisconnectMs);
  }
}

void onPair(const btlink::Packet &packet) {
  btlink::PairMessage m;
  if (!btlink::decodePair(packet, m)) return;
  g_source.stopScan();
  if (const auto evicted = g_peers.list().add(m.address, m.name)) {
    g_plan.forget(evicted->address);
    g_source.removeBond(evicted->address);
  }
  g_peers.save();
  // Pairing from the search always pairs afresh. Headphones in pairing
  // mode have dropped their old key; using ours made authentication fail
  // after 30 s (found 2026-10-05 with Marshall Major V).
  g_source.removeBond(m.address);
  if (g_enabled) switchTo(m.address, kAfterBondRemovalMs);
}

// Remembered headphones, picked in Settings: their bond is kept.
void onSelect(const btlink::Packet &packet) {
  btlink::Address address{};
  if (!btlink::decodeAddress(packet, btlink::PacketType::Select, address)) return;
  if (g_peers.list().indexOf(address) < 0) return;
  if (g_peers.list().touch(address)) g_peers.save();
  if (g_enabled) switchTo(address, 0);
}

void onForget(const btlink::Packet &packet) {
  btlink::Address address{};
  if (!btlink::decodeAddress(packet, btlink::PacketType::Forget, address)) return;
  if (!linkIdle() && g_target == address) {
    g_source.cancelConnect();
    g_source.disconnect();
  }
  g_plan.forget(address);
  g_source.removeBond(address);
  if (g_peers.list().remove(address)) g_peers.save();
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
      } else if (linkIdle()) {
        connectNext(nowMs);
      }
      break;
    case btlink::PacketType::ScanStart:
      if (g_enabled) {
        const bool started = g_source.startScan();
        report("scan %s (link %d)", started ? "started" : "refused",
               static_cast<int>(g_source.link()));
      }
      break;
    case btlink::PacketType::ScanStop:
      g_source.stopScan();
      break;
    case btlink::PacketType::Pair:
      onPair(packet);
      break;
    case btlink::PacketType::Select:
      onSelect(packet);
      break;
    case btlink::PacketType::Forget:
      onForget(packet);
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
      // Whoever it is -- headphones also come back by themselves -- they are
      // now the most recently used.
      g_target = event.address;
      if (g_peers.list().touch(g_target)) g_peers.save();
      g_plan.connected();
      report("connected to %s", nameOf(g_target).c_str());
      sendState();
      break;
    case bt::BtSourceEvent::Kind::Disconnected:
      report("disconnected");
      postponeConnect(kAfterDisconnectMs);
      sendState();
      break;
    case bt::BtSourceEvent::Kind::AclUp:
      if (g_source.link() == btlink::LinkState::Idle) {
        report("they came back; waiting before connecting");
        postponeConnect(kAfterTheirLinkMs);
      }
      break;
    case bt::BtSourceEvent::Kind::Log:
      report("%s", event.text);
      break;
  }
}

}  // namespace

void setup() {
  pinMode(kDacUnmutePin, OUTPUT);
  digitalWrite(kDacUnmutePin, HIGH);
  Serial.begin(115200);
  g_peers.begin();
  g_link.begin();
  g_source.begin();
  report("started, protocol %u, %u remembered", btlink::kProtocolVersion,
         static_cast<unsigned>(g_peers.list().peers().size()));
  for (const auto &peer : g_peers.list().peers()) report("remembered: %s", peer.name.c_str());
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
  // millis(), not `now`: a connect started in this very loop() is newer
  // than `now`, and the difference would wrap to a huge value.
  if (g_source.link() == btlink::LinkState::Connecting &&
      millis() - g_source.connectingSinceMs() >= kConnectTimeoutMs) {
    g_source.abandonConnect();
    postponeConnect(kAfterDisconnectMs);
  }
  if (g_enabled && linkIdle() && static_cast<int32_t>(now - g_nextConnectMs) >= 0) {
    connectNext(now);
  }
  delay(5);
}
