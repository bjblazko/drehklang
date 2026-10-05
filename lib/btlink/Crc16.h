#pragma once

#include <cstddef>
#include <cstdint>

namespace drehklang::btlink {

// CRC-16/CCITT-FALSE (polynomial 0x1021, start 0xFFFF), the link's
// checksum (ADR 0027). "123456789" gives 0x29B1. Bitwise rather than a
// table: at 300 KB/s it costs the U4WDH about 4 % of one core.
inline uint16_t crc16(const uint8_t *data, size_t length, uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i] << 8);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace drehklang::btlink
