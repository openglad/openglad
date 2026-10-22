#!/usr/bin/env python3
"""Draw the CHANGELOG! keyboard pictures from keyboard_map.txt.

The CHANGELOG! page (src/interface/ui/changelog_screen.cpp) shows the new
default keyboard: pix/changelog/keyboard_belt.png on desktop builds and
pix/changelog/keyboard_belt_web.png on the web build (where player 1 attacks
with Z, because Ctrl+W closes the browser tab).

The keys come from scripts/keyboard_map/keyboard_map.txt and from nowhere
else. That file is written from the game's own default key tables by
tests/unit/test_keyboard_map_asset.cpp, which fails when the tables and the
file disagree. keyboard_map.stamp holds the SHA-256 of the text file and of
both pictures, and scripts/check_keyboard_map.sh fails the build when any of
them changed without the stamp. So a key change cannot reach the picture
without someone redrawing it:

    # 1. after changing a default key, rewrite the text file from the tables
    OG_KEYBOARD_MAP_WRITE=1 ./build/ci-test/og_unit_core \\
        --gtest_filter='KeyboardMapAsset.*'
    # 2. redraw both pictures and the stamp (needs the nix dev shell: magick
    #    with librsvg, and the DejaVu fonts)
    nix develop -c python3 scripts/keyboard_map/make_keyboard_map.py --write

Other mode:
    make_keyboard_map.py --preview DIR   # write the SVGs and PNGs into DIR only

There is deliberately no way to rewrite the stamp alone: only --write, which
draws both pictures FROM the text file, writes it, so a hand-edited picture
can never be stamped as the real one.

The picture is the approved "Proposal 1" keyboard, cut down to what the page
needs: no title, legend or footer (the page says those things in its own
text), no navigation cluster, and labels about twice the size, because the
game draws it into a 312 x 110 band of its 320 x 200 menu screen.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MAP_TXT = "scripts/keyboard_map/keyboard_map.txt"
STAMP = "scripts/keyboard_map/keyboard_map.stamp"
PICTURES = {
    "desktop": "pix/changelog/keyboard_belt.png",
    "web": "pix/changelog/keyboard_belt_web.png",
}

# The picture's size. Its aspect is the page's picture band (312 x 110 menu
# pixels), so the game scales it without bars.
WIDTH = 1560
HEIGHT = 550

# One key unit, the gap between caps, the outer margin.
U = 83
GAP = 5
MARGIN = 14

PLAYER_COLOURS = {1: "#e5484d", 2: "#3e8ef7", 3: "#30a46c", 4: "#f5b400",
                  0: "#c9a7ff"}
PLAYER_TAGS = {1: "P1", 2: "P2", 3: "P3", 4: "P4", 0: "ALL"}

# keyboard_map.txt key tokens -> the cap ids below.
NAMED_KEYS = {
    "SPACE": "Space", "RETURN": "Enter", "TAB": "Tab", "BACKSPACE": "Bksp",
    "ESCAPE": "Esc", "LSHIFT": "LShift", "RSHIFT": "RShift",
    "LCTRL": "LCtrl", "RCTRL": "RCtrl", "LALT": "LAlt", "RALT": "RAlt",
    "UP": "Up", "DOWN": "Down", "LEFT": "Left", "RIGHT": "Right",
}
for _n in range(1, 13):
    NAMED_KEYS[f"F{_n}"] = f"F{_n}"

# What a cap prints as its name.
CAP_TEXT = {"LShift": "Shift", "RShift": "Shift", "LCtrl": "Ctrl",
            "RCtrl": "Ctrl", "LAlt": "Alt", "RAlt": "Alt", "LWin": "Win",
            "RWin": "Win", "Up": "↑", "Down": "↓",
            "Left": "←", "Right": "→"}

# (cap id, width in units); None is a blank of that width.
ROWS = [
    [("Esc", 1), (None, 1), ("F1", 1), ("F2", 1), ("F3", 1), ("F4", 1),
     (None, .5), ("F5", 1), ("F6", 1), ("F7", 1), ("F8", 1), (None, .5),
     ("F9", 1), ("F10", 1), ("F11", 1), ("F12", 1)],
    [("`", 1), ("1", 1), ("2", 1), ("3", 1), ("4", 1), ("5", 1), ("6", 1),
     ("7", 1), ("8", 1), ("9", 1), ("0", 1), ("-", 1), ("=", 1),
     ("Bksp", 2)],
    [("Tab", 1.5), ("Q", 1), ("W", 1), ("E", 1), ("R", 1), ("T", 1),
     ("Y", 1), ("U", 1), ("I", 1), ("O", 1), ("P", 1), ("[", 1), ("]", 1),
     ("\\", 1.5)],
    [("Caps", 1.75), ("A", 1), ("S", 1), ("D", 1), ("F", 1), ("G", 1),
     ("H", 1), ("J", 1), ("K", 1), ("L", 1), (";", 1), ("'", 1),
     ("Enter", 2.25)],
    [("LShift", 2.25), ("Z", 1), ("X", 1), ("C", 1), ("V", 1), ("B", 1),
     ("N", 1), ("M", 1), (",", 1), (".", 1), ("/", 1), ("RShift", 2.75)],
    [("LCtrl", 1.25), ("LWin", 1.25), ("LAlt", 1.25), ("Space", 6.25),
     ("RAlt", 1.25), ("RWin", 1.25), ("Menu", 1.25), ("RCtrl", 1.25)],
]
# The arrow cluster sits right of the main block, on its two bottom rows.
ARROWS = [[(None, 1), ("Up", 1), (None, 1)],
          [("Left", 1), ("Down", 1), ("Right", 1)]]
ARROW_GAP_UNITS = 0.3
F_ROW_GAP = 0.25  # extra space under the function-key row, in units

# The keys everyone shares, which the key tables do not hold. On the web
# build the browser keeps Escape (it leaves fullscreen) and Backspace does
# its job (include/openglad/interface/web_back_key.h).
SHARED_KEYS = {
    "desktop": {"Esc": "PAUSE"},
    "web": {"Bksp": "PAUSE"},
}


def key_id(token: str) -> str:
    """A keyboard_map.txt key token -> the cap id it lights up."""
    if token in NAMED_KEYS:
        return NAMED_KEYS[token]
    if len(token) == 1:
        return token.upper()
    raise SystemExit(f"keyboard_map.txt: unknown key '{token}'")


def read_map(path: str) -> dict[str, dict[str, tuple[int, str]]]:
    """variant -> {cap id: (player, action)}."""
    maps: dict[str, dict[str, tuple[int, str]]] = {"desktop": {}, "web": {}}
    with open(path, encoding="utf-8", newline="") as handle:
        text = handle.read()
    if "\r" in text:
        raise SystemExit("keyboard_map.txt: CR line ends (it is LF only)")
    for number, line in enumerate(text.split("\n"), start=1):
        if not line or line.startswith("#"):
            continue
        fields = line.split(" ")
        if len(fields) != 4:
            raise SystemExit(f"keyboard_map.txt:{number}: want 4 fields")
        variant, player, action, token = fields
        if variant not in maps or player[:1] != "P":
            raise SystemExit(f"keyboard_map.txt:{number}: bad line")
        cap = key_id(token)
        if cap in maps[variant]:
            raise SystemExit(f"keyboard_map.txt:{number}: {token} is bound "
                             f"twice in the {variant} map")
        maps[variant][cap] = (int(player[1:]), action.replace("_", " "))
    for variant, keys in maps.items():
        if not keys:
            raise SystemExit(f"keyboard_map.txt: no {variant} lines")
    return maps


def esc(text: str) -> str:
    return (text.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;"))


def action_size(action: str, cap_w: float) -> float:
    """The largest action label size (px) that fits the cap, at most 21."""
    # DejaVu Sans Condensed Bold capitals run about 0.62 em wide.
    fit = (cap_w - 10) / (0.62 * max(1, len(action)))
    return round(min(21.0, fit), 1)


def draw_key(out: list[str], x: float, y: float, units: float, cap: str,
             binding: tuple[int, str] | None) -> None:
    w = units * U - GAP
    h = U - GAP
    name = esc(CAP_TEXT.get(cap, cap))
    if binding is None:
        out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" '
                   f'height="{h:.1f}" rx="8" fill="#2b2d31" '
                   f'stroke="#45474d" stroke-width="1.5"/>')
        out.append(f'<text x="{x + 9:.1f}" y="{y + 27:.1f}" font-size="21" '
                   f'fill="#9a9da4">{name}</text>')
        return
    player, action = binding
    colour = PLAYER_COLOURS[player]
    out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" '
               f'height="{h:.1f}" rx="8" fill="{colour}" stroke="#ffffff" '
               f'stroke-opacity=".35" stroke-width="1.5"/>')
    out.append(f'<text x="{x + 8:.1f}" y="{y + 29:.1f}" font-size="26" '
               f'font-weight="700" fill="#111111">{name}</text>')
    # The player tag goes top right when it clears the cap's name (the
    # colour says the player either way). DejaVu Sans Bold runs about
    # 0.72 em a glyph.
    tag = PLAYER_TAGS[player]
    name_end = 8 + 0.72 * 26 * len(CAP_TEXT.get(cap, cap))
    tag_start = w - 7 - 0.72 * 16 * len(tag)
    if name_end + 6 <= tag_start:
        out.append(f'<text x="{x + w - 7:.1f}" y="{y + 24:.1f}" '
                   f'font-size="16" font-weight="800" fill="#111111" '
                   f'fill-opacity=".72" text-anchor="end">{tag}</text>')
    size = action_size(action, w)
    out.append(f'<text x="{x + w / 2:.1f}" y="{y + h - 11:.1f}" '
               f'font-family="DejaVu Sans Condensed" font-stretch="condensed" '
               f'font-size="{size}" font-weight="700" fill="#111111" '
               f'text-anchor="middle">{esc(action)}</text>')


def build_svg(keys: dict[str, tuple[int, str]], variant: str) -> str:
    bound = dict(keys)
    for cap, action in SHARED_KEYS[variant].items():
        if cap in bound:
            raise SystemExit(f"{cap} is a player key and the shared "
                             f"{action} key")
        bound[cap] = (0, action)
    known = {cap for row in ROWS + ARROWS for cap, _ in row if cap}
    missing = sorted(set(bound) - known)
    if missing:
        raise SystemExit(f"keys not on the picture: {missing}")

    main_units = 15.0
    total_units = main_units + ARROW_GAP_UNITS + 3
    x0 = (WIDTH - total_units * U) / 2 + GAP / 2
    height_units = len(ROWS) + F_ROW_GAP
    y0 = (HEIGHT - height_units * U) / 2 + GAP / 2

    out: list[str] = []
    for r, row in enumerate(ROWS):
        x = x0
        y = y0 + r * U + (F_ROW_GAP * U if r > 0 else 0)
        for cap, units in row:
            if cap:
                draw_key(out, x, y, units, cap, bound.get(cap))
            x += units * U
    arrow_x = x0 + (main_units + ARROW_GAP_UNITS) * U
    for r, row in enumerate(ARROWS):
        x = arrow_x
        y = y0 + (len(ROWS) - 2 + r) * U + F_ROW_GAP * U
        for cap, units in row:
            if cap:
                draw_key(out, x, y, units, cap, bound.get(cap))
            x += units * U

    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" '
            f'height="{HEIGHT}" font-family="DejaVu Sans, sans-serif">'
            f'<rect width="100%" height="100%" fill="#16171a"/>'
            f'<rect x="4" y="4" width="{WIDTH - 8}" height="{HEIGHT - 8}" '
            f'rx="18" fill="#1e1f23" stroke="#33353a" stroke-width="2"/>'
            + "".join(out) + "</svg>\n")


def render_png(svg_path: str, png_path: str) -> None:
    magick = shutil.which("magick")
    if magick is None:
        raise SystemExit("magick not found: run inside `nix develop`")
    # PNG32: truecolour with alpha, which the game's loader asks for.
    # -strip drops the time stamps, so a redraw of unchanged keys keeps its
    # bytes.
    subprocess.run([magick, "-background", "none", svg_path, "-strip",
                    "-define", "png:exclude-chunks=date,time",
                    f"PNG32:{png_path}"], check=True)


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def write_stamp() -> None:
    lines = [f"{sha256(os.path.join(ROOT, rel))}  {rel}\n"
             for rel in (MAP_TXT, PICTURES["desktop"], PICTURES["web"])]
    with open(os.path.join(ROOT, STAMP), "w", encoding="utf-8",
              newline="\n") as handle:
        handle.write("".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true",
                       help="redraw both pictures and the stamp")
    group.add_argument("--preview", metavar="DIR",
                       help="write SVGs and PNGs into DIR only")
    args = parser.parse_args()

    maps = read_map(os.path.join(ROOT, MAP_TXT))
    with tempfile.TemporaryDirectory() as scratch:
        for variant, rel in PICTURES.items():
            svg = build_svg(maps[variant], variant)
            name = os.path.splitext(os.path.basename(rel))[0]
            if args.preview:
                os.makedirs(args.preview, exist_ok=True)
                svg_path = os.path.join(args.preview, name + ".svg")
                png_path = os.path.join(args.preview, name + ".png")
            else:
                svg_path = os.path.join(scratch, name + ".svg")
                png_path = os.path.join(ROOT, rel)
                os.makedirs(os.path.dirname(png_path), exist_ok=True)
            with open(svg_path, "w", encoding="utf-8") as handle:
                handle.write(svg)
            render_png(svg_path, png_path)
            print(f"wrote {png_path}")
    if args.write:
        write_stamp()
        print(f"wrote {os.path.join(ROOT, STAMP)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
