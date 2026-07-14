# Level 12 — The Old Count's Vault  (OPTIONAL: the side venture)

The mine's west wall broke into a dead lord's burial vault. Two hundred
years of grave-goods — minted from the SAME warm metal, which means the
wrongness is old, not new. Single floor of WORKED STONE (deliberate
contrast with 11's raw dirt and with the two-floor delve shape Westlands
used): a concentric guard puzzle — outer gallery ring, gated inner
precinct, walled rotunda — held by the Count's household dead.

- id: 12 (grid_file scen0012)
- title: "The Old Count's Vault" (21 bytes)
- type_bits: 0 (kill-all, then walk out). PROTECTED-BIT PLAN: no npc_flags
  bit2, no team-0 NPCs. "The Count" is a NAMED ENEMY (team 2) via the
  place_named_foe helper — named foes never interact with SAVE_ALL.
- floors: 1
- grid: 56x44
- par_value: 4
- time_bonus_limit: 4500

Reached only via 11's breach exit. OPTIONAL RULE: tuned +1 over the act's
median (the meta table pins 12 at 6-7: arrive at crew 6 off the 5-6 mine,
plays like a crew-7 level) and pays it back in treasure — the act's
biggest hoard.

## TERRAIN PLAN (single floor; fill first, carve rooms, wall lines matter)

1. Fill: paint_rect(0,0,55,43, PIX_WALL2).
2. Carve PIX_FLOOR1 (worked stone, not dirt):
   - BREACH ENTRY room (west): (2,16)-(9,28) — the mine side of 11's
     broken wall; opens directly into the west link (x9/x10 adjacency).
   - Outer gallery RING: north (10,6)-(46,11); south (10,33)-(46,38);
     west link (10,12)-(15,32); east link (41,12)-(46,32). (Corners
     overlap; the ring is fully connected.)
   - Inner PRECINCT: (17,13)-(39,31). The un-carved lines x16, x40, y12,
     y32 remain wall — the precinct is sealed off the ring except at the
     four gates below.
3. Re-wall the VAULT BLOCK inside the precinct: paint_rect(21,17,35,27,
   PIX_WALL2); then carve the ROTUNDA inside it: paint_rect(23,19,33,25,
   PIX_FLOOR1). Block wall thickness 2 on every side.
4. GATES (carve through the wall lines, 3 wide / 3 tall):
   - north gate (27,12)-(29,12); south gate (27,32)-(29,32);
   - west gate (16,21)-(16,23); east gate (40,21)-(40,23);
   - VAULT GATE through the block's north wall: (27,17)-(29,18).
5. smooth_world(w).
6. Post-smooth dressing:
   - paint_pavement over the rotunda (23,19)-(33,25) (the tomb floor).
   - paint_path breach->west link scuff: (3,21)-(9,23) (the robbers'
     drag-marks; walkable, flavor only).

### Decor ambience (bones decor is the level's signature)
- DECOR_BONES mod 4 over the outer galleries (all four ring rects);
  mod 6 over the precinct bands; hand-placed cluster at the breach room
  (3,17),(6,26),(8,19) — the last crew that tried.
- DECOR_TORCH1 gate shoulders (walkable cells OFF the 3-wide gate lanes,
  reachability check confirms): (26,11),(30,11) north; (26,33),(30,33)
  south; (15,20),(15,24) west; (41,20),(41,24) east; (26,16),(30,16)
  flanking the vault gate approach.
- DECOR_COLUMN_BOTTOM at the four precinct band corners only:
  (17,13),(39,13),(17,31),(39,31). Columns BLOCK ground movement —
  corner cells are never on a gate lane or the kill-all routes, and the
  reachability self-check re-verifies with them placed.
- DECOR_BRAZIER rotunda corners: (23,19),(33,19),(23,25),(33,25) —
  painted BEFORE the treasure lattice, which skips decor-blocked cells.
- DECOR_PEBBLES mod 9 over the breach entry room (rubble).

## ARMIES (team 2 = the Count's household; 33 livings, NO generators —
finite set-piece, the Westlands delve lesson holds)

| group | family | count | level | guard | placement | spawn_delay |
|-------|--------|-------|-------|-------|-----------|-------------|
| breach watch (roam) | SKELETON | 2 | 4 | no | (4,20),(7,25) | 0 |
| gallery patrol (roam) | SKELETON | 8 | 5 | no | (14,8),(26,8),(38,8),(44,15),(44,28),(38,35),(24,35),(12,28) | 0 |
| gallery lights (posted) | GHOST | 2 | 5 | YES | (12,7) NW corner, (44,36) SE corner | 0 |
| GATE WARDS — the guard puzzle. Each ring gate gets a staggered giant + skeleton pair: bait the skeleton and it comes alone; charge the gate and both answer. |||||||
| north gate ward | GIANT_SKELETON | 1 | 7 | YES | anchor (22,13), 4x4 (22-25, 13-16), west of the gate lane | 0 |
| north gate escort | SKELETON | 1 | 6 | YES | (31,14), east of the lane | 0 |
| south gate ward | GIANT_SKELETON | 1 | 7 | YES | anchor (26,28), 4x4 (26-29, 28-31) | 0 |
| south gate escort | SKELETON | 1 | 6 | YES | (23,29) | 0 |
| west gate ward | GIANT_SKELETON | 1 | 7 | YES | anchor (17,20), 4x4 (17-20, 20-23) | 0 |
| west gate escort | SKELETON | 1 | 6 | YES | (17,25) | 0 |
| east gate ward | GIANT_SKELETON | 1 | 7 | YES | anchor (36,20), 4x4 (36-39, 20-23) | 0 |
| east gate escort | SKELETON | 1 | 6 | YES | (38,25) | 0 |
| gate lights (posted) | GHOST | 2 | 6 | YES | (24,14) north band, (32,29) south band | 0 |
| precinct patrol (roam) | SKELETON | 4 | 5 | no | (18,17),(37,16),(18,27),(36,28) | 0 |
| VAULT WARD (blocks the straight line north gate -> vault gate) | GIANT_SKELETON | 1 | 8 | YES | anchor (27,13), 4x4 (27-30, 13-16) | 0 |
| rotunda honor guard | GIANT_SKELETON | 1 | 8 | YES | anchor (24,20), 4x4 (24-27, 20-23) | 0 |
| the dead stir (dormant, posted) | GHOST | 4 | 6 | YES | galleries (16,7),(42,7),(16,37),(42,37) | 800 |

Named enemy (place_named_foe):
| name | family | team | level | cell | guard |
|------|--------|------|-------|------|-------|
| "The Count" (9 ch) | GIANT_SKELETON | 2 | 9 | anchor (29,21), 4x4 (29-32, 21-24) in the rotunda | YES |

Footprint audit called out: GIANT_SKELETON is 4x4 — every anchor above
keeps the full footprint inside its carve; the vault ward (27,13) and the
north gate ward (22,13) do not overlap (cols 22-25 vs 27-30); the Count
(29-32) and the honor guard (24-27) share the rotunda without collision.

Totals: 33 team-2 livings (18 SKELETON + 8 GHOST + 7 GIANT_SKELETON);
0 team-0; 0 generators; 4 delayed spawns (tick 800, NEXT WAVE HUD).
MAXOBS ledger: 33 livings + 0 gens + 9 markers + ~40 treasures (~16
rotunda bars + 8 corner + 6 gallery + 6 drumsticks + 3 potions + 1
invulnerable) + 2 exits = ~84 objects; comfortable under 150.

## TREASURE (the point of the level)

- The ROTUNDA hoard: GOLD/SILVER checkerboard over (23,19)-(33,25) —
  (gx+gy)%2==0, SILVER_BAR when ((gx+gy)/2)%3==2 else GOLD_BAR; cells
  under the Count/honor-guard footprints and the corner braziers skipped
  via cell_near_entity (~16 bars survive — the grave-coin, warm).
- Precinct corner caches: GOLD_BAR x2 each at (18,14),(19,14);
  (37,29),(38,29); (18,29),(19,29); (37,14),(38,14) — 8 bars.
- Gallery grave-coins: SILVER_BAR x6 at (13,7),(30,7),(45,13),(45,31),
  (30,37),(13,37).
- DRUMSTICK x6 (funeral offerings, still good — the engine's wry joke):
  (11,10),(45,10),(11,34),(45,34),(20,22),(36,22).
- MAGIC_POTION x3: (12,9),(44,35),(28,25) (rotunda back row).
- INVULNERABLE_POTION x1: (28,16) — one gulp before the vault ward.

## HEROES / NAMED

No team-0 heroes. The Count (named enemy) is the set-piece boss; his
death-line belongs to the results screen, not the briefing.

## START MARKERS (9, breach entry room; lead FIRST, 2x2 clearance)

lead (8,22) — at the room's east mouth facing the west link; then
(6,18),(6,22),(6,26),(4,18),(4,22),(4,26),(2,22),(2,26).
Backtrack exit (2,16) clears every marker footprint (nearest is (4,18),
covering rows 18-19).

## EXITS

| floor | cell    | destination |
|-------|---------|-------------|
| 0 | (2,16)  | 11 (backtrack: the breach, back into the mine) |
| 0 | (45,37) | 13 (the Count's stair — up to the Smelter's Road; the
              optional branch rejoins the mainline) |

Walkable route (lead -> 13): (8,22) → west link → ring (either way) →
south gallery → (45,37). The vault crawl hangs off the ring: any of the
four gates into the precinct, the vault gate at (27,17)-(29,18), the
rotunda. Kill-all coverage: ring + precinct bands + rotunda + breach room;
every living A*-reachable from the lead marker (empty allowlist; gate
wards stand IN carves, not in wall).

## BRIEFING (6 lines; char counts 30/26/29/29/29/31)

```
Ledger, side venture. The mine
broke into the Old Count's
vault. Two hundred years dead
and his grave-coin is warm as
our pay. Dead men keep house.
Rob it anyway. Note the losses.
```

(Warm-coin thread: lines 3-5 — the metal predates the Foundry. The arc's
biggest clue, buried in the optional level, soldier-shrugged in the voice.)

## BALANCE NOTES

Battle shape: a ring-and-gates puzzle crawl. The galleries are the safe(ish)
lap — roaming patrols and corner ghost lights the crew can pull apart. Each
precinct gate is a STAGGERED pair (giant + offset skeleton escort): pull
the escort with a ranged shot and it comes alone; walk into the gate lane
and the giant answers too — angle-picking is the "guard puzzle" made of
npc_flags, not scripts. Inside: precinct patrols leak through whichever
gate falls first, the lvl-8 vault ward denies the straight line to the
vault gate, and the rotunda is a two-giant + Count finale in a treasure
room too rich to fight tidily in. The tick-800 gallery ghosts punish
looting the ring before winning it.

Calibration gate (kill-all, OPTIONAL +1 rule; curve crew 6):
- evaluated at crew 6 with the optional allowance: MAY fail the standard
  2/3 clear at curve. Its own design gate: the giants HOLD their posts
  (foe count decays only by roamers/escorts in a stalled run: 33 -> >=18
  at tick 3000 when the crew stalls), the crew is not extinct before tick
  900 on >=2/3 seeds at crew 6, and 3/3 clears at crew 7.
- The Count never leaves the rotunda (ACT_GUARD pin); if a sweep shows
  him chasing into the precinct, the build is wrong.
- structural: 0 generators; 4 dormant at tick 0; exit destinations
  {11, 13} in package; footing audit (seven 4x4 giants inside carves);
  no PIX_AIR (fall-line trivial); reward audit — total bar count on this
  level must exceed any mainline autumn level (it pays for its risk).
