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
