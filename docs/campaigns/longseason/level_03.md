# Level 3 — Saltmere Bell (OPTIONAL contract)

- id: 3
- title: "Saltmere Bell" (13 bytes)
- type_bits: 0 (kill-all + exits; treasure-heavy per the optional-pays-
  it-back rule). No protected bit.
- floors: 2 — the flooded parish below, the belfry above the water
- grid: 50 x 50
- par_value: 3
- time_bonus_limit: 4000

The OPTIONAL spring contract: the temple's boat rows the company out to
the sunken bell-tower of Saltmere. Floor 0 is the drowned parish (open
water everywhere the streets aren't); floor 1 is the belfry over it,
with an open bell-eye looking down into the nave. Optional rule: +1
harder than the act's median (gates at CREW 2), and the hoard pays it.

## TERRAIN PLAN

init_world(level, 2, 50, 50).

### Floor 0 — the drowned parish, pre-smooth:
1. Everything under water: paint_rect(0, 0, 49, 49, PIX_WATER1)
2. The boat landing (SW islet): paint_rect(4, 40, 12, 46, PIX_DIRT_1)
3. The processional street, just proud of the flood (painted after the
   water): east leg paint_rect(13, 42, 34, 44, PIX_DIRT_1); north leg
   paint_rect(32, 20, 34, 44, PIX_DIRT_1)
4. The churchyard mound: paint_rect(20, 10, 38, 26, PIX_DIRT_1)
5. The tower footprint, solid: paint_rect(24, 12, 33, 21, PIX_WALL2)
6. Churchyard spurs off the mound:
   - crypt spur, west: paint_rect(12, 16, 19, 19, PIX_DIRT_1)
   - graveyard spur, east: paint_rect(39, 14, 45, 18, PIX_DIRT_1)

### Floor 1 — the belfry, pre-smooth:
7. paint_rect(0, 0, 49, 49, PIX_AIR) — open sky over the flood
8. paint_rect(24, 12, 33, 21, PIX_WALL2) — the tower's upper story

smooth_world(w), then post-smooth:

### Floor 0:
9. The nave (tower interior, ankle-deep): marsh_fill(25, 13, 32, 20)
   (PIX_MARSH1/2 checker); the bell dais paint_pavement(27, 15, 30, 18)
10. Tower door, south face: paint_pavement(28, 21, 29, 21)
11. Marsh ring on the mound's outer edge (the flood laps at it):
    marsh_fill on (20,10)-(38,11), (20,25)-(38,26), (20,12)-(21,24),
    (37,12)-(38,24)
12. Street paths: paint_path(5, 43, 33, 43) landing to the corner;
    paint_path(33, 21, 33, 42) north leg to the mound;
    crypt lane paint_path(13, 17, 23, 17);
    graveyard lane paint_path(34, 16, 44, 16)
13. Torches flanking the tower door: paint(27, 22, PIX_TORCH1);
    paint(30, 22, PIX_TORCH1) — door lane x28-29 stays open

### Floor 1:
14. Belfry interior: paint_pavement(25, 13, 32, 20)
15. The bell-eye — open center over the dais:
    paint_rect(28, 16, 29, 17, PIX_AIR)
    FALL-LINE RULE: every floor-1 walkable cell adjacent to this AIR
    drops onto the floor-0 DAIS (pavement (27,15)-(30,18) covers the
    full projection) — standable ground, legal. The outside AIR is
    sealed off by the wall shell (never walkable-adjacent).
16. Belfry braziers: paint(25, 20, PIX_BRAZIER1);
    paint(32, 13, PIX_BRAZIER1)

### Stairs:
17. stair_pair(w, 0, 25, 19) — floor-0 cell is nave marsh (standable),
    floor-1 cell is belfry pavement. Exactly one aligned pair on the
    single floor boundary — self-check satisfied, no opt-out.

Decor ambience (non-blocking, after entity placement):
18. DECOR_BONES on the mound and both spurs: mound (20,10)-(38,26)
    mod 7, spurs mod 5 (Dirt+Marsh) — the parish buried its dead here
19. DECOR_PEBBLES on the street paths: (5,43)-(33,43), (33,21)-(33,42)
    and both lanes, mod 9 (Path)
20. No shrubs (nothing grows underwater); no boulder scatter (the
    streets are narrow — keep them clean)

## ARMIES (team 2 — the drowned)

| # | team | family | count | level | guard | floor | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|-------|-----------|-------------|--------------|
| 1 | 2 | FAMILY_SKELETON (street risers) | 4 | 1 | no | 0 | street: (18,43),(24,43),(33,38),(33,30) | 0 | no |
| 2 | 2 | FAMILY_SKELETON (churchyard watch) | 4 | 2 | YES | 0 | mound: (22,23),(35,23),(22,11),(36,12) | 0 | no |
| 3 | 2 | FAMILY_GHOST (drifters) | 3 | 2 | no | 0 | (33,34) street, (24,25) mound, (36,16) graveyard lane | 0 | no |
| 4 | 2 | FAMILY_SMALL_SLIME (nave scum) | 4 | 1 | no | 0 | nave marsh: (26,14),(30,14),(26,19),(30,19) | 0 | no |
| 5 | 2 | FAMILY_SKELETON (crypt guards) | 3 | 2 | YES | 0 | crypt spur: (13,16),(16,16),(18,18) | 0 | no |
| 6 | 2 | FAMILY_GIANT_SKELETON ("the Ringer" — UNNAMED boss beat) | 1 | 3 | YES | 1 | (30,16) — beside the bell-eye | 0 | no |
| 7 | 2 | FAMILY_SKELETON (belfry wards) | 2 | 2 | YES | 1 | (27,19) stair-head, (26,13) north side | 0 | no |
| 8 | 2 | FAMILY_GHOST (the deep answers the bell) | 2 | 3 | no | 0 | water's edge: (34,25) mound rim, (12,18) crypt spur | 500 | no |

Generators (the endgame texture — ghosts rise until it's smashed):
| family | team | level | floor/cell |
|--------|------|-------|------------|
| FAMILY_BONES | 2 | 2 | 0 / (42, 15) — the open grave on the graveyard spur |

Totals: 23 team-2 livings + 1 generator. Delayed spawns: 2.
Guards: 10. MAXOBS ledger: 23 livings + 1 gen + 8 markers +
13 treasures + 2 exits = 47 objects; generator output bounded well
inside 150.

Footing + reachability: every ground unit on dirt/marsh/path/pavement;
NOTHING anchored on open water — the ghost drifters (flyers) are
deliberately placed on standable street/mound cells so the A*
reachability self-check passes with an EMPTY allowlist (the Westlands
water-locked-ghost lesson). Floor-1 units reachable via the stair at
(25,19); no ground unit over PIX_AIR.

## HEROES / NAMED

None. (An unnamed giant skeleton carries the boss beat; naming him would
add a SAVE_ALL hazard pattern for zero story gain — the cast's named
roles don't visit Saltmere.)

## START MARKERS (8; lead first — the temple's boat grounds on the SW
islet; all anchors 2x2-clear on the dirt)

1. (9, 43)  — LEAD, at the street's mouth, facing east
2. (6, 41)
3. (6, 45)
4. (11, 41)
5. (11, 45)
6. (4, 43)
7. (8, 40)
8. (8, 45)

## TREASURE (the optional contract PAYS — hoard placement is the reward
curve)

- Floor 1, the bell hoard (ringing the bell-eye): FAMILY_GOLD_BAR at
  (28,15),(29,15),(28,18),(29,18); FAMILY_MAGIC_POTION at (26,16)
- Floor 0, the crypt: FAMILY_SILVER_BAR at (15,18),(17,16),(19,18);
  FAMILY_INVIS_POTION at (19,17) — the diver's trick
- Floor 0, the graveyard: FAMILY_SILVER_BAR at (40,15);
  FAMILY_DRUMSTICK at (40,17),(44,17)
- Floor 0, the dais (visible through the bell-eye from above):
  FAMILY_MAGIC_POTION at (28,17)

## EXITS (both on the landing islet — the boat is the only way in or out)

| floor | cell | destination |
|-------|------|-------------|
| 0 | (5, 41) | 4 — the temple's boat rows you east to the high road |
| 0 | (5, 45) | 2 — backtrack: back downriver to the ferry |

Walkable routes: trivial from every marker (same islet). The forward
exit means a company can take the dive AFTER the ferry and still make
the assessor's job — and 4's hub exit lets a company that skipped the
dive come BACK for it.

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, still raining. Temple     (29)
job: raise the Saltmere bell      (28)
before the drowned claim it.      (28)
Temple silver is old and cold     (29)
and honest. Note the difference.  (32)
Wet boots. Wetter dead.           (23)
```

Warm-coin thread: lines 4-5 — the temple's COLD, honest silver is the
control sample; the ledger is starting to sort its pay by temperature.

## BALANCE NOTES (calibration gate: CREW 2 — optional, +1 over the
spring median)

- Battle shape: a pier-and-pocket crawl. Street risers meet the landing
  push; the mound is a guarded ring with two treasure spurs (crypt =
  guarded hoard, graveyard = the BONES generator that must be smashed
  or ghosts trickle forever); the nave is a slime puddle with the stair
  in the corner; the belfry is a small elite room — the lvl-3 Ringer
  and two wards over the bell hoard, with the bell-eye as a drop-back
  escape (a bloodied hero can jump down onto the dais potion).
- Gate (kill-all, OPTIONAL rules): clears 3/3 seeds @crew 3; @crew 2
  the attempt must be VIABLE (a live crew at tick 6000 on >=2/3 seeds,
  street + nave cleared) but MAY fail to full-clear — the Westlands
  L9 "may fail badly" precedent, paid back in the hoard. The AI floor
  will park at the belfry guard posts; judge foe-decay, not game_ended.
- SAVE_ALL n/a (type 0, nobody named).
- 300-tick smoke: street risers engaged/dead by 300; deep ghosts
  dormant at 300 (delay 500 round-trips, holds level_done 0); the
  Ringer alive at 300 (guard, upstairs, unreachable by the smoke crew).
- Recipe C: A* lead -> both exits; lead -> crypt guards, generator
  cell, and the floor-1 Ringer (proves street, both lanes, door, and
  stair chain); footing audit incl. no ground unit over floor-1 AIR
  and giant-skeleton 2x2 footprint inside the belfry pavement.
- Stairs: exactly one aligned pair (25,19) on boundary 0-1.
