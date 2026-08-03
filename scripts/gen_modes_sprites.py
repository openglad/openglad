#!/usr/bin/env python3
"""Generate the Multiplayer Game Modes pack sprites.

Writes tools/modes_mapgen/pack/sprites/{flag.png,flag.json,ctfpoint.png,
ball.png,aura.png,aura.json}. The flag and control-point art are painted by
the same functions scripts/gen_ctf_sprites.py uses for pix/flag.png and
pix/ctfpoint.png (imported, not copied), so the pack copies stay
byte-identical to the repo-global originals for as long as both exist. The
ball (soccer) and aura (mutant identity FX) are new painters following the
same palette rules:

  - indexed 8-bit PNG, the full 256-entry our.pal palette, index 0
    transparent
  - team-tint band 248..255 only where a sprite is team-owned (flag,
    ctfpoint); the ball is neutral grays and the aura is fixed red/gold
    (the Mutant reads the same on every team)
  - multi-frame sprites stack frames vertically and carry an Aseprite
    "Hash" sidecar <base>.json

Run from the repo root:  python3 scripts/gen_modes_sprites.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gen_ctf_sprites as ctf  # noqa: E402  (path bootstrap above)

OUT = os.path.join(ctf.REPO, "tools", "modes_mapgen", "pack", "sprites")

# Red ramp entries for the aura ring (team-red base 40..47) and gold sparks
# (yellow ramp 88..95). Fixed colors on purpose: the Mutant's identity must
# read identically for every viewer regardless of the wearer's team.
RED_BRIGHT = 40
RED_MID = 42
GOLD_BRIGHT = 88
GOLD_MID = 89


def make_ball(size=12):
    """The soccer ball: a neutral gray sphere with pentagon dots."""
    px = bytearray([ctf.TRANSPARENT] * (size * size))
    cx = cy = (size - 1) / 2.0
    radius = size / 2.0 - 0.5
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy)
            if d > radius:
                continue
            if d > radius - 1.0:
                c = ctf.GRAY_DARK  # 1px outline
            elif (x - cx) + (y - cy) > 2.0:
                c = ctf.GRAY_LIGHT  # lower-right shading
            else:
                c = ctf.GRAY_BRIGHT
            px[y * size + x] = c
    # Pentagon dots: four dark patches in a diamond around the center.
    for dx, dy in ((0, -3), (3, 1), (-3, 1), (0, 3)):
        for ox, oy in ((0, 0), (1, 0), (0, 1)):
            x = int(cx) + dx + ox
            y = int(cy) + dy + oy
            if math.hypot(x - cx, y - cy) <= radius - 1.0:
                px[y * size + x] = ctf.GRAY_SHADOW
    # Center patch.
    px[int(cy) * size + int(cx)] = ctf.GRAY_SHADOW
    px[int(cy) * size + int(cx) + 1] = ctf.GRAY_SHADOW
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
        px = bytearray([ctf.TRANSPARENT] * (size * size))
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
    palette = ctf.parse_gpl(os.path.join(ctf.PIX, "openglad.gpl"))
    engine = ctf.parse_engine_palette(
        os.path.join(ctf.REPO, "src", "resources", "our_palette.cpp"))
    for i, (a, b) in enumerate(zip(palette, engine)):
        for c in range(3):
            if abs(a[c] - b[c]) > 1:
                raise SystemExit(
                    f"palette drift at entry {i} channel {c}: "
                    f"gpl {a[c]} vs engine {b[c]}")

    os.makedirs(OUT, exist_ok=True)
    grays = {ctf.GRAY_SHADOW, ctf.GRAY_DARK, ctf.GRAY_MID, ctf.GRAY_LIGHT,
             ctf.GRAY_BRIGHT}

    # Flag + control point: the CTF painters, verbatim, into the pack dir.
    flag_frames = ctf.make_flag_frames()
    for f in flag_frames:
        ctf.check_band("flag.png", f, team_ok=True, neutral_ok=grays)
    n = ctf.write_indexed_png(os.path.join(OUT, "flag.png"), 10,
                              14 * len(flag_frames), b"".join(flag_frames),
                              palette)
    print(f"wrote {OUT}/flag.png ({n} bytes, {len(flag_frames)} frames)")
    with open(os.path.join(OUT, "flag.json"), "w", encoding="utf-8") as f:
        f.write(ctf.aseprite_sidecar("flag", 10, 14, len(flag_frames)))
    print(f"wrote {OUT}/flag.json")

    point = ctf.make_ctf_point()
    ctf.check_band("ctfpoint.png", point, team_ok=True, neutral_ok=grays)
    n = ctf.write_indexed_png(os.path.join(OUT, "ctfpoint.png"), 16, 16,
                              point, palette)
    print(f"wrote {OUT}/ctfpoint.png ({n} bytes, single 16x16 frame)")

    ball = make_ball()
    ctf.check_band("ball.png", ball, team_ok=False, neutral_ok=grays)
    n = ctf.write_indexed_png(os.path.join(OUT, "ball.png"), 12, 12, ball,
                              palette)
    print(f"wrote {OUT}/ball.png ({n} bytes, single 12x12 frame)")

    aura_frames = make_aura_frames()
    aura_colors = {RED_BRIGHT, RED_MID, GOLD_BRIGHT, GOLD_MID}
    for f in aura_frames:
        ctf.check_band("aura.png", f, team_ok=False, neutral_ok=aura_colors)
    n = ctf.write_indexed_png(os.path.join(OUT, "aura.png"), 16,
                              16 * len(aura_frames), b"".join(aura_frames),
                              palette)
    print(f"wrote {OUT}/aura.png ({n} bytes, {len(aura_frames)} frames)")
    with open(os.path.join(OUT, "aura.json"), "w", encoding="utf-8") as f:
        f.write(ctf.aseprite_sidecar("aura", 16, 16, len(aura_frames)))
    print(f"wrote {OUT}/aura.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
