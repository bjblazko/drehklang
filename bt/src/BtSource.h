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
  // Log: a line for loop() to print (`text`) -- callbacks never print.
  enum class Kind : uint8_t { Found, Button, Connected, Disconnected, Log } kind;
  btlink::Address address{};
  int8_t rssi = 0;
  btlink::ButtonCode button = btlink::ButtonCode::Play;
  char name[btlink::kMaxNameBytes + 1] = {};
  char text[64] = {};
};

// ESP-IDF's Bluedroid as an A2DP source with an AVRCP target (ADR 0027).
// Bluedroid encodes SBC itself from the PCM the data callback renders out
// of the HeadphoneStream.
class BtSource {
 public:
  explicit BtSource(btaudio::HeadphoneStream &stream);
  void begin();
  bool nextEvent(BtSourceEvent &out);

  // False when Bluedroid refused to start discovery.
  bool startScan();
  void stopScan();
  // False when Bluedroid refused to start the connection; the link is then
  // Idle again, so the 10 s retry picks it up.
  bool connect(const btlink::Address &address);
  // A connection attempt that never reported back: Idle again, and the
  // attempt dropped in Bluedroid too. Call from
  // loop() when Connecting has lasted too long.
  void abandonConnect();
  // Since when the link has been Connecting, either side having started it.
  uint32_t connectingSinceMs() const { return connectingSince_.load(); }
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
  static void log(const char *format, ...);

  btaudio::HeadphoneStream &stream_;
  QueueHandle_t events_ = nullptr;
  std::atomic<btlink::LinkState> link_{btlink::LinkState::Idle};
  std::atomic<bool> scanning_{false};
  std::atomic<uint32_t> connectingSince_{0};
  // Whom the link is with, or is being made with: what disconnect() ends.
  btlink::Address peer_{};
};

}  // namespace drehklang::bt
