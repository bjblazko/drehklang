#include "BtLink.h"

#include <esp_heap_caps.h>
#include <freertos/task.h>

#include <cstring>

namespace drehklang::drivers {

namespace {
constexpr uart_port_t kPort = UART_NUM_1;
constexpr int kTxPin = 48;
constexpr int kRxPin = 38;
constexpr int kRxBufferBytes = 1024;
constexpr int kTxBufferBytes = 2048;
// At the driver's default (120 of 128 bytes) the ESP32 side lost bytes at
// 3 Mbaud; 64 leaves the interrupt ~210 us (spike, 2026-10-05).
constexpr uint8_t kRxFullThreshold = 64;
constexpr size_t kQueueDepth = 16;
constexpr uint32_t kTaskStackBytes = 3072;
constexpr UBaseType_t kTaskPriority = 3;
constexpr BaseType_t kLinkCore = 0;
constexpr uint32_t kPacketSamples = btlink::kAudioFramesPerPacket * 2;
}  // namespace

void BtLink::begin() {
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

  pcm_ = static_cast<int16_t *>(heap_caps_malloc(kPacketSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  payload_ = static_cast<uint8_t *>(heap_caps_malloc(btlink::kAudioPayloadBytes, MALLOC_CAP_SPIRAM));
  frame_ = static_cast<uint8_t *>(heap_caps_malloc(btlink::kMaxPacketBytes, MALLOC_CAP_SPIRAM));
  toLink_ = xQueueCreate(kQueueDepth, sizeof(Control));
  fromLink_ = xQueueCreate(kQueueDepth, sizeof(Control));
  xTaskCreatePinnedToCore(&BtLink::taskTrampoline, "btlink", kTaskStackBytes, this,
                          kTaskPriority, nullptr, kLinkCore);
}

void BtLink::send(btlink::PacketType type, const uint8_t *payload, uint16_t length) {
  if (toLink_ == nullptr || length > kMaxControlPayload) return;
  Control control{type, static_cast<uint8_t>(length), {}};
  if (length > 0) std::memcpy(control.payload, payload, length);
  xQueueSend(toLink_, &control, 0);
}

bool BtLink::receive(btlink::Packet &out) {
  Control control;
  if (fromLink_ == nullptr || xQueueReceive(fromLink_, &control, 0) != pdTRUE) return false;
  out.type = control.type;
  out.length = control.length;
  std::memcpy(out.payload.data(), control.payload, control.length);
  return true;
}

void BtLink::taskTrampoline(void *self) { static_cast<BtLink *>(self)->taskLoop(); }

void BtLink::taskLoop() {
  for (;;) {
    // All three every round: one finding work must not starve the others.
    const bool received = pumpReceived();
    const bool controlled = pumpControl();
    const bool streamed = pumpAudio();
    if (!received && !controlled && !streamed) vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool BtLink::pumpReceived() {
  uint8_t bytes[128];
  const int n = uart_read_bytes(kPort, bytes, sizeof(bytes), 0);
  for (int i = 0; i < n; ++i) {
    if (!reader_.feed(bytes[i])) continue;
    const btlink::Packet &packet = reader_.packet();
    if (packet.length > kMaxControlPayload) continue;
    Control control{packet.type, static_cast<uint8_t>(packet.length), {}};
    std::memcpy(control.payload, packet.payload.data(), packet.length);
    xQueueSend(fromLink_, &control, 0);
  }
  return n > 0;
}

bool BtLink::pumpControl() {
  Control control;
  if (xQueueReceive(toLink_, &control, 0) != pdTRUE) return false;
  write(control.type, control.payload, control.length);
  return true;
}

bool BtLink::pumpAudio() {
  if (!tap_.forwarding()) {
    ring_.clear();
    return false;
  }
  if (ring_.available() < kPacketSamples) return false;
  ring_.read(pcm_, kPacketSamples);
  btlink::putU32(payload_, tap_.rate());
  for (uint32_t i = 0; i < kPacketSamples; ++i) {
    const auto s = static_cast<uint16_t>(pcm_[i]);
    payload_[btlink::kAudioHeaderBytes + i * 2] = static_cast<uint8_t>(s & 0xFF);
    payload_[btlink::kAudioHeaderBytes + i * 2 + 1] = static_cast<uint8_t>(s >> 8);
  }
  write(btlink::PacketType::Audio, payload_, btlink::kAudioPayloadBytes);
  return true;
}

// Blocks while the UART's TX buffer is full: at 3 Mbaud a packet drains in
// ~3.4 ms, which is what paces this task.
void BtLink::write(btlink::PacketType type, const uint8_t *payload, uint16_t length) {
  const size_t n = btlink::encodePacket(type, txSeq_++, payload, length, frame_);
  if (n > 0) uart_write_bytes(kPort, frame_, n);
}

}  // namespace drehklang::drivers
