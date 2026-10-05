# 0026: Arduino-ESP32 3.x, upstream ESP32-audioI2S, and one owner of the DAC

## Status

Accepted — 2026-10-05. Supersedes ADR 0017 (two decode paths) and ADR 0020
(patching ESP32-audioI2S). Amends ADR 0001 (the platform comes from
pioarduino), ADR 0006 (the library decodes on its own task), ADR 0013
(Ogg, Opus and FLAC can shuttle), ADR 0022 (blips no longer play over
music) and ADR 0024 (the tone output opens its own I2S channel).

## Context

Official PlatformIO supports Arduino-ESP32 only up to 2.0.x (ESP-IDF 4.4).
Staying there cost a workaround in nearly every layer:

- **ESP32-audioI2S** had to be the esphome fork at 2.3.0, the last release
  for the 2.x core. It has no Vorbis decoder, so Drehklang carried a
  second decode path on stb_vorbis (ADR 0017), whose cueing stayed choppy.
- That fork never called its own M4A seek setup, so every M4A seek went to
  0:00 until a build-time patch added the call (ADR 0020).
- It also crashed M4A playback through an unguarded weak hook, and halved
  every sample, which another hook doubled back.
- **Arduino_GFX** was pinned at 1.4.9, the last release for the 2.x core.

The planned features need more: WiFi radio and podcasts want HTTP and HLS
streaming, Opus is a third format, and Bluetooth headphones need a clean
PCM tap. ADR 0017's own "revisit when" list named the move to the 3.x core
as the trigger. The board must stay: the firmware runs on this Waveshare
knob.

Bluetooth was part of the question but is not decided by the platform: the
ESP32-S3 has only BLE, and its controller cannot do LE Audio, so no S3
firmware can drive headphones. The board's second chip (ESP32-U4WDH) has
Classic Bluetooth and is the way to do it; see device.md for how the two
chips are wired.

## Decision

### Platform

- **pioarduino's `platform-espressif32`**, pinned to release 55.03.312-1:
  Arduino-ESP32 3.3.12 on ESP-IDF 5.5.5. It is the community-maintained
  continuation that Espressif points to for PlatformIO; the 4.0 release
  candidate (ESP-IDF 6.1) is not used.
- **Arduino and PlatformIO stay** (ADR 0001). Raw ESP-IDF with ESP-ADF was
  rejected: ADF's decoders are closed binary blobs, which conflicts with
  distributing GPL-3.0 firmware.
- **The partition table is named explicitly** (`default_16MB.csv`):
  pioarduino's copy of the board switched to `esp_sr_16.csv`. Keeping the
  old table keeps NVS (settings, resume, touch calibration) and the
  coredump partition where they were.

### One decoder library

**Upstream schreibfaul1/ESP32-audioI2S 4.0.0** (GPL-3.0) decodes MP3,
AAC/M4A, FLAC, Vorbis, Opus and WAV, so the following are gone:
`VorbisBackend`, the vendored `stb_vorbis.c`, `DecoderBackend`,
`AudioBackendKind`/`backendForPath()`, `scripts/patch-audioi2s.py`, the
`audio_info()` null guard and the 2× level compensation.

What Drehklang keeps on top of the library, and why:

- **Volume curve.** The library takes its curve from
  `AudioGain::volumeCurveDb()` (`setVolumeCurve()`). That is the same
  22-step table as before, so the knob feels the same.
- **Exact durations and byte seeks.** The library's `setAudioPlayTime()`
  converts seconds to bytes with its own bitrate guess, which is wrong for
  VBR. `Mp3Duration`/`Mp4Parser` timing and `setAudioFilePosition()` stay.
- **Resume positions are applied late.** The library refuses a byte
  position until it has parsed the file's header. Vorbis also needs the
  setup header before any seek, or it never decodes. So the driver holds a
  pending position and applies it from its loop task once the decoder has
  produced its first samples. The output stays silent until then
  (`setHoldOutput()`, `decoderProducedSinceHold()`).
- **Two weak hooks**, in `AudioHooks.cpp`:
  - `audio_process_raw_samples()` taps the samples *before* the volume
    for the spectrum. It no longer has to divide the volume back out.
  - `audio_process_i2s()` applies the sleep timer's fade after it.

### One owner of the DAC

Under ESP-IDF 5 the legacy and the new I2S drivers cannot run side by
side, and upstream keeps its channel handle private. Rather than patch the
library or share one port between two writers, **the DAC has one owner at
a time** (`playback::DacArbiter`, host-tested):

- **The player** (`Esp32AudioI2SDriver`) creates `Audio`, and with it the
  I2S channel, while it owns the DAC. When the tone output claims the DAC,
  the player **parks** its track: it keeps the path and the heard position,
  destroys `Audio`, and reopens the track there on the next `resume()`. A
  parked track still counts as running, so the main loop never takes it for
  finished.
- **The tone output** (`ToneOutput`, blips and the tone generator) opens
  its own 16-bit channel on the first sound, at 22.05 kHz or 48 kHz, and
  closes it when the player claims the DAC back.
- **Games now pause music on entry**, as the tone generator always did.
  Blips no longer play over a track (ADR 0022 mixed them in), which is
  what makes the handover safe. The user asked for the simpler
  architecture over the mixing.

This deletes the idle-stream watcher, the rate re-claiming on resume and
the mixing in `AudioOutputStage`. It is also the shape the Bluetooth
output will need: a second destination for the same samples.

### Positions stored before this change

An Ogg position used to be a sample index; the library reads it as a byte
offset. The resume record (v3) and the bookmarks (v2) still read their
previous version, but reset an Ogg track's position to the start of that
part (`resume::forgetLegacyOggPosition()`, host-tested). The title and the
part are kept.

### Other ports the new core needed

- **Touch:** `Cst816Driver` uses `Wire` instead of the legacy
  `driver/i2c.h`. Like I2S, the legacy and new I2C drivers abort at boot
  together ("i2c: CONFLICT! driver_ng").
- **NVS:** the namespace migration uses ESP-IDF 5's iterator API.
- **Encoder:** `IRAM_ATTR` only on the definitions. ESP-IDF 5 gives each
  one its own section, so marking declaration and definition conflicts.
- **`std::min`/`std::max`:** explicit types. The new toolchain's
  `uint32_t` is `unsigned long`, so mixed calls became ambiguous.
- **`CORE_DEBUG_LEVEL`** is defined in `build_flags`. The library's log
  macros need it, and PlatformIO does not set it.

### About and licences on the device

Settings > About (also opened by tapping the wordmark on Home) shows the
version, the author (Timo Böwing), github.com/bjblazko/drehklang, the
licence with the no-warranty sentence, and that nothing leaves the
device. Its Licences button lists every component with its licence.
Each component opens a page with its version, its use, its website and
any notice its licence requires word for word, for example FAAD2's "Code
from FAAD2 is copyright (c) Nero AG, www.nero.com". The full licence
texts are in `licenses/`. `test_credits` fails when:

- a `lib_deps` entry has no credit, or a credit's version differs from
  the pin;
- a credit or its notice is missing from THIRD-PARTY.md;
- a licence text is missing.

## Consequences

- **HE-AAC is a release blocker.** Upstream's AAC decoder is now FAAD2,
  built with SBR and parametric stereo on the ESP32-S3. AAC-LC's patents
  have expired; some HE-AAC patents may not have. This only matters once a
  binary or a flashed device is passed on. **Before the first binary
  release, or at the WiFi radio phase (where HE-AAC streams are common),
  whichever comes first, decide:** keep it after a proper check, or turn
  SBR/PS off with a documented build patch.
- Every format the library decodes can now shuttle (ADR 0013): MP3, M4A,
  WAV, FLAC, Ogg, Opus. Each still needs checking on the device.
- The library allocates about 0.75 MB of PSRAM for buffers while `Audio`
  exists. Internal free heap after boot is 71.6 KB (2026-10-05), in line
  with the earlier ~70 KB baseline. WiFi will need its buffers moved to
  PSRAM through pioarduino's `custom_sdkconfig`; that waits for the WiFi
  phase, so this change stays a platform swap.
- **Weak-hook trap, new form:** upstream's `Audio.h` *declares* the hooks
  weak, and a definition in a file that includes it is weak too. The
  linker then kept the library's empty stubs. The hooks live in a file
  that does not include `Audio.h` (verify with `nm`: `T`, not `W`).
- **`Audio` lives in internal RAM, zero-filled** (both found on the device
  the same day). Unzeroed memory left the library's M4A header state as
  garbage, and every M4A ended at once.
  The core's `CONFIG_I2S_ISR_IRAM_SAFE` rejects an I2S callback context in
  PSRAM. The library aborted on that, and every first play rebooted the
  board. While a track plays, internal free heap drops from ~72 KB to
  ~31 KB, most of it the library's I2S DMA buffers. That is the budget
  the WiFi phase has to fit into; shrinking the library's DMA settings is
  the first lever.
- Any other legacy ESP-IDF 4 driver header (`driver/i2s.h`,
  `driver/i2c.h`) brought in later will abort at boot. The new core links
  the new drivers itself.

## Where this leaves the roadmap (2026-10-05)

Phase 1 of the platform plan is done and verified on the device: MP3, M4A,
Ogg and WAV play, shuttle and resume; Tones and games hand the DAC over
and back. Next, each in its own session:

1. **Bluetooth headphones** (A2DP source, headphone buttons via AVRCP,
   auto-reconnect). Only the second chip, the ESP32-U4WDH, has Classic
   Bluetooth: a second firmware there, fed PCM by the S3 over the shared
   UART (device.md, "The second chip and the audio switch"). The S3 side
   becomes another DAC owner behind `playback::DacArbiter`, fed from the
   same `audio_process_i2s` hook (`continueI2S = false` swallows the
   samples). Start with a design spec: the link's framing and flow
   control (there are no RTS/CTS lines; 44.1 kHz stereo needs at least
   1.77 Mbaud), pairing UI, button mapping, what the jack does meanwhile,
   and flashing the second chip (factory image in `hardware-backups/`).
2. **WiFi** (web radio; podcasts downloaded to SD). Decide HE-AAC first
   (above), and fit the internal-RAM budget: ~31 KB free while playing.
3. Opus and 24-bit FLAC: the library decodes both; the library scan
   (`lib/library/AudioFileTypes.h`) does not list Opus yet.

