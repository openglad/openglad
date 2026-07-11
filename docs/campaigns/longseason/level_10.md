# Level 10 — The Ledger Debt  (AUTUMN opens: the forced contract)

The Foundry buys the Brass Kettle Company's paper; the first debt-job is
clearing their Undermill — a working watermill over race channels, squatters
holding the deck and loft, slime breeding in the races. This is the turn of
the campaign: from here the company works for its creditor.

- id: 10 (grid_file scen0010)
- title: "The Ledger Debt" (15 bytes)
- type_bits: 0 (classic kill-all, then walk to an exit). PROTECTED-BIT PLAN:
  no placed NPC carries npc_flags bit2 on this level — SAVE_ALL is not set
  and the scoping rule never engages. "The Factor" (team-0 named soldier) is
  deliberately expendable: if he dies the mission does NOT fail (the briefing
  voice would only dock the company's fee in fiction).
- floors: 2 (floor 0 = yard + mill ground floor + race channels;
  floor 1 = the grain loft over the mill footprint, PIX_AIR outside it)
- grid: 58x44
- par_value: 3
- time_bonus_limit: 4000

Entered from 9 (Ashfall Fair). Curve position: first autumn level — a
mainline crew arrives here at crew level 5 (the meta table's rung).

## TERRAIN PLAN

### Floor 0 — the yard, the mill, the races
1. Fill stays init grass.
2. Autumn ground (pre-smooth): PIX_GRASS_DARK_1 patches
   paint_rect(3,30,14,40) and paint_rect(46,3,56,12).
3. The head-race (PIX_WATER1): paint_rect(0,20,57,23) — full-width channel.
   The tail-race: paint_rect(44,24,47,43) running south off it.
4. The mill (order matters, all pre-smooth):
   a. Walls: perimeter of (20,8)-(41,36) in PIX_WALL2
      (paint_rect the box, then carve).
   b. Interior: paint_rect(21,9,40,35, PIX_FLOOR1).
   c. Wheel pits — re-paint the race through the building:
      paint_rect(21,20,40,23, PIX_WATER1); re-open the wall over the
      channel: paint (20,20)-(20,23) and (41,20)-(41,23) PIX_WATER1.
   d. Doors: north (30,8),(31,8) → PIX_FLOOR1; south (30,36),(31,36).
5. smooth_world(w).
6. Post-smooth (autotiler-inert):
   - Bridges (paint_pavement over water): west footbridge (10,20)-(11,23);
     the pit catwalk (28,20)-(29,23) inside the mill; east ore-bridge
     (52,20)-(53,23); tail-race bridge (44,38)-(47,39).
   - Paths (paint_path): north approach (30,2)-(31,7); yard spur west
     along y4: (4,4)-(29,4); the ore-track: (30,37)-(31,39) off the south
     door, then (32,38)-(54,39) east to the forward exit (crosses the
     tail-race on the pavement bridge at x44-47).
7. scatter_boulders(w,0, 0,0, 18,16, 31) — a few mossy yard stones.

### Floor 1 — the grain loft
1. Fill: paint_rect(0,0,57,43, PIX_AIR).
2. Upper-storey wall ring over the floor-0 wall line: perimeter of
   (20,8)-(41,36) in PIX_WALL2. (FALL-LINE RULE: the ring means no walkable
   loft cell is ever adjacent to AIR that sits over an unstandable wall
   cell; the only falls are the hatches, below.)
3. Loft floor: paint_rect(21,9,40,35, PIX_FLOOR1).
4. Grain hatches (PIX_AIR, post-fill):
   - hatch 1: (28,21)-(29,22) — every cell sits over the floor-0 pit
     CATWALK pavement (28,20)-(29,23): standable below, legal fall.
   - hatch 2: (34,28)-(35,29) — over floor-0 south-hall PIX_FLOOR1.
5. The Miller's den: PIX_CARPET_M paint_rect(24,10,30,14) (inert dressing).
6. smooth_world already ran; carpet/hatches painted after are inert.

### Stairs (aligned pairs, both cells interior floor on BOTH floors)
- stair_pair(w, 0, 38, 10) — NE stair, ground floor north hall up to loft.
- stair_pair(w, 0, 23, 34) — SW stair, south hall up to loft.

### Decor ambience
- DECOR_TORCH1 flanking the doors inside: f0 (29,9),(32,9),(29,35),(32,35);
  loft stairheads f1 (37,11),(24,33).
- DECOR_BRAZIER f1 (22,10),(39,10) — the squatters' fires in the den.
- DECOR_PEBBLES mod 13 over the paths (rects (4,4)-(31,7) and
  (30,37)-(54,39), Path ground class).
- DECOR_BONES mod 7 along the race banks OUTSIDE the mill (rects
  (0,18)-(19,19), (42,18)-(57,19), (0,24)-(19,25), (48,24)-(57,25)) —
  what the slime left of the mill hands.
- DECOR_SHRUB mod 17 in the yard corners only: (0,0)-(16,14) and
  (48,28)-(57,43) — concealment kept OFF the door lanes, the bridges,
  and the ore-track.

## ARMIES

### Team 2 — squatters up top, slime below (33 livings incl. The Miller, 0 generators)
| group | family | count | level | guard | floor | placement | spawn_delay |
|-------|--------|-------|-------|-------|-------|-----------|-------------|
| yard pickets (roam) | THIEF | 3 | 3 | no | 0 | (14,10),(44,10),(16,28) | 0 |
| bank vermin | SMALL_SLIME | 6 | 3 | no | 0 | (6,18),(15,18),(49,18),(6,25),(16,25),(50,25) | 0 |
| bank vermin | MEDIUM_SLIME | 4 | 4 | no | 0 | (8,17),(52,17),(8,27),(52,27) | 0 |
| tail-race mothers | SLIME | 2 | 5 | YES | 0 | (42,30) west bank, (50,30) east bank | 0 |
| north hall squat | THIEF | 4 | 4 | no | 0 | (24,12),(36,14),(26,17),(34,17) | 0 |
| south hall squat | THIEF | 3 | 4 | no | 0 | (25,27),(35,28),(30,31) | 0 |
| pit-crept slime | SLIME | 2 | 4 | no | 0 | (23,25),(38,25) | 0 |
| loft crew (roam) | THIEF | 4 | 5 | no | 1 | (24,16),(36,12),(26,30),(36,32) | 0 |
| the night shift returns | THIEF | 4 | 4 | no | 0 | yard edges (2,12),(4,14),(54,10),(56,12) | 500 |

Named enemy (place_living + the L8 "Long Tom" place_named_foe helper):
| name | family | team | level | floor,cell | guard |
|------|--------|------|-------|------------|-------|
| "The Miller" (10 ch) | THIEF | 2 | 6 | f1 (27,12) on the den carpet | YES |

### Team 0 — the creditor's witness
| name | family | team | level | floor,cell | guard | specials_off | spawn_delay | bit2 |
|------|--------|------|-------|------------|-------|--------------|-------------|------|
| "The Factor" (10 ch) | SOLDIER | 0 | 5 | f0 (5,6) by the backtrack gate | YES | yes | 0 | NO |

Totals: 33 team-2 livings (28 placed + 4 night shift + The Miller) +
1 team-0 ally = 34 livings; 0 generators (finite kill-all set-piece, per
the flood lesson); 4 delayed spawns hold level_done open and show in the
NEXT WAVE HUD.
MAXOBS ledger: 34 livings + 0 gens + 10 markers + 16 treasures + 2 exits
= 62 objects; huge headroom under 150.

## TREASURE

- The Miller's hoard (loft den, skip cells under his footprint via
  cell_near_entity): GOLD_BAR x6 checkerboard over f1 (24,10)-(30,14);
  SILVER_BAR x4 over f1 (32,10)-(35,12).
- DRUMSTICK x5: f0 (22,28),(24,33),(37,33); f1 (38,30); yard f0 (33,3).
- MAGIC_POTION x1: f1 (39,34) — behind the SW stairhead.

## START MARKERS (10, floor 0 yard; lead FIRST, 2x2 shoulder room)

lead (30,6) — on the north approach path facing the door; then
(28,4),(32,4),(26,6),(34,6),(28,2),(32,2),(24,4),(36,4),(22,2).
All on open yard grass/path north of the mill, non-overlapping at 2x2,
clear of The Factor (5,6) and both exits.

## EXITS

| floor | cell    | destination |
|-------|---------|-------------|
| 0 | (3,4)   | 9  (backtrack: the road back to Ashfall — the Foundry's men watch it) |
| 0 | (55,38) | 11 (the ore-track east, down to the Foundry's deep mine) |

Walkable route (lead -> 11): (30,6) → east around the mill's north side
(x42-57, y0-19 all open) → east ore-bridge (52,20)-(53,23) → south-east
quarter → ore-track y38-39 → (55,38). Kill-all coverage: mill interior via
either door; loft via both stairs; SW quarter via the west footbridge;
west-of-tail-race south via the tail-race bridge (44,38)-(47,39); The
Miller's den via either stairhead. Every living A*-reachable from the lead
marker (empty allowlist); slime mothers stand banks, not water.

## BRIEFING (6 lines; char counts 32/28/31/31/30/33)

```
Ledger, first frost. The Foundry
bought our paper. All of it.
Terms: work it off underground.
Job one, clear their Undermill.
Squatters up top, slime below.
The advance was warm coin. Again.
```

(Warm-coin thread: line 6 — the debt itself is paid in the strange metal.)

## BALANCE NOTES

Battle shape: a three-theatre sweep a crew-5 team can take in any order —
yard pickets first (gentle), then the mill halls (thief brawl in corridored
rooms), then the loft (the Miller's guarded den + the two hatches as fast
drops back down), with the race banks as a slime mop-up the bridges
chokepoint. The tick-500 night shift (4 thieves at the yard edges) lands
after the yard is clear and keeps a slow crew honest. No generators; enemy
levels 3-6 against crew 5.

Calibration gate (kill-all; curve crew 5 — the campaign's Wave-F method,
8-mixed roster primary, 4-soldier floor recorded):
- clears (level_done==1) within 6000 ticks on >=2/3 seeds at crew 5 AND
  3/3 at crew 6; crew 4 may fail;
- neither side extinct before tick 150;
- structural: 2 aligned stair pairs; night-shift dormants wake at 500
  (dormant census 4 at tick 0); exit destinations {9, 11} both in package;
  footing audit incl. no ground unit over the pit water; fall-line audit
  passes (loft ring + hatches over catwalk/floor);
- The Factor is NOT gated (expendable ally; expect him to survive at
  curve most seeds since the yard clears first).
