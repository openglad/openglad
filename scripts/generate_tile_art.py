#!/usr/bin/env python3
"""Generate the eight Westlands terrain tiles (pix/16{snow,lava,marsh,ash}{1,2}.png)
and the two standalone column tiles (pix/16colm{0,1}.png).

Deterministic, stdlib-only source of truth for the committed PNGs: rerunning it
reproduces the same bytes (every pixel is a pure function of (x, y, salt) and
zlib.compress(data, 9) is stable). Run from the repo root:

    python3 scripts/generate_tile_art.py

Design: scratchpad westlands tiles design (see docs/z-axis-design.md for the
add-a-tile engine checklist). Key palette facts (our.pal, 6-bit RGB):
  - 208-223 WATER cycled band, 224-231 ORANGE cycled band: do_cycle rotates
    these every frame. LAVA deliberately lives in 224-231 (the rotation IS the
    flow animation); nothing else may touch 208-231. MARSH glints are STATIC
    pale cyans (117/118) — cycled glints blinked and tiled as a dot grid
    (playtest bug #13).
  - 232-233 static copy of the fire ramp (lava crust), 134 dark brown (cracks).
  - 27-31 white/grey ramp (snow), 139-142 mud browns + 160-165 murky greens
    (marsh), 248-253 warm dark grey-browns + 3 cold dark grey (ash).
  - Index 0 is transparent-by-convention: never used (opaque floor tiles).

The two column tiles replaced legacy art that was an 8x8 fragment of a
two-tile dungeon column in one corner of an otherwise transparent tile (it
rendered as a black square with a grey corner when the Westlands maps placed
it as a standalone pillar on open pavement). The new art is a self-contained
TOP-DOWN pillar filling the tile: 16colm0 is a round fluted stone drum
(specular offset toward the upper-left light, notched rim, neutral dark
shadow ring), 16colm1 is a square chamfered plinth plate with a cracked top.
Both are dual-use — opaque grid tiles PIX_COLUMN1/PIX_COLUMN2 AND decor
cut-outs DECOR_COLUMN_BOTTOM/TOP (graphlib.cpp loads the same PNGs twice) —
so exactly the four extreme corner pixels stay index 0: the decor path needs
transparency (tests/unit/test_decor_art.cpp pins >= 1 transparent pixel) and
on the opaque tile path those four pixels read as the darkest corner of the
shadow ring (palette 0 is black). Column shading is INTEGER-ONLY (doubled
coordinates, squared distances, comparison-based sectors) — no libm
transcendentals whose last-ulp rounding could differ across platforms.

The palette (PLTE + tRNS chunks) is lifted VERBATIM from a donor tile so
read_pixie_file's +-1 palette conformance check passes; never let an image
editor re-save these files.
"""

import math
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PIX = REPO / "pix"
DONOR = PIX / "16grass1.png"
SIZE = 16

M = 0xFFFFFFFF


def hash_u32(v):
    """The engine's xorshift-multiply finalizer (render/effects.cpp), 32-bit."""
    v &= M
    v ^= v >> 16
    v = (v * 0x7FEB352D) & M
    v ^= v >> 15
    v = (v * 0x846CA68B) & M
    v ^= v >> 16
    return v


def h(x, y, salt):
    """Per-pixel roll, x,y in 0..15."""
    return hash_u32((salt << 16) ^ (y << 4) ^ x)


def hcell(cx, cy, salt):
    """Per-4x4-cell roll, cx,cy in 0..3."""
    return hash_u32((salt << 8) ^ (cy << 2) ^ cx)


def pick(r, table):
    """Weighted pick: r in 0..999 against a cumulative (threshold, index) table."""
    for threshold, index in table:
        if r < threshold:
            return index
    raise AssertionError("cumulative table must end at 1000")


# --- per-tile recipes ------------------------------------------------------

SNOW_TABLE = [
    (620, 30),   # body (54,54,54)
    (800, 31),   # sparkle highlights (57,57,57)
    (940, 29),   # soft shadow grain (51,51,51)
    (990, 28),   # footprint/dimple shadow (48,48,48)
    (1000, 27),  # rare deep dimple (45,45,45)
]


def snow(x, y, salt):
    return pick(h(x, y, salt) % 1000, SNOW_TABLE)


def lava(x, y, salt):
    # Static crust plates: ~30% of the sixteen 4x4 cells. Crust never leaves
    # its cell, so there is no seam constraint.
    cx, cy = x // 4, y // 4
    if hcell(cx, cy, salt) % 10 < 3:
        lx, ly = x % 4, y % 4
        if lx in (1, 2) and ly in (1, 2):
            return 233  # static red-orange plate core
        if lx in (0, 3) and ly in (0, 3):
            return 134  # dark brown crack pit at the corners
        return 232      # static deep red rim
    # Flow field: diagonal banding across the full ORANGE cycled ramp so the
    # do_cycle rotation reads as crawl. The band term is salt-independent
    # (x period 16 = 0 mod 8, 2y period 32 = 0 mod 8), so lava rivers flow
    # phase-locked across tile borders and across mixed variants.
    return 224 + ((x + 2 * y + (h(x, y, salt) % 2)) & 7)


MARSH_MUD_TABLE = [
    (450, 140),   # (30,20,10)
    (800, 141),   # (25,15,5)
    (950, 142),   # (20,10,0)
    (1000, 139),  # (35,25,15)
]

MARSH_BOG_TABLE = [
    (400, 162),   # body (0,13,5)
    (650, 163),   # body dark (0,11,5)
    (800, 161),   # body light (0,16,6)
    (900, 164),   # deep murk (0,8,3)
    (955, 140),   # mud fleck
    (990, 165),   # black-green pit (0,6,2)
    (995, 118),   # static pale-cyan glint (28,39,39) -- wet sheen
    (1000, 117),  # static pale-cyan glint (26,42,42), brighter
]


def marsh(x, y, salt):
    cx, cy = x // 4, y // 4
    r = h(x, y, salt) % 1000
    if hcell(cx, cy, salt + 50) % 5 == 0:  # ~20% mud-clump cells
        return pick(r, MARSH_MUD_TABLE)
    return pick(r, MARSH_BOG_TABLE)


ASH_TABLE = [
    (380, 249),   # (30,20,20)
    (680, 250),   # (32,22,22)
    (830, 248),   # (28,18,18)
    (930, 251),   # (34,24,24)
    (965, 252),   # (36,26,26)
    (990, 3),     # cold dark grey pit (24,24,24)
    (1000, 253),  # (38,28,28)
]


def ash(x, y, salt):
    return pick(h(x, y, salt) % 1000, ASH_TABLE)


# --- column tiles (integer-only shading; see module docstring) ---------------
#
# Grey ramp: 17 (15,15,15) .. 31 (57,57,57) in steps of 3 (6-bit). Index 16 is
# a black duplicate and stays forbidden. Doubled coordinates X = 2x - 15,
# Y = 2y - 15 are odd integers (never zero), so squared distances and sector
# comparisons need no floats and no axis special cases.

GREY_LO, GREY_HI = 17, 31


def grey(v):
    return max(GREY_LO, min(GREY_HI, v))


def sector16(X, Y):
    """Angular sector 0..15, counter-clockwise from +X, by integer compares
    (quadrant boundaries at ~22/45/68 degrees; slight unevenness is invisible
    at 16px). Used to notch the drum rim into 8 flutes."""
    ax, ay = abs(X), abs(Y)
    if 5 * ay < 2 * ax:
        s = 0
    elif ay < ax:
        s = 1
    elif 2 * ay <= 5 * ax:
        s = 2
    else:
        s = 3
    if X > 0 and Y < 0:      # screen-up is -Y; quadrant 0 = up-right
        return s
    if X < 0 and Y < 0:
        return 7 - s
    if X < 0 and Y > 0:
        return 8 + s
    return 15 - s


def column_drum(x, y, salt):
    """16colm0: round fluted stone drum seen from above."""
    X, Y = 2 * x - 15, 2 * y - 15
    d2 = X * X + Y * Y            # 2 .. 450 (corner)
    if d2 > 400:                  # only the 4 extreme corners (d2 == 450)
        return 0
    if d2 > 190:                  # neutral shadow ring, radial falloff
        if d2 > 330:
            return 19
        return 18 if d2 > 260 else 17
    # Drum surface: dome ramp off a specular center offset up-left
    # (math.isqrt: exact integer sqrt, no float rounding).
    dl2 = (X + 5) * (X + 5) + (Y + 5) * (Y + 5)
    v = 31 - math.isqrt(dl2) // 2
    if 150 < d2 <= 190 and sector16(X, Y) % 2 == 1:
        v -= 3                    # rim notches: 8 flutes seen edge-on
    elif 160 < d2 <= 190:
        v -= 1                    # rim rolloff between the notches
    if 66 < d2 <= 92:
        v -= 1                    # concentric drum-ring groove
    v += h(x, y, salt) % 2        # stone grain
    return grey(v)


# Cracked-top polyline across the plinth plate (16colm1), dark grey pits.
PLINTH_CRACK = {
    (4, 5), (5, 5), (6, 6), (7, 6), (8, 7), (9, 7), (9, 8), (10, 8),
    (11, 9), (6, 7), (5, 8),
}


def column_plinth(x, y, salt):
    """16colm1: square chamfered plinth/capital plate with a cracked top."""
    if (x in (0, 15)) and (y in (0, 15)):
        return 0                  # transparent extreme corners (decor cut-out)
    if x in (0, 15) or y in (0, 15):
        return 17 if (x in (0, 15)) == (y in (0, 15)) else 18  # shadow ring
    ex = min(x - 1, 14 - x)       # distance into the plate, 0 = outer edge
    ey = min(y - 1, 14 - y)
    e = min(ex, ey)
    if e == 0:                    # chamfered outer bevel: lit up-left
        if y == 1 or (x == 1 and y < 14):
            return grey(29 + h(x, y, salt) % 2)
        return grey(20 + h(x, y, salt) % 2)
    if e == 1:                    # inner bevel step, softer
        if y == 2 or (x == 2 and y < 13):
            return grey(27)
        return grey(22 + h(x, y, salt) % 2)
    if (x, y) in PLINTH_CRACK:
        return 18                 # cracked top
    # Flat top: subtle up-left light plus stone grain.
    v = 25 - (x + y - 12) // 8 + h(x, y, salt) % 2
    return grey(v)


TILES = {
    "16snow1.png": (snow, 1),
    "16snow2.png": (snow, 2),
    "16lava1.png": (lava, 3),
    "16lava2.png": (lava, 4),
    "16marsh1.png": (marsh, 5),
    "16marsh2.png": (marsh, 6),
    "16ash1.png": (ash, 7),
    "16ash2.png": (ash, 8),
    "16colm0.png": (column_drum, 9),
    "16colm1.png": (column_plinth, 10),
}


# --- PNG plumbing ----------------------------------------------------------

def read_donor_palette(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path}: not a PNG"
    off = 8
    plte = trns = None
    while off < len(data):
        (length,) = struct.unpack(">I", data[off:off + 4])
        ctype = data[off + 4:off + 8]
        payload = data[off + 8:off + 8 + length]
        if ctype == b"PLTE":
            plte = payload
        elif ctype == b"tRNS":
            trns = payload
        off += 12 + length
    assert plte is not None and len(plte) == 768, "donor PLTE must be 768 bytes"
    return plte, trns


def chunk(ctype, payload):
    return (struct.pack(">I", len(payload)) + ctype + payload +
            struct.pack(">I", zlib.crc32(ctype + payload)))


def write_png(path, rows, plte, trns):
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 3, 0, 0, 0)
    idat = zlib.compress(b"".join(b"\x00" + bytes(row) for row in rows), 9)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"PLTE", plte)
    if trns is not None:
        out += chunk(b"tRNS", trns)
    out += chunk(b"IDAT", idat) + chunk(b"IEND", b"")
    path.write_bytes(out)


# --- self-checks -----------------------------------------------------------

def check(name, rows):
    flat = [v for row in rows for v in row]
    if "colm" in name:
        # Dual-use tile/decor: exactly the 4 extreme corners transparent
        # (decor cut-out rule), everything else in the grey ramp 17..31.
        zeros = [(x, y) for y in range(SIZE) for x in range(SIZE)
                 if rows[y][x] == 0]
        assert zeros == [(0, 0), (15, 0), (0, 15), (15, 15)], \
            f"{name}: transparency must be exactly the 4 corners, got {zeros}"
        assert all(GREY_LO <= v <= GREY_HI for v in flat if v != 0), \
            f"{name}: columns must stay in the grey ramp {GREY_LO}..{GREY_HI}"
        return
    assert 0 not in flat and 16 not in flat, f"{name}: index 0/16 forbidden"
    water = sum(1 for v in flat if 208 <= v <= 223)
    orange = sum(1 for v in flat if 224 <= v <= 231)
    if "snow" in name:
        assert all(27 <= v <= 31 for v in flat), f"{name}: snow must stay in 27..31"
    elif "lava" in name:
        allowed = set(range(224, 232)) | {232, 233, 134}
        assert set(flat) <= allowed, f"{name}: off-recipe index {set(flat) - allowed}"
        assert orange >= len(flat) * 40 // 100, f"{name}: <40% flowing pixels"
        assert water == 0, f"{name}: lava may not touch the WATER band"
    elif "marsh" in name:
        glints = sum(1 for v in flat if v in (117, 118))
        assert 1 <= glints <= 8, f"{name}: want 1..8 glints, got {glints}"
        assert water == 0 and orange == 0, \
            f"{name}: marsh may not touch a cycled band (blinking dot grid)"
    elif "ash" in name:
        assert water == 0 and orange == 0, f"{name}: ash must not shimmer"


def main():
    plte, trns = read_donor_palette(DONOR)
    for name, (recipe, salt) in TILES.items():
        rows = [[recipe(x, y, salt) for x in range(SIZE)] for y in range(SIZE)]
        check(name, rows)
        write_png(PIX / name, rows, plte, trns)
        print(f"wrote pix/{name} (salt {salt})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
