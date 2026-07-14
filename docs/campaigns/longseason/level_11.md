# Level 11 — Cold Seams  (the deep mine; the metal is wrong)

The Foundry's deep mine. The dead miners never clocked out; things of fire
nest where the seams run WARM — the campaign's first physical hint that the
coin's metal is wrong. The west wall of the deep workings has broken into
old built vaults (the branch to optional 12).

- id: 11 (grid_file scen0011)
- title: "Cold Seams" (10 bytes)
- type_bits: 0 (kill-all, then walk to an exit). PROTECTED-BIT PLAN: no
  npc_flags bit2 anywhere; no team-0 NPCs at all on this level — the
  company goes down alone.
- floors: 2 (floor 1 = the upper gallery / adit level, ENTRY floor;
  floor 0 = the deep seams with the lava-vein visuals)
- grid: 62x48
- par_value: 4
- time_bonus_limit: 4500

Entered from 10 (the ore-track). Curve position: crew level 5.

## TERRAIN PLAN

### Floor 1 — the upper gallery (entry)
1. Fill: paint_rect(0,0,61,47, PIX_WALL2) — solid rock.
2. Carve PIX_DIRT_DARK_1 (pre-smooth):
   - adit mouth + entry hall (4,20)-(14,28)
   - main gallery, east-west (15,22)-(46,26)
   - north timber hall (24,8)-(40,18), corridor (28,19)-(31,21)
   - south store rooms (20,27)-(32,34) (open stope off the gallery)
   - east winch room (47,18)-(58,30)
3. smooth_world(w).

### Floor 0 — the deep seams
1. Fill: paint_rect(0,0,61,47, PIX_WALL2).
2. Carve PIX_DIRT_DARK_1:
   - the deep crossing (24,18)-(40,30) (central)
   - west drift (8,20)-(23,25)
   - the VAULT BREACH room (4,26)-(12,36) (adjacent to the drift at
     rows 25/26, cols 8-12)
   - north seam gallery (26,6)-(48,17)
   - east seam (41,20)-(56,30)
   - south seam (20,31)-(44,40)
   - shaft room (50,32)-(58,42), link carve (52,31)-(53,31)
3. smooth_world already ran per-floor via the shared call; then
4. WARM SEAMS — lava veins (PIX_LAVA1, POST-smooth impassable ribbons;
   solid to ground walkers, flyers/projectiles cross; each leaves a
   trodden gap on the required route):
   - vein A (34,6)-(35,13), north seam gallery — walk lanes y14-17 open
   - vein B (46,22)-(47,28), east seam — gap rows y20-21 top and
     y29-30 bottom
   - vein C (28,36)-(41,37), south seam — lanes y31-35 above; below via
     x20-27 west and x42-44 east
   - vein D (53,40)-(56,42), shaft room SE corner (decorative glow,
     severs nothing; the forward exit sits at (56,34), clear of it)

### Stairs (aligned pairs; the cell is carved on BOTH floors)
- stair_pair(w, 0, 28, 24) — the main winze: deep crossing (f0) up to the
  main gallery (f1).
- stair_pair(w, 0, 50, 28) — the winch shaft: east seam (f0) up to the
  winch room (f1).

### Decor ambience (no shrubs underground)
- DECOR_TORCH1: f1 entry hall mouth (5,21),(5,27); winch room (48,19),
  (57,19); f1 stairheads (27,23),(49,27) shoulder cells; f0 stair
  landings (30,25),(52,27).
- DECOR_PEBBLES mod 9 over all carved dirt, both floors (DirtDark class).
- DECOR_BONES mod 6 over the dead-miner workings: f0 north seam gallery
  (26,6)-(48,17) and the deep crossing (24,18)-(40,30); f1 timber hall
  (24,8)-(40,18).
- scatter_litter(w,0, 20,31, 44,40, 47) — sparse jagged spoil in the
  south seam (modulus high; blocks even shots, kept off the y31-35 lane
  by the scatter's entity/route clearance check at gen time).

## ARMIES (team 2 = the mine's dead + the warm nests; 39 livings, 1 gen)

### Floor 1 — the upper gallery
| group | family | count | level | guard | placement | spawn_delay |
|-------|--------|-------|-------|-------|-----------|-------------|
| adit watch (roam) | SKELETON | 3 | 4 | no | (17,23),(19,25),(21,23) | 0 |
| timber hall dead (roam) | SKELETON | 2 | 4 | no | (26,10),(36,15) | 0 |
| timber hall dead (roam) | SKELETON | 2 | 5 | no | (30,12),(33,10) | 0 |
| hall light (posted) | GHOST | 1 | 5 | YES | (32,16) | 0 |
| store seep (roam) | SLIME | 3 | 4 | no | (22,29),(27,32),(31,29) | 0 |
| winch wards (posted) | SKELETON | 3 | 5 | YES | (50,20),(54,24),(50,26) | 0 |
| winch light (posted) | GHOST | 1 | 5 | YES | (55,28) | 0 |

Generators:
| family | team | level | floor,cell | note |
|--------|------|-------|------------|------|
| BONES | 2 | 3 | f1 (25,9) | 3x3 footprint (25-27, 9-11) inside the timber hall; LOW level per the generator-flood lesson — a trickle, not a stream |

### Floor 0 — the deep seams
| group | family | count | level | guard | placement | spawn_delay |
|-------|--------|-------|-------|-------|-----------|-------------|
| dead miners (roam) | SKELETON | 8 | 5 | no | (26,20),(34,22),(38,28),(30,14),(44,10),(48,24),(30,34),(38,39) | 0 |
| vein riders (flyers posted ON lava) | GHOST | 4 | 5 | YES | (34,8) vein A, (46,25) vein B, (33,36) vein C, (54,41) vein D | 0 |
| warm nests (posted at vein gaps) | FIREELEMENTAL | 2 | 5 | YES | (37,15),(43,21) | 0 |
| warm nests (posted) | FIREELEMENTAL | 2 | 6 | YES | (26,38),(52,36) | 0 |
| breach warden (wandered through) | GIANT_SKELETON | 1 | 7 | YES | (8,31) — 4x4 footprint (8-11, 31-34) inside the breach room; foreshadows 12 | 0 |
| breach watch (posted) | SKELETON | 3 | 5 | YES | (6,28),(11,27),(10,35) | 0 |
| the seams wake (dormant, posted) | FIREELEMENTAL | 2 | 5 | YES | (30,8),(24,36) | 700 |
| the seams wake (dormant, posted) | GHOST | 2 | 6 | YES | (40,12),(36,32) | 700 |

Totals: f1 15 + f0 24 = 39 team-2 livings + 1 BONES generator (lvl 3);
0 team-0; 4 delayed spawns (NEXT WAVE HUD at tick 700).
MAXOBS ledger: 39 livings + 1 gen + 10 markers + 21 treasures + 3 exits
= 74 objects; ~76 headroom covers the generator's trickle.

## TREASURE

- Ore-glints (the warm metal, in situ): SILVER_BAR x8 — f0 (28,7),(44,7),
  (44,15),(54,22),(54,29),(22,33),(43,39),(28,29).
- The shift-boss's pay chest: GOLD_BAR x4 — f1 winch room (56,21),(57,22);
  f0 shaft room (57,33),(57,35).
- DRUMSTICK x6 (the miners' cache): f1 stores (21,28),(24,33),(30,33);
  f0 crossing (25,19),(39,19); f0 west drift (10,21).
- MAGIC_POTION x2: f1 timber hall (39,9); f0 south seam (21,39).
- FLIGHT_POTION x1: f0 (42,33) — by vein C; fly the veins, once.

## HEROES / NAMED

None. No team-0 placements; the named cast is elsewhere this act.

## START MARKERS (10, floor 1 entry hall; lead FIRST, 2x2 clearance)

lead (13,24) — at the hall's east mouth facing the main gallery; then
(11,21),(11,24),(11,27),(9,21),(9,24),(9,27),(7,21),(7,24),(7,27).
Backtrack exit (4,20) stays clear of every footprint.

## EXITS (the briefing names the branch)

| floor | cell    | destination |
|-------|---------|-------------|
| 1 | (4,20)  | 10 (backtrack: the adit out to the Undermill track) |
| 0 | (5,34)  | 12 (OPTIONAL: through the breach, into the Old Count's vault) |
| 0 | (56,34) | 13 (main: the ore shaft down to the Smelter's Road) |

Walkable route (lead -> 13): (13,24) → main gallery → winze stair (28,24)
down → deep crossing → east seam via x41 band, around vein B through the
y20-21 gap → link (52,31)-(53,31) → shaft room → (56,34). Branch route:
crossing → west drift → breach room → (5,34), past the warden. All livings
and the generator A*-reachable from the lead marker (empty allowlist);
vein-rider ghosts are flyers and exempt from the ground footing audit.

## BRIEFING (6 lines; char counts 27/27/27/30/29/27)

```
Ledger, week two below. The
Foundry's dead miners never
left. Sweep the seams. They
are warm to the touch, same as
the coin. The west wall broke
into old vaults. Your call.
```

(Warm-coin thread: lines 4-5 — seams warm as the pay. Branch named in
fiction: "old vaults. Your call." = the 12 exit.)

## BALANCE NOTES

Battle shape: floor 1 is the approach act (skeleton pickets, a low BONES
trickle to snuff in the timber hall, store slimes), floor 0 the real sweep:
roaming dead miners meet the crew in the crossing, fire elementals hold the
vein gaps the routes must thread (first "the metal is wrong" beat delivered
as a fight), vein ghosts harass from over the lava where melee can't answer
— archers/mages earn their pay. The breach warden (lvl-7 giant) is the
level's mini-boss and the signpost for the optional vault. Tick-700 wake
(2 fire + 2 ghost, posted) punishes camping the crossing.

Calibration gate (kill-all; curve crew 5):
- 8-mixed roster clears within 6000 ticks >=2/3 seeds at crew 5, 3/3 at
  crew 6; crew 4 may fail;
- the BONES generator dies in every clearing run (it is on the required
  sweep and lvl 3 cannot outbreed a curve crew);
- neither side extinct before tick 150; dormant census 4 at tick 0;
- structural: 2 aligned stair pairs; exit destinations {10, 12, 13} all in
  package; footing audit (giant's 4x4 inside the breach carve; ghosts on
  lava exempt as flyers); fall-line audit trivial (no PIX_AIR authored);
  vein gaps keep A*-reachability with the empty allowlist.
