# Level 8 — The Paymaster Vanishes

- id: 8 (grid_file scen0008)
- title: "The Paymaster Vanishes" (22 bytes; picker truncates >20 to
  "The Paymaster Van..." — accepted, the ledger voice survives it; the
  30-byte .fss field holds the full title)
- type_bits: 0 (kill-all, then walk to an exit). The "recover the chest"
  objective is authored as the treasure heap in Long Tom's hollow ON the
  route to the forward exit — the crew walks through the coin to leave.
  Protected-bit plan: NONE (no team-0 NPC; "Long Tom" is a NAMED TEAM-2
  boss — SAVE_ALL semantics never touch enemy teams).
- floors: 1 (no PIX_AIR)
- grid: 60x60
- par_value: 4
- time_bonus_limit: 5000
- weather: roll (zero snow tiles; dry summer hills)

## TERRAIN PLAN (floor 0)

Concept: goat tracks switchbacking up dry hills to a rock hollow where
Long Tom sits on the army's pay chest. Three cliff bands force the S-climb
(the Westlands High Pass gauntlet shape, re-dressed for summer drought);
ambushes on each terrace; the boss camp is a guard-locked ring at the top.

Paint order (before smooth_world):
1. init_world(level, 1, 60, 60) — base grass.
2. Drought scrub (PIX_GRASS_DARK_1): big patches (4,34)-(56,42),
   (4,20)-(56,28), (24,48)-(44,58).
3. Dry dust pans (PIX_DIRT_1): (20,50)-(28,55) (the dead tarn),
   (10,36)-(16,40), (36,22)-(42,26).
4. Cliff band A (lowest, PIX_WALL2): (0,44)-(49,46). Gap = x50-59.
5. Cliff band B (middle):  (10,30)-(59,32). Gap = x0-9.
6. Cliff band C (upper):   (0,16)-(49,18). Gap = x50-59.
7. Long Tom's hollow (PIX_WALL2 ring shell): rect (6,2)-(20,10); mouth
   carved on the east side by leaving (20,5)-(20,8) unpainted (paint the
   east wall as two runs: (20,2)-(20,4) and (20,9)-(20,10)).
8. Pine clumps (PIX_TREE_M1): (32,34)-(37,38); (14,20)-(19,24);
   (40,2)-(46,7).

smooth_world(w), then post-smooth decor:
9. Hollow floor: paint_pavement(7,3,19,9) (kept clean — no ring accent;
   the guard fight needs open floor).
10. The goat track (paint_path), the whole climb:
    - south approach: paint_path(52,47,53,58)
    - through gap A:  paint_path(52,44,53,46)
    - terrace 1 traverse west: paint_path(6,38,53,39)
    - connector + gap B: paint_path(4,33,5,37); paint_path(4,30,5,32)
    - terrace 2 traverse east: paint_path(4,24,53,25); connector
      paint_path(52,19,53,23)
    - through gap C: paint_path(52,16,53,18)
    - head traverse west to the mouth: paint_path(22,12,53,13); connector
      paint_path(21,6,22,12) (up to the mouth rows y5-8)
11. Camp fire at the mouth: DECOR_BRAZIER at (22,4) (off the mouth lane
    y5-8 and off the track).
12. Jagged scree (PIX_JAGGED_GROUND_1), SPARINGLY and OFF every lane
    (jagged blocks all movement INCLUDING projectiles): two cells
    (36,35),(37,35) inside the scrub patch; two cells (16,21),(17,21)
    inside the pine shadow. Nothing else.
13. scatter_boulders: (0, 0,19, 49,29, 23); (0, 10,33, 59,43, 23);
    (0, 30,47, 59,59, 27); (0, 22,0, 49,15, 25).

## ARMIES (team 2 = Long Tom's hillmen + war deserters; team 1 EMPTY)

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|---------------------|-------------|--------------|
| 1 | 2 | THIEF (scrub ambush) | 6 | 3 | no | lower apron: (18,52),(30,52),(24,48),(44,50),(48,54),(40,56) | 0 | no |
| 2 | 2 | SOLDIER (deserters) | 4 | 3 | no | terrace 1: (10,36),(22,40),(34,36),(44,40) | 0 | no |
| 3 | 2 | ARCHER | 3 | 3 | YES | terrace 1 overwatch on band B lip: (14,34),(28,34),(42,34) | 0 | no |
| 4 | 2 | BARBARIAN | 5 | 4 | no | terrace 2: (8,22),(20,26),(30,21),(40,26),(48,22) | 0 | no |
| 5 | 2 | THIEF | 2 | 4 | no | terrace 2 skulkers: (14,26),(36,22) | 0 | no |
| 6 | 2 | ARCHER | 2 | 4 | YES | head-zone approach: (30,10),(38,12) | 0 | no |
| 7 | 2 | THIEF "Long Tom" (8 chars) | 1 | 7 | YES | the hollow, by the chest: (13,6) | 0 | no |
| 8 | 2 | BARBARIAN (his heavies) | 2 | 5 | YES | (11,5),(15,8) | 0 | no |
| 9 | 2 | ARCHER (his eyes) | 2 | 4 | YES | (10,8),(16,4) | 0 | no |
| 10 | 2 | CLERIC (the bought chirurgeon) | 1 | 4 | YES | (12,8) | 0 | no |
| 11 | 2 | THIEF (rear-guard) | 5 | 4 | no | dormant, SW of the start: (36,55),(39,56),(42,57),(36,58),(39,54) | 450 | no |

Generators: none (the boss ring IS the difficulty; no respawn noise).
Totals: 33 team-2 livings, 0 generators. MAXOBS ledger: 33 livings +
10 markers + 8 treasures + 2 exits = 53 objects; wide headroom.

THE CHEST (treasure objective, inside the hollow):
FAMILY_GOLD_BAR at (9,4),(10,4),(9,5),(10,5) (the chest, spilled);
FAMILY_SILVER_BAR at (11,4),(11,5); FAMILY_MAGIC_POTION at (17,8);
FAMILY_DRUMSTICK at (17,3) (Tom ate well).

## HEROES / NAMED

- "Long Tom" — FAMILY_THIEF, TEAM 2, level 7, guard=true, at (13,6).
  Named ENEMY boss (the campaign's first): name fits the 11-char field.
  Being team 2 he is invisible to SAVE_ALL logic; the name exists for the
  census/HUD and the fiction. Thief special (invisibility) stays ENABLED —
  a vanishing paymaster-thief is the joke; the doc notes the specials_off
  column is "no" deliberately.
- No team-0 named units.

## START MARKERS (10; lead FIRST; south apron scrub/grass; 2x2 clearance;
clear of the dormant rear-guard cells x36-42)

lead (50,56); then (46,54),(54,54),(46,58),(54,58),(50,53),(57,56),
(48,51),(57,52),(52,50). ((52,50) sits on the track x52-53 — path is
walkable, markers only need footing + 2x2 clearance, both hold.)

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (18,8)  | 9 (inside the hollow: Tom's own bolt-track down to Ashfall) |
| 0 | (57,57) | 6 (backtrack: the goat track back to the hay cross) |

Forward exit placement: INSIDE the hollow, 2 cells east of the chest —
the crew must break the guard ring to leave, and walks over the recovered
pay to do it. Walkable route (lead -> exit): (50,56) north on the track ->
gap A (x50-59 at y44-46) -> terrace 1 -> west to gap B (x0-9 at y30-32) ->
terrace 2 -> east to gap C (x50-59 at y16-18) -> head traverse west
(rows 12-13) -> connector to the mouth (20,5)-(20,8) -> hollow floor ->
(18,8). All path/grass/pavement; the jagged cells (step 12) are off every
traverse. Reachability: every living including the hollow ring is
A*-reachable via the mouth; the rear-guard wakes on open scrub.

## BRIEFING (6 lines; char counts 29/28/28/30/26/31)

```
Ledger, black day. The army's
paymaster is gone, chest and
all. A hill thief, Long Tom,
took the season's warm coin up
the goat tracks. Fetch the
chest back or we fetch nothing.
```

(Warm-coin thread: line 4 — the WHOLE season's warm coin in one chest;
losing it would zero the ledger, which is why the company climbs. Pays off
the level-5 "Kettle kept both purses" beat: now every purse is one purse.)

## BALANCE NOTES

Curve position: crew 4 (summer's mainline peak; the fork rejoins here from
6 or 7). Battle shape: a gauntlet of three spaced terrace fights, rear
pressure at tick 450 (the rear-guard wakes behind the crew), then a
guard-locked boss ring the crew must pick an angle on: Long Tom goes
invisible, his archers hold corners, the chirurgeon patches the heavies —
the intended play is killing the cleric first through the mouth choke.

Calibration gate (kill-all, curve 4): 8-mixed roster reaches level_done==1
within 6000 ticks on >=2/3 seeds at crew 4 AND 3/3 at crew 5; crew 3 may
fail. Watch for the L9-Westlands pathology (brawler AI charges a guard
ring head-on and loses): if sweeps show 0/3 clears at curve, trim the
hollow ring (drop one heavy or the cleric to lvl 3) rather than Long Tom —
the boss level is the contract. 300-tick smoke: apron ambush engaged;
terraces 1-2 untouched (t2 alive >= 25); rear-guard dormant at 300, awake
by 500 (round-trips the 450 delay); hollow ring all alive.

## Decor ambience

- Track pebbles the whole climb: (0,0)-(59,59) mod 13 (Path only).
- Drought bones (the paymaster's escort didn't make it): (4,34)-(56,42)
  mod 21 (DarkGrass) and (20,50)-(28,55) mod 9 (the dust pan — Dirt
  ground class).
- Scrub shrubs off the track: (4,20)-(56,28) mod 19 (DarkGrass; the
  terrace-2 traverse rows 24-25 are Path, so shrubs never land on the
  lane).
- Hand accents: the mouth brazier (step 11); DECOR_TORCH1 inside the
  hollow at (7,3) and (7,9) (west corners — clear of the chest, the
  guards, the exit, and the mouth lane y5-8).
