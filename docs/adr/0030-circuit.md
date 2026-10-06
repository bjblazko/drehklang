# 0030: Circuit, a pseudo-3D racer with its own renderer

## Status

Proposed — 2026-10-06. The third entry in ADR 0022's Games menu. Gives a
game the panel to itself (bypassing LVGL's drawing) and adds a
three-voice sound source next to `playback::ToneGenerator`.

## Context

The user asked for a simple racer in the manner of the 1982 arcade
racers and the late-80s home-computer ones: colour, a look between C64
and Amiga (leaning Amiga), no real 3D, generated sprites, no music but
8/16-bit effects (engine, crash, collision), a closed circuit with
computer cars, steering by the knob.

Settled with the user on 2026-10-06:

| Question | Decision |
|---|---|
| Goal | Against the clock: checkpoints add time, computer cars are traffic to pass |
| Collisions | A damage bar; the race ends when time runs out *or* the car is wrecked; no repair |
| Tracks | Three, chosen before the race: Coast, Desert, Night |
| Gearbox | Automatic |
| Throttle | **Always full once the countdown ends**; touching the screen (anywhere) brakes. Touching and turning the knob at the same time is awkward, so the game asks for it as rarely as possible |
| Steering | The knob sets a steering angle that stays where it is left, like a wheel |
| Records | Best lap per track, kept in NVS |
| Sprites | Pixel art drawn in a script, emitted as C arrays — no third-party assets |
| Traffic | 5–6 computer cars |
| HUD | In the sky: time left large, lap and lap time small, damage as a bar at the top, speed bottom centre |
| Controls hint | Said once during the countdown, not painted on |
| Name | **Circuit** — "Pole Position" (Namco) and "Test Drive" (Accolade) are marks; ADR 0022's reasoning applies |

### What a frame costs (measured, 2026-10-06)

Unlike Table Tennis and Gravity, every row of a pseudo-3D road changes
every frame, so this game cannot lean on LVGL's partial invalidation —
a full 360×360 redraw is unavoidable. A throwaway probe (branch
`spike/racer-fps`, stashed) drew an animated road plus seven scaled,
colour-keyed sprites for 150 frames each way:

| Variant | Render | Push | fps |
|---|---|---|---|
| Full res, frame in PSRAM, one push | 15.9 ms | 33.9 ms | 19.7 |
| Full res, 36-row stripes in internal DMA RAM | 10.6 ms | 31.6 ms | 23.1 |
| Half res (180×180), pixel-doubled stripes | 6.2 ms | 31.6 ms | 25.6 |

The bus is the limit, not the CPU: a full frame over QSPI at 40 MHz
(Arduino_GFX's default) takes ~31.6 ms whatever is drawn. Half
resolution therefore buys almost nothing and the road stays at full
resolution. Rendering and pushing were sequential in the probe;
overlapping them, and skipping the corners outside the round panel
(~21 % of the square), is what brings 30 fps in reach. It looked smooth
on the device at 23 fps already (user, 2026-10-06).

Music is paused while a game is open (ADR 0026), so ADR 0006's concern —
heavy redraw starving the decoder — does not apply here.

## Decision

### The game is pure logic: `games::CircuitGame`

No LVGL, no Arduino, an explicit `tick(nowMs)` split into 8 ms slices,
integer fixed point with carried remainders (ADR 0023's lesson), all of
it host-tested in `test/test_circuit`.

- **Track** (`CircuitTrack.h`): segments of length, curve and hill;
  scenery placements (kind, side, offset); checkpoints. The three tracks
  are `constexpr` tables in `CircuitTracks.h`, with their start time and
  checkpoint bonus. A fourth track is one more table.
- **Player**: position `z` along the track, lateral `x` in road
  half-widths, speed, steering angle in detents (clamped to ±12 to start with; tuned on the device). Throttle
  is on from GO; braking is the only pedal. The automatic gearbox derives
  the gear from speed and exposes engine revs for the sound.
- **Lateral motion** per slice: steering × speed minus curve × speed²
  (centrifugal), so a curve has to be held against.
- **Damage** 0–100: off the road the top speed drops and a little damage
  accrues per second; running into a computer car from behind drops the
  speed below its speed and costs a medium amount; hitting a scenery
  object is a crash — speed to zero, a lot of damage, the car is put back
  on the road. 100 is wrecked.
- **Computer cars**: six, each with its own lane and a fixed speed of
  60–85 % of the player's top speed, now and then a slow lane change.
  They do not collide with each other.
- **End**: time at zero ("TIME UP") or damage at 100 ("WRECKED"), named,
  as Gravity names its failures. Laps completed and the best lap are the
  result.
- **Sounds**: one-shot events (crash, bump, checkpoint, lap, countdown,
  wrecked) through a small queue; continuous states (revs, off-road,
  tyre squeal in a hard curve) read directly by the screen — ADR 0023
  found that a held sound in the event queue pushes the one-shots out.
- **Phases**: track select → countdown → race → result.

### Its own renderer, and the panel handed over

`games::CircuitRenderer` renders rows `[y0, y0+n)` of a frame into a
`uint16_t` RGB565 buffer from a snapshot of the game. Pure C++, so it is
host-tested too.

- **Projection**: the classic segment-based pseudo-3D. Segments are
  projected front to back, curve offsets accumulate, hills raise and lower
  the road, and a "highest row so far" clip lets a crest hide what is
  behind it. Each row alternates light/dark bands for grass, rumble strip,
  road and centre line.
- **Sky**: a gradient in a few hard steps (the Amiga copper look) and a
  parallax backdrop that shifts in curves — sea and islands, dunes and
  mesas, a city skyline with lit windows.
- **Sprites**: drawn back to front after the road, scaled
  nearest-neighbour, colour index 0 transparent, clipped at the crest.
- **Assets**: `scripts/generate-circuit-sprites.py` draws the pixel art
  (player car from behind straight/left/right, computer car, palm, cactus,
  rock, lamp post, sign, checkpoint banner, explosion/smoke frames, an
  8×8 pixel font) and emits `CircuitSprites.c/.h`: 8-bit indexed, with a
  palette per track. The night track is a palette swap and the computer
  cars are one car in several liveries — the Amiga trick, and it keeps
  flash small.
- **HUD**: drawn by the renderer in the pixel font at 2×, not LVGL
  labels — two writers to the same pixels would need ordering, and
  Montserrat would announce a modern UI. Time left top centre, lap and lap
  time below it, damage as a segment bar along the top edge, speed bottom
  centre with a small steering-angle indicator, a faint glow at the edges
  while braking.
- **Pipeline**: once per frame the main loop copies the game state into
  a snapshot. A render task on core 0 renders 16-row stripes into two
  alternating internal DMA buffers (~23 KB; PSRAM if the internal heap
  cannot spare it — checked against `heap_caps_get_free_size` during
  bring-up), while the main loop pushes the previous stripe. Each stripe is
  rendered and pushed only across the span inside the circle. Physics and
  sound tick every loop iteration; drawing is gated to ~30 fps (ADR
  0022). The encoder is interrupt-driven, so a loop busy pushing for
  ~25 ms per frame loses no detents.
- **Panel ownership**: `LvglGlue` gets a switch, "the panel belongs to a
  game". While it is set, `flushCb` discards LVGL's output (still calling
  `lv_disp_flush_ready`); on leaving, the whole screen is invalidated and
  LVGL repaints. The renderer also mirrors its stripes into the shadow
  framebuffer, so `SCREENSHOT` works mid-race.

### Three voices: `playback::ChipVoices`

`ToneGenerator` is one voice and restarts on every trigger, which is
right for a blip and wrong for an engine whose pitch glides under a
crash. Rather than reshape it under Table Tennis and Gravity, a separate
source sits next to it, a small SID/Paula-like chip:

1. **Engine** — narrow pulse wave, frequency follows revs (about 55 Hz
   idle to 240 Hz), drops audibly on an automatic upshift, slight
   vibrato.
2. **Noise** — ADR 0023's LFSR with a variable clock: low and quiet off
   the road, high and modulated for tyre squeal, loud with a falling
   clock for a crash.
3. **Effects** — square wave with a decaying envelope and an optional
   pitch sweep: bump (short downward sweep), checkpoint (two rising
   notes), lap (short arpeggio), countdown (three short, one long and
   higher), wrecked (long downward sweep).

Frequency and level change live, without resetting phase (no clicks),
handed from the main loop to the audio task through atomics as
`ToneGenerator` does. The voices are summed and hard-limited; the engine
sits below the effects because it never stops (Gravity's thrust at ⅓, for
the same reason). `ToneOutput` takes a small "sample source" interface so
it plays either `ToneGenerator` or `ChipVoices`; nothing changes for the
other games. A `CHIP <voice> <hz>` serial command plays a voice, so the
sounds can be tuned by ear (as `BLIP` does).

### One screen, four phases

`kGames` gains a "Circuit" row and `ScreenKind` an appended `Circuit`,
never restored from resume. `lib/ui/ScreenManagerCircuit.cpp` holds the
usual `renderCircuit()`/`tickCircuit()` pair. All four phases are drawn
by the renderer, so the game looks like one cabinet throughout:

1. **Track select** — the knob cycles Coast/Desert/Night, showing that
   track at standstill, its name and best lap; a tap starts.
2. **Countdown** — 3-2-1-GO with beeps, "TURN TO STEER / TOUCH TO BRAKE"
   in the sky. Steering already works (ADR 0023: a control that does
   nothing until later reads as broken).
3. **Race.**
4. **Result** — "TIME UP" or "WRECKED", laps, best lap, "NEW RECORD" if
   so; a tap returns to track select.

The whole screen is one invisible LVGL object for the brake, taking
`PRESSED`, `RELEASED` and `PRESS_LOST` (a finger sliding off must not
leave the brake on). Leaving is the app-wide right swipe, announced once
on entry as in the other games; mid-race it abandons the race, there is no
pause. Whether a resting brake finger that slides can trigger that swipe
by accident is checked on the device; if it can, mid-race the swipe must
start at the left edge. During countdown and race `tickCircuit()` reports
activity to the `IdleTimer`, or the panel would dim and freeze the race.

Best laps: three `uint32_t` milliseconds under `circuitBest` through the
existing `KeyValueStore`, zero meaning none; written only on a new record.

### Tests and checks

- Host (`pio test -e native`): `test_circuit` (physics, centrifugal
  drift, damage per cause, both endings, checkpoints and laps, traffic
  passing, determinism over a replayed drive), `test_circuit_renderer`
  (straight road centred and symmetric, a curve shifts the vanishing
  point, sprite clipping at the edge and the crest, nothing written
  outside the circle), `test_chip_voices` (pitch by zero crossings, no
  phase jump on a frequency change, envelope decay, sweep end pitch, no
  int16 overflow in the mix).
- Device: `-DDREHKLANG_CIRCUIT_DEBUG` logs fps, worst render and push
  times every 90 frames; a `SCREENSHOT` of each phase; the swipe-vs-brake
  check above; sounds judged by ear.
- Before release: the `huepattl-legal-check` skill (new bundled assets:
  self-drawn sprites, the name).

## Consequences

- A game may now own the panel. Anything else that wants to draw outside
  LVGL must use the same switch and invalidate on the way out, or LVGL's
  picture and the game's will interleave.
- `ToneOutput` plays one of two sources; a fourth game picks one or the
  other rather than adding a third path.
- Not settled until played: steering degrees per detent, the
  centrifugal factor, start time and checkpoint bonus per track. Table
  Tennis's knob took three attempts (ADR 0022); expect the same here.
- Deliberately absent: manual gears, repair, pit stops, a qualifying lap,
  music, two players, collisions between computer cars.
