# Level 16 — The Frozen Ford (CAN_EXIT collapsing-route run)

- id: 16 (grid_file scen0016)
- title: "The Frozen Ford" (15 bytes)
- type_bits: 1 (SCEN_TYPE_CAN_EXIT) — run, don't win: the exit works
  while foes remain. NO SAVE_ALL (the Reeve stays in Thornby; nothing on
  this map carries npc_flags bit2 — protected-bit plan: none).
- floors: 1 (no PIX_AIR; fall-line rule trivially satisfied — the
  "collapse" is authored ice geometry + pursuit, not Z holes)
- grid: 90 x 40 (wide crossing, the level-2-of-Westlands run shape)
- par_value: 4
- time_bonus_limit: 3500
- weather: FORCED SNOW (banks and ice lanes are snow-painted; hundreds
  of snow tiles >= 40; the blizzard sells the break-up).

## STORY POSITION

The river below Thornby, frozen — barely. Spring is a lie the ice tells
in the afternoons. The route across is three braided ice causeways, and
the middle one is already open water where the company's OWN pay chest
sat overnight: warm coin melts its footing. That is the level's story
beat and the act's warm-coin line — the metal is not just strange, it is
ACTIVELY warm, and from here the company knows it. The east bank road
climbs toward Ashfall (17). Turning back to Thornby is declining the
crossing (backtrack exit = the withdraw flow).

"Collapsing route" mechanics honestly stated: the engine has no timed
terrain — the collapse is (a) pre-broken ice forcing lane changes,
(b) bounded pursuit waves waking BEHIND the crew at 350/700/1000 so
lingering gets more dangerous the longer the crossing takes, and
(c) CAN_EXIT so the crew never has to clear the map. Same honest recipe
as Westlands' flight levels.

## TERRAIN PLAN (floor 0, the only floor)

Concept: west bank (muster) -> the river (x29..63, open water with three
snow-ice causeways braided by connectors) -> east bank (the climb-out
and the road to Ashfall).

Paint order (before smooth_world):
1. Snow fill: paint_snow(0, 0, 89, 39) — banks first.
2. The river: paint_rect(29, 0, 63, 39, PIX_WATER1) — impassable black
   water, bank to bank, edge to edge.
3. Ice causeways (paint_snow OVER the water — snow tiles over a water
   pattern read as river ice, per the skeleton's note):
   - NORTH lane (short, guarded): paint_snow(29, 8, 63, 11).
   - MID lane, BROKEN: west half paint_snow(29, 19, 45, 22); open lead
     (water stays) x46..49; east half paint_snow(50, 19, 63, 22).
   - SOUTH lane (long, intact): paint_snow(29, 30, 63, 33).
   - Connector N<->M: paint_snow(44, 12, 45, 18).
   - Connector M<->S: paint_snow(52, 23, 53, 29).
   - The islet (a gravel bar between N and M): paint_snow(46, 14, 49, 17);
     islet neck to the N lane: paint_snow(47, 12, 48, 13).
4. West bank pines (PIX_TREE_M1): (2,2)-(8,8), (2,31)-(8,37).
5. East bank bluffs (PIX_WALL2): (70,0)-(89,6) and (70,33)-(89,39) —
   the climb-out funnels to the middle road.
6. East bank pines: (66,2)-(69,5).
7. Scrub through the bank snow (PIX_GRASS_DARK_1): (12,12)-(18,17),
   (74,24)-(80,29).

smooth_world(w), then post-smooth (autotiler-inert):
8. The ford road (paint_path): west approach paint_path(4, 19, 28, 20);
   east climb-out paint_path(64, 19, 86, 20).
9. Waymarks (dead torches relit by the crew's passing — decorless base
   accents): paint(28, 18, PIX_TORCH1), paint(28, 21, PIX_TORCH1) at the
   west ice-edge; paint(64, 18, PIX_TORCH1), paint(64, 21, PIX_TORCH1)
   at the east ice-edge.
10. scatter_boulders(0, 10,2, 27,37, 25) — west bank moraine only
    (NO litter/jagged anywhere: this is a chase map, nothing may block).

Route audit (ground walker, lead -> east exit; the mid lane meets both
banks at y19..22, flush with the road at y19..20):
west bank (open) -> mid-lane west half x29..45 -> connector (44..45,
12..18) up to NORTH lane -> north lane x46..63 east -> east bank at
x64 (bank is open snow y7..32 between the bluffs) -> road y19..20 ->
exit. ALTERNATE: south route via M<->S connector (52..53, 23..29) from
the mid-lane's east half is reachable only from the EAST side of the
break — so southbound weaving goes: mid west -> N connector -> north
lane -> (either run the north gauntlet, or drop back down at the islet
neck is N-side only) — plus SOUTH lane direct from the west bank at
x29, y30..33, its own full crossing. THREE viable braids; the south
lane is the unguarded long way, the north lane is short but held, the
mid lane forces a lane change at the break. All verified contiguous on
the paint rects above.

## DECOR AMBIENCE (decor plane, all non-blocking)

- DECOR_BONES frozen INTO the ice (last year's crossing): hand-set
  (35,10), (58,9) north lane; (33,21), (55,20) mid lane; (40,32),
  (60,31) south lane; (47,16) islet.
- DECOR_PEBBLES on the bank roads: Path tiles only, rect (0,0)-(89,39)
  mod 9 (a ford road is gravel).
- DECOR_SHRUB on the bank scrub patches ONLY ((12,12)-(18,17) and
  (74,24)-(80,29) mod 5, dark-grass ground class) — nothing concealing
  on the ice; the lanes stay honest.
- NO decor of any kind on the connectors (they are the choke cells).

## ARMIES (team 2; guards via npc_flags bit1; every dormant wave wakes BEHIND the lead marker)

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|
| 1 | 2 | ORC (bank wolves, lane-mouth tolls) | 6 | 6 | YES | N mouth (30,8),(30,11); M mouth (30,19),(30,22); S mouth (30,30),(30,33) — 2x2 bodies at lane EDGES, each 4-wide lane keeps 2 clear tiles for a sprinting 2x2 runner | 0 | no |
| 2 | 2 | GHOST (ice wraiths) | 4 | 6 | YES | ON the lanes: (40,9) north, (38,20) mid-west, (56,21) mid-east, (44,31) south | 0 | YES |
| 3 | 2 | BARBARIAN (islet wards) | 2 | 6 | YES | islet (47,15),(48,16) | 0 | no |
| 4 | 2 | BARBARIAN (the far shore's toll) | 4 | 7 | YES | east climb-out: (66,9),(66,30),(67,18),(67,21) | 0 | no |
| 5 | 2 | GHOST (the river dead, pursuit 1) | 3 | 6 | no | west edge: (2,18),(2,21),(3,20) | 350 | YES |
| 6 | 2 | GHOST (pursuit 2) | 3 | 7 | no | (1,19),(3,17),(3,22) | 700 | YES |
| 7 | 2 | BARBARIAN (creditors' men, pursuit 3) | 4 | 6 | no | (2,16),(2,23),(4,18),(4,21) | 1000 | no |

NO GENERATORS (bounded pursuit; an endless den behind a run level just
farms attrition — the Westlands level-2 lesson applies even without
SAVE_ALL, because slow crews get ground down past fun).

GHOST specials_disabled EVERYWHERE, deliberately: the scare-wail
force-marches every foe ~6-7 tiles through terrain (pure distance, no
LOS). Over open water that can shove walkers onto unstandable tiles or
strand them mid-river — wedge city. Sheathed on all 10 ghosts; they fly
and fight, they do not wail.

Ghost placement rule (the Westlands L11 lesson — "the moat is the
ward" removal): NO water-locked flyers. Every wraith stands ON a snow
lane, so the footing audit passes and every living is A*-reachable from
the lead marker; self-check reachability allowlist EMPTY.

Totals: team 2 = 26 livings; 0 generators; team 0 places nothing.
Delayed spawns: 10 (350/700/1000 — dormant, NEXT WAVE HUD reads as the
ice groaning behind). MAXOBS ledger: 26 livings + 10 markers + 2 exits +
7 treasures = 45 objects; huge headroom (a run level stays light).

## HEROES / NAMED

None placed. Nothing on this map is named, so nothing can trip loss
logic; the pay chest travels in fiction only (the briefing carries it —
placing a bearer-style cargo here would demand SAVE_ALL and this level
is the one breather where the company risks only itself).

## START MARKERS (10; lead FIRST; west bank open snow, 2x2 clearance; ALL at x >= 6, clear of the x1..4 pursuit cells)

1. (9, 20) — LEAD, on the ford road, facing the ice
2. (6, 16)  3. (6, 24)  4. (8, 16)  5. (8, 24)
6. (11, 17)  7. (11, 23)  8. (13, 20)  9. (6, 20)  10. (13, 24)

## TREASURE

- The islet cache (a sunken toll-cart's strongbox): GOLD_BAR (46,14),
  GOLD_BAR (49,17), MAGIC_POTION (48,14) — warm coin again; the islet
  ice around it is authored as the widest melt (the mid-lane break sits
  beside it — the coin did that).
- Dropped supplies on the lanes (a fighting company eats mid-run):
  DRUMSTICK (36,9) north lane, DRUMSTICK (42,20) mid lane,
  DRUMSTICK (50,31) south lane.
- SPEED_POTION (28,20) at the west ice-edge — the "go NOW" prop.

## EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (86, 20) | 17 — east bank, the road up to Ashfall Gate (CAN_EXIT: usable while foes remain) |
| 0 | (5, 27)  | 16->15 backtrack — the Thornby road, west bank (clear of pursuit cells x1..4/y16..23 and of all markers; declining the crossing = the withdraw flow) |

## INTERCEPT GEOMETRY (verify on the built package with the A* solve)

Speeds (px/tick): soldier 4, ghost 4 (flies straight over water),
barbarian 3, orc 3. GRID_SIZE 16.

- Crew nonstop route (lead (9,20) -> exit (86,20), mid->N-connector->
  north-lane braid): ~87 tiles incl. the lane change ≈ 1392 px ≈ 348
  ticks. South-lane braid: ~92 tiles ≈ 368 ticks.
- Pursuit 1 (ghost, spawn x2..3, delay 350, straight flight ~84 tiles =
  336 ticks): at the east exit tick ~686 — x1.97 the nonstop crew. It
  chases; it never holds the finish.
- Pursuit 2 (delay 700): at the exit ~1036, x2.98.
- Pursuit 3 (barbarian, speed 3, delay 1000, ~83 tiles = 443 ticks on
  the road+ice): at the ice edge ~1150+ — it only ever meets a crew
  that stopped to farm the banks.
- Placed guards are all ON the route by design (lane tolls); the far
  shore's four lvl-7 barbarians are the one fight a runner cannot fully
  skip — they guard a 26-tile-wide bank funnelled by the bluffs to the
  y7..32 window, so a crew can pick an angle but not a bypass.

## BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, thaw-that-lies. The       (27)
ford is ice and the ice is        (26)
going. The pay chest melts its    (30)
own footing; warm coin, warm      (28)
grief. Losses on the ice stay     (29)
there. Cross fast, count later.   (31)
```

Lines 3-4 are the warm-coin thread made physical (the metal melts ice);
line 5 is the ledger's loss-accounting register doing the level's dread.

## BALANCE NOTES / CALIBRATION GATE (curve position: crew 7, winter's exit)

Curve: crew 7 (the meta band for 16 is 7-8; 17 opens the Reckoning at 8).
Bracket sweep at {6, 7, 8} x seeds {42, 1337, 2025} x rosters {A, B}.

- Battle shape: a rolling crossing. Lane-mouth wolves tax the entry
  choice; wraith + islet fights tax greed; the break forces one lane
  change; pursuit waves 350/700/1000 punish lingering; the far-shore
  toll is the one mandatory fight, sized for a crew that arrives mostly
  whole.
- EXIT GATE (the F4 exit form): the 8-mixed roster at crew 7 holds
  >= 50% strength (>=4/8) at tick 900 on 3/3 seeds — 900 is 2.4-2.6x
  the nonstop-crossing ETA (348-368 ticks), inside the escape-viability
  window convention. The 4-soldier roster is the recorded floor and may
  sag (brawlers cannot kite wraiths).
- No protectee -> no SAVE_ALL sub-gate. The harness never exits (AI
  cannot use exits): read crew-strength-at-900 and the foe-decay curve,
  not game_ended; crew deaths after a would-have-exited window are the
  documented artifact.
- Withdraw check: the backtrack exit at (5,27) must trigger the
  withdraw prompt once 15 is completed and 16 is not (exit_on_eat
  destination-completed path).
- Structural watches: connector chokes (2-wide) — watch for wedged
  melee pairs on (44,12..18)/(52..53,23..29) and widen to 3 if sweeps
  wedge; guards at lane EDGES never block both clear tiles (2x2 runner
  rule, asserted by the footing/route audit); no walker may ever path
  onto bare PIX_WATER1 (impassable — the audit's job); sheathed ghosts
  must still fight (specials off, weapons on).
- Weather smoke: >=40 snow tiles trivially; WeatherKind::Snow forced.
- Pins to add: roster row {16, t0 0, t2 26, gens 0, markers 10, exits
  {17, 15}}, delayed 10, type_bits 1, all-ghost specials_disabled pin,
  briefing 6 lines.
