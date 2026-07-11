#!/usr/bin/env python3
"""Encode FX-capture PPM frame directories into an APNG review site.

Input: an anim directory of <scene>/NNN.ppm sequences, as produced by the
env-gated capture tests (see scripts/fx_review/generate.sh). Output: a static
site (index.html + one looping APNG per scene) that any browser renders
natively — no JS, no video codecs.

The encoder is pure Python (zlib only): each PPM frame becomes an fcTL+fdAT
APNG chunk pair, 2x nearest-neighbor upscaled so the 320x200 art reads well.
Scenes with no frame directory are skipped with a note, so partial captures
still produce a reviewable site.
"""
import argparse
import os
import struct
import zlib


def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c))


def load_ppm(path):
    raw = open(path, 'rb').read()
    header, rest = raw.split(b'255\n', 1)
    dims = header.split()
    w, h = int(dims[1]), int(dims[2])
    return w, h, rest


def upscale2x(w, h, data):
    out = bytearray()
    for y in range(h):
        row = bytearray()
        base = y * w * 3
        for x in range(w):
            px = data[base + x * 3: base + x * 3 + 3]
            row += px + px
        out += row + row
    return w * 2, h * 2, bytes(out)


def raw_to_idat(w, h, data):
    rows = b''.join(b'\x00' + data[y * w * 3:(y + 1) * w * 3] for y in range(h))
    return zlib.compress(rows, 6)


def make_apng(frames_dir, out_path, delay_num=6, delay_den=100):
    files = sorted(f for f in os.listdir(frames_dir) if f.endswith('.ppm'))
    if not files:
        return 0
    seq = 0
    first = True
    body = b''
    W = H = 0
    for fname in files:
        w, h, data = load_ppm(os.path.join(frames_dir, fname))
        w, h, data = upscale2x(w, h, data)
        W, H = w, h
        comp = raw_to_idat(w, h, data)
        fctl = struct.pack('>IIIIIHHBB', seq, w, h, 0, 0,
                           delay_num, delay_den, 0, 0)
        body += chunk(b'fcTL', fctl)
        seq += 1
        if first:
            body += chunk(b'IDAT', comp)
            first = False
        else:
            body += chunk(b'fdAT', struct.pack('>I', seq) + comp)
            seq += 1
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    png += chunk(b'acTL', struct.pack('>II', len(files), 0))
    png += body
    png += chunk(b'IEND', b'')
    open(out_path, 'wb').write(png)
    return len(files)


def compose_side_by_side(dir_a, dir_b, out_dir, divider=4):
    """Pair frames from two capture dirs into one wide PPM stream (host |
    joiner) so synchronized sessions can be reviewed as a single loop."""
    os.makedirs(out_dir, exist_ok=True)
    fa = sorted(f for f in os.listdir(dir_a) if f.endswith('.ppm'))
    fb = sorted(f for f in os.listdir(dir_b) if f.endswith('.ppm'))
    n = min(len(fa), len(fb))
    for i in range(n):
        wa, ha, da = load_ppm(os.path.join(dir_a, fa[i]))
        wb, hb, db = load_ppm(os.path.join(dir_b, fb[i]))
        h = min(ha, hb)
        W = wa + divider + wb
        rows = bytearray()
        for y in range(h):
            rows += da[y * wa * 3:(y + 1) * wa * 3]
            rows += b'\x20\x20\x28' * divider
            rows += db[y * wb * 3:(y + 1) * wb * 3]
        with open(os.path.join(out_dir, f"{i:03d}.ppm"), 'wb') as f:
            f.write(b'P6\n' + f"{W} {h}".encode() + b'\n255\n' + bytes(rows))
    return n


EPICS = [
    ("epic_605", "The Deeping Wall",
     "Level 605. The Wall Watch (a hero crew deployed at the level start markers) holds the rampart and gate against the horde. At frame 500, look to the east: the Grey Wizard arrives at dawn behind the flank in a teleport flash (the delayed-spawn feature) and the battle turns. Live simulation, spectator camera."),
    ("epic_606", "The Wizard's Vale",
     "Level 606. Tree-folk and druids march from the forest ring across the moat bridges while mages hold a four-floor tower — the cut to the glass observatory looks straight down through the fights below."),
    ("epic_607", "The Bridge of Shadow",
     "Level 607. The rearguard crosses the causeway while the Grey Wizard holds mid-span against the fire elemental and the undead tide. His teleport is disabled by the per-NPC scenario flag: he cannot leave the bridge. He shall not pass; he also, presumably, shall not survive."),
    ("epic_608", "Under the Mountain",
     "Level 608. Raiders descend three floors — entry hall, pillared gallery, treasure vault — where golems, giant skeletons and a sleeping keeper ward the hoard."),
    ("epic_609", "The Black Gate",
     "Level 609. The biggest battle in the set: ~115 living fighters plus reinforcement generators on both sides, meeting at a fortified gate. The camera sweeps the full mile of the field before settling on the clash."),
    ("epic_610", "The Frozen Wall",
     "Level 610. The wall itself is the arena — a tracking shot along the walkable top under the open storm, wildlings and worse climbing from the north, the Watch holding the line."),
]

MENUS = [
    ("menu_tour", "Menu system tour",
     "The picker driven by a scripted session: main menu into SETTINGS, the controls screen, and back. Watch buttons highlight and screens swap — this is the real SDL menu loop, captured live."),
    ("menu_effects", "Effects settings walkthrough",
     "The three-way FX split: GAMEPLAY FX, UI FX and GRAPHICS FX subscreens, with toggles flipping red/green live (each is flipped twice so your saved settings are untouched)."),
]

MULTI = [
    ("splitscreen_gameplay", "Local split-screen (2 players)",
     "A real two-player local session: both viewports run live simulation side by side, each camera following its own hero. One shared world, one machine."),
    # net_host/net_joiner capture is not wired up yet: the harness that drove
    # a genuine host+joiner session and dumped both screens at the same
    # authoritative tick was lost with its throwaway worktree. Rebuild it from
    # the host_and_join_* e2e patterns in tests/test_game_loop.cpp, dump to
    # anim/net_host and anim/net_joiner, and this card lights up again.
    ("net_sync", "Networked multiplayer — host and joiner, synchronized",
     "A genuine networked session in one capture: the HOST's screen (left) and the JOINING CLIENT's screen (right), every frame taken at the same authoritative tick. Note the same weather on both sides — the per-level weather kind is world state, rolled by the host and synced through snapshots."),
]

GAMEPLAY = [
    ("gameplay_clouds", "Real gameplay — clouds",
     "Genuine game_frame simulation on campaign level 1: an AI-driven hero team meets the level's enemies while cloud banks and their ground shadows drift over the battle. Watch real walk animations, combat, blood, and the radar."),
    ("gameplay_rain", "Real gameplay — slanted rain + lightning",
     "Level 1 under the Rain weather kind: streaks fall at a slight angle (1px right per 4 down) across the whole viewport, with a scheduled lightning flash mid-loop. Everything else is live sim."),
    ("gameplay_combat", "Real gameplay — combat effects",
     "Campaign level 3, all effects on, no weather: shadows under every fighter, trails and glow on live projectiles, gore where someone falls, screen shake on detonations. Battles come in waves — quiet moments are real too."),
    ("gameplay_water", "Real gameplay — shoreline battle",
     "The wateriest early level (6) under clouds: ripples and reflections where fighters cross water, cloud shadows sweeping the field, live combat throughout."),
]

SCENES = [
    ("shadows", "Unit & weapon shadows",
     "Squashed ground shadows under the walkers. Watch the knife: as it arcs upward the shadow stays on the ground and the gap between blade and shadow grows with height."),
    ("glass_reflections", "Glass reflections",
     "The walker crossing the glass pane casts a flipped, team-colored reflection that appears only on glass tiles and tracks the walk."),
    ("water_reflections", "Water reflections",
     "Same mirror effect on open water: walking the shoreline paints a shimmering flipped reflection in the (palette-animated) lake."),
    ("ripples", "Water ripples",
     "Expanding elliptical rings under units standing or wading in water, phased per-entity so the two walkers do not pulse in sync."),
    ("clouds", "Clouds + ground shadows",
     "Weather kind: Clouds. Soft banks drift over the field while their displaced dark shadows sweep the ground (sun from the northwest). No rain in this mode."),
    ("rain", "Rain + lightning",
     "Weather kind: Rain. Full-screen streaks falling at a slight angle with continuous motion (no twinkling). A scheduled lightning flash brightens two frames early in the loop. No cloud banks in this mode."),
    ("fire_glow", "Fire glow",
     "Warm halos around the fire elemental and fireball, painted with the stable (non-cycled) fire color. The glow breathes slowly — a fast ~1s pulse over a slower swell — instead of strobing."),
    ("trails", "Projectile trails",
     "Fading dot trails behind the flying projectiles: fire-colored behind the fireball, pale behind the knife."),
    ("dust", "Falling dust (multifloor)",
     "The camera is on the lower floor; someone pacing on the floor above sheds grey specks that drift down into your space — a cross-floor presence cue."),
    ("depth_tint", "Depth tint (multifloor)",
     "Looking down through air holes from two floors up. The tint covers EVERYTHING on a lower floor — tiles and characters alike: the soldier one floor down is cold-muted, the one two floors down colder still, while the camera-floor soldier stays untinted."),
    ("screen_shake", "Screen shake",
     "A detonation is active mid-screen: the whole camera jitters 1-2px with deterministic decaying jolts while the explosion lives."),
    ("all_together", "Everything at once",
     "The full ensemble: clouds + ground shadows overhead, shoreline reflections and ripples, an arcing fireball with trail, glow and ground shadow, plus screen shake from the detonation."),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--anim', required=True,
                    help='directory of <scene>/NNN.ppm frame sequences')
    ap.add_argument('--site', required=True,
                    help='output directory for index.html + APNGs')
    args = ap.parse_args()
    anim, site = args.anim, args.site
    os.makedirs(site, exist_ok=True)

    if (os.path.isdir(os.path.join(anim, "net_host"))
            and os.path.isdir(os.path.join(anim, "net_joiner"))):
        n = compose_side_by_side(os.path.join(anim, "net_host"),
                                 os.path.join(anim, "net_joiner"),
                                 os.path.join(anim, "net_sync"))
        print(f"net_sync composed: {n} pairs")

    def cards_for(entries):
        cards = []
        for name, title, desc in entries:
            frames_dir = os.path.join(anim, name)
            if not os.path.isdir(frames_dir):
                print(f"SKIP {name} (no frames)")
                continue
            n = make_apng(frames_dir, os.path.join(site, f"{name}.png"))
            print(f"{name}: {n} frames")
            cards.append(f"""
    <div class="card">
      <img src="{name}.png" alt="{title}" loading="lazy">
      <h2>{title}</h2>
      <p>{desc}</p>
    </div>""")
        return ''.join(cards)

    sections = [
        ("Epic levels (spectator mode, AI armies, cinematic camera)",
         cards_for(EPICS)),
        ("Multiplayer (live simulation)", cards_for(MULTI)),
        ("Menu system", cards_for(MENUS)),
        ("Actual gameplay (live simulation, campaign levels)",
         cards_for(GAMEPLAY)),
        ("Effect close-ups (real renderer, scripted scenes)",
         cards_for(SCENES)),
    ]
    section_html = ''.join(
        f'\n<h2 style="text-align:center;color:#f0c060;font-weight:normal;'
        f'margin-top:2.5rem;">{title}</h2>\n<div class="grid">{cards}</div>'
        for title, cards in sections if cards)

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenGlad — Multifloor FX Review</title>
<style>
  body {{ background:#14161a; color:#e8e6e0; font-family: Georgia, serif; margin:0; padding:2rem; }}
  h1 {{ text-align:center; font-weight:normal; letter-spacing:.06em; }}
  .sub {{ text-align:center; color:#9a978e; margin-bottom:2rem; }}
  .grid {{ display:grid; grid-template-columns:repeat(auto-fill,minmax(680px,1fr)); gap:2rem; max-width:1500px; margin:0 auto; }}
  .card {{ background:#1d2026; border:1px solid #2c3038; border-radius:8px; padding:1rem; }}
  .card img {{ width:640px; max-width:100%; image-rendering:pixelated; border-radius:4px; display:block; margin:0 auto; }}
  .card h2 {{ font-size:1.1rem; margin:.8rem 0 .3rem; color:#f0c060; font-weight:normal; }}
  .card p {{ font-size:.92rem; line-height:1.45; color:#b8b5aa; margin:0; }}
</style>
</head>
<body>
<h1>OpenGlad &mdash; Multifloor Special Effects Review</h1>
<p class="sub">All frames come from the real engine at 320&times;200 (upscaled 2&times;) with live palette cycling. All animations loop.</p>
{section_html}
</body>
</html>'''
    with open(os.path.join(site, "index.html"), "w") as f:
        f.write(html)
    print("site written:", os.path.join(site, "index.html"))


if __name__ == '__main__':
    main()
