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
  // A first pairing can sit 30 s in its first authentication and then
  // succeed (BT log, 2026-10-14), so "Could not connect" waits past that.
  static constexpr uint32_t kPairTimeoutMs = 45000;
  // How long a fresh scan counts as running before the U4WDH confirms it:
  // its first STATE after SCAN_START still says not scanning.
  static constexpr uint32_t kScanStartGraceMs = 3000;
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
      ++revision_;
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
        onState(packet, nowMs);
        break;
      case btlink::PacketType::ScanResult:
        onScanResult(packet);
        break;
      case btlink::PacketType::Button:
        onButton(packet);
        break;
      case btlink::PacketType::Stats:
        btlink::decodeStats(packet, stats_);
        noteStats();
        break;
      case btlink::PacketType::Log:
        note(std::string(reinterpret_cast<const char *>(packet.payload.data()), packet.length));
        break;
      default:
        break;
    }
  }

  BtState state() const { return state_; }
  bool enabled() const { return enabled_; }

  // What happened on the link since the last call, for the Bluetooth log
  // on the SD card: the U4WDH's own lines, state changes, and new
  // underruns or losses. At most kMaxLogLines are kept between calls.
  std::vector<std::string> takeLog() {
    std::vector<std::string> lines;
    lines.swap(log_);
    return lines;
  }
  static constexpr size_t kMaxLogLines = 64;

  void setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    store_.setU8(kEnabledKey, on ? 1 : 0);
    ++revision_;
    if (answered_) sendEnable();
  }

  bool scanning() const { return scanning_; }
  const std::vector<ScanEntry> &scanResults() const { return results_; }

  void startScan(uint32_t nowMs) {
    results_.clear();
    setScanning(true);
    scanConfirmed_ = false;
    scanStartedMs_ = nowMs;
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
    startPending(entry.address, entry.name, nowMs);
  }

  // Remembered headphones' row: these, now, switching over from any others
  // (their bond is kept, unlike pair()).
  void select(const btlink::Address &address, uint32_t nowMs) {
    uint8_t payload[6];
    link_.send(btlink::PacketType::Select, payload,
               static_cast<uint16_t>(btlink::encodeAddress(address, payload)));
    std::string name;
    for (const auto &peer : peers_) {
      if (peer.address == address) name = peer.name;
    }
    startPending(address, name, nowMs);
  }

  void forget(const btlink::Address &address) {
    uint8_t payload[6];
    link_.send(btlink::PacketType::Forget, payload,
               static_cast<uint16_t>(btlink::encodeAddress(address, payload)));
  }

  // The remembered headphones, the most recently used first.
  const std::vector<btlink::Peer> &peers() const { return peers_; }
  // Which of them the link is with or being made with, -1 none.
  int activePeer() const { return active_; }
  // Empty unless connected.
  std::string connectedName() const {
    if (state_ != BtState::Connected || active_ < 0) return {};
    return peers_[static_cast<size_t>(active_)].name;
  }
  // The headphones a pair() or select() is still waiting for.
  std::optional<btlink::Address> pendingAddress() const {
    if (!pairing_) return std::nullopt;
    return pairingAddress_;
  }
  // Whom the last pair() or select() was for, for "Could not connect to ...".
  const std::string &pairingName() const { return pairingName_; }

  bool forwardAudio() const { return state_ == BtState::Connected; }

  // A scan result by address. The UI resolves a tap this way: the list
  // re-sorts as results arrive, so a row's position can change under the
  // finger.
  std::optional<ScanEntry> resultFor(const btlink::Address &address) const {
    for (const auto &entry : results_) {
      if (entry.address == address) return entry;
    }
    return std::nullopt;
  }

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
  void startPending(const btlink::Address &address, const std::string &name, uint32_t nowMs) {
    pairing_ = true;
    pairingSinceMs_ = nowMs;
    pairingAddress_ = address;
    pairingName_ = name;
    ++revision_;
  }

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

  void onState(const btlink::Packet &packet, uint32_t nowMs) {
    btlink::StateMessage m;
    if (!btlink::decodeState(packet, m)) return;
    // The U4WDH starts up off: after a restart of its own it has forgotten.
    if ((m.link == btlink::LinkState::Off) == enabled_) sendEnable();
    if (!samePeers(m.peers) || active_ != m.active) {
      peers_ = std::move(m.peers);
      active_ = m.active;
      ++revision_;
    }
    if (m.scanning) scanConfirmed_ = true;
    const bool starting = !scanConfirmed_ && scanning_ && nowMs - scanStartedMs_ < kScanStartGraceMs;
    if (!starting) setScanning(m.scanning);
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

  bool samePeers(const std::vector<btlink::Peer> &other) const {
    if (other.size() != peers_.size()) return false;
    for (size_t i = 0; i < other.size(); ++i) {
      if (other[i].address != peers_[i].address || other[i].name != peers_[i].name) return false;
    }
    return true;
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

  static const char *nameOf(BtState state) {
    switch (state) {
      case BtState::Starting: return "starting";
      case BtState::FirmwareMissing: return "firmware missing";
      case BtState::FirmwareOutdated: return "firmware outdated";
      case BtState::NotAnswering: return "not answering";
      case BtState::Off: return "off";
      case BtState::Idle: return "idle";
      case BtState::Connecting: return "connecting";
      case BtState::Connected: return "connected";
    }
    return "?";
  }

  void note(std::string line) {
    if (log_.size() >= kMaxLogLines) log_.erase(log_.begin());
    log_.push_back(std::move(line));
  }

  // Only what changed: the counters arrive every 5 s.
  void noteStats() {
    if (stats_.underruns == loggedStats_.underruns && stats_.lostPackets == loggedStats_.lostPackets &&
        stats_.crcErrors == loggedStats_.crcErrors) {
      return;
    }
    note("stats underruns " + std::to_string(stats_.underruns) + " lost " +
         std::to_string(stats_.lostPackets) + " crc " + std::to_string(stats_.crcErrors));
    loggedStats_ = stats_;
  }

  void setState(BtState next) {
    if (next == state_) return;
    note(std::string("state ") + nameOf(state_) + " -> " + nameOf(next));
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
  std::vector<std::string> log_;
  btlink::StatsMessage loggedStats_{};
  bool answered_ = false;
  bool scanning_ = false;
  bool scanConfirmed_ = true;
  bool pairing_ = false;
  std::vector<btlink::Peer> peers_;
  int active_ = -1;
  btlink::Address pairingAddress_{};
  std::string pairingName_;
  std::vector<ScanEntry> results_;
  std::deque<BtEvent> events_;
  btlink::StatsMessage stats_;
  uint32_t startedMs_ = 0;
  uint32_t lastHelloMs_ = 0;
  uint32_t lastPacketMs_ = 0;
  uint32_t pairingSinceMs_ = 0;
  uint32_t scanStartedMs_ = 0;
  uint32_t revision_ = 0;
};

}  // namespace drehklang::bluetooth
