# VU meters in a full-screen visualization view — design

*2026-11-03. Worked out with the user in a planning session. Marked "my
call" where the user did not decide something themselves.*

The visual reference is
[`2026-11-03-vu-meter-preview.html`](2026-11-03-vu-meter-preview.html).
The user saw it live and said "bitte genau so nehmen". The device should
look like that page: palette, lamp light, glass, fonts, scale layout,
needle tempo and segment behaviour.

## Why

The user wants a full-screen visualization over music, in the same way as
the full-screen cover (ADR 0033). It starts with two meters:

- **Analog**: a moving-coil VU meter in the style of a 1990s AKAI deck.
  It has a cream scale, warm backlighting from two lamps behind it, glass
  and a needle that casts a shadow.
- **Digital**: a fluorescent (FL) bar meter in the style of a 1990s
  Technics deck. It has cyan-blue segments with red at the top, faintly
  visible unlit segments and the fine mesh in front of the glass.

Later the view will hold more visualizations (the user mentioned
Winamp-style ones). That is why it has its own button and is not a slot
page.

## Goals

- Both meters look like the preview: graphic, with shading from the
  lamps, not flat.
- The analog meter has **one needle for L+R**: the linear RMS of both
  channels, averaged. It uses **true VU ballistics**: ~300 ms to 99 %,
  1–2 % overshoot, mechanical stops at both ends. The preview models this
  as a second-order system with ω = 20 rad/s and ζ = 0.8.
- The analog meter has a red **PEAK** lamp. It lights when either
  channel's sample peak exceeds −1 dBFS and fades over ~350 ms.
- The digital meter shows **L and R separately** as dBFS sample peaks. It
  attacks instantly, falls at about 0.55 of the scale per second and holds
  each peak for 1.5 s. The scale runs over 32 segments, from 50 to 0 dB,
  red from −3 dB (scale points in the preview's `SCALE` table).
- 0 VU = −14 dBFS (my call, shown to the user as an assumption; a single
  constant).
- A small toggle keeps the display awake (see below).

## Non-goals

- No other visualizations yet, no Winamp-like ones.
- No brand names, logos or recreated nameplates on either meter.
- No user-adjustable reference level, no choice of ballistics.
- No meter for the tone generator, the dictaphone or games, which are
  other DAC owners. The view belongs to the music player's Now Playing,
  across all collections.

## Anti-goals

- The meters must not look flat or "themed". Snow White, Theme.h colours
  or Rams rules applied to them would be a failure (see the design-rule
  exemption below).
- Music must never stutter because of the view. If it does, that is a
  failure even if everything looks right.

## How the user gets there and what they can do

- **Entry**: a fourth button, **"Visualisierung"**, in Now Playing's
  options panel (ADR 0014, now Shuffle, Repeat, Lock). It opens the view
  full-screen, the way the cover view opens. The user decided this. Its
  glyph is the `equalizer` icon that ADR 0028 left unused in
  `IconFont.c` (my call). The panel held four buttons before ADR 0028, so
  the row fits.
- **Tap** anywhere except the toggle switches to the next instrument:
  analog, then digital, then analog again. The list grows later.
- **Swipe** goes back to Now Playing, as on the cover view. Only taps
  switch instruments; a drag never does.
- **Knob** sets the volume, with no ring, as on the cover view.
- **Remembered**: the last instrument and the toggle's state survive a
  reboot (NVS).
- **Keep-awake toggle**: a small round control at the bottom edge,
  inside the circle. The user asked for it and agreed to these details:
  - Off (the default): the display goes dark after the usual 60 s
    (`IdleTimer`).
  - On: the display stays on while the view is open **and music is
    playing**. Paused, the normal timeout applies again.
  - On, it shows green like Shuffle's `confirm` state. It is the one
    regular UI element on the screen and follows Theme.h. Off, it is a
    light-surface circle like the cover view's pills, so it reads on both
    the cream and the black face (my call). It needs a
    44 px touch target and an accessible label ("Display bleibt an").
- **Pause or mute**: the needle falls back with its ballistics, the
  segments go out with their release, and the peak hold expires.
- **Track change**: the view stays open.
- **Playback ends and Now Playing closes**: the view closes with it, as
  the cover view does (my call).
- **Lock**: locking from the panel works as before. Nothing in the view
  locks.
- **Bluetooth headphones**: the meter works the same way. The levels come
  from the decoder, independent of the output.

## Signal

- Measure per channel in `AudioOutputStage::noteStereo32()`, which already
  sees interleaved stereo before the volume (ADR 0026). Accumulate the sum
  of squares and the max |sample| per channel, and publish them through
  atomics. The UI reads and resets them once per frame. The level is the
  track's own, after the EQ with its headroom compensated, before the
  volume (my call: what the listener hears, minus the knob).
- The audio task must never print, lock or allocate for this (AGENTS.md).
  Its cost must stay negligible next to the ~40 % decode load on core 0.
- Ballistics, peak hold, release, the scale mapping and the lamp's fade are
  pure logic in a new host-tested library. The preview's `frame()` is the
  reference behaviour.

## Drawing

- Each instrument's static part is one pre-rendered 360×360 RGB565 image
  (~253 KB): scale, lamp light, paper speckle, housing, glass, unlit
  segments, legends and mesh. The images are rendered from the preview's
  drawing code by a script under `scripts/` and compiled into the
  firmware. There is room: the firmware is 2.5 MB of a 6.25 MB app slot.
  How the script renders (headless browser, node-canvas, Cairo) is the
  builder's choice. It must be reproducible.
- **Analog needle**: drawn live per frame as a thin tapered dark polygon
  with a soft offset shadow, clipped to the face, over the background.
  Only the area the needle sweeps between two frames is redrawn. The
  preview draws its glass highlight over the needle; on the device it may
  be baked under it if blending a large alpha layer per frame costs too
  much (builder's call, judge on the device).
- **Peak lamp**: a small pre-rendered lit sprite shown or hidden over the
  off lamp.
- **Digital segments**: my suggestion is a second pre-rendered image with
  every segment lit (glow included). Each bar is then drawn as a region of
  that image clipped to the current level, and the hold segment as one
  more clipped region. This avoids a per-segment blur on the device. Any
  other approach is fine if it looks like the preview.
- Aim for ~30 fps of needle or segment updates, with no audible effect on
  any format (MP3, AAC, Ogg, FLAC, WAV). Check with `TASKS` and by ear.
- Text fonts in the images (Archivo Narrow, Share Tech Mono, both SIL
  OFL) end up as pixels in the firmware. Run the `huepattl-legal-check`
  skill on them before release.

## Design rules

The meters are exempt from `huepattl-rams-design` and from
`docs/design/ux-guidelines.md` in the same way as games: a cabinet, not a
front panel. This was the user's call. Write the exemption into
ux-guidelines.md §2.6 (the games exception) as a deliberate, bounded
deviation: visualizations only, the keep-awake toggle excepted.

## Documentation

- New ADR 0034 for the view, the meters and the signal tap, with on-device
  measurements (frame cost, core-0 load, heap).
- README screenshots via `scripts/readme-screenshots.py`, if the view gets
  a step there (builder's call).

## Left to the builder

- Module and file names (e.g. `lib/visualizer/...`,
  `ScreenManagerVisualization.cpp`).
- The NVS keys.
- The toggle's glyph and its exact position, within the bottom segment of
  the circle.
- Whether the images stay in flash or are copied to PSRAM when the view
  opens.
