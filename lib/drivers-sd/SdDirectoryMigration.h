#pragma once

#include <Arduino.h>
#include <SD_MMC.h>

namespace drehklang::drivers {

// The product was renamed from "knobify" via "DialHard" to "Drehklang"
// (2026-10-05), and with it the SD card's cache folder. Renaming the old
// folder keeps a card's indexes and covers, so the first boot after the
// rename needs no rescan. Newest first: only one folder is taken over, and
// the newest one is the freshest. Anything left over (or an old folder next
// to an existing new one) stays for the user to delete -- it is derived
// state either way (ADR 0004).
constexpr const char *kLegacyCacheDirs[] = {"/dialhard", "/knobify"};

inline void migrateLegacyCacheDir(const char *newDir) {
  if (SD_MMC.exists(newDir)) return;
  for (const char *oldDir : kLegacyCacheDirs) {
    if (!SD_MMC.exists(oldDir)) continue;
    if (SD_MMC.rename(oldDir, newDir)) {
      Serial.printf("[migrate] SD %s -> %s\n", oldDir, newDir);
    } else {
      Serial.println("[migrate] SD cache folder rename failed");
    }
    return;
  }
}

}  // namespace drehklang::drivers
