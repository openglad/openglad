# Level 14 — The Long Toll (WINTER, hold the pass)

- id: 14 (grid_file scen0014)
- title: "The Long Toll" (13 bytes)
- type_bits: 0 — classic kill-all + walk-to-exit. NO SAVE_ALL here: the
  garrison are unnamed team-0 posts, so no protected-bit is set anywhere
  (protected-bit plan: none; the first bit2 walker of winter is the Reeve
  in 15).
- floors: 1 (no PIX_AIR anywhere; fall-line rule trivially satisfied)
- grid: 60 x 60
- par_value: 5
- time_bonus_limit: 6000
- weather: FORCED SNOW — the map is snow-filled wall to wall (~2000+ snow
  tiles, far over the >=40 threshold), so the post-roll override always
  lands WeatherKind::Snow. This is the task's explicit "snow + Snow
  weather" requirement for the winter act opener.

## STORY POSITION / CALLBACK CONTRACT

The Grey Tolls pass, the same fort as optional level 7 — IN WINTER. Level
7 is optional, so the briefing must read true whether or not the crew
garrisoned it in summer (see BRIEFING: "Some of us drew summer pay here;
the rest heard the story"). The map does NOT need to byte-match 7's fort
(winter re-dress: drifted walls, frozen tarn, buried outworks), but it
keeps the landmark grammar so returning players recognize it: a walled
gatehouse ASTRIDE the road, west and east gates, the toll strongroom in
the northeast corner. COORDINATION NOTE for the level-7 designer: keep
those three landmarks in 7 and the callback lands for free.

Warm-coin thread: the toll chest is entirely warm coin this year — the
briefing says so, and the strongroom's gold bars are the physical prop.

## TERRAIN PLAN (floor 0, the only floor)

Concept: an east-west pass corridor between two cliff bands, with the
toll fort straddling the road at center. The crew holds the fort;
toll-breakers come from both mouths.

Paint order (before smooth_world):
1. Snow fill: paint_snow(0, 0, 59, 59) — the shared helper cloned from
   westlands_mapgen (deterministic (x*7+y*13)%2 picks PIX_SNOW1/PIX_SNOW2,
   genre-inert painted variants).
2. North cliff band: paint_rect(0, 12, 59, 20, PIX_WALL2).
3. South cliff band: paint_rect(0, 39, 59, 47, PIX_WALL2).
   (Pass corridor = y21..38, 18 tiles tall, open snow. The snow above
   y12 and below y47 is scenic mountainside — NOTHING is placed there.)
4. NW gully (carve through the north band, then the pocket):
   gap paint_snow(8, 12, 11, 20); pocket paint_snow(4, 4, 14, 11).
5. Fort walls: paint_rect(24, 22, 37, 37, PIX_WALL2); carve the
   courtyard paint_snow(26, 24, 35, 35); carve the WEST gate
   paint_snow(24, 28, 25, 31) and the EAST gate paint_snow(36, 28, 37, 31)
   (2-thick walls, 4-wide gates — a 2x2 crew passes a 2x2 edge guard).
6. Toll strongroom (NE corner of the courtyard): wall stubs
   paint_rect(32, 24, 32, 26, PIX_WALL2) and
   paint_rect(33, 26, 34, 26, PIX_WALL2); the nook (33..35, 24..25)
   opens only through the gap at (35, 26).
7. Frozen tarn (black ice choking the east approach south side):
   paint_rect(44, 33, 52, 37, PIX_WATER1) — impassable; the east road
   y28..31 stays clear.
8. Pine clumps (PIX_TREE_M1): (2,22)-(6,26) west mouth north side;
   (50,22)-(55,26) east mouth north side.
9. Scrub through the snow (PIX_GRASS_DARK_1): (14,34)-(18,37),
   (40,22)-(43,25).

smooth_world(w), then post-smooth (autotiler-inert):
10. Courtyard pavement: paint_pavement(26, 24, 35, 35) (under the wall
    stubs' shadow too — pavement first, stubs were painted pre-smooth so
    re-assert them if the order needs it; the concept builders paint
    decor AFTER smoothing, stubs BEFORE).
11. The toll road (paint_path): west approach paint_path(1, 29, 23, 30);
    through the courtyard paint_path(26, 29, 35, 30); east approach
    paint_path(38, 29, 58, 30).
12. Gate torches: paint(24, 27, PIX_TORCH1), paint(24, 32, PIX_TORCH1),
    paint(37, 27, PIX_TORCH1), paint(37, 32, PIX_TORCH1).
13. Courtyard braziers (the winter watch's fires): paint(27, 25,
    PIX_BRAZIER1), paint(27, 34, PIX_BRAZIER1), paint(34, 34,
    PIX_BRAZIER1).

## DECOR AMBIENCE (decor plane, all non-blocking)

- DECOR_PEBBLES worn through the snow on the road: Path tiles only,
  whole map rect (0,0)-(59,59) mod 13.
- DECOR_BONES, the pass takes its dead: hand-set (6,31), (18,27) west
  approach; (42,31), (54,27) east approach; (9,10) gully pocket.
- DECOR_BOULDER scatter on the open pass floor: scatter_boulders
  (0, 0,21, 23,38, 23) and (0, 38,21, 59,38, 23) — outside the fort only.
- NO DECOR_SHRUB anywhere near the road (battle lanes stay honest); two
  accents in the scrub patches at (15,35), (41,23) are off every lane.

## ARMIES

Team 0 — the winter watch (unnamed garrison; guard via npc_flags bit1):

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|
| 1 | 0 | SOLDIER (gate watch) | 4 | 6 | YES | (26,28),(26,31) west gate; (35,28),(35,31) east gate | 0 | no |
| 2 | 0 | ARCHER (wall watch) | 2 | 5 | YES | courtyard corners (27,26),(34,33) | 0 | no |

Team 2 — wolves first, then the toll-breakers in four waves:

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|
| 3 | 2 | ORC (wolf strays) | 6 | 4 | no | pass floor: (12,24),(16,35),(20,26),(42,25),(48,28),(54,35) | 0 | no |
| 4 | 2 | GHOST (the frozen watch) | 2 | 4 | no | gully pocket: (6,6),(12,9) | 0 | no |
| 5 | 2 | SOLDIER (west wave 1) | 5 | 5 | no | west mouth: (1,26),(1,29),(2,27),(2,31),(3,29) | 300 | no |
| 6 | 2 | ARCHER (west wave 1) | 2 | 5 | no | (1,32),(3,25) | 300 | no |
| 7 | 2 | BARBARIAN (east wave 1) | 4 | 5 | no | east mouth: (57,26),(57,29),(58,27),(58,31) | 700 | no |
| 8 | 2 | ORC (east wave 1) | 2 | 4 | no | (56,24),(58,24) | 700 | no |
| 9 | 2 | SOLDIER (west wave 2) | 5 | 6 | no | west mouth: (1,24),(2,25),(1,31),(2,33),(3,31) | 1200 | no |
| 10 | 2 | ARCHER (west wave 2) | 2 | 6 | no | (3,27),(3,33) | 1200 | no |
| 11 | 2 | BARBARIAN (the caravan master, unnamed) | 1 | 8 | no | (58,29) | 1800 | no |
| 12 | 2 | BARBARIAN (east wave 2) | 4 | 6 | no | (56,27),(56,31),(57,33),(58,33) | 1800 | no |
| 13 | 2 | BIG_ORC (dire wolves) | 2 | 6 | no | (55,29),(55,31) | 1800 | no |

Generators:

| family | team | level | cell (floor 0) | note |
|--------|------|-------|----------------|------|
| BONES (ghosts — the frozen watch rises) | 2 | 3 | (9,7) gully pocket | LOW level: the post-obmap-fix rule (a lvl-5+ generator outbreeds a curve crew); destroying it is the kill-all's last errand |

Totals: team 0 = 6 livings; team 2 = 35 livings + 1 generator.
Delayed spawns: 27 (waves at 300/700/1200/1800 — dormant, NEXT WAVE HUD,
they hold level_done open so the hold cannot end early). MAXOBS ledger:
41 livings + 1 gen + 10 markers + 2 exits + 6 treasures = 60 objects;
authored livings 41 << 120 budget, generator output has ample headroom.

Every wave spawns at a pass MOUTH (x1..3 / x55..58), far from the crew's
courtyard deploy — dormant walkers are untargetable and wake with a
flash, so no wake-on-top-of-crew shocks.

## HEROES / NAMED

None placed. Kettle appears in 4/9/18 per the skeleton cast list; the
Sergeant narrates and is never placed. (No names = nothing for SAVE_ALL
to watch even if the type bit were misread; the bit is 0 anyway.)

## START MARKERS (10; lead FIRST; all on courtyard pavement, 2x2 clearance, clear of guards/stubs/braziers)

1. (29, 30) — LEAD, mid-courtyard on the road line
2. (27, 28)  3. (32, 28)  4. (27, 32)  5. (32, 32)
6. (29, 26)  7. (29, 33)  8. (33, 30)  9. (27, 30)  10. (33, 32)

(Strongroom cells x32..35 / y24..26 and guard posts x26/x35 avoided;
each marker has 2x2 shoulder room on open pavement.)

## TREASURE

- The toll chest (strongroom nook): GOLD_BAR (33,25), GOLD_BAR (34,25),
  GOLD_BAR (34,24) — the year's takings, warm to the touch.
- Watch stores (courtyard): DRUMSTICK (28,25), DRUMSTICK (33,34),
  MAGIC_POTION (30,25).

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57, 34) | 15 — east mouth, the road down to Thornby (clear of the tarn x44..52 and of east wave cells y26..33... wave cells are y24..33 at x55..58; (57,34) sits one row south of the lowest wave cell (58,33) — no footprint overlap) |
| 0 | (2, 36)  | 14->13 backtrack — west mouth south shoulder, the switchbacks back to the Smelter's Road (west wave cells are y24..33 at x1..3; (2,36) is 3 rows clear) |

Reachability: lead (29,30) -> west gate -> west mouth is a flat road
walk; east gate -> east mouth likewise; the gully generator + ghosts are
reached through the carved gap at (8..11, 12..20). Self-check reachability
allowlist: EMPTY (every living + the generator is A*-reachable from the
lead marker; nothing is placed on the scenic mountains or in the tarn).

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, winter work. The Grey     (29)
Tolls want holding through the    (30)
pass trade. Some of us drew       (27)
summer pay here; the rest heard   (31)
the story. Toll chest is warm     (29)
coin to the last penny. Hold.     (29)
```

Line 5-6 carry the act's warm-coin thread: EVERY toll paid this year was
struck from the same warm metal — the chest in the strongroom is the
proof the crew can stand on.

## BALANCE NOTES / CALIBRATION GATE (curve position: crew 7)

Curve: the campaign starts at crew 1 (levels 1-2 gate at crew 1); a
mainline crew enters winter at 7 (the meta table's rung, off 13's crew
6). This level is tuned as a DEFENSE at crew 7 — bracket sweep at
{6, 7, 8} x seeds {42, 1337, 2025} x rosters {4-soldier A, 8-mixed B}.

- Battle shape: wolf strays probe first (ticks 0-300), then alternating
  mouth waves at 300/700/1200/1800 — never both mouths in the same
  breath until 1800, when the caravan master's east push and nothing
  else remains. The fort's two 4-wide gates are the only doors; the
  garrison's 6 posts anchor them but cannot win alone.
- DEFENSE GATE (the Westlands F4 defense form): team-0 alive (garrison +
  crew) at tick 3000 >= 5 on 3/3 seeds with the 8-mixed roster at crew 7
  (band = ceil(initial_t0/3) with initial_t0 = 6 placed + 8 crew = 14).
  The 4-soldier roster is recorded as the pessimistic floor and may sag
  to 3.
- KILL-ALL COMPLETION (type 0 + exits present -> someone must walk out):
  the harness never exits; read the foe-decay curve, not game_ended.
  Expect t2 35 -> <=8 by tick 4500 at curve (the survivors are usually
  the gully ghosts + generator trickle). The lvl-3 BONES generator must
  be killable by a curve crew in one push through the gap.
- Wave sanity: no wave arrives after the battle is decided — 1800 is the
  last wake and the sweeps must show t2 engagement (not a parked wave)
  by tick 2200 on every seed.
- Structural watches: gate wedges (two 2x2 guards + a wave in a 4-wide
  gate — the F1 standoff fix covers the melee, but watch for new
  shapes); generator flooding (lvl 3 cap is the guard-rail); the tarn
  must never trap an east-wave pather (waves spawn north of it and the
  road is clear).
- Weather smoke (unit test): loaded grid >= 40 snow tiles; post-roll
  override reports WeatherKind::Snow.
- Pins to add (test_longseason_levels.cpp): roster row {14, t0 6, t2 35,
  gens 1, markers 10, exits {15, 13}}, delayed-spawn count 27, briefing
  6 lines, strongroom gold present, aligned-stairs N/A (1 floor).
