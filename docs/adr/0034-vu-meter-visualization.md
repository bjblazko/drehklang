# 0034: VU and bar meters as a full-screen visualization

## Status

Accepted — 2026-11-03. Amends ADR 0014: the options panel holds four
buttons again (Shuffle, Repeat, Visuals, Lock).

## Context

The user asked for an analog VU meter full-screen, in the style of a
1990s AKAI deck, and a digital meter in the style of a 1990s Technics
deck. Both were to be graphic, with lamp light and shading, and both
would belong to a group of visualizations that might later include
Winamp-style ones. That made them a view of their own rather than another
page of Now Playing's cover slot (ADR 0028).

Two candidates were built as a live preview at device size, and the user
chose them as they were ("bitte genau so nehmen"). The preview is the
visual reference:
[`2026-11-03-vu-meter-preview.html`](../superpowers/specs/2026-11-03-vu-meter-preview.html).
Design: [`2026-11-03-vu-meter-design.md`](../superpowers/specs/2026-11-03-vu-meter-design.md).

Neither meter carries a brand name, a logo or a copy of a nameplate.

## Decision

### Entry and gestures

- A fourth button in the options panel, **Visuals** (the `equalizer`
  glyph ADR 0028 had left unused), opens the view. For spoken word, which
  has no Shuffle, it is the second of three.
- A tap switches to the next meter (analog, then bars, then analog again),
  a swipe goes back to Now Playing, and the knob sets the volume. The last
  meter chosen is kept (`vizKind`).
- A glyph at the bottom keeps the display awake while the view is open
  **and music plays** (`vizAwake`, off by default). Paused, the usual 60 s
  apply. It does nothing while the display is dark or locked, because
  holding it awake then would wake it.

### Levels

- `AudioOutputStage::noteStereo32()` measures every decoder block per
  channel, after the equalizer with its headroom given back and before
  the volume (`signal::StereoLevelMeter`): the sum of squares and the
  largest sample since the last read, so the RMS covers all of them. A
  block is 256 frames (5.8 ms at 44.1 kHz). One block per frame would
  swing with the phase of a bass note. Per sample the work is integer
  only (the top 24 bits squared into 64 bits), and the floats go into
  atomics once per block: no lock, no print, no allocation.
- Unread for 128 blocks, the meter stops measuring, and the next read
  throws away what it had. So it costs nothing while the view is closed,
  a film included, where core 0 has no time to spare (ADR 0031). An old
  peak also never lights the lamp when the view opens or the display
  wakes.
- The main loop takes them once per frame through
  `PlaybackDriver::readLevels()`. They are zero unless playing and zero
  while muted, the same as for the spectrum. `signal::LevelHold` repeats
  the last read for up to 250 ms when a frame got no block: at 8 kHz a
  block is 32 ms, about one per frame.
- The levels run ahead of the sound by the I2S DMA queue (16 × 256
  frames: ~93 ms at 44.1 kHz, ~0.5 s at 8 kHz) plus Bluetooth's latency,
  as the spectrum does.

### The analog meter

- One needle for L+R: a VU meter reads voltage, so the two channels'
  RMS are averaged linearly. 0 VU = −14 dBFS, and the scale ends at +3 VU.
- The ballistics are a second-order system with ω = 20 rad/s and ζ = 0.8,
  1.4 % overshoot, with stops at both ends that the needle bounces off a
  little (`visualizer::vu::NeedleBallistics`). This is the preview's
  tempo, which the user approved. It reaches 99 % in ~200 ms, a little
  quicker than IEC 60268-17's 300 ms; ω ≈ 13 would give the standard.
- The red PEAK lamp lights while either channel's sample peak is above
  −1 dBFS and fades out over 350 ms.

### The bar meter

- L and R as dBFS sample peaks on 32 segments, red from −3 dB, with
  instant attack, a release of 0.55 of the scale per second, and a peak
  held for 1.5 s before it falls (`visualizer::bars::BarMeter`).

### Drawing

- The static art is rendered by `scripts/vu-art/generate.mjs`, which uses
  the preview's own drawing functions (on `@napi-rs/canvas`, build-time
  only), into `lib/visualizer-art/VuMeterArt.c` as RGB565. It dithers the
  opaque images, or the lamp light bands on the panel, and seeds its
  random speckle, so every run writes the same bytes. The fonts it draws
  with (Archivo Narrow, Share Tech Mono, OFL-1.1) reach the firmware only
  as pixels. They are credited in Settings > Licences.
- Analog: the face, then a full-screen layer whose draw event paints the
  needle's shadow and the needle as two LVGL polygons, then the lamp
  sprite and the glass highlight (alpha) on top. A frame invalidates only
  the needle's old and new bounding boxes.
- Bars: the dark face, then four clip boxes (level and hold per channel)
  over two lit 360 × 54 bands. Moving a level changes a box's width, and
  only when its segment count changed.
- The art costs ~0.7 MB of flash (firmware 3.27 MB of 6.25 MB) and no RAM.

### Design rules

The meters, and their toggle, are exempt from the Theme.h rules (the UX
guidelines, §2.6). The toggle started as a Theme.h secondary button. On
the device the user found it disturbing, so it became a bare glyph in the
meter's own colours: cream at 30 % on the analog housing and lamp amber
when on; an unlit-segment teal on the bars and the legends' cyan when on.

## Checked on the device (2026-11-03)

- Both meters render as the preview, and the needle and bars follow the
  music (MP3).
- The first version measured in double precision on every sample, all
  the time. Software doubles cost the decoder's task ~8.7 points of core
  0 (58.7 % against 50.0 % with the integer version, MP3, found in review).
  Now, with the meter running (view open) against stopped (Now Playing),
  IDLE0 reads 31.7 % against 32.3 %, the decoder's task 50.0 % against
  50.1 % (`TASKS`). The glass over the needle costs nothing measurable.
- The swipe goes back, and the options panel shows four buttons.
- At volume 0 both meters stay still, as intended. This looked like a
  bug in the first screenshot.

## Consequences

- The options panel's Lock moved from third to fourth place.
  `scripts/readme-screenshots.py` taps it at (288, 244).
- `PlaybackDriver::readLevels()` has a default, so the tests' fake drivers
  did not change.
- A new meter means new art in `generate.mjs` (and in the preview), a
  value appended to `visualizer::Visualization`, and a render function.
