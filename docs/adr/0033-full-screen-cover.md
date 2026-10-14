# 0033: A full-screen cover in Now Playing

## Status

Accepted — 2026-10-14. Extends ADR 0028's cover slot: a tap there, which
did nothing, now opens the cover full-screen.

## Context

The user asked for a full-screen view of the album cover: a tap on the
cover shows only the cover, as large as it can be with its corners
touching the round edge; the rest of the circle filled with the cover's
average colour or, if possible, the cover enlarged again and strongly
blurred. A second tap shows the song's name and time, a third returns to
the player.

What the board decides:

- **The cached cover is 96 px** (`CoverArtCache::kCoverSize`), made for
  Now Playing's slot. The largest square inside the 360 px circle is
  254 px. Enlarging 96 px 2.6 times is soft at best.
- **Most covers can not be decoded large.** About 90 % of this library's
  embedded covers are progressive JPEGs, which JPEGDEC decodes only from
  their DC coefficients at 1/8 scale (ADR 0016): a 1000 px cover gives
  125 px. Baseline JPEGs decode at up to full size.

Each decision below was put to the user as a question before any code
was written.

## Decision

### The cover is decoded again from the original when the view opens

The embedded picture or `cover.jpg` — found by the same search caching
uses, now `CoverArtCache::readCoverJpeg()` — is decoded at 254 px by the
existing `JpegDecoder::decodeSquare()`, which enlarges bilinearly where
the decode comes out smaller. Baseline covers come out sharp, progressive
ones as sharp as their DC image allows. Without an original, the cached
cover is enlarged instead.

### The background is the cover, blurred

The 96 px cached cover, box-blurred (radius 8, three passes, close to a
Gaussian) until only its colours are left, enlarged to 360 px and drawn
behind the sharp copy (`coverview::CoverBackdrop`, host-tested). At that
blur its low resolution can not show. Enlarging uses an 8-bit fixed-point
bilinear of its own and skips the middle the cover hides:
`SquareResampler`'s float version took ~190 ms for the full 360 px.

Measured on the device (2026-10-14), a 137 KB baseline cover: 296 ms
from the tap to the view — reading the JPEG 96 ms, decoding 115 ms,
blurring 30 ms, enlarging the rest. Both images (~390 KB) live in PSRAM
only while the view is open and are kept across its re-renders, so the
info tap does not decode again. Internal RAM is unchanged.

### Taps, swipes and the knob

- A tap (not a swipe) on the slot's cover page opens the view; swipes
  still turn the slot's pages.
- A second tap shows the time (live) in a pill in the segment above the
  square and the title in one below; the cover itself stays clear. The
  pills are the light surface, as everywhere else, since the background
  can be any colour. A third tap closes.
- Only taps count on the view: LVGL reports a click after a drag too, and
  a swipe there is the way back — it closes the view, not Now Playing.
- The knob is still the volume, without the ring (it belongs to Now
  Playing's own layout).
- A track from another album shows that album's cover; one with no cover
  closes the view.

## Consequences

- Opening the view costs about 0.3 s and one file read while music plays.
- Progressive covers stay soft; making them sharp would need a full
  progressive decoder, which ran out of PSRAM above ~1000 px (AGENTS.md).
- Turning the volume on the view gives no visual feedback.
