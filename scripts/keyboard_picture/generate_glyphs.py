#!/usr/bin/env python3
"""Bake the keyboard picture's DejaVu labels into a glyph atlas.

The keyboard picture (KEYMAP and the CHANGELOG! page) is drawn at run time by
og::ui::keyboard_picture (src/interface/ui/keyboard_picture.cpp). Its labels
keep the look of the approved picture, DejaVu Sans, without the game loading
a TrueType font: this script rasterizes every glyph the picture can print, at
the few sizes it prints them, into two committed files:

    pix/keyboard/glyphs.png                 the glyphs' coverage, 8-bit grey
    src/interface/ui/keyboard_glyphs.inc    where each glyph sits in the atlas,
                                            its bearings and advance, the
                                            kerning pairs, and the SHA-256 of
                                            the atlas, of this script and of
                                            the three fonts

The renderer composes caps and text from those two files; nothing else
draws the keyboard.

    nix develop -c python3 scripts/keyboard_picture/generate_glyphs.py
                                     # write both files
    python3 scripts/keyboard_picture/generate_glyphs.py --check
                                     # what the build runs; writes nothing
    nix develop -c python3 scripts/keyboard_picture/generate_glyphs.py \\
        --sheet build/media/controls # a contact sheet of every style

--check (scripts/check_keyboard_glyphs.sh, a build-time check) always
verifies that the committed atlas and this script hash to what the table
records, so an atlas edited by hand, or a script edited without
regenerating, fails the build on every lane. Where Pillow and the DejaVu
fonts are present (the nix dev shell has both) it also renders the atlas and
the table again and fails unless both equal the committed ones; elsewhere it
says that part was skipped.

Writing needs Pillow and the DejaVu fonts: run it inside `nix develop`
(flake.nix carries both). The fonts are found through XDG_DATA_DIRS, which
the dev shell sets, or OG_KEYBOARD_FONT_DIR.

The sizes are the approved picture's (scripts/keyboard_map/make_keyboard_map.py
before it retired): cap names in DejaVu Sans Bold 26, a free cap's name in
DejaVu Sans 21, the player tag in DejaVu Sans Bold 16, and the action in
DejaVu Sans Condensed Bold at the largest size up to 21 that fits the cap.
The action sizes come in half-pixel steps from 21 down to 8, so the
renderer's fit rule lands within half a pixel of the old picture's sizes.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
ATLAS_REL = "pix/keyboard/glyphs.png"
TABLE_REL = "src/interface/ui/keyboard_glyphs.inc"
SCRIPT_REL = "scripts/keyboard_picture/generate_glyphs.py"

FONT_BOLD = "DejaVuSans-Bold.ttf"
FONT_BOOK = "DejaVuSans.ttf"
FONT_CONDENSED_BOLD = "DejaVuSansCondensed-Bold.ttf"
FONTS = (FONT_BOLD, FONT_BOOK, FONT_CONDENSED_BOLD)

PRINTABLE = "".join(chr(c) for c in range(0x20, 0x7F))
ARROWS = "←↑→↓"  # the arrow caps' names
# What an action label or a player tag can print: the binding panels'
# capitals, digits, the diagonals' hyphen and the split cap's slash.
ACTION_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/+"
TAG_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+"

ACTION_MAX = 21.0
ACTION_MIN = 8.0

# (style name, font file, size, characters). The C++ side finds the styles
# by these names (keyboard_picture.cpp); "action" repeats once per size,
# largest first.
STYLES = [
    ("cap_name", FONT_BOLD, 26.0, PRINTABLE + ARROWS),
    ("cap_name_free", FONT_BOOK, 21.0, PRINTABLE + ARROWS),
    ("tag", FONT_BOLD, 16.0, TAG_CHARS),
]
_size = ACTION_MAX
while _size >= ACTION_MIN:
    STYLES.append(("action", FONT_CONDENSED_BOLD, _size, ACTION_CHARS))
    _size -= 0.5

ATLAS_W = 1024
PAD = 1  # clear pixels between glyphs, so a scaled draw never bleeds


class CheckFailed(Exception):
    pass


# --- fonts ------------------------------------------------------------------

def font_search_roots() -> list[str]:
    roots: list[str] = []
    override = os.environ.get("OG_KEYBOARD_FONT_DIR")
    if override:
        roots.append(override)
    for entry in os.environ.get("XDG_DATA_DIRS", "").split(os.pathsep):
        if entry:
            roots.append(os.path.join(entry, "fonts"))
    seen: set[str] = set()
    unique = []
    for root in roots:
        if root not in seen and os.path.isdir(root):
            seen.add(root)
            unique.append(root)
    return unique


def find_fonts() -> dict[str, str] | None:
    found: dict[str, str] = {}
    for name in FONTS:
        for root in font_search_roots():
            hits = sorted(glob.glob(os.path.join(root, "**", name),
                                    recursive=True))
            if hits:
                found[name] = hits[0]
                break
        else:
            return None
    return found


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


# --- rasterizing ------------------------------------------------------------

def render(fonts: dict[str, str]):
    """(atlas rows as bytes, atlas height, style records, glyph records,
    kerning records)."""
    from PIL import Image, ImageDraw, ImageFont

    # Raqm (HarfBuzz) lays text out as the approved picture's renderer did:
    # unhinted advances and the fonts' GPOS kerning (basic layout sees no
    # kerning in DejaVu at all).
    layout = ImageFont.Layout.RAQM
    loaded: dict[tuple[str, float], object] = {}
    bitmaps = []  # (style index, codepoint, Image or None, left, top, advance)
    kerns = []    # (style index, left cp, right cp, adjust)
    styles = []
    for index, (name, font_file, size, chars) in enumerate(STYLES):
        key = (font_file, size)
        if key not in loaded:
            loaded[key] = ImageFont.truetype(fonts[font_file], size,
                                             layout_engine=layout)
        font = loaded[key]
        first = len(bitmaps)
        for ch in chars:
            advance = font.getlength(ch)
            left, top, right, bottom = font.getbbox(ch, anchor="ls")
            image = None
            if ch != " " and right > left and bottom > top:
                image = Image.new("L", (right - left, bottom - top), 0)
                ImageDraw.Draw(image).text((-left, -top), ch, font=font,
                                           fill=255, anchor="ls")
                if image.getbbox() is None:
                    image = None
            bitmaps.append((index, ord(ch), image, left, top, advance))
        kern_first = len(kerns)
        widths = {ch: font.getlength(ch) for ch in chars}
        for a in chars:
            for b in chars:
                adjust = font.getlength(a + b) - widths[a] - widths[b]
                if abs(adjust) >= 0.01:
                    kerns.append((index, ord(a), ord(b), adjust))
        ascent, descent = font.getmetrics()
        styles.append((name, font_file, size, first, len(bitmaps) - first,
                       kern_first, len(kerns) - kern_first, ascent, descent))

    # Shelf packing in table order: deterministic, and the table reads the
    # rectangles back from where they landed.
    placed = []
    x = y = shelf = 0
    for (style, cp, image, left, top, advance) in bitmaps:
        if image is None:
            placed.append((style, cp, 0, 0, 0, 0, left, top, advance))
            continue
        w, h = image.size
        if x + w + PAD > ATLAS_W:
            x = 0
            y += shelf + PAD
            shelf = 0
        placed.append((style, cp, x, y, w, h, left, top, advance))
        x += w + PAD
        shelf = max(shelf, h)
    height = y + shelf
    atlas = bytearray(ATLAS_W * height)
    for (style, cp, image, *_), (_, _, ax, ay, w, h, *_) in zip(bitmaps,
                                                               placed):
        if image is None:
            continue
        pixels = image.tobytes()
        for row in range(h):
            start = (ay + row) * ATLAS_W + ax
            atlas[start:start + w] = pixels[row * w:(row + 1) * w]
    rows = [bytes(atlas[r * ATLAS_W:(r + 1) * ATLAS_W]) for r in range(height)]
    return rows, height, styles, placed, kerns


# --- the files ----------------------------------------------------------------

def png_bytes(rows: list[bytes], width: int) -> bytes:
    """An 8-bit greyscale PNG, filter 0 on every row (decode_png reads it)."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    raw = b"".join(b"\x00" + row for row in rows)
    ihdr = struct.pack(">IIBBBBB", width, len(rows), 8, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def decode_png(data: bytes) -> tuple[int, list[bytes]]:
    """(width, rows) of a PNG png_bytes wrote; CheckFailed for anything else."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise CheckFailed(f"{ATLAS_REL} is not a PNG")
    pos = 8
    width = height = 0
    idat = b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if depth != 8 or colour != 0:
                raise CheckFailed(f"{ATLAS_REL} is not 8-bit greyscale")
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    rows = []
    for r in range(height):
        line = raw[r * (width + 1):(r + 1) * (width + 1)]
        if line[:1] != b"\x00":
            raise CheckFailed(f"{ATLAS_REL} row {r} is filtered; "
                              "the generator writes filter 0 only")
        rows.append(line[1:])
    return width, rows


def table_text(styles, placed, kerns, height: int, atlas_sha: str,
               script_sha: str, font_shas: dict[str, str]) -> str:
    out = [
        "// Generated by scripts/keyboard_picture/generate_glyphs.py: do not",
        "// edit. Run it inside `nix develop` to change the keyboard picture's",
        "// labels; scripts/check_keyboard_glyphs.sh fails the build when this",
        "// table, the atlas and the script disagree.",
        "// clang-format off",
        f'inline constexpr char kAtlasPath[] = "{ATLAS_REL}";',
        f"inline constexpr int kAtlasW = {ATLAS_W};",
        f"inline constexpr int kAtlasH = {height};",
        f'inline constexpr char kAtlasSha256[] = "{atlas_sha}";',
        f'inline constexpr char kGeneratorSha256[] = "{script_sha}";',
    ]
    for name in FONTS:
        out.append(f"// {name} sha256 {font_shas[name]}")
    out.append("inline constexpr GlyphStyle kStyles[] = {")
    for (name, font_file, size, first, count, kfirst, kcount, ascent,
         descent) in styles:
        out.append(f'    {{"{name}", {size:.1f}f, {first}, {count}, {kfirst}, '
                   f"{kcount}, {ascent}, {descent}}},  // {font_file}")
    out.append("};")
    out.append("inline constexpr Glyph kGlyphs[] = {")
    for (style, cp, x, y, w, h, left, top, advance) in placed:
        out.append(f"    {{{cp}, {x}, {y}, {w}, {h}, {left}, {top}, "
                   f"{advance:.4f}f}},")
    out.append("};")
    out.append("inline constexpr Kern kKerns[] = {")
    if not kerns:
        out.append("    {0, 0, 0.0f},  // none")
    for (style, a, b, adjust) in kerns:
        out.append(f"    {{{a}, {b}, {adjust:.4f}f}},")
    out.append("};")
    out.append("// clang-format on")
    return "\n".join(out) + "\n"


def recorded(table: str, name: str) -> str | None:
    marker = f"inline constexpr char {name}[] = \""
    start = table.find(marker)
    if start < 0:
        return None
    start += len(marker)
    end = table.find('"', start)
    return table[start:end] if end > start else None


def build(fonts: dict[str, str]) -> tuple[bytes, str]:
    rows, height, styles, placed, kerns = render(fonts)
    png = png_bytes(rows, ATLAS_W)
    font_shas = {name: sha256_file(path) for name, path in fonts.items()}
    table = table_text(styles, placed, kerns, height,
                       hashlib.sha256(png).hexdigest(),
                       sha256_file(os.path.join(ROOT, SCRIPT_REL)), font_shas)
    return png, table


def read(rel: str, mode: str = "rb"):
    path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        raise CheckFailed(f"{rel} is missing")
    if mode == "rb":
        with open(path, "rb") as handle:
            return handle.read()
    with open(path, encoding="utf-8", newline="") as handle:
        return handle.read()


def check() -> int:
    try:
        table = read(TABLE_REL, "r")
        atlas = read(ATLAS_REL)
        want_atlas = recorded(table, "kAtlasSha256")
        want_script = recorded(table, "kGeneratorSha256")
        if want_atlas != hashlib.sha256(atlas).hexdigest():
            raise CheckFailed(f"{ATLAS_REL} is not the atlas {TABLE_REL} was "
                              "generated with (edited by hand?)")
        if want_script != sha256_file(os.path.join(ROOT, SCRIPT_REL)):
            raise CheckFailed(f"{SCRIPT_REL} changed since {TABLE_REL} was "
                              "generated")
        fonts = find_fonts()
        try:
            import PIL  # noqa: F401
        except ImportError:
            fonts = None
        if fonts is None:
            print("check_keyboard_glyphs: atlas, table and generator agree; "
                  "the full redraw compare needs Pillow and the DejaVu fonts "
                  "(nix develop) and was skipped here")
            return 0
        for name, path in fonts.items():
            line = f"// {name} sha256 {sha256_file(path)}"
            if line not in table:
                raise CheckFailed(f"{name} at {path} is not the font the "
                                  "atlas was drawn from")
        png, fresh_table = build(fonts)
        width, rows = decode_png(atlas)
        fresh_width, fresh_rows = decode_png(png)
        if (width, rows) != (fresh_width, fresh_rows):
            raise CheckFailed(f"{ATLAS_REL} differs from what the generator "
                              "draws now")
        # The table records the atlas's bytes, which follow zlib's version;
        # compare everything else.
        def strip(text: str) -> str:
            return "\n".join(line for line in text.split("\n")
                             if "kAtlasSha256" not in line)
        if strip(table) != strip(fresh_table):
            raise CheckFailed(f"{TABLE_REL} differs from what the generator "
                              "writes now")
    except CheckFailed as error:
        print(f"check_keyboard_glyphs: {error}", file=sys.stderr)
        return 1
    print("check_keyboard_glyphs: the atlas and its table are the "
          "generator's output")
    return 0


def sheet(directory: str) -> int:
    from PIL import Image
    width, rows = decode_png(read(ATLAS_REL))
    image = Image.frombytes("L", (width, len(rows)), b"".join(rows))
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "keyboard-glyphs.png")
    image.resize((width * 2, len(rows) * 2), Image.NEAREST).save(path)
    print(f"wrote {path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--check", action="store_true",
                       help="compare with the committed files; write nothing")
    group.add_argument("--sheet", metavar="DIR",
                       help="write the atlas at 2x into DIR")
    args = parser.parse_args()
    if args.check:
        return check()
    if args.sheet:
        return sheet(args.sheet)
    fonts = find_fonts()
    if fonts is None:
        print("generate_glyphs: the DejaVu fonts were not found; run inside "
              "`nix develop` or set OG_KEYBOARD_FONT_DIR", file=sys.stderr)
        return 2
    png, table = build(fonts)
    os.makedirs(os.path.dirname(os.path.join(ROOT, ATLAS_REL)), exist_ok=True)
    with open(os.path.join(ROOT, ATLAS_REL), "wb") as handle:
        handle.write(png)
    with open(os.path.join(ROOT, TABLE_REL), "w", encoding="utf-8",
              newline="\n") as handle:
        handle.write(table)
    print(f"wrote {ATLAS_REL} and {TABLE_REL}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
