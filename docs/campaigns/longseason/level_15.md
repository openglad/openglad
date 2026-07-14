# Level 15 — Wolf Winter (SAVE_ALL: The Reeve; dusk waves)

- id: 15 (grid_file scen0015)
- title: "Wolf Winter" (11 bytes)
- type_bits: 4 (SCEN_TYPE_SAVE_ALL) — kill-all win; the mission fails if
  the protected walker dies.
- PROTECTED-BIT PLAN (npc_flags bit2 scoping, the F2 lesson): bit2 is set
  on EXACTLY ONE walker — "The Reeve". No other placed NPC gets bit2, so
  the engine watches ONLY him: the unnamed door wards and militia can die
  without ending the mission, and no archmage is present to summon a
  named "Phantom". The Reeve is also the ONLY NAMED team-0 walker on the
  map (belt and braces: even under legacy any-named scoping the loss
  condition would be him alone).
- floors: 1 (no PIX_AIR; fall-line rule trivially satisfied)
- grid: 60 x 60
- par_value: 5
- time_bonus_limit: 5000
- weather: FORCED SNOW (snow-filled map, thousands of snow tiles >= 40).

## STORY POSITION

Thornby, a village snowed in below the Grey Tolls, three days without
the road. The wolves have had the run of it since the drifts closed.
The Reeve of Thornby is the winter act's hinge: he kept the district's
tithe ledgers, and he has seen the warm coin's mint-mark before — he
knows WHOSE ledger it balances. He talks after the wolves are dead
(i.e. the reveal is 16's and 17's fuel; here he only has to LIVE).
Pay for this contract is not coin at all — it is the Reeve's word.

## TERRAIN PLAN (floor 0, the only floor)

Concept: a palisaded village on a green, forest to the north and south,
the toll road in from the west (from 14) and out east (toward the ford).
Wolves probe from the fields at once; the packs come out of the
treelines in dusk waves.

Paint order (before smooth_world):
1. Snow fill: paint_snow(0, 0, 59, 59).
2. North forest: paint_rect(0, 0, 59, 6, PIX_TREE_M1).
3. South forest: paint_rect(0, 53, 59, 59, PIX_TREE_M1).
4. Wolf den (carved INTO the north forest, so wave 4 is reachable):
   den paint_snow(26, 4, 30, 6); den mouth paint_snow(28, 7, 28, 9).
   (y7..9 is open snow anyway; the carve is the den itself.)
5. Palisade arcs (PIX_WALL2, 1 thick, with gaps):
   north paint_rect(18, 10, 42, 10) then carve gap paint_snow(29, 10, 31, 10);
   south paint_rect(18, 50, 42, 50) then carve gap paint_snow(29, 50, 31, 50).
   (East and west sides are open — the road was the defense, once.)
6. Moot-house (the Reeve's hall, center green): walls
   paint_rect(27, 26, 34, 32, PIX_WALL2); interior carve
   paint_snow(28, 27, 33, 31); door gap paint_snow(30, 32, 31, 32)
   (south-facing, 2 wide).
7. Village houses (solid PIX_WALL2 blocks — roofs seen from above):
   (12,14)-(15,16), (44,14)-(47,16), (12,42)-(15,44), (44,42)-(47,44),
   (20,45)-(23,47), (38,12)-(41,14).
8. Frozen mill pond (SE fields): paint_rect(48, 34, 54, 38, PIX_WATER1).
9. Winter fields, stubble through snow (PIX_GRASS_DARK_1):
   (6,20)-(16,26), (44,22)-(54,28), (8,32)-(18,38).

smooth_world(w), then post-smooth (autotiler-inert):
10. Moot-house floor: paint_pavement(28, 27, 33, 31); hearth
    paint(28, 28, PIX_BRAZIER1).
11. The road: west paint_path(1, 29, 26, 30); across the green
    paint_path(27, 33, 34, 34) skirting the hall's south face;
    east paint_path(35, 29, 58, 30). Green paths to the gaps:
    paint_path(29, 11, 30, 25) north; paint_path(29, 35, 30, 49) south.
12. Door torches: paint(29, 32, PIX_TORCH1), paint(32, 32, PIX_TORCH1)
    flanking the moot-house door; one at each palisade gap:
    paint(28, 10, PIX_TORCH1)... palisade cells are wall — put the gap
    torches on the snow INSIDE: paint(28, 11, PIX_TORCH1),
    paint(32, 11, PIX_TORCH1), paint(28, 49, PIX_TORCH1),
    paint(32, 49, PIX_TORCH1).

## DECOR AMBIENCE (decor plane, all non-blocking)

- DECOR_BONES — the wolves' winter so far: hand-set at the den (27,5),
  (29,6); field kills (10,23), (47,25), (14,35); one by the pond (50,33).
- DECOR_PEBBLES on the road and green paths: Path tiles only, rect
  (0,0)-(59,59) mod 11 (a village road is well worn).
- DECOR_SHRUB hedgerow stubs in the FIELDS ONLY (LightGrass/dark-grass
  ground class): rects (6,20)-(16,26) and (44,22)-(54,28) mod 7 — off
  the green, off the road, off every wave lane (concealment stays out of
  the battle lanes; the lanes here are the two path spines and the road).
- DECOR_BOULDER: scatter_boulders(0, 2,12, 10,48, 27) west waste edge.

## ARMIES

Team 0 — Thornby's own (all UNNAMED except the Reeve; guard = npc_flags bit1):

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis | bit2 |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|------|
| 1 | 0 | CLERIC "The Reeve" (9 chars) | 1 | 6 | YES | moot-house, hearth corner (29,28) — off the door's line of sight | 0 | YES | **YES (the only one)** |
| 2 | 0 | SOLDIER (door wards, unnamed) | 2 | 7 | YES | inside the door: (30,31),(31,31) | 0 | no | no |
| 3 | 0 | SOLDIER (militia, unnamed) | 4 | 4 | YES | palisade gaps (30,12),(30,48); road posts (10,29),(50,29) | 0 | no | no |

Reeve protection recipe (per the Westlands E4 pattern, simplified): no
ghosts exist on this map, so there is NO scare-wail force-march threat —
walls + two lvl-7 door wards + guard-held Reeve suffice; no raw-rock
cleft needed. The Reeve is a cleric (a ledger-keeping official, staff
not sword) with specials_disabled so the AI never walks him out the door
chasing a heal.

Team 2 — the packs (ORC = wolves, BIG_ORC = dire wolves, per convention):

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|
| 4 | 2 | ORC (field roamers) | 8 | 5 | no | (8,22),(13,25),(10,34),(16,37),(46,24),(52,27),(46,44),(24,40) | 0 | no |
| 5 | 2 | ORC (dusk wave 1, north treeline) | 6 | 5 | no | (22,8),(26,8),(32,8),(36,8),(24,9),(34,9) | 400 | no |
| 6 | 2 | ORC (dusk wave 2, south treeline) | 6 | 6 | no | (22,51),(26,52),(32,52),(36,51),(24,51),(34,51) | 900 | no |
| 7 | 2 | ORC (dusk wave 3, down the west road) | 5 | 6 | no | (2,26),(2,32),(4,28),(4,31),(6,29) | 1500 | no |
| 8 | 2 | BIG_ORC (wave 3 leaders) | 2 | 7 | no | (3,29),(5,27) | 1500 | no |
| 9 | 2 | BIG_ORC (the winter-king, unnamed) | 1 | 8 | no | den heart (28,5) | 2200 | no |
| 10 | 2 | ORC (the king's pack) | 4 | 6 | no | den (26,4),(30,4),(26,6),(30,6) | 2200 | no |

NO GENERATORS — deliberate, the level-2 Westlands lesson: on a SAVE_ALL
map an endless den guarantees eventual attrition against the protectee;
every pack here is BOUNDED, and wave 4 "empties" the den for good.

Totals: team 0 = 7 livings (1 named); team 2 = 32 livings; 0 generators.
Delayed spawns: 24 across four dusk waves (400/900/1500/2200 — dormant,
NEXT WAVE HUD counts the packs down, level_done held open until the den
empties). MAXOBS ledger: 39 livings + 10 markers + 2 exits + 8 treasures
= 59 objects; far under budget.

All wave cells sit on open snow (y8/9 north strip, y51/52 south strip,
x2..6 west road, the carved den) — every one A*-reachable from the lead
marker; self-check reachability allowlist EMPTY.

## HEROES / NAMED

- "The Reeve" (9 chars, fits the 11-char field) — FAMILY_CLERIC, team 0,
  level 6, guard=YES, specials_disabled=YES, spawn_delay 0, npc_flags
  bit2 PROTECTED, floor 0 at (29,28) inside the moot-house by the
  hearth. SAVE_ALL watches him and ONLY him from tick 0.

## START MARKERS (10; lead FIRST; on the green around the moot-house, 2x2 clearance, clear of walls/torches/wards)

1. (30, 35) — LEAD, on the road before the moot-house door, facing the fields
2. (26, 35)  3. (34, 35)  4. (25, 29)  5. (36, 29)
6. (26, 24)  7. (34, 24)  8. (30, 22)  9. (30, 38)  10. (36, 33)

(All on open green snow/path; the door torches at (29,32)/(32,32) and
the ward pair inside keep their own cells; markers keep 2 tiles off the
hall walls.)

## TREASURE

- The Reeve's strongbox (moot-house): SILVER_BAR (32,27), SILVER_BAR
  (33,27), GOLD_BAR (33,28) — tithe coin; the gold bar is a warm-coin
  sample the Reeve kept back "for evidence".
- Village stores: DRUMSTICK (16,15) on open snow just east of the NW
  house block (12..15, 14..16); DRUMSTICK (43,43) just west of the SE
  house; DRUMSTICK (24,44) beside the S house block (20..23, 45..47).
- MAGIC_POTION (30,24) on the green; MAGIC_POTION (28,30) in the hall.

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57, 33) | 16 — the east road, down to the ford (south shoulder, clear of the road posts and wave lanes) |
| 0 | (2, 35)  | 15->14 backtrack — the west road up to the Grey Tolls (3 rows south of wave-3 cells y26..32) |

Reachability: both exits sit on open snow adjoining the road; flat walks
from the lead marker.

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, deep winter. Thornby is   (31)
snowed in and the wolves have     (29)
the roads. Pay is the Reeve's     (29)
word: he knows whose ledger our   (31)
warm coin balances. Keep the      (28)
old man alive till he says it.    (30)
```

Lines 3-5 are the act's warm-coin thread AND the skeleton's beat: the
Reeve knows the mint's ledgers. The dusk-wave mechanic is deliberately
NOT in the briefing (the NEXT WAVE HUD teaches it); the ledger cares
about the pay and the client.

## BALANCE NOTES / CALIBRATION GATE (curve position: crew 7)

Curve: crew 7 entering (from 14, the meta table's winter rung), crew
7-8 leaving winter. Bracket sweep at {6, 7, 8} x seeds {42, 1337, 2025}
x rosters {A, B}.

- Battle shape: a defense that must end as a HUNT. Roamers pull the crew
  into the fields early; the dusk waves converge on the village from
  three sides at 400/900/1500; the den wave at 2200 is the last count on
  the HUD and the kill-all's final errand — the crew must eventually
  leave the palisade and take the den (mouth at (28,7..9)).
- KILL-ALL GATE (type 4 counts as kill-all in the F4 form): the 8-mixed
  roster at crew 7 reaches level_done==1 (exits present -> 1, not 2)
  within 6000 ticks on >=2/3 seeds AND 3/3 at crew 8. Crew 6 may fail.
- SAVE_ALL SUB-GATE (the binding one): NO run at curve, either roster,
  where the Reeve dies before the crew wipes (census-tick granularity,
  3/3 seeds). The wards + walls must hold wave 1 and 2 leakage without
  crew help; wave 3 (the road wave, lvl 6-7 with dire leaders) is
  allowed to reach the hall ONLY if the crew ignored it — sweeps should
  show ward HP touched but standing at curve.
- Wave sanity: each wave engages (roamers by 300, w1 by 700, w2 by 1300,
  w3 by 2000, den by 2800 at the latest) — no wave may arrive after the
  battle is decided; if sweeps show the den wave mopping into an empty
  field, pull 2200 down to 1800.
- Structural watches: the den mouth is a 1-wide carve — watch for wedged
  melee pairs in it (widen to 2 if the sweeps wedge); SHRUB concealment
  is field-only, so LOS pathologies on the green should not appear; the
  mill pond must not trap SE roamer pathing.
- SAVE_ALL trap to assert in-sim: killing the Reeve emits
  EndGame(SCEN_TYPE_SAVE_ALL); killing a door ward does NOT (bit2
  scoping pin).
- Weather smoke: >=40 snow tiles, WeatherKind::Snow forced.
- Pins to add: roster row {15, t0 7, t2 32, gens 0, markers 10, exits
  {16, 14}}, delayed 24, ReevePin {level 6, cleric, guard, bit2, the
  only bit2 walker on the map}, briefing 6 lines.
