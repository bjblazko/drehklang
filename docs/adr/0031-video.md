# 0031: Video, as a Motion-JPEG picture and an AAC sound

## Status

Accepted, 2026-10-13. Running on the device. Uses ADR 0030's way of
giving the panel to something other than LVGL.

## Context

The user asked for films on the device, in a top-level Video menu that is
off by default and lists `/Videos`. The ESP32-S3 has no video decoder,
not even a JPEG one, and its second core is half taken by the audio
library while sound plays.

Measured on the device (2026-10-13), 320 px wide pictures:

| Step | Time per frame |
|---|---|
| Read a frame from the SD card | 4.5 ms |
| Decode it with JPEGDEC | 20–22 ms |
| Push it to the panel | 17.5–18 ms |
| Budget at 25 fps | 40 ms |

## Decision

1. **Motion-JPEG at 320 px wide, 25 fps, in an AVI; the sound beside it
   as an M4A.** One JPEG per frame is what JPEGDEC, already in the
   firmware for covers, decodes. H.264 would need a software decoder
   and brings patent questions. The sound as its own file plays through
   the existing driver, so volume, equalizer and Bluetooth work as for
   music, and nothing has to demux it. `scripts/convert-video.sh` makes
   both. The picture stays under 1 GB, because past that ffmpeg writes
   OpenDML, which has no complete `idx1`.
2. **The sound is the clock.** A picture is due when the DAC has been
   handed that much sound (`outputFrames()` in the I2S hook, minus the
   DMA's 4096 frames), and it is dropped if it is already a frame late.
   Seeking and resuming go through `idx1`.
3. **Decode on core 1, push on core 0 above the audio decoder.** The
   loop decodes (`VideoPresenter::tick()`) into two 16-row stripes, and a
   task on core 0 at priority 3 pushes them. At priority 1 the push only
   got the gaps left by the library's decoder (prio 2, ~40 % of core 0):
   80 ms per frame, and 847 of 1498 frames were dropped. At priority 3
   all 1499 of 1499 were shown, and the sound has ~280 ms of buffers to
   catch up from. The CPU runs at 240 MHz while a film is open, like
   Circuit.
4. **The music's track is lent, not stopped.** The film's sound borrows
   the decoder (`playVideoSound()` / `endVideoSound()`). The track comes
   back parked with its position and timing, and `filePosition()` keeps
   answering for the music meanwhile, so the music's resume record is
   not overwritten.
5. **Controls like Now Playing's:** a tap pauses. Paused, the panel goes
   back to LVGL, which shows the picture as a still (decoded once more
   into PSRAM), the title, Play, and the time pill. Holding the pill and
   turning jumps a minute per detent, with the hint shown as in Now
   Playing; letting go plays on from there, and 0:00:00 is the start.
   There is no preview while turning. The jump reopens the sound at the
   byte that time maps to in the M4A's audio data
   (`soundOffsetAt()`). Otherwise the knob is the volume, and a swipe to
   the right leaves. The place is kept per
   film in NVS (the sound's byte offset and the frame shown,
   `video::soundKey()` / `frameKey()`), and the end clears it.
6. **Video starts hidden.** `MenuVisibility` learned entries that are
   hidden by default. Masks stored before had every unused bit set, so
   they are corrected once (`menuVisV` = 1). Switching Video on says
   "Convert first: see README".

7. **With Bluetooth headphones, every second picture, pushed by the
   loop.** Core 0 then also feeds the second chip, and the sound broke up
   on both outputs while the push task ran there. Over 30 s of film the
   second chip ran dry 14–22 times at prio 2–3, and 3 times at prio 1,
   where only 8.6 pictures a second got through. At 12.5 pictures a
   second, reading, decoding and pushing fit on core 1 (~44 of 80 ms):
   no underruns in 60 s, and no picture late. The picture also waits the
   ~150 ms the headphones lag behind the jack (ADR 0027).

## Consequences

- **A toy, on purpose** (user, 2026-10-13). This hardware can never make
  it a serious video player: no video decoder, a 320 px picture, both
  cores near full. The README says so.

- Core 0 runs at ~93 % during a film. Nothing else may be added there.
- The picture is pushed in stripes without waiting for the panel's
  refresh, so fast motion shows a little tearing.
- Films need converting on a computer. The README says how.
- **Later, maybe: `esp_new_jpeg`.** The factory firmware played full-screen
  MJPEG with Espressif's SIMD decoder, which ships in the Arduino core
  (`libesp_new_jpeg.a`). It is binary-only ("All rights reserved"), so the
  licence question of ADR 0026 has to be settled before it is used.
