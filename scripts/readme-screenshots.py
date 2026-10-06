#!/usr/bin/env python3
"""Takes the README's screenshots from a running Drehklang, end to end.

Walks the interface over the serial commands main.cpp understands (HOME,
TAP, HOLD, KNOB, WHERE, SCREENSHOT), checks after each step that it landed
on the screen it meant to, and writes each capture to docs/screenshots/ as a
round PNG: the 360x360 frame cut to the display's circle, a black ring
where the glass ends, and the corners transparent.

Before running:
- Flash the current firmware, unlock the device, and plug in the S3's USB.
- Now Playing's cover slot shows the spectrum (options panel > Spectrum):
  album covers are someone else's work and stay out of the README.
- Music has at least one artist with one album.

The tour plays music and a tone, so turn the volume down if headphones
are on. Usage: readme-screenshots.py [--port /dev/cu.XXXX] [--only name ...]
"""

import argparse
import math
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot import capture, find_port  # noqa: E402

import serial  # noqa: E402

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "docs", "screenshots")
SIZE = 360  # The display, square framebuffer behind a round glass.
RING = 8  # The black ring drawn outside the display's circle, in pixels.
SETTLE_S = 1.0  # After a tap: the next screen renders, then the knob counts.
LIBRARY_SCROLL = 10  # Knob detents down the artist list before its picture.

# navigation::ScreenKind values (lib/navigation/ScreenId.h), what WHERE prints.
KIND = {
    "Home": 0, "Settings": 1, "Artists": 3, "Albums": 4, "Tracks": 5,
    "NowPlaying": 7, "SleepTimer": 9, "Games": 18, "TableTennis": 19,
    "Gravity": 20, "ToneGenerator": 21, "About": 24, "Bluetooth": 25,
    "Equalizer": 28,
}

# Home's carousel, from its first entry (HOME selects that one).
CAROUSEL = {"Music": 0, "Settings": 2, "Sleep": 3, "Games": 4, "Tones": 5}


class Device:
    """The serial commands, one at a time, with the checks a tour needs."""

    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=1)

    def send(self, line, settle=0.3):
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        time.sleep(settle)

    def tap(self, x, y):
        self.send(f"TAP {x} {y}", SETTLE_S)

    def knob(self, detents):
        # One line rather than one per detent: fewer lines, fewer to lose.
        self.send(f"KNOB {detents}", 0.6)

    def where(self):
        self.ser.reset_input_buffer()
        self.send("WHERE", 0)
        deadline = time.time() + 3
        while time.time() < deadline:
            line = self.ser.readline().decode(errors="replace").strip()
            if line.startswith("[where] "):
                return int(line.split()[1])
        raise TimeoutError("no answer to WHERE")

    def expect(self, kind):
        found = self.where()
        if found != KIND[kind]:
            raise RuntimeError(f"expected {kind} ({KIND[kind]}), on screen {found}")

    def open_from_home(self, entry, kind):
        self.send("HOME", SETTLE_S)
        self.knob(CAROUSEL[entry])
        self.tap(180, 160)
        self.expect(kind)

    def shot(self, name):
        width, height, pixels = capture(self.ser)
        path = os.path.join(OUT_DIR, f"{name}.png")
        write_png(path, *round_frame(width, height, pixels))
        print(f"  {name}.png")


# --- Round PNG ---------------------------------------------------------------

def _coverage(x, y, centre, radius):
    """(display, ring) fractions of canvas pixel (x, y), antialiased at edges."""
    d = math.hypot(x + 0.5 - centre, y + 0.5 - centre)
    if d < radius - 1:
        return 1.0, 0.0
    if radius + 1 < d < radius + RING - 1:
        return 0.0, 1.0
    if d > radius + RING + 1:
        return 0.0, 0.0
    return _supersample(x, y, centre, radius)


def _supersample(x, y, centre, radius):
    offsets = (0.125, 0.375, 0.625, 0.875)
    inside = ring = 0
    for sy in offsets:
        for sx in offsets:
            d = math.hypot(x + sx - centre, y + sy - centre)
            if d <= radius:
                inside += 1
            elif d <= radius + RING:
                ring += 1
    return inside / 16, ring / 16


def _rgb565(pixels, width, x, y):
    value = pixels[(y * width + x) * 2] | (pixels[(y * width + x) * 2 + 1] << 8)
    return (((value >> 11) & 0x1F) * 255 // 31,
            ((value >> 5) & 0x3F) * 255 // 63,
            (value & 0x1F) * 255 // 31)


def round_frame(width, height, pixels):
    """The capture on a transparent canvas: display circle plus black ring."""
    canvas = SIZE + 2 * RING
    centre, radius = canvas / 2, SIZE / 2
    rows = []
    for y in range(canvas):
        row = bytearray()
        for x in range(canvas):
            inside, ring = _coverage(x, y, centre, radius)
            alpha = inside + ring
            if alpha == 0:
                row += b"\0\0\0\0"
                continue
            sx = min(max(x - RING, 0), width - 1)
            sy = min(max(y - RING, 0), height - 1)
            r, g, b = (round(c * inside / alpha) for c in _rgb565(pixels, width, sx, sy))
            row += bytes((r, g, b, round(alpha * 255)))
        rows.append(bytes(row))
    return canvas, canvas, rows


def write_png(path, width, height, rows):
    """RGBA, 8 bits per channel, no filter: what every viewer reads."""
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    raw = b"".join(b"\0" + row for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


# --- The tour ------------------------------------------------------------------

def home(dev):
    dev.send("HOME", SETTLE_S)
    dev.expect("Home")
    dev.shot("home")


def library(dev):
    dev.open_from_home("Music", "Artists")
    # From the middle of the list rather than its top, where an entry
    # that is nobody else's business sits.
    dev.knob(LIBRARY_SCROLL)
    dev.shot("library")
    dev.send("HOME", SETTLE_S)
    dev.open_from_home("Music", "Artists")
    dev.tap(120, 183)  # The first artist, below Browse by and Shuffle.
    dev.expect("Albums")
    dev.tap(120, 139)  # Its first album, below Shuffle.
    dev.expect("Tracks")
    dev.shot("tracks")
    dev.tap(120, 139)  # The first track: plays the album from there.
    dev.expect("NowPlaying")
    # The cover slot's last page, the spectrum, then back one at a time
    # (ADR 0028): the cover page, first when there is one, is never shown.
    for _ in range(4):
        dev.send(SLOT_NEXT, SETTLE_S)
    time.sleep(2)  # Let the picture fill.
    dev.shot("now-playing-spectrum")
    dev.send(SLOT_BACK, 2)
    dev.shot("now-playing-scope")
    dev.send(SLOT_BACK, 2)
    dev.shot("now-playing")
    dev.tap(180, 340)  # The options handle.
    dev.shot("options")
    dev.tap(252, 238)  # Lock, the third of three (ADR 0028).
    dev.shot("locked")
    unlock(dev)


def unlock(dev):
    """Holds the unlock button and turns past the threshold (ADR 0005)."""
    dev.send("HOLD 180 180 3500", 0.2)
    dev.knob(12)
    time.sleep(SETTLE_S)
    dev.expect("NowPlaying")


def tones(dev):
    dev.open_from_home("Tones", "ToneGenerator")
    dev.tap(180, 315)  # Start.
    time.sleep(1)
    dev.shot("tones")
    dev.send("SWIPE 300 110 60 110", SETTLE_S)  # The band turns to the spectrum.
    dev.shot("tones-spectrum")
    dev.send("SWIPE 60 110 300 110", SETTLE_S)
    dev.tap(180, 315)  # Stop.


def sleep_timer(dev):
    dev.open_from_home("Sleep", "SleepTimer")
    dev.shot("sleep")


def settings(dev):
    dev.open_from_home("Settings", "Settings")
    dev.shot("settings")
    dev.tap(150, 234)  # Bluetooth, the fourth row.
    dev.expect("Bluetooth")
    dev.shot("bluetooth")
    dev.open_from_home("Settings", "Settings")
    dev.tap(150, 190)  # Equalizer, the third row.
    dev.expect("Equalizer")
    dev.tap(180, 308)  # Flat, so the curve below is the whole curve.
    # A gentle smile: deep bass and air up, the middle left alone.
    for band, db in ((0, 6), (1, 3), (5, 2), (6, 4)):
        dev.tap(180 + (band - 3) * 36, 184)  # Selects the band.
        dev.knob(db)
    dev.tap(180 + (1 - 3) * 36, 184)
    dev.shot("equalizer")
    dev.tap(180, 308)  # Flat again: the tour leaves no setting behind.


def games(dev):
    for row_y, kind, name in ((95, "TableTennis", "table-tennis"), (139, "Gravity", "gravity")):
        dev.open_from_home("Games", "Games")
        dev.tap(150, row_y)
        dev.expect(kind)
        dev.tap(180, 200)  # Serve, or start the descent.
        time.sleep(1.5)
        dev.shot(name)


def about(dev):
    dev.send("HOME", SETTLE_S)
    dev.tap(180, 53)  # The wordmark.
    dev.expect("About")
    dev.shot("about")


TOUR = (home, library, tones, sleep_timer, settings, games, about)
# Swipes inside Now Playing's cover slot (ADR 0028).
SLOT_NEXT = "SWIPE 280 92 80 92"
SLOT_BACK = "SWIPE 80 92 280 92"
ATTEMPTS = 3  # Now and then a serial command goes missing; a step starts over.


def run(step, dev):
    for attempt in range(1, ATTEMPTS + 1):
        try:
            step(dev)
            return
        except (RuntimeError, TimeoutError) as error:
            print(f"  attempt {attempt}: {error}")
            dev.send("HOME", SETTLE_S)
    sys.exit(f"{step.__name__} failed {ATTEMPTS} times")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="serial port (auto-detected if omitted)")
    parser.add_argument("--only", nargs="*", help="tour steps to run, e.g. tones about")
    args = parser.parse_args()
    port = args.port or find_port()
    if not port:
        sys.exit("Could not find the board's serial port. Pass --port.")
    os.makedirs(OUT_DIR, exist_ok=True)
    dev = Device(port)
    for step in TOUR:
        if args.only and step.__name__ not in args.only:
            continue
        print(step.__name__)
        run(step, dev)
    dev.send("HOME", 0)


if __name__ == "__main__":
    main()
