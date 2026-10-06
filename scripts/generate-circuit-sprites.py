#!/usr/bin/env python3
"""Draws Circuit's pixel art and writes it as C++ (ADR 0030).

Everything here is drawn by this script -- no third-party artwork -- in
the manner of an Amiga game: few colours, hard edges, a one-pixel black
outline around every sprite. Sprites are 8-bit indexed; each track has its
own palette, so the night track and the computer cars' liveries are palette
swaps rather than more pixels.

Run from the repository root:

    python3 scripts/generate-circuit-sprites.py

Writes lib/games/CircuitSprites.h and lib/games/CircuitSprites.cpp.
`--preview sheet.png` also writes every sprite and backdrop, 3x, in each
track's palette, to look at before flashing.
"""

import math
import random
import struct
import sys
import zlib
from pathlib import Path

OUT_DIR = Path(__file__).resolve().parent.parent / "lib" / "games"

# --- Palette slots ------------------------------------------------------
# Index 0 is transparent in every sprite. The rest are slots whose colour
# each track chooses.
T, OUTLINE, BODY, STRIPE, TYRE, CHROME, WHITE, TAIL = range(8)
LEAF_DARK, LEAF_LIGHT, TRUNK, ROCK_DARK, ROCK_LIGHT, SIGN, WINDOW, WALL = range(8, 16)
FIRE_YELLOW, FIRE_ORANGE, FIRE_RED, CACTUS_DARK, CACTUS_LIGHT, GLOW, SEA, SEA_LIGHT = range(16, 24)
FAR, NEAR, CLOUD, CLOUD_SHADE, MESA_LIGHT, MESA_DARK, SKYLINE, MOON = range(24, 32)
SLOTS = 32


def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def hexrgb(text):
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


# Shared colours, overridden per track below.
BASE = {
    T: "000000", OUTLINE: "101018", BODY: "D02020", STRIPE: "F0F0F0",
    TYRE: "282830", CHROME: "A0A0B0", WHITE: "F8F8F8", TAIL: "FF3030",
    LEAF_DARK: "207830", LEAF_LIGHT: "50B040", TRUNK: "8A5A2A",
    ROCK_DARK: "6A5A50", ROCK_LIGHT: "A89888", SIGN: "F8D020",
    WINDOW: "FFD860", WALL: "384050", FIRE_YELLOW: "FFF060",
    FIRE_ORANGE: "FF9020", FIRE_RED: "C02010", CACTUS_DARK: "2A7038",
    CACTUS_LIGHT: "58A050", GLOW: "FFF0B0", SEA: "1858B8", SEA_LIGHT: "4890E0",
    FAR: "6878A8", NEAR: "3A6848", CLOUD: "F8F8FF", CLOUD_SHADE: "C0C8E0",
    MESA_LIGHT: "D88848", MESA_DARK: "985030", SKYLINE: "202838", MOON: "F0F0D0",
}

# Per track: sprite-slot overrides plus the road and sky colours the
# renderer paints without sprites. Sky runs top to horizon in hard bands,
# the copper-list look.
TRACKS = [
    {
        "name": "coast",
        "slots": {},
        "sky": ["2850C8", "3868D8", "4880E0", "6098E8", "80B0F0", "A8C8F8"],
        "grass": ["44AC48", "2C8834"],
        "road": ["707078", "68686F"],
        "rumble": ["F0F0F0", "D02020"],
        "lane": "F0F0F0",
    },
    {
        "name": "desert",
        "slots": {FAR: "B87850", NEAR: "C89060", ROCK_DARK: "905838",
                  ROCK_LIGHT: "D09868", SIGN: "F8F0E0"},
        "sky": ["4078C8", "5890D0", "78A8D8", "98C0E0", "C0D0D8", "E8D8C0"],
        "grass": ["D8B068", "C8A060"],
        "road": ["786860", "70625A"],
        "rumble": ["F0F0F0", "2860C8"],
        "lane": "F8E8C0",
    },
    {
        "name": "night",
        "slots": {LEAF_DARK: "103018", LEAF_LIGHT: "205028", BODY: "C02838",
                  CHROME: "707888", ROCK_DARK: "303038", ROCK_LIGHT: "505060",
                  SIGN: "E8C030", WALL: "182030", FAR: "283050", NEAR: "1A2030",
                  CLOUD: "404868", CLOUD_SHADE: "303858"},
        "sky": ["080818", "0C1024", "101830", "182040", "202848", "283050"],
        "grass": ["183020", "142818"],
        "road": ["383840", "34343C"],
        "rumble": ["C8C8C8", "B02030"],
        "lane": "D8D8A0",
    },
]

# Computer cars: body and stripe swapped per livery (yellow, blue, green,
# white). The player's car is livery -1: the track's own BODY/STRIPE.
LIVERIES = [("F0C020", "202020"), ("2050D0", "F0F0F0"),
            ("20A050", "F0F0F0"), ("E8E8E8", "2050D0")]


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[T] * w for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(int(cy - ry), int(cy + ry) + 1):
            for x in range(int(cx - rx), int(cx + rx) + 1):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.set(x, y, c)

    def thick_line(self, x0, y0, x1, y1, c, r=1):
        steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(steps + 1):
            t = i / steps
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            for dy in range(-r + 1, r):
                for dx in range(-r + 1, r):
                    self.set(int(round(x + dx)), int(round(y + dy)), c)

    def outline(self):
        """A one-pixel black edge around everything drawn: the Amiga look."""
        edge = []
        for y in range(self.h):
            for x in range(self.w):
                if self.px[y][x] != T:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < self.w and 0 <= ny < self.h and self.px[ny][nx] not in (T, OUTLINE):
                        edge.append((x, y))
                        break
        for x, y in edge:
            self.px[y][x] = OUTLINE
        return self

    def flat(self):
        return [c for row in self.px for c in row]


# --- Sprites -------------------------------------------------------------

def car(lean):
    """A low sports car from behind, 48x24. lean -1/0/1 shifts the cabin
    the way the car turns, so steering reads at a glance."""
    c = Canvas(48, 24)
    shift = lean * 2
    # Tyres first, so the body overlaps them.
    c.rect(2, 15, 9, 22, TYRE)
    c.rect(38, 15, 45, 22, TYRE)
    c.rect(3, 16, 4, 21, CHROME)
    c.rect(43, 16, 44, 21, CHROME)
    # Cabin and rear window, leaning.
    c.rect(13 + shift, 1, 34 + shift, 3, BODY)
    c.rect(11 + shift, 4, 36 + shift, 7, OUTLINE)
    c.rect(13 + shift, 4, 34 + shift, 6, WALL)
    # Body: widening toward the wheels.
    for y in range(7, 19):
        half = 17 + min(y - 7, 5)
        c.rect(24 - half, y, 23 + half, y, BODY)
    c.rect(2, 8, 45, 8, STRIPE)
    # Slatted tail, tail lights, bumper, exhausts.
    for y in (11, 13):
        c.rect(12, y, 35, y, OUTLINE)
    c.rect(3, 10, 10, 13, TAIL)
    c.rect(37, 10, 44, 13, TAIL)
    c.rect(4, 16, 43, 17, CHROME)
    c.rect(14, 18, 16, 19, OUTLINE)
    c.rect(31, 18, 33, 19, OUTLINE)
    return c.outline()


def palm():
    c = Canvas(40, 72)
    # A trunk with a lean, ringed.
    for i in range(52):
        y = 71 - i
        x = 20 + int(5 * math.sin(i / 52 * math.pi * 0.7))
        c.rect(x - 2, y, x + 1, y, TRUNK)
        if i % 5 == 0:
            c.rect(x - 2, y, x + 1, y, ROCK_DARK)
    top_x, top_y = 20 + int(5 * math.sin(0.7 * math.pi)), 71 - 52
    # Fronds: drooping arcs, light on top, dark under.
    for angle in (-160, -125, -90, -55, -20, 200):
        a = math.radians(angle)
        for i in range(17):
            t = i / 16
            x = top_x + math.cos(a) * 17 * t
            y = top_y + math.sin(a) * 10 * t + 9 * t * t
            c.thick_line(x, y, x, y + 2, LEAF_DARK, 1)
            c.set(int(round(x)), int(round(y)), LEAF_LIGHT)
    c.ellipse(top_x, top_y + 1, 3, 2, TRUNK)
    return c.outline()


def cactus():
    c = Canvas(28, 52)
    c.rect(11, 6, 16, 51, CACTUS_DARK)
    c.ellipse(13.5, 6, 3, 3, CACTUS_DARK)
    c.rect(13, 4, 13, 51, CACTUS_LIGHT)
    # Two arms.
    c.rect(4, 22, 10, 26, CACTUS_DARK)
    c.rect(4, 12, 8, 25, CACTUS_DARK)
    c.rect(5, 12, 5, 24, CACTUS_LIGHT)
    c.rect(17, 28, 23, 32, CACTUS_DARK)
    c.rect(19, 16, 23, 31, CACTUS_DARK)
    c.rect(21, 16, 21, 30, CACTUS_LIGHT)
    return c.outline()


def rock():
    c = Canvas(44, 26)
    c.ellipse(22, 16, 20, 10, ROCK_DARK)
    c.ellipse(16, 12, 11, 7, ROCK_LIGHT)
    c.ellipse(30, 14, 9, 6, ROCK_DARK)
    rnd = random.Random(7)
    for _ in range(40):  # A little dither, by hand as it were.
        x, y = rnd.randrange(4, 40), rnd.randrange(8, 24)
        if c.px[y][x] == ROCK_DARK:
            c.set(x, y, ROCK_LIGHT if (x + y) % 2 else ROCK_DARK)
    return c.outline()


def lamp():
    c = Canvas(20, 72)
    c.rect(4, 8, 6, 71, CHROME)
    c.rect(4, 6, 15, 8, CHROME)
    c.rect(12, 9, 17, 11, GLOW)
    c.rect(3, 66, 7, 71, WALL)
    return c.outline()


def sign():
    """A roadside chevron board on two posts."""
    c = Canvas(48, 36)
    c.rect(8, 20, 10, 35, CHROME)
    c.rect(37, 20, 39, 35, CHROME)
    c.rect(1, 1, 46, 21, SIGN)
    for k in range(3):
        x0 = 8 + k * 12
        for i in range(8):
            c.rect(x0 + i, 4 + i, x0 + i + 3, 4 + i, OUTLINE)
            c.rect(x0 + i, 17 - i, x0 + i + 3, 17 - i, OUTLINE)
    return c.outline()


def tower():
    """A city block for the night track: a dark wall of lit windows."""
    c = Canvas(48, 96)
    c.rect(2, 6, 45, 95, WALL)
    c.rect(18, 0, 29, 6, WALL)
    c.rect(23, 0, 24, 0, TAIL)
    rnd = random.Random(11)
    for y in range(10, 92, 6):
        for x in range(6, 42, 6):
            c.rect(x, y, x + 2, y + 3, WINDOW if rnd.random() < 0.55 else SKYLINE)
    return c.outline()


def banner():
    """The checkpoint gantry: two posts and a chequered beam, drawn the
    width of the road."""
    c = Canvas(128, 40)
    c.rect(2, 8, 6, 39, CHROME)
    c.rect(121, 8, 125, 39, CHROME)
    c.rect(2, 2, 125, 13, WHITE)
    for x in range(2, 126, 6):
        for y in (2, 8):
            if ((x // 6) + (y // 6)) % 2 == 0:
                c.rect(x, y, x + 5, y + 5, OUTLINE)
    return c.outline()


def explosion(frame):
    c = Canvas(56, 44)
    rnd = random.Random(31 + frame)
    radius = 8 + frame * 6
    for colour, scale in ((FIRE_RED, 1.0), (FIRE_ORANGE, 0.75), (FIRE_YELLOW, 0.45)):
        for _ in range(9):
            a = rnd.random() * math.tau
            d = rnd.random() * radius * 0.5 * scale
            r = radius * scale * (0.45 + rnd.random() * 0.3)
            c.ellipse(28 + math.cos(a) * d, 26 + math.sin(a) * d * 0.7, r, r * 0.8, colour)
    if frame == 2:  # Smoke takes over.
        for _ in range(6):
            c.ellipse(28 + rnd.randint(-12, 12), 14 + rnd.randint(-6, 6), 6, 5, ROCK_DARK)
    return c.outline()


# --- Backdrops: one wrapping strip per track -----------------------------

BACKDROP_W, BACKDROP_H = 512, 64


def backdrop_coast():
    c = Canvas(BACKDROP_W, BACKDROP_H)
    rnd = random.Random(3)
    for cx in range(0, BACKDROP_W, 96):  # Clouds.
        x = cx + rnd.randint(0, 40)
        y = rnd.randint(6, 18)
        c.ellipse(x, y, 16, 5, CLOUD_SHADE)
        c.ellipse(x - 4, y - 2, 12, 4, CLOUD)
    for i in range(4):  # Islands.
        x = 60 + i * 130 + rnd.randint(-20, 20)
        w = rnd.randint(30, 60)
        for dx in range(-w, w + 1):
            h = int(18 * (1 - (dx / w) ** 2))
            c.rect(x + dx, 52 - h, x + dx, 52, FAR)
    c.rect(0, 50, BACKDROP_W - 1, 63, SEA)
    for y in range(52, 64, 3):
        for x in range(rnd.randint(0, 9), BACKDROP_W, 13):
            c.rect(x, y, x + 4, y, SEA_LIGHT)
    return c


def backdrop_desert():
    c = Canvas(BACKDROP_W, BACKDROP_H)
    rnd = random.Random(5)
    for dx in range(BACKDROP_W):  # Far dunes.
        h = int(8 + 5 * math.sin(dx / BACKDROP_W * math.tau * 3))
        c.rect(dx, 63 - h, dx, 63, FAR)
    for i in range(5):  # Mesas: flat tops, shaded right flank.
        x = 30 + i * 100 + rnd.randint(-15, 15)
        w = rnd.randint(22, 44)
        h = rnd.randint(22, 38)
        c.rect(x - w, 63 - h, x + w, 63, MESA_LIGHT)
        c.rect(x + w // 3, 63 - h, x + w, 63, MESA_DARK)
        for s in range(6):  # Sloping skirts.
            c.rect(x - w - s * 2, 63 - h + 6 + s * 5, x - w, 63, MESA_LIGHT)
            c.rect(x + w, 63 - h + 6 + s * 5, x + w + s * 2, 63, MESA_DARK)
    return c


def backdrop_night():
    c = Canvas(BACKDROP_W, BACKDROP_H)
    rnd = random.Random(9)
    c.ellipse(400, 10, 7, 7, MOON)
    c.ellipse(403, 8, 5, 5, T)  # A crescent.
    x = 0
    while x < BACKDROP_W:
        w = rnd.randint(14, 34)
        h = rnd.randint(14, 50)
        c.rect(x, 63 - h, min(x + w, BACKDROP_W - 1), 63, SKYLINE)
        for wy in range(63 - h + 3, 62, 4):
            for wx in range(x + 2, min(x + w - 1, BACKDROP_W - 1), 4):
                if rnd.random() < 0.35:
                    c.set(wx, wy, WINDOW)
        x += w + rnd.randint(0, 3)
    return c


# --- Font: 5x7 in an 8x8 cell -------------------------------------------

FONT = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11100", "10010", "10001", "10001", "10001", "10010", "11100"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "10001", "11001", "10101", "10011", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
    "<": ["00010", "00100", "01000", "10000", "01000", "00100", "00010"],
    ">": ["01000", "00100", "00010", "00001", "00010", "00100", "01000"],
    " ": ["00000"] * 7,
}


# --- Output ----------------------------------------------------------------

def c_bytes(values, per_line=24):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def palette_for(track):
    colours = dict(BASE)
    colours.update(track["slots"])
    return [rgb565(hexrgb(colours[i])) for i in range(SLOTS)]


def write_preview(path, sprites, backdrops, scale=3):
    """A contact sheet: one row of sprites per track palette, backdrops below."""
    rows = []
    for t, track in enumerate(TRACKS):
        colours = dict(BASE)
        colours.update(track["slots"])
        rgb = {i: hexrgb(colours[i]) for i in range(SLOTS)}
        sky = hexrgb(track["sky"][3])
        items = [c for _, c in sprites] + [backdrops[t]]
        height = max(c.h for c in items)
        width = sum(c.w + 4 for c in items)
        band = [[sky] * width for _ in range(height)]
        x0 = 0
        for c in items:
            for y in range(c.h):
                for x in range(c.w):
                    if c.px[y][x] != T:
                        band[height - c.h + y][x0 + x] = rgb[c.px[y][x]]
            x0 += c.w + 4
        rows += band + [[(0, 0, 0)] * width for _ in range(4)]
    width = max(len(r) for r in rows)
    raw = b""
    for r in rows:
        line = [r[x] if x < len(r) else (0, 0, 0) for x in range(width)]
        scaled = b"".join(bytes(p) * scale for p in line)
        raw += (b"\0" + scaled) * scale

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", width * scale, len(rows) * scale, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    Path(path).write_bytes(png)


def main():
    sprites = [
        ("kCarStraight", car(0)), ("kCarLeft", car(-1)), ("kCarRight", car(1)),
        ("kPalm", palm()), ("kCactus", cactus()), ("kRock", rock()),
        ("kLamp", lamp()), ("kSign", sign()), ("kTower", tower()),
        ("kBanner", banner()),
        ("kExplosion0", explosion(0)), ("kExplosion1", explosion(1)),
        ("kExplosion2", explosion(2)),
    ]
    backdrops = [backdrop_coast(), backdrop_desert(), backdrop_night()]

    header = [
        "// Generated by scripts/generate-circuit-sprites.py -- do not edit.",
        "// Circuit's pixel art (ADR 0030), drawn by that script.",
        "#pragma once", "", "#include <cstdint>", "",
        "namespace drehklang::games::circuit_art {", "",
        "// 8-bit indexed pixels, row-major; index 0 is transparent.",
        "struct Sprite {", "  uint16_t width;", "  uint16_t height;",
        "  const uint8_t *pixels;", "};", "",
        "// Colours a track paints without sprites: the sky in hard bands",
        "// from the top down to the horizon, and the road's alternating",
        "// light/dark bands.",
        "struct Palette {",
        f"  uint16_t slots[{SLOTS}];  // RGB565 for each sprite index",
        "  uint16_t sky[6];", "  uint16_t grass[2];", "  uint16_t road[2];",
        "  uint16_t rumble[2];", "  uint16_t lane;", "};", "",
        f"constexpr int kPaletteSlots = {SLOTS};",
        f"constexpr uint8_t kBodySlot = {BODY};",
        f"constexpr uint8_t kStripeSlot = {STRIPE};",
        f"constexpr int kLiveryCount = {len(LIVERIES)};",
        "// Body and stripe colours of the computer cars, RGB565.",
        "extern const uint16_t kLiveries[kLiveryCount][2];", "",
    ]
    for name, _ in sprites:
        header.append(f"extern const Sprite {name};")
    header += [
        "", f"constexpr int kTrackCount = {len(TRACKS)};",
        "extern const Palette kPalettes[kTrackCount];",
        "extern const Sprite kBackdrops[kTrackCount];", "",
        "// 5x7 glyphs in an 8x8 cell, one byte per row, bit 4 leftmost.",
        "// nullptr for a character the font does not have.",
        "const uint8_t *glyph(char c);", "",
        "}  // namespace drehklang::games::circuit_art", "",
    ]

    body = [
        "// Generated by scripts/generate-circuit-sprites.py -- do not edit.",
        '#include "CircuitSprites.h"', "",
        "namespace drehklang::games::circuit_art {", "", "namespace {", "",
    ]
    for name, canvas in sprites:
        body += [f"const uint8_t {name}Pixels[] = {{", c_bytes(canvas.flat()), "};", ""]
    for i, canvas in enumerate(backdrops):
        body += [f"const uint8_t kBackdrop{i}Pixels[] = {{", c_bytes(canvas.flat()), "};", ""]
    glyph_chars = sorted(FONT)
    body.append("const uint8_t kGlyphs[][8] = {")
    for ch in glyph_chars:
        rows = [int(r, 2) for r in FONT[ch]] + [0]
        body.append("    {" + ", ".join(str(r) for r in rows) + "},  // " + repr(ch))
    body += ["};", "", f'const char kGlyphChars[] = "{"".join(glyph_chars)}";', "",
             "}  // namespace", ""]
    for name, canvas in sprites:
        body.append(f"const Sprite {name}{{{canvas.w}, {canvas.h}, {name}Pixels}};")
    body.append("")
    body.append("const uint16_t kLiveries[kLiveryCount][2] = {")
    for b, s in LIVERIES:
        body.append(f"    {{{rgb565(hexrgb(b))}, {rgb565(hexrgb(s))}}},")
    body += ["};", "", "const Palette kPalettes[kTrackCount] = {"]
    for track in TRACKS:
        def row(key):
            return ", ".join(str(rgb565(hexrgb(c))) for c in track[key])
        body.append("    {{" + ", ".join(str(v) for v in palette_for(track)) + "},")
        body.append(f"     {{{row('sky')}}}, {{{row('grass')}}}, {{{row('road')}}},")
        body.append(f"     {{{row('rumble')}}}, {rgb565(hexrgb(track['lane']))}}},")
    body += ["};", "", "const Sprite kBackdrops[kTrackCount] = {"]
    for i in range(len(backdrops)):
        body.append(f"    {{{BACKDROP_W}, {BACKDROP_H}, kBackdrop{i}Pixels}},")
    body += ["};", "",
             "const uint8_t *glyph(char c) {",
             "  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');",
             "  for (int i = 0; kGlyphChars[i] != '\\0'; ++i) {",
             "    if (kGlyphChars[i] == c) return kGlyphs[i];",
             "  }",
             "  return nullptr;",
             "}", "",
             "}  // namespace drehklang::games::circuit_art", ""]

    (OUT_DIR / "CircuitSprites.h").write_text("\n".join(header))
    (OUT_DIR / "CircuitSprites.cpp").write_text("\n".join(body))
    total = sum(c.w * c.h for _, c in sprites) + sum(c.w * c.h for c in backdrops)
    if "--preview" in sys.argv:
        write_preview(sys.argv[sys.argv.index("--preview") + 1], sprites, backdrops)
    print(f"wrote {len(sprites)} sprites, {len(backdrops)} backdrops, "
          f"{len(FONT)} glyphs, {total} bytes of pixels")


if __name__ == "__main__":
    main()
