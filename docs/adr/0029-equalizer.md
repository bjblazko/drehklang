# 0029: A seven-band equalizer

## Status

Accepted — 2026-10-06.

## Context

The user asked for an equalizer in Settings: five or seven sliders,
classic with a centre line, and very deep bass and very high treble
covered. Open were the bands, where it runs, what it costs, and how seven
sliders are worked on a 1.8" round screen.

## Decision

### Seven bands, honest at both ends

31 · 100 · 300 · 1k · 3k · 8k · 16k Hz, ±12 dB in 1 dB steps
(`signal::GraphicEqualizer`, host-tested). Bells about 1.6 octaves apart
(Q 0.9, RBJ cookbook). The bottom band is a bell, not a shelf, so nothing
below hearing is lifted (it would cost excursion and battery for nothing).
The top band is a high shelf: a bell that close to half the sample rate
is squeezed out of shape, and "air" is a shelf anyway. A band at or above
0.45 x the sample rate is left out (a 22.05 kHz audiobook has no 16 kHz).

### It never clips

Before the volume, everything is lowered by the curve's highest point,
found on a 256-point log grid plus the band centres (overlapping bells add
up), so nothing is pushed past full scale in the 32-bit samples. Tested
with every band at +12 dB.

After the volume, all of that headroom is given back, at every volume
step, through a peak limiter (`signal::PeakLimiter`,
`applyEqualizerMakeup()`), before the sleep fade and the Bluetooth copy.
The music the equalizer did not lift keeps exactly its level; only where
a lifted band would pass full scale does the gain dip, for that peak, and
come back over ~80 ms. Both channels share the gain, so the image does not
move. It cannot clip: the limiter holds everything under -0.18 dBFS. The
spectrum's tap gets the headroom back too, so the pictures do not look
quieter either.

**Superseded the same week:** the first makeup gave back only
`min(headroom, 64 / kVolumeTable[step])`, i.e. as much as the volume step
left room for if the lifted band were at full scale all the time. That
worst case cost the common one: from step 10 up the rest of the music got
quieter whenever a band was lifted -- -4.5 dB at step 13, -7.5 dB at 16,
-12 dB at full volume for +12 dB of bass (user, 2026-10-09: "wenn ich den
Bass anhebe, wird der Rest relativ leiser").

Found on the device the same day, both as "the equalizer makes
everything quieter, whatever I set":

- **Headroom without makeup.** The first version only lowered: +12 dB at
  31 Hz made the whole song 12 dB quieter, at any volume.
- **The headroom itself was wrong at low frequencies.** |H|^2 in its
  expanded form in cos(w) cancels to rounding noise near DC in float:
  +12 dB at 31 Hz was taken as +21.6. The RBJ cookbook's factored form in
  sin^2(w/2) is exact; a test now holds the headroom to the lift, not just
  above it.

### Where it runs

In `audio_process_raw_samples` (ADR 0026): on the decode task, before the
volume, and before the spectrum's tap, so Now Playing's spectrum and scope
show what is heard. Both the jack and Bluetooth get it, since Bluetooth
copies the output later in the chain (ADR 0027). The tone generator owns
the DAC on its own path and stays flat, as a measuring instrument must.
Gains are handed over through atomics; the decode task works out the
filters itself when they or the sample rate change. Flat is skipped
entirely.

### Cost, measured on the device (2026-10-06, MP3 at 44.1 kHz)

| Version | Every band on |
|---|---|
| One band after another per sample | 12.2% of core 0 |
| A block at a time, band by band | 10.6% |
| Both channels in one loop | **8.6%** |

Each filter output waits for the one before it; two independent channels
side by side hide that wait. Recomputing the filters after a change takes
up to ~4 ms once (the headroom grid), against ~90 ms of DMA buffer. The
firmware does not scale the CPU clock, so the extra work only shortens
the idle task's clock-gated wait: a few mA, small next to the display.
Build with `-DDREHKLANG_EQ_DEBUG` to log the share every 5 s.

### Working it

Settings > Equalizer, between USB drive and Bluetooth: seven sliders on a
0 dB centre line, their frequencies under them, the selected band and its
gain above ("100 Hz · +3 dB"), and a secondary Flat button. Tapping a
slider selects it and the knob sets it in 1 dB steps; dragging one sets it
directly. Only the selected handle is in accent -- the value being set, as
on Brightness. The sliders are about 36 px apart, which a finger covers
while dragging, so the knob is the precise way and the drag the expected
one. The slider area is reported by `swipeStartsOnControl()`, so a drag
that wanders sideways does not go back. Gains are kept like brightness,
once they have settled (`eq0`..`eq6`, gain + 12).

## Consequences

- One more thing runs on the decode task; with every band on it is the
  biggest single item after decoding. If audio ever stutters with the
  equalizer on and not off, look here first.
- `test_equalizer` and `test_equalizer_setting` hold the curve, the
  headroom, the Nyquist rule and the persistence.
