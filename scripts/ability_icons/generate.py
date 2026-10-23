#!/usr/bin/env python3
"""Compile the ability icon grids into the PNGs the game loads.

Every ability has a 16x16 picture in the engine palette, authored as a text
grid (scripts/ability_icons/grids/<name>.txt, the format is in README.md) and
compiled to pix/icons/<name>.png. A class pack's own icons are authored the
same way under pack_grids/<pack>/ and compiled into the pack's sprites/
directory (PACK_GRIDS below). Stdlib only; the PNG encoder, the palette
reader and the sheet cutter are the ones scripts/generate_special_art.py
already ships, imported, not copied.

    python3 scripts/ability_icons/generate.py                # write every PNG
    python3 scripts/ability_icons/generate.py --check        # compare, write nothing
    python3 scripts/ability_icons/generate.py --sheet DIR [--names a,b]
                                                             # the acceptance sheets
    python3 scripts/ability_icons/generate.py --dump pix/mage.png 13 [--at X,Y]
                                                             # a sprite frame as a grid
    python3 scripts/ability_icons/generate.py --self-test    # the gates' own teeth

--check runs at build time (scripts/check_ability_icons.sh): it fails when a
grid breaks a gate below, when a committed PNG is not its grid re-encoded, or
when a PNG under pix/icons/ has no grid. Every gate prints in the structure
line the contact sheet shows under each icon.

The gates (a grid that breaks one is refused):
  - more than 153 opaque pixels (60 % of 256): a filled tile is a button;
  - more than 24 of the 60 border pixels opaque: the edge is the slot's;
  - opaque on all four edges, unless the grid says `# edge: ok`;
  - more than 16 non-zero palette indices;
  - any index in 1-15 (the UI greys), 168-191 (FFA ramps made at run time),
    208-231 (rotated every frame by the palette cycle) or 248-255 (the team
    band);
  - a locked copy (locked_lut) whose interior pixels (non-zero, not 16) are
    fewer than half at least 30 luma away from the slot fill 18 (luma 72).
"""

import json
import os
import random
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "scripts"))
import generate_special_art as art  # noqa: E402

PIX = os.path.join(REPO, "pix")
GRIDS = os.path.join(HERE, "grids")
ICONS = os.path.join(PIX, "icons")
# (grid directory, output directory) for a pack's own icons.
PACK_GRIDS = (
    (os.path.join(HERE, "pack_grids", "emberwisp"),
     os.path.join(REPO, "docs", "modding", "examples", "emberwisp", "sprites")),
)

SIZE = 16
OPAQUE_MAX = 153        # 60 % of 256
BORDER_MAX = 24         # of the 60 border pixels
COLOURS_MAX = 16
CELL_FILL = 18          # the slot ground (72, 72, 72)
CELL_FRAME = 16         # the slot frame, black
BOX_GREY = 13           # the OVERLAY box and the button face grey
LOCKED_INK = 8          # BUTTON_FACING - 5, the ABILITIES locked label
LABEL_INK = 72          # DARK_BLUE, the ABILITIES label
LOCKED_FAR = 30         # luma a locked interior pixel must stand off the fill
FORBIDDEN = (set(range(1, 16)) | set(range(168, 192)) | set(range(208, 232))
             | set(range(248, 256)))
FLOORS = ("16grass1.png", "16stone1.png", "16dstone1.png", "16cob1.png")
BAD_LEGEND = "#.= "

PALETTE = art.parse_gpl(os.path.join(PIX, "openglad.gpl"))


class GridError(Exception):
    pass


# --- the grid format ---------------------------------------------------------

class Grid:
    def __init__(self, name, path, pixels, edge_ok, title):
        self.name = name
        self.path = path
        self.pixels = pixels      # 256 bytes, row-major
        self.edge_ok = edge_ok
        self.title = title        # the first comment line


def parse_grid_text(text, where):
    """A grid file's text -> (pixels, edge_ok, title). Raises GridError."""
    legend = {".": 0}
    rows = []
    edge_ok = False
    title = ""
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if not line.strip():
            continue
        if line.startswith("#"):
            body = line[1:].strip()
            if body.lower() == "edge: ok":
                edge_ok = True
            elif not title:
                title = body
            # '# = 40' would make every row starting with '#' a comment.
            if body.startswith("=") and line[1:2] in (" ", "="):
                raise GridError(f"{where}:{number}: legend letter '#' is not "
                                "allowed (a row starting with it would read "
                                "as a comment)")
            continue
        if "=" in line:
            letter, _, value = line.partition("=")
            if len(letter) != 2 or letter[1] != " " or letter[0] in BAD_LEGEND:
                shown = letter.rstrip() or " "
                raise GridError(f"{where}:{number}: bad legend letter "
                                f"'{shown}' (one character, never "
                                "'#', '.', '=' or a space)")
            letter = letter[0]
            try:
                index = int(value.strip())
            except ValueError:
                raise GridError(f"{where}:{number}: legend '{letter}' needs a "
                                f"palette index, got '{value.strip()}'")
            if not 0 <= index <= 255:
                raise GridError(f"{where}:{number}: legend '{letter}' = "
                                f"{index} is not a palette index (0..255)")
            if letter in legend:
                raise GridError(f"{where}:{number}: legend letter '{letter}' "
                                "is defined twice")
            legend[letter] = index
            continue
        rows.append((number, line))
    if len(rows) != SIZE:
        raise GridError(f"{where}: {len(rows)} rows, a grid has exactly "
                        f"{SIZE}")
    pixels = bytearray()
    for number, row in rows:
        if len(row) != SIZE:
            raise GridError(f"{where}:{number}: a row is {SIZE} characters, "
                            f"this one is {len(row)}")
        for ch in row:
            if ch not in legend:
                raise GridError(f"{where}:{number}: letter '{ch}' is not in "
                                "the legend")
            pixels.append(legend[ch])
    return bytes(pixels), edge_ok, title


def read_grid(path):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    rel = os.path.relpath(path, REPO)
    pixels, edge_ok, title = parse_grid_text(text, rel)
    name = os.path.splitext(os.path.basename(path))[0]
    return Grid(name, path, pixels, edge_ok, title)


def grid_sets():
    """[(grid dir, out dir)] for the core icons and every pack."""
    return ((GRIDS, ICONS),) + PACK_GRIDS


def all_grids():
    """[(Grid, png path)] in a stable order; GridError on a bad file."""
    out = []
    for gdir, odir in grid_sets():
        if not os.path.isdir(gdir):
            continue
        for fn in sorted(os.listdir(gdir)):
            if fn.endswith(".txt"):
                grid = read_grid(os.path.join(gdir, fn))
                out.append((grid, os.path.join(odir, grid.name + ".png")))
    return out


# --- the gates -----------------------------------------------------------------

def luma(index):
    r, g, b = PALETTE[index]
    return 0.299 * r + 0.587 * g + 0.114 * b


def locked_lut():
    """The locked look (og::ui::locked_icon_lut): 0 -> 0, 16 -> 16, every
    other index -> 21 + round(luma / 230 * 10), never 17..20."""
    lut = [0] * 256
    for i in range(1, 256):
        lut[i] = 16 if i == 16 else 21 + max(0, min(10, int(
            luma(i) / 230 * 10 + 0.5)))
    return lut


LOCKED = locked_lut()


def locked(pixels):
    return bytes(LOCKED[c] for c in pixels)


def structure(pixels, edge_ok, lut=None):
    """(structure line, [problems]) for one 16x16 picture. `lut` is the
    locked look to judge (the shipped one unless a self-test passes another:
    with locked_lut every interior grey is already 37 luma off the fill, so
    this gate guards against a future LUT, not against a grid)."""
    lut = LOCKED if lut is None else lut
    opaque = sum(1 for c in pixels if c)
    border = sum(1 for y in range(SIZE) for x in range(SIZE)
                 if (x in (0, SIZE - 1) or y in (0, SIZE - 1))
                 and pixels[y * SIZE + x])
    edges = (any(pixels[x] for x in range(SIZE)),
             any(pixels[(SIZE - 1) * SIZE + x] for x in range(SIZE)),
             any(pixels[y * SIZE] for y in range(SIZE)),
             any(pixels[y * SIZE + SIZE - 1] for y in range(SIZE)))
    colours = sorted(set(pixels) - {0})
    bad = [c for c in colours if c in FORBIDDEN]
    interior = [lut[c] for c in pixels if lut[c] not in (0, 16)]
    far = sum(1 for c in interior
              if abs(luma(c) - luma(CELL_FILL)) >= LOCKED_FAR)
    problems = []
    if opaque > OPAQUE_MAX:
        problems.append(f"opaque {opaque} > {OPAQUE_MAX}")
    if border > BORDER_MAX:
        problems.append(f"border {border} > {BORDER_MAX}")
    if all(edges) and not edge_ok:
        problems.append("touches all four edges (add '# edge: ok' only "
                        "when the picture must)")
    if len(colours) > COLOURS_MAX:
        problems.append(f"{len(colours)} colours > {COLOURS_MAX}")
    if bad:
        problems.append("forbidden indices " + ",".join(map(str, bad)))
    if interior and far * 2 < len(interior):
        problems.append(f"locked contrast {far}/{len(interior)} < 50 %")
    edge_text = "".join("TBLR"[i] for i, e in enumerate(edges) if e) or "-"
    if edge_ok:
        edge_text += " (edge: ok)"
    line = (f"opaque {opaque}/256  border {border}/60  edges {edge_text}  "
            f"colours {len(colours)}  locked-far {far}/{len(interior)}")
    return line, problems


def encode(pixels):
    return art.encode_indexed_png(SIZE, SIZE, pixels, PALETTE)


# --- write / check -----------------------------------------------------------------

def check_palette():
    engine = art.parse_engine_palette(
        os.path.join(REPO, "src", "resources", "our_palette.cpp"))
    for i, (a, b) in enumerate(zip(PALETTE, engine)):
        if 168 <= i <= 191:
            continue  # synthesised at run time; the loader skips them too
        for c in range(3):
            if abs(a[c] - b[c]) > 1:
                raise SystemExit(f"pix/openglad.gpl drifted from "
                                 f"our_pal_lookup at entry {i} channel {c}")


def run_write():
    check_palette()
    status = 0
    for grid, png in all_grids():
        line, problems = structure(grid.pixels, grid.edge_ok)
        if problems:
            print(f"REFUSED {os.path.relpath(grid.path, REPO)}: "
                  + "; ".join(problems))
            status = 1
            continue
        os.makedirs(os.path.dirname(png), exist_ok=True)
        data = encode(grid.pixels)
        try:
            with open(png, "rb") as f:
                same = f.read() == data
        except FileNotFoundError:
            same = False
        if not same:
            with open(png, "wb") as f:
                f.write(data)
            print(f"wrote {os.path.relpath(png, REPO)}")
        print(f"  {grid.name:20s} {line}")
    return status


def run_check():
    check_palette()
    problems = []
    try:
        grids = all_grids()
    except GridError as e:
        print(f"check_ability_icons: {e}")
        return 1
    expected = {}
    for grid, png in grids:
        rel = os.path.relpath(grid.path, REPO)
        _, gate = structure(grid.pixels, grid.edge_ok)
        for p in gate:
            problems.append(f"{rel}: {p}")
        expected[os.path.normpath(png)] = grid
        try:
            with open(png, "rb") as f:
                have = f.read()
        except FileNotFoundError:
            have = None
        if have != encode(grid.pixels):
            problems.append(f"{os.path.relpath(png, REPO)} is not {rel} "
                            "re-encoded (rerun generate.py)")
    # Every committed core icon has its grid (a pack's sprites/ directory
    # holds other art, so only pix/icons/ is walked).
    if os.path.isdir(ICONS):
        for fn in sorted(os.listdir(ICONS)):
            path = os.path.normpath(os.path.join(ICONS, fn))
            if fn.endswith(".png") and path not in expected:
                problems.append(f"pix/icons/{fn} has no grid in "
                                "scripts/ability_icons/grids/")
    if problems:
        print("check_ability_icons: the ability icons and their grids "
              "disagree:")
        for p in problems:
            print("  " + p)
        return 1
    print(f"check_ability_icons: {len(grids)} grids, every gate passes and "
          "every PNG is its grid")
    return 0


# --- --dump --------------------------------------------------------------------

LETTERS = "kabcdefghijlmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"


def team_fixed(c):
    """The team band at its team-0 look (the red ramp), the cycled fire band
    on its still copy; everything else as it is."""
    if 248 <= c <= 255:
        return 40 + (255 - c)
    if 224 <= c <= 231:
        return c + 8
    return c


def frame_size(sheet_path, width, height):
    """(frame w, frame h, frames) from the Aseprite sidecar, or one frame."""
    sidecar = os.path.splitext(sheet_path)[0] + ".json"
    if not os.path.exists(sidecar):
        return width, height, 1
    with open(sidecar, "r", encoding="utf-8") as f:
        doc = json.load(f)
    frames = list(doc["frames"].values())
    first = frames[0]["frame"]
    return first["w"], first["h"], len(frames)


def dump_text(sheet_arg, frame, at=None):
    """A sheet frame as grid text, team band fixed, ready to edit."""
    path = sheet_arg
    if not os.path.exists(path):
        path = os.path.join(PIX, sheet_arg)
    sheet = art.read_indexed_png(path)
    fw, fh, frames = frame_size(path, sheet[0], sheet[1])
    if not 0 <= frame < frames:
        raise SystemExit(f"{sheet_arg}: frame {frame} out of 0..{frames - 1}")
    cells = art.frame_cells(sheet, fw, fh, frame)
    if at is None:
        at = ((SIZE - fw) // 2, (SIZE - fh) // 2)
    cv = art.Canvas(SIZE, SIZE)
    cv.stamp([[team_fixed(c) for c in row] for row in cells], at[0], at[1])
    legend = {}
    rows = []
    for y in range(SIZE):
        row = ""
        for x in range(SIZE):
            c = cv.get(x, y)
            if c == 0:
                row += "."
                continue
            if c not in legend:
                legend[c] = LETTERS[len(legend)]
            row += legend[c]
        rows.append(row)
    rel = os.path.relpath(path, REPO)
    out = [f"# NAME: cut from {rel} frame {frame} ({fw}x{fh}, placed at "
           f"{at[0]},{at[1]}); say what the icon shows here",
           "# legend: one character = one palette index (pix/openglad.gpl)"]
    bad = sorted(c for c in legend if c in FORBIDDEN)
    if bad:
        out.append("# NOTE: forbidden indices " + ",".join(map(str, bad))
                   + " must be recoloured before --check passes")
    for c, letter in legend.items():
        out.append(f"{letter} = {c}")
    out.append("")
    out.extend(rows)
    return "\n".join(out) + "\n"


# --- the sheets ----------------------------------------------------------------

FONT = art.read_indexed_png(os.path.join(PIX, "text.png"))  # 5x6, index = ASCII


class RGB:
    def __init__(self, w, h, fill=(165, 165, 165)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(fill) * (w * h))

    def put(self, x, y, rgb):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.px[i:i + 3] = bytes(rgb)

    def fill(self, x0, y0, x1, y1, index):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, PALETTE[index])

    def rect(self, x0, y0, x1, y1, index):
        for x in range(x0, x1 + 1):
            self.put(x, y0, PALETTE[index])
            self.put(x, y1, PALETTE[index])
        for y in range(y0, y1 + 1):
            self.put(x0, y, PALETTE[index])
            self.put(x1, y, PALETTE[index])

    def tile(self, x0, y0, x1, y1, tile):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, PALETTE[tile[(y % 16) * 16 + (x % 16)]])

    def icon(self, x0, y0, pixels):
        for y in range(SIZE):
            for x in range(SIZE):
                c = pixels[y * SIZE + x]
                if c:
                    self.put(x0 + x, y0 + y, PALETTE[c])

    def text(self, x, y, s, ink, k=1):
        """The game's 5x6 font; the glyph's team band takes the ink."""
        for i, ch in enumerate(s.upper()):
            code = ord(ch)
            if code >= FONT[1] // 6:
                continue
            for yy, row in enumerate(art.frame_cells(FONT, 5, 6, code)):
                for xx, c in enumerate(row):
                    if not c:
                        continue
                    rgb = PALETTE[ink if c > 247 else c]
                    for dy in range(k):
                        for dx in range(k):
                            self.put(x + (i * 6 + xx) * k + dx,
                                     y + yy * k + dy, rgb)

    def scale(self, k):
        out = RGB(self.w * k, self.h * k)
        rows = []
        for y in range(self.h):
            line = self.px[y * self.w * 3:(y + 1) * self.w * 3]
            wide = b"".join(bytes(line[i:i + 3]) * k
                            for i in range(0, len(line), 3))
            rows.append(wide * k)
        out.px = bytearray(b"".join(rows))
        return out

    def png(self):
        raw = bytearray()
        for y in range(self.h):
            raw.append(0)
            raw.extend(self.px[y * self.w * 3:(y + 1) * self.w * 3])
        ihdr = struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + art.png_chunk(b"IHDR", ihdr)
                + art.png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
                + art.png_chunk(b"IEND", b""))

    def save(self, path):
        with open(path, "wb") as f:
            f.write(self.png())


def floor_tile(name):
    return art.read_indexed_png(os.path.join(PIX, name))[2]


def draw_slot(img, x0, y0, pixels, frame=CELL_FRAME):
    """One 18x18 slot: fill 18, frame, the icon at +1,+1 (None = empty)."""
    img.fill(x0, y0, x0 + 17, y0 + 17, CELL_FILL)
    img.rect(x0, y0, x0 + 17, y0 + 17, frame)
    if pixels is not None:
        img.icon(x0 + 1, y0 + 1, pixels)


def draw_face(img, x, y, w, pixels, label, is_locked):
    """An ABILITIES face, 20 px tall: the button bevel, the same slot at
    +2,+1, the label at +22 (baseline +7)."""
    h = 20
    img.fill(x, y, x + w - 1, y + h - 1, BOX_GREY)
    img.rect(x, y, x + w - 1, y + h - 1, 15)
    for xx in range(x, x + w):
        img.put(xx, y + h - 1, PALETTE[11])
    for yy in range(y, y + h):
        img.put(x + w - 1, yy, PALETTE[12])
    draw_slot(img, x + 2, y + 1, pixels)
    img.text(x + 22, y + 7, label, LOCKED_INK if is_locked else LABEL_INK)


def label_of(name):
    return name.replace("_", " ").strip().upper()


def sheet_contact(grids):
    """Every icon at 4x on mid grey with its name and structure line."""
    per_col = 18
    cols = (len(grids) + per_col - 1) // per_col
    col_w = 4 + 64 + 8 + 50 * 12
    row_h = 72
    img = RGB(cols * col_w + 4, min(per_col, len(grids)) * row_h + 8)
    for i, grid in enumerate(grids):
        cx = 4 + (i // per_col) * col_w
        cy = 4 + (i % per_col) * row_h
        big = RGB(SIZE, SIZE)
        big.icon(0, 0, grid.pixels)
        big = big.scale(4)
        for y in range(64):
            for x in range(64):
                j = (y * 64 + x) * 3
                rgb = tuple(big.px[j:j + 3])
                if rgb != (165, 165, 165):
                    img.put(cx + x, cy + y, rgb)
        line, problems = structure(grid.pixels, grid.edge_ok)
        tx = cx + 64 + 8
        img.text(tx, cy + 2, grid.name, 16, 2)
        parts = line.split("  ")
        img.text(tx, cy + 18, "  ".join(parts[:2]), 16, 2)
        img.text(tx, cy + 32, "  ".join(parts[2:]), 16, 2)
        img.text(tx, cy + 46, "OK" if not problems else
                 "FAIL: " + "; ".join(problems), 16 if not problems else 40,
                 2)
    return img


def sheet_cells(grids, grass):
    """Per icon: slot ready / locked on grass (the no-box look), on the box
    grey (the box look), then the ABILITIES face ready and locked."""
    per_col = 18
    cols = (len(grids) + per_col - 1) // per_col
    row_h = 26
    col_w = 64 + 4 * 22 + 8 + 2 * 102 + 8
    img = RGB(cols * col_w, min(per_col, len(grids)) * row_h + 8)
    for i, grid in enumerate(grids):
        x = (i // per_col) * col_w
        y = 4 + (i % per_col) * row_h
        ready, grey = grid.pixels, locked(grid.pixels)
        img.text(x + 2, y + 7, grid.name[:10], 16)
        img.tile(x + 64, y - 2, x + 64 + 43, y + 21, grass)
        draw_slot(img, x + 66, y, ready)
        draw_slot(img, x + 66 + 20, y, grey)
        img.fill(x + 64 + 46, y - 2, x + 64 + 46 + 43, y + 21, BOX_GREY)
        draw_slot(img, x + 66 + 46, y, ready)
        draw_slot(img, x + 66 + 66, y, grey)
        fx = x + 64 + 96
        img.fill(fx, y - 2, fx + 2 * 102 + 3, y + 21, BOX_GREY)
        label = label_of(grid.name)[:10]
        draw_face(img, fx + 2, y - 1, 100, ready, "1 " + label, False)
        draw_face(img, fx + 104, y - 1, 100, grey, "S3 " + label, True)
    return img


def sheet_floors(grids, tiles):
    """The bare icon on each floor, ready and locked: a second opinion."""
    per_band = 12
    bands = (len(grids) + per_band - 1) // per_band
    band_h = 48 * len(tiles) * 2 + 8
    img = RGB(48 * min(per_band, len(grids)) + 8, bands * band_h)
    for i, grid in enumerate(grids):
        ox = 4 + (i % per_band) * 48
        band = (i // per_band) * band_h
        for fi, tile in enumerate(tiles):
            for variant in (0, 1):
                oy = band + 4 + (fi * 2 + variant) * 48
                img.tile(ox, oy, ox + 47, oy + 47, tile)
                pixels = grid.pixels if variant == 0 else locked(grid.pixels)
                img.icon(ox + 16, oy + 16, pixels)
    return img


def sheet_blind(grids, grass, seed=7):
    """The slots on grass, no names, shuffled; numbered by position."""
    order = list(grids)
    random.Random(seed).shuffle(order)
    per_row = 10
    rows = (len(order) + per_row - 1) // per_row
    img = RGB(4 + per_row * 26, 4 + rows * 54)
    img.tile(0, 0, img.w - 1, img.h - 1, grass)
    for i, grid in enumerate(order):
        x = 4 + (i % per_row) * 26
        y = 4 + (i // per_row) * 54
        draw_slot(img, x, y, grid.pixels)
        draw_slot(img, x, y + 20, locked(grid.pixels))
        img.text(x + 1, y + 41, str(i + 1), 31)
    key = "".join(f"{i + 1} {g.name}\n" for i, g in enumerate(order))
    return img, key


def run_sheet(out_dir, names=None):
    grids = [g for g, _ in all_grids()]
    if names:
        wanted = set(names)
        missing = wanted - {g.name for g in grids}
        if missing:
            raise SystemExit("no grid for: " + ", ".join(sorted(missing)))
        grids = [g for g in grids if g.name in wanted]
    os.makedirs(out_dir, exist_ok=True)
    tiles = [floor_tile(f) for f in FLOORS]
    grass = tiles[0]
    written = []

    def save(img, name):
        path = os.path.join(out_dir, name)
        img.save(path)
        written.append(path)

    save(sheet_contact(grids), "icons-contact.png")
    cells = sheet_cells(grids, grass)
    save(cells, "icons-cells-1x.png")
    save(cells.scale(2), "icons-cells.png")
    save(cells.scale(4), "icons-cells-4x.png")
    floors = sheet_floors(grids, tiles)
    save(floors, "icons-floors-1x.png")
    save(floors.scale(2), "icons-floors.png")
    blind, key = sheet_blind(grids, grass)
    save(blind.scale(2), "icons-blind.png")
    key_path = os.path.join(out_dir, "icons-blind-key.txt")
    with open(key_path, "w", encoding="utf-8") as f:
        f.write(key)
    written.append(key_path)
    for path in written:
        print(f"wrote {path}")
    return 0


# --- the gates' own teeth -------------------------------------------------------

def self_test():
    """Each gate and each parse refusal, on a grid built to break it."""
    failures = []

    def grid_text(rows, legend, extra=()):
        head = ["# SELF-TEST"] + list(extra)
        head += [f"{k} = {v}" for k, v in legend.items()]
        return "\n".join(head + [""] + rows) + "\n"

    def expect_parse_error(text, needle, what):
        try:
            parse_grid_text(text, "self-test")
        except GridError as e:
            if needle not in str(e):
                failures.append(f"{what}: refused, but said '{e}'")
            return
        failures.append(f"{what}: accepted")

    def expect_gate(rows, legend, needle, what, extra=(), lut=None):
        pixels, edge_ok, _ = parse_grid_text(grid_text(rows, legend, extra),
                                             "self-test")
        _, problems = structure(pixels, edge_ok, lut)
        hit = [p for p in problems if needle in p]
        if needle and not hit:
            failures.append(f"{what}: not refused ({problems})")
        if not needle and problems:
            failures.append(f"{what}: refused ({problems})")

    blank = ["." * 16] * 16
    dot = ["." * 16] * 7 + ["......aa........"] * 2 + ["." * 16] * 7
    expect_gate(dot, {"a": 31}, "", "a small white dot passes")
    # opaque: a 13x13 block is 169 pixels
    block = (["." * 16] * 1 + ["." + "a" * 13 + ".."] * 13 + ["." * 16] * 2)
    expect_gate(block, {"a": 31}, "opaque", "a filled 13x13 block")
    # border: the whole top and left edges plus a little (31 border pixels)
    frame = ["a" * 16] + ["a" + "." * 15] * 15
    expect_gate(frame, {"a": 31}, "border", "an L along two edges")
    # four edges: a thin cross touching all four, then waived
    cross = ["." * 7 + "a" + "." * 8] * 7 + ["a" * 16] + \
            ["." * 7 + "a" + "." * 8] * 8
    expect_gate(cross, {"a": 31}, "four edges", "a cross touching all edges")
    expect_gate(cross, {"a": 31}, "", "the same cross with '# edge: ok'",
                ("# edge: ok",))
    # colours: seventeen indices
    many = list(blank)
    letters = "abcdefghijklmnopq"
    many[8] = letters[:16]
    many[9] = "q" + "." * 15
    legend = {ch: 21 + (i % 11) if i < 11 else 48 + i
              for i, ch in enumerate(letters)}
    expect_gate(many, legend, "colours", "seventeen colours")
    # forbidden bands, one per band
    for index in (5, 170, 212, 226, 250):
        expect_gate(dot, {"a": index}, "forbidden", f"index {index}")
    # locked contrast: the first prototype's LUT spread every colour over the
    # whole 16..31 ramp and put the teleport ring (74) on 18, the fill itself
    v1 = [0] * 256
    for i in range(1, 256):
        v1[i] = 16 + max(0, min(15, int(luma(i) / 230 * 15 + 0.5)))
    expect_gate(dot, {"a": 74}, "locked contrast",
                "the v1 LUT melts the teleport blue into the fill", lut=v1)
    expect_gate(dot, {"a": 74}, "", "the shipped LUT keeps it off the fill")
    # parse refusals
    expect_parse_error(grid_text(blank[:15], {}), "15 rows", "15 rows")
    expect_parse_error(grid_text(blank[:15] + ["." * 15], {}),
                       "this one is 15", "a short row")
    expect_parse_error(grid_text(["z" + "." * 15] + blank[1:], {}),
                       "letter 'z'", "an unknown letter")
    for bad in (".", "="):
        expect_parse_error(f"{bad} = 31\n" + "\n".join(blank) + "\n",
                           "bad legend letter", f"legend letter '{bad}'")
    expect_parse_error(" = 31\n" + "\n".join(blank) + "\n",
                       "bad legend letter", "legend letter ' '")
    expect_parse_error("# = 31\n" + "\n".join(blank) + "\n",
                       "legend letter '#'", "legend letter '#'")
    expect_parse_error(grid_text(blank, {"a": 300}), "0..255",
                       "an index past 255")
    expect_parse_error("a = 1\na = 2\n" + "\n".join(blank) + "\n",
                       "defined twice", "a letter defined twice")
    # the LUT the design pins (og::ui::locked_icon_lut agrees, see
    # tests/unit/test_ability_icons.cpp)
    for index, want in ((0, 0), (16, 16), (74, 22), (40, 24), (239, 30)):
        if LOCKED[index] != want:
            failures.append(f"locked_lut[{index}] = {LOCKED[index]}, "
                            f"want {want}")
    if any(v not in (0, 16) and not 21 <= v <= 31 for v in LOCKED):
        failures.append("locked_lut leaves 0, 16 and 21..31")
    # --dump fixes the team band to the red ramp and the fire band to its
    # still copy
    if (team_fixed(255), team_fixed(248), team_fixed(226), team_fixed(74)) \
            != (40, 47, 234, 74):
        failures.append("team_fixed mapping")
    if failures:
        print("ability icon generator self-test FAILED:")
        for f in failures:
            print("  " + f)
        return 1
    print("ability icon generator self-test: every gate and refusal bites")
    return 0


# --- main ----------------------------------------------------------------------

def main(argv):
    args = argv[1:]
    try:
        if not args:
            return run_write()
        if args == ["--check"]:
            return run_check()
        if args == ["--self-test"]:
            return self_test()
        if args[0] == "--sheet" and len(args) in (2, 4):
            names = None
            if len(args) == 4:
                if args[2] != "--names":
                    raise SystemExit(__doc__)
                names = [n for n in args[3].split(",") if n]
            return run_sheet(args[1], names)
        if args[0] == "--dump" and len(args) in (3, 5):
            at = None
            if len(args) == 5:
                if args[3] != "--at":
                    raise SystemExit(__doc__)
                at = tuple(int(v) for v in args[4].split(","))
            sys.stdout.write(dump_text(args[1], int(args[2]), at))
            return 0
    except GridError as e:
        print(f"generate.py: {e}")
        return 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
