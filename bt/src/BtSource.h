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
