# Level 18 — "The Warm Mint" (CAMPAIGN FINALE)

- id: 18
- title: `The Warm Mint` (13 bytes, <=30 OK)
- type_bits: **1** = SCEN_TYPE_CAN_EXIT — the point is the climb to the
  master ledger, not extermination; the creditors' war below is scenery
  you fight THROUGH. PROTECTED-BIT PLAN: none. Kettle is placed here but
  is **protect-optional per the skeleton cast list** — SAVE_ALL stays off
  and npc_flags bit2 is set on NOBODY. (The campaign's bit2 SAVE_ALL
  protectees are exactly the Assessor in 4 and the Reeve in 15; Kettle
  is never a loss condition, here or anywhere.)
- floors: **3**
- grid: **64 x 44** (x 0..63, y 0..43) per floor
- par_value: **9**, time_bonus_limit: **7000**
  (longseason's save_level_files takes explicit par/time, westlands-style)

Fiction: the mint has been striking coin from the city's own foundations;
the vault floor is falling into the melt as it is stripped. Three sides:
the creditor companies forcing the doors (team 1 — this campaign's FIRST
and ONLY use of team 1), the mint's own (team 2), and the company (team
0). The master ledger on the crucible floor is the exit: reading the book
IS the settlement.

## TERRAIN PLAN

Interior level: no weather. BRAZIER/FLAME decor everywhere is the finale's
signature — every door, stair, and hall is fire-lit, and the lava channels
carry the cycled-flame band through all three floors.

### Floor 0 — the gatehall and the casting floor

Pre-smooth (PIX_WALL2):
1. Shell: paint_rect(0,0,63,1), paint_rect(0,42,63,43), paint_rect(0,0,1,43),
   paint_rect(62,0,63,43) — 2-thick outer walls.
2. Gatehall wall: paint_rect(18, 2, 18, 41, PIX_WALL2).
3. Stair-chamber wall: paint_rect(52, 2, 52, 41, PIX_WALL2).

smooth_world(w), then post-smooth:
4. Pave every interior zone (paint_pavement): gatehall (2..17, 2..41),
   casting floor (19..51, 2..41), stair chamber (53..61, 2..41).
5. Carve the doors (paint_pavement over the wall cells):
   gatehall→casting: (18, 10..13) and (18, 26..29);
   casting→stair chamber: (52, 19..24).
6. Casting channels (paint_lava — the runnels the coin is poured down;
   cycled-flame band 224-231 animates them):
   - channel A: paint_lava(19, 8, 51, 9)
   - channel B: paint_lava(19, 20, 51, 21)
   - channel C: paint_lava(19, 32, 51, 33)
   Bridge gaps (re-pavement over the lava, each 3 wide):
   A: paint_pavement(34, 8, 36, 9); B: paint_pavement(26, 20, 28, 21) and
   paint_pavement(44, 20, 46, 21); C: paint_pavement(34, 32, 36, 33).
   Bands: N y2..7, band1 y10..19, band2 y22..31, band3 y34..41. The west
   doors open onto band1/band2; the east door (52, 19..24) is reachable
   from band1 at (51,19) and band2 at (51,22..24) — both routes >=2 wide.
7. `stair_pair(w, 0, 57, 21)` — UP in the stair chamber / DOWN on floor 1
   at the same cell. Painted after decor.

Decor floor 0 (after armies; blocking decor off every route):
8. Brazier lines (DECOR_BRAZIER) — braziers block ground, so every cell
   below is PAVEMENT beside (never in) a door lane or bridge gap:
   - gatehall door flanks: (16,9),(16,14),(16,25),(16,30)
   - casting-floor band corners, one per channel end:
     (20,6),(50,6),(20,15),(50,15),(20,25),(50,25),(20,36),(50,36)
     (band2 pair sits at y25, clear of the south door lane y26..29)
   - stair-chamber corners: (54,4),(60,4),(54,39),(60,39)
9. Column pairs (DECOR_COLUMN_BOTTOM) in the gatehall nave: (10,8),(10,17),
   (10,26),(10,35) — a colonnade the door fights swirl around (crew pocket
   is west of it at x<=9).
10. Torch lines (DECOR_TORCH1) on the stair-chamber east wall pavement:
    (61,10),(61,16),(61,27),(61,33).
11. Ambience (scatter_decor, non-blocking): cinder-pebbles over the whole
    casting floor `(19,2)-(51,41)` mod 17 {Pavement}; old bones where the
    companies already died at the doors `(2,2)-(17,41)` mod 19 {Pavement}.

### Floor 1 — the vault floor (collapsing into the melt)

Pre-smooth: same shell as floor 0; internal walls paint_rect(18,2,18,41)
and paint_rect(52,2,52,41).

smooth_world already ran once for the whole world — NOTE for the builder:
paint all three floors' pre-smooth walls BEFORE the single smooth_world
call (westlands convention), then do every post-smooth step per floor.

Post-smooth:
1. Pave: counting rooms (2..17, 2..41), vault hall (19..51, 2..41), east
   landing (53..61, 2..41).
2. Doors: counting←vault (18, 12..15) and (18, 28..31); vault←landing
   (52, 19..24).
3. THE COLLAPSE — PIX_AIR holes punched through the vault floor. Every
   hole sits over OPEN FLOOR-0 PAVEMENT (never over a casting channel or
   a wall — the fall-line rule; a faller lands standing, one floor down,
   in the middle of the creditors' war):
   - H1: paint_rect(30, 13, 33, 16, PIX_AIR)  → floor-0 band1, clear
   - H2: paint_rect(40, 24, 43, 27, PIX_AIR)  → floor-0 band2, clear
   - H3: paint_rect(22, 35, 25, 38, PIX_AIR)  → floor-0 band3, clear
   - H4: paint_rect(44, 4, 46, 6, PIX_AIR)    → floor-0 N band, clear
   (Floor-0 entity placements under the holes were audited: nothing
   dormant or guarded stands beneath a hole footprint.)
4. Melt burn-throughs (paint_lava — the floor already gone liquid):
   pool P1 (19,20)-(22,23); pool P2 (47,30)-(50,33); pool P3 (33,20)-(36,22).
5. `stair_pair(w, 1, 9, 21)` — UP in the counting rooms / DOWN on floor 2
   at the same cell. The climb zigzags: east on floor 0, west on floor 1,
   east again on floor 2.
   Route check (>=2 wide throughout): landing (57,21) → door (52,19..24)
   → vault along y17..19 north of P3 (H1 caps at y16, P3 starts x33 —
   the y17..19 lane threads between them) → west door (18,12..15) →
   counting rooms → stair (9,21). The south door (18,28..31) serves the
   heap-looting loop.
6. Decor floor 1: braziers at the vault doors (17,11),(17,16),(17,27),
   (17,32) and landing corners (54,4),(60,4),(54,39),(60,39); torch lines
   in the counting rooms (3,6),(3,12),(3,30),(3,36); ambience: spilled
   coin reads as pebble-glitter — scatter_decor DECOR_PEBBLES
   `(19,2)-(51,41)` mod 13 {Pavement}; the mint's burned dead —
   DECOR_BONES `(19,2)-(51,41)` mod 19 {Pavement}. scatter_decor skips
   the holes (air) and the stair cells by construction.

### Floor 2 — the crucible floor

Pre-smooth:
1. paint_rect(0, 0, 63, 43, PIX_AIR) — open heat-haze void.
2. The deck slab: paint_rect(3, 13, 60, 30, PIX_WALL2).

Post-smooth:
3. Carve the deck interior to pavement: paint_pavement(5, 15, 58, 28).
4. The crucible itself: paint_lava(14, 18, 49, 25) — a lake of melt in
   the center; north walk y15..17 and south walk y26..28 (both 3 wide),
   west landing (5..13, 15..28), east dais (50..58, 15..28).
5. LAVA SEAL RING (the westlands E5 fall-line lesson, applied from the
   start): paint_lava(3, 13, 60, 14), paint_lava(3, 29, 60, 30),
   paint_lava(3, 15, 4, 28), paint_lava(59, 15, 60, 28). The deck's outer
   edge runs with fire — NO walkable cell on floor 2 is adjacent to
   PIX_AIR, so the only fall lines in the level are the four authored
   vault holes. Ground units cannot step off; flyers can cross.
6. Decor floor 2: braziers at the dais corners (51,16),(51,27),(57,16),
   (57,27); NO decor on the two 3-wide walks (finale fight lanes stay
   clean). The master ledger's lectern is the exit object itself.

### Stair summary (self-check: >=1 aligned pair per boundary)
- boundary 0→1: (57, 21)
- boundary 1→2: (9, 21)

## THE CLIMB ROUTE (provable, on foot, from the lead marker)

(6,21) gatehall → east doors (18,10..13 or 18,26..29) through the
creditors' push → casting floor bands past the furnace-men and the
channel-gap elementals → east door (52,19..24) → golem-warded stair
chamber → UP (57,21) → floor-1 landing → vault hall, threading holes and
melt pools, past the heap wards → west doors → counting rooms → UP (9,21)
→ floor-2 west landing → north or south walk (each held by a lvl-9 fire
elemental) → the dais, The Founder, and the master ledger. Self-check
must A*-solve lead → both stair cells and lead → both exits.

## ARMIES

### Team 1 — the creditor companies (28 livings; the campaign's only
team-1 use — hostile to the mint AND to us; the war "rages below")

All floor 0.

| # | family | count | level | guard | placement | delay | spec.dis |
|---|--------|-------|-------|-------|-----------|-------|----------|
| 1 | SOLDIER | 10 | 6 | no | gatehall, pressing the doors: (11,6),(14,6),(11,10),(14,10),(11,15),(14,15),(11,28),(14,28),(11,33),(14,33) | 0 | no |
| 2 | BARBARIAN | 4 | 7 | no | at the door mouths: (16,11),(16,33),(13,12),(13,31) | 0 | no |
| 3 | ARCHER | 4 | 6 | no | rear rank: (5,6),(5,10),(5,32),(5,36) | 0 | no |
| 4 | THIEF | 4 | 6 | no | already looting the casting floor: (24,12),(40,16),(30,28),(44,30) | 0 | no |
| 5 | SOLDIER | 6 | 7 | no | more companies force the doors behind us — NW/SW corners: (3,3),(6,3),(9,3),(3,39),(6,39),(9,39) | 600 | no |

### Team 2 — the mint (33 livings + 1 generator)

| # | family | count | level | guard | floor / placement | delay | spec.dis |
|---|--------|-------|-------|-------|-------------------|-------|----------|
| 1 | BIG_ORC (furnace-men) | 6 | 7 | no | 0: (22,5),(34,5),(46,5),(26,38),(34,37),(46,37) | 0 | no |
| 2 | FIREELEMENTAL | 2 | 8 | YES | 0: on the channel-B bridge gaps: (26,20),(45,20) | 0 | no |
| 3 | GOLEM (stair wards, warm metal) | 2 | 8 | YES | 0: (55,19),(55,24), flanking the stair | 0 | no |
| 4 | SKELETON (the furnace dead wake) | 6 | 7 | no | 0: (24,15),(36,15),(48,15),(24,26),(36,26),(48,26) | 400 | no |
| 5 | GOLEM (heap wards) | 4 | 9 | YES | 1: one per treasure heap: (27,7),(46,16),(25,32),(43,38) | 0 | no |
| 6 | SKELETON (vault dead) | 6 | 7 | no | 1: (30,8),(38,10),(22,25),(46,25),(30,34),(38,31) | 0 | no |
| 7 | MAGE (the counting-room clerks) | 2 | 7 | YES | 1: (12,8),(12,34) | 0 | no |
| 8 | FIREELEMENTAL | 2 | 9 | YES | 2: mid-walks: (30,16),(30,27) | 0 | no |
| 9 | GHOST | 2 | 8 | no | 2: hovering OVER the crucible melt: (24,21),(40,22) — flyers, legal footing | 0 | no |
| 10 | **The Founder** (named ARCHMAGE) | 1 | 10 | YES | 2: the dais: (52,21) | 0 | no |

Generator:

| family | team | level | floor / cell |
|--------|------|-------|--------------|
| FAMILY_TOWER (mages) | 2 | 4 | 1 / (47, 4) — 4x4 at (47..50, 4..7), beside (not under) hole H4 |

NAMED FOE NOTE: "The Founder" is **11 chars — exactly fills** the 12-byte
.fss name field (the skeleton's "Foundry Master" is 14 and does NOT fit;
briefing text may say "the Foundry Master" only where 33 chars allow —
this doc's briefing doesn't need it). The longseason mapgen clone needs a
`place_named_foe(w, family, team, floor, tx, ty, level, name, guard)`
helper (westlands precedent: The Lurker / Moon Warden).

Census: t1 = 28 livings (6 delayed); t2 = 33 livings (6 delayed) + 1
generator; t0 placed = 1 (Kettle). Total placed 62 + trickle headroom
≈ 90 max — comfortably under MAXOBS 150 (author budget 120).

## HEROES / NAMED (team 0)

| name | chars | family | level | floor | cell | guard | delay |
|------|-------|--------|-------|-------|------|-------|-------|
| Kettle | 6 | SOLDIER | 8 | 0 | (3, 33) | YES | 0 |

(FAMILY_SOLDIER, matching his 4 and 9 placements — the quartermaster
keeps one face all year.) Kettle holds the door pocket beside the
backtrack exit — "Kettle holds the door" is literal: ACT_GUARD keeps
him at the west wall, anchoring whoever falls back, and the team-1 push
breaks on the crew before it reaches him. Protect-OPTIONAL: his death is a ledger line, not
a mission failure (no SAVE_ALL, no bit2 — see type_bits above).

## START MARKERS (10; lead FIRST; 2x2 anchors, all clear of t1 ranks)

Gatehall west pocket, wedge pointing at the doors:
1. **(6, 21)** — LEAD
2. (4, 18)   3. (4, 24)
4. (8, 18)   5. (8, 24)
6. (6, 15)   7. (6, 27)
8. (4, 15)   9. (4, 27)
10. (8, 21)

The creditors' nearest rank stands at x11 — the brawl starts immediately
but nobody deploys in contact.

## TREASURE (the season's pay, heaped where the vault is breaking)

Floor 1 — four heaps, one golem ward each (12 GOLD_BAR, 6 SILVER_BAR):
- Heap 1 (NW): GOLD (25,5),(26,6),(27,5); SILVER (25,7)
- Heap 2 (NE): GOLD (44,14),(45,15),(46,14); SILVER (44,16)
- Heap 3 (SW): GOLD (23,30),(24,31),(25,30); SILVER (23,32)
- Heap 4 (SE): GOLD (41,36),(42,37),(43,36); SILVER (41,38)
- Loose: SILVER (57,25) at the landing; SILVER (12,10) counting room
- DRUMSTICK (12,12),(12,30) — the clerks' suppers, still warm too
- INVULNERABLE_POTION (7,19) — beside the floor-1 stair, for the summit
Floor 0: MAGIC_POTION (33,14) on the casting floor (band1, off the gap).

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 2 | (55, 21) | **19** — THE MASTER LEDGER: reading the book settles the season; on to Settlement Day |
| 0 | (2, 36)  | **17** — backtrack, the doors behind us (withdraw = declining to collect; 17 is always completed by now, so this is the withdraw prompt, never a completion) |

The dais exit sits 3 tiles east of The Founder's guard post — the ledger
is read over his body or not at all. CAN_EXIT means a ghost-ahead player
could in principle dodge past; the two rim elementals and the 3-wide
walks make that a real gamble, and that is accepted finale design.

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, last entry this year.     (29)
The mint pays out tonight. Its    (30)
coin was the city's own bones,    (30)
melted and struck. The master     (29)
book sits on the crucible floor.  (32)
Kettle holds the door. Collect.   (31)
```

Warm-coin thread: lines 2-4 — the mystery RESOLVES here (the warm metal
is the city's stripped foundations, still cooling).

## BALANCE NOTES / calibration

Curve position: crew power 8 (finale). Level type: **exit (CAN_EXIT)**.

Battle shape: three-way. The team-1 push and the team-2 furnace line
grind each other on floor 0 while the crew picks its moment — the door
fight is REAL (t1 ranks stand between the markers and the casting floor)
but both hostile teams bleed each other all run. The climb is three gated
fights: stair-ward golems (floor 0), heap wards + vault dead among the
holes (floor 1 — falling through a hole is survivable and drops you into
the war, a punishment but not a death), and the rim duel: two lvl-9
elementals, two crucible ghosts, then The Founder at the book. Wave
beats: the furnace dead (t2, 400) wake as the crew crosses the casting
floor; the second creditor push (t1, 600) forces the doors behind them —
the NEXT WAVE HUD carries the finale's dread.

Calibration gates (exit type, curve 8; brackets {7,8,9} x 3 seeds x 2
rosters):
- 8-mixed @8 holds >=50% strength (>=4/8) at tick 900 on 3/3 seeds
  (escape-viability window; the full climb's nonstop ETA is ~700-1000
  ticks with three stair gates). 4-soldier roster recorded as floor.
- Kettle design expectation (NOT a SAVE_ALL gate): no Kettle death while
  any crew member lives within tick 1800, 3/3 at curve — he is a guarded
  lvl-8 soldier in the far pocket behind the t1/t0 brawl; if this fails,
  deepen his level before moving his post.
- Three-way sanity: BOTH hostile teams' alive counts strictly decline by
  tick 3000 in unattended runs (the war must actually rage — if t1 and
  t2 never engage, the door/band geometry has a pathing dead zone).
- Climb-freshness: all floor-1/floor-2 guards alive at tick 900 in
  unattended runs (the war stays downstairs).

300-tick smoke (stand-in crew 4x lvl 8 on markers 1-4, 3 seeds):
- Team 0 not extinct; Kettle scen_min_hp > 0.
- t1 alive < 22 AND t2 floor-0 non-dormant alive < 10 → the two hostile
  teams are fighting each other, not just us.
- All 9 upstairs guards (4+2 golems... precisely: heap golems 4, clerks
  2, rim elementals 2, The Founder 1) alive at 300.
- Delayed round-trip: 12 dormant at 300 (6 t2 @400, 6 t1 @600), holding
  level_done open; NEXT WAVE HUD shows the t2 wave first.
- Static pins: floors=3, grid 64x44, 10 markers, t0 livings 1, t1
  livings 28 (±3), t2 livings 33 (±3), 1 generator, exits dests {19,17},
  aligned stair pairs at (57,21) and (9,21), footing audit clean (the
  crucible ghosts are the only over-lava entities — flyers), fall-line
  audit: the four vault holes all land on open floor-0 pavement; floor 2
  has zero walkable-adjacent AIR (lava seal ring).
- Recipe C: A* lead (6,21) → (55,21) f2, → (2,36) f0, → (57,21) f0,
  → (9,21) f1 all non-empty. Reachability of every living/generator from
  the lead marker: empty allowlist EXCEPT the two crucible ghosts
  (deliberate: flyers over lava, unreachable on foot — same exemption
  shape westlands 24 used for its caldera ghosts).
