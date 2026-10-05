# 0027: Bluetooth headphones through the second chip

## Status

Accepted, 2026-10-05. Amends ADR 0026: Bluetooth is a tap behind the DAC's
owner, not a third owner.

Design: `docs/superpowers/specs/2026-10-05-bluetooth-headphones-design.md`.
Plan: `docs/superpowers/plans/2026-10-05-bluetooth-headphones.md`.

## Context

The ESP32-S3 has only BLE, and its controller cannot do LE Audio. The
board's second chip, the ESP32-U4WDH, has Classic Bluetooth (A2DP, AVRCP).
The two chips share one UART without flow-control lines (device.md).

The user asked for:

- **everything audible on the headphones:** music, spoken word, blips,
  games and the tone generator;
- **the jack playing in parallel;**
- **one volume:** the knob's;
- **one paired device** and **Play/Pause from the headphones only**;
- **auto-reconnect.**

## Decision

### A tap, not a DAC owner

ADR 0026 sketched Bluetooth as another `DacOwner` that swallows the
samples (`continueI2S = false`). With the jack playing in parallel it is a
**tap** instead. Whoever owns the DAC, the player or `ToneOutput`, copies
each chunk after the volume and the sleep timer's fade into
`bluetooth::AudioTap`, a lock-free ring of 8192 samples (16 KB) in PSRAM.
`DacArbiter` is unchanged, and there is only ever one source.

The tap costs no more than an owner would, and it is simpler:

- **Same hook work.** The samples are copied either way.
- **Same RAM.** The library creates its I2S channel whether or not samples
  reach it.
- **No back-pressure.** The DAC keeps pacing the S3, so the link needs no
  flow control. The U4WDH only absorbs the drift between the S3's DAC clock
  and Bluetooth's.

### The link

**Speed and pins.** UART at 3 Mbaud, 8N1. The S3 uses UART1 with TX on
GPIO48 and RX on GPIO38. The U4WDH uses UART2 with TX on IO18 and RX on
IO23. device.md had the S3's pins the other way round; a pin probe settled
it on 2026-10-05.

**Throughput test (2026-10-05).** Both directions ran saturated for
10 minutes at ~301 KB/s:

- 174 413 and 186 515 packets, with no CRC errors, no losses and no FIFO
  overflows.
- That needs **both receivers to lower their RX FIFO threshold to 64
  bytes**. At the driver's default (120 of 128) the U4WDH lost ~5 % of
  packets.

**Packets** (`lib/btlink/`, shared by both firmwares and the host tests):

```
0xD5 0x4B | type u8 | seq u8 | length u16 LE | payload | CRC-16/CCITT-FALSE LE
```

- AUDIO carries the sample rate as a u32, then 256 stereo frames of int16.
  At 48 kHz that is ~1.95 Mbaud.
- Control messages:
  - HELLO with the protocol version, both ways;
  - from the S3: ENABLE, SCAN_START, SCAN_STOP, PAIR, FORGET;
  - from the U4WDH: STATE (once a second, the heartbeat), SCAN_RESULT,
    BUTTON (Play or Pause, kept apart), STATS.

**Errors.** A receiver hunts for the next sync word after an error. Losses
show as gaps in the sequence numbers, and nothing is retransmitted.

### The U4WDH

The firmware is the PlatformIO project `bt/`, a project of its own because
PlatformIO has one `src_dir` per project. It uses the same pioarduino
release as the S3 and takes `btlink` and `btaudio` from `../lib`.

- **The stream.** `btaudio::HeadphoneStream` buffers the stream in 16384
  samples of internal RAM (~186 ms) and resamples it linearly to 44.1 kHz.
- **Clock drift.** The resampler's step is trimmed by how far the ring is
  from half full. A deadband of 10 % means one packet's arrival never moves
  the pitch.
- **Underruns.** A ring that runs dry plays silence until it is half full
  again.
- **SBC.** Bluedroid's A2DP source encodes SBC itself, from the PCM the
  legacy data callback renders.
- **AVRCP.** Only Play, Pause and Stop are used.
- **Pairing.** It is "just works" (SSP with no I/O, or PIN 0000). The
  paired address and name live in the chip's own NVS.
- **Reconnect.** Every 10 s while enabled.
- **Hands off.** It does not touch IO32 (XSMT), its own knob or the
  CH445P.

### The S3

- **`BtLink`** runs a task on core 0 that sends the tap's packets and
  queued control packets, and queues what arrives for the main loop.
- **`BtController`**, on the main loop, handles:
  - the handshake: no answer in 3 s means "Firmware missing", another
    version means "Firmware outdated";
  - the heartbeat: 2 s of silence means "Not answering";
  - the state, the scan list and a 15 s pairing timeout;
  - the events for the UI.
- **Self-healing.** A STATE that disagrees with the enabled setting resends
  ENABLE, so a restarted U4WDH comes back by itself.
- **Persistence.** Bluetooth is off until the user turns it on. That is
  stored in NVS as `btOn`.

### The UI

- **Settings > Bluetooth** is a list: the switch, the paired headphones
  with their state, Find headphones and Forget headphones.
- **Without Drehklang firmware on the U4WDH**, it says so instead of
  offering a switch that would do nothing.
- **The search** is a list sorted by signal strength, led by "Search
  again".
- **Messages** say when headphones connect, disconnect, or cannot be
  connected to.
- **On the lock screen** a quiet mark sits beside the battery: Material's
  "sensors", not the Bluetooth logo (see Open, legal check).
- **Play/Pause from the headphones** works like Now Playing's button in
  the music and like start/stop on Tones, and does nothing in a game. A
  repeated Play never pauses.
- **Settings rows** now carry their value in the row table, so Brightness's
  percentage is no longer a special case.

## Consequences

- **Latency.** The jack runs ~150 ms ahead of the headphones.
- **Internal RAM on the S3:**
  - the link task's 3 KB stack;
  - the UART driver's buffers (1 KB RX, 2 KB TX);
  - two 16-entry control queues (~1 KB).

  The packet buffers and the ring are in PSRAM. The WiFi phase has to fit
  next to this.
- **Factory image.** A U4WDH running its factory image shows "Firmware
  missing", and everything else works as before.
- **Aliasing.** There is no low-pass before downsampling, so content above
  22 kHz in a 96 kHz file aliases.
- **Underrun count.** An underrun is also counted every time the S3 pauses,
  because the ring runs dry by design. Read STATS during continuous
  playback.
- **Before the first binary release, run the legal check.** It covers SBC,
  the Bluetooth trademark and Bluetooth SIG qualification, next to ADR
  0026's HE-AAC decision.

## Verified on the device (2026-10-05)

Tested with SOUNDPEATS Q headphones, in the order the problems were
found:

1. **The jack went silent** as soon as the U4WDH ran anything but its
   factory image (music, radio play and the tone generator, with either S3
   build).
   - A real power cycle changed nothing.
   - Flashing the pre-Bluetooth S3 build changed nothing either.
   - **Cause:** IO32 is the DAC's XSMT (active low), and the factory image
     drove it high.
   - **Fix:** `bt/src/main.cpp` drives IO32 high first thing. Recorded in
     device.md and AGENTS.md.
2. **Pairing, playback over Bluetooth with the jack in parallel, tones,
   the headphone button and reconnecting all work.**
   - After a few minutes of music: U4WDH underruns 0, CRC errors 0,
     packets lost 0, tap drops 0.
3. **Table Tennis's paddle blips (24 ms) did not arrive, or arrived many at
   once,** while the 240 ms score sound did.
   - **Cause:** a lone blip never fills the U4WDH's buffer to the half it
     starts playing from.
   - **Fix:** while the tone output owns the DAC and headphones are
     connected, it writes silence between sounds, so the stream keeps
     running.
   - Each blip now arrives, with a constant delay.
4. **Internal free heap while playing over Bluetooth is 21.5 KB**, against
   ~31 KB without it (ADR 0026), so Bluetooth costs ~10 KB of internal RAM.
   The WiFi phase has to fit into what is left.

5. **Fixes from the final review, checked on the device:**
   - Discovery now starts and is reported as running.
   - Long names are cut at a character boundary.
   - A search tap is resolved by address.
   - Settings no longer jumps to the top every 10 s.
6. **Marshall Major V**, pairing again from the search (the logs on the
   U4WDH made this visible):
   - A pairing could stay "Connecting" for good.
   - With the old key kept, authentication of headphones in pairing mode
     failed after 30 s.
   - A 20 s safety timeout cut into a pairing still in progress and left
     A2DP stuck.
   - **Fixes:** pairing from the search always removes the bond first, a
     refused connect is Idle again, and the safety timeout is 60 s.
   - Pairing, reconnecting after the headphones are switched off and on,
     and reconnecting after pairing mode all work.

## Open

- **Legal check (2026-10-05) before merging.**
  - **Licence:** the Bluetooth firmware links Espressif's binary-only
    libraries, so its files carry a GPL section 7 linking permission
    (LINKING-EXCEPTION.md).
  - **Still open for the first binary release:** the S3 firmware links the
    same kind of libraries together with ESP32-audioI2S (decide together
    with HE-AAC, ADR 0026), and Bluetooth qualification for devices passed
    on, since the patent and trademark licences of the Bluetooth SIG cover
    qualified products.
  - **Bluetooth logo:** the repository is public, so the lock screen no
    longer uses the logo (a Bluetooth SIG trademark) but Material's
    "sensors" mark: sound sent over the air, generic, and kept
    apart from a later Wi-Fi symbol (2026-10-05).
  - **No binary releases.** Drehklang is distributed as source only and
    built on the user's machine (`scripts/install.py`), so the two points
    above wait until binaries are passed on.
  - **About** now says that sound goes to the headphones.

- **Delay on the headphones in games.** Sound reaches the headphones
  ~300 ms after the jack:
  - Bluetooth itself (SBC and the headphones' buffer) accounts for
    ~150-250 ms.
  - The U4WDH's half-full buffer adds ~190 ms at 22.05 kHz, ~95 ms at
    44.1 kHz.

  Delaying the game itself to match was rejected: a reaction game would
  feel sluggish. Shrinking the U4WDH's buffer target would cut up to
  ~140 ms in games, at a higher risk of underruns, which would need
  measuring on the device. Left as it is for now (the user's decision,
  2026-10-05).
