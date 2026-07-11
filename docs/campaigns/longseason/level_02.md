# Level 2 — The Ferry Right

- id: 2
- title: "The Ferry Right" (15 bytes)
- type_bits: 0 (kill-all + exits; a hold-the-causeway defense in shape).
  No protected bit — the ferrymen are unnamed allies; losing them costs
  coin, not the mission.
- floors: 1
- grid: 80 x 40
- par_value: 3
- time_bonus_limit: 4500 (wave fight runs ~1500-2500 ticks)

The flooded river crossing. The company holds the causeway so the
toll-ferry runs; river bandits want the toll. THE FIRST WARM COIN
appears in this level's pay — the campaign's mystery opens here.
Gates at CREW 1 (the second half of the fresh-team start).

## TERRAIN PLAN (floor 0 only)

init_world(level, 1, 80, 40).

Pre-smooth paints:
1. The river in flood, full height: paint_rect(30, 0, 45, 39, PIX_WATER1)
2. The causeway — an earth bank over the flood, 5 rows:
   paint_rect(30, 18, 45, 22, PIX_DIRT_1) (painted after the water)
3. West bank (bandit shore):
   - scrub: paint_rect(8, 8, 20, 14, PIX_GRASS_DARK_1)
   - bandit camp clearing: paint_rect(4, 24, 14, 32, PIX_GRASS_DARK_1)
   - woods: paint_rect(0, 0, 6, 10, PIX_TREE_M1);
     paint_rect(0, 34, 10, 39, PIX_TREE_M1)
4. East bank (the held shore):
   - toll house: paint_rect(56, 12, 64, 18, PIX_WALL2)
   - fields: paint_rect(56, 26, 70, 34, PIX_GRASS_LIGHT_1)

smooth_world(w), then post-smooth:
5. The causeway road, west approach to the landing:
   paint_path(6, 20, 50, 21)
6. The ferry landing (pavement dock at the east waterline):
   paint_pavement(46, 23, 53, 27)
7. The east road, north-south: paint_path(54, 6, 55, 36)
8. The temple jetty, NE (the dive contract's boat waits here):
   paint_pavement(54, 2, 64, 5)
9. The east branch road (to the assessor's country):
   paint_path(56, 30, 78, 31)
10. Toll house interior + door: paint_pavement(57, 13, 63, 17);
    door, south face: paint_pavement(59, 18, 61, 18)
11. Marsh fringes (marsh_over_grass, plain grass only): west waterline
    marsh_over_grass(26, 0, 29, 39); east waterline
    marsh_over_grass(46, 0, 49, 39); the north shallows (boat-wave
    landing ground) marsh_over_grass(47, 6, 54, 10)
12. Torch standards at the causeway's east mouth: paint(46, 18,
    PIX_TORCH1); paint(46, 22, PIX_TORCH1) — road rows 20-21 stay open
13. Landing braziers: paint(47, 23, PIX_BRAZIER1);
    paint(53, 23, PIX_BRAZIER1)

Decor ambience (non-blocking, after entity placement):
14. DECOR_PEBBLES on all roads: (6,20)-(50,21), (54,6)-(55,36),
    (56,30)-(78,31), each mod 11 (Path only)
15. DECOR_BONES on the west-bank scrub and camp: (4,8)-(24,32) mod 17
    (DarkGrass) — the crossing has been robbed before
16. DECOR_SHRUB on the east fields: (56,26)-(70,34) mod 13 (LightGrass)
    — off the road and the landing
17. scatter_boulders(w, 0, 2, 12, 24, 18, 19) — stony west shore, north
    of the road only (rows 12-18; road rows 20-21 untouched)

## ARMIES

### Team 0 — the ferrymen (unnamed allied garrison; guards via
npc_flags bit1)

| team | family | count | level | guard | placement | delay | specials_off |
|------|--------|-------|-------|-------|-----------|-------|--------------|
| 0 | FAMILY_SOLDIER (ferrymen) | 4 | 2 | YES | causeway mouth posts (47,19),(47,21); landing posts (48,24),(52,24) | 0 | no |
| 0 | FAMILY_ARCHER (the toll-keeper) | 1 | 2 | YES | toll house interior (60,15) | 0 | no |

### Team 2 — the river bandits (bounded waves; NO generators — every
beat is authored, calibratable at crew 1)

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|---------------------|-------------|--------------|
| 1 | 2 | FAMILY_THIEF (knifemen on the span) | 4 | 1 | no | causeway west half: (31,19),(31,21),(33,20),(35,20) | 0 | no |
| 2 | 2 | FAMILY_SOLDIER (brigands) | 3 | 1 | no | west approach: (24,19),(24,21),(26,20) | 0 | no |
| 3 | 2 | FAMILY_ARCHER (camp bowmen) | 3 | 1 | YES | camp: (6,26),(10,26),(6,30) | 0 | no |
| 4 | 2 | FAMILY_SOLDIER (camp wardens) | 2 | 2 | YES | camp: (9,28),(12,30) | 0 | no |
| 5 | 2 | FAMILY_THIEF (the boats — beach upstream, hit the east flank) | 4 | 1 | no | north shallows marsh: (48,7),(51,7),(48,9),(51,9) | 400 | no |
| 6 | 2 | FAMILY_SOLDIER (second push) | 4 | 2 | no | west road: (18,19),(18,21),(20,19),(20,21) | 800 | no |
| 7 | 2 | FAMILY_ARCHER (second push) | 2 | 1 | no | (16,20),(16,22) | 800 | no |
| 8 | 2 | FAMILY_BARBARIAN (the river-master's bruisers) | 2 | 2 | no | camp mouth: (15,25),(15,29) | 1200 | no |
| 9 | 2 | FAMILY_THIEF (last push) | 2 | 2 | no | (13,27),(16,27) | 1200 | no |

Totals: 26 team-2 + 5 team-0 allies = 31 livings, 0 generators.
Delayed spawns: 14 (4 boats + 6 second push + 4 last push).
Guards: 5 team-0 + 5 team-2 camp posts.
MAXOBS ledger: 31 livings + 0 gens + 10 markers + 6 treasures + 3 exits
= 50 objects; ample headroom under 150.

Footing: causeway anchors on dirt/path; boat wave on marsh; camp on dark
grass. No entity in water, woods, or the toll-house wall. Reachability
from lead: every west-bank unit via the causeway (rows 18-22 dirt, road
rows 20-21); the boat wave over open east-bank grass/marsh. Empty
allowlist — bandits are all ground walkers (no flyers on this map).

## HEROES / NAMED

None placed. (Kettle bites the coin in the ledger, not on the field.)

## START MARKERS (10; lead first — the company forms at the causeway's
east mouth, in front of the ferrymen; all anchors 2x2-clear)

1. (50, 20)  — LEAD, plugging the east mouth, facing west
2. (49, 18)
3. (49, 22)
4. (52, 18)
5. (52, 22)
6. (50, 26)  — landing rear rank
7. (54, 20)  — road post
8. (54, 24)
9. (57, 21)
10. (57, 24)

## TREASURE

- The toll chest (toll house): FAMILY_GOLD_BAR at (58,14),(62,14);
  FAMILY_SILVER_BAR at (62,16) — the pay; one of these is THE warm coin
  (fiction only; treasure families carry no flag)
- FAMILY_MAGIC_POTION at (58,16) — the keeper's shelf
- FAMILY_DRUMSTICK at (47,26),(49,24) — the landing's stores

## EXITS (the briefing names both forward roads in-fiction)

| floor | cell | destination |
|-------|------|-------------|
| 0 | (78, 30) | 4 — east branch road end (the assessor's country) |
| 0 | (56, 3)  | 3 — the temple jetty (board the boat: the OPTIONAL dive) |
| 0 | (54, 36) | 1 — backtrack, south road end (the road home to Weycombe) |

Walkable routes from lead (50,20): landing lane to the east road x54-55;
north to the jetty pavement (56,3); south to (54,36); east branch rows
30-31 to (78,30). All path/pavement. The jetty exit sits 4+ rows south
of the map edge woods; the boat-wave thieves anchor at rows 7-9, no
overlap with the exit cell.

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, high water. Ferry job:    (30)
hold the causeway, the toll runs. (33)
Bandits upriver. Paid in advance, (33)
and one coin came warm as bread.  (32)
After: the temple wants its bell, (33)
or the assessor waits east.       (27)
```

Warm-coin thread: line 4 is the mystery's opening beat — the first odd
coin, warm to the touch, in the ferry pay. (Kettle's bite test lives in
the MEMORY of the ledger; level 4's briefing picks the thread up.)

## BALANCE NOTES (calibration gate: CREW 1)

- Battle shape: a hold in four beats. Knifemen + brigands hit the mouth
  immediately (the ferrymen's posts make the first fight winnable at
  crew 1); the boats flank the LANDING at 400 — the level's lesson:
  someone must hold the rear rank; second push 800 funnels up the
  causeway; the river-master's bruisers close it at 1200. Extermination
  then requires CROSSING the causeway to break the camp guards — the
  same "flip at ~1500" shape as a Westlands ford hold.
- Gate (defense — standing allied garrison, curve crew 1): team-0
  (5 ferrymen + 8 crew = 13) alive at tick 3000 >= 5 (ceil(13/3)),
  3/3 seeds, 8-mixed roster; 4-soldier floor recorded.
- Kill viability (secondary, human endgame): 8-mixed @crew 2 reaches
  level_done==1 on >=2/3 seeds within 6000 (the AI floor may stall on
  the guarded camp across the water — judge the foe-decay curve, not
  game_ended; exits mean the level never auto-ends).
- 300-tick smoke: NEITHER side extinct before 150 (siege guardrail);
  team 0 holds >= 1/3 at 300; waves 400/800/1200 all dormant at 300,
  round-tripping and holding level_done at 0.
- Recipe C: A* lead -> all THREE exits; lead -> both camp-guard cells
  (proves the causeway actually crosses).
- No flyers, no generators: the crew-1 gate has no unbounded pressure.
