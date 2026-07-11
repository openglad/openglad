#!/usr/bin/env python3
"""Generate the eight Westlands terrain tiles (pix/16{snow,lava,marsh,ash}{1,2}.png).

Deterministic, stdlib-only source of truth for the committed PNGs: rerunning it
reproduces the same bytes (every pixel is a pure function of (x, y, salt) and
zlib.compress(data, 9) is stable). Run from the repo root:

    python3 scripts/generate_tile_art.py

Design: scratchpad westlands tiles design (see docs/z-axis-design.md for the
add-a-tile engine checklist). Key palette facts (our.pal, 6-bit RGB):
  - 208-223 WATER cycled band, 224-231 ORANGE cycled band: do_cycle rotates
    these every frame. LAVA deliberately lives in 224-231 (the rotation IS the
    flow animation); MARSH carries a couple of 208/209 glint pixels; nothing
    else may touch 208-231.
  - 232-233 static copy of the fire ramp (lava crust), 134 dark brown (cracks).
  - 27-31 white/grey ramp (snow), 139-142 mud browns + 160-165 murky greens
    (marsh), 248-253 warm dark grey-browns + 3 cold dark grey (ash).
  - Index 0 is transparent-by-convention: never used (opaque floor tiles).

The palette (PLTE + tRNS chunks) is lifted VERBATIM from a donor tile so
read_pixie_file's +-1 palette conformance check passes; never let an image
editor re-save these files.
"""

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
    (995, 208),   # WATER-band glint (cycled -- deliberate)
    (1000, 209),  # WATER-band glint
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


TILES = {
    "16snow1.png": (snow, 1),
    "16snow2.png": (snow, 2),
    "16lava1.png": (lava, 3),
    "16lava2.png": (lava, 4),
    "16marsh1.png": (marsh, 5),
    "16marsh2.png": (marsh, 6),
    "16ash1.png": (ash, 7),
    "16ash2.png": (ash, 8),
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
        glints = sum(1 for v in flat if v in (208, 209))
        assert 1 <= glints <= 8, f"{name}: want 1..8 glints, got {glints}"
        assert all(not (210 <= v <= 231) for v in flat), \
            f"{name}: only 208/209 may shimmer"
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
