#!/usr/bin/env python3
"""Census of the two autotiler gaps in src/gameplay/smooth.cpp, over the
committed campaign art.

Why a census at all
-------------------
`smoother::smooth()` decides a tile from the genre of its four neighbours,
packed as a 0-15 adjacency mask (TO_UP 1, TO_RIGHT 2, TO_DOWN 4, TO_LEFT 8).
Two arms of that switch never write a tile:

  * TYPE_WALL's inner `switch (around)` has no case 0, 2, 8 or 10, and its
    2002 `default:` keeps `herepix` -- so an isolated wall, the left or right
    end of a horizontal run, and a horizontal middle all keep whatever the
    generator happened to place instead of being autotiled.
  * TYPE_GRASS_DARK's mask-14 arm (TO_LEFT|TO_RIGHT|TO_DOWN, i.e. the top
    middle of a dark patch) is written `{} // do nothing`, which leaves
    `newvalue` on its `PIX_GRASS1` initialiser at the top of smooth().  The
    cell is then WRITTEN BACK as light grass.

The dark-grass fingerprint, and why it cascades
-----------------------------------------------
The smoother edits the grid in place, row by row, so the converted cell is
already light grass when the cell BELOW it is smoothed.  That cell now reads
UP == TYPE_GRASS (not dark), so it becomes the new top middle, takes the same
do-nothing arm, and turns light too -- and so on down the patch.  One stray
therefore paints a full light-green column through a dark-grass band, which
is what makes this quirk visible in a screenshot at all.

In finished art the fingerprint of that arm is: a PIX_GRASS1 cell whose left,
right and down neighbours are all TYPE_GRASS_DARK and whose up neighbour is
not.  (`up` is not dark precisely because the cascade already converted it,
or because the stray sits on the top row of the patch.)

Usage
-----
  scripts/media/smooth_mask_census.py                     # every campaign grid
  scripts/media/smooth_mask_census.py --campaign longseason
  scripts/media/smooth_mask_census.py --level campaigns/longseason/pix/scen0008.png
  scripts/media/smooth_mask_census.py --all-rows          # include zero rows

`--level` prints the cell coordinates (the same x,y the editor and
OPENGLAD_DEMO_CAPTURE_FOCUS=cell:<x>,<y> use) so a capture can be aimed at an
artefact.  Everything is read from the repository: the PIX_* ids from
include/openglad/core/pixdefs.h, the genres from
include/openglad/core/terrain_types.h, and the PIX -> genre table from
src/gameplay/smooth.cpp itself, so the census cannot drift away from the code
it describes.  Level grids are 8-bit indexed PNGs whose palette index IS the
PIX_* id; they are decoded here with zlib from the standard library, no
Pillow, no numpy.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import zlib

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

PIXDEFS_H = os.path.join(REPO_ROOT, "include", "openglad", "core", "pixdefs.h")
TERRAIN_H = os.path.join(REPO_ROOT, "include", "openglad", "core", "terrain_types.h")
SMOOTH_CPP = os.path.join(REPO_ROOT, "src", "gameplay", "smooth.cpp")

# The TYPE_WALL inner switch in smooth() has no arm for these adjacency masks.
WALL_GAP_MASKS = (0, 2, 8, 10)
WALL_GAP_NAMES = {
    0: "isolated",
    2: "left end",
    8: "right end",
    10: "horizontal middle",
}


# --------------------------------------------------------------------------
# Source parsing: the constants and the genre table come from the tree.
# --------------------------------------------------------------------------
def parse_defines(path: str, prefix: str) -> dict[str, int]:
    """`#define PIX_FOO 12` -> {'PIX_FOO': 12}."""
    out: dict[str, int] = {}
    pattern = re.compile(r"^\s*#define\s+(" + prefix + r"\w*)\s+(-?\d+)")
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = pattern.match(line)
            if match:
                out[match.group(1)] = int(match.group(2))
    if not out:
        raise SystemExit(f"no {prefix}* defines found in {path}")
    return out


def parse_constexpr_ints(path: str, prefix: str) -> dict[str, int]:
    """`inline constexpr int TYPE_FOO = 3;` -> {'TYPE_FOO': 3}."""
    out: dict[str, int] = {}
    pattern = re.compile(
        r"^\s*inline\s+constexpr\s+int\s+(" + prefix + r"\w*)\s*=\s*(-?\d+)\s*;")
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = pattern.match(line)
            if match:
                out[match.group(1)] = int(match.group(2))
    if not out:
        raise SystemExit(f"no {prefix}* constants found in {path}")
    return out


def parse_genre_table(path: str, pixdefs: dict[str, int],
                      types: dict[str, int]) -> dict[int, int]:
    """`table[PIX_FOO] = TYPE_BAR;` from make_pix_to_genre() -> {pix: genre}."""
    table: dict[int, int] = {}
    pattern = re.compile(r"^\s*table\[(PIX_\w+)\]\s*=\s*(TYPE_\w+)\s*;")
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = pattern.match(line)
            if not match:
                continue
            pix_name, type_name = match.group(1), match.group(2)
            if pix_name not in pixdefs:
                raise SystemExit(f"{path}: unknown {pix_name}")
            if type_name not in types:
                raise SystemExit(f"{path}: unknown {type_name}")
            table[pixdefs[pix_name]] = types[type_name]
    if not table:
        raise SystemExit(f"no genre table rows found in {path}")
    return table


# --------------------------------------------------------------------------
# Indexed PNG decoding (colour type 3, bit depth 8, no interlace)
# --------------------------------------------------------------------------
def decode_indexed_png(path: str) -> tuple[int, int, bytes]:
    """Return (width, height, palette indices, row-major)."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    pos = 8
    width = height = depth = colour = interlace = -1
    idat = bytearray()
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length
    if colour != 3 or depth != 8 or interlace != 0:
        raise SystemExit(
            f"{path}: expected 8-bit indexed non-interlaced PNG, got "
            f"colour type {colour}, depth {depth}, interlace {interlace}")

    raw = zlib.decompress(bytes(idat))
    stride = width  # one byte per pixel
    out = bytearray(width * height)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        filter_type = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if filter_type == 0:
            pass
        elif filter_type == 1:  # Sub
            for i in range(1, stride):
                line[i] = (line[i] + line[i - 1]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - 1] if i else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                left = line[i - 1] if i else 0
                up = prev[i]
                upleft = prev[i - 1] if i else 0
                estimate = left + up - upleft
                da, db, dc = (abs(estimate - left), abs(estimate - up),
                              abs(estimate - upleft))
                if da <= db and da <= dc:
                    pred = left
                elif db <= dc:
                    pred = up
                else:
                    pred = upleft
                line[i] = (line[i] + pred) & 0xFF
        else:
            raise SystemExit(f"{path}: unknown PNG filter {filter_type}")
        out[y * width:(y + 1) * width] = line
        prev = line
    return width, height, bytes(out)


# --------------------------------------------------------------------------
# The smoother's queries, replayed over a decoded grid
# --------------------------------------------------------------------------
class Grid:
    def __init__(self, path: str, constants: "Constants") -> None:
        self.path = path
        self.width, self.height, self.cells = decode_indexed_png(path)
        self.k = constants

    def pix(self, x: int, y: int) -> int:
        # smoother::query_x_y returns PIX_GRASS1 outside the grid.
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return self.k.pix_grass1
        return self.cells[x + y * self.width]

    def genre(self, x: int, y: int) -> int:
        value = self.pix(x, y)
        if value < 0 or value >= self.k.pix_max:
            return self.k.type_unknown
        return self.k.genre.get(value, self.k.type_unknown)

    def surrounds(self, x: int, y: int, genre: int) -> int:
        mask = 0
        if self.genre(x, y - 1) == genre:
            mask |= 1
        if self.genre(x + 1, y) == genre:
            mask |= 2
        if self.genre(x, y + 1) == genre:
            mask |= 4
        if self.genre(x - 1, y) == genre:
            mask |= 8
        return mask


class Constants:
    def __init__(self) -> None:
        pixdefs = parse_defines(PIXDEFS_H, "PIX_")
        types = parse_constexpr_ints(TERRAIN_H, "TYPE_")
        self.genre = parse_genre_table(SMOOTH_CPP, pixdefs, types)
        self.pix_max = pixdefs["PIX_MAX"]
        self.pix_grass1 = pixdefs["PIX_GRASS1"]
        self.type_wall = types["TYPE_WALL"]
        self.type_grass_dark = types["TYPE_GRASS_DARK"]
        self.type_unknown = types["TYPE_UNKNOWN"]
        # The arrow-slit tiles leave smooth()'s wall arm before the adjacency
        # switch is ever reached, so they are not gap candidates.
        self.arrow_slits = {
            pixdefs[name] for name in (
                "PIX_WALL_ARROW_GRASS", "PIX_WALL_ARROW_FLOOR", "PIX_WALL4",
                "PIX_WALL_ARROW_GRASS_DARK")
        }


def wall_gap_cells(grid: Grid) -> dict[int, list[tuple[int, int, int]]]:
    """Wall cells whose adjacency mask has no arm: {mask: [(x, y, pix)]}."""
    found: dict[int, list[tuple[int, int, int]]] = {m: [] for m in WALL_GAP_MASKS}
    for y in range(grid.height):
        for x in range(grid.width):
            if grid.genre(x, y) != grid.k.type_wall:
                continue
            here = grid.pix(x, y)
            if here in grid.k.arrow_slits:
                continue
            mask = grid.surrounds(x, y, grid.k.type_wall)
            if mask in found:
                found[mask].append((x, y, here))
    return found


def dark_grass_stray_cells(grid: Grid) -> list[tuple[int, int]]:
    """Cells carrying the mask-14 'do nothing' fingerprint (see module docs)."""
    dark = grid.k.type_grass_dark
    strays = []
    for y in range(grid.height):
        for x in range(grid.width):
            if grid.pix(x, y) != grid.k.pix_grass1:
                continue
            if (grid.genre(x - 1, y) == dark and grid.genre(x + 1, y) == dark
                    and grid.genre(x, y + 1) == dark
                    and grid.genre(x, y - 1) != dark):
                strays.append((x, y))
    return strays


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------
def terrain_grids(campaigns: list[str]) -> list[str]:
    """Every floor-terrain grid PNG; decor planes ('_dN') are not smoothed."""
    root = os.path.join(REPO_ROOT, "campaigns")
    paths = []
    for campaign in sorted(os.listdir(root)):
        if campaigns and campaign not in campaigns:
            continue
        pix_dir = os.path.join(root, campaign, "pix")
        if not os.path.isdir(pix_dir):
            continue
        for name in sorted(os.listdir(pix_dir)):
            if not name.startswith("scen") or not name.endswith(".png"):
                continue
            if re.search(r"_d\d+\.png$", name):
                continue
            paths.append(os.path.join(pix_dir, name))
    return paths


def level_label(path: str) -> str:
    campaign = os.path.basename(os.path.dirname(os.path.dirname(path)))
    return f"{campaign}/{os.path.basename(path)[:-4]}"


def report_level(path: str, constants: Constants) -> None:
    grid = Grid(path, constants)
    gaps = wall_gap_cells(grid)
    strays = dark_grass_stray_cells(grid)
    print(f"{level_label(path)}  ({grid.width}x{grid.height})")
    for mask in WALL_GAP_MASKS:
        cells = gaps[mask]
        if not cells:
            continue
        print(f"  wall mask {mask:<2} ({WALL_GAP_NAMES[mask]}): {len(cells)} cells")
        for x, y, pix in cells:
            print(f"    cell {x},{y} keeps pix {pix}")
    if strays:
        print(f"  dark-grass mask-14 strays: {len(strays)} cells")
        for x, y in strays:
            print(f"    cell {x},{y}")
    if not strays and not any(gaps[m] for m in WALL_GAP_MASKS):
        print("  no gap cells")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Census of smooth.cpp's unhandled autotiler masks.")
    parser.add_argument("--level", action="append", default=[],
                        help="report cell coordinates for this grid PNG")
    parser.add_argument("--campaign", action="append", default=[],
                        help="restrict the census to this campaign")
    parser.add_argument("--all-rows", action="store_true",
                        help="print levels with no gap cells too")
    args = parser.parse_args(argv)

    constants = Constants()

    if args.level:
        for path in args.level:
            report_level(path, constants)
        return 0

    paths = terrain_grids(args.campaign)
    header = (f"{'level':<28}{'m0':>5}{'m2':>5}{'m8':>5}{'m10':>5}"
              f"{'walls':>8}{'stray14':>9}")
    print(header)
    print("-" * len(header))
    totals = {mask: 0 for mask in WALL_GAP_MASKS}
    total_strays = 0
    shown = 0
    for path in paths:
        grid = Grid(path, constants)
        gaps = wall_gap_cells(grid)
        strays = len(dark_grass_stray_cells(grid))
        counts = {mask: len(gaps[mask]) for mask in WALL_GAP_MASKS}
        walls = sum(counts.values())
        for mask in WALL_GAP_MASKS:
            totals[mask] += counts[mask]
        total_strays += strays
        if not args.all_rows and walls == 0 and strays == 0:
            continue
        shown += 1
        print(f"{level_label(path):<28}"
              f"{counts[0]:>5}{counts[2]:>5}{counts[8]:>5}{counts[10]:>5}"
              f"{walls:>8}{strays:>9}")
    print("-" * len(header))
    print(f"{'TOTAL (' + str(len(paths)) + ' grids)':<28}"
          f"{totals[0]:>5}{totals[2]:>5}{totals[8]:>5}{totals[10]:>5}"
          f"{sum(totals.values()):>8}{total_strays:>9}")
    print(f"{shown} grid(s) carry at least one gap cell")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
