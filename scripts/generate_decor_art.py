#!/usr/bin/env python3
"""Generate the decor cut-out sprites (pix/16d*.png) for BASE+DECOR layering.

Deterministic, stdlib-only source of truth for the committed PNGs: rerunning
it reproduces the same bytes (cutouts are pure functions of the committed
legacy tile art; the two procedural sprites are pure functions of (x, y, salt)
and zlib.compress(data, 9) is stable). Run from the repo root:

    python3 scripts/generate_decor_art.py

Design: BASE + DECOR tile layering (docs in the tile-layering design; ids in
include/openglad/core/decordefs.h). Unlike the opaque floor tiles from
generate_tile_art.py, decor sprites are TRANSPARENT: index 0 marks see-through
pixels and the renderer blits them through the sprite path (walkputbuffer),
never the opaque tile path. The legacy combined tiles have NO transparent
background (0 index-0 pixels), so each sprite is cut from its combined tile by
a PER-DECOR foreground rule (a global band set does not work — see brazier):

  - torches (16torch1/2/3): fg = flame band 224-231 (cycled ORANGE — do_cycle
    keeps animating the flame for free) + handle browns 132-143. Composite
    over PIX_WALLSIDE_C (16brickc) leaves 21 residual px, max delta 61 (the
    torch background is a slightly different brick-mortar layout in the same
    grey band).
  - boulders (16stone1..4): fg = greys 16-31 over grass greens. Residuals:
    stone2 over 16grass3 = 0 px; stone1 = 3 px <= 20 and stone3/4 = 3 px <= 40
    over 16grass2.
  - pebbles (16grassr): fg = greys 16-31 (17/18/19). 54 residual px <= 40 over
    16grassd (a different mix of the same dark greens).
  - brazier (16braz1): a band cutout DOES NOT WORK — the bowl browns (140/142)
    are the same band as the plank background. SHAPE STENCIL instead:
    fg = (16braz1 byte != 16floor byte) OR flame band. Composite over
    PIX_FLOOR1 (16floor) is 0 residual BY CONSTRUCTION; on other bases the
    sprite carries ~12 plank-fringe px (accepted, invisible at 1x).
  - shrub / bones: NEW procedural art (no legacy combined tile). Shrub is a
    concealing leafy blob (grass greens + murky greens); bones are a walkable
    white scatter. Both keep out of the cycled bands 208-231 (glass-flashing
    precedent) and out of the team-recolor range >= 248.

Columns are NOT generated: DECOR_COLUMN_* reuse 16colm0/16colm1 verbatim
(their art already has index-0 transparency).

The palette (PLTE + tRNS) is lifted VERBATIM from a donor tile so
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

FLAME_BAND = set(range(224, 232))      # cycled ORANGE: torch/brazier fire only
WATER_BAND = set(range(208, 224))      # cycled WATER: forbidden everywhere
BROWN_BAND = set(range(132, 144))      # torch handle browns
GREY_BAND = set(range(16, 32))         # boulder/pebble greys
TEAM_RECOLOR_MIN = 248                 # walkputbuffer team-recolors >= 248


# --- PNG decode (committed legacy tiles are 8-bit indexed, any filter) ------

def read_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path}: not a PNG"
    off = 8
    idat = b""
    plte = trns = ihdr = None
    while off < len(data):
        (length,) = struct.unpack(">I", data[off:off + 4])
        ctype = data[off + 4:off + 8]
        payload = data[off + 8:off + 8 + length]
        if ctype == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", payload)
        elif ctype == b"PLTE":
            plte = payload
        elif ctype == b"tRNS":
            trns = payload
        elif ctype == b"IDAT":
            idat += payload
        off += 12 + length
    w, h, depth, ctype_, _, _, interlace = ihdr
    assert (w, h, depth, ctype_, interlace) == (SIZE, SIZE, 8, 3, 0), \
        f"{path}: want {SIZE}x{SIZE} 8-bit indexed non-interlaced, got {ihdr}"
    raw = zlib.decompress(idat)
    rows = []
    prev = bytes(w)
    pos = 0
    for _ in range(h):
        filt = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + w])
        pos += w
        if filt == 1:    # Sub
            for i in range(1, w):
                line[i] = (line[i] + line[i - 1]) & 255
        elif filt == 2:  # Up
            for i in range(w):
                line[i] = (line[i] + prev[i]) & 255
        elif filt == 3:  # Average
            for i in range(w):
                a = line[i - 1] if i else 0
                line[i] = (line[i] + (a + prev[i]) // 2) & 255
        elif filt == 4:  # Paeth
            for i in range(w):
                a = line[i - 1] if i else 0
                b = prev[i]
                c = prev[i - 1] if i else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        else:
            assert filt == 0, f"{path}: unknown filter {filt}"
        prev = bytes(line)
        rows.append(list(line))
    assert plte is not None and len(plte) == 768, f"{path}: PLTE must be 768 bytes"
    return rows, plte, trns


def read_donor_palette(path):
    _, plte, trns = read_png(path)
    return plte, trns


# --- cutouts -----------------------------------------------------------------

def band_cutout(rows, fg_bands):
    """Keep pixels whose index is in any fg band; everything else -> 0."""
    fg = set()
    for band in fg_bands:
        fg |= band
    return [[v if v in fg else 0 for v in row] for row in rows]


def shape_stencil(rows, base_rows):
    """Keep pixels that differ from the reference base tile, plus the flame
    band (a flame pixel may coincide with a base byte only by palette
    accident; the fire must always survive the cut)."""
    return [[v if (v != b or v in FLAME_BAND) else 0
             for v, b in zip(row, brow)]
            for row, brow in zip(rows, base_rows)]


# --- procedural sprites (same hash family as generate_tile_art.py) ----------

def hash_u32(v):
    v &= M
    v ^= v >> 16
    v = (v * 0x7FEB352D) & M
    v ^= v >> 15
    v = (v * 0x846CA68B) & M
    v ^= v >> 16
    return v


def h(x, y, salt):
    return hash_u32((salt << 16) ^ (y << 4) ^ x)


def shrub(x, y, salt):
    """Concealing leafy blob: a rough ellipse of grass greens (60-63) with
    murky-green (160-165) shadow bites, transparent margin all around."""
    dx, dy = x - 7.5, y - 8.0
    r2 = dx * dx + dy * dy * 1.35
    edge = 42.0 + (h(x, y, salt) % 12)  # ragged leaf edge, deterministic
    if r2 * 8.0 > edge * 8.0:
        return 0
    roll = h(x, y, salt + 1) % 1000
    if r2 > 26.0:  # outer canopy: darker, with murky bites
        if roll < 450:
            return 63
        if roll < 700:
            return 62
        if roll < 900:
            return 161
        return 162
    # inner canopy: brighter crown with sparse highlights
    if roll < 300:
        return 61
    if roll < 550:
        return 62
    if roll < 750:
        return 60
    if roll < 900:
        return 63
    return 160


BONES = [
    "................",
    "................",
    "....dw..........",
    "...dWWw.....dw..",
    "....dw.....dWw..",
    "............d...",
    "..w.............",
    "..Wd..wWWd......",
    "..wd............",
    "......dWw...ww..",
    ".....dWw...dWWd.",
    "................",
    "..dw........dw..",
    "..wWWd.......w..",
    "................",
    "................",
]


def bones(x, y, salt):
    """Walkable bone scatter: fixed skeletal layout, per-pixel white shade."""
    c = BONES[y][x]
    if c == ".":
        return 0
    if c == "d":
        return 25  # bone shadow grey
    if c == "w":
        return 28 + (h(x, y, salt) % 2)  # 28/29 worn bone
    return 30 + (h(x, y, salt) % 2)      # 30/31 bright bone (c == 'W')


# --- fidelity measurement (the committed proof numbers) ----------------------

def composite(base_rows, cut_rows):
    return [[c if c != 0 else b for b, c in zip(br, cr)]
            for br, cr in zip(base_rows, cut_rows)]


def residual(plte, got_rows, want_rows):
    """(pixel count, max channel delta) where composite != legacy tile."""
    count, max_delta = 0, 0
    for gr, wr in zip(got_rows, want_rows):
        for g, w in zip(gr, wr):
            if g != w:
                delta = max(abs(plte[3 * g + k] - plte[3 * w + k])
                            for k in range(3))
                count += 1
                max_delta = max(max_delta, delta)
    return count, max_delta


# --- PNG encode (identical plumbing to generate_tile_art.py) -----------------

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


# --- recipes ------------------------------------------------------------------

# (output, source, fg rule) + the pinned fidelity measurement:
# (implied base, expected residual px, expected max channel delta).
CUTOUTS = [
    ("16dtorch1.png", "16torch1.png", [FLAME_BAND, BROWN_BAND], "16brickc.png", 21, 61),
    ("16dtorch2.png", "16torch2.png", [FLAME_BAND, BROWN_BAND], "16brickc.png", 21, 61),
    ("16dtorch3.png", "16torch3.png", [FLAME_BAND, BROWN_BAND], "16brickc.png", 21, 61),
    ("16dstone1.png", "16stone1.png", [GREY_BAND], "16grass2.png", 3, 20),
    ("16dstone2.png", "16stone2.png", [GREY_BAND], "16grass3.png", 0, 0),
    ("16dstone3.png", "16stone3.png", [GREY_BAND], "16grass2.png", 3, 40),
    ("16dstone4.png", "16stone4.png", [GREY_BAND], "16grass2.png", 3, 40),
    ("16dpebble.png", "16grassr.png", [GREY_BAND], "16grassd.png", 54, 40),
]

PROCEDURAL = {
    "16dshrub.png": (shrub, 21),
    "16dbones.png": (bones, 22),
}

FLAME_SPRITES = {"16dtorch1.png", "16dtorch2.png", "16dtorch3.png", "16dbraz.png"}


def check(name, rows):
    flat = [v for row in rows for v in row]
    fg = [v for v in flat if v != 0]
    assert fg, f"{name}: cutout is empty"
    assert 0 in flat, f"{name}: decor must have transparent (index 0) pixels"
    assert all(v < TEAM_RECOLOR_MIN for v in fg), \
        f"{name}: indices >= {TEAM_RECOLOR_MIN} would team-recolor"
    assert not any(v in WATER_BAND for v in fg), \
        f"{name}: WATER cycled band is forbidden (flashing)"
    orange = [v for v in fg if v in FLAME_BAND]
    if name in FLAME_SPRITES:
        assert orange, f"{name}: fire must keep cycled flame pixels"
    else:
        assert not orange, \
            f"{name}: non-flame decor may not touch a cycled band (flashing)"


def main():
    plte, trns = read_donor_palette(DONOR)
    wrote = 0

    for name, source, fg_bands, base, want_px, want_delta in CUTOUTS:
        src_rows, _, _ = read_png(PIX / source)
        rows = band_cutout(src_rows, fg_bands)
        check(name, rows)
        base_rows, _, _ = read_png(PIX / base)
        got_px, got_delta = residual(plte, composite(base_rows, rows), src_rows)
        assert (got_px, got_delta) == (want_px, want_delta), (
            f"{name}: composite over {base} measured {got_px} px / "
            f"delta {got_delta}, pinned {want_px} px / delta {want_delta}")
        write_png(PIX / name, rows, plte, trns)
        print(f"wrote pix/{name} (cut from {source}; residual {got_px} px "
              f"<= delta {got_delta} over {base})")
        wrote += 1

    # Brazier: shape stencil against the plank floor (see module docstring).
    braz_rows, _, _ = read_png(PIX / "16braz1.png")
    floor_rows, _, _ = read_png(PIX / "16floor.png")
    rows = shape_stencil(braz_rows, floor_rows)
    check("16dbraz.png", rows)
    got_px, got_delta = residual(plte, composite(floor_rows, rows), braz_rows)
    assert (got_px, got_delta) == (0, 0), \
        f"16dbraz.png: stencil must be exact over 16floor, got {got_px} px"
    write_png(PIX / "16dbraz.png", rows, plte, trns)
    print("wrote pix/16dbraz.png (shape stencil vs 16floor; 0 residual)")
    wrote += 1

    for name, (recipe, salt) in PROCEDURAL.items():
        rows = [[recipe(x, y, salt) for x in range(SIZE)] for y in range(SIZE)]
        check(name, rows)
        write_png(PIX / name, rows, plte, trns)
        print(f"wrote pix/{name} (procedural, salt {salt})")
        wrote += 1

    print(f"{wrote} decor sprites OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
