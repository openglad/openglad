# Level 4 — The Assessor (HUB; the campaign's FIRST protect job)

- id: 4
- title: "The Assessor" (12 bytes)
- type_bits: SCEN_TYPE_SAVE_ALL (4) — kill-all win stays; the protect
  scope is pinned by npc_flags bit2 (see PROTECTED-BIT PLAN).
- floors: 1
- grid: 80 x 45
- par_value: 4
- time_bonus_limit: 5000

Escort the crown assessor through bandit country. His wagon has thrown
an axle at the barrow-road waystation; the company deploys around it,
breaks the ambush springs, and clears the road. He pays in the warm
coin and won't say whose stamp it is. HUB EXIT CHOICE: east to the
summer war roads (5) or south to the Saltmere jetty (3 — the optional
dive, take it now or never mind). Gates at CREW 2.

## PROTECTED-BIT PLAN (the SAVE_ALL scoping rule, applied exactly)

- "Assessor" carries npc_flags bit2 (protected) — reserved[3] = 6
  (bit1 guard | bit2 protected). He is the ONLY bit2 walker on the map.
- Because at least one placed NPC has bit2, the engine watches ONLY
  bit2 walkers: Kettle (named, team 0) is deliberately NOT bit2 —
  protect-OPTIONAL per the cast sheet; his death stings the ledger,
  not the mission. The unnamed door-wards and any summons never count.
- Unit-name budget: the .fss name field holds 11 chars; "The Assessor"
  is 12 and would overflow (the Westlands "White Rider" precedent), so
  the PLACED unit is named "Assessor" (8 chars). Briefings say "the
  crown assessor" — the 33-char lines can afford the article.
- No flyers anywhere on this map BY DESIGN (bandits + a TENT generator,
  which spawns ground skeletons): the ghost scare-wail hazard that
  forced Westlands' raw-rock redoubts does not exist here. A walled
  waystation + lvl-4 door-wards is sufficient protection at crew 2.

## TERRAIN PLAN (floor 0 only)

init_world(level, 1, 80, 45).

Pre-smooth paints:
1. West woods band with the road gap: paint_rect(0, 0, 5, 44,
   PIX_TREE_M1), then carve the gap paint_rect(0, 20, 5, 24, PIX_GRASS1)
2. NE copse: paint_rect(70, 0, 79, 6, PIX_TREE_M1)
3. The barrow field, north: paint_rect(8, 2, 30, 10, PIX_GRASS_DARK_1);
   barrow mounds paint_rect(12, 4, 15, 7, PIX_DIRT_DARK_1);
   paint_rect(20, 3, 23, 6, PIX_DIRT_DARK_1);
   paint_rect(26, 5, 29, 8, PIX_DIRT_DARK_1)
4. The south gorse: paint_rect(20, 34, 48, 42, PIX_GRASS_DARK_1)
5. The swollen stream, north-south (spring floods, still up):
   paint_rect(58, 0, 62, 44, PIX_WATER1)
6. The waystation, solid shell: paint_rect(36, 16, 50, 26, PIX_WALL2)

smooth_world(w), then post-smooth:
7. Waystation interior (pavement over wall carves it):
   paint_pavement(37, 17, 49, 25)
8. Gates (pavement over wall): west gate paint_pavement(36, 20, 36, 23);
   east gate paint_pavement(50, 20, 50, 23)
9. The barrow road: west segment paint_path(2, 21, 35, 22); east
   segment paint_path(51, 21, 78, 22) — the ford over the stream is the
   path-over-water span (58,21)-(62,22), the only crossing
10. The barrow lane, road to the field: paint_path(20, 7, 21, 20)
11. The south lane, road to the gorse and on to the water meadows
    (the Saltmere spur): paint_path(33, 23, 33, 43)
12. Marsh on the stream banks (marsh_over_grass, plain grass only):
    marsh_over_grass(55, 0, 57, 44); marsh_over_grass(63, 0, 65, 44)
13. The wagon (thrown axle, parked off the gate lane):
    paint(43, 19, PIX_BOULDER_2) — a blocking prop; lane rows 20-23
    stay clear
14. Waystation braziers: paint(38, 17, PIX_BRAZIER1);
    paint(49, 17, PIX_BRAZIER1)
15. Gate torches (outside the walls, off the road rows 21-22):
    paint(35, 19, PIX_TORCH1); paint(35, 24, PIX_TORCH1);
    paint(51, 19, PIX_TORCH1); paint(51, 24, PIX_TORCH1)

Decor ambience (non-blocking, after entity placement):
16. DECOR_BONES on the barrow field: (8,2)-(30,10) mod 7
    (DarkGrass+Dirt) — the diggings
17. DECOR_SHRUB on the gorse: (20,36)-(48,42) mod 9 (DarkGrass) —
    concealment IS the ambush country; kept south of row 35, entirely
    off the road and both lanes' required routes (lane x33 cells are
    Path, skipped by ground-class restriction)
18. DECOR_PEBBLES on road + lanes: (2,21)-(78,22), (20,7)-(21,20),
    (33,23)-(33,43), each mod 11 (Path)
19. scatter_boulders(w, 0, 6, 10, 30, 18, 17) — moor stones between
    field and road; scatter_boulders(w, 0, 64, 28, 76, 40, 19) — east
    bank scree. Road rows 20-24 excluded by both rects.

## ARMIES

### Team 0 — the halted column (guards via npc_flags bit1)

| name | family | level | guard | protected (bit2) | specials_off | placement |
|------|--------|-------|-------|------------------|--------------|-----------|
| "Assessor" (8 ch) | FAMILY_CLERIC | 4 | YES | YES — the only one | YES (he counts coin, he doesn't fight) | (45,17) — interior, north wall, off the gate lane |
| "Kettle" (6 ch) | FAMILY_SOLDIER | 3 | YES | no (protect-optional) | no | (38,24) — inside the west gate, south of the lane |
| unnamed door-wards | FAMILY_SOLDIER x2 | 4 | YES | no | no | (42,17),(47,17) — flanking the Assessor's post |

reserved[3] values: Assessor = 6 (guard+protected); Kettle and wards = 2
(guard). spawn_delay 0 for all team 0.

### Team 2 — bandit country

| # | family | count | level | guard | placement (floor 0) | spawn_delay | specials_off |
|---|--------|-------|-------|-------|---------------------|-------------|--------------|
| 1 | FAMILY_THIEF (road rocks, west) | 3 | 2 | YES | (10,20),(16,23),(24,20) | 0 | no |
| 2 | FAMILY_ARCHER (ford watch) | 2 | 2 | YES | east bank: (63,20),(63,23) | 0 | no |
| 3 | FAMILY_SOLDIER (east road toll) | 3 | 2 | YES | (66,20),(66,23),(70,21) | 0 | no |
| 4 | FAMILY_ORC (bandit dogs) | 4 | 1 | no | gorse: (24,36),(30,38),(38,36),(44,38) | 0 | no |
| 5 | FAMILY_THIEF (gorse camp) | 3 | 2 | YES | (28,40),(34,40),(40,40) — lane x33 stays open | 0 | no |
| 6 | FAMILY_SKELETON (what the digging woke) | 4 | 1 | no | barrow mounds: (13,5),(21,4),(27,6),(17,8) | 0 | no |
| 7 | FAMILY_THIEF (the first spring — from the rocks BEHIND the column) | 4 | 2 | no | west gap mouth: (6,19),(6,24),(8,18),(8,25) | 300 | no |
| 8 | FAMILY_ORC (gorse spring) | 3 | 2 | no | gorse north edge: (30,33),(36,33),(42,33) | 700 | no |
| 9 | FAMILY_BARBARIAN (the toll chief's push) | 3 | 3 | no | across the ford: (74,20),(74,23),(76,22) | 1100 | no |
| 10 | FAMILY_SOLDIER (the push's second rank) | 3 | 2 | no | (72,19),(72,24),(78,24) | 1100 | no |

Generators (the diggers' camp — the coin they dig for is the arc's):
| family | team | level | floor/cell |
|--------|------|-------|------------|
| FAMILY_TENT | 2 | 2 | 0 / (24, 8) — barrow field; spawns ground skeletons ONLY (no flyers, keeps the protect job honest at crew 2) |

Totals: 32 team-2 livings + 4 team-0 placed = 36 livings, 1 generator.
Delayed spawns: 13. Guards: 11 team-2 posts + 4 team-0.
MAXOBS ledger: 36 livings + 1 gen + 10 markers + 8 treasures + 2 exits
= 57 objects + bounded TENT trickle — ample headroom under 150.

Footing + reachability: all anchors on grass/dark-grass/dirt/path/
pavement; nothing on water, woods, walls, or the wagon prop cell. Every
unit + the TENT A*-reachable from the lead marker: west pockets via the
road, barrow group via the lane, gorse via the south lane, east groups
via the ford path span. Empty allowlist.

## START MARKERS (10; lead first — the company deploys INSIDE the
waystation around the wagon; all anchors 2x2-clear pavement)

1. (40, 21)  — LEAD, on the lane inside the west gate
2. (43, 20)  — beside the wagon
3. (40, 24)
4. (46, 21)
5. (43, 24)
6. (46, 24)
7. (40, 18)
8. (48, 20)
9. (37, 21)  — in the west gateway
10. (48, 24)

## TREASURE

- The assessor's strongbox: FAMILY_GOLD_BAR at (44,18),(44,20) — THE
  WARM COIN, paid on the spot (fiction; see briefing)
- FAMILY_SILVER_BAR at (39,17) — Kettle's float
- FAMILY_DRUMSTICK at (45,25),(39,22) — the column's stores
- FAMILY_MAGIC_POTION at (14,4) — the diggers' loot, barrow field
- FAMILY_SILVER_BAR at (34,41) — the gorse camp's take
- FAMILY_DRUMSTICK at (56,21) — a dropped pack at the ford

## EXITS (THE HUB — the briefing names both roads; per the campaign
graph this level exits FORWARD to 5 and BACK to the optional 3 only)

| floor | cell | destination |
|-------|------|-------------|
| 0 | (78, 21) | 5 — east road end, over the ford: the summer war roads |
| 0 | (33, 43) | 3 — south lane's end at the water meadows: the Saltmere jetty (take the dive contract now, or come back for the bell) |

Walkable routes from lead (40,21): east gate lane -> east road -> ford
path (58,21)-(62,22) -> (78,21); west gate -> road to x33 -> south lane
x33 rows 23-43 -> (33,43). The gorse camp thieves anchor at x28/34/40
row 40 with 2x2 footprints clear of lane column 33. The exit cell
(78,21) is left unoccupied (push soldiers anchor at (78,24)).

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, mud month. Escort job:    (30)
the crown assessor, kept alive    (30)
through the barrow road. He pays  (32)
in the warm coin and will not     (29)
say whose stamp it bears.         (25)
War pay east; the bell south.     (29)
```

Warm-coin thread: lines 4-5 are the act's cliffhanger — the first
employer who KNOWS what the metal is and won't say. (The bandits dig
the barrows for the same coin; the TENT camp is that fiction on the
map.) Line 6 names the hub branches in-fiction: 5 east, 3 south.

## BALANCE NOTES (calibration gate: CREW 2 + SAVE_ALL sub-gate)

- Battle shape: a defended-center clear. The crew wakes inside walls
  with the Assessor warded behind them; the first spring (tick 300)
  comes from BEHIND, through the west gap the column just walked — the
  protect lesson: don't empty the fort. Gorse spring at 700 tests the
  south gate lane; the toll chief's push at 1100 crosses the ford into
  whatever the crew has left. Clearing then requires three sorties:
  barrow field (smash the TENT), gorse camp, and the east toll posts.
- Gate (kill-all, curve crew 2, 8-mixed roster): level_done==1 within
  6000 on >=2/3 seeds @crew 2 AND 3/3 @crew 3; crew 1 may fail.
- SAVE_ALL sub-gate (the ONLY watched walker is the Assessor): no run
  at curve, EITHER roster, 3/3 seeds, where the Assessor dies while
  any crew member lives (census-tick granularity; deaths after a full
  crew wipe in never-exiting AI runs are the documented harness
  artifact). Kettle's death is NOT a loss condition — assert that a
  Kettle-only death does not emit EndGame(SCEN_TYPE_SAVE_ALL).
- Protection stack for the gate: walls + 2 lvl-4 wards + guard-bit
  Assessor (lvl-4 cleric hp) + zero flyers + TENT capped at lvl 2.
  If the sweep shows ward attrition, harden wards to lvl 5 before
  widening the walls (builders-only tuning, Westlands F4 discipline).
- 300-tick smoke: team 0 not extinct; Assessor alive (guard, interior);
  the first spring DORMANT at 250 and ACTIVE by 300 (delay 300
  round-trips); springs 700/1100 dormant, holding level_done at 0.
- Recipe C: A* lead -> both exits; lead -> TENT cell, gorse camp,
  ford watch, and east toll posts (proves both gates, the ford span,
  and both lanes). Footing audit: wagon prop cell (43,19) hosts no
  entity; door-ward/Assessor/Kettle footprints all interior pavement.
- HUB bookkeeping: completing 4 unlocks {5, 3} via harvested exits —
  a company that skipped the dive gets its second chance here; one
  that took it sees 3 already completed (the withdraw prompt path).
