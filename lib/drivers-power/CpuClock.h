#pragma once

#include <Arduino.h>

namespace drehklang::drivers {

// The CPU clock (2026-10-13). The prebuilt Arduino core has no power
// management (CONFIG_PM_ENABLE is off), so there is no automatic scaling:
// the clock is set by hand. 160 MHz carries music and the UI -- the audio
// decoder takes ~40 % of core 0 at 240 MHz -- and keeps APB at 80 MHz, so
// UART, I2S and the display's QSPI keep their clocks. Circuit, which fills
// core 0, asks for 240 while it owns the panel.
inline constexpr uint32_t kEconomyCpuMhz = 160;
inline constexpr uint32_t kFullCpuMhz = 240;

inline void setFullCpuSpeed(bool full) {
  setCpuFrequencyMhz(full ? kFullCpuMhz : kEconomyCpuMhz);
}

}  // namespace drehklang::drivers
