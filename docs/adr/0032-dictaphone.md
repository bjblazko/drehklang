# 0032: Dictaphone, the microphone, and sharing I2S0 with it

## Status

Accepted — 2026-10-14. The second of the three tools ADR 0024 cut its
signal layer for (tone generator, voice recorder, analyzer). Adds a ninth
main-menu entry, which widens ADR 0018's visibility mask, and a third
owner to ADR 0026's DAC handover.

## Context

The user asked for a dictaphone as a top-level function: record from the
internal microphone to the SD card in a simple format, list the
recordings, play them back, and show the band (scope and spectrum) exactly
as the tone generator does, live even while nothing is being recorded.
A Bluetooth microphone only if it came easily.

What the board decides:

- **The microphone is a PDM MEMS part on GPIO 45 (CLK) / 46 (DATA)**,
  untested before this ADR. A spike (2026-10-14, `device.md`) found it
  answering on the **left** PDM slot only, through the S3's PDM-to-PCM
  filter, with a DC offset of ~1375 and speech at arm's length around
  -45..-33 dBFS RMS. It needs a DC filter and gain.
- **PDM RX exists on I2S0 only**, the port the player and the tone output
  already hand over through `playback::DacArbiter` (ADR 0026).
- **The U4WDH is an A2DP source only** (ADR 0027): nothing comes back from
  the headphones. A Bluetooth microphone would mean HFP on the second
  chip, a new link message and a second codec path — not "easy", so it
  is left out.
- **There is no clock** (`BtLogFile.h`): recordings cannot be named by
  date. They are numbered.

Each decision below was put to the user as a question before any code
was written.

## Decision

### The microphone is a third owner of I2S0

`drivers::MicInput` is a `DacOwner` like the player and `ToneOutput`: it
claims the port when the recorder opens, which parks a playing track the
same way a tone does, and closes its PDM channel in `releaseDac()`. The
arbiter's job is now "one owner of I2S0", not only of the DAC. Moving the
outputs to I2S1 instead would let music play on while recording, but it
touches the two hardware-verified output paths for something nobody asked
for: a recording made over music records the room, not the music.

The microphone's task (core 0, prio 2, like the tone task) reads 20 ms
blocks, removes the DC with a one-pole high-pass, applies a fixed gain
and clips (`dictaphone::MicConditioner`, host-tested), and stores every
sample in the output stage's ring. The band reads that ring like it reads
the tone generator's, so `SignalBand` needs no change.

### WAV, 16 kHz, 16 bit, mono

The decoder library already plays WAV, a Mac opens it without help, and
16 kHz is voice quality at 1.9 MB a minute. The spectrum's axis stays at
20 Hz – 20 kHz as on the tone generator ("exactly as"), so it is empty
above 8 kHz.

The file is written with a placeholder header whose sizes are patched
when recording stops. A recording cut off by a reset keeps a header with
zero sizes; `dictaphone::WavHeader` takes the sizes from the file's length
when it reads one back, so the recording still plays in full.

### SD writes never block the microphone

The microphone task hands samples to `btaudio::PcmRing` (Bluetooth's
lock-free ring, reused; 4 s in PSRAM);
`drivers::SdWavWriter`'s own task (core 0, prio 1) drains it in 4 KB
writes. A write that fails (card full, card pulled) stops the recording,
patches the header with what reached the card, and the screen says so.

### Playback has its own small player

`PlaybackStateMachine::play()` would replace the music's queue and its
resume record with a recording. `drivers::RecordingPlayer` instead reads
the PCM itself (32 KB/s), applies the volume with `AudioGain`, writes to
its own I2S0 channel as a `DacOwner`, notes its samples for the band and
copies them to the Bluetooth tap, as `ToneOutput` does.

### Names

`/Recordings/REC0001.wav`, shown as "Recording 1", newest first. The next
number is the highest present plus one (`dictaphone::RecordingShelf`), so
nothing is overwritten and no number is stored anywhere.

### Screens

Three `ScreenKind`s, appended, never restored on resume:

- **Dictaphone** — the recorder, opened from the menu. The band as on the
  tone generator (same size, same swipe between scope and spectrum), the
  time recorded with a yellow dot beside it while it runs (time passing;
  yellow text would not read on the light surface), the name the next
  recording will get, and a row of three: **List** on the left,
  Record/Stop as the one primary button (72 px) in the middle, and on the
  right **Pause**, shown only while recording, which holds the recording
  without closing its file (the time stands still, the dot goes) and
  becomes a dot to go on. After Stop the same place plays back the
  recording just made, right there (user request, 2026-10-14): the player
  takes I2S0 from the microphone, the band and the time ("0:01 / 0:04")
  follow the playback and the name shows that recording; at its end, or
  when Record starts the next one, the microphone takes over again.
  Leaving the recorder forgets which recording that was. The row keeps the primary button on the
  screen's axis with a secondary one either side: one secondary button
  beside it alone read as off-centre (user feedback, 2026-10-14). The
  microphone runs while the screen is open.
  Leaving it stops and saves a recording; the display going dark or the
  lock does not.
- **Recordings** — the generic list, like Video's, each row ending in the
  recording's length. The length comes from the file size the directory
  listing already has (header plus 32 KB a second), not from opening each
  file: many opens straight after a listing is what upset the SD card
  before (AGENTS.md).
- **Recording** — one recording: the band fed by the player, position and
  length, and the same row: Delete on the left (it asks first in a modal
  dialog, Cancel / Delete), Play/Pause in the middle, five seconds back on
  the right (Material Symbols' "replay_5"), short enough to catch the last
  words again while typing a dictation up. The knob is the volume,
  as on Now Playing.

The USB drive is started from Settings, so the recorder is never open
while the card is exported; starting it is refused for the moment a
stopped recording is still being finished.

The three new tasks (microphone, writer, player) keep their stacks in
PSRAM. With internal stacks they cost 12 KB of internal RAM even when
the dictaphone is never opened, and music then played with 26 KB internal
free instead of ~31 (AGENTS.md: the player's I2S buffers need it). With
PSRAM stacks: 79 KB free after boot, 75 KB while recording, 72 KB while a
recording plays (2026-10-14). The microphone and the writer together
took ~2 % of core 0 while recording.

### The main menu gets a ninth entry

`navigation::MenuVisibility` becomes a 16-bit mask under a new NVS key;
the old byte is migrated and keeps the user's choices. The new entry is
visible by default (the user's choice). Its icon is Material Symbols'
"mic", added to the existing icon font.

## Consequences

- Opening the dictaphone pauses music, as the tone generator does.
- The microphone's gain is a constant measured on this board's part; a
  different microphone needs a new spike.
- Nothing records in the background of other screens.
- A Bluetooth microphone remains possible later, behind the same
  `SampleSource`.

## Implementation notes

- **Holding a lock across a blocking I2S call starved the main loop**
  (found on the device 2026-10-14, with Bluetooth headphones connected).
  The microphone's and the player's tasks held their mutex while
  `i2s_channel_read`/`write` blocked, and took it back at once. The main
  loop on the other core, waiting for the same mutex in `close()` (leaving
  the recorder) or `rewind()`, got it only seconds later. The UI froze while the
  recording played on, the Bluetooth link's state went stale ("disconnected",
  "connected" while the headphones stayed connected), and the tap that
  opened the list was taken again by the next screen's mini-bar. Both tasks
  now sleep one tick outside the lock after every block; the DMA buffers
  hold ~90 ms. Measured afterwards: the main loop answered within 10 ms of
  each rewind, and the list opened 5 times out of 5 (before: 0 of 3).
