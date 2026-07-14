# Level 1 — Mud Pay

- id: 1
- title: "Mud Pay" (7 bytes)
- type_bits: 0 (kill-all, then walk to the exit). No protected bit —
  nobody named is placed; the first protect job is level 4.
- floors: 1
- grid: 60 x 40
- par_value: 2
- time_bonus_limit: 3000

SPRING, first thaw. The Weycombe granary dikes: flooded fields south, a
drowned orchard north, the granary the company is paid (in grain) to keep.
Gentle opener — the campaign's Wave-F contract starts a FRESH team here:
this level MUST gate at crew 1.

## TERRAIN PLAN (floor 0 only; longseason_mapgen, clone of westlands API)

init_world(level, 1, 60, 40) — base fill grass.

Pre-smooth paints:
1. The south flood (the fields are under water):
   paint_rect(0, 27, 59, 39, PIX_WATER1)
2. The dike — an earth bank holding the flood, full width:
   paint_rect(2, 24, 57, 26, PIX_DIRT_1)
3. Three drowned field-dikes jutting south into the flood (dead-end
   spurs; the vermin nest at the tips) — painted AFTER the water so the
   dirt sits on top:
   - Spur A: paint_rect(12, 27, 14, 34, PIX_DIRT_1)
   - Spur B: paint_rect(28, 27, 30, 36, PIX_DIRT_1)
   - Spur C: paint_rect(44, 27, 46, 33, PIX_DIRT_1)
4. North pool (the flood came over the top here too):
   paint_rect(6, 2, 20, 10, PIX_GRASS_LIGHT_1); then
   paint_rect(8, 3, 18, 8, PIX_WATER1)
5. The drowned orchard: paint_rect(24, 2, 34, 8, PIX_TREE_M1) — scenery
   block, no entities inside, no route through it needed.
6. The granary: paint_rect(44, 6, 54, 14, PIX_WALL2)
7. Village yard, west: paint_rect(4, 14, 16, 22, PIX_GRASS_LIGHT_1);
   the reeve's hut paint_rect(6, 16, 10, 19, PIX_WALL2)

smooth_world(w), then post-smooth:
8. Granary interior + door (pavement over wall carves the doorway):
   paint_pavement(45, 7, 53, 13); door paint_pavement(48, 14, 50, 14)
9. Reeve's hut interior + door: paint_pavement(7, 17, 9, 18);
   door paint_pavement(8, 19, 8, 19)
10. The village road, west yard to the east map edge (the exit road):
    paint_path(6, 20, 58, 21)
11. Granary lane, door to road: paint_path(48, 15, 49, 19)
12. Dike-top path: paint_path(3, 25, 56, 25); ramp connecting road to
    dike: paint_path(20, 22, 21, 24)
13. Spur paths (centerlines): paint_path(13, 27, 13, 33);
    paint_path(29, 27, 29, 35); paint_path(45, 27, 45, 32)
14. Marsh (post-smooth, marsh_over_grass — plain-grass cells only, edge
    tiles kept): the waterlogged band above the dike
    marsh_over_grass(0, 22, 59, 23); the pool fringe
    marsh_over_grass(4, 1, 22, 11)

Decor ambience (non-blocking scatter_decor AFTER entity placement):
15. DECOR_BONES in the mud — what the flood left: band (0,22)-(59,23)
    mod 13 (Grass+Marsh) and the spur rects mod 9 (Dirt)
16. DECOR_PEBBLES on the road and dike paths: (3,20)-(58,25) mod 11
    (Path only)
17. DECOR_SHRUB on the orchard margin: (22,1)-(36,10) mod 9 (Grass) —
    off the road, off every battle lane
18. scatter_boulders(w, 0, 2, 22, 18, 23, 21) — dike-toe rubble west
    (rows 22-23 only; road rows 20-21 stay clear)

## ARMIES (team 2 — the drowned vermin)

| # | team | family | count | level | guard | placement (floor 0, tile) | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|---------------------------|-------------|--------------|
| 1 | 2 | FAMILY_SMALL_SLIME | 6 | 1 | no | spur tips: (13,33),(13,31) A; (29,35),(29,33) B; (45,32),(45,30) C | 0 | no |
| 2 | 2 | FAMILY_MEDIUM_SLIME | 3 | 1 | no | spur mids: (13,28),(29,29),(45,28) | 0 | no |
| 3 | 2 | FAMILY_ORC (marsh wolves) | 4 | 1 | no | dike top: (8,25),(24,25),(36,25),(52,25) — they rush the yard | 0 | no |
| 4 | 2 | FAMILY_ORC (rats in the grain) | 2 | 1 | YES | granary door flanks: (47,15),(51,15) — door lane x48-50 stays open | 0 | no |
| 5 | 2 | FAMILY_SLIME (the flood keeps giving) | 2 | 2 | no | north pool fringe (grass, NOT water): (7,9),(19,9) | 400 | no |

Generators: none (gentle intro; the whole level is bounded).
Totals: 17 team-2 livings, 0 team-0 placed, 0 generators.
MAXOBS ledger: 17 livings + 0 gens + 9 markers + 6 treasures + 1 exit
= 33 objects — huge headroom (big slimes split when cut, still
trivially inside 150).

Footing: every anchor on dirt/path/grass/marsh; nothing in water, trees,
or wall rects. Reachability from lead: dike wolves via the ramp; spur
slimes via the spur paths; granary rats via road + lane; pool slimes over
open grass. Empty allowlist.

## HEROES / NAMED

None. The Sergeant narrates; Kettle is quoted in the briefing but not
placed until level 4. All team-0 slots come from start markers.

## START MARKERS (9; lead first; every anchor 2x2-clear)

1. (18, 20)  — LEAD, on the road by the dike ramp, facing the fields
2. (14, 17)
3. (14, 21)
4. (16, 15)
5. (12, 19)
6. (10, 21)
7. (16, 23)
8. (20, 17)
9. (22, 21)

## TREASURE

- FAMILY_DRUMSTICK at (46,8),(52,8),(49,10) — the granary: pay is grain
- FAMILY_DRUMSTICK at (29,30) — a dropped sack on spur B
- FAMILY_SILVER_BAR at (8,17) — the reeve's hut: the season's one honest
  coin (the ledger will remember it)
- FAMILY_MAGIC_POTION at (19,4) — pool fringe, exploration reward

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (58, 20) | 2 — east road end (downriver, to the ferry) |

No backtrack — level 1 has no predecessor. Walkable route: lead (18,20)
east along road rows 20-21 to (58,20); pure path tiles.

## BRIEFING (6 lines, each <=33 chars — counted; the skeleton's ledger
sample, binding voice)

```
Ledger, first thaw. Took the      (28)
granary job off Weycombe's reeve. (33)
Pay is grain and a dry roof.      (28)
The dikes crawl with what the     (29)
flood left. Kettle says: earn     (29)
the roof. Losses go in the book.  (32)
```

Warm-coin thread: "Pay is grain" — the arc opens with NO coin at all;
the one silver bar in the reeve's hut is the baseline of honest money
the later warm coin will be measured against.

## BALANCE NOTES (calibration gate: CREW 1 — campaign start)

- Battle shape: 4 dike wolves rush the yard immediately; the crew then
  walks the dike and pokes out the three slime spurs (dead-end pocket
  clears, no flank risk); granary rats are a held door-fight; at tick
  400 the two lvl-2 big slimes wake at the pool — the NEXT WAVE HUD's
  first appearance, and the level's only "twist".
- Gate (kill-all, curve crew 1, Wave-F method): 8-mixed roster clears
  (level_done==1) within 6000 ticks on >=2/3 seeds @crew 1 AND 3/3
  @crew 2; crew 0 doesn't exist, so no curve-1 bracket — record the
  4-soldier floor @crew 1 instead (expect 3/3: all foes are lvl 1-2,
  softer than Westlands L1's lvl-4 ghosts).
- 300-tick smoke (stand-in crew on lead markers): team 0 not extinct;
  dike wolves dead or engaged by 300; pool slimes DORMANT at 300
  (delay 400 round-trips) and holding level_done at 0.
- Recipe C: A* lead (18,20) -> exit (58,20); lead -> each spur tip and
  the granary door (proves ramp, spur paths, and lane).
- Fall-line rule: no PIX_AIR on this level. Slimes never placed on
  water cells (they are ground walkers).
