#!/usr/bin/env python3
"""Draw the "what Lua controls" ownership overlays over two captured stills.

The Base Camp is a shared screen: a campaign's Lua composes the widgets
inside the panel, and the engine owns everything around them. A screenshot
alone cannot show that seam, so this script paints it on — a blue fill for
every rectangle a campaign script composes, an orange outline for the engine
chrome no script can move, and a numbered legend under the picture.

Inputs (produced by scripts/media/capture_campaign_scripting.sh):
    <media-dir>/zone_camp_westlands.png     640x400
    <media-dir>/zone_submenu_stores.png     640x400

Outputs:
    <media-dir>/lua_ownership_basecamp.png  640 wide
    <media-dir>/lua_ownership_submenu.png   640 wide

Both outputs stay exactly 640px wide on purpose: GitHub's content column is
about 850px and its markdown CSS never upscales, so a 640px image renders
1:1 and the game keeps the crisp 2x nearest-neighbour pixels the capture
produced. A wider side-legend layout gets downscaled and softens every glyph
in the screenshot.

Usage:
    scripts/media/make_lua_ownership_overlays.py [media-dir]

Needs ImageMagick and a DejaVu TrueType file. Both are in the dev shell:
    nix develop --command scripts/media/make_lua_ownership_overlays.py
or, without the repo's shell:
    nix shell nixpkgs#imagemagick nixpkgs#dejavu_fonts --command \
        python3 scripts/media/make_lua_ownership_overlays.py
"""

from __future__ import annotations

import argparse
import glob
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import List, NoReturn, Optional, Sequence, Tuple

# The capture script rescales each 320x200 PPM to 640x400 with a nearest
# neighbour filter (capture_campaign_scripting.sh, the ZONE_SHOTS loop), so
# every rectangle below is authored on the game grid and doubled here. Keep
# the source rects on the game grid: that is the grid the cited code uses.
SCALE = 2
STILL_W = 320 * SCALE
STILL_H = 200 * SCALE

# Blue = script, orange = engine. Blue/orange is the colour-blind-safe pair,
# and the engine fill is deliberately weak (0.10): at 0.16 the command strip
# and header read as brown, which makes live buttons look disabled.
LUA_FILL = "rgba(56,166,255,0.20)"
LUA_LINE = "rgba(130,206,255,0.95)"
LUA_INK = "#BFE2FF"
LUA_BADGE = "#0A2540F0"

CPP_FILL = "rgba(255,146,48,0.10)"
CPP_LINE = "rgba(255,186,110,0.95)"
CPP_INK = "#FFDCB4"
CPP_BADGE = "#3A1C05F0"

PAGE_BG = "#0D1117"
TITLE_INK = "#E6EDF3"
KEY_INK = "#8B98A5"
DESC_INK = "#909DAA"

# Legend block geometry, in pixels below the 400px-tall still.
LEGEND_TITLE_Y = 408
LEGEND_KEY_X = 232
LEGEND_ROW_0 = 436
LEGEND_ROW_PITCH = 26
LEGEND_COL_PITCH = 318
LEGEND_PAD = 12


@dataclass(frozen=True)
class Region:
    """One annotated rectangle.

    `rect` is on the 320x200 game grid and `code` cites the source that draws
    it, so a reader can check the claim; `badge` is a hand-placed anchor in
    the 640x400 still, chosen to sit in blank pixels rather than on one of the
    game's own labels.
    """

    n: int
    name: str
    desc: str
    owner: str  # "lua" | "cpp" | "mixed" (engine slot, script-supplied text)
    rect: Tuple[int, int, int, int]
    badge: Tuple[int, int]
    code: str
    dashed: bool = False
    fill: bool = True
    width: int = 2
    badge_r: int = 10

    def rect640(self) -> Tuple[int, int, int, int]:
        x, y, w, h = self.rect
        return (x * SCALE, y * SCALE, w * SCALE, h * SCALE)


# --- zone_camp_westlands.png ----------------------------------------------
# Band geometry is derived, not guessed: westlands' fire.lua composes
# [readout, text, roster, actions]; CampaignZoneSession::adopt hoists the
# readout into the header band and hands the roster the 3 rows left over, so
# the bands land at unit 0 (text), 1 (roster) and 5 (actions) and each unit
# is 14px at y = 45 + 14*unit (menu_screen_specs.cpp:2284-2285).
CAMP: List[Region] = [
    Region(1, "HEADER LINE A", "COMPANY + GOLD - engine",
           "cpp", (8, 2, 304, 8), (300, 12),
           "menu_screen_specs.cpp:4400-4410"),
    Region(2, "STATUS LINE", "engine slot, script toast text",
           "mixed", (8, 16, 208, 8), (416, 40),
           "menu_screen_specs.cpp:4418-4470; picker_common.h:645"),
    # The badge sits on HIRE's left edge, not on its face: the label ink runs
    # x=452..495 in the still and a centred badge clipped the final E.
    Region(3, "HIRE", "engine button; script can only hide it",
           "cpp", (220, 14, 34, 12), (444, 40),
           "menu_screen_specs.cpp:2369-2372, 2664-2668", badge_r=7),
    Region(4, "ROSTER PAGER", "engine < p/N > (hidden this frame)",
           "cpp", (258, 15, 54, 10), (610, 40),
           "menu_screen_specs.cpp:2350-2358", dashed=True, fill=False),
    Region(5, "PANEL FRAME", "engine bevel; the inside is script's",
           "cpp", (8, 28, 304, 133), (28, 310),
           "menu_screen_specs.cpp:4349-4360", fill=False, width=3),
    Region(6, "READOUT", "<=3 label:value cells, 100px pitch",
           "lua", (10, 30, 300, 14), (600, 74),
           "menu_screen_specs.cpp:4525-4553; campaign_picker_session.cpp:418-424"),
    Region(7, "TEXT WIDGET", "narrative lines, 8px pitch, 49 chars",
           "lua", (10, 45, 300, 14), (600, 104),
           "menu_screen_specs.cpp:4497-4521; campaign_picker_session.cpp:353-358"),
    Region(8, "ROSTER WIDGET", "capabilities / locks / oath column",
           "lua", (10, 59, 300, 56), (600, 212),
           "campaign_picker_session.cpp:462-476; menu_screen_specs.cpp:3769-3806"),
    Region(9, "ACTIONS WIDGET", "script rows: levels, buys, doors",
           "lua", (10, 115, 300, 42), (600, 296),
           "menu_screen_specs.cpp:3860-3890; campaign_picker_session.cpp:485-497"),
    Region(10, "SEAT RAIL", "per-level player seats - engine",
           "cpp", (8, 164, 304, 10), (248, 338),
           "menu_screen_specs.cpp:2446, 2712-2758"),
    # Parked in the blank right end of the BACK door: its label ink runs
    # x=38..81 in the still and the door's face ends at x=103, so a badge
    # centred at 93 clears the word by two pixels. Centring it on the door
    # itself, or tucking it into the left margin, put the circle on the B.
    Region(11, "COMMAND STRIP", "BACK/DIFFICULTY/SCENARIO/NETWORK/GO",
           "cpp", (8, 178, 304, 18), (93, 374),
           "menu_screen_specs.cpp:2390-2411", badge_r=9),
]

# --- zone_submenu_stores.png ----------------------------------------------
# A zone submenu has no command strip: the top is the same two engine header
# lines as the camp, and the only footer is BACK plus two pagers. The row
# band top is derived from the page's own narrative line count,
# band.top = 33 + lines*8 + 4, so two lines put it at y=53.
SUBMENU: List[Region] = [
    Region(1, "HEADER LINE A", "COMPANY + GOLD - engine",
           "cpp", (8, 2, 304, 8), (300, 12),
           "menu_screen_specs.cpp:1568-1577"),
    Region(2, "TITLE LINE", "engine 'CAMP: ' + script page title",
           "mixed", (8, 16, 250, 8), (500, 40),
           "menu_screen_specs.cpp:1586-1600"),
    Region(3, "PANEL FRAME", "same engine panel as the camp",
           "cpp", (8, 28, 304, 133), (28, 310),
           "menu_screen_specs.cpp:1536-1541", fill=False, width=3),
    Region(4, "PAGE LINES", "script narrative from y=33, 8px pitch",
           "lua", (10, 30, 300, 22), (600, 82),
           "menu_screen_specs.cpp:1604-1611; picker_sdl_defs.h:404"),
    Region(5, "PAGE ROWS", "script rows: top=53, 12px pitch",
           "lua", (12, 53, 296, 60), (600, 166),
           "menu_screen_specs.cpp:1388-1398, 1470-1478"),
    Region(6, "ROW CAPACITY", "8 rows fit under 2 lines (53..149)",
           "lua", (12, 53, 296, 96), (600, 282),
           "menu_screen_specs.cpp:1394-1397; picker_sdl_defs.h:408",
           dashed=True, fill=False),
    # Same nudge as the camp's badge 11: this BACK's ink runs x=42..85 and
    # its face ends at x=107, so the badge sits in the blank right end.
    Region(7, "BACK", "engine footer; pops one script page",
           "cpp", (10, 169, 44, 20), (96, 357),
           "menu_screen_specs.cpp:1357-1361", badge_r=9),
    Region(8, "PREV / NEXT", "engine pagers (hidden this frame)",
           "cpp", (220, 169, 90, 20), (606, 358),
           "menu_screen_specs.cpp:1364-1375", dashed=True, fill=False),
]


@dataclass
class Sheet:
    src: str
    out: str
    title: str
    regions: Sequence[Region]


SHEETS = [
    Sheet("zone_camp_westlands.png", "lua_ownership_basecamp.png",
          "BASE CAMP - WESTLANDS", CAMP),
    Sheet("zone_submenu_stores.png", "lua_ownership_submenu.png",
          "ZONE SUBMENU - STORES", SUBMENU),
]


def die(message: str, hint: str = "") -> NoReturn:
    print(f"error: {message}", file=sys.stderr)
    if hint:
        print(hint, file=sys.stderr)
    raise SystemExit(1)


def find_magick() -> str:
    """ImageMagick 7's `magick`, or a clear sentence about how to get it."""
    exe = shutil.which("magick")
    if exe:
        return exe
    die("ImageMagick is not on PATH (looked for `magick`)",
        "Enter the repo dev shell, which ships it:\n"
        "    nix develop --command scripts/media/"
        "make_lua_ownership_overlays.py\n"
        "or borrow it for one command:\n"
        "    nix shell nixpkgs#imagemagick nixpkgs#dejavu_fonts --command \\\n"
        "        python3 scripts/media/make_lua_ownership_overlays.py")


def font_search_roots() -> List[str]:
    """Directories that might hold fonts, most specific first.

    The dev shell exports the DejaVu package through XDG_DATA_DIRS; a bare
    `nix shell nixpkgs#dejavu_fonts` exports nothing but does put the
    package's (empty) bin directory on PATH, so its sibling share/ is
    reachable from there. System font directories cover a plain distro box.
    """
    roots: List[str] = []
    override = os.environ.get("OG_OVERLAY_FONT_DIR")
    if override:
        roots.append(override)
    for entry in os.environ.get("XDG_DATA_DIRS", "").split(os.pathsep):
        if entry:
            roots.append(os.path.join(entry, "fonts"))
    for entry in os.environ.get("PATH", "").split(os.pathsep):
        if entry.endswith("/bin"):
            roots.append(os.path.join(os.path.dirname(entry), "share", "fonts"))
    roots += [
        os.path.expanduser("~/.nix-profile/share/fonts"),
        os.path.expanduser("~/.local/share/fonts"),
        os.path.expanduser("~/.fonts"),
        "/usr/local/share/fonts",
        "/usr/share/fonts",
    ]
    seen = set()
    unique = []
    for root in roots:
        if root not in seen and os.path.isdir(root):
            seen.add(root)
            unique.append(root)
    return unique


def find_font(*names: str) -> Optional[str]:
    """First readable TrueType file matching any of `names`, or None."""
    explicit = os.environ.get("OG_OVERLAY_FONT")
    if explicit and os.path.isfile(explicit):
        return explicit
    for root in font_search_roots():
        for name in names:
            hits = sorted(glob.glob(os.path.join(root, "**", name),
                                    recursive=True))
            if hits:
                return hits[0]
    return None


def load_fonts() -> Tuple[str, str]:
    """(bold, regular). DejaVu Sans Condensed keeps the legend narrow."""
    bold = find_font("DejaVuSansCondensed-Bold.ttf", "DejaVuSans-Bold.ttf")
    regular = find_font("DejaVuSansCondensed.ttf", "DejaVuSans.ttf")
    if not bold or not regular:
        die("no DejaVu TrueType font found",
            "The overlay draws its badges and legend with a real font file.\n"
            "Enter the repo dev shell (it ships dejavu_fonts):\n"
            "    nix develop --command scripts/media/"
            "make_lua_ownership_overlays.py\n"
            "or point the script at a font you have:\n"
            "    OG_OVERLAY_FONT=/path/to/Whatever-Bold.ttf "
            "OG_OVERLAY_FONT_DIR=/path/to/fonts ...")
    return bold, regular


def rect_mvg(x: int, y: int, w: int, h: int) -> str:
    return f"rectangle {x},{y} {x + w - 1},{y + h - 1}"


def region_mvg(region: Region) -> List[str]:
    fill = LUA_FILL if region.owner != "cpp" else CPP_FILL
    stroke = LUA_LINE if region.owner == "lua" else CPP_LINE
    out = ["push graphic-context",
           f"fill {fill if region.fill else 'none'}",
           f"stroke {stroke}",
           f"stroke-width {region.width}"]
    if region.dashed:
        out.append("stroke-dasharray 8,6")
    out += [rect_mvg(*region.rect640()), "pop graphic-context"]
    return out


def badge_mvg(cx: int, cy: int, r: int, owner: str, n: int, bold: str) -> List[str]:
    """Circle plus a centred digit.

    MVG's `text` takes a BASELINE, which is exact. ImageMagick's -annotate
    positions a bounding box whose height is not the point size, so centring
    digits that way drifts three or four pixels and visibly clips them.
    """
    stroke = LUA_LINE if owner == "lua" else CPP_LINE
    disc = LUA_BADGE if owner == "lua" else CPP_BADGE
    ink = CPP_INK if owner == "cpp" else LUA_INK
    # Two digits need a slightly smaller face to keep clear of the disc edge.
    size = max(9, r + 3) - (1 if n >= 10 else 0)
    return ["push graphic-context",
            f"fill {disc}", f"stroke {stroke}", "stroke-width 2",
            f"circle {cx},{cy} {cx + r},{cy}",
            "pop graphic-context",
            "push graphic-context",
            f"font '{bold}'", f"font-size {size}", "stroke none",
            f"fill {ink}", "text-anchor middle",
            f"text {cx},{cy + max(3, round(size * 0.36))} '{n}'",
            "pop graphic-context"]


def still_size(magick: str, path: str) -> str:
    proc = subprocess.run([magick, "identify", "-format", "%wx%h", path],
                          check=True, capture_output=True, text=True)
    return proc.stdout.strip()


def render(magick: str, bold: str, regular: str, sheet: Sheet,
           media_dir: str) -> str:
    src = os.path.join(media_dir, sheet.src)
    out = os.path.join(media_dir, sheet.out)
    if not os.path.isfile(src):
        die(f"missing still {src}",
            "Capture the stills first:\n"
            "    scripts/media/capture_campaign_scripting.sh")
    size = still_size(magick, src)
    if size != f"{STILL_W}x{STILL_H}":
        die(f"{src} is {size}, expected {STILL_W}x{STILL_H}",
            "The overlay rectangles assume the capture script's 2x stills.")

    rows_per_col = (len(sheet.regions) + 1) // 2
    legend_h = (LEGEND_ROW_0 - STILL_H) + rows_per_col * LEGEND_ROW_PITCH \
        + LEGEND_PAD
    total_h = STILL_H + legend_h

    over: List[str] = []
    for region in sheet.regions:
        over += region_mvg(region)
    for region in sheet.regions:
        cx, cy = region.badge
        over += badge_mvg(cx, cy, region.badge_r, region.owner, region.n, bold)

    legend: List[str] = []
    for index, region in enumerate(sheet.regions):
        col, row = divmod(index, rows_per_col)
        legend += badge_mvg(20 + col * LEGEND_COL_PITCH,
                            LEGEND_ROW_0 + row * LEGEND_ROW_PITCH,
                            9, region.owner, region.n, bold)

    cmd = [magick, src, "-draw", "\n".join(over),
           "-background", PAGE_BG, "-gravity", "North",
           "-extent", f"{STILL_W}x{total_h}", "-gravity", "NorthWest",
           "-font", bold, "-pointsize", "12", "-fill", TITLE_INK,
           "-annotate", f"+14+{LEGEND_TITLE_Y}", sheet.title,
           "-font", regular, "-pointsize", "11", "-fill", KEY_INK,
           "-annotate", f"+{LEGEND_KEY_X}+{LEGEND_TITLE_Y - 6}",
           "BLUE = COMPOSED BY THE CAMPAIGN'S LUA",
           "-annotate", f"+{LEGEND_KEY_X}+{LEGEND_TITLE_Y + 7}",
           "ORANGE = ENGINE-OWNED    BLUE IN ORANGE = ENGINE SLOT, SCRIPT TEXT",
           "-draw", "\n".join(legend)]
    for index, region in enumerate(sheet.regions):
        col, row = divmod(index, rows_per_col)
        x = 34 + col * LEGEND_COL_PITCH
        y = LEGEND_ROW_0 - 4 + row * LEGEND_ROW_PITCH
        cmd += ["-font", bold, "-pointsize", "11", "-fill", TITLE_INK,
                "-annotate", f"+{x}+{y}", region.name,
                "-font", regular, "-pointsize", "10", "-fill", DESC_INK,
                "-annotate", f"+{x}+{y + 11}", region.desc]
    cmd.append(out)
    subprocess.run(cmd, check=True)
    return out


def print_manifest(sheet: Sheet) -> None:
    print(f"  {sheet.src} -> {sheet.out}")
    for region in sheet.regions:
        x, y, w, h = region.rect
        print(f"    {region.n:>2}. {region.owner:<5} "
              f"{x},{y} {w}x{h} (game grid)  {region.name}  [{region.code}]")


def main(argv: Sequence[str]) -> int:
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    parser = argparse.ArgumentParser(
        description="Draw the Lua/engine ownership overlays over the "
                    "campaign-scripting stills.")
    parser.add_argument(
        "media_dir", nargs="?",
        default=os.path.join(repo_root, "build", "media",
                             "campaign-scripting"),
        help="directory holding the captured stills (default: "
             "build/media/campaign-scripting)")
    parser.add_argument("--manifest", action="store_true",
                        help="print the region table with its code citations "
                             "and exit without drawing")
    args = parser.parse_args(list(argv))

    if args.manifest:
        for sheet in SHEETS:
            print_manifest(sheet)
        return 0

    magick = find_magick()
    bold, regular = load_fonts()
    for sheet in SHEETS:
        out = render(magick, bold, regular, sheet, args.media_dir)
        print(f"wrote {out} ({still_size(magick, out)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
