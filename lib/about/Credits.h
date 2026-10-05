#pragma once

#include <cstddef>

namespace drehklang::about {

// One component built into the firmware, for Settings > Licences
// (ADR 0026). THIRD-PARTY.md is the longer form of the same list, and the
// full licence texts are in licenses/; test_credits keeps all three and
// platformio.ini's lib_deps in step.
struct Credit {
  const char *name;
  const char *version;  // As shipped; "" when it has none of its own.
  const char *use;      // One line: what it does here.
  const char *licence;  // SPDX identifier.
  const char *home;     // Project website, without the scheme.
  // Text the licence requires to travel with binaries, verbatim; nullptr
  // when it asks for nothing beyond the licence text itself.
  const char *notice;
  // The lib_deps entry this comes from (a substring of that line);
  // nullptr when it is bundled with something else rather than declared.
  const char *dependency;
};

inline constexpr Credit kCredits[] = {
    {"LVGL", "8.4.0", "The touch interface", "MIT", "lvgl.io",
     "Copyright (c) 2021 LVGL Kft", "lvgl/lvgl"},
    {"ESP32-audioI2S", "4.0.0", "Decodes MP3, AAC, FLAC, Vorbis, Opus and WAV", "GPL-3.0",
     "github.com/schreibfaul1/ESP32-audioI2S", nullptr, "schreibfaul1/ESP32-audioI2S"},
    {"FAAD2", "", "AAC decoding, inside ESP32-audioI2S", "GPL-2.0-or-later",
     "github.com/knik0/faad2", "Code from FAAD2 is copyright (c) Nero AG, www.nero.com",
     nullptr},
    {"Opus decoder", "", "Opus decoding, inside ESP32-audioI2S", "BSD-3-Clause", "opus-codec.org",
     "Copyright (c) 2006-2011 Skype Limited; (c) 2007-2008 CSIRO; (c) 2007-2010 Xiph.Org "
     "Foundation; (c) 2008 Gregory Maxwell",
     nullptr},
    {"Vorbis decoder", "", "Vorbis decoding, inside ESP32-audioI2S", "BSD-3-Clause", "xiph.org",
     "Copyright (c) 1994-2007 Xiph.Org Foundation", nullptr},
    {"Simple FLAC", "", "FLAC decoding, inside ESP32-audioI2S", "MIT",
     "nayuki.io/page/simple-flac-implementation", "Copyright (c) Project Nayuki", nullptr},
    {"Arduino_GFX", "1.6.8", "Drives the round display", "BSD-2-Clause",
     "github.com/moononournation/Arduino_GFX", "Copyright (c) 2012 Adafruit Industries",
     "GFX Library for Arduino"},
    {"JPEGDEC", "1.8.4", "Decodes album covers", "Apache-2.0", "github.com/bitbank2/JPEGDEC",
     "Copyright 2020 BitBank Software, Inc.", "bitbank2/JPEGDEC"},
    {"Arduino-ESP32", "3.3.12", "Arduino core, USB and SD card", "LGPL-2.1-or-later",
     "github.com/espressif/arduino-esp32", nullptr, nullptr},
    {"ESP-IDF", "5.5.5", "FreeRTOS, file system, drivers", "Apache-2.0",
     "github.com/espressif/esp-idf", nullptr, nullptr},
    {"Bluedroid", "", "Bluetooth and SBC on the second chip, inside ESP-IDF", "Apache-2.0",
     "github.com/espressif/esp-idf", nullptr, nullptr},
    {"ESP-DSP", "", "The spectrum's FFT, inside ESP32-audioI2S", "Apache-2.0",
     "github.com/espressif/esp-dsp", nullptr, nullptr},
    {"Material Symbols", "", "Icons", "Apache-2.0", "github.com/google/material-design-icons",
     nullptr, nullptr},
    {"Montserrat", "", "Text", "OFL-1.1", "fonts.google.com/specimen/Montserrat", nullptr,
     nullptr},
    {"Font Awesome 5 Free", "", "Symbols in the text font", "CC-BY-4.0", "fontawesome.com",
     "Font Awesome by Fonticons, Inc.", nullptr},
};

inline constexpr std::size_t kCreditCount = sizeof(kCredits) / sizeof(kCredits[0]);

// Drehklang's own facts, for Settings > About.
inline constexpr const char *kAuthor = "Timo B\xC3\xB6wing";
inline constexpr const char *kOwnLicence = "GPL-3.0-or-later";
inline constexpr const char *kOwnHome = "github.com/bjblazko/drehklang";
inline constexpr const char *kNoWarranty = "Free software, without any warranty.";
// True as long as the firmware has no network at all. With Bluetooth on,
// the sound goes to the paired headphones (ADR 0027), so it says so.
inline constexpr const char *kPrivacy = "Nothing leaves the device but sound for your headphones.";

}  // namespace drehklang::about
