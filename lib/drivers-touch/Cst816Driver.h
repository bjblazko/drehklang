#pragma once

#include <Wire.h>

#include "TouchDriver.h"

namespace drehklang::drivers {

// Touch pins/address, per device.md's pinout. Shares the I2C bus with
// the (out-of-v1-scope) DRV2605 haptics driver.
constexpr int kTouchSdaPin = 11;
constexpr int kTouchSclPin = 12;
constexpr uint8_t kTouchI2cAddress = 0x15;
constexpr uint32_t kTouchI2cHz = 300 * 1000;

// CST816 touch controller, polled over I2C -- protocol ported from
// Waveshare's own official demo for this board (register 0x00, 7 bytes:
// byte[2] = touch point count, bytes[3-4] = 12-bit X, bytes[5-6] = 12-bit
// Y). This chip isn't interrupt-driven here (the demo doesn't use the
// INT pin either); poll() always reports the current state, and lets
// GestureRecognizer infer down/up transitions from the pressed sequence.
class Cst816Driver : public input::TouchDriver {
 public:
  // Arduino's Wire, not ESP-IDF's legacy driver/i2c.h: on ESP-IDF 5 the
  // core's own I2C layer links the new driver, and the two together abort
  // at boot ("i2c: CONFLICT! driver_ng ...", 2026-10-05).
  bool begin() {
    if (!Wire.begin(kTouchSdaPin, kTouchSclPin, kTouchI2cHz)) return false;
    uint8_t normalMode = 0x00;
    writeRegister(0x00, &normalMode, 1);
    return true;
  }

  bool poll(input::TouchSample &out) override {
    uint8_t data[7] = {0};
    readRegister(0x00, data, sizeof(data));
    bool touched = data[2] != 0;
    if (touched) {
      lastX_ = static_cast<int16_t>(((data[3] & 0x0F) << 8) | data[4]);
      lastY_ = static_cast<int16_t>(((data[5] & 0x0F) << 8) | data[6]);
    }
    out = input::TouchSample{lastX_, lastY_, touched};
    return true;
  }

  // Before the sleep timer's deep sleep (ADR 0015): the chip must keep
  // scanning and pull INT low on a touch, since that is what wakes the
  // ESP32. Register values from the CST816S datasheet (0xFA IrqCtl:
  // EnTouch | EnChange; 0xFE DisAutoSleep) -- unverified on this board.
  void armWakeOnTouch() {
    uint8_t irqOnTouch = 0x60;
    writeRegister(0xFA, &irqOnTouch, 1);
    uint8_t noAutoSleep = 0x01;
    writeRegister(0xFE, &noAutoSleep, 1);
  }

 private:
  void writeRegister(uint8_t reg, const uint8_t *data, size_t len) {
    Wire.beginTransmission(kTouchI2cAddress);
    Wire.write(reg);
    Wire.write(data, len);
    Wire.endTransmission();
  }

  // The chip NACKs while nothing touches it (device.md); `out` then stays
  // as the caller zeroed it, which reads as "not pressed".
  void readRegister(uint8_t reg, uint8_t *out, size_t len) {
    Wire.beginTransmission(kTouchI2cAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return;
    const size_t got = Wire.requestFrom(kTouchI2cAddress, len);
    for (size_t i = 0; i < got && i < len; ++i) out[i] = static_cast<uint8_t>(Wire.read());
  }

  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
};

}  // namespace drehklang::drivers
