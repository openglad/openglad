# Level 17 — "Ashfall Gate" (THE RECKONING, part 1)

- id: 17
- title: `Ashfall Gate` (12 bytes, <=30 OK)
- type_bits: **0** (kill-all, then walk to the gate exit). PROTECTED-BIT
  PLAN: none — no SAVE_ALL here, no npc_flags bit2 on anyone. No named
  team-0 NPCs are placed at all (Kettle appears only in 4/9/18 per the
  skeleton cast list), so an accidental SAVE_ALL bit could never scope
  correctly anyway; the bit stays off.
- floors: 1
- grid: 70 x 44 (x 0..69, y 0..43)
- par_value: 7
- time_bonus_limit: 5500

Fiction: the foundry-city's west gate. Every company the year stiffed is
camped on the ash, and the Brass Kettle Company has to fight through its
own kind to present its bill first. The slag runnels leaking out under the
wall are the first sight of the melt — the warm coin's source, one level
early.

## TERRAIN PLAN (floor 0 only)

init_world(level, 1, 70, 44) — base fill grass (walls smooth against it).

Pre-smooth paints (PIX_WALL2):
1. City wall, full east edge: paint_rect(64, 0, 69, 43, PIX_WALL2).
2. Gate passage carve (placeholder grass; re-ashed post-smooth):
   paint_rect(64, 19, 69, 24, PIX_GRASS1) — a 6x6 throat through the wall.
3. Gatehouse towers jutting west, framing the approach:
   paint_rect(60, 15, 63, 18, PIX_WALL2) and
   paint_rect(60, 25, 63, 28, PIX_WALL2) — approach channel x 60..63,
   y 19..24 (6 wide) between them.
4. North camp palisade (the horse companies), opening SOUTH:
   paint_rect(16, 5, 28, 5, PIX_WALL2); paint_rect(16, 5, 16, 11, PIX_WALL2);
   paint_rect(28, 5, 28, 11, PIX_WALL2). Interior (17..27, 6..11).
5. South camp palisade (the hired bows and casters), opening NORTH:
   paint_rect(16, 38, 28, 38, PIX_WALL2); paint_rect(16, 32, 16, 38,
   PIX_WALL2); paint_rect(28, 32, 28, 38, PIX_WALL2). Interior
   (17..27, 32..37).
6. Center wagon-camp (the foot companies), two wagon-walls astride the
   road: paint_rect(36, 16, 44, 17, PIX_WALL2) and
   paint_rect(36, 26, 44, 27, PIX_WALL2). Corridor interior x 36..44,
   y 18..25 (8 tiles wide — man-sized 2x2 walkers pass freely).

smooth_world(w).

Post-smooth (all genre-inert paint):
7. Ash the plain: `paint_ash_open(grid0, 0, 0, 63, 43)` — NEW HELPER for
   the longseason mapgen clone: identical checker recipe to westlands
   paint_ash but SKIPS non-ground tiles (smoothed wall/tree cells), so one
   call ashes the whole plain around the palisades. Then re-ash the carved
   gate passage unconditionally: paint_ash(64, 19, 69, 24).
8. Slag runnels (PIX_LAVA1/2 checker — impassable to ground, the foundry
   leaking under its own wall):
   - north runnel: paint_lava(30, 13, 62, 14)
   - south runnel: paint_lava(30, 29, 62, 30)
   - causeway gaps (re-ash, 3 wide): paint_ash(34, 13, 36, 14) and
     paint_ash(46, 29, 48, 30)
   The runnels pin the road band (y 15..28) as the main approach; the
   flanks cross only at the two gaps.
9. The road: paint_path(2, 21, 63, 22) — west edge to the gate throat.

Decor (paint AFTER armies are placed; blocking decor off all routes):
10. Gate braziers: paint_decor DECOR_BRAZIER at (63, 18) and (63, 25) —
    the throat corners, off the 6-wide passage.
11. Camp watch-torches (DECOR_TORCH1) flanking the openings:
    north camp (17, 12) and (27, 12); south camp (17, 31) and (27, 31).
12. Cook-fire braziers: (26, 10) north camp NE corner; (18, 37) south
    camp SW corner (both clear of the mage/elf footprints and the camp
    openings). The wagon camp gets NO brazier — its corridor is the
    spine battle lane and stays clean of blocking decor.
13. scatter_boulders(w, 0, 0, 0, 15, 43, 25) — sparse west-margin scree.
14. E7-style ambience (scatter_decor, non-blocking):
    - old bones on the ash: `(0,0)-(59,43)` mod 21 {Ash}, thickening in
      the approach channel `(54,15)-(63,28)` mod 9 {Ash} — companies have
      died at this gate before.
    - road pebbles `(2,19)-(63,24)` mod 11 {Path}.
    - cinder-grit `(16,2)-(48,41)` mod 23 {Ash} around the camps.

## ARMIES (team 2 — the creditor companies + the city's gate wards)

All floor 0. 48 livings + 2 generators; MAXOBS ledger: 48 livings +
2 gens + 10 markers + 11 treasures + 2 exits = 73 objects — ample
headroom under 150 for the two lvl-3 trickles.

| # | team | family | count | level | guard | placement | delay | spec.dis |
|---|------|--------|-------|-------|-------|-----------|-------|----------|
| 1 | 2 | SOLDIER (foot companies) | 10 | 5+i%2 | no | wagon corridor: (37,19),(39,19),(41,19),(43,19),(37,24),(39,24),(41,24),(43,24),(39,21),(41,22) | 0 | no |
| 2 | 2 | ARCHER (their pickets) | 4 | 5 | YES | corridor mouths: (35,19),(35,24),(45,19),(45,24) | 0 | no |
| 3 | 2 | BARBARIAN (horse companies) | 6 | 6+i%2 | no | north camp: (18,6),(21,6),(25,6),(18,9),(21,9),(22,11) | 0 | no |
| 4 | 2 | ELF (the hired bows) | 6 | 5+i%2 | no | south camp: (18,33),(21,33),(24,33),(18,36),(21,36),(24,36) | 0 | no |
| 5 | 2 | MAGE (the hired casters) | 2 | 6 | YES | south camp east side: (26,33),(26,36) | 0 | no |
| 6 | 2 | GOLEM (the city's gate wards, cast from the warm metal) | 2 | 9 | YES | approach channel: (61,20),(61,23) | 0 | no |
| 7 | 2 | BIG_ORC (door-muscle) | 4 | 7 | YES | tower feet + passage: (58,16),(58,27),(66,20),(66,23) | 0 | no |
| 8 | 2 | SOLDIER (the second watch) | 8 | 6 | no | camp rears: (17,2),(20,2),(23,2),(26,2),(17,41),(20,41),(23,41),(26,41) | 350 | no |
| 9 | 2 | THIEF (cut-purses — they come for OUR chest) | 6 | 5 | no | west margin, behind the crew: (10,10),(14,13),(12,6),(10,34),(14,30),(12,38) | 700 | no |

Guard notes: rows 2/5/6/7 hold posts via npc_flags bit1 (start-as-guard).
The golem pair leaves only 1-wide seams in the approach channel — a 2x2
crew cannot slip the wards; kill-all means they must fall anyway. The gate
fight is the level's boss beat.

Generators:

| family | team | level | floor/cell | note |
|--------|------|-------|------------|------|
| FAMILY_TENT (skeletons) | 2 | 3 | 0 / (24, 8) — 4x4 inside north camp (24..27, 8..11) | camp trickle |
| FAMILY_TOWER (mages) | 2 | 3 | 0 / (21, 39) — 4x4 behind the south palisade (21..24, 39..42) | camp trickle |

Footing: every placement on ash/path; nothing on lava/wall/tower cells;
all 2x2 footprints checked pairwise non-overlapping (incl. the TENT 4x4 vs
barbarians and the TOWER 4x4 vs the south wall at y38).

## HEROES / NAMED

None placed. The company's own names stay in the ledger this level; Kettle
is 4/9/18 only (skeleton cast list). The Sergeant never deploys.

## START MARKERS (10; lead FIRST; every anchor 2x2 with shoulder room)

West edge, wedge on the road, point east:
1. **(6, 21)** — LEAD, on the road under the banner
2. (4, 18)   3. (4, 24)
4. (2, 15)   5. (2, 21)   6. (2, 27)
7. (8, 17)   8. (8, 25)
9. (4, 12)   10. (4, 30)

(Delayed thieves at x 10..14 are >=2 tiles clear of every anchor; they are
dormant until 700 and wake with a flash behind the crew's line.)

## TREASURE (every camp has a pay chest; all of it warm)

- North camp: GOLD_BAR (26,6),(26,7); SILVER_BAR (17,11); DRUMSTICK (19,11)
- Wagon camp: GOLD_BAR (44,21),(44,22)
- South camp: GOLD_BAR (17,37),(27,37); SILVER_BAR (17,32); DRUMSTICK (19,32)
- Road cache before the wards: MAGIC_POTION (54,21)

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (67, 21) | **18** — through the gate, into the Warm Mint |
| 0 | (1, 26)  | **16** — backtrack, the road back to the Frozen Ford (withdraw = declining the reckoning) |

Walkable route: lead (6,21) → road y21-22 → wagon corridor (8 wide) →
road band between the runnels → approach channel (60..63, 19..24, through
the golem wards) → gate passage (64..69, 19..24) → (67,21). Backtrack is
5 tiles from the lead anchor, offset off the road; no marker overlaps it.

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, ash season. Every company (33)
the year stiffed is camped at the (33)
foundry gates, howling for pay.   (31)
So are we. The line forms behind  (32)
our banner. Collect at the gate;  (32)
the coin inside is still warm.    (30)
```

Warm-coin thread: line 6 (and the golem wards ARE the warm metal, per the
fiction note — the briefing keeps it to coin, the terrain shows the melt).

## BALANCE NOTES / calibration

Curve position: crew power 8 entering (the meta table pins 17 at 8; THE
RECKONING assumes the winter contracts are done). Level type: **kill-all**.

Battle shape: the wagon corridor is the spine fight (10 soldiers + 4
picket archers); the flank camps commit as the crew passes their openings
(no guard on rows 3/4, so barbarians/elves stream out once alerted). At
tick 350 the second watch (8 soldiers) folds in from the camp rears —
timed to land while the corridor fight is still live. At 700 the six
cut-purses wake BEHIND the crew, on the crew's own treasure line — the
rear is never safe on Settlement's eve. The gate ward pair (lvl-9 golems +
door-muscle) is a deliberate wall: kill-all keeps it honest, and the two
generators must be torn out (they don't hold level_done, but they flood
if ignored — TENT/TOWER at lvl 3 are trickles, not floods, per the F4
generator-trim lesson).

Calibration gates (kill-all, curve 8; bracket sweep {7,8,9} x seeds
{42,1337,2025} x rosters {4-soldier A, 8-mixed B}):
- 8-mixed @8 reaches level_done==1 within 6000 ticks on >=2/3 seeds,
  AND 3/3 @9. @7 may fail. 4-soldier roster recorded as the floor.
- Wave sanity: both delayed groups must arrive before the battle is
  decided in curve runs (team-2 alive > 12 at tick 700 expected; if the
  camps melt faster, deepen rows 1/3/4 one level before touching delays).
- No generator flooding: team-2 alive at 6000 in LOST runs stays < 60
  (lvl-3 trickle pin).

300-tick smoke (stand-in crew 4x lvl 8 on markers 1-4, 3 seeds):
- Team 0 not extinct; team-2 alive <= 40 (corridor fight underway).
- All 8 guards (archers/mages/golems/big orcs) alive at 300 — nothing
  reaches the gate early; the boss beat keeps.
- Delayed spawns round-trip: census 14 dormant at tick 300 (8@350 not yet
  woken at the 300 checkpoint, 6@700), and they hold level_done open.
- Static pins: floors=1, grid 70x44, 10 markers, team2 livings 48 (±3),
  2 generators, exits dests {18, 16}, footing audit clean, fall-line
  audit clean (no PIX_AIR on the level at all).
- Recipe C reachability: A* lead (6,21) → (67,21) and → (1,26) non-empty;
  every living + both generators A*-reachable from the lead marker
  (empty allowlist — the palisade camps all have openings, the wall
  passage is carved).
