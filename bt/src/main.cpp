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

// IO32 drives the PCM5100A's XSMT (soft mute, active low). Left floating,
// the DAC stays muted and the jack is silent -- the factory image drove it
// high (found on the device 2026-10-05: no sound with this firmware, with
// either S3 build).
constexpr int kDacUnmutePin = 32;

constexpr uint32_t kStateIntervalMs = 1000;
constexpr uint32_t kStatsIntervalMs = 5000;
constexpr uint32_t kReconnectIntervalMs = 10000;
// A connection attempt Bluedroid never reports back on is given up after
// this, so the retry can start again (found 2026-10-05: a pairing that
// stayed "Connecting" until the chip restarted). Well past Bluedroid's own
// timeouts (authentication takes up to ~30 s): at 20 s it cut into a
// pairing still in progress, and the second connect left A2DP stuck.
constexpr uint32_t kConnectTimeoutMs = 60000;

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
  Serial.printf("[bt] connecting to %s\n", g_peer.name().c_str());
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
  // Pairing from the search always pairs afresh. Headphones in pairing
  // mode have dropped their old key; using ours made authentication fail
  // after 30 s (found 2026-10-05 with Marshall Major V).
  g_source.removeBond(m.address);
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
      if (g_enabled) {
        const bool started = g_source.startScan();
        Serial.printf("[bt] scan %s (link %d)\n", started ? "started" : "refused",
                      static_cast<int>(g_source.link()));
      }
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
    case bt::BtSourceEvent::Kind::Log:
      Serial.printf("[bt] %s\n", event.text);
      break;
  }
}

}  // namespace

void setup() {
  pinMode(kDacUnmutePin, OUTPUT);
  digitalWrite(kDacUnmutePin, HIGH);
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
  // millis(), not `now`: a connect started in this very loop() is newer
  // than `now`, and the difference would wrap to a huge value.
  if (g_source.link() == btlink::LinkState::Connecting &&
      millis() - g_source.connectingSinceMs() >= kConnectTimeoutMs) {
    g_source.abandonConnect();
  }
  if (g_enabled && g_peer.has() && g_source.link() == btlink::LinkState::Idle &&
      now - g_lastConnectMs >= kReconnectIntervalMs) {
    connectToPeer(now);
  }
  delay(5);
}
