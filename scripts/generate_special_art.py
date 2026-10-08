#!/usr/bin/env python3
"""Generate the New Specials sprites: pix/{mine,banner,bonewall,ember}.png
and the blank pix/kit_marker.png.

Deterministic, stdlib-only (struct + zlib) source of truth for the committed
PNGs and their Aseprite "Hash" sidecars. Rerunning it reproduces the same
bytes. Run from the repo root:

    python3 scripts/generate_special_art.py          # write the files
    python3 scripts/generate_special_art.py --check  # compare, write nothing

Every sprite is built from art the game already ships, so the new things look
like they belong next to the old ones:

  - mine.png (9x9, 2 frames): the body of the dark bomb (bomb1.png frame 10)
    with its fuse ember painted out, ringed by a lit grey rim, and a small
    light in the middle painted in the team band (frame 2 is dimmer, so the
    light blinks). The thief's team colours it.
  - banner.png (12x22, 4 frames): the skull from the skeleton's head
    (skeleton.png frame 0) on top of a wooden pole and cross yard in the wood
    browns, with a team-band cloth that ripples the way the capture flag does.
  - bonewall.png (16x16, 2 frames): three upright posts, each two of the
    thrown bone's upright frames (bone1.png frame 0) stacked, lashed with two
    rows of its sideways frame (bone1.png frame 2) running post to post.
    No ground shadow is painted in: like every obstacle the game ships, it
    leaves that to the engine. Frame 2 is the cracked wall: a post snapped,
    a bar broken and hanging, cracks.
  - ember.png (7x7, 2 frames): a charred spot in the darkest greys with the
    flame of the meteor bolt (meteor.png frame 0) standing on it.
    Frame 2 is the dying ember. The flame keeps the cycled fire band
    224-231, so the palette rotation animates it like a torch.

The kit marker (the invisible helper that times DIG IN, LEGION, IMMOLATE,
PHASE and METEOR RAIN) needs a picture of its own size and nothing in it:
kit_marker.png is one 40x60 frame of index 0, so the marker keeps the box it
is centred with and draws nothing. It has no sidecar (one frame).

Palette rules (pix/openglad.gpl, read_pixie_file in og_file.cpp):
  - index 0 is transparent;
  - 208-223 (water) and 224-231 (fire) are rotated every frame by do_cycle:
    only the ember's flame may use the fire band, nothing uses water;
  - 248-255 is the team band, remapped to the owner's team colour at draw
    time: only the mine's light and the banner's cloth use it.
These rules are pinned by tests/unit/test_special_art.cpp.
"""

import json
import math
import os
import re
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIX = os.path.join(REPO, "pix")

TRANSPARENT = 0

# Team band: walkputbuffer remaps p > 247 to teamcolor + (255 - p).
TEAM_BRIGHT = 255
TEAM_LIGHT = 254
TEAM_MID = 252
TEAM_DARK = 250
TEAM_DARKEST = 248

# Neutral grey ramp 16..31 (16 is black, 31 near white).
GREY_BLACK = 16
GREY_SHADOW = 17
GREY_DARK = 19
GREY_MID = 21
GREY_RIM = 23
GREY_BONE = 27

# Wood browns 136..143 (the skeleton's weapon handle is 139).
WOOD_LIGHT = 138
WOOD = 139
WOOD_DARK = 141
WOOD_DARKEST = 143

FIRE_BAND = range(224, 232)
WATER_BAND = range(208, 224)
TEAM_BAND = range(248, 256)


# --- PNG in/out --------------------------------------------------------------

def read_indexed_png(path):
    """Decode an 8-bit indexed PNG (any filter) to (width, height, pixels)."""
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    pos = 8
    idat = b""
    width = height = None
    while pos < len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        kind = blob[pos + 4:pos + 8]
        data = blob[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype = struct.unpack(">IIBB", data[:10])
            if depth != 8 or ctype != 3:
                raise SystemExit(f"{path}: not 8-bit indexed")
        elif kind == b"IDAT":
            idat += data
    raw = zlib.decompress(idat)
    out = bytearray()
    prev = bytearray(width)
    i = 0
    for _ in range(height):
        filt = raw[i]
        i += 1
        line = bytearray(raw[i:i + width])
        i += width
        for x in range(width):
            left = line[x - 1] if x else 0
            up = prev[x]
            corner = prev[x - 1] if x else 0
            if filt == 1:
                line[x] = (line[x] + left) & 255
            elif filt == 2:
                line[x] = (line[x] + up) & 255
            elif filt == 3:
                line[x] = (line[x] + ((left + up) >> 1)) & 255
            elif filt == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                if pa <= pb and pa <= pc:
                    pred = left
                elif pb <= pc:
                    pred = up
                else:
                    pred = corner
                line[x] = (line[x] + pred) & 255
        out += line
        prev = line
    return width, height, bytes(out)


def png_chunk(kind, payload):
    raw = kind + payload
    return (struct.pack(">I", len(payload)) + raw
            + struct.pack(">I", zlib.crc32(raw) & 0xFFFFFFFF))


def encode_indexed_png(width, height, pixels, palette):
    """An indexed 8-bit PNG with the 256-entry palette and tRNS[0] = 0."""
    if len(pixels) != width * height:
        raise SystemExit("pixel buffer size mismatch")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(c) for c in palette)
    trns = bytes([0] + [255] * 255)
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)  # filter: none
        scanlines.extend(pixels[y * width:(y + 1) * width])
    return (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", ihdr)
            + png_chunk(b"PLTE", plte)
            + png_chunk(b"tRNS", trns)
            + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), 9))
            + png_chunk(b"IEND", b""))


def parse_gpl(path):
    colors = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if (not line or line.startswith("#") or line.startswith("GIMP")
                    or line.startswith("Name:")
                    or line.startswith("Columns:")):
                continue
            m = re.match(r"^(\d+)\s+(\d+)\s+(\d+)", line)
            if m:
                colors.append(tuple(int(m.group(i)) for i in (1, 2, 3)))
    if len(colors) != 256:
        raise SystemExit(f"{path}: expected 256 colours, got {len(colors)}")
    return colors


def parse_engine_palette(path):
    """The 6-bit our_pal_lookup table the loader checks every PNG against."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"\bdata(?:\[\])?\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"{path}: palette data array not found")
    values = [int(v) for v in re.findall(r"\d+", m.group(1))]
    if len(values) != 768:
        raise SystemExit(f"{path}: expected 768 palette bytes")
    return [tuple((values[i * 3 + c] * 255) // 63 for c in range(3))
            for i in range(256)]


def aseprite_sidecar(base, frame_w, frame_h, frames):
    """Aseprite 'Hash' export, the shape of the committed pix/*.json."""
    doc = {"frames": {}, "meta": {}}
    for i in range(frames):
        doc["frames"][f"{base} {i}.aseprite"] = {
            "frame": {"x": 0, "y": i * frame_h, "w": frame_w, "h": frame_h},
            "rotated": False,
            "trimmed": False,
            "spriteSourceSize": {"x": 0, "y": 0, "w": frame_w, "h": frame_h},
            "sourceSize": {"w": frame_w, "h": frame_h},
            "duration": 100,
        }
    doc["meta"] = {
        "app": "https://www.aseprite.org/",
        "version": "1.3.7",
        "image": f"{base}.png",
        "format": "I8",
        "size": {"w": frame_w, "h": frame_h * frames},
        "scale": "1",
        "frameTags": [],
        "layers": [{"name": "Layer 1", "opacity": 255,
                    "blendMode": "normal"}],
        "slices": [],
    }
    return json.dumps(doc, indent=2) + "\n"


# --- a tiny canvas -------------------------------------------------------------

class Canvas:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.px = bytearray([TRANSPARENT] * (w * h))

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return TRANSPARENT

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def stamp(self, cells, ox, oy):
        """Paint every opaque cell of a 2-D list at an offset."""
        for y, row in enumerate(cells):
            for x, c in enumerate(row):
                if c != TRANSPARENT:
                    self.put(ox + x, oy + y, c)

    def copy(self):
        other = Canvas(self.w, self.h)
        other.px[:] = self.px
        return other


def frame_cells(sheet, frame_w, frame_h, index, x0=0, y0=0, w=None, h=None):
    """A rectangle of one frame of a vertically stacked sheet, as rows."""
    width, _, pixels = sheet
    w = frame_w - x0 if w is None else w
    h = frame_h - y0 if h is None else h
    rows = []
    for y in range(h):
        base = (index * frame_h + y0 + y) * width + x0
        rows.append(list(pixels[base:base + w]))
    return rows


def load_sheet(name):
    return read_indexed_png(os.path.join(PIX, name))


# --- the four sprites ----------------------------------------------------------

def make_mine():
    """The dark bomb's body, rimmed and fitted with a blinking team light."""
    bomb = load_sheet("bomb1.png")
    # bomb1.png frame 10: the unlit bomb, its body in rows 4..10, cols 3..9,
    # with the fuse ember (fire band) at (7, 6).
    body = frame_cells(bomb, 13, 13, 10, x0=3, y0=4, w=7, h=7)
    for row in body:
        for x, c in enumerate(row):
            if c in FIRE_BAND:
                row[x] = GREY_DARK  # no fuse on a mine
    frames = []
    for lit, glow in ((TEAM_BRIGHT, TEAM_MID), (TEAM_DARK, TEAM_DARKEST)):
        cv = Canvas(9, 9)
        cv.stamp(body, 1, 1)
        # A rim one pixel outside the body: lit on the upper left, shaded on
        # the lower right, so the bomb reads as a flat plate in the ground.
        rim = []
        for y in range(9):
            for x in range(9):
                if cv.get(x, y) != TRANSPARENT:
                    continue
                touches = any(cv.get(x + dx, y + dy) != TRANSPARENT
                              for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if touches:
                    rim.append((x, y))
        for x, y in rim:
            cv.put(x, y, GREY_RIM if x + y < 8 else GREY_MID)
        # The light: a plus in the middle, the rest of the 3x3 stays dark.
        cv.put(4, 4, lit)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            cv.put(4 + dx, 4 + dy, glow)
        for dx, dy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
            cv.put(4 + dx, 4 + dy, GREY_BLACK)
        frames.append(cv)
    return 9, 9, frames


def make_banner():
    """A skull on a pole with a rippling team cloth (the capture flag's wave)."""
    skel = load_sheet("skeleton.png")
    # skeleton.png frame 0, rows 0..4, cols 5..9: the skull, eye sockets and
    # nose in grey 17. Any team-band pixel would turn into the team colour;
    # there is none in this window, but keep it bone-grey whatever happens.
    skull = frame_cells(skel, 15, 13, 0, x0=5, y0=0, w=5, h=5)
    for row in skull:
        for x, c in enumerate(row):
            if c in TEAM_BAND:
                row[x] = GREY_BONE
    w, h = 12, 22
    frames = []
    cloth_left, cloth_right = 4, 11
    cloth_top, cloth_bottom = 7, 16
    for fi in range(4):
        phase = fi * (math.pi / 2.0)
        cv = Canvas(w, h)
        # Pole, lit on its left edge, from under the skull to the ground.
        for y in range(5, h):
            cv.put(2, y, WOOD_LIGHT)
            cv.put(3, y, WOOD_DARK)
        for x in range(1, 5):
            cv.put(x, h - 1, WOOD_DARKEST)  # foot
        # Cross yard the cloth hangs from.
        for x in range(1, cloth_right + 1):
            cv.put(x, 6, WOOD)
        cv.put(cloth_right, 6, WOOD_DARK)
        cv.put(1, 6, WOOD_DARK)
        # Cloth: hangs from the yard; a wave runs along it so each column's
        # free bottom edge rises and falls and its shade follows the wave
        # (the capture flag's rule, turned to hang down).
        for x in range(cloth_left, cloth_right + 1):
            t = (x - cloth_left) / (cloth_right - cloth_left)
            wave = math.sin(t * 2.0 * math.pi + phase)
            bottom = cloth_bottom - 1 + (0 if x == cloth_left
                                         else round(wave))
            if 7 <= x <= 8:
                bottom -= 2  # swallowtail notch
            for y in range(cloth_top, bottom + 1):
                if wave > 0.5:
                    c = TEAM_BRIGHT
                elif wave > -0.25:
                    c = TEAM_LIGHT if y <= cloth_top + 3 else TEAM_MID
                else:
                    c = TEAM_MID if y <= cloth_top + 3 else TEAM_DARK
                # Edges read darker so the cloth has a silhouette.
                if x in (cloth_left, cloth_right) or y == bottom:
                    c = TEAM_DARK if c >= TEAM_MID else TEAM_DARKEST
                cv.put(x, y, c)
        # The fallen one's head on top.
        cv.stamp(skull, 0, 0)
        cv.put(2, 5, WOOD_DARK)  # the pole shows under the jaw
        frames.append(cv)
    return w, h, frames


def make_bonewall():
    """Upright posts of stacked bones lashed with sideways bones."""
    bone = load_sheet("bone1.png")
    upright = frame_cells(bone, 7, 7, 0, x0=2, y0=0, w=5, h=7)   # 5x7
    sideways = frame_cells(bone, 7, 7, 2, x0=0, y0=2, w=7, h=4)  # 7x4

    def paint(cracked):
        cv = Canvas(16, 16)
        # No ground shadow: no obstacle sprite the game ships has one, the
        # engine draws its own. Posts stand at x=1, 6 and 11 (shafts 2-3,
        # 7-8 and 12-13) so the wall is centred, x=1..14. Each bar is lashed
        # post to post: its two end knobs sit under the shafts it joins.
        # The bars go up first so the posts stand in front of them. On the
        # cracked wall the upper right bar has broken off at post 2 and its
        # loose end hangs between post 2 and post 3.
        cv.stamp(sideways, 2, 2)
        if cracked:
            cv.stamp([row[:4] for row in sideways], 7, 2)
            cv.stamp([[29, 23], [22, 19]], 9, 5)
        else:
            cv.stamp(sideways, 7, 2)
        cv.stamp(sideways, 2, 8)
        cv.stamp(sideways, 7, 8)
        for px in (1, 6, 11):
            # The cracked wall's middle post has lost its top bone.
            if not (cracked and px == 6):
                cv.stamp(upright, px, 1)
            cv.stamp(upright, px, 7)
        if cracked:
            # Cracks across the left post and the lower bar, and a splinter
            # on the ground under the broken bar.
            for x, y in ((2, 10), (3, 11), (2, 12), (10, 9), (11, 10)):
                cv.put(x, y, GREY_SHADOW)
            cv.put(9, 14, GREY_BONE)
            cv.put(10, 14, GREY_DARK)
        return cv

    return 16, 16, [paint(False), paint(True)]


def make_ember():
    """A charred spot with the meteor's flame standing on it."""
    meteor = load_sheet("meteor.png")
    # meteor.png frame 0, cols 2..5: a flame shape with flickering tips on
    # top (rows 0..1), a full body (rows 2..5) and a narrow root (row 6).
    flame = frame_cells(meteor, 8, 8, 0, x0=2, y0=0, w=4, h=7)
    char = Canvas(7, 7)
    for y in range(7):
        for x in range(7):
            d = ((x - 3) / 3.4) ** 2 + ((y - 5.5) / 1.5) ** 2
            if d <= 0.45:
                char.put(x, y, GREY_BLACK)
            elif d <= 1.0:
                char.put(x, y, GREY_SHADOW)
    live = char.copy()
    # Rows 1..6 of the flame, its root on the charred spot.
    live.stamp(flame[1:], 1, 0)
    # A second, smaller lick beside it.
    live.put(5, 3, flame[2][3])
    live.put(5, 4, flame[4][3])
    dying = char.copy()
    # Only the root of the flame is left, and two glowing coals.
    dying.stamp(flame[5:], 1, 4)
    dying.put(4, 3, flame[0][2])
    dying.put(5, 6, 227)
    dying.put(1, 5, 225)
    return 7, 7, [live, dying]


SPRITES = (
    ("mine", make_mine),
    ("banner", make_banner),
    ("bonewall", make_bonewall),
    ("ember", make_ember),
)


# Blank sprites: name -> (width, height). One frame, every pixel transparent.
BLANKS = (
    ("kit_marker", 40, 60),
)


def check_bands(name, frames):
    """The palette rules the art test pins, checked before anything is written."""
    for fi, cv in enumerate(frames):
        if all(p == TRANSPARENT for p in cv.px):
            raise SystemExit(f"{name}: frame {fi} is empty")
        for p in cv.px:
            if p in WATER_BAND:
                raise SystemExit(f"{name}: water band index {p}")
            if p in FIRE_BAND and name != "ember":
                raise SystemExit(f"{name}: fire band index {p}")
            if p in TEAM_BAND and name not in ("mine", "banner"):
                raise SystemExit(f"{name}: team band index {p}")


def build(palette):
    """name -> (png bytes, sidecar text)."""
    out = {}
    for name, painter in SPRITES:
        w, h, frames = painter()
        check_bands(name, frames)
        if len({bytes(cv.px) for cv in frames}) != len(frames):
            raise SystemExit(f"{name}: two frames are identical")
        pixels = b"".join(bytes(cv.px) for cv in frames)
        png = encode_indexed_png(w, h * len(frames), pixels, palette)
        out[name] = (png, aseprite_sidecar(name, w, h, len(frames)))
    for name, w, h in BLANKS:
        blank = bytes([TRANSPARENT]) * (w * h)
        out[name] = (encode_indexed_png(w, h, blank, palette), None)
    return out


def main(argv):
    check = "--check" in argv[1:]
    palette = parse_gpl(os.path.join(PIX, "openglad.gpl"))
    engine = parse_engine_palette(
        os.path.join(REPO, "src", "resources", "our_palette.cpp"))
    for i, (a, b) in enumerate(zip(palette, engine)):
        for c in range(3):
            if abs(a[c] - b[c]) > 1:
                raise SystemExit(f"palette drift at entry {i} channel {c}")
    stale = []
    for name, (png, sidecar) in build(palette).items():
        targets = [(os.path.join(PIX, f"{name}.png"), png, "wb")]
        if sidecar is not None:
            targets.append((os.path.join(PIX, f"{name}.json"), sidecar, "w"))
        for path, data, mode in targets:
            if check:
                want = data if isinstance(data, bytes) else data.encode()
                try:
                    with open(path, "rb") as f:
                        have = f.read()
                except FileNotFoundError:
                    have = None
                if have != want:
                    stale.append(os.path.relpath(path, REPO))
                continue
            kwargs = {} if mode == "wb" else {"encoding": "utf-8"}
            with open(path, mode, **kwargs) as f:
                f.write(data)
            print(f"wrote {os.path.relpath(path, REPO)}")
    if stale:
        print("out of date (rerun without --check): " + ", ".join(stale))
        return 1
    if check:
        print("special art is up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
