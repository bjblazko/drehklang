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
