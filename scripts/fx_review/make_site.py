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


WESTLANDS_INTRO = (
    "A 26-level story campaign (org.openglad.westlands): a burdened thief "
    "and his escort flee east out of a burning vale while the war for the "
    "Westlands breaks behind them. Act I is the flight, Act II the dark "
    "road under and over the mountains; at the Falls the road forks — the "
    "war in the west for the armies, the Burden's Road through marsh and "
    "ash for the Bearer — and every road ends at the Mountain of Fire. "
    "Eleven of the twenty-six levels are captured below, in story order: "
    "the six war epics in spectator mode, the two new giants (the White "
    "City and the finale), the High Pass blizzard on the new snow terrain, "
    "and two Bearer's-road levels filmed as real gameplay.")

WESTLANDS = [
    ("westlands_flight", "The Forest Road — the flight (level 2)",
     "Real gameplay, the camera riding with the company's thief. The vale is burning behind them and the Pale Riders wake on the road at their backs (tick 150, and again at 400) while wolf pockets guard every wrong turn of a corridor maze cut through wall-to-wall trees, rain coming down through the canopy. The briefing says it plainly: do not fight — RUN. The Bearer waits at the road's end, and he must not fall."),
    ("epic_5", "The High Pass — blizzard (level 5)",
     "The new SNOW terrain under the new Snow weather kind — this level never rolls dry, because blizzard country is always snowing (the terrain override). Spectator camera: the crew musters on the south apron by the frozen tarn, the tarn wolf-pack hits first, then a cut to the wild men and the chief's band holding the mine gate at the head wall before the camera drifts back down to the fight. The summit road is buried; the only way on is under the mountain."),
    ("epic_6", "Under the Mountain (level 6)",
     "Through the mine gate and down: three floors — entry hall, pillared gallery, treasure vault — where golems, giant skeletons and a sleeping keeper ward the hoard. The camera descends floor by floor with the raid."),
    ("epic_7", "The Frozen Wall (level 7, the northern plea)",
     "The northern watch begs aid, and the road makes its one cold detour. The wall itself is the arena — a tracking shot along the walkable top under the open storm, wild men and worse climbing from the north, the Watch holding the line. Optional level; it rejoins the road at the High Pass."),
    ("epic_8", "The Bridge of Shadow (level 8)",
     "Out of the deep it came, a shadow wreathed in flame. The rearguard runs the causeway while the Grey Wizard holds mid-span against the fire elemental and the undead tide — his teleport disabled by the per-NPC scenario flag: he cannot leave the bridge. He does not pass; neither does he survive. His last word: run. Grieve later."),
    ("epic_14", "The Wizard's Vale (level 14)",
     "The war in the west opens at the traitor's tower: tree-folk and druids march from the forest ring across the moat bridges while the turncoat's mages hold four floors — the cut to the glass observatory looks straight down through the fights below."),
    ("epic_15", "The Deeping Wall (level 15)",
     "The night-siege in the rain. The Wall Watch (a hero crew deployed at the level start markers) holds the rampart and gate against the horde. At frame ~500, look to the east: the White Rider — the Grey Wizard, returned — arrives at dawn behind the flank in a teleport flash (a delayed spawn), and the battle turns. Live simulation, spectator camera."),
    ("epic_16", "The White City (level 16)",
     "The biggest siege of the campaign, on three floors. The gate is already breached and two giant-skeleton war-beasts lead the columns through; ghosts drift over the walls; fires burn in the lower town. The camera opens on the breach, follows the street fighting, cuts up to the wall-top ramparts, holds the beacon on the White Tower's glass crown — then catches the Horse-lord's dawn relief flashing in on the north-west plain at tick 700."),
    ("epic_17", "The Black Gate (level 17)",
     "The feint before the gate, fought to turn every eye from the Bearer. Still the biggest single battle in the set: ~115 living fighters plus reinforcement generators on both sides, meeting at a fortified gate. The camera sweeps the full mile of the field before settling on the clash, then climbs to the gatehouse ramparts."),
    ("westlands_marshes", "The Dead Marshes (level 19)",
     "Real gameplay on the new MARSH tiles: a drowned battlefield of six meres, dead faces burning in the pools. Ghost-lights rise off every mere — their bone-pile wellsprings are sunk on the bog islands, so extermination is impossible by design — while the escort holds the firm shelf and the Bearer shelters behind the line. Marsh glints, ripples, and a great deal of the restless dead."),
    ("epic_24", "The Mountain of Fire (level 24)",
     "The finale: a three-floor volcano cone over an ash plain cut by lava rivers, the war host and the dark host meeting below while the summit waits above. The camera establishes the wedge (the Bearer's warded cleft at the map's west edge), follows the line-brawl, holds the Undergate throat, then climbs the cone — terrace ring, the north lava fall, the summit rim over a caldera of fire, the twin summit cracks — before coming back down to the war. The wild-men relief wakes at tick 400. The point is the climb, not extermination."),
]

LONGSEASON_INTRO = (
    "A 19-level original campaign (org.openglad.longseason): the ledger of "
    "the Brass Kettle Company, hired blades, across one bad-luck year. Every "
    "briefing is a ledger entry — spring flood work, summer contracts, "
    "autumn on the Smelter's Road, a winter of holding passes for pay — and "
    "from the second job onward the pay keeps coming up wrong: coin warm as "
    "bread, in every chest the company opens. The thread runs to a "
    "foundry-city that has been striking its money from its own melted "
    "foundations. Four scenes are captured below, in ledger order: the "
    "spring causeway hold as real gameplay, and three spectator films — the "
    "blizzard toll fort, the creditors' camps at the ash gate, and the "
    "three-floor climb through the mint itself.")

LONGSEASON = [
    ("ls_2", "The Ferry Right (level 2)",
     "Real gameplay in the spring flood, rain coming down on a drowned river. The company plugs the causeway's east mouth so the toll-ferry keeps running: knifemen come over the span first, brigands behind them, and at tick 400 the bandits' boats beach on the north shallows and hit the ferry landing from behind — the level's lesson, learned on camera, is that someone has to hold the rear rank. The ledger notes the job was paid in advance, and that one coin in the pay came warm as bread. The mystery opens here."),
    ("ls_14", "The Long Toll — blizzard (level 14)",
     "Winter work: the Grey Tolls fort astride the pass road, snow wall to wall — this map never rolls dry weather. Spectator camera: establish the courtyard (the watch's braziers, the strongroom with the year's takings), follow the wolf strays probing the gates, then cut to the west mouth as the first toll-breaker wave wakes in a flash at tick 300 and ride that fight back up the road; the east mouth answers at 700. The frozen watch drifts in its gully north of the pass, and the toll chest is warm coin to the last penny."),
    ("ls_17", "Ashfall Gate (level 17)",
     "The reckoning begins: every company the year stiffed is camped on the ash before the foundry-city's west gate, howling for pay, and the Brass Kettle Company has to fight through its own kind to present its bill first. The camera tours the camps — the horse companies' palisade, the hired bows and casters, the wagon-wall corridor of the foot companies — then holds the gate throat, where slag runnels leak out under the city wall (the first sight of the melt) and two golems cast from the warm metal ward the door. At tick 700 the cut-purses wake behind the crew, on the crew's own treasure line."),
    ("ls_18", "The Warm Mint — the finale climb (level 18)",
     "The last entry of the year, on three floors. The mint has been striking coin from the city's own foundations and the vault floor is falling into the melt as it is stripped; the creditor companies force the doors below while the mint's furnace-men, elementals and warm-metal golems hold the works — a three-way war the company climbs through rather than wins. Kettle holds the door pocket, literally. Up the first stair past the casting channels; across the vault floor, threading collapse holes that drop straight into the war below and treasure heaps each warded by a golem; up again to the crucible floor — a lake of melt, ghosts hovering over it, a fire elemental on each rim walk, and The Founder waiting on the dais. The master ledger beside him is the exit: reading the book is the settlement."),
]

MENUS = [
    ("menu_tour", "Menu system tour",
     "The picker driven by a scripted session: main menu into SETTINGS, the controls screen, and back. Watch buttons highlight and screens swap — this is the real SDL menu loop, captured live."),
    ("menu_difficulty", "Difficulty submenu walkthrough",
     "The DIFFICULTY door on the main menu opens the new submenu: Difficulty (Battle/Skirmish/Slaughter), Respawns (Off/Heroes/Everyone), Respawn Delay (Normal/Fast/Slow), Permadeath (On/Off) and Generators (Normal/Calm/Frenzy). Every row is cycled through a full loop — labels change live and every setting ends where it started."),
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
    ("marsh_ripples", "Marsh — static glints, wading-only ripples",
     "The bog under live palette cycling: the pale glint pixels never blink "
     "(they left the cycled water band) and rings appear ONLY under the "
     "wading mover — the standing unit's marsh stays quiet."),
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
    ("screen_shake", "Screen shake",
     "A detonation is active mid-screen: the whole camera jitters 1-2px with deterministic decaying jolts while the explosion lives."),
    ("all_together", "Everything at once",
     "The full ensemble: clouds + ground shadows overhead, shoreline reflections and ripples, an arcing fireball with trail, glow and ground shadow, plus screen shake from the detonation."),
]


DEPTH_INTRO = (
    "One setting, four looks: effects/depth_fx picks how floors BELOW the "
    "camera are treated when an air hole exposes them — fog (the default), "
    "haze, mist, tint, or off. All four scenes share the same vantage: "
    "looking down through air holes from two floors up, in normal play (no "
    "key needed — lower floors always render depth-faded in the standing "
    "view, and the chosen treatment rides that fade; the hold-look-up key "
    "only ghosts floors ABOVE you and never touches these). Every mode "
    "covers everything on a lower floor — tiles and characters alike — and "
    "deepens per story: the soldier one floor down reads muted, the one two "
    "floors down more so, while the camera-floor soldier stays untouched. "
    "Off keeps just the fade.")

DEPTH = [
    ("depth_fog", "Depth fog — the default",
     "Aerial haze plus slow-drifting fog patches pooling over the exposed "
     "lower floors — the banks visibly crawl while everything else holds "
     "still. The drift is deterministic value-noise on the render-only "
     "effects clock (a different seed and scale than the sky clouds, so the "
     "depths never mirror the weather). This is what shipping players see: "
     "fog is what depth_fx means when the setting was never touched."),
    ("depth_haze", "Depth haze",
     "Pure aerial perspective: each story down blends further toward a pale "
     "steel grey, washing out contrast the way distance does. Static by "
     "construction — no animation, just depth you can read at a glance."),
    ("depth_mist", "Depth mist",
     "A checkerboard dither of the haze color instead of any alpha blend — "
     "every pixel is either the floor's own color or the exact mist entry, "
     "period-correct with zero requantization. The weave tightens per "
     "story: sparse one floor down, full checkerboard two floors down."),
    ("depth_tint", "Depth tint — the legacy look",
     "The original cold blue-grey blend, preserved byte-for-byte from when "
     "depth_tint was its own toggle: lower floors chill toward slate, "
     "colder per story. Pick it if you liked the old look."),
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
        ("War of the Westlands (the campaign)", WESTLANDS_INTRO,
         cards_for(WESTLANDS)),
        ("The Long Season (an original campaign)", LONGSEASON_INTRO,
         cards_for(LONGSEASON)),
        ("Multiplayer (live simulation)", '', cards_for(MULTI)),
        ("Menu system", '', cards_for(MENUS)),
        ("Actual gameplay (live simulation, campaign levels)", '',
         cards_for(GAMEPLAY)),
        ("Effect close-ups (real renderer, scripted scenes)", '',
         cards_for(SCENES)),
        ("Depth effects (multifloor, one setting — four looks)", DEPTH_INTRO,
         cards_for(DEPTH)),
    ]
    section_html = ''.join(
        f'\n<h2 style="text-align:center;color:#f0c060;font-weight:normal;'
        f'margin-top:2.5rem;">{title}</h2>'
        + (f'\n<p class="intro">{intro}</p>' if intro else '')
        + f'\n<div class="grid">{cards}</div>'
        for title, intro, cards in sections if cards)

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
  .intro {{ max-width:900px; margin:0 auto 1.5rem; color:#b8b5aa; font-size:.95rem; line-height:1.5; text-align:center; }}
  .grid {{ display:grid; grid-template-columns:repeat(auto-fill,minmax(680px,1fr)); gap:2rem; max-width:1500px; margin:0 auto; }}
  .card {{ background:#1d2026; border:1px solid #2c3038; border-radius:8px; padding:1rem; }}
  .card img {{ width:640px; max-width:100%; image-rendering:pixelated; border-radius:4px; display:block; margin:0 auto; }}
  .card h2 {{ font-size:1.1rem; margin:.8rem 0 .3rem; color:#f0c060; font-weight:normal; }}
  .card p {{ font-size:.92rem; line-height:1.45; color:#b8b5aa; margin:0; }}
</style>
</head>
<body>
<h1>OpenGlad &mdash; Multifloor Special Effects Review</h1>
<p style="text-align:center;margin:0.2rem 0 0.8rem;">
  <a href="play/play.html" style="display:inline-block;background:#f0c060;color:#14161a;
     font-weight:bold;padding:0.55rem 1.6rem;border-radius:6px;text-decoration:none;
     letter-spacing:.05em;">&#9654;&nbsp;PLAY THIS BUILD IN YOUR BROWSER</a>
</p>
<p class="sub" style="margin-top:0;">The Play link appears when a WebAssembly build
(scripts/build_web.sh) has been copied to <code>site/play/</code>; it runs this exact
branch, campaigns included.</p>
<p class="sub">All frames come from the real engine at 320&times;200 (upscaled 2&times;) with live palette cycling. All animations loop.</p>
{section_html}
</body>
</html>'''
    with open(os.path.join(site, "index.html"), "w") as f:
        f.write(html)
    print("site written:", os.path.join(site, "index.html"))


if __name__ == '__main__':
    main()
