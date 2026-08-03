#!/usr/bin/env python3
"""Generate the Multiplayer Game Modes pack sprites.

Writes tools/modes_mapgen/pack/sprites/{flag.png,flag.json,ctfpoint.png,
ball.png,aura.png,aura.json}. Stdlib-only (struct + zlib). Output PNGs are
indexed 8-bit with the full 256-entry OpenGlad palette parsed from
pix/openglad.gpl, matching what read_pixie_file (src/resources/io/
og_file.cpp) validates:
  - colortype 3 (palette), bitdepth 8
  - exactly 256 palette entries, each within +/-1 per channel of the
    engine palette (our_pal_lookup 6-bit values scaled by *255/63)
  - frames stacked vertically; frame metadata in an Aseprite "Hash"
    sidecar <base>.json (single-frame sprites need no sidecar)

Sprite rules:
  - palette index 0 is transparent
  - indices 248..255 are the team-tint band: walkputbuffer remaps
    p > 247 to teamcolor + (255 - p), so 255 is the brightest team
    shade and 248 the darkest; team tint only where a sprite is
    team-owned (flag, ctfpoint). The ball is neutral grays and the aura
    is fixed red/gold (the Mutant reads the same on every team).

The flag and control-point painters were folded in from the retired
scripts/gen_ctf_sprites.py when the core CTF families left the engine.

Run from the repo root:  python3 scripts/gen_modes_sprites.py
"""

import json
import math
import os
import re
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIX = os.path.join(REPO, "pix")  # palette source (openglad.gpl) only

# Team-tint band (drawn shade depends on the owning team's base color).
TEAM_BRIGHT = 255   # remaps to teamcolor + 0 (brightest team shade)
TEAM_LIGHT = 254
TEAM_MID = 252
TEAM_DARK = 250
TEAM_DARKEST = 248  # remaps to teamcolor + 7 (darkest team shade)

# Neutral grays from the 16..31 gray ramp (see pix/openglad.gpl).
GRAY_SHADOW = 17    # ~(60,60,60)
GRAY_DARK = 19      # ~(85,85,85)
GRAY_MID = 21       # ~(109,109,109)
GRAY_LIGHT = 23     # ~(133,133,133)
GRAY_BRIGHT = 25    # ~(157,157,157)

TRANSPARENT = 0


def parse_gpl(path):
    """Parse a GIMP palette file into a list of (r, g, b) tuples."""
    colors = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if (not line or line.startswith("#") or line.startswith("GIMP")
                    or line.startswith("Name:") or line.startswith("Columns:")):
                continue
            m = re.match(r"^(\d+)\s+(\d+)\s+(\d+)", line)
            if m:
                colors.append(tuple(int(m.group(i)) for i in (1, 2, 3)))
    if len(colors) != 256:
        raise SystemExit(f"{path}: expected 256 palette entries, got {len(colors)}")
    return colors


def parse_engine_palette(path):
    """Cross-check source: the 6-bit our_pal_lookup table in our_palette.cpp."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"\bdata(?:\[\])?\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"{path}: could not locate palette data array")
    values = [int(v) for v in re.findall(r"\d+", m.group(1))]
    if len(values) != 768:
        raise SystemExit(f"{path}: expected 768 palette bytes, got {len(values)}")
    return [tuple((values[i * 3 + c] * 255) // 63 for c in range(3))
            for i in range(256)]


def png_chunk(kind, payload):
    raw = kind + payload
    return struct.pack(">I", len(payload)) + raw + struct.pack(">I", zlib.crc32(raw) & 0xFFFFFFFF)


def write_indexed_png(path, width, height, pixels, palette):
    """Write an indexed 8-bit PNG with a 256-entry palette and tRNS[0]=0."""
    if len(pixels) != width * height:
        raise SystemExit(f"{path}: pixel buffer size mismatch")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(c) for c in palette)
    trns = bytes([0] + [255] * 255)
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)  # filter: none
        scanlines.extend(pixels[y * width:(y + 1) * width])
    idat = zlib.compress(bytes(scanlines), 9)
    blob = (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", ihdr)
            + png_chunk(b"PLTE", plte)
            + png_chunk(b"tRNS", trns)
            + png_chunk(b"IDAT", idat)
            + png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(blob)
    return len(blob)


def make_flag_frames(width=10, height=14, frames=4):
    """A pole on the left with a rippling team-tinted banner."""
    out = []
    cloth_top, cloth_bottom = 1, 7          # banner rows
    cloth_left, cloth_right = 1, 9          # banner columns (pole at x=0)
    for fi in range(frames):
        phase = fi * (math.pi / 2.0)
        px = bytearray([TRANSPARENT] * (width * height))

        def put(x, y, c):
            if 0 <= x < width and 0 <= y < height:
                px[y * width + x] = c

        # Pole: neutral grays, full height, lit cap and grounded base.
        for y in range(height):
            put(0, y, GRAY_DARK)
        put(0, 0, GRAY_BRIGHT)
        put(0, 1, GRAY_LIGHT)
        put(0, height - 1, GRAY_SHADOW)
        put(0, height - 2, GRAY_MID)

        # Banner cloth: per-column vertical ripple; brightness follows the
        # wave so the cloth reads as fabric catching light. Team band only.
        for x in range(cloth_left, cloth_right + 1):
            t = (x - cloth_left) / max(1, cloth_right - cloth_left)
            wave = math.sin(t * 2.0 * math.pi + phase)
            dy = round(wave * (0.0 if x == cloth_left else 1.0))
            # Swallowtail: the trailing column keeps only its outer rows.
            rows = range(cloth_top, cloth_bottom + 1)
            for y in rows:
                if x == cloth_right and y in (cloth_top + 2, cloth_top + 3, cloth_top + 4):
                    continue  # notch
                yy = y + dy
                if wave > 0.5:
                    c = TEAM_BRIGHT
                elif wave > -0.25:
                    c = TEAM_LIGHT if y <= cloth_top + 2 else TEAM_MID
                else:
                    c = TEAM_MID if y <= cloth_top + 2 else TEAM_DARK
                # Edges read darker so the banner has a silhouette.
                if y in (cloth_top, cloth_bottom) or x == cloth_right:
                    c = TEAM_DARK if c >= TEAM_MID else TEAM_DARKEST
                put(x, yy, c)
        # Hoist column stays bright and anchored against the pole.
        for y in range(cloth_top, cloth_bottom + 1):
            put(cloth_left, y, TEAM_LIGHT if y % 2 else TEAM_BRIGHT)
        out.append(bytes(px))
    return out


def make_ctf_point(size=16):
    """A stone pad with a team-tint ring and a neutral center."""
    px = bytearray([TRANSPARENT] * (size * size))
    cx = cy = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy)
            if d > 7.6:
                continue
            if d > 6.0:
                # Team-tint ring: lit on the upper-left, shaded lower-right.
                lit = (x - cx) + (y - cy) < 0
                c = TEAM_LIGHT if lit else TEAM_DARK
                if d > 7.2:
                    c = TEAM_DARKEST
            elif d > 5.0:
                c = GRAY_SHADOW  # grout line between ring and pad
            else:
                # Neutral stone center with deterministic speckle.
                c = GRAY_MID if (x * 7 + y * 13) % 5 else GRAY_LIGHT
                if d < 1.2:
                    c = GRAY_BRIGHT  # polished center boss
            px[y * size + x] = c
    return bytes(px)


def aseprite_sidecar(base, frame_w, frame_h, frames):
    """Aseprite 'Hash' export format, mirroring the committed sidecars."""
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
        "layers": [{"name": "Layer 1", "opacity": 255, "blendMode": "normal"}],
        "slices": [],
    }
    return json.dumps(doc, indent=2) + "\n"


def check_band(name, pixels, team_ok, neutral_ok):
    for i, p in enumerate(pixels):
        if p == TRANSPARENT:
            continue
        if 248 <= p <= 255:
            if not team_ok:
                raise SystemExit(f"{name}: unexpected team-band pixel at {i}")
        elif p not in neutral_ok:
            raise SystemExit(f"{name}: pixel {p} at {i} outside the allowed set")



OUT = os.path.join(REPO, "tools", "modes_mapgen", "pack", "sprites")

# Red ramp entries for the aura ring (team-red base 40..47) and gold sparks
# (yellow ramp 88..95). Fixed colors on purpose: the Mutant's identity must
# read identically for every viewer regardless of the wearer's team.
RED_BRIGHT = 40
RED_MID = 42
GOLD_BRIGHT = 88
GOLD_MID = 89


def make_ball(size=12):
    """The soccer ball: a neutral gray sphere with pentagon dots."""
    px = bytearray([TRANSPARENT] * (size * size))
    cx = cy = (size - 1) / 2.0
    radius = size / 2.0 - 0.5
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy)
            if d > radius:
                continue
            if d > radius - 1.0:
                c = GRAY_DARK  # 1px outline
            elif (x - cx) + (y - cy) > 2.0:
                c = GRAY_LIGHT  # lower-right shading
            else:
                c = GRAY_BRIGHT
            px[y * size + x] = c
    # Pentagon dots: four dark patches in a diamond around the center.
    for dx, dy in ((0, -3), (3, 1), (-3, 1), (0, 3)):
        for ox, oy in ((0, 0), (1, 0), (0, 1)):
            x = int(cx) + dx + ox
            y = int(cy) + dy + oy
            if math.hypot(x - cx, y - cy) <= radius - 1.0:
                px[y * size + x] = GRAY_SHADOW
    # Center patch.
    px[int(cy) * size + int(cx)] = GRAY_SHADOW
    px[int(cy) * size + int(cx) + 1] = GRAY_SHADOW
    return bytes(px)


def make_aura_frames(size=16, frames=4):
    """The mutant aura: a pulsing broken ring, red with gold compass sparks.

    Frame radii grow toward a nearly-closed ring on the last frame; the
    ring gaps rotate per frame so the loop reads as a crackling pulse.
    """
    out = []
    cx = cy = (size - 1) / 2.0
    radii = (3.6, 4.8, 6.0, 7.1)
    for fi in range(frames):
        r = radii[fi]
        px = bytearray([TRANSPARENT] * (size * size))
        # Nearly full circle on the last frame; wider gaps earlier.
        gap_width = (0.9, 0.7, 0.5, 0.15)[fi]
        gap_phase = fi * (math.pi / 3.0)
        for y in range(size):
            for x in range(size):
                d = math.hypot(x - cx, y - cy)
                if abs(d - r) > 0.75:
                    continue
                ang = math.atan2(y - cy, x - cx)
                # Two opposing gaps, rotating with the frame.
                rel = math.remainder(ang - gap_phase, math.pi)
                if abs(rel) < gap_width / 2.0:
                    continue
                lit = ((x + y + fi) % 2) == 0
                px[y * size + x] = RED_BRIGHT if lit else RED_MID
        # Gold sparks at the four compass points of the ring.
        for ang in (0.0, math.pi / 2.0, math.pi, -math.pi / 2.0):
            x = int(round(cx + r * math.cos(ang)))
            y = int(round(cy + r * math.sin(ang)))
            if 0 <= x < size and 0 <= y < size:
                px[y * size + x] = GOLD_BRIGHT if fi % 2 else GOLD_MID
        out.append(bytes(px))
    return out


def main():
    palette = parse_gpl(os.path.join(PIX, "openglad.gpl"))
    engine = parse_engine_palette(
        os.path.join(REPO, "src", "resources", "our_palette.cpp"))
    for i, (a, b) in enumerate(zip(palette, engine)):
        for c in range(3):
            if abs(a[c] - b[c]) > 1:
                raise SystemExit(
                    f"palette drift at entry {i} channel {c}: "
                    f"gpl {a[c]} vs engine {b[c]}")

    os.makedirs(OUT, exist_ok=True)
    grays = {GRAY_SHADOW, GRAY_DARK, GRAY_MID, GRAY_LIGHT,
             GRAY_BRIGHT}

    # Flag + control point: the CTF painters, verbatim, into the pack dir.
    flag_frames = make_flag_frames()
    for f in flag_frames:
        check_band("flag.png", f, team_ok=True, neutral_ok=grays)
    n = write_indexed_png(os.path.join(OUT, "flag.png"), 10,
                              14 * len(flag_frames), b"".join(flag_frames),
                              palette)
    print(f"wrote {OUT}/flag.png ({n} bytes, {len(flag_frames)} frames)")
    with open(os.path.join(OUT, "flag.json"), "w", encoding="utf-8") as f:
        f.write(aseprite_sidecar("flag", 10, 14, len(flag_frames)))
    print(f"wrote {OUT}/flag.json")

    point = make_ctf_point()
    check_band("ctfpoint.png", point, team_ok=True, neutral_ok=grays)
    n = write_indexed_png(os.path.join(OUT, "ctfpoint.png"), 16, 16,
                              point, palette)
    print(f"wrote {OUT}/ctfpoint.png ({n} bytes, single 16x16 frame)")

    ball = make_ball()
    check_band("ball.png", ball, team_ok=False, neutral_ok=grays)
    n = write_indexed_png(os.path.join(OUT, "ball.png"), 12, 12, ball,
                              palette)
    print(f"wrote {OUT}/ball.png ({n} bytes, single 12x12 frame)")

    aura_frames = make_aura_frames()
    aura_colors = {RED_BRIGHT, RED_MID, GOLD_BRIGHT, GOLD_MID}
    for f in aura_frames:
        check_band("aura.png", f, team_ok=False, neutral_ok=aura_colors)
    n = write_indexed_png(os.path.join(OUT, "aura.png"), 16,
                              16 * len(aura_frames), b"".join(aura_frames),
                              palette)
    print(f"wrote {OUT}/aura.png ({n} bytes, {len(aura_frames)} frames)")
    with open(os.path.join(OUT, "aura.json"), "w", encoding="utf-8") as f:
        f.write(aseprite_sidecar("aura", 16, 16, len(aura_frames)))
    print(f"wrote {OUT}/aura.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
