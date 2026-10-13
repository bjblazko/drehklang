#pragma once

#include <Wire.h>

#include <cstdint>

#include "HapticFeedback.h"

namespace drehklang::drivers {

// The DRV2605L on the touch controller's I2C bus (device.md). It drives a
// linear resonant actuator (LRA): in its factory ERM mode nothing was felt
// at all (checked by hand, 2026-10-13). Open loop, so no auto-calibration
// is needed, and the LRA library's effects (TI's table, library 6). Wire
// must already be up (Cst816Driver::begin()); every call comes from the
// loop task, like the touch driver's.
class Drv2605Driver : public haptics::HapticSink {
 public:
  static constexpr uint8_t kAddress = 0x5A;

  // False when nothing answers at the address.
  bool begin() {
    if (read(kStatus) < 0) return false;
    write(kMode, kInternalTrigger);  // Also leaves standby.
    write(kRealTimeInput, 0);
    const int feedback = read(kFeedback);
    write(kFeedback, static_cast<uint8_t>((feedback < 0 ? 0 : feedback) | kLraMode));
    const int control3 = read(kControl3);
    write(kControl3, static_cast<uint8_t>((control3 < 0 ? 0 : control3) | kLraOpenLoop));
    write(kLibrary, kLraLibrary);
    ready_ = true;
    return true;
  }

  void play(uint8_t effect) override {
    if (!ready_) return;
    write(kSequence, effect);
    write(kSequence + 1, 0);  // End of the sequence.
    write(kGo, 1);
  }

 private:
  static constexpr uint8_t kStatus = 0x00;
  static constexpr uint8_t kMode = 0x01;
  static constexpr uint8_t kRealTimeInput = 0x02;
  static constexpr uint8_t kLibrary = 0x03;
  static constexpr uint8_t kSequence = 0x04;
  static constexpr uint8_t kGo = 0x0C;
  static constexpr uint8_t kFeedback = 0x1A;
  static constexpr uint8_t kControl3 = 0x1D;
  static constexpr uint8_t kInternalTrigger = 0x00;
  static constexpr uint8_t kLraMode = 0x80;
  static constexpr uint8_t kLraOpenLoop = 0x01;
  static constexpr uint8_t kLraLibrary = 6;

  int read(uint8_t reg) {
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(kAddress, static_cast<uint8_t>(1)) != 1) return -1;
    return Wire.read();
  }

  void write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

  bool ready_ = false;
};

}  // namespace drehklang::drivers
