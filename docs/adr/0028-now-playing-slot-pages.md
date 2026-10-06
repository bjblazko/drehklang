# 0028: Now Playing's cover slot as swiped pages

## Status

Accepted — 2026-10-06. Amends ADR 0009 (no swipe, no page dots), ADR 0014
(the Cover / Spectrum button) and ADR 0024 (the band becomes a component).

## Context

The cover slot showed the cover or the dot-matrix spectrum, switched from
a button in the options panel. The tone generator meanwhile had a scope
and an absolute spectrum, swiped between in its band with two page dots
under it. The user wanted both of those over the music too, and the slot
swiped like the band.

## Decision

- **Four pages, swiped.** Cover (only when the album has one), the dot
  matrix as before, the scope and the spectrum. Right to left goes on,
  left to right back, without wrapping. Page dots sit under the slot.
  The page swiped to is kept (`npView`; the old `npSpectrum` switch is
  taken over once). An album without a cover shows the dot matrix where
  the cover page was chosen. `visualizer::CoverSlotPages` holds this,
  host-tested.
- **The swipe stays in the slot.** A transparent box over the slot takes
  it, and `swipeStartsOnControl()` reports it, so the app-wide back swipe
  does not start there. Back still works on a swipe anywhere else.
- **The options panel loses its Cover / Spectrum button**: Shuffle,
  Repeat, Lock. Its top edge moved down 6 px to clear the dots.
- **One component for the band.** `ui::SignalBand` holds the scope and
  spectrum drawing, their scales and the sample buffer, and knows nothing
  about the source; `ui_widgets::PageDots` is the row of dots. The tone
  generator uses both and is unchanged on screen.
- **200 × 96 for scope and spectrum**, as wide as the bezel allows at that
  height; the cover and the dot matrix stay 96 × 96. No scale label in the
  player: there is no row for it, and the picture is for watching, not
  measuring.
- **Over music**, the scope's timebase is a fixed 20 ms (music has no
  pitch to choose one for, like the tone generator's noise). Its level
  range follows the loudest sample on screen in the same 10 dB steps and
  with the same hold as a set level. The samples are the decoder's mono
  downmix from before the volume (ADR 0026), so the spectrum reads the
  track's own dBFS, at the track's sample rate. Paused or muted shows
  nothing, as the dot matrix does.

## Checked on the way

- **The spectrum drew a plateau above Nyquist.** Columns past half the
  sample rate repeated the last bin: a 22.05 kHz audiobook would have
  shown about -38 dB of treble that is not there from 11 to 20 kHz. Never
  visible at the generator's 48 kHz. Fixed, with a test.
- The dot matrix already handled other rates correctly (bands past
  Nyquist are empty).
- On the device: all four pages with an MP3 (its encoder's lowpass shows
  as the spectrum falling to the floor above ~16 kHz), the swipe, back
  from below the slot, no audible stutter, and the tone generator as
  before.

## Consequences

- Scope and spectrum cost 4-5 ms a frame on the main loop while shown,
  as on the tone generator; their ~40 KB of buffers come from PSRAM and
  are released when Now Playing is left.
- The `image` and `equalizer` glyphs stay in `IconFont.c`, unused.
