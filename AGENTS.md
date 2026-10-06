# Notes for AI agents (and humans) working on this repo

This file exists specifically to stop expensive hardware-bring-up
lessons from being re-learned from scratch in a future session. Update
it whenever you find a genuinely non-obvious hardware fact or fix a bug
that only showed up on the physical board (not from compiling). Keep
entries short; link to the fuller writeup (ADR, `device.md`) instead of
duplicating it.

**Before touching hardware-facing code**, read:
- [`device.md`](device.md) — pinout, board identification, hardware
  quirks observed.
- [`docs/adr/0004-navigation-library-and-index-architecture.md`](docs/adr/0004-navigation-library-and-index-architecture.md)
  — the "Implementation status" section documents every real bug found
  during hardware bring-up, in the order it was found, with root cause
  and fix. It's long but it's the actual debugging history — read it
  before assuming something works just because it compiles.

## Hardware gotchas (quick index — see linked sections for detail)

- **Touch X is miscalibrated in the raw CST816 data** (raw ~= 1.18 *
  visual - 66), corrected by `lib/input/TouchCalibration.h` (defaults)
  or a re-fit saved from Settings > Touch calibration (NVS `touchcal`,
  ADR 0010). "Buttons miss taps" → run that calibration first (its fit
  is logged as `[touchcal]` on serial), and use `-DDREHKLANG_TOUCH_DEBUG`
  logging before touching sizes/timing. See ADR 0004.

- **Spectrum frame cost**: build with `-DDREHKLANG_SPECTRUM_DEBUG` to log
  samples/rate/gain and the worst per-frame analyzer time every 90 frames
  (~2 ms measured 2026-09-14). The spectrum taps the decoder's samples
  *before* the volume (`audio_process_raw_samples`, ADR 0026), so it has no
  gain to divide out. See ADR 0009.
- **This board's rotary encoder is rotation-only** (no click/push) and,
  more subtly, **is not a standard 4-state quadrature encoder** — its
  raw pin states never visit `00` (both contacts closed), only `11`
  (rest), `01`, and `10`. A decoder written against the textbook
  Gray-code model (any well-known Arduino rotary-encoder library
  included) will silently never fire on this hardware. See
  `lib/drivers-encoder/GpioEncoderDriver.h`'s class comment for the
  actual 3-state model used, and don't assume it transfers to a
  different encoder without re-capturing raw pin states first (the
  `drainLog`-style raw logging approach used to find this is worth
  repeating for any new input hardware, rather than guessing).
- **Board config must be `4d_systems_esp32s3_gen4_r8n16`, not
  `esp32-s3-devkitc-1`.** The latter is explicitly labeled "No PSRAM" in
  its own board definition; using it (even with manual `psram_type`
  overrides) causes real PSRAM init failures on this R8-variant chip,
  and it also lacks `ARDUINO_USB_CDC_ON_BOOT=1`, which silently sends
  all `Serial` output to unconnected physical UART0 pins instead of the
  native USB port this board is flashed through. See `platformio.ini`'s
  comment block and ADR 0004.
- **QSPI display goes through Arduino_GFX, not ESP-IDF's
  `esp_lcd_panel_io_spi`.** On ESP-IDF 4.4 that component compiled and
  returned `ESP_OK` for everything while never driving the panel; ESP-IDF 5
  supports QSPI there, but the Arduino_GFX path (`Arduino_ESP32QSPI`,
  1.6.8, with the vendored init table in `lib/drivers-display/`) is the
  hardware-verified one. See `platformio.ini` and ADR 0026.
- **This board's USB-serial reaches the primary ESP32-S3R8 two ways**:
  the documented CH340 path, or (confirmed working) the chip's own
  native USB-Serial-JTAG peripheral. `scripts/flash-primary-mcu.sh`
  auto-detects either. Cable orientation can also matter for which of
  the board's *two MCUs* (primary S3 vs. secondary U4WDH) the CH340 path
  reaches — see `device.md`.
- **`LV_COLOR_16_SWAP` must be `0` with the Arduino_GFX driver**, not `1`
  (which is what Waveshare's own esp_lcd-based demo needs for its raw
  transmission path). Arduino_GFX already sends bytes in the order the
  panel wants; leaving the swap on renders every LVGL color wrong (theme
  blue appeared bright green) while raw `Arduino_GFX::fillScreen()`
  calls looked fine, because those bypass LVGL's color pipeline
  entirely. See `include/lv_conf.h`.
- **An SD card formatted by macOS Disk Utility can mount but fail almost
  every file read on this board's SDMMC bus** -- not a wiring, code, or
  card-quality problem. Symptom: `SD_MMC.begin()` succeeds, directory
  listing works (file/folder names all show up correctly), but opening
  actual file contents fails for the overwhelming majority of files with
  `E (...) diskio_sdmmc: sdmmc_read_blocks failed (257)`, consistently
  and reproducibly (the same tiny handful of files succeed every boot).
  Reproduced across three different SanDisk cards and multiple SDMMC
  bus configs (40MHz/20MHz, 4-bit/1-bit, with/without explicit internal
  pull-ups on CMD/D0-D3) -- none of that made any difference, ruling out
  card quality, bus speed, and pull-ups as the cause. The board's stock
  factory firmware read its own bundled SD card perfectly on the same
  physical slot, ruling out a wiring/soldering defect. The fix: erase
  and reformat the card as FAT32 with the SD Association's official
  layout (the *SD Memory Card Formatter* app, or `newfs_msdos` with an
  explicit, conservative cluster size like `-c 8`/4KB, not macOS
  Disk Utility's defaults, which produced 32KB clusters and a 4MB
  partition offset). After reformatting, `sdmmc_read_blocks failed`
  errors dropped to zero and the full library scan succeeded. Root
  cause of *why* the embedded FatFs stack chokes on Disk Utility's
  layout specifically was not conclusively identified (plausibly BPB/
  geometry fields that differ from SD-Association-compliant tools) --
  but the fix reliably works, so: **whenever "many/most files fail to
  open" shows up again, reformat the card properly before suspecting
  anything else.**
- **A released touch sample has no position.** Its coordinates are
  leftovers (x=11 wherever the finger was). `LvglGlue` therefore reports
  the last pressed point on release. Anything that measures a swipe from
  LVGL's release point depends on that (2026-09-18, ADR 0024). To drive a
  swipe without a hand on the device, send `SWIPE x1 y1 x2 y2` over
  serial.
- **The DAC has one owner at a time** (ADR 0026): the player or the tone
  output (blips, tone generator), each with its own I2S channel, handed over
  by `playback::DacArbiter`. A blip or tone parks the player's track
  (path and position kept) and the next resume reopens it. Nothing writes
  to another owner's channel, so there is no shared sample rate to restore
  any more. Opening a game or starting a tone pauses music first; keep
  that, or a blip silences a track the UI still shows as playing.
- **Talking to the second chip (ESP32-U4WDH).** Its UART0 is the CH340
  side of the cable (flip the USB-C plug; `/dev/cu.usbserial-*`). Opening
  that port with pyserial's defaults asserts RTS and **holds the chip in
  reset** -- set `dtr = rts = False` before `open()`, or the log stays
  empty. The link between the chips is S3 GPIO48 (TX) / GPIO38 (RX) at
  3 Mbaud, and both ends must lower the RX FIFO threshold to 64 or bytes
  get lost (device.md, ADR 0027). `scripts/flash-bt-mcu.sh` flashes it.
  **Whatever runs on the U4WDH must drive its IO32 high** -- it is the
  DAC's XSMT, and floating it mutes the jack for both chips (2026-10-05).
- **ESP32-audioI2S's own messages are silent by default.** To see them,
  set `-DCORE_DEBUG_LEVEL=3` (instead of 0) and add `-DAUDIO_LOG` in
  `[env:esp32-s3]`'s `build_flags` temporarily: `Esp32AudioI2SDriver.cpp`
  then routes `Audio::audio_info_callback` to serial (non-blocking, lines
  that don't fit are dropped). Remove both again afterwards.
- **Weak-hook link gotchas (ESP32-audioI2S).** Volume 21 is 0 dBFS
  (`AudioGain::volumeCurveDb()` is the curve the library uses). The two
  hooks (`audio_process_raw_samples`, `audio_process_i2s`) live in
  `lib/drivers-audio/AudioHooks.cpp`, which must **not** include
  `<Audio.h>`: that header declares them weak, a definition after a weak
  declaration is weak too, and the linker then keeps the library's empty
  stubs (found 2026-10-05). The file is linked because the driver calls its
  `setHoldOutput()`; a weak reference alone never pulls an object out of a
  static library. Verify with `xtensa-esp32s3-elf-nm -C firmware.elf |
  grep audio_process`: `T`, not `W`. Going past 0 dBFS isn't worthwhile:
  the PCM5100A is a >=1 kOhm line driver, so 32 Ohm headphones are
  current-limited anyway.
- **A custom LVGL icon font generated by `lv_font_conv` renders
  completely invisibly (correct label sizing, zero pixels drawn) if this
  project's `lv_conf.h` (`LV_USE_FONT_COMPRESSED 0`) doesn't match the
  font's own compression.** `lv_font_conv` RLE-compresses glyph bitmaps
  by default; without the decompressor compiled in, glyph *metadata*
  (advance width, box size) still reads fine — a label sizes itself
  correctly — but the bitmap itself never draws, which looks identical to
  a font/encoding mismatch and can send you down the wrong debugging path
  entirely. Always pass `--no-compress` when generating a font for this
  project (see `lib/ui-widgets/IconFont.c`'s header comment, ADR 0005) —
  this applies equally to the `drehklang_text_font_*` fonts generated by
  `scripts/generate-text-fonts.sh` (ADR 0019).
  Diagnosed via `scripts/screenshot.py` (see below) rendering a plain
  label with the font on a contrasting background, outside any button, to
  isolate font-rendering from button/theme interaction.
- **A screenshot tool exists for diagnosing on-device UI bugs without a
  phone photo or a written description**: `scripts/screenshot.sh` sends
  `SCREENSHOT\n` over serial, and `lib/ui/LvglGlue::writeScreenshotToSerial()`
  dumps a full-frame shadow buffer (kept in sync inside `flushCb`, since
  LVGL itself only ever flushes partial stripes) as raw RGB565, decoded
  into a BMP host-side. Use this whenever a UI/layout bug needs verifying
  on real hardware — much faster and more precise than asking for a photo
  or a description. Requires pyserial; the `.sh` wrapper falls back to
  PlatformIO's bundled Python if the system one lacks it.
- **An LVGL `lv_arc` reserves padding on `LV_PART_MAIN` sized for its
  (draggable) knob, even after the knob's own style is removed** — a
  ring meant to hug an edge renders visibly smaller than its host object
  unless you also `lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN)`. Found
  building the round-edge volume/unlock rings (ADR 0005,
  `lib/ui-widgets/EdgeArc.h`) via a real-hardware screenshot: the ring
  didn't reach the display's edge despite its host already being sized
  to the full framebuffer.
- **An LVGL screen (`lv_obj_create(nullptr)`) is scrollable by default,
  and a child sized larger than the screen makes that visible** as thin
  grey scrollbar lines along the screen's right/bottom edges — easy to
  mistake for a rendering artifact in the content itself. This app
  deliberately oversizes some widgets beyond the screen and lets the
  round bezel clip the excess (see `ScreenManager::renderNowPlaying()`'s
  volume ring host), so every screen now clears
  `LV_OBJ_FLAG_SCROLLABLE` in `ScreenManager::render()` — this app has
  its own swipe-gesture handling (`GestureRecognizer`) and never wants
  built-in scroll behavior anyway.
- **`Audio::loop()` must run often, away from LVGL.** It refills the
  decoder's input buffer from SD; the library decodes on its own task, but
  a starved buffer still underruns. It runs on the driver's own task on
  core 0 (ADR 0006), mutex-guarded against the main thread's calls,
  because LVGL's display flush on the main loop blocks for long stretches
  (animations, fast list scrolling). If audio stutter reappears, check
  what's producing heavy LVGL redraw activity first.
- **This specific board unit repeatedly goes into a state where it
  "runs" but a peripheral is silently dead, and only a real power cycle
  (unplug USB, wait, replug) fixes it -- a soft/RTS reset is not
  enough.** Seen now for the display (stayed white after a soft reset),
  the SD card (0x107 "card not responding" until reseated/repowered),
  and audio (played per every log line -- `connecttoFS=OK`,
  `processLocalFile()` with a correct `m_audioDataStart` matching the
  file's real ID3-tag size, no errors at all -- yet the jack stayed
  silent until a full power cycle). Before spending time debugging code
  for "X looks like it should work but doesn't, no errors logged" on
  this board, try a full power cycle first.
- **Boot used to take 20+ seconds before the SD library scan became
  visible; boot no longer scans the SD card at all.** Found via live
  serial capture (`millis()` timestamps around each boot step)
  2026-09-13. Two compounding causes: `computeSignature()`
  (`src/main.cpp`) does one full recursive SD directory walk
  (`SdFileLister::walk()`), ~17-19s on a real library (512 tracks, 560
  macOS AppleDouble `._` sidecar files also walked) with zero UI
  feedback; and `writeIndexCacheFile()` (`kIndexCachePath =
  "/drehklang/library.idx"`) silently failed on **every single boot**
  (`vfs_api.cpp: open(): ... does not exist, no permits for creation`)
  because the SD_MMC/FATFS layer refuses to create a file inside a
  directory that doesn't exist, and nothing ever created `/drehklang` --
  so the cache never persisted and every boot paid for the full walk
  *twice* (signature + `LibraryScanner::scan()`, which used to
  `lister.reset()` again) plus a full tag-read scan, forever. First fix
  (mkdir + `FileLister::rewind()` so the scanner replays an
  already-walked lister instead of re-walking) got a routine boot down
  to the one ~17-19s signature walk. Then, per a follow-up feature
  request, the signature walk was removed from boot entirely: `setup()`
  now just decodes the cached `library.idx` directly (a single small
  file read) and shows whatever that contains, instantly, even if it's
  stale. Detecting changes and rebuilding the index is now **on-demand
  only**, via Settings > "Rescan library" (`ScreenManager::runRescan()`;
  a refresh button on the library root until ADR 0010) that calls a small `library::LibraryRescanner`
  interface (`lib/library/LibraryRescanner.h`) implemented in
  `src/main.cpp` (`SdLibraryRescanner`) so the UI layer doesn't need to
  know about the concrete SD types. A device with no cache yet (e.g.
  first boot after flashing) just shows an empty library until the user
  runs it.
- **A UI action that runs a synchronous, multi-second blocking call
  (like the scan button above) must NOT call `lv_timer_handler()` to
  flush progress to the screen while it's running -- it corrupts touch
  input app-wide, not just on that screen.** `LvglGlue::pump()` (called
  every `loop()`) *is* `lv_timer_handler()`; a button's `LV_EVENT_CLICKED`
  handler runs from inside that very call, so calling
  `lv_timer_handler()` again from inside the handler is a reentrant call
  into LVGL's own timer/input dispatch -- LVGL explicitly does not
  support this. Symptom on real hardware (2026-09-13): after tapping the
  scan button once, taps became unreliable everywhere in the app (list
  items, the lock button, playback controls), not just around the scan
  UI -- easy to misdiagnose as "the button is dead" or "SD scan runs in
  the background" (neither was true; the scan was synchronous and
  finished, but input stayed corrupted afterward). Fix: use
  `lv_refr_now(nullptr)` instead, which only forces the pending redraw
  without touching input devices or other timers, so it's safe to call
  from inside an event handler. Also delete any scratch UI created for
  the duration (e.g. a progress overlay) with `lv_obj_del_async()`, not
  `lv_obj_del()`, for the same reason `ScreenManager::render()` already
  does for screen swaps triggered from a click handler (see that
  function's own comment). See
  `lib/ui/ScreenManager.cpp`'s `ScanProgressLabelListener` and
  `onScanClicked()`.
- **A full recursive SD directory walk (`computeSignature()` /
  `SdFileLister::reset()`), immediately followed by a batch of
  individual file opens in the same call, reliably drives the SD_MMC
  peripheral into `sdmmc_read_blocks failed (257)` for every single one
  of those opens** -- found 2026-09-13 while adding album-cover
  generation that ran right after this walk. Confusingly, this is NOT a
  generic "SD card is flaky" issue: an isolated single file open done
  elsewhere (e.g. starting playback) succeeds reliably even immediately
  afterward, and a genuine full library rescan (which does the exact
  same "walk then open every file" pattern) has worked before. It also
  survives a full chip reset (even `esp_deep_sleep_start()`, which
  resets far more hardware state than a normal reboot) with 100%
  reproducibility, which points at the SD *card's own* internal
  controller being left in a bad state (not an ESP32-side register) --
  and if a battery is connected (PH1.25 connector), unplugging USB does
  **not** actually power-cycle the board, so the usual "real power
  cycle" fix for a stuck peripheral may not even be exercisable. Found
  this leaves `SdFileLister::reset()`'s own top-level directory handle
  unclosed too (only `walk()`'s child entries were being closed) --
  fixed, but closing that leak alone did NOT fix the read failures, so
  don't assume it's the whole story if this resurfaces. Workaround
  adopted: don't batch-generate covers right after a walk at all --
  `ScreenManager::renderNowPlaying()` instead generates a missing cover
  lazily, from the one isolated file open playback already needs, the
  first time an album is actually played. If a future feature needs to
  do many individual file opens right after a directory walk again,
  expect this same failure and budget time to design around it (e.g.
  interleave the opens into the walk itself) rather than pacing/delays,
  which did not help in testing.
- **Most real-world embedded cover art is Progressive JPEG** (~90% of
  this library's ID3 APIC and MP4 covr pictures), which TJpg_Decoder
  couldn't decode at all. Since ADR 0016 covers go through JPEGDEC
  (`lib/drivers-jpeg/JpegDecAdapter.h`): progressive images decode from
  their DC coefficients at 1/8 scale (19 ms, ~11 KB for 600 px). Don't
  switch to a full progressive decoder: stb_image needed 4.2 MB of PSRAM
  at 600 px and ran out above ~1000 px (measured 2026-09-15).
- **The firmware uses TinyUSB, not the S3's USB-Serial-JTAG** (ADR 0016,
  `ARDUINO_USB_MODE=0`), for the USB drive. Consequences found on the
  device 2026-09-16:
  - Flashing needs a **1200-baud touch** to reboot into the ROM
    bootloader (esptool's DTR/RTS reset fails with "No serial data
    received"); `scripts/flash-primary-mcu.sh` does it. The first switch
    from a USB-Serial-JTAG build needed one USB replug.
  - **A crash's output never reaches USB** (the port reappears after
    boot). Send `INFO` over serial: reset reason 4 is a panic. Then run
    `scripts/read-coredump.sh` (the core dump sits in the `coredump`
    partition) with the ELF of the crashing build.
  - **`Serial.write()` spins forever whenever the CDC endpoint can't
    drain** (Arduino-ESP32 2.0.x `USBCDC::write` has no timeout; not
    re-checked on 3.3, so keep the rule): a host
    that holds the port open without reading, or a busy USB drive
    starving CDC. The loop freezes until the watchdog resets it -- one
    `[battery]` line during a drive copy was enough (core dump showed
    loopTask in `tu_fifo_count`, 2026-09-16). Nothing may print while
    `UsbMscStorage::exporting()`; keep it that way when adding logs, and
    keep a monitor reading continuously (never two readers on the port).
  - **A hung loop leaves no trace by itself.** Build with
    `-DDREHKLANG_LOOP_WDT` (PLATFORMIO_BUILD_FLAGS) to arm a task watchdog
    on loop(): the hang becomes a panic whose core dump names the call.
    That is how the CDC spin above was found. Its timeout must stay
    clear of the longest legitimate blocking call, a full library rescan
    -- at 15 s the watchdog killed the scan itself (2026-09-16), so it is
    90 s now. Flash a normal build before handing the device back.
  - The port drops on every USB drive start/stop (re-enumeration):
    `UsbMscStorage::printEvents()` prints the drive's host events later.
  - Serial commands for driving the device without a hand on it:
    `TAP x y`, `SWIPE x1 y1 x2 y2`, `KNOB n`, `INFO`, `BLIP <hz>`,
    `SCREENSHOT`, `HOME`, `WHERE`, `HOLD x y ms`, `BT`.
  - **Never `Serial.printf()` from the audio task (core 0)**, not even in a
    debug build. A `[generator]` line from `ToneOutput` spun in
    `USBCDC::write` during a `SCREENSHOT` transfer until the task watchdog
    reset the board (reset reason 6, core dump in `tud_cdc_n_write_available`,
    2026-09-18). Format into a buffer and write only if
    `Serial.availableForWrite()` has room, otherwise drop the line.
  - **`SCREENSHOT` works again** (2026-10-05): on the 3.x core one
    259 KB `Serial.write()` gave up part-way (197 KB arrived), so
    `LvglGlue::writeScreenshotToSerial()` now writes 512-byte chunks and
    waits for room, giving up after 2 s without progress. Earlier, on the
    2.0.x core, a transfer once dropped the board off USB until a power
    cycle (2026-09-16); keep a reader on the port while it runs.
  - **Scripted tours:** `HOME` (Home, first entry selected, browse tabs at
    their roots, display woken), `WHERE` (prints `[where] <ScreenKind>`),
    `HOLD x y ms` (a held finger; send `KNOB n` meanwhile for
    hold-and-turn). `scripts/readme-screenshots.py` is the example: it
    waits ~1 s after each tap (knob detents sent before the next screen
    has rendered went to the old one) and retries a step from Home, since
    a serial line now and then goes missing.
  - Bulk transfer over the old USB-Serial-JTAG CDC dropped bytes; TinyUSB
    CDC with an 8 KB ack per chunk was reliable but slow (0.14 MB/s) and
    stalled once after ~30 MB. Use USB drive mode for files.
- **USB drive on macOS**: macOS reads the entire FAT when mounting, at the
  drive's ~0.87 MB/s. 4 KB clusters on a 32 GB card (31 MB FAT) time out;
  32 KB clusters (3.9 MB) mount in ~6 s. And **a locked Mac ejects new
  removable storage right after probing it** (START STOP UNIT with eject,
  ~1.5 s after export) -- check `CGSSessionScreenIsLocked` before
  debugging the firmware.
- **UI colors must be judged on the device, not a monitor or a
  screenshot's hex values.** The panel is RGB565 (subtle neutrals get
  rounded — `#F4F4F0` arrived as neutral `#F6F6F6`) and visibly shifts
  toward green. Attempts to "warm" the surface to compensate all looked
  off on the device, so it's the original Snow White `#F4F4F0` — don't
  re-tint it without checking on hardware. Use `lib/ui/Theme.h` tokens
  only. See ADR 0008.
- **`LV_LABEL_LONG_DOT` does nothing on a content-height label** — it
  just wraps. Use `setClampedText()` in `ScreenManager.cpp` (explicit
  line budget). Text uses the project's own `drehklang_text_font_*`
  fonts (`lib/ui-widgets/TextFont.h`), generated from Montserrat by
  `scripts/generate-text-fonts.sh`, not LVGL's built-in (ASCII-only)
  Montserrat. They cover Latin-1 Supplement and Latin Extended-A; to
  widen the range further, edit the codepoint list in that script and
  rerun it. See ADR 0008 and ADR 0019.
- **Small heap allocations must go to PSRAM, or SD reads fail.** ESP-IDF
  keeps every `malloc` under 4096 bytes in internal RAM by default, so
  the library index's and a play queue's path strings filled it (38.9 KB
  free after boot, 1.7 KB after queueing 512 tracks) and the SD driver's
  DMA buffers then failed: `sdmmc_read_blocks failed (257)`
  (`ESP_ERR_NO_MEM`), `connecttoFS=FAILED`, a library shuffle showed a
  track but never played. `setup()` now calls
  `heap_caps_malloc_extmem_enable(32)` first (93.9 KB internal free after
  boot, unchanged by the queue). If SD opens start failing with 257,
  check internal heap before suspecting the card. See ADR 0011.
- **ESP32-audioI2S's durations and time seeks are estimates, wrong for
  VBR.** Every MP3 here is VBR (checked 2026-09-15); the library converts
  seconds to bytes with an average bitrate. `library::Mp3Duration` and
  `Mp4Parser` give the exact duration, and the driver seeks by byte with
  `setAudioFilePosition()`, never `setAudioPlayTime()`.
  `getAudioFilePosition()` already subtracts the input buffer (what is
  heard, not what is read). A seek refills at most 64 KB before sound
  resumes. A resume position is refused until the header is parsed, so
  the driver applies it from its loop task and keeps the output silent
  until then. "Accepted" is not enough: Vorbis accepts a seek before it has
  read its setup header, and then never decodes (an Ogg resumed after the
  tone generator raced silently through the file, 2026-10-05). The driver
  waits for the first decoded samples (`decoderProducedSinceHold()`). See
  ADR 0012, 0013, 0026.
- **The player is parameterised by a "collection", not hardcoded to
  music** (ADR 0018). `lib/collection/CollectionProfile.h` is the table:
  Music `/Music`, Audiobooks `/Audiobooks`, Radio Plays `/RadioPlays`,
  each with its own `/drehklang/*.idx`. Two things bite when editing it:
  `CollectionId` and `ScreenManagerMenu.cpp`'s `kMenuEntries` are both
  **append-only** (their order is a stored resume value and the bit order
  of the `menuVis` NVS byte), and a screen's `artistId`/`albumId` are
  indices into *its own* collection's index — always carry
  `ScreenParams::collection` along when pushing, or ids silently resolve
  against Music.
- **`lv_font_conv` needs `@latest` from npm**: the pinned older version in
  some caches lacks `--lv-font-name`, and without it the generated font's
  symbol won't match `IconFont.h`'s `LV_FONT_DECLARE` (or, for the text
  fonts, `TextFont.h`'s). The exact invocation is in each `IconFont*.c`
  and `TextFont*.c` header comment — keep `--no-compress` (see above).
- **Don't name a member function `bit()`** (or any other Arduino.h macro:
  `bit`, `_BV`, `abs`, `min`, `max`, `round`, `degrees`,
  `radians` -- `degrees()` bit 2026-10-06). A header that compiles fine
  host-side in `pio test -e native` fails inside the firmware build with a
  baffling error pointing at Arduino.h itself, not at your code. Found
  2026-09-16 writing `navigation::MenuVisibility`.
- **One decoder library for every format** (ADR 0026): upstream
  ESP32-audioI2S on Arduino-ESP32 3.x (pioarduino). Positions are byte
  offsets for every format. Ogg positions stored before ADR 0026 were
  sample indices; the resume record (v3) and bookmarks (v2) reset those
  to the start of their part.
- **Legacy ESP-IDF 4 drivers abort at boot on the 3.x core.** The core
  links the new I2C and I2S drivers itself, and `driver/i2c.h` or
  `driver/i2s.h` next to them stops the boot loop with "CONFLICT!
  driver_ng is not allowed to be used with this old driver" (2026-10-05,
  the touch driver). Use `Wire`, or `driver/i2s_std.h`.
- **ESP32-audioI2S's `Audio` object must live in internal RAM.** The core
  is built with `CONFIG_I2S_ISR_IRAM_SAFE=1`, so IDF rejects an I2S
  callback context in PSRAM ("user context not in internal RAM",
  `ESP_ERR_INVALID_ARG`). The library passes `this` and wraps the call in
  `ESP_ERROR_CHECK`, so a heap-allocated `Audio` (PSRAM, because of
  `heap_caps_malloc_extmem_enable`) panicked on every first play: the
  screen went dark and the board rebooted to Home (2026-10-05). The driver
  allocates it with `MALLOC_CAP_INTERNAL`, and **zeroed**
  (`heap_caps_calloc`): the library is written for a global `Audio`, and
  its constructor leaves state uninitialised -- from plain malloc every M4A
  computed a nonsense header skip and ended at once, silently (found the
  same day by buffering the library's log and dumping the file's atoms). While a track plays, internal
  free drops from ~72 KB to ~31 KB, mostly the library's I2S DMA buffers
  (16 x 256 frames, 32-bit stereo).
- **The partition table is pinned** (`board_build.partitions =
  default_16MB.csv`). pioarduino's board file switched to
  `esp_sr_16.csv`; changing it would move NVS and lose settings and
  resume.
- **On the new toolchain `uint32_t` is `unsigned long`**, so
  `std::min(uint32_value, 31u)` no longer compiles; give the type
  explicitly (`std::min<uint32_t>`).
## Where things are documented (so you add to the right place)

- **Pure hardware facts** (pinout, board identification, electrical
  quirks) → `device.md`.
- **Architecture-level decisions and why** (navigation model, why a
  library was swapped, format decisions) → an ADR in `docs/adr/` (see
  `docs/coding-guidelines.md`'s Documentation section for when one's
  warranted).
- **A bug found and fixed during hardware bring-up** → the
  "Implementation status" narrative in the relevant ADR (currently ADR
  0004), in the order found, with root cause and fix -- not just the
  end state. That history is what saves the next debugging session.
- **A fact any agent should see before starting work at all** → this
  file, as a short pointer into the above -- not a replacement for them.
