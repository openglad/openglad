# The Long Season — campaign meta (yaml text, icon, story bible, curve)

Campaign id **org.openglad.longseason** (satisfies is_safe_campaign_id:
alnum._-). Title "The Long Season" is distinct from every shipped campaign
title (gladiator/ctf/arenas/tryxian/concept/westlands), so the
select-screen dedup helper never suffixes " [raw.id]". Package built by a
new `tools/longseason_mapgen` clone of westlands_mapgen (multi-file: one
TU per season + builders.h; ExpectedLevel rows; full self-check). New
helpers this campaign needs beyond the westlands set:
- `paint_ash_open(grid, rect)` — ash checker that skips non-ground
  (smoothed wall/tree) cells (level 17's camp plain).
- `place_named_foe(...)` — named team-2 enemy (level 18's The Founder;
  westlands precedent: The Lurker, Moon Warden).

## campaign.yaml

```yaml
format_version:  1
title:           The Long Season
version:         1
first_level:     1
suggested_power: 0
authors:         OpenGlad
contributors:    

description:     |
    The Brass Kettle Company takes
    what work the year brings: flood
    dikes in spring, two armies in
    summer, mines in autumn, snow
    passes in winter. Every employer
    pays in the same strange warm
    coin, and every road bends toward
    the foundry that struck it.
    A year of wages. Keep the book;
    collect at the gate.
```

(10 description lines, each <=34 chars, matching the concept/westlands
yaml visual width. suggested_power 0 — the company starts as nobodies and
the calibration contract below assumes a fresh team.)

## 32x32 icon — the brass kettle sigil (procedural, our.pal indices)

A round-bellied brass kettle on a black field, one warm coin glowing
beside it. Palette notes (from the westlands tile work's decoded our.pal):
index 0 = black field; 128-130 = the bright end of the warm orange-brown
ramp (brass body); 133-134 = dark browns (shadow/iron); 136-138 = tan
ramp (optional mid-brass); 24-28 = greys (steam, ground line);
**238-239 = the yellow end of the STATIC fire-ramp copy** (brass glint,
coin face). HARD RULE: no cycled bands anywhere — nothing in 208-223
(water) or 224-231 (fire); the sigil must not shimmer. The static copy
232-239 is safe. Verify ramp directions visually once against our.pal
(if 238/239 read too red for "brass", fall back to 128 body + 31 white
glints).

Paint plan (`put(x, y, c)` on a `make_grid(32, 32, 0)`):

1. Field: leave 0 (black) everywhere.
2. Kettle body (129, warm brass), row spans:
   y12: x12..19; y13: x10..21; y14..21: x9..22; y22: x10..21;
   y23: x11..20; y24: x13..18.
3. Belly highlight: for (x,y) in (11..13, 14..16) → 128;
   specular dot put(12, 15, 238).
4. Base shadow: re-put rows y23..24 spans with 133 on their outer 2
   pixels each side (x11..12 / x19..20 at y23; x13..14 / x17..18 at y24).
5. Rim/collar: y11: x12..19 → 130. Lid knob: (15..16, 9..10) → 129;
   put(15, 9, 238).
6. Spout, out to the left: put(8,13,129); put(7,12,129); put(6,12,129);
   mouth put(5, 11, 130).
7. Handle arc (134, dark iron): (11,8),(20,8) anchors; arc
   (12,7),(13,6),(14,5),(15,5),(16,5),(17,5),(18,6),(19,7).
8. Steam wisps (greys): put(14,3,26); put(17,2,27); put(15,1,28).
9. THE WARM COIN, lower right — the campaign's mystery on the sigil:
   disc rim at (24..27, 25..28) edge cells → 130; core (25..26, 26..27)
   → 238; glint put(25, 26, 239).
10. Ground line: y25: x10..21 → 24 (dark grey).

Reading: a squat pot-bellied kettle with spout, iron handle, and three
steam wisps, plus one glowing coin at its foot, on pure black. ~140 lit
pixels; reads clearly at 32x32; brass vs coin vs steam are three distinct
value groups.

## STORY BIBLE (the voice — binding for all 19 briefings)

Every briefing is an entry in the company ledger of the Brass Kettle
Company, written by The Sergeant (the narrating hand; NEVER placed as a
unit). Entries open with "Ledger," plus a season or occasion ("Ledger,
first thaw." / "Ledger, ash season.") and stay practical, wry, soldierly:
coin in, coin owed, men lost, weather complained about. Losses go in the
book, dryly; pay is named in real terms (grain, a dry roof, beer). NO
epic-prophecy register — that was Westlands; no fate, no doom, no
portents, no exclamation marks; numbers spelled small; sentence fragments
welcome ("Pay is grain."). Every entry opens "Ledger," plus a season or
occasion. THE WARM-COIN THREAD IS MANDATORY: every
level's briefing carries at least one line tying pay/coin/metal to the
arc, and it escalates by season — spring: an odd coin turns up in pay;
summer: the coin is common and nobody says who mints it; autumn: the
seams and the coin are warm to the touch; the Reckoning: the source (the
mint striking the city's own bones); epilogue: one coin kept, nailed over
the door. Geography is grounded, non-epic, spelled identically in every
entry: Weycombe, Saltmere, the Grey Tolls, Ashfall, the Undermill,
Thornby (the snowed-in village of 15, named in 14-16).
People are named by role or company nickname and MUST fit the 11-char
.fss unit-name field where placed: Kettle (6), Assessor (8 — the
skeleton's "The Assessor" is 12 and does NOT fit as a unit name; briefing
TEXT may say "the assessor"), The Reeve (9), Long Tom (8), The Founder
(11 exactly — the skeleton's "Foundry Master" is 14; briefing text may
say "the Foundry Master" only where the 33-char line allows). Employers
are institutions, not villains: the reeve, the temple, the quartermasters,
the crown, the Foundry. Character continuity: Kettle is placed in 4/9/18
only, is FAMILY_SOLDIER in all three placements (one face all year),
and is protect-OPTIONAL everywhere (never a SAVE_ALL loss
condition); the SAVE_ALL protectees are exactly Assessor (4) and The
Reeve (15), each carrying npc_flags bit2 on their level and nobody else
— no other level sets a bit2 or the SAVE_ALL type (9's strongroom hold
and 13's wagon run are design gates, not type bits);
Long Tom (8) does not reappear after his level. Withdrawing (backtrack
exits) is always framed in-fiction as declining continued work — the
company can walk away from a contract, and the ledger notes it without
judgment.

## Difficulty curve (intended crew power entering each level)

"Crew power" = average hero level of a 4-hero crew; the calibration
harness sweeps {curve-1, curve, curve+1} x 3 seeds x 2 rosters
(4-soldier floor "A", 8-mixed gate roster "B") per the westlands F4
method. THE CAMPAIGN STARTS AT CREW LEVEL 1: a fresh team (new save,
level-1 characters, default difficulty) must beat levels 1 and 2 at
crew 1 — that is the anchor of the whole table.

| id | title | season | crew power | gate type | notes |
|----|-------|--------|-----------|-----------|-------|
| 1  | Mud Pay              | spring | **1** | kill    | tutorial-gentle; MUST gate at crew 1 |
| 2  | The Ferry Right      | spring | **1** | defense | hold the causeway; MUST gate at crew 1 |
| 3  | Saltmere Bell (opt)  | spring | 2  | kill    | multifloor dive; optional +1 rule |
| 4  | The Assessor         | spring | 2  | kill    | SAVE_ALL: Assessor (bit2); type 4 = kill-all win; HUB EXIT CHOICE |
| 5  | Two Banners          | summer | 3  | kill    | fields, guards, waves |
| 6  | The Hay War          | summer | 3  | kill    | generator camps; fire decor |
| 7  | Grey Tolls (opt)     | summer | 4  | defense | toll fort; rejoins at 8; fort returns in 14 |
| 8  | The Paymaster Vanishes | summer | 4 | kill   | boss: Long Tom + guards; treasure chest |
| 9  | Ashfall Fair         | summer | 5  | defense | riot; protect the strongroom; Kettle placed |
| 10 | The Ledger Debt      | autumn | 5  | kill    | forced contract; 2-floor Undermill |
| 11 | Cold Seams           | autumn | 5-6 | kill   | lava-vein visuals; seams warm to touch |
| 12 | The Old Count's Vault (opt) | autumn | 6-7 | kill | hardest optional; loot to match |
| 13 | The Smelter's Road   | autumn | 6  | exit    | wagon escort under ambush; first snow dusting |
| 14 | The Long Toll        | winter | 7  | defense | the Grey Tolls in winter; Snow weather |
| 15 | Wolf Winter          | winter | 7  | kill    | SAVE_ALL: The Reeve (bit2); dusk wolf waves |
| 16 | The Frozen Ford      | winter | 7-8 | exit   | CAN_EXIT run; collapsing ice route |
| 17 | Ashfall Gate         | reckoning | **8** | kill | creditors' camp; gate-ward boss beat |
| 18 | The Warm Mint        | reckoning | **8** | exit | FINALE: 3 floors, CAN_EXIT climb; only team-1 level |
| 19 | Settlement Day       | epilogue | **8-9** | kill | dessert, NEVER spikes; exit loops to 1 |

Curve rules the level docs must respect:
- Levels 1-2 gate at crew 1 (fresh-team anchor); the spring act never
  exceeds crew 3.
- Optional contracts (3, 7, 12) are +1 harder than their season's median
  and pay it back in treasure; they MAY fail the standard gate at curve
  if their own documented design gate holds (westlands L9 precedent).
- The Reckoning (17-18) assumes the winter contracts are done: crew 8.
- 19 is dessert: it must clear at crew 7 too (a mauled crew still
  finishes the year) and on the 4-soldier floor roster.
- Skipping every optional must still land the crew at 8 by level 17
  (mainline XP/treasure budget is tuned to that; the optionals are
  gravy, not rungs).

### Calibration gates per level type (the F4 contract, inherited)

Bracket sweeps via the playtest harness (openglad_text --protocol,
census): crew {curve-1, curve, curve+1} x seeds {42, 1337, 2025} x
rosters {4-soldier A, 8-mixed B}; 6000-tick never-exiting AI runs; gates
evaluated on the 8-mixed roster, 4-soldier recorded as the pessimistic
floor. The AI stand-in crew cannot kite, heal deliberately, or exit —
judge relative difficulty and structural pathologies, not absolute
winnability.

- **kill-all** (win = extermination): 8-mixed at CURVE reaches
  level_done==1 within 6000 ticks on >=2/3 seeds AND 3/3 at curve+1;
  curve-1 may fail. (Levels 1, 19 additionally: see their stricter
  per-level gates — L1 must clear 3/3 at crew 1, L19 3/3 at 7/8/9.)
- **exit** (CAN_EXIT and/or escort): crew at curve holds >=50% strength
  at tick 900 (escape-viability window) on 3/3 seeds; where a SAVE_ALL
  protectee exists, the protectee NEVER dies while any crew member lives
  within tick 1800 (census-tick granularity), 3/3, both rosters.
- **defense** (standing allied garrison + assault waves): team-0
  (defenders + crew) alive at tick 3000 >= the level doc's band (default
  ceil(initial_t0/3)), 3/3 seeds.
- **SAVE_ALL sub-gate** wherever bit2 protectees exist (4, 15): no
  protectee-dies-before-crew-wipes run at curve.
- Structural sweeps everywhere: no sides that never engage, no generator
  flooding (trickle levels only — the F4 obmap-fix lesson: a lvl-5+
  generator outbreeds a curve crew), no waves landing after the battle
  is decided, delayed spawns hold level_done open and show in the NEXT
  WAVE HUD.

A cheap CI test pins this table (crew-power column + gate type per
level), westlands-style; the longseason_mapgen ExpectedLevel rows and the
committed .glad move in lockstep with any retune.

## F4 CALIBRATION TABLE (measured 2026-07-09 — the contract as shipped)

Method: scripts/longseason_playtest.sh (openglad_text --protocol; since
the F4 context-before-load fix it is bit-identical to the in-test sim),
crew brackets {curve-1, curve, curve+1} x seeds {42, 1337, 2025} x
rosters {4-soldier "A", 8-mixed 0,0,0,0,1,2,5,16 "B"}, 6000-tick
never-exiting AI runs; the contested levels were additionally
characterized over TEN seeds per bracket (the "wide" columns below)
because a single parked straggler flips a 3-seed verdict. Range rows in
the curve table resolve to the level docs' own gate positions: 11@5,
16@7, 19@8. Eight tuning batches, builders only (levels, guard/roam
postures, generator trims, spawn delays, four surgical terrain/placement
fixes); package + mapgen expectations + test pins moved in lockstep each
batch. CI pins the cheap invariant in
tests/unit/test_longseason_calibration.cpp (LongSeasonCalibration:
8-mixed crew at curve, seed 42, >= floor alive at tick 600; floors are
measured minima across the three pinned seeds — no dead slack).

| lvl | title | gate | crew | verdict | evidence (8-mixed @curve unless noted) |
|-----|-------|------|------|---------|----------------------------------------|
| 1 | Mud Pay | kill | 1 | PASS | clears 3/3 @1 (837/784/740), 3/3 @2; 4-sold 3/3 @1 — the fresh-team anchor holds |
| 2 | The Ferry Right | defense | 1 | PASS | t0@3000 [6,7,6] vs band 5; kill-viability @2 3/3 (~1450) |
| 3 | Saltmere Bell (opt) | kill | 2 | TRADE | viability @2 3/3 (full crew alive at 6000); clears @3 1-2/3 pinned, 5/10 wide — the losing runs end with 8/8 crew parked beside 1-5 full-HP stragglers (the parked-remnant stall, below) |
| 4 | The Assessor | kill+SAVE_ALL | 2 | PASS | clears 2/3 @2, 3/3 @3; NO Assessor-dies-while-crew-lives run, either roster (the counting-nook + lvl-8 body-block ward) |
| 5 | Two Banners | kill | 3 | PASS | clears 3/3 @3 (1108-1295), 3/3 @4 |
| 6 | The Hay War | kill | 3 | PASS | clears 3/3 @3 (2582-3563, all 3 tents torn out), 3/3 @4 |
| 7 | Grey Tolls (opt) | defense+kill | 4 | PASS | t0@3000 [7,6,6] vs band 5 (4-sold [6,6,4]); kill 2/3 @4, 3/3 @5 |
| 8 | The Paymaster Vanishes | kill | 4 | TRADE | 0/10 wide @4 AND @5; crews reach the hollow and kill 17-19 of 23 (foes 23->4-6), then park — the last remnant set always contains the INVISIBLE lvl-7 Long Tom. The westlands L8 precedent, accepted |
| 9 | Ashfall Fair | defense | 5 | PASS | t0@3000 [8,10,10] vs band 4; kill 3/3 @5 (~1220), 3/3 @6 |
| 10 | The Ledger Debt | kill | 5 | PARTIAL | clears 5/10 wide @5, 6/10 @6 (2/3 pinned @6); crew survives 10/10 both brackets — the coin-flip is the parked-remnant lottery, not the fight |
| 11 | Cold Seams | kill | 5 | PASS* | 2/3 pinned @5 (gate met); 9/10 wide @5 AND @6 — the pinned @6 triplet (2/3 vs the 3/3 letter) is one unlucky draw |
| 12 | The Old Count's Vault (opt) | kill | 6 | TRADE | crew-alive@900 @6 3/3 (its own documented survival gate; 8/8 alive in most runs); clears 0/10 — the ACT_GUARD giant wall + the lvl-9 Count park the AI floor by design ("the giants HOLD their posts" is the doc's pin) |
| 13 | The Smelter's Road | exit | 6 | PASS | crew@900 [6,6,4] >= 4 3/3; Ore Wagon violations: none, either roster |
| 14 | The Long Toll | defense | 7 | PASS | t0@3000 [9,7,5] vs band 5 (4-sold [8,5,7]) |
| 15 | Wolf Winter | kill+SAVE_ALL | 7 | PASS | clears 3/3 @7 (2426-2572), 3/3 @8; NO Reeve violation, either roster |
| 16 | The Frozen Ford | exit | 7 | PASS | crew@900 [7,7,8] >= 4 3/3 (4-sold [3,3,3]); unattended foes never engage — the harness cannot exit, by design |
| 17 | Ashfall Gate | kill | 8 | TRADE | crew survives 9-10/10 wide with 5-8 alive@900; corridor, camps and both waves die (foes 49->27); clears 0/10 — the AI floor parks at the DELIBERATE gate-ward wall (2 lvl-7 golems + muscle, the doc's boss beat) |
| 18 | The Warm Mint | exit | 8 | PARTIAL | crew@900 >= 4 on 5/10 wide (pinned [3,5,1]); three-way PASSES (t1 AND t2 strictly declining by 3000 every seed, foes 51->33); Kettle violations: none; the gatehall brawl decides the rest |
| 19 | Settlement Day | kill | 8 | PASS | clears 3/3 at 7 AND 8 AND 9 (590-620 ticks); 4-sold 3/3 @8 — the dessert never spikes |

PASS* = the letter of the 3-seed gate missed on one triplet draw; the
10-seed rate is at or above 90%.

### The documented trades (and why they are engine floors, not levels)

Every TRADE above shares one measured failure mode, characterized during
the sweeps: **the brawler-AI stand-in parks**. Late in a battle, wounded
survivors and remote ACT_GUARD posts (or an invisible foe) stop seeking
each other — state dumps show frozen tableaux of full-HP guards and idle
crews 9-20 tiles apart for 4000+ ticks, on both teams, including at
DOUBLE curve (L8 at crew 8, L12 at crew 9 — power is not the variable).
Narrow carves compound it: two-tile tubes deadlock opposing 2x2 walkers
(L8's bands, pre-fix). A human player ends all of these runs trivially;
the harness cannot exit, kite, or sweep corners. This is the same
engine-behavior floor War of the Westlands shipped against (its L8/L9
kill gates, 0/3 at every bracket, documented and accepted); the Long
Season trades are confined to one optional dive (3), one optional vault
whose OWN survival gate passes (12), the invisible-boss joke level (8),
and the two Reckoning set pieces whose survival/three-way/protectee
gates all pass (17, 18).

### What the calibration changed (builders only, lockstep each batch)

- Foe levels shed 1-3 across the mid-campaign: the passing westlands
  ratio is mob ranks at curve-2..curve-4 with elites at curve..curve+1,
  because a placed NPC of level N outweighs a crew guy of level N by
  roughly three levels.
- Garrisons hardened to westlands door-ward weight (lvl 6-8 anchors on
  2/7/9/14/15) — the AI crew abandons every fort, so forts must hold
  themselves.
- All generators trimmed to lvl 1-2 trickles (the F4 obmap-fix lesson).
- Big-slime SPLIT sheathed on 10/11 (a lost run compounded to 143 of the
  150 MAXOBS); small/medium splits sheathed on 10 after they alone still
  flooded lost runs.
- Guard/roam repostures: remote posts that parked became skirmishers
  (L3 crypt/belfry, L5 hedges, L8 ring, L10 mothers, L11 whole-mine
  shifts with delayed second waves); rush mobs that wiped crews became
  posts (L17 corridor and camps, L18 furnace line).
- Four surgical fixes: L3's ghost drifters became ground families
  (flyers over the flood are permanently unreachable); L4 grew a walled
  counting nook whose mouth one lvl-8 ward body-blocks (the westlands
  hut pattern) plus a third ward on the approach; L8's cliff bands got
  two 4-tile goat scrambles (two-walker width — narrower tubes
  deadlock); L18's creditor ranks moved from the crew's pocket to the
  gatehall doors, pre-engaged with the mint, and Kettle anchors
  mid-pocket.
- 11@5, 16@7, 19@8 resolve the meta table's range rows (the level docs'
  own gate positions); scripts/longseason_playtest.sh carries the same
  table.

### Exit graph, Reckoning tail (this doc's levels)

16 → 17 (the road ends at the gate); 17 → 18 (the gate) + backtrack → 16
(withdraw); 18 → 19 (the master ledger) + backtrack → 17 (withdraw,
floor-0 doors); 19 → 1 (the loop — no backtrack; the mint burns behind).
The hub/branch graph for 1-16 belongs to those levels' docs; the skeleton
is binding: 3, 7, 12 are optional contracts, 4 is the first HUB EXIT
CHOICE, and every withdraw is "declining continued work" in the ledger.
