# Level 9 — Ashfall Fair

- id: 9 (grid_file scen0009)
- title: "Ashfall Fair" (12 bytes)
- type_bits: 0 (kill-all + walk to an exit; the strongroom hold is a
  DEFENSE band in calibration, not a type bit — the meta table's gate
  type for 9 is defense). PROTECTED-BIT PLAN: NONE — npc_flags bit2 is
  set on NOBODY. Kettle is placed here but is protect-OPTIONAL per the
  story bible (the campaign's bit2 SAVE_ALL protectees are exactly the
  Assessor in 4 and the Reeve in 15): his death is a ledger line, not a
  mission failure. The two strongroom wardens are UNNAMED and unprotected.
- floors: 1 (no PIX_AIR)
- grid: 60x50
- par_value: 3
- time_bonus_limit: 4500
- weather: roll (zero snow tiles)

## TERRAIN PLAN (floor 0)

Concept: the war ends mid-fair; the fairground riots. A cobbled market
square with two stall rows, the town strongroom on its north side (Kettle
and the season's pay inside), looter mobs already in the square and three
more waves converging through the fair gates, plus two looters' bonfires
(TENT generators) feeding the riot until stamped out.

Paint order (before smooth_world):
1. init_world(level, 1, 60, 50) — base grass.
2. Fair green ring (PIX_GRASS_LIGHT_1): (14,10)-(46,38) (trampled pale
   grass around the square).
3. Corner houses (PIX_WALL2, solid facades, off all routes):
   (6,6)-(14,12); (46,6)-(54,12); (6,38)-(14,44); (46,38)-(54,44).
4. Strongroom (PIX_WALL2 shell): rect (26,8)-(34,14).
5. Well water: paint(23,22, PIX_WATER1) — single impassable cell.
   (Stall stubs are deliberately NOT painted pre-smooth; see step 8b.)

smooth_world(w), then post-smooth decor:
8. Market square pavement: paint_pavement(18,15,42,34) — the whole square
   floor as one field.
8b. Stall stubs, RAW post-smooth PIX_WALL2 on top of the pavement (the
   Westlands raw-rock pattern — raw wall blocks all movers and shots and
   the autotiler never touches post-smooth paints):
   row A (y18-19): (20,18)-(22,19); (26,18)-(28,19); (32,18)-(34,19);
   (38,18)-(40,19). Row B (y26-27): (23,26)-(25,27); (29,26)-(31,27);
   (35,26)-(37,27).
9. Strongroom interior + door: paint_pavement(27,9,33,13); door
   paint_pavement(30,14,30,14).
10. Roads (paint_path):
    - west gate: paint_path(2,24,17,25)
    - east gate: paint_path(43,24,57,25)
    - south gate: paint_path(29,35,30,48)
    - north lane behind the strongroom: paint_path(29,2,30,7)
11. Well surround: paint_pavement(22,21,24,23) (the water cell stays at
    (23,22), a one-cell hazard in the square).
12. Fair torches (decor, blocking — all OFF lanes and marker footprints):
    DECOR_TORCH1 at stall shoulders (19,18),(41,18),(22,26),(38,26);
    strongroom front pair at (26,15),(34,15) (front corners — NOT on the
    shell wall cells, clear of the warden posts (28,15)/(32,15) and the
    door lane x30).
13. Bonfire pits: DECOR_BRAZIER at (11,20),(49,30) beside each TENT.

## ARMIES

Team 0 — the strongroom watch:

| # | team | family | count | level | guard | bit2 | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|------|-----------|-------------|--------------|
| A | 0 | SOLDIER "Kettle" (6 chars) | 1 | 5 | YES | no (protect-optional) | strongroom interior: (30,11) | 0 | no |
| B | 0 | SOLDIER (wardens, unnamed) | 2 | 3 | YES | no | flanking the door: (28,15),(32,15) | 0 | no |

Team 2 — the riot (team 1 EMPTY):

| # | team | family | count | level | guard | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|-----------|-------------|--------------|
| 1 | 2 | THIEF (square mob) | 8 | 2 | no | (20,21),(24,17),(36,17),(40,21),(21,29),(27,31),(33,31),(39,29) | 0 | no |
| 2 | 2 | THIEF (fire-tenders) | 2 | 3 | YES | by the bonfires: (13,21),(47,29) | 0 | no |
| 3 | 2 | THIEF (west gate wave) | 6 | 3 | no | (2,23),(4,24),(2,26),(6,25),(4,26),(6,23) | 250 | no |
| 4 | 2 | THIEF (east gate wave) | 5 | 3 | no | (53,23),(55,24),(57,25),(53,27),(55,26) | 600 | no |
| 5 | 2 | BARBARIAN (drunk mercs) | 2 | 4 | no | with wave 4: (57,23),(57,27) | 600 | no |
| 6 | 2 | THIEF (south gate wave) | 6 | 3 | no | (27,44),(32,44),(28,46),(31,46),(27,48),(32,48) | 1000 | no |
| 7 | 2 | BARBARIAN (the ringleader) | 1 | 5 | no | (29,47) — leads the last wave in | 1000 | no |

Generators (the riot feeds itself until the fires are stamped out):
| family | team | level | cell |
|--------|------|-------|------|
| TENT (looters' bonfire, west green) | 2 | 2 | (11,21) |
| TENT (looters' bonfire, east green) | 2 | 2 | (49,29) |

Totals: team 0 = 3; team 2 = 30 livings + 2 generators. MAXOBS ledger:
33 authored livings + 2 gens + 10 markers + 8 treasures + 2 exits = 55
objects; lvl-2 tents pace far under the cap.

Treasure — THE SEASON'S PAY, inside the strongroom (the crew eats its own
wages; the ledger notes it dryly): FAMILY_GOLD_BAR at (28,10),(29,10),
(31,10); FAMILY_SILVER_BAR at (28,12),(32,12). Fair goods:
FAMILY_DRUMSTICK at (24,18),(36,18) (open stall lanes x23-25/x35-37);
FAMILY_MAGIC_POTION at (25,22) (by the well).

## HEROES / NAMED

- "Kettle" — FAMILY_SOLDIER, team 0, level 5, guard=true, NO bit2
  (protect-optional per the story bible; SAVE_ALL is off), specials_
  disabled=false, at (30,11) inside the strongroom. The company
  quartermaster (skeleton cast: placed in 4/9/18). The design still
  wants him breathing — see the Kettle expectation gate below — but his
  death docks the ledger, not the mission. Interior post + guard AI +
  the door choke at (30,14) keep him out of mob reach while any warden
  or crew member holds the door.
- No other named team-0 units (wardens unnamed by design).

## START MARKERS (10; lead FIRST; square pavement north band and lanes;
every 2x2 footprint checked against stalls (rows y18-19/y26-27), wardens
(28,15)/(32,15), the well surround, and the torch decor)

lead (30,16); then (24,16),(36,16),(20,16),(40,16),(24,21),(30,22),
(36,21),(18,21),(42,21).
(Lead deploys the crew directly between the strongroom door and the square
mob — protect-first posture from tick 0.)

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57,24) | 10 (east: the Foundry's road — autumn buys our debt next) |
| 0 | (2,24)  | 8 (backtrack: west road into the hills) |

Backtrack note: the west wave spawns dormant on cells adjacent to (2,24)
until tick 250; exits are fx objects (no footing conflict), and an early
withdraw walks through the waking mob — priced in. Walkable route: lead
(30,16) -> square pavement -> east road rows 24-25 -> (57,24). Strongroom
reachable via door (30,14); both TENTs stand on open green flanked by
their fire-tenders; every wave path (west/east/south roads) reaches the
square. Reachability check: all livings + generators A*-reachable from
the lead; stall stubs and corner houses are the only interior walls and
all have full perimeter access.

## BRIEFING (6 lines; char counts 30/30/30/28/28/32)

```
Ledger, fair day. War ended at
noon; Ashfall Fair went mad by
two. Looters at the strongroom
where our season's pay sits.
Kettle guards the door. Keep
him breathing. The coin is warm.
```

(Warm-coin thread: line 6 — the recovered chest from level 8 now sits in
Ashfall's strongroom, and Ashfall is the foundry-city's fair: the autumn
act (10, "The Ledger Debt") opens with the Foundry buying the company's
own debt. Kettle beat: cast note says 4/9/18 — this is his middle
appearance; the level protects him hard so the arc stays continuous,
but mechanically he is optional — the SAVE_ALL bits belong to 4 and 15.)

## BALANCE NOTES

Curve position: crew 5 (summer close-out, one step over 8's crew 4 —
the riot presses harder than the guard ring, and the meta table pins 9
at 5). Battle shape: a riot —
the square mob presses the strongroom immediately (protect-first opening),
then three timed gate waves (250/600/1000) force the crew to rotate
around the stall rows while the two bonfires trickle lvl-2 levies until
stamped out. The ringleader's south wave at 1000 is the crisis beat: it
arrives straight up the south road at the door.

Calibration gates (defense primary per the meta table, curve 5):
- DEFENSE BAND: team-0 alive (Kettle + 2 wardens + crew = 11) at tick
  3000 >= 4 (ceil(11/3)), 3/3 seeds at crew 5, 8-mixed roster;
  4-soldier floor recorded.
- KILL completion (secondary, human endgame): 8-mixed reaches
  level_done==1 within 6000 ticks on >=2/3 seeds at crew 5 AND 3/3 at
  crew 6; crew 4 may fail.
- Kettle design expectation (NOT a SAVE_ALL gate — bit2 is on nobody):
  no Kettle death while any crew member lives within tick 1800 at
  curve, 3/3 seeds, both rosters (census-tick granularity; deaths after
  a full crew wipe are the documented harness artifact). If it fails,
  deepen the wardens before moving his post.
- Generator sanity: both tents destroyed in every clearing run; bonfire
  output alive never exceeds ~10 at once (lvl-2 pacing).
300-tick smoke: square mob engaged; west wave awake (250 delay
round-trips), east/south dormant; Kettle at full hp behind the door
(guard, interior — the mob has no reason to reach him before the crew
does); wardens may be bloodied but t0 >= 3 expected at 300.

## Decor ambience

- Road pebbles: (2,2)-(57,48) mod 11 (Path only — all four gate roads).
- Trampled-green shrubs: (14,10)-(46,38) mod 21 (LightGrass; the square
  itself is Pavement so the battle floor stays clean — SHRUB concealment
  never lands on the fight).
- Bones behind the corner houses (the fair's butcher row): (6,45)-(14,48)
  mod 9 (Grass).
- Hand accents: stall torches + strongroom front pair + bonfire braziers
  (steps 12-13).
