# Bluetooth headphones — design

*2026-10-05. Approved in a planning session with the user; decisions below
were each asked and answered.*

## Why

ADR 0026 put Bluetooth headphones first on the roadmap and asked for a
design spec before any code. The ESP32-S3 has only BLE, and its controller
cannot do LE Audio, so no S3 firmware can drive headphones. The board's
second chip, the ESP32-U4WDH, has Classic Bluetooth (A2DP, AVRCP). It gets a
firmware of its own, and the S3 feeds it PCM over the UART the two chips
share (device.md, "The second chip and the audio switch"):

- S3 GPIO48 (TX) → U4WDH IO23 (RX)
- U4WDH IO18 (TX) → S3 GPIO38 (RX)
- no RTS/CTS lines

Measured on 2026-10-05: 3 Mbaud works both ways once both receivers
lower their RX FIFO threshold to 64 bytes (device.md).

## What the user gets

- **Everything audible goes to the headphones:** music, audiobooks and
  radio plays, and also blips, games and the tone generator.
- **The jack keeps playing in parallel.** It runs about 150 ms ahead of
  the headphones. That only matters to someone listening to both at once,
  and the user accepted it.
- **One volume.** The knob sets one value. The headphones get the samples
  after the volume and the sleep timer's fade, exactly like the jack. AVRCP
  absolute volume is not used.
- **One paired device.** Pairing a new one replaces it.
- **One headphone button:** Play/Pause.
- **Auto-reconnect** after boot and after the connection drops, every 10 s
  while Bluetooth is on.

Out of scope for v1:

- several remembered devices
- Next/Previous, volume or FF/RW buttons
- AVRCP absolute volume
- updating the U4WDH from the S3 (OTA)
- microphone or HFP

## Deviation from ADR 0026

ADR 0026 sketched Bluetooth as a second `DacOwner` behind
`playback::DacArbiter` that swallows the samples (`continueI2S = false`).
With the jack playing in parallel, Bluetooth is a **tap** instead:
`continueI2S` stays `true`, and `DacArbiter` is unchanged. Whoever owns the
DAC feeds the tap, so there is only ever one source. The tap is no more
work than an owner would be:

- **Same hook work.** The samples are copied to the link either way.
- **Same RAM.** The library creates its I2S channel whether or not
  samples reach it, so swallowing them would save nothing.
- **Simpler flow control.** As an owner, nothing would pace the decoder,
  and the link would need back-pressure without RTS/CTS lines. As a tap,
  the DAC keeps pacing the S3, and the U4WDH only absorbs the small clock
  drift between the DAC and Bluetooth.

ADR 0027 records this.

## Interaction

UI text is English, like the rest of the device. Settings stays a list
(ux-guidelines §5), and nothing here needs a new widget.

**Settings row "Bluetooth"** ends in its value as plain text:

| State | Value |
|---|---|
| Off | `Off` |
| On, not connected | `Not connected` |
| Connected | the device's name, e.g. `WH-1000XM4` |
| No or outdated BT firmware | `Firmware missing` / `Firmware outdated` |

**The Bluetooth screen** is a list:

```
          ‹                     back
      Bluetooth                 caption
  Bluetooth             On      tap toggles; On/Off as plain text, like Main menu's
  WH-1000XM4     Connected      paired device and its state:
                                Connected / Connecting… / Not in range
  Find headphones          ›    opens the search
  Forget headphones             only while a device is paired
```

- With no paired device, the device row is left out.
- **No or outdated firmware:** the screen shows only a text, either
  `Bluetooth chip has no Drehklang firmware.` or `Bluetooth chip firmware
  is outdated.`, followed by `Flash it with scripts/flash-bt-mcu.sh.`
  There is no toggle that would then do nothing (ux-guidelines §2.6,
  honest).
- **Chip not answering** after it had answered: the screen shows
  `Bluetooth chip not answering.` until it answers again.

**The search** (pushed from "Find headphones"):

- It scans for 10 s and lists devices by signal strength, strongest first.
- The caption reads `Searching…` while scanning and `N found` afterwards.
- The first row is `Search again`, which is greyed out while a scan runs.
- With nothing found, a message says `None found. Pairing mode on?`.
- **Pairing:**
  - Tapping a device pairs it and returns to the Bluetooth screen, with
    the message `Connecting to <name>`.
  - If the connection fails within 15 s, the message reads `Could not
    connect to <name>`, and the old pairing is already gone.
  - Leaving the search stops the scan.

**Status** is shown only when it matters (ux-guidelines §6):

- Connecting and disconnecting show a message through `MessageTimer`:
  `Headphones connected` and `Headphones disconnected`.
- The lock screen shows a quiet Bluetooth glyph in `structure` next to the
  battery while headphones are connected.
- There is no permanent icon elsewhere.

**Play/Pause on the headphones:**

| Where | Effect |
|---|---|
| A track is loaded (playing, paused or parked) | Same as the Play/Pause button on Now Playing |
| Tones screen | Starts or stops the tone |
| A game, or no track loaded | Nothing |

**Persistence:**

- The S3 keeps "Bluetooth on/off" in NVS and sends it to the U4WDH after
  the handshake.
- The U4WDH keeps the bonding keys (Bluedroid's own storage) and the
  paired address (its own NVS).

## Architecture

```
ESP32-S3 (Drehklang)                               ESP32-U4WDH (bt/)
──────────────────────────────────                 ─────────────────────────────────
Esp32AudioI2SDriver                                 LinkReceiver (UART, IO23/IO18)
  audio_process_i2s ──┐                              ├─ audio ─► ring (~32 KB internal)
ToneOutput            ├─► BtAudioTap ─► BtLink ═════►│          ─► AdaptiveResampler
  after each chunk ───┘   16-bit ring    UART1      │             (source rate → 44.1 kHz,
                          16 KB PSRAM    3 Mbaud    │              holds the ring at 50 %)
                                         GPIO38/48  │          ─► A2DP source callback (SBC)
BtController (main loop) ◄═ STATE, SCAN_RESULT, ◄═══ └─ control: scan, pair, forget,
  └─ ScreenManager: Settings > Bluetooth   BUTTON       AVRCP Play/Pause, reconnect, NVS
```

### `lib/btlink/` — the protocol, shared by both firmwares and `native`

Pure C++, no Arduino. Namespace `drehklang::btlink`.

**Packet**, same in both directions:

```
| 0xD5 0x4B | type u8 | seq u8 | length u16 LE | payload | CRC16-CCITT |
```

- The CRC covers type, seq, length and payload.
- `seq` counts per direction and wraps at 255.
- A payload is at most 1100 bytes.

**Audio** (S3 → BT):

- The sample rate as u32 LE, so any source rate gets through (a 32 kHz
  MP3, a 96 kHz FLAC).
- Then 256 stereo frames of 16-bit little-endian PCM, 1024 bytes.
- At 48 kHz that is about 188 packets/s. With framing it comes to
  1.95 Mbaud at 8N1, which leaves about 35 % headroom at 3 Mbaud.

**Control, S3 → BT:**

| Type | Payload |
|---|---|
| `HELLO` | protocol version u8 |
| `ENABLE` | on/off u8 |
| `SCAN_START` | – |
| `SCAN_STOP` | – |
| `PAIR` | Bluetooth address, 6 bytes |
| `FORGET` | – |

**Control, BT → S3:**

| Type | Payload |
|---|---|
| `HELLO` | protocol version u8 |
| `STATE` | state u8 (off, idle, scanning, connecting, connected), paired address (6 bytes), paired name (u8 length + UTF-8, at most 32 bytes); sent on every change and once a second, as the heartbeat |
| `SCAN_RESULT` | address, RSSI i8, name (u8 length + UTF-8); one packet per device found |
| `BUTTON` | button u8: 1 = Play, 2 = Pause. Kept apart: a toggle would undo a press the headphones repeat |
| `STATS` | underruns u32, CRC errors u32, packets lost u32; every 5 s |

**The receiver:**

- After a CRC error or an unknown type, it hunts for the next sync word.
- A gap in `seq` counts as lost packets. The resampler bridges the gap,
  and nothing is retransmitted.

`PacketWriter` and `PacketReader` (a byte-at-a-time state machine) are
host-tested.

### S3: `lib/bluetooth/` — namespace `drehklang::bluetooth`

**`BtAudioTap`** (host-tested apart from where it is called):

- Called from `audio_process_i2s` (`lib/drivers-audio/AudioHooks.cpp`)
  after the fade and the hold, so it gets exactly what the jack gets.
- Also called from `ToneOutput` after each chunk it writes, with that
  chunk's rate.
- Converts 32-bit samples to 16-bit and writes them with their rate into a
  lock-free single-producer, single-consumer ring of 16 KB in PSRAM
  (about 85 ms at 48 kHz). PSRAM is fine because both callers are tasks,
  not ISRs.
- Returns at once while not connected. When the ring is full it drops the
  new samples. It never blocks the decode task.

**`BtLink`** (driver, `lib/drivers-bluetooth/`):

- Runs its own FreeRTOS task on core 0 on UART1 at 3 Mbaud, with GPIO48
  as TX and GPIO38 as RX, RX FIFO threshold 64.
- Cuts the ring into audio packets, sends queued control packets between
  them, and feeds received bytes to a `PacketReader`.
- Internal RAM: a 2 KB UART TX buffer, a 1 KB RX buffer and a 3 KB task
  stack, about 6 KB in total. The WiFi phase has to fit next to this
  (ADR 0026: about 31 KB internal free while playing).
- Never prints from its task (AGENTS.md, "Never `Serial.printf()` from the
  audio task").

**`BtController`** (main loop, host-tested):

- States: `Starting` (the first 3 s, before any answer), `Off`,
  `FirmwareMissing`, `FirmwareOutdated`, `NotAnswering`, `Idle`,
  `Connecting`, `Connected`. Scanning is a flag beside the state, because
  a scan can run while connected.
- **Handshake:**
  - After boot it sends `HELLO` every 500 ms.
  - No answer within 3 s → `FirmwareMissing`.
  - A different version → `FirmwareOutdated`.
  - After a successful handshake it sends `ENABLE` from NVS.
- **Heartbeat:** 2 s without any packet from the U4WDH → `NotAnswering`,
  and it goes back to sending `HELLO`.
- **Gating:** tells `BtAudioTap` whether to forward, which is true only in
  `Connected`.
- **Play/Pause:** turns `BUTTON` into the action in the Interaction table
  above, through the same entry point Now Playing's button uses.
- **For the UI:** keeps the scan list and posts the connect and disconnect
  messages.

**UI.**

- `ScreenManagerBluetooth.cpp` holds the Bluetooth screen and the search,
  like `ScreenManagerToneGenerator.cpp`.
- A new Settings row is appended to `kSettingsRows` in
  `lib/ui/ScreenManager.h`, before About, so About stays the last row.
- The lock-screen glyph comes from the icon font (regenerate with
  `--no-compress`, AGENTS.md).

### U4WDH: the `bt/` sub-project

**Build:**

- A PlatformIO project of its own: `bt/platformio.ini`, env `esp32-bt`.
- The same pioarduino platform as the S3 (55.03.312-1, Arduino-ESP32
  3.3.12 on ESP-IDF 5.5.5), board `esp32dev`, 4 MB.
- Uses `lib/btlink/` through `lib_extra_dirs`.
- It is a sub-project rather than a second env in the root
  `platformio.ini`: PlatformIO has one `src_dir` per project, and filtering
  sources across directories per env would be fiddly.

**Code:**

- **Bluetooth:** Arduino's `setup()`/`loop()`, with Bluetooth through the
  ESP-IDF Bluedroid API directly (`esp_a2dp_api.h`, `esp_avrc_api.h`,
  `esp_gap_bt_api.h`), not through an extra library.
- **`LinkReceiver`:** UART2 on IO23 (RX) and IO18 (TX) at 3 Mbaud. It feeds
  audio into a ring of about 32 KB in internal RAM (the chip has no
  PSRAM), about 180 ms at 44.1 kHz.
- **`AdaptiveResampler`** (host-tested, in `bt/lib/`):
  - Linear interpolation from the source rate (22.05, 44.1 or 48 kHz) to
    44.1 kHz.
  - The step is trimmed by a slow controller that holds the ring at 50 %
    fill, which absorbs the clock drift between the S3's DAC and
    Bluetooth.
  - A rate change resets it. Linear interpolation is enough for tones and
    speech, and most music is already 44.1 kHz.
- **A2DP source:** Bluedroid calls the data callback and encodes SBC
  itself. While the ring is below its threshold the callback returns
  silence (see the error cases below).
- **AVRCP:** Play/Pause from the headphones becomes a `BUTTON` packet.
- **Reconnect:** while enabled and paired but not connected, it tries the
  paired address every 10 s.
- **Hands off:** it leaves IO32 (DAC XSMT), its own knob (IO19/IO22) and
  the CH445P alone. The S3 keeps the DAC.

## Error cases

| Case | Behaviour |
|---|---|
| U4WDH ring runs empty | It sends silence and resumes only once the ring is back at 50 %, so there is no crackle |
| S3 ring full | New samples are dropped; the decode task never blocks |
| Headphones gone (out of range, switched off) | The jack keeps playing; `Headphones disconnected`; reconnect every 10 s |
| U4WDH silent for 2 s | `NotAnswering`; the S3 resends `HELLO` until it answers |
| Paused or stopped | No audio packets; the U4WDH sends silence and keeps the A2DP stream open (some headphones switch off otherwise) |
| Factory image on the U4WDH | `FirmwareMissing`; the rest of the device works as before |

## Flashing the U4WDH

- **`scripts/flash-bt-mcu.sh`:**
  - Finds the CH340 port.
  - Checks with `esptool chip_id` that an ESP32 answers there, not the S3.
    If the S3 answers, it says to flip the USB-C cable (device.md,
    "USB-C cable orientation matters").
  - Builds and uploads `bt/`.
- **Back to the factory image:** `hardware-backups/restore.sh secondary`.
- The protocol version in `HELLO` is what tells the S3 that the U4WDH
  needs reflashing after a protocol change.

## Docs

- **ADR 0027:** Bluetooth as a tap rather than a DAC owner, the link
  protocol, the `bt/` sub-project, and what the baud test measured.
- **device.md:** the measured UART speed. Also which level of GPIO0
  selects which side of the CH445P, if that gets measured along the way.
- **AGENTS.md:** short pointers, such as how to reach the U4WDH, the
  flash script, and that the tap must never block.
- **ux-guidelines §5/§6:** the Bluetooth settings and the lock-screen
  glyph.
- **About > Licences:** a credit for ESP-IDF/Bluedroid (Apache-2.0) as
  used by the BT firmware; `test_credits` keeps it honest.

## Tests (TDD, `pio test -e native`)

- `btlink`:
  - encode and decode round trip for every type
  - a wrong CRC is rejected
  - resync after garbage bytes and after a packet cut short
  - `seq` gaps counted, including the wrap at 255
  - the length cap
- `AdaptiveResampler`:
  - 48 → 44.1 kHz gives the exact output count over 1 s
  - 44.1 → 44.1 kHz is bit-exact
  - with the consumer ±200 ppm off, the fill settles near 50 % and never
    under- or overruns over 10 simulated minutes
- `BtAudioTap`:
  - 32 → 16-bit conversion
  - drops when full or gated off
  - carries the rate
- `BtController`:
  - handshake timeout leads to `FirmwareMissing`
  - a version mismatch leads to `FirmwareOutdated`
  - heartbeat loss leads to `NotAnswering`, and recovers
  - `ENABLE` is sent after the handshake
  - `BUTTON` is routed correctly in each context
  - the scan list is ordered by RSSI

## Verification on the device

In this order.

0. With the U4WDH running anything but its factory image, music still
   reaches the jack. The U4WDH's IO32 drives the DAC's XSMT; if the jack
   is silent, the BT firmware must drive IO32 high.
1. **Baud test. Done 2026-10-05:** 10 minutes at 3 Mbaud, both directions
   saturated, no errors (with the RX threshold at 64). What was planned: A throwaway sketch on each chip sends 3 Mbaud of
   counter packets across GPIO38/48 ↔ IO23/IO18, and the test counts CRC
   errors over 10 minutes. The goal is zero. If 3 Mbaud fails, try
   2 Mbaud (1.95 Mbaud is needed at 48 kHz). If both fail, go back to
   this design before writing anything else.
2. `HELLO` handshake both ways. With the factory image, the S3 shows
   `Firmware missing`.
3. Pair real headphones from the search.
4. 60 minutes of music over Bluetooth with the jack connected. `STATS`
   shows no underruns after the start, and nothing is heard dropping out.
5. The tone generator (48 kHz) and a game (22.05 kHz) are heard on the
   headphones.
6. Switch the headphones off and on: they reconnect within about 10 s,
   with both messages.
7. Play/Pause on the headphones in Now Playing, in Tones and in a game.
8. Internal free heap while playing with Bluetooth connected, compared
   with the ~31 KB baseline.

## Open before a binary release

Per the user's global instructions, run `huepattl-legal-check` before the
first binary or flashed device is passed on. That check covers the new BT
firmware and its components, SBC, and the Bluetooth trademark and SIG
qualification. It sits next to ADR 0026's HE-AAC decision.
