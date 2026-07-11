# Level 13 — The Smelter's Road  (ore-wagon escort; first snow)

Down the mountain switchbacks from the mine head to the Foundry's smelter,
walking the season's ore past every broke company in the hills. Four
terraces, three bends, an ambush at each, pursuit on the clock behind. The
first snow dusts the shoulders — autumn is ending.

- id: 13 (grid_file scen0013)
- title: "The Smelter's Road" (18 bytes)
- type_bits: 1 = SCEN_TYPE_CAN_EXIT.
  An escort RUN: the crew may exit while foes remain. PROTECTED-BIT
  PLAN: NONE — npc_flags bit2 is set on NOBODY (the campaign's bit2
  SAVE_ALL protectees are exactly the Assessor in 4 and the Reeve in
  15). The Ore Wagon's survival is a CALIBRATION design gate, not a
  mission-fail: lose it and the ledger loses the pay, but the road
  still ends at the smelter. "Spare Cart" and the two drovers are
  team-0 losses the ledger absorbs without comment.
- floors: 1
- grid: 50x60 (tall; north = mine gate, south = smelter gate)
- par_value: 3
- time_bonus_limit: 4000

Entered from 11 (main) or 12 (the Count's stair — both funnel to the road).
Curve position: crew level 6 — the autumn act's close.

## TERRAIN PLAN

1. Fill stays init grass.
2. Autumn ground (pre-smooth): PIX_GRASS_DARK_1 sweeps
   paint_rect(0,16,49,25) and paint_rect(0,44,49,53) (the lower, colder
   terraces darken).
3. CRAG BANDS (PIX_WALL2, pre-smooth) — the scarps that force the
   serpentine; each leaves one wide gap (>=11 tiles, roomy for the
   wagon's large footprint):
   - band A: (0,12)-(38,15) — gap EAST, x39-49 open at y12-15
   - band B: (12,26)-(49,29) — gap WEST, x0-11 open at y26-29
   - band C: (0,40)-(38,43) — gap EAST, x39-49 open at y40-43
   Terraces: T1 y0-11 (mine gate), T2 y16-25, T3 y30-39, T4 y44-59
   (smelter approach).
4. Tree cover clumps (PIX_TREE_M1, pre-smooth, all clear of gaps/road):
   (4,8)-(7,10); (44,20)-(47,22); (4,34)-(7,36); (44,52)-(47,54).
5. smooth_world(w).
6. FIRST SNOW (post-smooth, PIX_SNOW1 patches on the shoulders):
   (2,2)-(5,4) = 12 tiles; (44,16)-(47,18) = 12; (2,44)-(5,46) = 12.
   TOTAL 36 snow tiles — DELIBERATELY under the 40-tile threshold, so
   WeatherKind::Snow is NOT forced: a dusting, not the blizzard (winter
   owns level 14). The self-check pins the count <= 39.
7. The road (paint_path, post-smooth): (24,2)-(25,7) off the mine gate →
   east along (26,6)-(43,7) → south through gap A (42,8)-(43,15) → west
   along (6,20)-(43,21) → south through gap B (6,22)-(7,29) → east along
   (6,34)-(43,35) → south through gap C (42,36)-(43,43) → west along
   (10,48)-(43,49) → south (24,50)-(25,58) to the smelter gate.
8. scatter_boulders on the crag shoulders: (w,0, 0,12, 49,15, 23),
   (w,0, 12,26, 49,29, 23), (w,0, 0,40, 49,43, 23) — rockfall dressing
   (decor boulders keep 1-tile entity clearance and stay off gaps by the
   scatter's checks).

### Decor ambience
- DECOR_PEBBLES mod 13 down the whole road (Path ground class).
- DECOR_BONES hand-cluster at bend 1 (the LAST convoy): (44,9),(46,12),
  (43,13).
- DECOR_SHRUB mod 17 ONLY on the two ambush shoulders where the guards
  post — (44,10)-(48,14) and (2,24)-(6,28) — deliberate concealment for
  the ambush fiction; noted smoke-bounds risk: shrubs sit OFF the road
  band itself, and the calibration sweep runs with them in.
- DECOR_BRAZIER at the smelter gate: (22,57),(27,57) — warm light at the
  end of the road (and the exit beacon).

## ARMIES

### Team 0 — the convoy (4 livings)
| name | family | level | guard | anchor (footprint) | specials_off | bit2 |
|------|--------|-------|-------|--------------------|--------------|------|
| "Ore Wagon" (9 ch) | GOLEM | 8 | no (it must roll) | (23,3) — 4x4-class (23-26, 3-6) | yes | no — design-gated, not bit2 |
| "Spare Cart" (10 ch) | GOLEM | 6 | no | (29,1) — (29-32, 1-4) | yes | no |
| drover | SOLDIER | 5 | no | (21,3) | no | no |
| drover | SOLDIER | 5 | no | (33,5) — clear of both cart footprints | no | no |

GOLEM notes: huge HP pool (base_stats +270 HP bonus, no specials, boulder
default weapon) — the wagon IS the pile of ore; it "throws its load" when
pressed. Slow and tanky by family, which is exactly a wagon. It does not
need to reach the exit (AI never exits): the WIN is the crew reaching
the smelter gate; keeping the Ore Wagon alive is the design gate (and
the fiction's pay), wherever it has trundled.

### Team 2 — every broke company in the hills (27 placed + 10 pursuit)
| group | family | count | level | guard | placement | spawn_delay |
|-------|--------|-------|-------|-------|-----------|-------------|
| bend 1 shoulder bows | ARCHER | 3 | 5 | YES | (45,10),(47,12),(44,14) — in the shrub patch | 0 |
| terrace 2 line | BARBARIAN | 3 | 6 | YES | (14,19),(20,18),(28,22) | 0 |
| terrace 2 bows | ARCHER | 2 | 6 | YES | (34,17),(10,22) | 0 |
| bend 2 knives | THIEF | 3 | 5 | YES | (2,24),(4,27),(9,26) — in the shrub patch | 0 |
| bend 2 bows | ARCHER | 2 | 5 | YES | (2,31),(9,31) — south lip of the gap | 0 |
| terrace 3 line | BARBARIAN | 3 | 6 | YES | (16,32),(26,36),(36,33) | 0 |
| terrace 3 muscle | ORC | 4 | 5 | no (roam) | (12,37),(22,33),(30,37),(40,32) | 0 |
| bend 3 bows | ARCHER | 3 | 6 | YES | (45,38),(47,40),(44,42) | 0 |
| toll takers (smelter approach) | BARBARIAN | 2 | 7 | YES | (24,52),(28,54) | 0 |
| lower prowlers | THIEF | 2 | 6 | no | (14,48),(38,50) | 0 |
| PURSUIT wave 1 (mine gate, west side) | ORC | 4 | 5 | no | (14,2),(17,2),(11,3),(14,4) | 400 |
| PURSUIT wave 1 leader | BARBARIAN | 1 | 6 | no | (20,1) | 400 |
| PURSUIT wave 2 (mine gate, east side) | THIEF | 3 | 6 | no | (34,2),(37,2),(40,3) | 800 |
| PURSUIT wave 2 bows | ARCHER | 2 | 6 | no | (34,4),(37,4) | 800 |

Wave spawn cells sit x<=20 (w1) / x>=34 (w2): clear of both cart
footprints (cols 23-26 and 29-32) and the backtrack exit.

Generators:
| family | team | level | cell | note |
|--------|------|-------|------|------|
| TENT | 2 | 3 | (36,37) — 3x3 (36-38, 37-39), the terrace-3 bandit camp | CAN_EXIT means the trickle pressures but can never block the win; lvl 3 per the flood lesson |

Totals: 37 team-2 livings + 4 team-0 = 41 livings + 1 generator;
10 delayed spawns (NEXT WAVE HUD ticks 400/800 — the pursuit is on a
visible clock behind the convoy).
MAXOBS ledger: 41 + 1 gen + 10 markers + 11 treasures + 2 exits = 65;
plenty of headroom for the tent's trickle.

## TREASURE

- Rest-stone caches (DRUMSTICK x5, one per leg): (8,20),(40,21),(8,34),
  (40,35),(20,48).
- The bandits' takings (GOLD_BAR x3, at the tent camp): (33,37),(34,39),
  (39,36).
- MAGIC_POTION x2: (6,26) in bend 2, (46,41) in bend 3.
- SPEED_POTION x1: (24,44) — downhill legs for the last stretch.

## HEROES / NAMED

The convoy table above IS the named roster: Ore Wagon (design-gated,
not bit2), Spare Cart (expendable — "the second cart is ballast; the
assay ore rides the lead"), two unnamed drovers. No other team-0
placements.

## START MARKERS (10, terrace 1, AHEAD of the carts; lead FIRST)

lead (24,8) — on the road, downhill of the wagon (crew screens the front;
the pursuit takes the rear); then (21,8),(27,8),(19,6),(29,6),(17,8),
(31,8),(19,10),(29,10),(24,10).
Clearances audited: lead (24-25, 8-9) vs wagon rows 3-6 — one clear row
between; (29,6) does NOT collide with Spare Cart because the cart anchors
at (29,1) with footprint rows 1-4; all markers clear of band A (y12).

## EXITS

| floor | cell    | destination |
|-------|---------|-------------|
| 0 | (26,1)  | 11 (backtrack: the mine gate — refusing the road) |
| 0 | (24,58) | 14 (the smelter gate; winter's contract begins) |

Walkable route (lead -> 14): (24,8) → east on y6-7 → gap A (x39-49,
y12-15) → west on y20-21 → gap B (x0-11, y26-29) → east on y34-35 →
gap C (x39-49, y40-43) → west on y48-49 → south to (24,58). Nonstop
crossing ETA ~600-800 ticks (inside the 450-900 escape-viability window).
Every living + the tent A*-reachable from the lead marker (empty
allowlist; the crag-shoulder archers stand on carved-free shoulder cells,
not in wall).

## BRIEFING (6 lines; char counts 25/28/26/26/30/29)

```
Ledger, first snow on the
switchbacks. We walk the ore
down to the smelter. Every
broke company in the hills
wants a cut of the warm metal.
Lose the lead cart, lose all.
```

(Warm-coin thread: line 5 — the ore IS the metal's source, and everyone
unpaid this season knows it. Line 6 states the stake in ledger
arithmetic — enforced as the wagon-survival design gate, not a type bit.)

## BALANCE NOTES

Battle shape: a moving fight the level's geometry paces. Each bend is a
posted ambush the crew must break BEFORE the slow wagon waddles into bow
range (guards hold until approached — the crew chooses when each fight
starts); the terraces between are roamer skirmishes; the pursuit waves
(400/800, from the mine gate BEHIND the convoy) chase downhill and catch
whatever lags — the wagon's huge golem HP is the buffer, the drovers and
Spare Cart are the ablative fiction. The tent camp on terrace 3 pressures
a slow run but can't block the win (CAN_EXIT). The toll takers at the
smelter approach are the last stand-up fight with wave 2 arriving at the
crew's back if they dawdled.

Calibration gate (exit type; curve crew 6; 8-mixed roster primary):
- ESCAPE VIABILITY: crew holds >=50% strength at tick 900 on 3/3 seeds
  at crew 6 (4-soldier floor recorded);
- WAGON SURVIVAL (design expectation, NOT a SAVE_ALL gate — bit2 is on
  nobody): "Ore Wagon" never dies while any crew member lives, 3/3
  seeds x both rosters, enforced within the escape window (tick 1800
  = 2x viability; later deaths in never-exiting 6000-tick AI runs are the
  documented harness artifact);
- Spare Cart / drover deaths are NOT gated (nothing on this map is bit2;
  no death here fails the mission);
- structural: snow-tile count 36 <= 39 per floor (no forced Snow weather
  — pinned); dormant census 10 at tick 0, wakes 400/800; exit
  destinations {11, 14} in package; footing audit (both golems' full
  footprints on open terrace); no PIX_AIR (fall-line trivial); shrub
  patches confined to the two ambush shoulders (smoke re-run after any
  shrub move).
