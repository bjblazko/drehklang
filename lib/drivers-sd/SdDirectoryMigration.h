#pragma once

#include <Arduino.h>
#include <SD_MMC.h>

namespace dialhard::drivers {

// The product was renamed from "knobify" to "DialHard" (2026-10-05), and
// with it the SD card's cache folder. Renaming the old folder keeps a
// card's indexes and covers, so the first boot after the rename needs no
// rescan. If both folders exist, the new one wins and the old one is left
// for the user to delete -- it is derived state either way (ADR 0004).
constexpr const char *kLegacyCacheDir = "/knobify";

inline void migrateLegacyCacheDir(const char *newDir) {
  if (!SD_MMC.exists(kLegacyCacheDir) || SD_MMC.exists(newDir)) return;
  if (SD_MMC.rename(kLegacyCacheDir, newDir)) {
    Serial.printf("[migrate] SD %s -> %s\n", kLegacyCacheDir, newDir);
  } else {
    Serial.println("[migrate] SD cache folder rename failed");
  }
}

}  // namespace dialhard::drivers
