# Third-party components

Drehklang itself is licensed under GPL-3.0-or-later (see [LICENSE](LICENSE)).
The firmware links the components below; each keeps its own licence, and
all of them are compatible with GPL-3.0-or-later. Versions are pinned in
[`platformio.ini`](platformio.ini). The full licence texts are in
[`licenses/`](licenses/), and the device lists the same components under
Settings > Licences (`lib/about/Credits.h`; `test/test_credits` keeps the
three in step).

| Component | Used for | Licence | Notice |
|---|---|---|---|
| [LVGL](https://lvgl.io) 8.4.0 | The touch interface | MIT | Copyright (c) 2021 LVGL Kft |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) 4.0.0 | Decoding MP3, AAC/M4A, FLAC, Vorbis, Opus and WAV; I2S output | GPL-3.0 | |
| [FAAD2](https://github.com/knik0/faad2), inside ESP32-audioI2S | AAC decoding | GPL-2.0-or-later | Code from FAAD2 is copyright (c) Nero AG, www.nero.com |
| [Opus decoder](https://opus-codec.org), inside ESP32-audioI2S | Opus decoding (SILK and CELT) | BSD-3-Clause | Copyright (c) 2006-2011 Skype Limited; (c) 2007-2008 CSIRO; (c) 2007-2010 Xiph.Org Foundation; (c) 2008 Gregory Maxwell |
| [Vorbis decoder](https://xiph.org), inside ESP32-audioI2S | Vorbis decoding | BSD-3-Clause | Copyright (c) 1994-2007 Xiph.Org Foundation |
| [Simple FLAC](https://www.nayuki.io/page/simple-flac-implementation), inside ESP32-audioI2S | FLAC decoding | MIT | Copyright (c) Project Nayuki |
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) 1.6.8 | QSPI display driver | BSD-2-Clause | Copyright (c) 2012 Adafruit Industries |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) 1.8.4 | Album cover decoding (baseline and progressive JPEG) | Apache-2.0 | Copyright 2020 BitBank Software, Inc. |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) 3.3.12 | Arduino core, TinyUSB, SD_MMC | LGPL-2.1-or-later | |
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.5.5 (bundled with the core) | FreeRTOS, FatFs, drivers | Apache-2.0 | |
| Bluedroid, bundled with ESP-IDF | Bluetooth stack and SBC encoder of the ESP32-U4WDH firmware (`bt/`) | Apache-2.0 | |
| [ESP-DSP](https://github.com/espressif/esp-dsp), bundled with the core | The FFT ESP32-audioI2S links | Apache-2.0 | |
| [Material Symbols](https://github.com/google/material-design-icons) | The icon glyphs in `lib/ui-widgets/IconFont*.c` | Apache-2.0 | |
| [Montserrat](https://fonts.google.com/specimen/Montserrat) (Medium, bundled by LVGL as `scripts/built_in_font/Montserrat-Medium.ttf`) | Text glyphs in `lib/ui-widgets/TextFont*.c` (replaces LVGL's built-in Montserrat fonts, extended to Latin-1 Supplement/Latin Extended-A for European tag text) | OFL-1.1 | |
| [Font Awesome 5 Free](https://fontawesome.com) (bundled by LVGL as `scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff`) | The `LV_SYMBOL_*` icon glyphs embedded in `lib/ui-widgets/TextFont*.c` (same codepoint range as LVGL's own built-in Montserrat fonts) | CC-BY-4.0 (icons) / OFL-1.1 (font) | Font Awesome by Fonticons, Inc. |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | Host-side unit tests only (not shipped) | MIT | |

The Bluetooth firmware links Espressif's binary-only Bluetooth controller
and radio libraries (`libbtdm_app`, `libbtbb`, `libphy`, `libcoexist`, part
of ESP-IDF); its own files carry an additional permission for that, see
[LINKING-EXCEPTION.md](LINKING-EXCEPTION.md).

GPL-3.0-or-later for Drehklang is not a free choice: ESP32-audioI2S is
GPL-3.0, so any distributed firmware linking it is covered as a whole.

`bb_spi_lcd` is downloaded as a dependency of JPEGDEC but never compiled
into the firmware, so it is not listed.

## AAC/M4A decoding

M4A files are decoded by FAAD2, which ESP32-audioI2S bundles and
distributes as part of its GPL-3.0 release (until ADR 0026 it was the
Helix AAC decoder). Drehklang adds no decoder of its own; it reads MP4
metadata (`lib/library/Mp4Parser.h`) and hands the audio to that library.

AAC-LC — the profile Drehklang's library uses — dates from the mid-1990s
and its core patents have expired; the licensing pool for it has wound
down. **ESP32-audioI2S builds HE-AAC (SBR and parametric stereo) into the
decoder on the ESP32-S3**, and those newer tools are a different matter.
Whether to keep or disable them is an open decision, recorded in
[ADR 0026](docs/adr/0026-arduino-esp32-3-and-upstream-audioi2s.md) as a
blocker for the first binary release. This is a description of the
project's situation, not legal advice: anyone distributing builds should
reach their own conclusion.
