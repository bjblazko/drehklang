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
