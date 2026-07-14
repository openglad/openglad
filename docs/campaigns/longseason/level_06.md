# Level 6 — The Hay War

- id: 6 (grid_file scen0006)
- title: "The Hay War" (11 bytes)
- type_bits: 0 (kill-all, then walk to an exit). NOTE: "burn the camps" is
  authored as three hostile TENT generators — GEN_EXIT (bit 2) is a dead bit
  with no sim consumer, so the objective is enforced de facto: the levy
  respawns until every camp is torched, so extermination REQUIRES killing
  the generators. Protected-bit plan: NONE (no team-0 NPCs).
- floors: 1 (no PIX_AIR)
- grid: 60x60
- par_value: 3
- time_bonus_limit: 4500
- weather: roll (zero snow tiles)

## TERRAIN PLAN (floor 0)

Concept: a dirt-road cross through four hay quarters. Three walled harvest
depots (NW, NE, S) each hold a muster tent (TENT generator) — the "camps to
burn". Road patrols rove between them; a relief column marches in from the
east at tick 600. THE COMPANY EARNS ITS NAME HERE (briefing line 4-5).

Paint order (before smooth_world):
1. init_world(level, 1, 60, 60) — base grass.
2. Hay quarters (PIX_GRASS_LIGHT_1): NW (4,4)-(24,24); NE (36,4)-(56,24);
   SW (4,36)-(24,56); SE (36,36)-(56,56).
3. Old-battle fallow (PIX_GRASS_DARK_1): (4,31)-(20,34) (west of center,
   south of the road — bones decor lands here).
4. Depot shells (PIX_WALL2):
   - NW: rect (8,8)-(16,15)
   - NE: rect (42,8)-(50,15)
   - S:  rect (26,42)-(34,49)
5. Hedgerows (PIX_TREE_M1) framing the east-west road:
   - north side row: (4,26)-(24,27) with lane gap x11-13;
     (36,26)-(56,27) with lane gap x45-47
   - south side row: (4,33)-(24,34) — NOTE: overlaps the fallow strip;
     paint hedges AFTER the fallow rect so trees win; keep gap x11-13.
     (36,33)-(56,34) with gap x45-47.
6. Pond (PIX_WATER1): (48,33)-(54,37). To make room, the SE south hedge in
   step 5 is shortened to (36,33)-(46,34) (no overlap).

smooth_world(w), then post-smooth decor:
7. Depot interiors + gates (pavement over wall carves the doorway):
   - NW: paint_pavement(9,9,15,14); gate paint_pavement(12,15,12,15)
   - NE: paint_pavement(43,9,49,14); gate paint_pavement(46,15,46,15)
   - S:  paint_pavement(27,43,33,48); gate paint_pavement(30,42,30,42)
8. Roads (paint_path):
   - east-west: paint_path(2,29,57,30)
   - north-south: paint_path(29,2,30,41) (ends at the S depot gate lane;
     gate at (30,42) meets it)
   - depot lanes: paint_path(12,16,12,28) (NW); paint_path(46,16,46,28) (NE)
9. Burn-fires flanking each gate (the torch ambience the briefing sells):
   paint_decor DECOR_BRAZIER at (10,16),(14,16) [NW], (44,16),(48,16) [NE],
   (28,41),(32,41) [S] — all one tile OFF the gate lanes.
10. scatter_boulders(w, 0, 4,31, 20,34, 21) — stones in the fallow.

## ARMIES (team 2 = the enemy levy; team 1 EMPTY)

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|---------------------|-------------|--------------|
| 1 | 2 | SOLDIER | 3 | 2 | YES | NW depot: (10,10),(14,10),(12,13) | 0 | no |
| 2 | 2 | ARCHER  | 2 | 3 | YES | NW depot: (9,12),(15,12) | 0 | no |
| 3 | 2 | SOLDIER | 4 | 2+(i%2) | YES | NE depot: (44,10),(48,10),(44,13),(48,13) | 0 | no |
| 4 | 2 | SOLDIER | 3 | 3 | YES | S depot: (28,44),(32,44),(30,47) | 0 | no |
| 5 | 2 | BARBARIAN (hay-reeve) | 1 | 4 | YES | S depot gate: (30,43) | 0 | no |
| 6 | 2 | SOLDIER (patrols) | 6 | 2 | no | roads: (20,29),(40,30),(29,20),(30,40),(24,30),(36,29) | 0 | no |
| 7 | 2 | ARCHER (patrols) | 4 | 3 | no | (29,12),(30,24),(29,36),(16,30) | 0 | no |
| 8 | 2 | SOLDIER (relief column) | 6 | 3 | no | dormant, east road: (53,29),(55,28),(57,29),(53,31),(55,31),(57,30) | 600 | no |

Generators (the camps to burn):
| family | team | level | cell (floor 0) |
|--------|------|-------|----------------|
| TENT (levy muster) | 2 | 2 | (12,11) — NW depot interior |
| TENT (levy muster) | 2 | 2 | (46,11) — NE depot interior |
| TENT (levy muster) | 2 | 2 | (30,45) — S depot interior |

Totals: 29 team-2 livings + 3 generators. MAXOBS ledger: 29 authored
livings + 3 gens + 10 markers + 6 treasures + 3 exits = 51 objects; tent
output at lvl 2 stays far under the 150 cap (generators pace-limited; the
F4 flood pathology was lvl-5+ gens — these are lvl 2 by design).

Treasure: FAMILY_DRUMSTICK at (10,13) [NW interior, clear of the
(9,12)/(10,10) guard footprints] and (49,10) [NE interior corner];
S depot FAMILY_GOLD_BAR at (31,46) + DRUMSTICK (28,47);
FAMILY_MAGIC_POTION at (51,32) (pond fringe, exploration);
FAMILY_DRUMSTICK at (18,32) (fallow, near the bones).

## HEROES / NAMED

None placed. (Kettle is briefing-voice only until level 9.)

## START MARKERS (10; lead FIRST; on the west road / verges; 2x2 clearance)

lead (4,29); (2,28),(6,28),(2,31),(6,31),(8,29),(4,24),(8,24),(4,35),(8,35).
(All anchors verified against hedge rows 26-27/33-34: (4,24)/(8,24) sit in
the hay quarter above the north hedge; (4,35)/(8,35) on grass below the
south hedge — all 2x2 clear.)

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57,30) | 8 (mainline east: the paymaster's road) |
| 0 | (29,2)  | 7 (OPTIONAL north: the toll road to the Grey Tolls) |
| 0 | (2,30)  | 5 (backtrack: west to the Two Banners field) |

Branch note: 6 is the summer hub — completing it unlocks BOTH 7 and 8
(get_accessible_levels expands all exits of completed levels); 7 rejoins
at 8. Briefing names the choice in-fiction (lines 5-6).
Walkable route: lead (4,29) -> east-west road rows 29-30 -> depot lanes
x12/x46 -> gates -> interiors; north-south road x29-30 -> (29,2) exit and
-> S depot gate. All exits sit on path/grass. Reachability: every guard and
generator is inside a depot with a carved pavement gate on a lane; the
relief column is on the east road; nothing walled off.

## BRIEFING (6 lines; char counts 30/31/28/30/26/25)

```
Ledger, hay war. Burn the levy
camps before they muster twice.
The troops call us the Brass
Kettles now. It sticks. Pay is
warm coin again. Toll fort
north; the pay road east.
```

(Warm-coin thread: line 5. Company-name beat: lines 3-4, per the
skeleton's "the company gets a name" summer note. Lines 5-6 name the
summer hub's branch in-fiction — the toll fort north is optional 7, the
pay road east is mainline 8; the level 7/8 briefings pick the thread up
either way.)

## BALANCE NOTES

Curve position: crew 3 (the summer rung holds at 3, per the meta table:
5 and 6 both gate there). Battle shape: a hub-and-spoke
sweep — the crew owns the road cross early (patrols come to them), then
storms three guard-locked depots one at a time; each depot is a small siege
(gate chokepoint, archers inside, tent respawning lvl-2 skeleton-levies
until torched). The tick-600 relief column punishes slow sweeps by
retaking the road.

Calibration gate (kill-all, curve 3): 8-mixed roster reaches level_done==1
within 6000 ticks on >=2/3 seeds at crew 3 AND 3/3 at crew 4; crew 2 may
fail. Sub-checks: all 3 generators destroyed in every clearing run (they
must be, by construction); no generator-flood pathology (tent output alive
never exceeds ~12 at once at lvl 2 pacing — watch the census curve).
300-tick smoke: patrols engaged (team-2 alive < 29), all 3 tents alive at
tick 300 (asserts depots hold until the crew arrives), relief column still
dormant.

## Decor ambience

- Road pebbles: (2,2)-(57,57) mod 11 (Path only — the cross + lanes).
- Hay-field shrubs: all four quarters mod 19 (LightGrass), off the roads by
  ground-class.
- Bones in the old-battle fallow: (4,31)-(20,34) mod 9 (DarkGrass) — last
  summer's hay war, unaccounted.
- Hand accents: the six gate braziers (step 9); DECOR_TORCH1 pairs inside
  each depot at interior corners clear of guards/treasure/tents:
  (9,9),(15,9) NW; (43,9),(49,9) NE (treasure sits at (49,10), one cell
  south — no overlap); (27,43),(33,43) S.
