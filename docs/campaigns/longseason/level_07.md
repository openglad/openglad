# Level 7 — Grey Tolls (OPTIONAL)

- id: 7 (grid_file scen0007)
- title: "Grey Tolls" (10 bytes)
- type_bits: 0 (kill-all win; the "defense" is a playtest band, not a type
  bit — no SAVE_ALL interplay at all). Protected-bit plan: NONE; the
  toll-warders are deliberately UNNAMED team-0 guards and carry no bit2.
- floors: 1 (no PIX_AIR)
- grid: 50x60
- par_value: 4
- time_bonus_limit: 5000
- weather: roll. ZERO snow tiles — this is the SUMMER fort; level 14
  "The Long Toll" repaints this pass in winter (the callback the skeleton
  promises), so the layout below is written to be re-usable by the L14
  builder with snow fill + Snow weather.

## TERRAIN PLAN (floor 0)

Concept: a mountain toll fort astride a north-south pass road. Cliffs wall
both sides; the company garrisons the bailey beside the standing toll-watch
and holds the wall while a mercenary assault comes up from the south camp
in four waves plus a goat-path flank. Optional level: +1 harder than the
summer median, paid back in the strongbox room.

Paint order (before smooth_world):
1. init_world(level, 1, 50, 60) — base grass.
2. Cliff masses (PIX_WALL2): west block (0,0)-(16,59); east block
   (33,0)-(49,59). Pass corridor = x17-32, full height.
3. Goat path carve (paint grass BACK over the west block):
   paint_rect(6,2,9,23, PIX_GRASS1) then paint_rect(9,20,16,23, PIX_GRASS1)
   — a narrow shelf that lets the tick-1000 flankers reach the corridor at
   (17,20)-(17,23).
4. Fort bailey (PIX_WALL2 shell): rect (15,24)-(34,35) (its side walls sink
   into the cliff blocks — reads as the fort plugging the pass).
5. South camp clearing: the corridor already provides it (x17-32,
   y44-57); scrub it with PIX_GRASS_DARK_1 patches (18,48)-(31,56).

smooth_world(w), then post-smooth decor:
4b. Strongbox room shell (pre-smooth, with the bailey): PIX_WALL2 rect
   (17,26)-(21,29).
6. Bailey interior pavement, painted AROUND the strongbox shell (pavement
   over wall carves doorways, so the shell must be skipped):
   paint_pavement(22,25,33,34); paint_pavement(16,25,21,25);
   paint_pavement(16,30,21,34); paint_pavement(16,26,16,29).
7. Gates (here pavement-over-wall is the point — it carves the doorway):
   north gate paint_pavement(24,24,25,24); south gate
   paint_pavement(24,35,25,35).
8. Strongbox interior + door: paint_pavement(18,27,20,28); door
   paint_pavement(19,29,19,29).
9. Toll road (paint_path): cols 24-25, y2..23 (north approach), y36..57
   (south approach); the bailey pavement carries it through y24-35.
10. Gate torches (decor): DECOR_TORCH1 at (23,23),(26,23) [north gate
    shoulders] and (23,36),(26,36) [south gate shoulders] — all off the
    road cols 24-25.
11. South camp braziers: DECOR_BRAZIER at (18,51),(31,51).
12. scatter_boulders(w, 0, 17,2, 32,22, 19) and
    scatter_boulders(w, 0, 17,36, 32,47, 19) — corridor scree margins
    (modulus high, road cols stay statistically clear; scatter_boulders
    keeps 1-tile entity clearance).

## ARMIES

Team 0 — the toll-watch (placed allied garrison, ALL guards via npc_flags
bit1, ALL UNNAMED):

| # | team | family | count | level | guard | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|-----------|-------------|--------------|
| A | 0 | SOLDIER | 3 | 4 | YES | south gate posts: (24,33),(25,33),(24,29) | 0 | no |
| B | 0 | ARCHER  | 2 | 4 | YES | wall flanks: (18,31),(31,31) | 0 | no |
| C | 0 | CLERIC (toll-clerk) | 1 | 3 | YES | (27,28) — patches the line | 0 | no |

Team 2 — the assault (from the south camp; team 1 EMPTY):

| # | team | family | count | level | guard | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|-----------|-------------|--------------|
| 1 | 2 | SOLDIER | 6 | 3 | no | y44-46: (20,45),(23,44),(26,45),(29,44),(22,46),(27,46) | 0 | no |
| 2 | 2 | BARBARIAN | 5 | 3+(i%2) | no | y49-51: (21,50),(24,49),(27,50),(23,51),(26,51) | 350 | no |
| 3 | 2 | SOLDIER | 6 | 4 | no | y53-55: (19,54),(22,53),(25,54),(28,53),(31,54),(24,55) | 800 | no |
| 4 | 2 | ARCHER | 2 | 4 | no | with wave 3: (20,56),(29,56) | 800 | no |
| 5 | 2 | THIEF (goat-path flank) | 4 | 4 | no | the shelf: (7,4),(8,7),(7,10),(8,13) | 1000 | no |
| 6 | 2 | BARBARIAN (the captain + heavies) | 4 | 5, captain 6 | no | y56-57: captain (24,57); (21,57),(27,57),(24,56) | 1400 | no |
| 7 | 2 | SOLDIER (camp keepers) | 2 | 4 | YES | holding the tents: (18,52),(31,52) — clear of the wave-3 cells and the braziers at y51 | 0 | no |

Generators (the assault's muster — burn the camp to end it):
| family | team | level | cell |
|--------|------|-------|------|
| TENT | 2 | 3 | (20,52) |
| TENT | 2 | 3 | (29,52) |

Totals: team 0 = 6 garrison; team 2 = 29 livings + 2 generators.
MAXOBS ledger: 35 authored livings + 2 gens + 10 markers + 8 treasures
+ 2 exits = 57 objects; lvl-3 tents pace well under the cap.

Treasure (the optional level pays): strongbox room — FAMILY_GOLD_BAR at
(18,27),(19,27),(20,27); FAMILY_SILVER_BAR at (18,28),(20,28);
FAMILY_MAGIC_POTION at (19,28). Larder: FAMILY_DRUMSTICK at (32,26),(32,27).

## HEROES / NAMED

None. The toll-watch is deliberately unnamed (no SAVE_ALL scope creep; the
fort CAN lose warders without failing the mission — the defense band below
is the design measure instead).

## START MARKERS (10; lead FIRST; bailey interior pavement; 2x2 clearance,
checked against garrison posts, strongbox walls, and treasure)

lead (22,31); (22,27),(28,31),(28,26),(30,28),(20,32),(26,32),(30,32),
(24,26),(32,29).
(Every anchor's 2x2 footprint verified cell-by-cell against the garrison
posts, the strongbox shell (17,26)-(21,29), the larder treasure at
(32,26)/(32,27), and both gate lanes — all clear on open bailey pavement.)

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (24,3)  | 8 (north: down to the paymaster's road — rejoin mainline) |
| 0 | (24,56) | 6 (backtrack: the toll road south to the Hay War cross) |

Backtrack note: (24,56) sits between wave-4 spawn cells (captain (24,57) is
adjacent but dormant until tick 1400; exits are fx objects, no footing
conflict). Withdrawing early means walking out through the camp — priced in.
Walkable route: lead (22,31) -> south/north gates on road cols 24-25; the
goat-path shelf connects to the corridor at (17,20)-(17,23) — flankers and
their cells are A*-reachable from the lead via the north gate + corridor.
Strongbox door (19,29) opens onto the bailey. Both cliff blocks are
non-route; nothing lives inside them.

## BRIEFING (6 lines; char counts 27/28/30/31/30/29)

```
Ledger, side work. The Grey
Tolls fort wants a garrison.
Pay is a cut of the road-toll,
counted in that same warm coin.
Hold the wall till the assault
tires. Losses go in the book.
```

(Warm-coin thread: line 4 — even the mountain toll is paid in it; the
autumn act will trace WHY every purse this year is the same mint.)

## BALANCE NOTES

Curve position: crew 4, OPTIONAL (+1 over the summer mainline median 3,
per the optional rule), and it pays it back in the strongbox. Battle
shape: a true defense — the crew + 6 warders hold two gates; waves land at
ticks 0/350/800/1400 with the goat-path thieves arriving BEHIND the north
gate at 1000 (the fort's blind side; teaches gate-watching both ways).
Ending it requires a sortie south to torch the two tents — turtling never
finishes the level (tents respawn lvl-3 levies), which is the sortie beat
the fiction wants.

Calibration gates (defense + kill, curve 4):
- DEFENSE BAND: team-0 alive (warders + crew) at tick 3000 >= 5 on 3/3
  seeds at crew 4 (initial t0 = 6 + 8-mixed crew = 14; band = the
  default ceil(14/3) = 5).
- KILL: 8-mixed clears (both tents + all waves) within 6000 ticks on
  >=2/3 seeds at crew 4 and 3/3 at crew 5; crew 3 MAY fail badly
  (optional +1 rule).
- Wave sanity from the census curve: no wave arrives after the battle is
  decided (wave 4 at 1400 must land while team-2 alive > 0 pressure
  persists — if sweeps show the crew camps the tents by 1200, pull wave 4
  to 1100).
300-tick smoke: wave 1 engaged at the south gate (t2 alive < 29+spawn);
waves 2-4 + flankers dormant at 300; garrison >= 5 alive.

## Decor ambience

- Road pebbles: (17,2)-(32,57) mod 11 (Path only).
- Corridor stones: the two boulder scatters (step 12) already dress the
  scree; add scatter_decor DECOR_PEBBLES (17,36)-(32,47) mod 13 (Grass).
- Scrub shrubs in the south camp: (18,48)-(31,56) mod 15 (DarkGrass) —
  the camp is not a crew battle lane until the sortie; shrubs stay off
  Path by ground-class.
- Hand accents: gate torches + camp braziers (steps 10-11); strongbox
  torch DECOR_TORCH1 at (21,26) (inner corner, off the door lane).
