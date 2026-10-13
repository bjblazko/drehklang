#pragma once

#include <Arduino.h>
#include <SD_MMC.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace drehklang::drivers {

// The Bluetooth log on the SD card (2026-10-13): what the link and the
// headphones did, so a drop-out in daily use can be read afterwards -- by
// USB drive, or with "BTLOG" over serial. There is no clock, so each line
// carries the boot number and the time since that boot:
//
//   #42 +01:23:45.678 acl disconnected: reason 0x13
//
// At kMaxBytes the file becomes bt.old.log, keeping the one before.
class BtLogFile {
 public:
  static constexpr const char *kPath = "/drehklang/bt.log";
  static constexpr const char *kOldPath = "/drehklang/bt.old.log";
  static constexpr size_t kMaxBytes = 512 * 1024;

  void setBoot(uint32_t boot) { boot_ = boot; }

  void append(const std::vector<std::string> &lines, uint32_t nowMs) {
    if (lines.empty()) return;
    rotateIfFull();
    fs::File file = SD_MMC.open(kPath, FILE_APPEND);
    if (!file) return;
    char stamp[40];
    std::snprintf(stamp, sizeof(stamp), "#%lu +%02lu:%02lu:%02lu.%03lu ",
                  static_cast<unsigned long>(boot_), static_cast<unsigned long>(nowMs / 3600000),
                  static_cast<unsigned long>(nowMs / 60000 % 60),
                  static_cast<unsigned long>(nowMs / 1000 % 60),
                  static_cast<unsigned long>(nowMs % 1000));
    for (const std::string &line : lines) {
      file.print(stamp);
      file.println(line.c_str());
    }
  }

  // The newest `bytes` of the log to serial, in chunks that wait for room
  // (AGENTS.md: a blocking Serial.write can hang the loop).
  void printTail(size_t bytes) {
    fs::File file = SD_MMC.open(kPath, FILE_READ);
    if (!file) {
      Serial.println("[btlog] empty");
      return;
    }
    const size_t size = file.size();
    file.seek(size > bytes ? size - bytes : 0);
    uint8_t chunk[256];
    const uint32_t startMs = millis();
    while (file.available() && millis() - startMs < 5000) {
      const size_t n = file.read(chunk, std::min<size_t>(sizeof(chunk), Serial.availableForWrite()));
      if (n == 0) {
        delay(1);
        continue;
      }
      Serial.write(chunk, n);
    }
    Serial.println("[btlog] end");
  }

 private:
  void rotateIfFull() {
    fs::File file = SD_MMC.open(kPath, FILE_READ);
    if (!file) return;
    const size_t size = file.size();
    file.close();
    if (size < kMaxBytes) return;
    SD_MMC.remove(kOldPath);
    SD_MMC.rename(kPath, kOldPath);
  }

  uint32_t boot_ = 0;
};

}  // namespace drehklang::drivers
