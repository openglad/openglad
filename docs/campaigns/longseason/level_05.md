# Level 5 — Two Banners

- id: 5 (grid_file scen0005)
- title: "Two Banners" (11 bytes)
- type_bits: 0 (kill-all, then walk to exit). Protected-bit plan: NONE —
  no team-0 named NPC placed, no bit2 anywhere.
- floors: 1 (no PIX_AIR; fall-line rule trivially satisfied)
- grid: 60x40
- par_value: 3
- time_bonus_limit: 4000
- weather: roll (zero snow tiles; summer fields)

## TERRAIN PLAN (floor 0, the only floor)

Concept: a wheat valley between two hired armies. The company deploys at the
blue camp's apron (west edge) and clears the russet skirmish line east across
two hedged field strips and a stream ford, ending at the russet palisade.
Summer war contract #1: fields, guards, one dormant reserve wave.

Paint order (before smooth_world):
1. Base fill grass (init_world(level, 1, 60, 40)).
2. Wheat strips (PIX_GRASS_LIGHT_1 reads as ripe grain):
   (10,4)-(26,14); (10,25)-(26,36); (36,4)-(52,14); (36,25)-(52,36).
3. Fallow headland (PIX_GRASS_DARK_1): north strip (10,0)-(52,2).
4. Hedgerow west (PIX_TREE_M1): x14-15, y0-16 and y23-39 (road gap y17-22).
5. Stream (PIX_WATER1): x30-31, y0-16 and y23-39 (ford gap y17-22).
6. Hedgerow east (PIX_TREE_M1): x44-45, y0-16 and y23-39 (road gap y17-22).
7. Pond in the SW fallow corner: paint_rect(18,30,24,35, PIX_WATER1).
8. Russet palisade (PIX_WALL2 shell): rect (50,12)-(58,27); west gate gap
   left open at (50,19)-(50,20) (carved post-smooth with pavement, below).

smooth_world(w), then post-smooth decor (autotiler-inert):
9. Blue camp apron: paint_pavement(2,17,5,23); cook-fires as decor:
   paint_decor DECOR_BRAZIER at (2,17) and (5,17) (blocking decor, both
   off the road rows 19-20 and off every marker footprint).
10. Russet camp interior: paint_pavement(51,13,57,26); gate doorway
    paint_pavement(50,19,50,20). Gate torches: paint_decor DECOR_TORCH1 at
    (49,17) and (49,22) (outside the gate lane, on grass).
11. The contract road (paint_path), west edge to the russet gate:
    paint_path(2,19,49,20). Ford planks are just the road rows crossing the
    stream gap (grass under, path over — y19-20 x30-31 stays walkable).
12. scatter_boulders(w, 0, 10,0, 52,2, 25) — stones in the fallow headland.

## ARMIES (team 2 = the russet banner; team 1 EMPTY per convention)

| # | team | family | count | level | guard | placement (floor 0, tile) | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|---------------------------|-------------|--------------|
| 1 | 2 | SOLDIER (pickets) | 6 | 1+(i%2) | no | strip A (x16-29): (18,8),(22,12),(25,6),(18,28),(22,32),(25,26) | 0 | no |
| 2 | 2 | BARBARIAN (ford mercs) | 3 | 3 | no | east bank: (33,18),(34,21),(36,20) | 0 | no |
| 3 | 2 | ARCHER | 4 | 2 | YES | behind east hedge: (46,10),(46,14),(46,26),(46,30) | 0 | no |
| 4 | 2 | SOLDIER | 6 | 2 | no | strip B (x36-49): (38,8),(42,12),(47,6),(38,30),(42,26),(47,33) | 0 | no |
| 5 | 2 | SOLDIER (the sergeant) | 1 | 5 | YES | before the gate: (48,20) | 0 | no |
| 6 | 2 | ARCHER | 2 | 3 | YES | inside gate on pavement: (51,16),(51,24) | 0 | no |
| 7 | 2 | SOLDIER (reserve) | 5 | 2 | no | dormant NE headland: (52,2),(54,3),(56,4),(55,6),(57,7) | 500 | no |

Generators: none (first war level; generators debut at 6).
Totals: 27 team-2 livings, 0 generators. MAXOBS ledger: 27 livings +
10 markers + 5 treasures + 2 exits = 44 objects; huge headroom.

Treasure: FAMILY_DRUMSTICK at (3,18) and (4,22) (blue camp stores);
FAMILY_GOLD_BAR at (53,14) (the RUSSET purse — the second warm payment,
briefing calls it out); FAMILY_MAGIC_POTION at (21,29) (pond fringe);
FAMILY_DRUMSTICK at (52,25) (russet larder).

## HEROES / NAMED

None. The Sergeant narrates; Kettle keeps the purses off-map. No named
team-0 unit anywhere (type 0, no SAVE_ALL interplay possible).

## START MARKERS (10; lead FIRST; 2x2 clearance each; all on apron/road grass)

lead (5,20); then (3,19),(7,18),(7,22),(3,22),(9,20),(6,17),(9,17),(6,23),
(9,23). All anchors clear of the brazier decor at (2,17)/(5,17) (each
marker's 2x2 footprint checked cell-by-cell).

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (56,20) | 6 (inside the russet camp — take their muster road on) |
| 0 | (1,19)  | 4 (backtrack: the assessor's road west) |

Walkable route (lead -> forward exit): (5,20) east on road rows 19-20 ->
west hedge gap (x14-15 open y17-22) -> strip A -> ford (x30-31 open y17-22)
-> strip B -> east hedge gap -> gate (50,19)-(50,20) -> pavement -> (56,20).
Nothing on the route is wall/water/tree. Backtrack exit is 4 tiles west of
the lead marker on the road. Reachability: every living is on grass/path/
pavement connected to this route (archers row x46 stand between hedge x44-45
and palisade x50 — the road gap y17-22 connects them; NE reserve stands on
fallow/grass connected via strip B).

## BRIEFING (6 lines; char counts 30/28/31/31/28/30)

```
Ledger, hay-month. Two banners
hired us for the same field.
Clerks' error. Kettle kept both
purses. The blue lot paid first
and their coin came up warm.
Break the russet line by dusk.
```

(Warm-coin thread: line 5. The both-purses gag pays off at level 8 when the
season's coin is stolen in one chest.)

## BALANCE NOTES

Curve position: crew 3 (summer opens at 3 per the campaign_meta table;
the campaign gates 1-2 at crew 1 and the spring hub 4 at crew 2 — this
is the summer step up). Battle shape: three spaced fights west-to-east
(pickets -> ford mercs -> strip B + archer hedge), then a short guard fight
at the gate (sergeant + 2 archers never leave their posts, so the whole map
never piles on). The tick-500 reserve wakes behind the crew's line of
advance and holds level_done open — the NEXT WAVE HUD teaches waves here.

Calibration gate (kill-all, curve 3): 8-mixed roster reaches level_done==1
within 6000 ticks on >=2/3 seeds at crew 3 AND 3/3 at crew 4; crew 2 may
fail. 4-soldier roster recorded as floor. 300-tick smoke: team 0 not
extinct; team-2 alive >= 18 at tick 300 (east half untouched — asserts the
line fights stay staged); the 5 reserves still dormant at tick 300.
Footing/self-check: all placements on grass/path/pavement; archers at x46
have 2x2 footing between hedge and palisade (columns 46-47 clear, y10-30).

## Decor ambience

- Road pebbles: scatter_decor (2,17)-(57,23) mod 11 (Path only).
- Wheat-margin shrubs: (10,4)-(26,36) mod 17 (LightGrass) and
  (36,4)-(52,36) mod 17 (LightGrass) — kept off the road band y17-22
  (ground-class restriction: road is Path, ford band is Grass — shrubs
  restricted to LightGrass never land on the battle lane).
- Fallow bones (an older season's skirmish): (10,0)-(52,2) mod 23
  (DarkGrass).
- Hand accents: DECOR_BRAZIER (2,17),(5,17); DECOR_TORCH1 (49,17),(49,22).
