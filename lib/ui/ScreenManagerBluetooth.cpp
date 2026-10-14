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
      return bluetooth_->connectedName();
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
  const auto &peers = bluetooth_->peers();
  shownBtPeers_.clear();
  for (size_t i = 0; i < peers.size(); ++i) {
    items.emplace_back(peers[i].name, kBtPeerRowBase + static_cast<int>(i));
    shownBtPeers_.push_back(peers[i].address);
  }
  items.emplace_back("Find headphones", kBtFindRow);
  if (!peers.empty()) items.emplace_back("Forget headphones", kBtForgetRow);
}

void ScreenManager::appendBluetoothForgetRows(
    std::vector<std::pair<std::string, int>> &items) const {
  if (bluetooth_ == nullptr) return;
  const auto &peers = bluetooth_->peers();
  shownBtPeers_.clear();
  for (size_t i = 0; i < peers.size(); ++i) {
    items.emplace_back(peers[i].name, static_cast<int>(i));
    shownBtPeers_.push_back(peers[i].address);
  }
}

std::string ScreenManager::bluetoothRowValue(int itemId) const {
  if (bluetooth_ == nullptr) return {};
  if (itemId == kBtToggleRow) return bluetooth_->enabled() ? "On" : "Off";
  const int index = itemId - kBtPeerRowBase;
  const auto &peers = bluetooth_->peers();
  if (index < 0 || static_cast<size_t>(index) >= peers.size()) return {};
  if (bluetooth_->state() == BtState::Connected && bluetooth_->activePeer() == index) {
    return "Connected";
  }
  // Only the headphones asked for: the others are tried in turn in the
  // background, and a value hopping between rows would say nothing.
  if (bluetooth_->pendingAddress() == peers[static_cast<size_t>(index)].address) {
    return "Connecting\xE2\x80\xA6";
  }
  return {};
}

void ScreenManager::onBluetoothRow(int itemId) {
  if (bluetooth_ == nullptr) return;
  switch (itemId) {
    case kBtToggleRow:
      bluetooth_->setEnabled(!bluetooth_->enabled());
      break;
    case kBtFindRow:
      openBluetoothSearch();
      return;
    case kBtForgetRow:
      // One pair goes at once, as before; of several, the user picks.
      if (bluetooth_->peers().size() == 1) {
        bluetooth_->forget(bluetooth_->peers()[0].address);
        break;
      }
      tabs_.activeStack().push(Screen{ScreenKind::BluetoothForget, {}});
      break;
    default: {
      const int index = itemId - kBtPeerRowBase;
      if (index < 0 || static_cast<size_t>(index) >= shownBtPeers_.size()) return;
      const bool connectedHere = bluetooth_->state() == BtState::Connected &&
                                 bluetooth_->activePeer() == index;
      if (!connectedHere) bluetooth_->select(shownBtPeers_[static_cast<size_t>(index)], millis());
      break;
    }
  }
  render();
}

void ScreenManager::onBluetoothForgetRow(int itemId) {
  if (bluetooth_ == nullptr || itemId < 0 ||
      static_cast<size_t>(itemId) >= shownBtPeers_.size()) {
    return;
  }
  bluetooth_->forget(shownBtPeers_[static_cast<size_t>(itemId)]);
  tabs_.activeStack().pop();
  render();
}

void ScreenManager::openBluetoothSearch() {
  tabs_.activeStack().push(Screen{ScreenKind::BluetoothSearch, {}});
  bluetooth_->startScan(millis());
  btSearchOpen_ = true;
  shownBtScanning_ = true;
  render();
}

void ScreenManager::appendBluetoothSearchRows(
    std::vector<std::pair<std::string, int>> &items) const {
  if (bluetooth_ == nullptr) return;
  items.emplace_back("Search again", kBtSearchAgainItemId);
  const auto &results = bluetooth_->scanResults();
  shownBtResults_.clear();
  for (size_t i = 0; i < results.size(); ++i) {
    items.emplace_back(results[i].name, static_cast<int>(i));
    shownBtResults_.push_back(results[i].address);
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
    bluetooth_->startScan(millis());
    shownBtScanning_ = true;
    render();
    return;
  }
  if (itemId < 0 || static_cast<size_t>(itemId) >= shownBtResults_.size()) return;
  const auto found = bluetooth_->resultFor(shownBtResults_[static_cast<size_t>(itemId)]);
  if (!found) return;
  const auto entry = *found;
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
  context.inGame = kind == ScreenKind::TableTennis || kind == ScreenKind::Gravity ||
                   kind == ScreenKind::Circuit || kind == ScreenKind::VideoPlayer ||
                   // The music would take I2S0 from the microphone or the
                   // recording (ADR 0032).
                   kind == ScreenKind::Dictaphone || kind == ScreenKind::Recording;
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
                              kind == ScreenKind::BluetoothSearch ||
                              kind == ScreenKind::BluetoothForget;
  if (!showsBluetooth) {
    shownBtRevision_ = bluetooth_->revision();
    return;
  }
  if (renderedKind_ != kind || touchHeld()) return;
  shownBtRevision_ = bluetooth_->revision();
  std::string view = bluetoothView(kind);
  if (view == shownBtView_) return;
  shownBtView_ = std::move(view);
  render();
}

std::string ScreenManager::bluetoothView(ScreenKind kind) const {
  std::vector<std::pair<std::string, int>> rows;
  std::string view;
  switch (kind) {
    case ScreenKind::Settings:
      return bluetoothSettingsValue();
    case ScreenKind::Bluetooth:
      appendBluetoothRows(rows);
      view = bluetoothEmptyText();
      for (const auto &row : rows) view += "\n" + row.first + "\t" + bluetoothRowValue(row.second);
      return view;
    case ScreenKind::BluetoothSearch:
      view = bluetoothSearchCaption();
      for (const auto &entry : bluetooth_->scanResults()) view += "\n" + entry.name;
      return view;
    case ScreenKind::BluetoothForget:
      for (const auto &peer : bluetooth_->peers()) view += "\n" + peer.name;
      return view;
    default:
      return {};
  }
}

}  // namespace drehklang::ui
