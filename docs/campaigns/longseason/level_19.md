# Level 19 — "Settlement Day" (EPILOGUE — full circle, loops to 1)

- id: 19
- title: `Settlement Day` (14 bytes, <=30 OK)
- type_bits: **0** (kill-all, then walk to the winter-quarters exit).
  PROTECTED-BIT PLAN: none — no named NPCs are placed, so no bit2 anywhere.
- floors: 1
- grid: **44 x 30** (x 0..43, y 0..29) — deliberately tiny; dessert
- par_value: 2
- time_bonus_limit: 2500

Fiction: back at the Weycombe dikes where the year began (level 1 "Mud
Pay"). Rich or broke, the company clears its OWN dike one last time —
the granary job again, on the house — then walks into winter quarters.
The exit loops to level 1: next spring, the ledger opens again.

FULL-CIRCLE NOTE for the level-1 designer: the callback is THEMATIC, not
byte-level. This map is an independent tiny dike scene (level 1's map is
authored concurrently and larger); if a shared `paint_weycombe_motifs`
helper turns out practical (granary shell + dike path + marsh belt), take
it, but do not block either level on it. The briefing's "first thaw
again" line is the load-bearing callback.

## TERRAIN PLAN (floor 0 only)

init_world(level, 1, 44, 30) — base fill grass.

Pre-smooth:
1. North treeline: paint_rect(0, 0, 10, 2, PIX_TREE_M1).
2. Floodwater pool (the winter melt): paint_rect(6, 24, 12, 28, PIX_WATER1).
3. The granary: paint_rect(18, 4, 26, 10, PIX_WALL2).
4. The winter quarters (the company's house), east:
   paint_rect(34, 8, 40, 13, PIX_WALL2).

smooth_world(w), then post-smooth:
5. Interiors + doors (pavement over wall makes the doorway):
   - granary: paint_pavement(19, 5, 25, 9); door paint_pavement(22, 10, 22, 10)
   - house: paint_pavement(35, 9, 39, 12); door paint_pavement(37, 13, 37, 13)
6. The dike (a worn embankment path, west-east):
   paint_path(2, 18, 41, 19).
7. Lanes: granary lane paint_path(22, 11, 22, 17); house lane
   paint_path(37, 14, 37, 17).
8. The marsh belt (paint_marsh checker, PIX_MARSH1/2 — walkable bog),
   south of the dike, parted around the pool:
   - paint_marsh(0, 22, 43, 23) — the long fringe under the dike
   - paint_marsh(0, 24, 5, 29) — west of the pool
   - paint_marsh(13, 24, 43, 29) — east of the pool

Decor (after armies; blocking decor off the dike, lanes, and doors):
9. Door torches (DECOR_TORCH1): granary (21,11),(23,11); house
   (36,14),(38,14) — flanking the lanes, never on them.
10. A single homecoming brazier by the house door: DECOR_BRAZIER (40,14)
    — lit for the crew's return.
11. Ambience (scatter_decor, non-blocking):
    - marsh bones `(0,22)-(43,29)` mod 17 {Marsh} — what the flood left
    - dike pebbles `(2,16)-(41,21)` mod 9 {Path}
    - hedgerow shrubs under the treeline `(0,3)-(12,8)` mod 13 {Grass}
    - scatter_boulders(w, 0, 12, 0, 43, 3, 27) — sparse north-field stones

## ARMIES (team 2 — what the flood left, one last time)

16 livings, 0 generators. All floor 0.

| # | team | family | count | level | guard | placement | delay | spec.dis |
|---|------|--------|-------|-------|-------|-----------|-------|----------|
| 1 | 2 | MEDIUM_SLIME | 4 | 3 | no | the marsh: (6,22),(16,25),(26,23),(34,26) | 0 | no |
| 2 | 2 | ORC (the dike vermin, as in level 1) | 6 | 2+i%2 | no | on and under the dike: (12,18),(16,19),(20,18),(28,18),(24,15),(32,19) | 0 | no |
| 3 | 2 | ORC | 2 | 3 | YES | inside the granary, gnawing the grain: (20,7),(24,7) | 0 | no |
| 4 | 2 | MEDIUM_SLIME (the marsh gives one more push at dusk) | 4 | 3 | no | marsh edge: (14,26),(22,26),(30,26),(38,25) | 300 | no |

Footing: slimes on marsh (walkable), orcs on path/grass/pavement; nothing
on water, walls, or trees. Every 2x2 footprint pairwise clear of markers,
treasure, and doors.

## HEROES / NAMED

None placed — deliberately. Kettle's placements are 4/9/18 (skeleton cast
list) and the epilogue belongs to the crew alone; the briefing keeps the
company voices present. No SAVE_ALL, nothing protected, nothing to lose
but the afternoon.

## START MARKERS (8; lead FIRST; 2x2 anchors with shoulder room)

The crew walks in from the west road, on the dike:
1. **(4, 18)** — LEAD, on the dike path
2. (2, 15)   3. (2, 21)
4. (6, 15)   5. (6, 21)
6. (8, 18)   7. (4, 14)
8. (4, 21)

Nearest enemy is the orc at (12,18) — 4 tiles east of the lead's
footprint edge; the fight starts on the dike, as it did in spring.

## TREASURE

- DRUMSTICK (21,6),(23,8) — the granary, stocked this time
- GOLD_BAR (35,10),(39,11) — the season's pay on the winter-quarters
  table (what survived the mint)
- MAGIC_POTION (13,26) — the pool fringe cache, where Weycombe always
  keeps one

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (37, 10) | **1** — the winter quarters; the ledger closes, and next spring it opens on Mud Pay again (the campaign loop) |

The exit sits INSIDE the house (interior 35..39 x 9..12, door at 37,13),
between the pay bars — collecting the season and ending it are the same
walk. NO backtrack exit, deliberately: the Warm Mint is burning behind
the company and the skeleton graph ends the year here; the loop-to-1 exit
is the only door (westlands-26 precedent for a terminal loop level).

Walkable route: lead (4,18) → dike y18-19 → house lane (37,14..17) →
door (37,13) → (37,10). Granary loop via lane (22,11..17) and door
(22,10). Marsh is walkable everywhere the slimes sit.

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, first thaw again. Home.   (31)
The season paid, the book square. (33)
One warm coin nailed over the     (29)
door, so we remember the year.    (30)
The dike crawls. One last job,    (30)
on the house. Then winter beer.   (31)
```

Warm-coin thread: lines 3-4 — the mystery's coda: one coin kept, not
spent, nailed up as a warning and a trophy.

## BALANCE NOTES / calibration

Curve position: crew power 8-9 (dessert — the epilogue NEVER spikes; the
curve table pins it as such). Level type: **kill-all**.

Battle shape: a victory lap with a pulse. Six lvl-2/3 dike vermin meet
the crew on the embankment; four marsh slimes ooze up from the south;
at tick 300 four more wake at the marsh edge (a token NEXT WAVE beat so
the HUD's last use is the year's smallest fight). The two granary orcs
are ACT_GUARD door-squatters — the last "boss" of the campaign is two
rats in the grain, on purpose.

Calibration gates (kill-all, dessert rules — stricter than the standard
kill gate because it must never wall anyone off from the loop):
- 8-mixed clears (level_done==1 within 6000 ticks) 3/3 seeds at crew 9,
  3/3 at crew 8, AND 3/3 at crew 7 (curve-2: even a mauled crew limping
  out of the mint finishes the year).
- 4-soldier floor roster clears 3/3 at crew 8 (westlands-25/26 precedent:
  dessert levels clear on the pessimistic floor too).
- Expected clear ticks 400-1200; if any seed runs past 2500, the marsh
  slime wave placement has a pathing dead zone — fix geometry, not levels.

300-tick smoke (stand-in crew 4x lvl 8 on markers 1-4, 3 seeds):
- Team 0 not extinct (trivially); team-2 alive <= 8 by tick 300 (the
  dike sweep is fast).
- The 2 granary guards alive at 300 (guard, indoors — the beat waits for
  the player's walk-in, as with the westlands-25 chief).
- Delayed round-trip: 4 dormant at 300 boundary (wake tick 300 —
  assert dormant at tick 299 / awake by 350), holding level_done open.
- Static pins: floors=1, grid 44x30, 8 markers, team2 livings 16 (exact
  — small enough to pin without tolerance), 0 generators, exit dest {1},
  footing audit clean, no PIX_AIR anywhere.
- Recipe C: A* lead (4,18) → exit (37,10) non-empty; every living
  reachable from the lead marker (empty allowlist — granary and house
  doors are carved, marsh is walkable).
- Weather: 0 snow tiles — spring thaw; the Snow roll must stay possible=
  no (assert the forced-snow threshold is not tripped).
