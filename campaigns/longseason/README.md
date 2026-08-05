# longseason — provenance and campaign design

GENERATED CAMPAIGN. `scen/`, `pix/`, `campaign.yaml` and `icon.png` are
regenerated wholesale by `tools/longseason_mapgen`
(`scripts/generate_longseason_campaign.sh`) — do not hand-edit them; port
changes into the generator. Every generated scen carries the
`SCEN_TYPE_GENERATED` bit in its `.fss` header (the level editor warns on
open), and the CI `campaign-drift` job reruns the generator and fails on
any diff.

This file is repo documentation: `scripts/make_glad.py` excludes README.md
from the composed `.glad` archive.

The rest of this file is the campaign's design record — the metadata and
story bible, the original story skeleton, and the per-level design docs
the generator is authored against. `tools/longseason_mapgen` and
`tests/unit/test_longseason_calibration.cpp` cite these sections by name.

## Contents

- [Campaign metadata — yaml text, icon, story bible, curve](#campaign-metadata--yaml-text-icon-story-bible-curve)
- [Story skeleton (Fable's design, draft 1)](#story-skeleton-fables-design-draft-1)
- [Levels](#levels)
  - [Level 01 — Mud Pay](#level-01--mud-pay)
  - [Level 02 — The Ferry Right](#level-02--the-ferry-right)
  - [Level 03 — Saltmere Bell (OPTIONAL contract)](#level-03--saltmere-bell-optional-contract)
  - [Level 04 — The Assessor (HUB; the campaign's FIRST protect job)](#level-04--the-assessor-hub-the-campaigns-first-protect-job)
  - [Level 05 — Two Banners](#level-05--two-banners)
  - [Level 06 — The Hay War](#level-06--the-hay-war)
  - [Level 07 — Grey Tolls (OPTIONAL)](#level-07--grey-tolls-optional)
  - [Level 08 — The Paymaster Vanishes](#level-08--the-paymaster-vanishes)
  - [Level 09 — Ashfall Fair](#level-09--ashfall-fair)
  - [Level 10 — The Ledger Debt  (AUTUMN opens: the forced contract)](#level-10--the-ledger-debt--autumn-opens-the-forced-contract)
  - [Level 11 — Cold Seams  (the deep mine; the metal is wrong)](#level-11--cold-seams--the-deep-mine-the-metal-is-wrong)
  - [Level 12 — The Old Count's Vault  (OPTIONAL: the side venture)](#level-12--the-old-counts-vault--optional-the-side-venture)
  - [Level 13 — The Smelter's Road  (ore-wagon escort; first snow)](#level-13--the-smelters-road--ore-wagon-escort-first-snow)
  - [Level 14 — The Long Toll (WINTER, hold the pass)](#level-14--the-long-toll-winter-hold-the-pass)
  - [Level 15 — Wolf Winter (SAVE_ALL: The Reeve; dusk waves)](#level-15--wolf-winter-save_all-the-reeve-dusk-waves)
  - [Level 16 — The Frozen Ford (CAN_EXIT collapsing-route run)](#level-16--the-frozen-ford-can_exit-collapsing-route-run)
  - [Level 17 — "Ashfall Gate" (THE RECKONING, part 1)](#level-17--ashfall-gate-the-reckoning-part-1)
  - [Level 18 — "The Warm Mint" (CAMPAIGN FINALE)](#level-18--the-warm-mint-campaign-finale)
  - [Level 19 — "Settlement Day" (EPILOGUE — full circle, loops to 1)](#level-19--settlement-day-epilogue--full-circle-loops-to-1)

## Campaign metadata — yaml text, icon, story bible, curve

Campaign id **longseason** (satisfies is_safe_campaign_id:
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

### campaign.yaml

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

### 32x32 icon — the brass kettle sigil (procedural, our.pal indices)

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

### STORY BIBLE (the voice — binding for all 19 briefings)

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

### Difficulty curve (intended crew power entering each level)

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

#### Calibration gates per level type (the F4 contract, inherited)

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

### F4 CALIBRATION TABLE (measured 2026-07-09 — the contract as shipped)

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

#### The documented trades (and why they are engine floors, not levels)

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

#### What the calibration changed (builders only, lockstep each batch)

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

#### Exit graph, Reckoning tail (levels 16-19)

16 → 17 (the road ends at the gate); 17 → 18 (the gate) + backtrack → 16
(withdraw); 18 → 19 (the master ledger) + backtrack → 17 (withdraw,
floor-0 doors); 19 → 1 (the loop — no backtrack; the mint burns behind).
The hub/branch graph for 1-16 belongs to those levels' sections; the skeleton
is binding: 3, 7, 12 are optional contracts, 4 is the first HUB EXIT
CHOICE, and every withdraw is "declining continued work" in the ledger.

## Story skeleton (Fable's design, draft 1)

Campaign id longseason, title "The Long Season", first_level 1.
AN ORIGINAL STORY, no adapted saga. Voice: the original gladiator campaign's
LOGBOOK style — every briefing is an entry in the company ledger of the
BRASS KETTLE COMPANY, a broke, unfashionable free company taking whatever
work the year brings. Entries are practical, wry, soldierly: coin in, coin
owed, men lost, weather complained about. Names get invented (Weycombe,
Saltmere, the Grey Tolls, Ashfall) — grounded, non-epic geography.

The hook: the company takes a bad-luck contract chain across ONE YEAR —
spring floods, summer war, autumn underground, winter passes — and slowly
discovers every employer this season is paying with coin minted from the
same strange, warm metal... which traces to a foundry-city eating itself
hollow. The finale is a creditors' war inside the burning mint.

STRUCTURE: gladiator-style HUB with contract branches (the company can take
or skip side contracts; withdraw = declining continued work). ~18 levels
+ 4 optional contracts. Difficulty calibrated for a FRESH team (the Wave F
method): the company starts as nobodies.

### SPRING — the flood contracts (marsh/water, gentle start)
- 1 "Mud Pay" — clear drowned vermin (slimes/wolves-as-orcs lvl1) from the
  Weycombe granary dikes. Ledger: "Took the granary job. Pay is grain."
- 2 "The Ferry Right" — hold a flooded causeway (marsh decor, water) against
  river bandits so the toll-ferry runs. First odd coin appears in pay.
- 3 "Saltmere Bell" — OPTIONAL contract: dive the sunken bell-tower
  (multifloor: tower top above water, flooded floor below) for the temple.
- 4 "The Assessor" — escort the crown assessor through bandit country
  (SAVE_ALL named NPC "The Assessor", the campaign's first protect job).
  He pays in the warm coin and won't say who minted it. HUB EXIT CHOICE.

### SUMMER — the war contracts (fields/sieges; the company gets a name)
- 5 "Two Banners" — hired by BOTH sides' quartermasters by clerical error;
  fight one skirmish for the side that paid first (fields, guards, waves).
- 6 "The Hay War" — burn/save the harvest depots (torch decor everywhere,
  fire FX; MUST_DESTROY-flavored via placed generator camps).
- 7 "Grey Tolls" — OPTIONAL: garrison a mountain toll fort for a cut
  (defense level, generators, delayed assault waves).
- 8 "The Paymaster Vanishes" — the army's paymaster disappears with the
  chest of warm coin; track him into the hills; recover the chest (treasure
  objective + boss thief "Long Tom" + his guards).
- 9 "Ashfall Fair" — the war ends abruptly at the trade fair; a riot level
  (dense civilians-as-NPCs? use low-level neutral-team... keep: chaotic
  brawl vs looters, protect the fair's strongroom).

### AUTUMN — the under contracts (mines/catacombs, multifloor)
- 10 "The Ledger Debt" — the company's OWN debt is bought by the Foundry;
  forced contract: clear the Undermill (2-floor mill over race channels).
- 11 "Cold Seams" — deep mine sweep; the seams are warm to the touch
  (lava veins visual, first hint the metal is wrong). OPTIONAL branch 12.
- 12 "The Old Count's Vault" — OPTIONAL: crack a dead lord's vault
  (catacombs, bones decor, giant skeletons, big treasure, guard puzzles).
- 13 "The Smelter's Road" — escort ore wagons down the switchbacks under
  ambush (autumn = first snow decor dusting; guards + pursuit waves).

### WINTER — the pass contracts (snow, blizzards)
- 14 "The Long Toll" — hold the Grey Tolls pass IN WINTER (snow + Snow
  weather; the toll fort from 7 if taken — callback briefing either way).
- 15 "Wolf Winter" — relieve a snowed-in village (wolf packs, delayed
  waves at dusk; SAVE_ALL the village reeve who knows the mint's ledgers).
- 16 "The Frozen Ford" — cross the iced river as it breaks (water/ice=snow
  tiles over water pattern, collapsing route = CAN_EXIT run level).

### THE RECKONING — the foundry-city (ash/lava finale arc)
- 17 "Ashfall Gate" — the foundry-city gates: every unpaid company of the
  season is here demanding coin; fight through the creditors' camp (ash).
- 18 "The Warm Mint" — FINALE: 3-floor foundry (lava channels, brazier
  decor, cycled-flame everywhere): the mint has been coining the city's own
  foundations; the vault floor is collapsing into the melt. Objective: reach
  the master ledger at the crucible floor (exit at the top) while the
  creditors' war rages below. CAN_EXIT + the season's pay as treasure heaps.
- 19 "Settlement Day" — epilogue: back at Weycombe, the company (rich or
  broke) clears its OWN dike one last time (full-circle to 1, tiny level,
  exit = the winter quarters; loops to 1).

Ledger voice sample (level 1 briefing, 33-char lines):
  "Ledger, first thaw. Took the
   granary job off Weycombe's reeve.
   Pay is grain and a dry roof.
   The dikes crawl with what the
   flood left. Kettle says: earn
   the roof. Losses go in the book."

Cast: The Sergeant (player-facing narrator voice in briefings, never placed),
"Kettle" the company quartermaster (named hero in 4/9/18, protect-optional),
"The Assessor" (4, SAVE_ALL), "Long Tom" (8 boss, named enemy thief),
"The Reeve" (15, SAVE_ALL), the Foundry Master (18, named enemy archmage).

Uses everything we built: marsh/snow/lava/ash tiles, Snow weather, decor
set-dressing per season, guards, delayed waves + NEXT WAVE UI, multifloor,
SAVE_ALL, CAN_EXIT runs, hub branches, withdraw-as-declining-work.
Calibration: Wave F method from day one — fresh team beats 1-2 at L1.

## Levels

One section per level, in campaign order. These are the design docs the
`tools/longseason_mapgen` builders are written against; the terrain plans,
army tables, briefings and calibration gates below are normative for the
generator.

### Level 01 — Mud Pay

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

#### TERRAIN PLAN (floor 0 only; longseason_mapgen, clone of westlands API)

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

#### ARMIES (team 2 — the drowned vermin)

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

#### HEROES / NAMED

None. The Sergeant narrates; Kettle is quoted in the briefing but not
placed until level 4. All team-0 slots come from start markers.

#### START MARKERS (9; lead first; every anchor 2x2-clear)

1. (18, 20)  — LEAD, on the road by the dike ramp, facing the fields
2. (14, 17)
3. (14, 21)
4. (16, 15)
5. (12, 19)
6. (10, 21)
7. (16, 23)
8. (20, 17)
9. (22, 21)

#### TREASURE

- FAMILY_DRUMSTICK at (46,8),(52,8),(49,10) — the granary: pay is grain
- FAMILY_DRUMSTICK at (29,30) — a dropped sack on spur B
- FAMILY_SILVER_BAR at (8,17) — the reeve's hut: the season's one honest
  coin (the ledger will remember it)
- FAMILY_MAGIC_POTION at (19,4) — pool fringe, exploration reward

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (58, 20) | 2 — east road end (downriver, to the ferry) |

No backtrack — level 1 has no predecessor. Walkable route: lead (18,20)
east along road rows 20-21 to (58,20); pure path tiles.

#### BRIEFING (6 lines, each <=33 chars — counted; the skeleton's ledger
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

#### BALANCE NOTES (calibration gate: CREW 1 — campaign start)

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

### Level 02 — The Ferry Right

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

#### TERRAIN PLAN (floor 0 only)

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

#### ARMIES

##### Team 0 — the ferrymen (unnamed allied garrison; guards via
npc_flags bit1)

| team | family | count | level | guard | placement | delay | specials_off |
|------|--------|-------|-------|-------|-----------|-------|--------------|
| 0 | FAMILY_SOLDIER (ferrymen) | 4 | 2 | YES | causeway mouth posts (47,19),(47,21); landing posts (48,24),(52,24) | 0 | no |
| 0 | FAMILY_ARCHER (the toll-keeper) | 1 | 2 | YES | toll house interior (60,15) | 0 | no |

##### Team 2 — the river bandits (bounded waves; NO generators — every
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

#### HEROES / NAMED

None placed. (Kettle bites the coin in the ledger, not on the field.)

#### START MARKERS (10; lead first — the company forms at the causeway's
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

#### TREASURE

- The toll chest (toll house): FAMILY_GOLD_BAR at (58,14),(62,14);
  FAMILY_SILVER_BAR at (62,16) — the pay; one of these is THE warm coin
  (fiction only; treasure families carry no flag)
- FAMILY_MAGIC_POTION at (58,16) — the keeper's shelf
- FAMILY_DRUMSTICK at (47,26),(49,24) — the landing's stores

#### EXITS (the briefing names both forward roads in-fiction)

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

#### BRIEFING (6 lines, each <=33 chars — counted)

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

#### BALANCE NOTES (calibration gate: CREW 1)

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

### Level 03 — Saltmere Bell (OPTIONAL contract)

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

#### TERRAIN PLAN

init_world(level, 2, 50, 50).

##### Floor 0 — the drowned parish, pre-smooth:
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

##### Floor 1 — the belfry, pre-smooth:
7. paint_rect(0, 0, 49, 49, PIX_AIR) — open sky over the flood
8. paint_rect(24, 12, 33, 21, PIX_WALL2) — the tower's upper story

smooth_world(w), then post-smooth:

##### Floor 0:
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

##### Floor 1:
14. Belfry interior: paint_pavement(25, 13, 32, 20)
15. The bell-eye — open center over the dais:
    paint_rect(28, 16, 29, 17, PIX_AIR)
    FALL-LINE RULE: every floor-1 walkable cell adjacent to this AIR
    drops onto the floor-0 DAIS (pavement (27,15)-(30,18) covers the
    full projection) — standable ground, legal. The outside AIR is
    sealed off by the wall shell (never walkable-adjacent).
16. Belfry braziers: paint(25, 20, PIX_BRAZIER1);
    paint(32, 13, PIX_BRAZIER1)

##### Stairs:
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

#### ARMIES (team 2 — the drowned)

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

#### HEROES / NAMED

None. (An unnamed giant skeleton carries the boss beat; naming him would
add a SAVE_ALL hazard pattern for zero story gain — the cast's named
roles don't visit Saltmere.)

#### START MARKERS (8; lead first — the temple's boat grounds on the SW
islet; all anchors 2x2-clear on the dirt)

1. (9, 43)  — LEAD, at the street's mouth, facing east
2. (6, 41)
3. (6, 45)
4. (11, 41)
5. (11, 45)
6. (4, 43)
7. (8, 40)
8. (8, 45)

#### TREASURE (the optional contract PAYS — hoard placement is the reward
curve)

- Floor 1, the bell hoard (ringing the bell-eye): FAMILY_GOLD_BAR at
  (28,15),(29,15),(28,18),(29,18); FAMILY_MAGIC_POTION at (26,16)
- Floor 0, the crypt: FAMILY_SILVER_BAR at (15,18),(17,16),(19,18);
  FAMILY_INVIS_POTION at (19,17) — the diver's trick
- Floor 0, the graveyard: FAMILY_SILVER_BAR at (40,15);
  FAMILY_DRUMSTICK at (40,17),(44,17)
- Floor 0, the dais (visible through the bell-eye from above):
  FAMILY_MAGIC_POTION at (28,17)

#### EXITS (both on the landing islet — the boat is the only way in or out)

| floor | cell | destination |
|-------|------|-------------|
| 0 | (5, 41) | 4 — the temple's boat rows you east to the high road |
| 0 | (5, 45) | 2 — backtrack: back downriver to the ferry |

Walkable routes: trivial from every marker (same islet). The forward
exit means a company can take the dive AFTER the ferry and still make
the assessor's job — and 4's hub exit lets a company that skipped the
dive come BACK for it.

#### BRIEFING (6 lines, each <=33 chars — counted)

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

#### BALANCE NOTES (calibration gate: CREW 2 — optional, +1 over the
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

### Level 04 — The Assessor (HUB; the campaign's FIRST protect job)

- id: 4
- title: "The Assessor" (12 bytes)
- type_bits: SCEN_TYPE_SAVE_ALL (4) — kill-all win stays; the protect
  scope is pinned by npc_flags bit2 (see PROTECTED-BIT PLAN).
- floors: 1
- grid: 80 x 45
- par_value: 4
- time_bonus_limit: 5000

Escort the crown assessor through bandit country. His wagon has thrown
an axle at the barrow-road waystation; the company deploys around it,
breaks the ambush springs, and clears the road. He pays in the warm
coin and won't say whose stamp it is. HUB EXIT CHOICE: east to the
summer war roads (5) or south to the Saltmere jetty (3 — the optional
dive, take it now or never mind). Gates at CREW 2.

#### PROTECTED-BIT PLAN (the SAVE_ALL scoping rule, applied exactly)

- "Assessor" carries npc_flags bit2 (protected) — reserved[3] = 6
  (bit1 guard | bit2 protected). He is the ONLY bit2 walker on the map.
- Because at least one placed NPC has bit2, the engine watches ONLY
  bit2 walkers: Kettle (named, team 0) is deliberately NOT bit2 —
  protect-OPTIONAL per the cast sheet; his death stings the ledger,
  not the mission. The unnamed door-wards and any summons never count.
- Unit-name budget: the .fss name field holds 11 chars; "The Assessor"
  is 12 and would overflow (the Westlands "White Rider" precedent), so
  the PLACED unit is named "Assessor" (8 chars). Briefings say "the
  crown assessor" — the 33-char lines can afford the article.
- No flyers anywhere on this map BY DESIGN (bandits + a TENT generator,
  which spawns ground skeletons): the ghost scare-wail hazard that
  forced Westlands' raw-rock redoubts does not exist here. A walled
  waystation + lvl-4 door-wards is sufficient protection at crew 2.

#### TERRAIN PLAN (floor 0 only)

init_world(level, 1, 80, 45).

Pre-smooth paints:
1. West woods band with the road gap: paint_rect(0, 0, 5, 44,
   PIX_TREE_M1), then carve the gap paint_rect(0, 20, 5, 24, PIX_GRASS1)
2. NE copse: paint_rect(70, 0, 79, 6, PIX_TREE_M1)
3. The barrow field, north: paint_rect(8, 2, 30, 10, PIX_GRASS_DARK_1);
   barrow mounds paint_rect(12, 4, 15, 7, PIX_DIRT_DARK_1);
   paint_rect(20, 3, 23, 6, PIX_DIRT_DARK_1);
   paint_rect(26, 5, 29, 8, PIX_DIRT_DARK_1)
4. The south gorse: paint_rect(20, 34, 48, 42, PIX_GRASS_DARK_1)
5. The swollen stream, north-south (spring floods, still up):
   paint_rect(58, 0, 62, 44, PIX_WATER1)
6. The waystation, solid shell: paint_rect(36, 16, 50, 26, PIX_WALL2)

smooth_world(w), then post-smooth:
7. Waystation interior (pavement over wall carves it):
   paint_pavement(37, 17, 49, 25)
8. Gates (pavement over wall): west gate paint_pavement(36, 20, 36, 23);
   east gate paint_pavement(50, 20, 50, 23)
9. The barrow road: west segment paint_path(2, 21, 35, 22); east
   segment paint_path(51, 21, 78, 22) — the ford over the stream is the
   path-over-water span (58,21)-(62,22), the only crossing
10. The barrow lane, road to the field: paint_path(20, 7, 21, 20)
11. The south lane, road to the gorse and on to the water meadows
    (the Saltmere spur): paint_path(33, 23, 33, 43)
12. Marsh on the stream banks (marsh_over_grass, plain grass only):
    marsh_over_grass(55, 0, 57, 44); marsh_over_grass(63, 0, 65, 44)
13. The wagon (thrown axle, parked off the gate lane):
    paint(43, 19, PIX_BOULDER_2) — a blocking prop; lane rows 20-23
    stay clear
14. Waystation braziers: paint(38, 17, PIX_BRAZIER1);
    paint(49, 17, PIX_BRAZIER1)
15. Gate torches (outside the walls, off the road rows 21-22):
    paint(35, 19, PIX_TORCH1); paint(35, 24, PIX_TORCH1);
    paint(51, 19, PIX_TORCH1); paint(51, 24, PIX_TORCH1)

Decor ambience (non-blocking, after entity placement):
16. DECOR_BONES on the barrow field: (8,2)-(30,10) mod 7
    (DarkGrass+Dirt) — the diggings
17. DECOR_SHRUB on the gorse: (20,36)-(48,42) mod 9 (DarkGrass) —
    concealment IS the ambush country; kept south of row 35, entirely
    off the road and both lanes' required routes (lane x33 cells are
    Path, skipped by ground-class restriction)
18. DECOR_PEBBLES on road + lanes: (2,21)-(78,22), (20,7)-(21,20),
    (33,23)-(33,43), each mod 11 (Path)
19. scatter_boulders(w, 0, 6, 10, 30, 18, 17) — moor stones between
    field and road; scatter_boulders(w, 0, 64, 28, 76, 40, 19) — east
    bank scree. Road rows 20-24 excluded by both rects.

#### ARMIES

##### Team 0 — the halted column (guards via npc_flags bit1)

| name | family | level | guard | protected (bit2) | specials_off | placement |
|------|--------|-------|-------|------------------|--------------|-----------|
| "Assessor" (8 ch) | FAMILY_CLERIC | 4 | YES | YES — the only one | YES (he counts coin, he doesn't fight) | (45,17) — interior, north wall, off the gate lane |
| "Kettle" (6 ch) | FAMILY_SOLDIER | 3 | YES | no (protect-optional) | no | (38,24) — inside the west gate, south of the lane |
| unnamed door-wards | FAMILY_SOLDIER x2 | 4 | YES | no | no | (42,17),(47,17) — flanking the Assessor's post |

reserved[3] values: Assessor = 6 (guard+protected); Kettle and wards = 2
(guard). spawn_delay 0 for all team 0.

##### Team 2 — bandit country

| # | family | count | level | guard | placement (floor 0) | spawn_delay | specials_off |
|---|--------|-------|-------|-------|---------------------|-------------|--------------|
| 1 | FAMILY_THIEF (road rocks, west) | 3 | 2 | YES | (10,20),(16,23),(24,20) | 0 | no |
| 2 | FAMILY_ARCHER (ford watch) | 2 | 2 | YES | east bank: (63,20),(63,23) | 0 | no |
| 3 | FAMILY_SOLDIER (east road toll) | 3 | 2 | YES | (66,20),(66,23),(70,21) | 0 | no |
| 4 | FAMILY_ORC (bandit dogs) | 4 | 1 | no | gorse: (24,36),(30,38),(38,36),(44,38) | 0 | no |
| 5 | FAMILY_THIEF (gorse camp) | 3 | 2 | YES | (28,40),(34,40),(40,40) — lane x33 stays open | 0 | no |
| 6 | FAMILY_SKELETON (what the digging woke) | 4 | 1 | no | barrow mounds: (13,5),(21,4),(27,6),(17,8) | 0 | no |
| 7 | FAMILY_THIEF (the first spring — from the rocks BEHIND the column) | 4 | 2 | no | west gap mouth: (6,19),(6,24),(8,18),(8,25) | 300 | no |
| 8 | FAMILY_ORC (gorse spring) | 3 | 2 | no | gorse north edge: (30,33),(36,33),(42,33) | 700 | no |
| 9 | FAMILY_BARBARIAN (the toll chief's push) | 3 | 3 | no | across the ford: (74,20),(74,23),(76,22) | 1100 | no |
| 10 | FAMILY_SOLDIER (the push's second rank) | 3 | 2 | no | (72,19),(72,24),(78,24) | 1100 | no |

Generators (the diggers' camp — the coin they dig for is the arc's):
| family | team | level | floor/cell |
|--------|------|-------|------------|
| FAMILY_TENT | 2 | 2 | 0 / (24, 8) — barrow field; spawns ground skeletons ONLY (no flyers, keeps the protect job honest at crew 2) |

Totals: 32 team-2 livings + 4 team-0 placed = 36 livings, 1 generator.
Delayed spawns: 13. Guards: 11 team-2 posts + 4 team-0.
MAXOBS ledger: 36 livings + 1 gen + 10 markers + 8 treasures + 2 exits
= 57 objects + bounded TENT trickle — ample headroom under 150.

Footing + reachability: all anchors on grass/dark-grass/dirt/path/
pavement; nothing on water, woods, walls, or the wagon prop cell. Every
unit + the TENT A*-reachable from the lead marker: west pockets via the
road, barrow group via the lane, gorse via the south lane, east groups
via the ford path span. Empty allowlist.

#### START MARKERS (10; lead first — the company deploys INSIDE the
waystation around the wagon; all anchors 2x2-clear pavement)

1. (40, 21)  — LEAD, on the lane inside the west gate
2. (43, 20)  — beside the wagon
3. (40, 24)
4. (46, 21)
5. (43, 24)
6. (46, 24)
7. (40, 18)
8. (48, 20)
9. (37, 21)  — in the west gateway
10. (48, 24)

#### TREASURE

- The assessor's strongbox: FAMILY_GOLD_BAR at (44,18),(44,20) — THE
  WARM COIN, paid on the spot (fiction; see briefing)
- FAMILY_SILVER_BAR at (39,17) — Kettle's float
- FAMILY_DRUMSTICK at (45,25),(39,22) — the column's stores
- FAMILY_MAGIC_POTION at (14,4) — the diggers' loot, barrow field
- FAMILY_SILVER_BAR at (34,41) — the gorse camp's take
- FAMILY_DRUMSTICK at (56,21) — a dropped pack at the ford

#### EXITS (THE HUB — the briefing names both roads; per the campaign
graph this level exits FORWARD to 5 and BACK to the optional 3 only)

| floor | cell | destination |
|-------|------|-------------|
| 0 | (78, 21) | 5 — east road end, over the ford: the summer war roads |
| 0 | (33, 43) | 3 — south lane's end at the water meadows: the Saltmere jetty (take the dive contract now, or come back for the bell) |

Walkable routes from lead (40,21): east gate lane -> east road -> ford
path (58,21)-(62,22) -> (78,21); west gate -> road to x33 -> south lane
x33 rows 23-43 -> (33,43). The gorse camp thieves anchor at x28/34/40
row 40 with 2x2 footprints clear of lane column 33. The exit cell
(78,21) is left unoccupied (push soldiers anchor at (78,24)).

#### BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, mud month. Escort job:    (30)
the crown assessor, kept alive    (30)
through the barrow road. He pays  (32)
in the warm coin and will not     (29)
say whose stamp it bears.         (25)
War pay east; the bell south.     (29)
```

Warm-coin thread: lines 4-5 are the act's cliffhanger — the first
employer who KNOWS what the metal is and won't say. (The bandits dig
the barrows for the same coin; the TENT camp is that fiction on the
map.) Line 6 names the hub branches in-fiction: 5 east, 3 south.

#### BALANCE NOTES (calibration gate: CREW 2 + SAVE_ALL sub-gate)

- Battle shape: a defended-center clear. The crew wakes inside walls
  with the Assessor warded behind them; the first spring (tick 300)
  comes from BEHIND, through the west gap the column just walked — the
  protect lesson: don't empty the fort. Gorse spring at 700 tests the
  south gate lane; the toll chief's push at 1100 crosses the ford into
  whatever the crew has left. Clearing then requires three sorties:
  barrow field (smash the TENT), gorse camp, and the east toll posts.
- Gate (kill-all, curve crew 2, 8-mixed roster): level_done==1 within
  6000 on >=2/3 seeds @crew 2 AND 3/3 @crew 3; crew 1 may fail.
- SAVE_ALL sub-gate (the ONLY watched walker is the Assessor): no run
  at curve, EITHER roster, 3/3 seeds, where the Assessor dies while
  any crew member lives (census-tick granularity; deaths after a full
  crew wipe in never-exiting AI runs are the documented harness
  artifact). Kettle's death is NOT a loss condition — assert that a
  Kettle-only death does not emit EndGame(SCEN_TYPE_SAVE_ALL).
- Protection stack for the gate: walls + 2 lvl-4 wards + guard-bit
  Assessor (lvl-4 cleric hp) + zero flyers + TENT capped at lvl 2.
  If the sweep shows ward attrition, harden wards to lvl 5 before
  widening the walls (builders-only tuning, Westlands F4 discipline).
- 300-tick smoke: team 0 not extinct; Assessor alive (guard, interior);
  the first spring DORMANT at 250 and ACTIVE by 300 (delay 300
  round-trips); springs 700/1100 dormant, holding level_done at 0.
- Recipe C: A* lead -> both exits; lead -> TENT cell, gorse camp,
  ford watch, and east toll posts (proves both gates, the ford span,
  and both lanes). Footing audit: wagon prop cell (43,19) hosts no
  entity; door-ward/Assessor/Kettle footprints all interior pavement.
- HUB bookkeeping: completing 4 unlocks {5, 3} via harvested exits —
  a company that skipped the dive gets its second chance here; one
  that took it sees 3 already completed (the withdraw prompt path).

### Level 05 — Two Banners

- id: 5 (grid_file scen0005)
- title: "Two Banners" (11 bytes)
- type_bits: 0 (kill-all, then walk to exit). Protected-bit plan: NONE —
  no team-0 named NPC placed, no bit2 anywhere.
- floors: 1 (no PIX_AIR; fall-line rule trivially satisfied)
- grid: 60x40
- par_value: 3
- time_bonus_limit: 4000
- weather: roll (zero snow tiles; summer fields)

#### TERRAIN PLAN (floor 0, the only floor)

Concept: a wheat valley between two hired armies. The company deploys at the
blue camp's apron (west edge) and clears the russet skirmish line east across
two hedged field strips and a stream ford, ending at the russet palisade.
Summer war contract #1: fields, guards, one dormant reserve wave.

Paint order (before smooth_world):
1. Base fill grass (init_world(level, 1, 60, 40)).
2. Wheat strips (PIX_GRASS_LIGHT_1 reads as ripe grain):
   (10,4)-(26,14); (10,25)-(26,36); (36,4)-(52,14); (36,25)-(52,36).
3. Fallow headland (PIX_GRASS_DARK_1): north strip (10,0)-(52,2).
4. Hedgerow west (PIX_TREE_M1): x14-15, y0-16 and y23-39 (road gap y17-22).
5. Stream (PIX_WATER1): x30-31, y0-16 and y23-39 (ford gap y17-22).
6. Hedgerow east (PIX_TREE_M1): x44-45, y0-16 and y23-39 (road gap y17-22).
7. Pond in the SW fallow corner: paint_rect(18,30,24,35, PIX_WATER1).
8. Russet palisade (PIX_WALL2 shell): rect (50,12)-(58,27); west gate gap
   left open at (50,19)-(50,20) (carved post-smooth with pavement, below).

smooth_world(w), then post-smooth decor (autotiler-inert):
9. Blue camp apron: paint_pavement(2,17,5,23); cook-fires as decor:
   paint_decor DECOR_BRAZIER at (2,17) and (5,17) (blocking decor, both
   off the road rows 19-20 and off every marker footprint).
10. Russet camp interior: paint_pavement(51,13,57,26); gate doorway
    paint_pavement(50,19,50,20). Gate torches: paint_decor DECOR_TORCH1 at
    (49,17) and (49,22) (outside the gate lane, on grass).
11. The contract road (paint_path), west edge to the russet gate:
    paint_path(2,19,49,20). Ford planks are just the road rows crossing the
    stream gap (grass under, path over — y19-20 x30-31 stays walkable).
12. scatter_boulders(w, 0, 10,0, 52,2, 25) — stones in the fallow headland.

#### ARMIES (team 2 = the russet banner; team 1 EMPTY per convention)

| # | team | family | count | level | guard | placement (floor 0, tile) | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|---------------------------|-------------|--------------|
| 1 | 2 | SOLDIER (pickets) | 6 | 1+(i%2) | no | strip A (x16-29): (18,8),(22,12),(25,6),(18,28),(22,32),(25,26) | 0 | no |
| 2 | 2 | BARBARIAN (ford mercs) | 3 | 3 | no | east bank: (33,18),(34,21),(36,20) | 0 | no |
| 3 | 2 | ARCHER | 4 | 2 | YES | behind east hedge: (46,10),(46,14),(46,26),(46,30) | 0 | no |
| 4 | 2 | SOLDIER | 6 | 2 | no | strip B (x36-49): (38,8),(42,12),(47,6),(38,30),(42,26),(47,33) | 0 | no |
| 5 | 2 | SOLDIER (the sergeant) | 1 | 5 | YES | before the gate: (48,20) | 0 | no |
| 6 | 2 | ARCHER | 2 | 3 | YES | inside gate on pavement: (51,16),(51,24) | 0 | no |
| 7 | 2 | SOLDIER (reserve) | 5 | 2 | no | dormant NE headland: (52,2),(54,3),(56,4),(55,6),(57,7) | 500 | no |

Generators: none (first war level; generators debut at 6).
Totals: 27 team-2 livings, 0 generators. MAXOBS ledger: 27 livings +
10 markers + 5 treasures + 2 exits = 44 objects; huge headroom.

Treasure: FAMILY_DRUMSTICK at (3,18) and (4,22) (blue camp stores);
FAMILY_GOLD_BAR at (53,14) (the RUSSET purse — the second warm payment,
briefing calls it out); FAMILY_MAGIC_POTION at (21,29) (pond fringe);
FAMILY_DRUMSTICK at (52,25) (russet larder).

#### HEROES / NAMED

None. The Sergeant narrates; Kettle keeps the purses off-map. No named
team-0 unit anywhere (type 0, no SAVE_ALL interplay possible).

#### START MARKERS (10; lead FIRST; 2x2 clearance each; all on apron/road grass)

lead (5,20); then (3,19),(7,18),(7,22),(3,22),(9,20),(6,17),(9,17),(6,23),
(9,23). All anchors clear of the brazier decor at (2,17)/(5,17) (each
marker's 2x2 footprint checked cell-by-cell).

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (56,20) | 6 (inside the russet camp — take their muster road on) |
| 0 | (1,19)  | 4 (backtrack: the assessor's road west) |

Walkable route (lead -> forward exit): (5,20) east on road rows 19-20 ->
west hedge gap (x14-15 open y17-22) -> strip A -> ford (x30-31 open y17-22)
-> strip B -> east hedge gap -> gate (50,19)-(50,20) -> pavement -> (56,20).
Nothing on the route is wall/water/tree. Backtrack exit is 4 tiles west of
the lead marker on the road. Reachability: every living is on grass/path/
pavement connected to this route (archers row x46 stand between hedge x44-45
and palisade x50 — the road gap y17-22 connects them; NE reserve stands on
fallow/grass connected via strip B).

#### BRIEFING (6 lines; char counts 30/28/31/31/28/30)

```
Ledger, hay-month. Two banners
hired us for the same field.
Clerks' error. Kettle kept both
purses. The blue lot paid first
and their coin came up warm.
Break the russet line by dusk.
```

(Warm-coin thread: line 5. The both-purses gag pays off at level 8 when the
season's coin is stolen in one chest.)

#### BALANCE NOTES

Curve position: crew 3 (summer opens at 3 per the campaign_meta table;
the campaign gates 1-2 at crew 1 and the spring hub 4 at crew 2 — this
is the summer step up). Battle shape: three spaced fights west-to-east
(pickets -> ford mercs -> strip B + archer hedge), then a short guard fight
at the gate (sergeant + 2 archers never leave their posts, so the whole map
never piles on). The tick-500 reserve wakes behind the crew's line of
advance and holds level_done open — the NEXT WAVE HUD teaches waves here.

Calibration gate (kill-all, curve 3): 8-mixed roster reaches level_done==1
within 6000 ticks on >=2/3 seeds at crew 3 AND 3/3 at crew 4; crew 2 may
fail. 4-soldier roster recorded as floor. 300-tick smoke: team 0 not
extinct; team-2 alive >= 18 at tick 300 (east half untouched — asserts the
line fights stay staged); the 5 reserves still dormant at tick 300.
Footing/self-check: all placements on grass/path/pavement; archers at x46
have 2x2 footing between hedge and palisade (columns 46-47 clear, y10-30).

#### Decor ambience

- Road pebbles: scatter_decor (2,17)-(57,23) mod 11 (Path only).
- Wheat-margin shrubs: (10,4)-(26,36) mod 17 (LightGrass) and
  (36,4)-(52,36) mod 17 (LightGrass) — kept off the road band y17-22
  (ground-class restriction: road is Path, ford band is Grass — shrubs
  restricted to LightGrass never land on the battle lane).
- Fallow bones (an older season's skirmish): (10,0)-(52,2) mod 23
  (DarkGrass).
- Hand accents: DECOR_BRAZIER (2,17),(5,17); DECOR_TORCH1 (49,17),(49,22).

### Level 06 — The Hay War

- id: 6 (grid_file scen0006)
- title: "The Hay War" (11 bytes)
- type_bits: 0 (kill-all, then walk to an exit). NOTE: "burn the camps" is
  authored as three hostile TENT generators — GEN_EXIT (bit 2) is a dead bit
  with no sim consumer, so the objective is enforced de facto: the levy
  respawns until every camp is torched, so extermination REQUIRES killing
  the generators. Protected-bit plan: NONE (no team-0 NPCs).
- floors: 1 (no PIX_AIR)
- grid: 60x60
- par_value: 3
- time_bonus_limit: 4500
- weather: roll (zero snow tiles)

#### TERRAIN PLAN (floor 0)

Concept: a dirt-road cross through four hay quarters. Three walled harvest
depots (NW, NE, S) each hold a muster tent (TENT generator) — the "camps to
burn". Road patrols rove between them; a relief column marches in from the
east at tick 600. THE COMPANY EARNS ITS NAME HERE (briefing line 4-5).

Paint order (before smooth_world):
1. init_world(level, 1, 60, 60) — base grass.
2. Hay quarters (PIX_GRASS_LIGHT_1): NW (4,4)-(24,24); NE (36,4)-(56,24);
   SW (4,36)-(24,56); SE (36,36)-(56,56).
3. Old-battle fallow (PIX_GRASS_DARK_1): (4,31)-(20,34) (west of center,
   south of the road — bones decor lands here).
4. Depot shells (PIX_WALL2):
   - NW: rect (8,8)-(16,15)
   - NE: rect (42,8)-(50,15)
   - S:  rect (26,42)-(34,49)
5. Hedgerows (PIX_TREE_M1) framing the east-west road:
   - north side row: (4,26)-(24,27) with lane gap x11-13;
     (36,26)-(56,27) with lane gap x45-47
   - south side row: (4,33)-(24,34) — NOTE: overlaps the fallow strip;
     paint hedges AFTER the fallow rect so trees win; keep gap x11-13.
     (36,33)-(56,34) with gap x45-47.
6. Pond (PIX_WATER1): (48,33)-(54,37). To make room, the SE south hedge in
   step 5 is shortened to (36,33)-(46,34) (no overlap).

smooth_world(w), then post-smooth decor:
7. Depot interiors + gates (pavement over wall carves the doorway):
   - NW: paint_pavement(9,9,15,14); gate paint_pavement(12,15,12,15)
   - NE: paint_pavement(43,9,49,14); gate paint_pavement(46,15,46,15)
   - S:  paint_pavement(27,43,33,48); gate paint_pavement(30,42,30,42)
8. Roads (paint_path):
   - east-west: paint_path(2,29,57,30)
   - north-south: paint_path(29,2,30,41) (ends at the S depot gate lane;
     gate at (30,42) meets it)
   - depot lanes: paint_path(12,16,12,28) (NW); paint_path(46,16,46,28) (NE)
9. Burn-fires flanking each gate (the torch ambience the briefing sells):
   paint_decor DECOR_BRAZIER at (10,16),(14,16) [NW], (44,16),(48,16) [NE],
   (28,41),(32,41) [S] — all one tile OFF the gate lanes.
10. scatter_boulders(w, 0, 4,31, 20,34, 21) — stones in the fallow.

#### ARMIES (team 2 = the enemy levy; team 1 EMPTY)

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|---------------------|-------------|--------------|
| 1 | 2 | SOLDIER | 3 | 2 | YES | NW depot: (10,10),(14,10),(12,13) | 0 | no |
| 2 | 2 | ARCHER  | 2 | 3 | YES | NW depot: (9,12),(15,12) | 0 | no |
| 3 | 2 | SOLDIER | 4 | 2+(i%2) | YES | NE depot: (44,10),(48,10),(44,13),(48,13) | 0 | no |
| 4 | 2 | SOLDIER | 3 | 3 | YES | S depot: (28,44),(32,44),(30,47) | 0 | no |
| 5 | 2 | BARBARIAN (hay-reeve) | 1 | 4 | YES | S depot gate: (30,43) | 0 | no |
| 6 | 2 | SOLDIER (patrols) | 6 | 2 | no | roads: (20,29),(40,30),(29,20),(30,40),(24,30),(36,29) | 0 | no |
| 7 | 2 | ARCHER (patrols) | 4 | 3 | no | (29,12),(30,24),(29,36),(16,30) | 0 | no |
| 8 | 2 | SOLDIER (relief column) | 6 | 3 | no | dormant, east road: (53,29),(55,28),(57,29),(53,31),(55,31),(57,30) | 600 | no |

Generators (the camps to burn):
| family | team | level | cell (floor 0) |
|--------|------|-------|----------------|
| TENT (levy muster) | 2 | 2 | (12,11) — NW depot interior |
| TENT (levy muster) | 2 | 2 | (46,11) — NE depot interior |
| TENT (levy muster) | 2 | 2 | (30,45) — S depot interior |

Totals: 29 team-2 livings + 3 generators. MAXOBS ledger: 29 authored
livings + 3 gens + 10 markers + 6 treasures + 3 exits = 51 objects; tent
output at lvl 2 stays far under the 150 cap (generators pace-limited; the
F4 flood pathology was lvl-5+ gens — these are lvl 2 by design).

Treasure: FAMILY_DRUMSTICK at (10,13) [NW interior, clear of the
(9,12)/(10,10) guard footprints] and (49,10) [NE interior corner];
S depot FAMILY_GOLD_BAR at (31,46) + DRUMSTICK (28,47);
FAMILY_MAGIC_POTION at (51,32) (pond fringe, exploration);
FAMILY_DRUMSTICK at (18,32) (fallow, near the bones).

#### HEROES / NAMED

None placed. (Kettle is briefing-voice only until level 9.)

#### START MARKERS (10; lead FIRST; on the west road / verges; 2x2 clearance)

lead (4,29); (2,28),(6,28),(2,31),(6,31),(8,29),(4,24),(8,24),(4,35),(8,35).
(All anchors verified against hedge rows 26-27/33-34: (4,24)/(8,24) sit in
the hay quarter above the north hedge; (4,35)/(8,35) on grass below the
south hedge — all 2x2 clear.)

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57,30) | 8 (mainline east: the paymaster's road) |
| 0 | (29,2)  | 7 (OPTIONAL north: the toll road to the Grey Tolls) |
| 0 | (2,30)  | 5 (backtrack: west to the Two Banners field) |

Branch note: 6 is the summer hub — completing it unlocks BOTH 7 and 8
(get_accessible_levels expands all exits of completed levels); 7 rejoins
at 8. Briefing names the choice in-fiction (lines 5-6).
Walkable route: lead (4,29) -> east-west road rows 29-30 -> depot lanes
x12/x46 -> gates -> interiors; north-south road x29-30 -> (29,2) exit and
-> S depot gate. All exits sit on path/grass. Reachability: every guard and
generator is inside a depot with a carved pavement gate on a lane; the
relief column is on the east road; nothing walled off.

#### BRIEFING (6 lines; char counts 30/31/28/30/26/25)

```
Ledger, hay war. Burn the levy
camps before they muster twice.
The troops call us the Brass
Kettles now. It sticks. Pay is
warm coin again. Toll fort
north; the pay road east.
```

(Warm-coin thread: line 5. Company-name beat: lines 3-4, per the
skeleton's "the company gets a name" summer note. Lines 5-6 name the
summer hub's branch in-fiction — the toll fort north is optional 7, the
pay road east is mainline 8; the level 7/8 briefings pick the thread up
either way.)

#### BALANCE NOTES

Curve position: crew 3 (the summer rung holds at 3, per the meta table:
5 and 6 both gate there). Battle shape: a hub-and-spoke
sweep — the crew owns the road cross early (patrols come to them), then
storms three guard-locked depots one at a time; each depot is a small siege
(gate chokepoint, archers inside, tent respawning lvl-2 skeleton-levies
until torched). The tick-600 relief column punishes slow sweeps by
retaking the road.

Calibration gate (kill-all, curve 3): 8-mixed roster reaches level_done==1
within 6000 ticks on >=2/3 seeds at crew 3 AND 3/3 at crew 4; crew 2 may
fail. Sub-checks: all 3 generators destroyed in every clearing run (they
must be, by construction); no generator-flood pathology (tent output alive
never exceeds ~12 at once at lvl 2 pacing — watch the census curve).
300-tick smoke: patrols engaged (team-2 alive < 29), all 3 tents alive at
tick 300 (asserts depots hold until the crew arrives), relief column still
dormant.

#### Decor ambience

- Road pebbles: (2,2)-(57,57) mod 11 (Path only — the cross + lanes).
- Hay-field shrubs: all four quarters mod 19 (LightGrass), off the roads by
  ground-class.
- Bones in the old-battle fallow: (4,31)-(20,34) mod 9 (DarkGrass) — last
  summer's hay war, unaccounted.
- Hand accents: the six gate braziers (step 9); DECOR_TORCH1 pairs inside
  each depot at interior corners clear of guards/treasure/tents:
  (9,9),(15,9) NW; (43,9),(49,9) NE (treasure sits at (49,10), one cell
  south — no overlap); (27,43),(33,43) S.

### Level 07 — Grey Tolls (OPTIONAL)

- id: 7 (grid_file scen0007)
- title: "Grey Tolls" (10 bytes)
- type_bits: 0 (kill-all win; the "defense" is a playtest band, not a type
  bit — no SAVE_ALL interplay at all). Protected-bit plan: NONE; the
  toll-warders are deliberately UNNAMED team-0 guards and carry no bit2.
- floors: 1 (no PIX_AIR)
- grid: 50x60
- par_value: 4
- time_bonus_limit: 5000
- weather: roll. ZERO snow tiles — this is the SUMMER fort; level 14
  "The Long Toll" repaints this pass in winter (the callback the skeleton
  promises), so the layout below is written to be re-usable by the L14
  builder with snow fill + Snow weather.

#### TERRAIN PLAN (floor 0)

Concept: a mountain toll fort astride a north-south pass road. Cliffs wall
both sides; the company garrisons the bailey beside the standing toll-watch
and holds the wall while a mercenary assault comes up from the south camp
in four waves plus a goat-path flank. Optional level: +1 harder than the
summer median, paid back in the strongbox room.

Paint order (before smooth_world):
1. init_world(level, 1, 50, 60) — base grass.
2. Cliff masses (PIX_WALL2): west block (0,0)-(16,59); east block
   (33,0)-(49,59). Pass corridor = x17-32, full height.
3. Goat path carve (paint grass BACK over the west block):
   paint_rect(6,2,9,23, PIX_GRASS1) then paint_rect(9,20,16,23, PIX_GRASS1)
   — a narrow shelf that lets the tick-1000 flankers reach the corridor at
   (17,20)-(17,23).
4. Fort bailey (PIX_WALL2 shell): rect (15,24)-(34,35) (its side walls sink
   into the cliff blocks — reads as the fort plugging the pass).
5. South camp clearing: the corridor already provides it (x17-32,
   y44-57); scrub it with PIX_GRASS_DARK_1 patches (18,48)-(31,56).

smooth_world(w), then post-smooth decor:
4b. Strongbox room shell (pre-smooth, with the bailey): PIX_WALL2 rect
   (17,26)-(21,29).
6. Bailey interior pavement, painted AROUND the strongbox shell (pavement
   over wall carves doorways, so the shell must be skipped):
   paint_pavement(22,25,33,34); paint_pavement(16,25,21,25);
   paint_pavement(16,30,21,34); paint_pavement(16,26,16,29).
7. Gates (here pavement-over-wall is the point — it carves the doorway):
   north gate paint_pavement(24,24,25,24); south gate
   paint_pavement(24,35,25,35).
8. Strongbox interior + door: paint_pavement(18,27,20,28); door
   paint_pavement(19,29,19,29).
9. Toll road (paint_path): cols 24-25, y2..23 (north approach), y36..57
   (south approach); the bailey pavement carries it through y24-35.
10. Gate torches (decor): DECOR_TORCH1 at (23,23),(26,23) [north gate
    shoulders] and (23,36),(26,36) [south gate shoulders] — all off the
    road cols 24-25.
11. South camp braziers: DECOR_BRAZIER at (18,51),(31,51).
12. scatter_boulders(w, 0, 17,2, 32,22, 19) and
    scatter_boulders(w, 0, 17,36, 32,47, 19) — corridor scree margins
    (modulus high, road cols stay statistically clear; scatter_boulders
    keeps 1-tile entity clearance).

#### ARMIES

Team 0 — the toll-watch (placed allied garrison, ALL guards via npc_flags
bit1, ALL UNNAMED):

| # | team | family | count | level | guard | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|-----------|-------------|--------------|
| A | 0 | SOLDIER | 3 | 4 | YES | south gate posts: (24,33),(25,33),(24,29) | 0 | no |
| B | 0 | ARCHER  | 2 | 4 | YES | wall flanks: (18,31),(31,31) | 0 | no |
| C | 0 | CLERIC (toll-clerk) | 1 | 3 | YES | (27,28) — patches the line | 0 | no |

Team 2 — the assault (from the south camp; team 1 EMPTY):

| # | team | family | count | level | guard | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|-----------|-------------|--------------|
| 1 | 2 | SOLDIER | 6 | 3 | no | y44-46: (20,45),(23,44),(26,45),(29,44),(22,46),(27,46) | 0 | no |
| 2 | 2 | BARBARIAN | 5 | 3+(i%2) | no | y49-51: (21,50),(24,49),(27,50),(23,51),(26,51) | 350 | no |
| 3 | 2 | SOLDIER | 6 | 4 | no | y53-55: (19,54),(22,53),(25,54),(28,53),(31,54),(24,55) | 800 | no |
| 4 | 2 | ARCHER | 2 | 4 | no | with wave 3: (20,56),(29,56) | 800 | no |
| 5 | 2 | THIEF (goat-path flank) | 4 | 4 | no | the shelf: (7,4),(8,7),(7,10),(8,13) | 1000 | no |
| 6 | 2 | BARBARIAN (the captain + heavies) | 4 | 5, captain 6 | no | y56-57: captain (24,57); (21,57),(27,57),(24,56) | 1400 | no |
| 7 | 2 | SOLDIER (camp keepers) | 2 | 4 | YES | holding the tents: (18,52),(31,52) — clear of the wave-3 cells and the braziers at y51 | 0 | no |

Generators (the assault's muster — burn the camp to end it):
| family | team | level | cell |
|--------|------|-------|------|
| TENT | 2 | 3 | (20,52) |
| TENT | 2 | 3 | (29,52) |

Totals: team 0 = 6 garrison; team 2 = 29 livings + 2 generators.
MAXOBS ledger: 35 authored livings + 2 gens + 10 markers + 8 treasures
+ 2 exits = 57 objects; lvl-3 tents pace well under the cap.

Treasure (the optional level pays): strongbox room — FAMILY_GOLD_BAR at
(18,27),(19,27),(20,27); FAMILY_SILVER_BAR at (18,28),(20,28);
FAMILY_MAGIC_POTION at (19,28). Larder: FAMILY_DRUMSTICK at (32,26),(32,27).

#### HEROES / NAMED

None. The toll-watch is deliberately unnamed (no SAVE_ALL scope creep; the
fort CAN lose warders without failing the mission — the defense band below
is the design measure instead).

#### START MARKERS (10; lead FIRST; bailey interior pavement; 2x2 clearance,
checked against garrison posts, strongbox walls, and treasure)

lead (22,31); (22,27),(28,31),(28,26),(30,28),(20,32),(26,32),(30,32),
(24,26),(32,29).
(Every anchor's 2x2 footprint verified cell-by-cell against the garrison
posts, the strongbox shell (17,26)-(21,29), the larder treasure at
(32,26)/(32,27), and both gate lanes — all clear on open bailey pavement.)

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (24,3)  | 8 (north: down to the paymaster's road — rejoin mainline) |
| 0 | (24,56) | 6 (backtrack: the toll road south to the Hay War cross) |

Backtrack note: (24,56) sits between wave-4 spawn cells (captain (24,57) is
adjacent but dormant until tick 1400; exits are fx objects, no footing
conflict). Withdrawing early means walking out through the camp — priced in.
Walkable route: lead (22,31) -> south/north gates on road cols 24-25; the
goat-path shelf connects to the corridor at (17,20)-(17,23) — flankers and
their cells are A*-reachable from the lead via the north gate + corridor.
Strongbox door (19,29) opens onto the bailey. Both cliff blocks are
non-route; nothing lives inside them.

#### BRIEFING (6 lines; char counts 27/28/30/31/30/29)

```
Ledger, side work. The Grey
Tolls fort wants a garrison.
Pay is a cut of the road-toll,
counted in that same warm coin.
Hold the wall till the assault
tires. Losses go in the book.
```

(Warm-coin thread: line 4 — even the mountain toll is paid in it; the
autumn act will trace WHY every purse this year is the same mint.)

#### BALANCE NOTES

Curve position: crew 4, OPTIONAL (+1 over the summer mainline median 3,
per the optional rule), and it pays it back in the strongbox. Battle
shape: a true defense — the crew + 6 warders hold two gates; waves land at
ticks 0/350/800/1400 with the goat-path thieves arriving BEHIND the north
gate at 1000 (the fort's blind side; teaches gate-watching both ways).
Ending it requires a sortie south to torch the two tents — turtling never
finishes the level (tents respawn lvl-3 levies), which is the sortie beat
the fiction wants.

Calibration gates (defense + kill, curve 4):
- DEFENSE BAND: team-0 alive (warders + crew) at tick 3000 >= 5 on 3/3
  seeds at crew 4 (initial t0 = 6 + 8-mixed crew = 14; band = the
  default ceil(14/3) = 5).
- KILL: 8-mixed clears (both tents + all waves) within 6000 ticks on
  >=2/3 seeds at crew 4 and 3/3 at crew 5; crew 3 MAY fail badly
  (optional +1 rule).
- Wave sanity from the census curve: no wave arrives after the battle is
  decided (wave 4 at 1400 must land while team-2 alive > 0 pressure
  persists — if sweeps show the crew camps the tents by 1200, pull wave 4
  to 1100).
300-tick smoke: wave 1 engaged at the south gate (t2 alive < 29+spawn);
waves 2-4 + flankers dormant at 300; garrison >= 5 alive.

#### Decor ambience

- Road pebbles: (17,2)-(32,57) mod 11 (Path only).
- Corridor stones: the two boulder scatters (step 12) already dress the
  scree; add scatter_decor DECOR_PEBBLES (17,36)-(32,47) mod 13 (Grass).
- Scrub shrubs in the south camp: (18,48)-(31,56) mod 15 (DarkGrass) —
  the camp is not a crew battle lane until the sortie; shrubs stay off
  Path by ground-class.
- Hand accents: gate torches + camp braziers (steps 10-11); strongbox
  torch DECOR_TORCH1 at (21,26) (inner corner, off the door lane).

### Level 08 — The Paymaster Vanishes

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

#### TERRAIN PLAN (floor 0)

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

#### ARMIES (team 2 = Long Tom's hillmen + war deserters; team 1 EMPTY)

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

#### HEROES / NAMED

- "Long Tom" — FAMILY_THIEF, TEAM 2, level 7, guard=true, at (13,6).
  Named ENEMY boss (the campaign's first): name fits the 11-char field.
  Being team 2 he is invisible to SAVE_ALL logic; the name exists for the
  census/HUD and the fiction. Thief special (invisibility) stays ENABLED —
  a vanishing paymaster-thief is the joke; the doc notes the specials_off
  column is "no" deliberately.
- No team-0 named units.

#### START MARKERS (10; lead FIRST; south apron scrub/grass; 2x2 clearance;
clear of the dormant rear-guard cells x36-42)

lead (50,56); then (46,54),(54,54),(46,58),(54,58),(50,53),(57,56),
(48,51),(57,52),(52,50). ((52,50) sits on the track x52-53 — path is
walkable, markers only need footing + 2x2 clearance, both hold.)

#### EXITS

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

#### BRIEFING (6 lines; char counts 29/28/28/30/26/31)

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

#### BALANCE NOTES

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

#### Decor ambience

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

### Level 09 — Ashfall Fair

- id: 9 (grid_file scen0009)
- title: "Ashfall Fair" (12 bytes)
- type_bits: 0 (kill-all + walk to an exit; the strongroom hold is a
  DEFENSE band in calibration, not a type bit — the meta table's gate
  type for 9 is defense). PROTECTED-BIT PLAN: NONE — npc_flags bit2 is
  set on NOBODY. Kettle is placed here but is protect-OPTIONAL per the
  story bible (the campaign's bit2 SAVE_ALL protectees are exactly the
  Assessor in 4 and the Reeve in 15): his death is a ledger line, not a
  mission failure. The two strongroom wardens are UNNAMED and unprotected.
- floors: 1 (no PIX_AIR)
- grid: 60x50
- par_value: 3
- time_bonus_limit: 4500
- weather: roll (zero snow tiles)

#### TERRAIN PLAN (floor 0)

Concept: the war ends mid-fair; the fairground riots. A cobbled market
square with two stall rows, the town strongroom on its north side (Kettle
and the season's pay inside), looter mobs already in the square and three
more waves converging through the fair gates, plus two looters' bonfires
(TENT generators) feeding the riot until stamped out.

Paint order (before smooth_world):
1. init_world(level, 1, 60, 50) — base grass.
2. Fair green ring (PIX_GRASS_LIGHT_1): (14,10)-(46,38) (trampled pale
   grass around the square).
3. Corner houses (PIX_WALL2, solid facades, off all routes):
   (6,6)-(14,12); (46,6)-(54,12); (6,38)-(14,44); (46,38)-(54,44).
4. Strongroom (PIX_WALL2 shell): rect (26,8)-(34,14).
5. Well water: paint(23,22, PIX_WATER1) — single impassable cell.
   (Stall stubs are deliberately NOT painted pre-smooth; see step 8b.)

smooth_world(w), then post-smooth decor:
8. Market square pavement: paint_pavement(18,15,42,34) — the whole square
   floor as one field.
8b. Stall stubs, RAW post-smooth PIX_WALL2 on top of the pavement (the
   Westlands raw-rock pattern — raw wall blocks all movers and shots and
   the autotiler never touches post-smooth paints):
   row A (y18-19): (20,18)-(22,19); (26,18)-(28,19); (32,18)-(34,19);
   (38,18)-(40,19). Row B (y26-27): (23,26)-(25,27); (29,26)-(31,27);
   (35,26)-(37,27).
9. Strongroom interior + door: paint_pavement(27,9,33,13); door
   paint_pavement(30,14,30,14).
10. Roads (paint_path):
    - west gate: paint_path(2,24,17,25)
    - east gate: paint_path(43,24,57,25)
    - south gate: paint_path(29,35,30,48)
    - north lane behind the strongroom: paint_path(29,2,30,7)
11. Well surround: paint_pavement(22,21,24,23) (the water cell stays at
    (23,22), a one-cell hazard in the square).
12. Fair torches (decor, blocking — all OFF lanes and marker footprints):
    DECOR_TORCH1 at stall shoulders (19,18),(41,18),(22,26),(38,26);
    strongroom front pair at (26,15),(34,15) (front corners — NOT on the
    shell wall cells, clear of the warden posts (28,15)/(32,15) and the
    door lane x30).
13. Bonfire pits: DECOR_BRAZIER at (11,20),(49,30) beside each TENT.

#### ARMIES

Team 0 — the strongroom watch:

| # | team | family | count | level | guard | bit2 | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|------|-----------|-------------|--------------|
| A | 0 | SOLDIER "Kettle" (6 chars) | 1 | 5 | YES | no (protect-optional) | strongroom interior: (30,11) | 0 | no |
| B | 0 | SOLDIER (wardens, unnamed) | 2 | 3 | YES | no | flanking the door: (28,15),(32,15) | 0 | no |

Team 2 — the riot (team 1 EMPTY):

| # | team | family | count | level | guard | placement | spawn_delay | specials_off |
|---|------|--------|-------|-------|-------|-----------|-------------|--------------|
| 1 | 2 | THIEF (square mob) | 8 | 2 | no | (20,21),(24,17),(36,17),(40,21),(21,29),(27,31),(33,31),(39,29) | 0 | no |
| 2 | 2 | THIEF (fire-tenders) | 2 | 3 | YES | by the bonfires: (13,21),(47,29) | 0 | no |
| 3 | 2 | THIEF (west gate wave) | 6 | 3 | no | (2,23),(4,24),(2,26),(6,25),(4,26),(6,23) | 250 | no |
| 4 | 2 | THIEF (east gate wave) | 5 | 3 | no | (53,23),(55,24),(57,25),(53,27),(55,26) | 600 | no |
| 5 | 2 | BARBARIAN (drunk mercs) | 2 | 4 | no | with wave 4: (57,23),(57,27) | 600 | no |
| 6 | 2 | THIEF (south gate wave) | 6 | 3 | no | (27,44),(32,44),(28,46),(31,46),(27,48),(32,48) | 1000 | no |
| 7 | 2 | BARBARIAN (the ringleader) | 1 | 5 | no | (29,47) — leads the last wave in | 1000 | no |

Generators (the riot feeds itself until the fires are stamped out):
| family | team | level | cell |
|--------|------|-------|------|
| TENT (looters' bonfire, west green) | 2 | 2 | (11,21) |
| TENT (looters' bonfire, east green) | 2 | 2 | (49,29) |

Totals: team 0 = 3; team 2 = 30 livings + 2 generators. MAXOBS ledger:
33 authored livings + 2 gens + 10 markers + 8 treasures + 2 exits = 55
objects; lvl-2 tents pace far under the cap.

Treasure — THE SEASON'S PAY, inside the strongroom (the crew eats its own
wages; the ledger notes it dryly): FAMILY_GOLD_BAR at (28,10),(29,10),
(31,10); FAMILY_SILVER_BAR at (28,12),(32,12). Fair goods:
FAMILY_DRUMSTICK at (24,18),(36,18) (open stall lanes x23-25/x35-37);
FAMILY_MAGIC_POTION at (25,22) (by the well).

#### HEROES / NAMED

- "Kettle" — FAMILY_SOLDIER, team 0, level 5, guard=true, NO bit2
  (protect-optional per the story bible; SAVE_ALL is off), specials_
  disabled=false, at (30,11) inside the strongroom. The company
  quartermaster (skeleton cast: placed in 4/9/18). The design still
  wants him breathing — see the Kettle expectation gate below — but his
  death docks the ledger, not the mission. Interior post + guard AI +
  the door choke at (30,14) keep him out of mob reach while any warden
  or crew member holds the door.
- No other named team-0 units (wardens unnamed by design).

#### START MARKERS (10; lead FIRST; square pavement north band and lanes;
every 2x2 footprint checked against stalls (rows y18-19/y26-27), wardens
(28,15)/(32,15), the well surround, and the torch decor)

lead (30,16); then (24,16),(36,16),(20,16),(40,16),(24,21),(30,22),
(36,21),(18,21),(42,21).
(Lead deploys the crew directly between the strongroom door and the square
mob — protect-first posture from tick 0.)

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57,24) | 10 (east: the Foundry's road — autumn buys our debt next) |
| 0 | (2,24)  | 8 (backtrack: west road into the hills) |

Backtrack note: the west wave spawns dormant on cells adjacent to (2,24)
until tick 250; exits are fx objects (no footing conflict), and an early
withdraw walks through the waking mob — priced in. Walkable route: lead
(30,16) -> square pavement -> east road rows 24-25 -> (57,24). Strongroom
reachable via door (30,14); both TENTs stand on open green flanked by
their fire-tenders; every wave path (west/east/south roads) reaches the
square. Reachability check: all livings + generators A*-reachable from
the lead; stall stubs and corner houses are the only interior walls and
all have full perimeter access.

#### BRIEFING (6 lines; char counts 30/30/30/28/28/32)

```
Ledger, fair day. War ended at
noon; Ashfall Fair went mad by
two. Looters at the strongroom
where our season's pay sits.
Kettle guards the door. Keep
him breathing. The coin is warm.
```

(Warm-coin thread: line 6 — the recovered chest from level 8 now sits in
Ashfall's strongroom, and Ashfall is the foundry-city's fair: the autumn
act (10, "The Ledger Debt") opens with the Foundry buying the company's
own debt. Kettle beat: cast note says 4/9/18 — this is his middle
appearance; the level protects him hard so the arc stays continuous,
but mechanically he is optional — the SAVE_ALL bits belong to 4 and 15.)

#### BALANCE NOTES

Curve position: crew 5 (summer close-out, one step over 8's crew 4 —
the riot presses harder than the guard ring, and the meta table pins 9
at 5). Battle shape: a riot —
the square mob presses the strongroom immediately (protect-first opening),
then three timed gate waves (250/600/1000) force the crew to rotate
around the stall rows while the two bonfires trickle lvl-2 levies until
stamped out. The ringleader's south wave at 1000 is the crisis beat: it
arrives straight up the south road at the door.

Calibration gates (defense primary per the meta table, curve 5):
- DEFENSE BAND: team-0 alive (Kettle + 2 wardens + crew = 11) at tick
  3000 >= 4 (ceil(11/3)), 3/3 seeds at crew 5, 8-mixed roster;
  4-soldier floor recorded.
- KILL completion (secondary, human endgame): 8-mixed reaches
  level_done==1 within 6000 ticks on >=2/3 seeds at crew 5 AND 3/3 at
  crew 6; crew 4 may fail.
- Kettle design expectation (NOT a SAVE_ALL gate — bit2 is on nobody):
  no Kettle death while any crew member lives within tick 1800 at
  curve, 3/3 seeds, both rosters (census-tick granularity; deaths after
  a full crew wipe are the documented harness artifact). If it fails,
  deepen the wardens before moving his post.
- Generator sanity: both tents destroyed in every clearing run; bonfire
  output alive never exceeds ~10 at once (lvl-2 pacing).
300-tick smoke: square mob engaged; west wave awake (250 delay
round-trips), east/south dormant; Kettle at full hp behind the door
(guard, interior — the mob has no reason to reach him before the crew
does); wardens may be bloodied but t0 >= 3 expected at 300.

#### Decor ambience

- Road pebbles: (2,2)-(57,48) mod 11 (Path only — all four gate roads).
- Trampled-green shrubs: (14,10)-(46,38) mod 21 (LightGrass; the square
  itself is Pavement so the battle floor stays clean — SHRUB concealment
  never lands on the fight).
- Bones behind the corner houses (the fair's butcher row): (6,45)-(14,48)
  mod 9 (Grass).
- Hand accents: stall torches + strongroom front pair + bonfire braziers
  (steps 12-13).

### Level 10 — The Ledger Debt  (AUTUMN opens: the forced contract)

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

#### TERRAIN PLAN

##### Floor 0 — the yard, the mill, the races
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

##### Floor 1 — the grain loft
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

##### Stairs (aligned pairs, both cells interior floor on BOTH floors)
- stair_pair(w, 0, 38, 10) — NE stair, ground floor north hall up to loft.
- stair_pair(w, 0, 23, 34) — SW stair, south hall up to loft.

##### Decor ambience
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

#### ARMIES

##### Team 2 — squatters up top, slime below (33 livings incl. The Miller, 0 generators)
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

##### Team 0 — the creditor's witness
| name | family | team | level | floor,cell | guard | specials_off | spawn_delay | bit2 |
|------|--------|------|-------|------------|-------|--------------|-------------|------|
| "The Factor" (10 ch) | SOLDIER | 0 | 5 | f0 (5,6) by the backtrack gate | YES | yes | 0 | NO |

Totals: 33 team-2 livings (28 placed + 4 night shift + The Miller) +
1 team-0 ally = 34 livings; 0 generators (finite kill-all set-piece, per
the flood lesson); 4 delayed spawns hold level_done open and show in the
NEXT WAVE HUD.
MAXOBS ledger: 34 livings + 0 gens + 10 markers + 16 treasures + 2 exits
= 62 objects; huge headroom under 150.

#### TREASURE

- The Miller's hoard (loft den, skip cells under his footprint via
  cell_near_entity): GOLD_BAR x6 checkerboard over f1 (24,10)-(30,14);
  SILVER_BAR x4 over f1 (32,10)-(35,12).
- DRUMSTICK x5: f0 (22,28),(24,33),(37,33); f1 (38,30); yard f0 (33,3).
- MAGIC_POTION x1: f1 (39,34) — behind the SW stairhead.

#### START MARKERS (10, floor 0 yard; lead FIRST, 2x2 shoulder room)

lead (30,6) — on the north approach path facing the door; then
(28,4),(32,4),(26,6),(34,6),(28,2),(32,2),(24,4),(36,4),(22,2).
All on open yard grass/path north of the mill, non-overlapping at 2x2,
clear of The Factor (5,6) and both exits.

#### EXITS

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

#### BRIEFING (6 lines; char counts 32/28/31/31/30/33)

```
Ledger, first frost. The Foundry
bought our paper. All of it.
Terms: work it off underground.
Job one, clear their Undermill.
Squatters up top, slime below.
The advance was warm coin. Again.
```

(Warm-coin thread: line 6 — the debt itself is paid in the strange metal.)

#### BALANCE NOTES

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

### Level 11 — Cold Seams  (the deep mine; the metal is wrong)

The Foundry's deep mine. The dead miners never clocked out; things of fire
nest where the seams run WARM — the campaign's first physical hint that the
coin's metal is wrong. The west wall of the deep workings has broken into
old built vaults (the branch to optional 12).

- id: 11 (grid_file scen0011)
- title: "Cold Seams" (10 bytes)
- type_bits: 0 (kill-all, then walk to an exit). PROTECTED-BIT PLAN: no
  npc_flags bit2 anywhere; no team-0 NPCs at all on this level — the
  company goes down alone.
- floors: 2 (floor 1 = the upper gallery / adit level, ENTRY floor;
  floor 0 = the deep seams with the lava-vein visuals)
- grid: 62x48
- par_value: 4
- time_bonus_limit: 4500

Entered from 10 (the ore-track). Curve position: crew level 5.

#### TERRAIN PLAN

##### Floor 1 — the upper gallery (entry)
1. Fill: paint_rect(0,0,61,47, PIX_WALL2) — solid rock.
2. Carve PIX_DIRT_DARK_1 (pre-smooth):
   - adit mouth + entry hall (4,20)-(14,28)
   - main gallery, east-west (15,22)-(46,26)
   - north timber hall (24,8)-(40,18), corridor (28,19)-(31,21)
   - south store rooms (20,27)-(32,34) (open stope off the gallery)
   - east winch room (47,18)-(58,30)
3. smooth_world(w).

##### Floor 0 — the deep seams
1. Fill: paint_rect(0,0,61,47, PIX_WALL2).
2. Carve PIX_DIRT_DARK_1:
   - the deep crossing (24,18)-(40,30) (central)
   - west drift (8,20)-(23,25)
   - the VAULT BREACH room (4,26)-(12,36) (adjacent to the drift at
     rows 25/26, cols 8-12)
   - north seam gallery (26,6)-(48,17)
   - east seam (41,20)-(56,30)
   - south seam (20,31)-(44,40)
   - shaft room (50,32)-(58,42), link carve (52,31)-(53,31)
3. smooth_world already ran per-floor via the shared call; then
4. WARM SEAMS — lava veins (PIX_LAVA1, POST-smooth impassable ribbons;
   solid to ground walkers, flyers/projectiles cross; each leaves a
   trodden gap on the required route):
   - vein A (34,6)-(35,13), north seam gallery — walk lanes y14-17 open
   - vein B (46,22)-(47,28), east seam — gap rows y20-21 top and
     y29-30 bottom
   - vein C (28,36)-(41,37), south seam — lanes y31-35 above; below via
     x20-27 west and x42-44 east
   - vein D (53,40)-(56,42), shaft room SE corner (decorative glow,
     severs nothing; the forward exit sits at (56,34), clear of it)

##### Stairs (aligned pairs; the cell is carved on BOTH floors)
- stair_pair(w, 0, 28, 24) — the main winze: deep crossing (f0) up to the
  main gallery (f1).
- stair_pair(w, 0, 50, 28) — the winch shaft: east seam (f0) up to the
  winch room (f1).

##### Decor ambience (no shrubs underground)
- DECOR_TORCH1: f1 entry hall mouth (5,21),(5,27); winch room (48,19),
  (57,19); f1 stairheads (27,23),(49,27) shoulder cells; f0 stair
  landings (30,25),(52,27).
- DECOR_PEBBLES mod 9 over all carved dirt, both floors (DirtDark class).
- DECOR_BONES mod 6 over the dead-miner workings: f0 north seam gallery
  (26,6)-(48,17) and the deep crossing (24,18)-(40,30); f1 timber hall
  (24,8)-(40,18).
- scatter_litter(w,0, 20,31, 44,40, 47) — sparse jagged spoil in the
  south seam (modulus high; blocks even shots, kept off the y31-35 lane
  by the scatter's entity/route clearance check at gen time).

#### ARMIES (team 2 = the mine's dead + the warm nests; 39 livings, 1 gen)

##### Floor 1 — the upper gallery
| group | family | count | level | guard | placement | spawn_delay |
|-------|--------|-------|-------|-------|-----------|-------------|
| adit watch (roam) | SKELETON | 3 | 4 | no | (17,23),(19,25),(21,23) | 0 |
| timber hall dead (roam) | SKELETON | 2 | 4 | no | (26,10),(36,15) | 0 |
| timber hall dead (roam) | SKELETON | 2 | 5 | no | (30,12),(33,10) | 0 |
| hall light (posted) | GHOST | 1 | 5 | YES | (32,16) | 0 |
| store seep (roam) | SLIME | 3 | 4 | no | (22,29),(27,32),(31,29) | 0 |
| winch wards (posted) | SKELETON | 3 | 5 | YES | (50,20),(54,24),(50,26) | 0 |
| winch light (posted) | GHOST | 1 | 5 | YES | (55,28) | 0 |

Generators:
| family | team | level | floor,cell | note |
|--------|------|-------|------------|------|
| BONES | 2 | 3 | f1 (25,9) | 3x3 footprint (25-27, 9-11) inside the timber hall; LOW level per the generator-flood lesson — a trickle, not a stream |

##### Floor 0 — the deep seams
| group | family | count | level | guard | placement | spawn_delay |
|-------|--------|-------|-------|-------|-----------|-------------|
| dead miners (roam) | SKELETON | 8 | 5 | no | (26,20),(34,22),(38,28),(30,14),(44,10),(48,24),(30,34),(38,39) | 0 |
| vein riders (flyers posted ON lava) | GHOST | 4 | 5 | YES | (34,8) vein A, (46,25) vein B, (33,36) vein C, (54,41) vein D | 0 |
| warm nests (posted at vein gaps) | FIREELEMENTAL | 2 | 5 | YES | (37,15),(43,21) | 0 |
| warm nests (posted) | FIREELEMENTAL | 2 | 6 | YES | (26,38),(52,36) | 0 |
| breach warden (wandered through) | GIANT_SKELETON | 1 | 7 | YES | (8,31) — 4x4 footprint (8-11, 31-34) inside the breach room; foreshadows 12 | 0 |
| breach watch (posted) | SKELETON | 3 | 5 | YES | (6,28),(11,27),(10,35) | 0 |
| the seams wake (dormant, posted) | FIREELEMENTAL | 2 | 5 | YES | (30,8),(24,36) | 700 |
| the seams wake (dormant, posted) | GHOST | 2 | 6 | YES | (40,12),(36,32) | 700 |

Totals: f1 15 + f0 24 = 39 team-2 livings + 1 BONES generator (lvl 3);
0 team-0; 4 delayed spawns (NEXT WAVE HUD at tick 700).
MAXOBS ledger: 39 livings + 1 gen + 10 markers + 21 treasures + 3 exits
= 74 objects; ~76 headroom covers the generator's trickle.

#### TREASURE

- Ore-glints (the warm metal, in situ): SILVER_BAR x8 — f0 (28,7),(44,7),
  (44,15),(54,22),(54,29),(22,33),(43,39),(28,29).
- The shift-boss's pay chest: GOLD_BAR x4 — f1 winch room (56,21),(57,22);
  f0 shaft room (57,33),(57,35).
- DRUMSTICK x6 (the miners' cache): f1 stores (21,28),(24,33),(30,33);
  f0 crossing (25,19),(39,19); f0 west drift (10,21).
- MAGIC_POTION x2: f1 timber hall (39,9); f0 south seam (21,39).
- FLIGHT_POTION x1: f0 (42,33) — by vein C; fly the veins, once.

#### HEROES / NAMED

None. No team-0 placements; the named cast is elsewhere this act.

#### START MARKERS (10, floor 1 entry hall; lead FIRST, 2x2 clearance)

lead (13,24) — at the hall's east mouth facing the main gallery; then
(11,21),(11,24),(11,27),(9,21),(9,24),(9,27),(7,21),(7,24),(7,27).
Backtrack exit (4,20) stays clear of every footprint.

#### EXITS (the briefing names the branch)

| floor | cell    | destination |
|-------|---------|-------------|
| 1 | (4,20)  | 10 (backtrack: the adit out to the Undermill track) |
| 0 | (5,34)  | 12 (OPTIONAL: through the breach, into the Old Count's vault) |
| 0 | (56,34) | 13 (main: the ore shaft down to the Smelter's Road) |

Walkable route (lead -> 13): (13,24) → main gallery → winze stair (28,24)
down → deep crossing → east seam via x41 band, around vein B through the
y20-21 gap → link (52,31)-(53,31) → shaft room → (56,34). Branch route:
crossing → west drift → breach room → (5,34), past the warden. All livings
and the generator A*-reachable from the lead marker (empty allowlist);
vein-rider ghosts are flyers and exempt from the ground footing audit.

#### BRIEFING (6 lines; char counts 27/27/27/30/29/27)

```
Ledger, week two below. The
Foundry's dead miners never
left. Sweep the seams. They
are warm to the touch, same as
the coin. The west wall broke
into old vaults. Your call.
```

(Warm-coin thread: lines 4-5 — seams warm as the pay. Branch named in
fiction: "old vaults. Your call." = the 12 exit.)

#### BALANCE NOTES

Battle shape: floor 1 is the approach act (skeleton pickets, a low BONES
trickle to snuff in the timber hall, store slimes), floor 0 the real sweep:
roaming dead miners meet the crew in the crossing, fire elementals hold the
vein gaps the routes must thread (first "the metal is wrong" beat delivered
as a fight), vein ghosts harass from over the lava where melee can't answer
— archers/mages earn their pay. The breach warden (lvl-7 giant) is the
level's mini-boss and the signpost for the optional vault. Tick-700 wake
(2 fire + 2 ghost, posted) punishes camping the crossing.

Calibration gate (kill-all; curve crew 5):
- 8-mixed roster clears within 6000 ticks >=2/3 seeds at crew 5, 3/3 at
  crew 6; crew 4 may fail;
- the BONES generator dies in every clearing run (it is on the required
  sweep and lvl 3 cannot outbreed a curve crew);
- neither side extinct before tick 150; dormant census 4 at tick 0;
- structural: 2 aligned stair pairs; exit destinations {10, 12, 13} all in
  package; footing audit (giant's 4x4 inside the breach carve; ghosts on
  lava exempt as flyers); fall-line audit trivial (no PIX_AIR authored);
  vein gaps keep A*-reachability with the empty allowlist.

### Level 12 — The Old Count's Vault  (OPTIONAL: the side venture)

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

#### TERRAIN PLAN (single floor; fill first, carve rooms, wall lines matter)

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

##### Decor ambience (bones decor is the level's signature)
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

#### ARMIES (team 2 = the Count's household; 33 livings, NO generators —
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

#### TREASURE (the point of the level)

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

#### HEROES / NAMED

No team-0 heroes. The Count (named enemy) is the set-piece boss; his
death-line belongs to the results screen, not the briefing.

#### START MARKERS (9, breach entry room; lead FIRST, 2x2 clearance)

lead (8,22) — at the room's east mouth facing the west link; then
(6,18),(6,22),(6,26),(4,18),(4,22),(4,26),(2,22),(2,26).
Backtrack exit (2,16) clears every marker footprint (nearest is (4,18),
covering rows 18-19).

#### EXITS

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

#### BRIEFING (6 lines; char counts 30/26/29/29/29/31)

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

#### BALANCE NOTES

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

### Level 13 — The Smelter's Road  (ore-wagon escort; first snow)

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

#### TERRAIN PLAN

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

##### Decor ambience
- DECOR_PEBBLES mod 13 down the whole road (Path ground class).
- DECOR_BONES hand-cluster at bend 1 (the LAST convoy): (44,9),(46,12),
  (43,13).
- DECOR_SHRUB mod 17 ONLY on the two ambush shoulders where the guards
  post — (44,10)-(48,14) and (2,24)-(6,28) — deliberate concealment for
  the ambush fiction; noted smoke-bounds risk: shrubs sit OFF the road
  band itself, and the calibration sweep runs with them in.
- DECOR_BRAZIER at the smelter gate: (22,57),(27,57) — warm light at the
  end of the road (and the exit beacon).

#### ARMIES

##### Team 0 — the convoy (4 livings)
| name | family | level | guard | anchor (footprint) | specials_off | bit2 |
|------|--------|-------|-------|--------------------|--------------|------|
| "Ore Wagon" (9 ch) | GOLEM | 8 | no (it must roll) | (23,3) — 4x4-class (23-26, 3-6) | yes | no — design-gated, not bit2 |
| "Spare Cart" (10 ch) | GOLEM | 6 | no | (29,1) — (29-32, 1-4) | yes | no |
| drover | SOLDIER | 5 | no | (21,3) | no | no |
| drover | SOLDIER | 5 | no | (33,5) — clear of both cart footprints | no | no |

GOLEM notes: huge HP pool (combat.hp +270, no specials, boulder
default weapon) — the wagon IS the pile of ore; it "throws its load" when
pressed. Slow and tanky by family, which is exactly a wagon. It does not
need to reach the exit (AI never exits): the WIN is the crew reaching
the smelter gate; keeping the Ore Wagon alive is the design gate (and
the fiction's pay), wherever it has trundled.

##### Team 2 — every broke company in the hills (27 placed + 10 pursuit)
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

#### TREASURE

- Rest-stone caches (DRUMSTICK x5, one per leg): (8,20),(40,21),(8,34),
  (40,35),(20,48).
- The bandits' takings (GOLD_BAR x3, at the tent camp): (33,37),(34,39),
  (39,36).
- MAGIC_POTION x2: (6,26) in bend 2, (46,41) in bend 3.
- SPEED_POTION x1: (24,44) — downhill legs for the last stretch.

#### HEROES / NAMED

The convoy table above IS the named roster: Ore Wagon (design-gated,
not bit2), Spare Cart (expendable — "the second cart is ballast; the
assay ore rides the lead"), two unnamed drovers. No other team-0
placements.

#### START MARKERS (10, terrace 1, AHEAD of the carts; lead FIRST)

lead (24,8) — on the road, downhill of the wagon (crew screens the front;
the pursuit takes the rear); then (21,8),(27,8),(19,6),(29,6),(17,8),
(31,8),(19,10),(29,10),(24,10).
Clearances audited: lead (24-25, 8-9) vs wagon rows 3-6 — one clear row
between; (29,6) does NOT collide with Spare Cart because the cart anchors
at (29,1) with footprint rows 1-4; all markers clear of band A (y12).

#### EXITS

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

#### BRIEFING (6 lines; char counts 25/28/26/26/30/29)

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

#### BALANCE NOTES

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

### Level 14 — The Long Toll (WINTER, hold the pass)

- id: 14 (grid_file scen0014)
- title: "The Long Toll" (13 bytes)
- type_bits: 0 — classic kill-all + walk-to-exit. NO SAVE_ALL here: the
  garrison are unnamed team-0 posts, so no protected-bit is set anywhere
  (protected-bit plan: none; the first bit2 walker of winter is the Reeve
  in 15).
- floors: 1 (no PIX_AIR anywhere; fall-line rule trivially satisfied)
- grid: 60 x 60
- par_value: 5
- time_bonus_limit: 6000
- weather: FORCED SNOW — the map is snow-filled wall to wall (~2000+ snow
  tiles, far over the >=40 threshold), so the post-roll override always
  lands WeatherKind::Snow. This is the task's explicit "snow + Snow
  weather" requirement for the winter act opener.

#### STORY POSITION / CALLBACK CONTRACT

The Grey Tolls pass, the same fort as optional level 7 — IN WINTER. Level
7 is optional, so the briefing must read true whether or not the crew
garrisoned it in summer (see BRIEFING: "Some of us drew summer pay here;
the rest heard the story"). The map does NOT need to byte-match 7's fort
(winter re-dress: drifted walls, frozen tarn, buried outworks), but it
keeps the landmark grammar so returning players recognize it: a walled
gatehouse ASTRIDE the road, west and east gates, the toll strongroom in
the northeast corner. COORDINATION NOTE for the level-7 designer: keep
those three landmarks in 7 and the callback lands for free.

Warm-coin thread: the toll chest is entirely warm coin this year — the
briefing says so, and the strongroom's gold bars are the physical prop.

#### TERRAIN PLAN (floor 0, the only floor)

Concept: an east-west pass corridor between two cliff bands, with the
toll fort straddling the road at center. The crew holds the fort;
toll-breakers come from both mouths.

Paint order (before smooth_world):
1. Snow fill: paint_snow(0, 0, 59, 59) — the shared helper cloned from
   westlands_mapgen (deterministic (x*7+y*13)%2 picks PIX_SNOW1/PIX_SNOW2,
   genre-inert painted variants).
2. North cliff band: paint_rect(0, 12, 59, 20, PIX_WALL2).
3. South cliff band: paint_rect(0, 39, 59, 47, PIX_WALL2).
   (Pass corridor = y21..38, 18 tiles tall, open snow. The snow above
   y12 and below y47 is scenic mountainside — NOTHING is placed there.)
4. NW gully (carve through the north band, then the pocket):
   gap paint_snow(8, 12, 11, 20); pocket paint_snow(4, 4, 14, 11).
5. Fort walls: paint_rect(24, 22, 37, 37, PIX_WALL2); carve the
   courtyard paint_snow(26, 24, 35, 35); carve the WEST gate
   paint_snow(24, 28, 25, 31) and the EAST gate paint_snow(36, 28, 37, 31)
   (2-thick walls, 4-wide gates — a 2x2 crew passes a 2x2 edge guard).
6. Toll strongroom (NE corner of the courtyard): wall stubs
   paint_rect(32, 24, 32, 26, PIX_WALL2) and
   paint_rect(33, 26, 34, 26, PIX_WALL2); the nook (33..35, 24..25)
   opens only through the gap at (35, 26).
7. Frozen tarn (black ice choking the east approach south side):
   paint_rect(44, 33, 52, 37, PIX_WATER1) — impassable; the east road
   y28..31 stays clear.
8. Pine clumps (PIX_TREE_M1): (2,22)-(6,26) west mouth north side;
   (50,22)-(55,26) east mouth north side.
9. Scrub through the snow (PIX_GRASS_DARK_1): (14,34)-(18,37),
   (40,22)-(43,25).

smooth_world(w), then post-smooth (autotiler-inert):
10. Courtyard pavement: paint_pavement(26, 24, 35, 35) (under the wall
    stubs' shadow too — pavement first, stubs were painted pre-smooth so
    re-assert them if the order needs it; the concept builders paint
    decor AFTER smoothing, stubs BEFORE).
11. The toll road (paint_path): west approach paint_path(1, 29, 23, 30);
    through the courtyard paint_path(26, 29, 35, 30); east approach
    paint_path(38, 29, 58, 30).
12. Gate torches: paint(24, 27, PIX_TORCH1), paint(24, 32, PIX_TORCH1),
    paint(37, 27, PIX_TORCH1), paint(37, 32, PIX_TORCH1).
13. Courtyard braziers (the winter watch's fires): paint(27, 25,
    PIX_BRAZIER1), paint(27, 34, PIX_BRAZIER1), paint(34, 34,
    PIX_BRAZIER1).

#### DECOR AMBIENCE (decor plane, all non-blocking)

- DECOR_PEBBLES worn through the snow on the road: Path tiles only,
  whole map rect (0,0)-(59,59) mod 13.
- DECOR_BONES, the pass takes its dead: hand-set (6,31), (18,27) west
  approach; (42,31), (54,27) east approach; (9,10) gully pocket.
- DECOR_BOULDER scatter on the open pass floor: scatter_boulders
  (0, 0,21, 23,38, 23) and (0, 38,21, 59,38, 23) — outside the fort only.
- NO DECOR_SHRUB anywhere near the road (battle lanes stay honest); two
  accents in the scrub patches at (15,35), (41,23) are off every lane.

#### ARMIES

Team 0 — the winter watch (unnamed garrison; guard via npc_flags bit1):

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|
| 1 | 0 | SOLDIER (gate watch) | 4 | 6 | YES | (26,28),(26,31) west gate; (35,28),(35,31) east gate | 0 | no |
| 2 | 0 | ARCHER (wall watch) | 2 | 5 | YES | courtyard corners (27,26),(34,33) | 0 | no |

Team 2 — wolves first, then the toll-breakers in four waves:

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|
| 3 | 2 | ORC (wolf strays) | 6 | 4 | no | pass floor: (12,24),(16,35),(20,26),(42,25),(48,28),(54,35) | 0 | no |
| 4 | 2 | GHOST (the frozen watch) | 2 | 4 | no | gully pocket: (6,6),(12,9) | 0 | no |
| 5 | 2 | SOLDIER (west wave 1) | 5 | 5 | no | west mouth: (1,26),(1,29),(2,27),(2,31),(3,29) | 300 | no |
| 6 | 2 | ARCHER (west wave 1) | 2 | 5 | no | (1,32),(3,25) | 300 | no |
| 7 | 2 | BARBARIAN (east wave 1) | 4 | 5 | no | east mouth: (57,26),(57,29),(58,27),(58,31) | 700 | no |
| 8 | 2 | ORC (east wave 1) | 2 | 4 | no | (56,24),(58,24) | 700 | no |
| 9 | 2 | SOLDIER (west wave 2) | 5 | 6 | no | west mouth: (1,24),(2,25),(1,31),(2,33),(3,31) | 1200 | no |
| 10 | 2 | ARCHER (west wave 2) | 2 | 6 | no | (3,27),(3,33) | 1200 | no |
| 11 | 2 | BARBARIAN (the caravan master, unnamed) | 1 | 8 | no | (58,29) | 1800 | no |
| 12 | 2 | BARBARIAN (east wave 2) | 4 | 6 | no | (56,27),(56,31),(57,33),(58,33) | 1800 | no |
| 13 | 2 | BIG_ORC (dire wolves) | 2 | 6 | no | (55,29),(55,31) | 1800 | no |

Generators:

| family | team | level | cell (floor 0) | note |
|--------|------|-------|----------------|------|
| BONES (ghosts — the frozen watch rises) | 2 | 3 | (9,7) gully pocket | LOW level: the post-obmap-fix rule (a lvl-5+ generator outbreeds a curve crew); destroying it is the kill-all's last errand |

Totals: team 0 = 6 livings; team 2 = 35 livings + 1 generator.
Delayed spawns: 27 (waves at 300/700/1200/1800 — dormant, NEXT WAVE HUD,
they hold level_done open so the hold cannot end early). MAXOBS ledger:
41 livings + 1 gen + 10 markers + 2 exits + 6 treasures = 60 objects;
authored livings 41 << 120 budget, generator output has ample headroom.

Every wave spawns at a pass MOUTH (x1..3 / x55..58), far from the crew's
courtyard deploy — dormant walkers are untargetable and wake with a
flash, so no wake-on-top-of-crew shocks.

#### HEROES / NAMED

None placed. Kettle appears in 4/9/18 per the skeleton cast list; the
Sergeant narrates and is never placed. (No names = nothing for SAVE_ALL
to watch even if the type bit were misread; the bit is 0 anyway.)

#### START MARKERS (10; lead FIRST; all on courtyard pavement, 2x2 clearance, clear of guards/stubs/braziers)

1. (29, 30) — LEAD, mid-courtyard on the road line
2. (27, 28)  3. (32, 28)  4. (27, 32)  5. (32, 32)
6. (29, 26)  7. (29, 33)  8. (33, 30)  9. (27, 30)  10. (33, 32)

(Strongroom cells x32..35 / y24..26 and guard posts x26/x35 avoided;
each marker has 2x2 shoulder room on open pavement.)

#### TREASURE

- The toll chest (strongroom nook): GOLD_BAR (33,25), GOLD_BAR (34,25),
  GOLD_BAR (34,24) — the year's takings, warm to the touch.
- Watch stores (courtyard): DRUMSTICK (28,25), DRUMSTICK (33,34),
  MAGIC_POTION (30,25).

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57, 34) | 15 — east mouth, the road down to Thornby (clear of the tarn x44..52 and of east wave cells y26..33... wave cells are y24..33 at x55..58; (57,34) sits one row south of the lowest wave cell (58,33) — no footprint overlap) |
| 0 | (2, 36)  | 14->13 backtrack — west mouth south shoulder, the switchbacks back to the Smelter's Road (west wave cells are y24..33 at x1..3; (2,36) is 3 rows clear) |

Reachability: lead (29,30) -> west gate -> west mouth is a flat road
walk; east gate -> east mouth likewise; the gully generator + ghosts are
reached through the carved gap at (8..11, 12..20). Self-check reachability
allowlist: EMPTY (every living + the generator is A*-reachable from the
lead marker; nothing is placed on the scenic mountains or in the tarn).

#### BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, winter work. The Grey     (29)
Tolls want holding through the    (30)
pass trade. Some of us drew       (27)
summer pay here; the rest heard   (31)
the story. Toll chest is warm     (29)
coin to the last penny. Hold.     (29)
```

Line 5-6 carry the act's warm-coin thread: EVERY toll paid this year was
struck from the same warm metal — the chest in the strongroom is the
proof the crew can stand on.

#### BALANCE NOTES / CALIBRATION GATE (curve position: crew 7)

Curve: the campaign starts at crew 1 (levels 1-2 gate at crew 1); a
mainline crew enters winter at 7 (the meta table's rung, off 13's crew
6). This level is tuned as a DEFENSE at crew 7 — bracket sweep at
{6, 7, 8} x seeds {42, 1337, 2025} x rosters {4-soldier A, 8-mixed B}.

- Battle shape: wolf strays probe first (ticks 0-300), then alternating
  mouth waves at 300/700/1200/1800 — never both mouths in the same
  breath until 1800, when the caravan master's east push and nothing
  else remains. The fort's two 4-wide gates are the only doors; the
  garrison's 6 posts anchor them but cannot win alone.
- DEFENSE GATE (the Westlands F4 defense form): team-0 alive (garrison +
  crew) at tick 3000 >= 5 on 3/3 seeds with the 8-mixed roster at crew 7
  (band = ceil(initial_t0/3) with initial_t0 = 6 placed + 8 crew = 14).
  The 4-soldier roster is recorded as the pessimistic floor and may sag
  to 3.
- KILL-ALL COMPLETION (type 0 + exits present -> someone must walk out):
  the harness never exits; read the foe-decay curve, not game_ended.
  Expect t2 35 -> <=8 by tick 4500 at curve (the survivors are usually
  the gully ghosts + generator trickle). The lvl-3 BONES generator must
  be killable by a curve crew in one push through the gap.
- Wave sanity: no wave arrives after the battle is decided — 1800 is the
  last wake and the sweeps must show t2 engagement (not a parked wave)
  by tick 2200 on every seed.
- Structural watches: gate wedges (two 2x2 guards + a wave in a 4-wide
  gate — the F1 standoff fix covers the melee, but watch for new
  shapes); generator flooding (lvl 3 cap is the guard-rail); the tarn
  must never trap an east-wave pather (waves spawn north of it and the
  road is clear).
- Weather smoke (unit test): loaded grid >= 40 snow tiles; post-roll
  override reports WeatherKind::Snow.
- Pins to add (test_longseason_levels.cpp): roster row {14, t0 6, t2 35,
  gens 1, markers 10, exits {15, 13}}, delayed-spawn count 27, briefing
  6 lines, strongroom gold present, aligned-stairs N/A (1 floor).

### Level 15 — Wolf Winter (SAVE_ALL: The Reeve; dusk waves)

- id: 15 (grid_file scen0015)
- title: "Wolf Winter" (11 bytes)
- type_bits: 4 (SCEN_TYPE_SAVE_ALL) — kill-all win; the mission fails if
  the protected walker dies.
- PROTECTED-BIT PLAN (npc_flags bit2 scoping, the F2 lesson): bit2 is set
  on EXACTLY ONE walker — "The Reeve". No other placed NPC gets bit2, so
  the engine watches ONLY him: the unnamed door wards and militia can die
  without ending the mission, and no archmage is present to summon a
  named "Phantom". The Reeve is also the ONLY NAMED team-0 walker on the
  map (belt and braces: even under legacy any-named scoping the loss
  condition would be him alone).
- floors: 1 (no PIX_AIR; fall-line rule trivially satisfied)
- grid: 60 x 60
- par_value: 5
- time_bonus_limit: 5000
- weather: FORCED SNOW (snow-filled map, thousands of snow tiles >= 40).

#### STORY POSITION

Thornby, a village snowed in below the Grey Tolls, three days without
the road. The wolves have had the run of it since the drifts closed.
The Reeve of Thornby is the winter act's hinge: he kept the district's
tithe ledgers, and he has seen the warm coin's mint-mark before — he
knows WHOSE ledger it balances. He talks after the wolves are dead
(i.e. the reveal is 16's and 17's fuel; here he only has to LIVE).
Pay for this contract is not coin at all — it is the Reeve's word.

#### TERRAIN PLAN (floor 0, the only floor)

Concept: a palisaded village on a green, forest to the north and south,
the toll road in from the west (from 14) and out east (toward the ford).
Wolves probe from the fields at once; the packs come out of the
treelines in dusk waves.

Paint order (before smooth_world):
1. Snow fill: paint_snow(0, 0, 59, 59).
2. North forest: paint_rect(0, 0, 59, 6, PIX_TREE_M1).
3. South forest: paint_rect(0, 53, 59, 59, PIX_TREE_M1).
4. Wolf den (carved INTO the north forest, so wave 4 is reachable):
   den paint_snow(26, 4, 30, 6); den mouth paint_snow(28, 7, 28, 9).
   (y7..9 is open snow anyway; the carve is the den itself.)
5. Palisade arcs (PIX_WALL2, 1 thick, with gaps):
   north paint_rect(18, 10, 42, 10) then carve gap paint_snow(29, 10, 31, 10);
   south paint_rect(18, 50, 42, 50) then carve gap paint_snow(29, 50, 31, 50).
   (East and west sides are open — the road was the defense, once.)
6. Moot-house (the Reeve's hall, center green): walls
   paint_rect(27, 26, 34, 32, PIX_WALL2); interior carve
   paint_snow(28, 27, 33, 31); door gap paint_snow(30, 32, 31, 32)
   (south-facing, 2 wide).
7. Village houses (solid PIX_WALL2 blocks — roofs seen from above):
   (12,14)-(15,16), (44,14)-(47,16), (12,42)-(15,44), (44,42)-(47,44),
   (20,45)-(23,47), (38,12)-(41,14).
8. Frozen mill pond (SE fields): paint_rect(48, 34, 54, 38, PIX_WATER1).
9. Winter fields, stubble through snow (PIX_GRASS_DARK_1):
   (6,20)-(16,26), (44,22)-(54,28), (8,32)-(18,38).

smooth_world(w), then post-smooth (autotiler-inert):
10. Moot-house floor: paint_pavement(28, 27, 33, 31); hearth
    paint(28, 28, PIX_BRAZIER1).
11. The road: west paint_path(1, 29, 26, 30); across the green
    paint_path(27, 33, 34, 34) skirting the hall's south face;
    east paint_path(35, 29, 58, 30). Green paths to the gaps:
    paint_path(29, 11, 30, 25) north; paint_path(29, 35, 30, 49) south.
12. Door torches: paint(29, 32, PIX_TORCH1), paint(32, 32, PIX_TORCH1)
    flanking the moot-house door; one at each palisade gap:
    paint(28, 10, PIX_TORCH1)... palisade cells are wall — put the gap
    torches on the snow INSIDE: paint(28, 11, PIX_TORCH1),
    paint(32, 11, PIX_TORCH1), paint(28, 49, PIX_TORCH1),
    paint(32, 49, PIX_TORCH1).

#### DECOR AMBIENCE (decor plane, all non-blocking)

- DECOR_BONES — the wolves' winter so far: hand-set at the den (27,5),
  (29,6); field kills (10,23), (47,25), (14,35); one by the pond (50,33).
- DECOR_PEBBLES on the road and green paths: Path tiles only, rect
  (0,0)-(59,59) mod 11 (a village road is well worn).
- DECOR_SHRUB hedgerow stubs in the FIELDS ONLY (LightGrass/dark-grass
  ground class): rects (6,20)-(16,26) and (44,22)-(54,28) mod 7 — off
  the green, off the road, off every wave lane (concealment stays out of
  the battle lanes; the lanes here are the two path spines and the road).
- DECOR_BOULDER: scatter_boulders(0, 2,12, 10,48, 27) west waste edge.

#### ARMIES

Team 0 — Thornby's own (all UNNAMED except the Reeve; guard = npc_flags bit1):

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis | bit2 |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|------|
| 1 | 0 | CLERIC "The Reeve" (9 chars) | 1 | 6 | YES | moot-house, hearth corner (29,28) — off the door's line of sight | 0 | YES | **YES (the only one)** |
| 2 | 0 | SOLDIER (door wards, unnamed) | 2 | 7 | YES | inside the door: (30,31),(31,31) | 0 | no | no |
| 3 | 0 | SOLDIER (militia, unnamed) | 4 | 4 | YES | palisade gaps (30,12),(30,48); road posts (10,29),(50,29) | 0 | no | no |

Reeve protection recipe (per the Westlands E4 pattern, simplified): no
ghosts exist on this map, so there is NO scare-wail force-march threat —
walls + two lvl-7 door wards + guard-held Reeve suffice; no raw-rock
cleft needed. The Reeve is a cleric (a ledger-keeping official, staff
not sword) with specials_disabled so the AI never walks him out the door
chasing a heal.

Team 2 — the packs (ORC = wolves, BIG_ORC = dire wolves, per convention):

| # | team | family | count | level | guard | placement (floor 0) | spawn_delay | spec.dis |
|---|------|--------|-------|-------|-------|---------------------|-------------|----------|
| 4 | 2 | ORC (field roamers) | 8 | 5 | no | (8,22),(13,25),(10,34),(16,37),(46,24),(52,27),(46,44),(24,40) | 0 | no |
| 5 | 2 | ORC (dusk wave 1, north treeline) | 6 | 5 | no | (22,8),(26,8),(32,8),(36,8),(24,9),(34,9) | 400 | no |
| 6 | 2 | ORC (dusk wave 2, south treeline) | 6 | 6 | no | (22,51),(26,52),(32,52),(36,51),(24,51),(34,51) | 900 | no |
| 7 | 2 | ORC (dusk wave 3, down the west road) | 5 | 6 | no | (2,26),(2,32),(4,28),(4,31),(6,29) | 1500 | no |
| 8 | 2 | BIG_ORC (wave 3 leaders) | 2 | 7 | no | (3,29),(5,27) | 1500 | no |
| 9 | 2 | BIG_ORC (the winter-king, unnamed) | 1 | 8 | no | den heart (28,5) | 2200 | no |
| 10 | 2 | ORC (the king's pack) | 4 | 6 | no | den (26,4),(30,4),(26,6),(30,6) | 2200 | no |

NO GENERATORS — deliberate, the level-2 Westlands lesson: on a SAVE_ALL
map an endless den guarantees eventual attrition against the protectee;
every pack here is BOUNDED, and wave 4 "empties" the den for good.

Totals: team 0 = 7 livings (1 named); team 2 = 32 livings; 0 generators.
Delayed spawns: 24 across four dusk waves (400/900/1500/2200 — dormant,
NEXT WAVE HUD counts the packs down, level_done held open until the den
empties). MAXOBS ledger: 39 livings + 10 markers + 2 exits + 8 treasures
= 59 objects; far under budget.

All wave cells sit on open snow (y8/9 north strip, y51/52 south strip,
x2..6 west road, the carved den) — every one A*-reachable from the lead
marker; self-check reachability allowlist EMPTY.

#### HEROES / NAMED

- "The Reeve" (9 chars, fits the 11-char field) — FAMILY_CLERIC, team 0,
  level 6, guard=YES, specials_disabled=YES, spawn_delay 0, npc_flags
  bit2 PROTECTED, floor 0 at (29,28) inside the moot-house by the
  hearth. SAVE_ALL watches him and ONLY him from tick 0.

#### START MARKERS (10; lead FIRST; on the green around the moot-house, 2x2 clearance, clear of walls/torches/wards)

1. (30, 35) — LEAD, on the road before the moot-house door, facing the fields
2. (26, 35)  3. (34, 35)  4. (25, 29)  5. (36, 29)
6. (26, 24)  7. (34, 24)  8. (30, 22)  9. (30, 38)  10. (36, 33)

(All on open green snow/path; the door torches at (29,32)/(32,32) and
the ward pair inside keep their own cells; markers keep 2 tiles off the
hall walls.)

#### TREASURE

- The Reeve's strongbox (moot-house): SILVER_BAR (32,27), SILVER_BAR
  (33,27), GOLD_BAR (33,28) — tithe coin; the gold bar is a warm-coin
  sample the Reeve kept back "for evidence".
- Village stores: DRUMSTICK (16,15) on open snow just east of the NW
  house block (12..15, 14..16); DRUMSTICK (43,43) just west of the SE
  house; DRUMSTICK (24,44) beside the S house block (20..23, 45..47).
- MAGIC_POTION (30,24) on the green; MAGIC_POTION (28,30) in the hall.

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (57, 33) | 16 — the east road, down to the ford (south shoulder, clear of the road posts and wave lanes) |
| 0 | (2, 35)  | 15->14 backtrack — the west road up to the Grey Tolls (3 rows south of wave-3 cells y26..32) |

Reachability: both exits sit on open snow adjoining the road; flat walks
from the lead marker.

#### BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, deep winter. Thornby is   (31)
snowed in and the wolves have     (29)
the roads. Pay is the Reeve's     (29)
word: he knows whose ledger our   (31)
warm coin balances. Keep the      (28)
old man alive till he says it.    (30)
```

Lines 3-5 are the act's warm-coin thread AND the skeleton's beat: the
Reeve knows the mint's ledgers. The dusk-wave mechanic is deliberately
NOT in the briefing (the NEXT WAVE HUD teaches it); the ledger cares
about the pay and the client.

#### BALANCE NOTES / CALIBRATION GATE (curve position: crew 7)

Curve: crew 7 entering (from 14, the meta table's winter rung), crew
7-8 leaving winter. Bracket sweep at {6, 7, 8} x seeds {42, 1337, 2025}
x rosters {A, B}.

- Battle shape: a defense that must end as a HUNT. Roamers pull the crew
  into the fields early; the dusk waves converge on the village from
  three sides at 400/900/1500; the den wave at 2200 is the last count on
  the HUD and the kill-all's final errand — the crew must eventually
  leave the palisade and take the den (mouth at (28,7..9)).
- KILL-ALL GATE (type 4 counts as kill-all in the F4 form): the 8-mixed
  roster at crew 7 reaches level_done==1 (exits present -> 1, not 2)
  within 6000 ticks on >=2/3 seeds AND 3/3 at crew 8. Crew 6 may fail.
- SAVE_ALL SUB-GATE (the binding one): NO run at curve, either roster,
  where the Reeve dies before the crew wipes (census-tick granularity,
  3/3 seeds). The wards + walls must hold wave 1 and 2 leakage without
  crew help; wave 3 (the road wave, lvl 6-7 with dire leaders) is
  allowed to reach the hall ONLY if the crew ignored it — sweeps should
  show ward HP touched but standing at curve.
- Wave sanity: each wave engages (roamers by 300, w1 by 700, w2 by 1300,
  w3 by 2000, den by 2800 at the latest) — no wave may arrive after the
  battle is decided; if sweeps show the den wave mopping into an empty
  field, pull 2200 down to 1800.
- Structural watches: the den mouth is a 1-wide carve — watch for wedged
  melee pairs in it (widen to 2 if the sweeps wedge); SHRUB concealment
  is field-only, so LOS pathologies on the green should not appear; the
  mill pond must not trap SE roamer pathing.
- SAVE_ALL trap to assert in-sim: killing the Reeve emits
  EndGame(SCEN_TYPE_SAVE_ALL); killing a door ward does NOT (bit2
  scoping pin).
- Weather smoke: >=40 snow tiles, WeatherKind::Snow forced.
- Pins to add: roster row {15, t0 7, t2 32, gens 0, markers 10, exits
  {16, 14}}, delayed 24, ReevePin {level 6, cleric, guard, bit2, the
  only bit2 walker on the map}, briefing 6 lines.

### Level 16 — The Frozen Ford (CAN_EXIT collapsing-route run)

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

#### STORY POSITION

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

#### TERRAIN PLAN (floor 0, the only floor)

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

#### DECOR AMBIENCE (decor plane, all non-blocking)

- DECOR_BONES frozen INTO the ice (last year's crossing): hand-set
  (35,10), (58,9) north lane; (33,21), (55,20) mid lane; (40,32),
  (60,31) south lane; (47,16) islet.
- DECOR_PEBBLES on the bank roads: Path tiles only, rect (0,0)-(89,39)
  mod 9 (a ford road is gravel).
- DECOR_SHRUB on the bank scrub patches ONLY ((12,12)-(18,17) and
  (74,24)-(80,29) mod 5, dark-grass ground class) — nothing concealing
  on the ice; the lanes stay honest.
- NO decor of any kind on the connectors (they are the choke cells).

#### ARMIES (team 2; guards via npc_flags bit1; every dormant wave wakes BEHIND the lead marker)

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

#### HEROES / NAMED

None placed. Nothing on this map is named, so nothing can trip loss
logic; the pay chest travels in fiction only (the briefing carries it —
placing a bearer-style cargo here would demand SAVE_ALL and this level
is the one breather where the company risks only itself).

#### START MARKERS (10; lead FIRST; west bank open snow, 2x2 clearance; ALL at x >= 6, clear of the x1..4 pursuit cells)

1. (9, 20) — LEAD, on the ford road, facing the ice
2. (6, 16)  3. (6, 24)  4. (8, 16)  5. (8, 24)
6. (11, 17)  7. (11, 23)  8. (13, 20)  9. (6, 20)  10. (13, 24)

#### TREASURE

- The islet cache (a sunken toll-cart's strongbox): GOLD_BAR (46,14),
  GOLD_BAR (49,17), MAGIC_POTION (48,14) — warm coin again; the islet
  ice around it is authored as the widest melt (the mid-lane break sits
  beside it — the coin did that).
- Dropped supplies on the lanes (a fighting company eats mid-run):
  DRUMSTICK (36,9) north lane, DRUMSTICK (42,20) mid lane,
  DRUMSTICK (50,31) south lane.
- SPEED_POTION (28,20) at the west ice-edge — the "go NOW" prop.

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (86, 20) | 17 — east bank, the road up to Ashfall Gate (CAN_EXIT: usable while foes remain) |
| 0 | (5, 27)  | 16->15 backtrack — the Thornby road, west bank (clear of pursuit cells x1..4/y16..23 and of all markers; declining the crossing = the withdraw flow) |

#### INTERCEPT GEOMETRY (verify on the built package with the A* solve)

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

#### BRIEFING (6 lines, each <=33 chars — counted)

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

#### BALANCE NOTES / CALIBRATION GATE (curve position: crew 7, winter's exit)

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

### Level 17 — "Ashfall Gate" (THE RECKONING, part 1)

- id: 17
- title: `Ashfall Gate` (12 bytes, <=30 OK)
- type_bits: **0** (kill-all, then walk to the gate exit). PROTECTED-BIT
  PLAN: none — no SAVE_ALL here, no npc_flags bit2 on anyone. No named
  team-0 NPCs are placed at all (Kettle appears only in 4/9/18 per the
  skeleton cast list), so an accidental SAVE_ALL bit could never scope
  correctly anyway; the bit stays off.
- floors: 1
- grid: 70 x 44 (x 0..69, y 0..43)
- par_value: 7
- time_bonus_limit: 5500

Fiction: the foundry-city's west gate. Every company the year stiffed is
camped on the ash, and the Brass Kettle Company has to fight through its
own kind to present its bill first. The slag runnels leaking out under the
wall are the first sight of the melt — the warm coin's source, one level
early.

#### TERRAIN PLAN (floor 0 only)

init_world(level, 1, 70, 44) — base fill grass (walls smooth against it).

Pre-smooth paints (PIX_WALL2):
1. City wall, full east edge: paint_rect(64, 0, 69, 43, PIX_WALL2).
2. Gate passage carve (placeholder grass; re-ashed post-smooth):
   paint_rect(64, 19, 69, 24, PIX_GRASS1) — a 6x6 throat through the wall.
3. Gatehouse towers jutting west, framing the approach:
   paint_rect(60, 15, 63, 18, PIX_WALL2) and
   paint_rect(60, 25, 63, 28, PIX_WALL2) — approach channel x 60..63,
   y 19..24 (6 wide) between them.
4. North camp palisade (the horse companies), opening SOUTH:
   paint_rect(16, 5, 28, 5, PIX_WALL2); paint_rect(16, 5, 16, 11, PIX_WALL2);
   paint_rect(28, 5, 28, 11, PIX_WALL2). Interior (17..27, 6..11).
5. South camp palisade (the hired bows and casters), opening NORTH:
   paint_rect(16, 38, 28, 38, PIX_WALL2); paint_rect(16, 32, 16, 38,
   PIX_WALL2); paint_rect(28, 32, 28, 38, PIX_WALL2). Interior
   (17..27, 32..37).
6. Center wagon-camp (the foot companies), two wagon-walls astride the
   road: paint_rect(36, 16, 44, 17, PIX_WALL2) and
   paint_rect(36, 26, 44, 27, PIX_WALL2). Corridor interior x 36..44,
   y 18..25 (8 tiles wide — man-sized 2x2 walkers pass freely).

smooth_world(w).

Post-smooth (all genre-inert paint):
7. Ash the plain: `paint_ash_open(grid0, 0, 0, 63, 43)` — NEW HELPER for
   the longseason mapgen clone: identical checker recipe to westlands
   paint_ash but SKIPS non-ground tiles (smoothed wall/tree cells), so one
   call ashes the whole plain around the palisades. Then re-ash the carved
   gate passage unconditionally: paint_ash(64, 19, 69, 24).
8. Slag runnels (PIX_LAVA1/2 checker — impassable to ground, the foundry
   leaking under its own wall):
   - north runnel: paint_lava(30, 13, 62, 14)
   - south runnel: paint_lava(30, 29, 62, 30)
   - causeway gaps (re-ash, 3 wide): paint_ash(34, 13, 36, 14) and
     paint_ash(46, 29, 48, 30)
   The runnels pin the road band (y 15..28) as the main approach; the
   flanks cross only at the two gaps.
9. The road: paint_path(2, 21, 63, 22) — west edge to the gate throat.

Decor (paint AFTER armies are placed; blocking decor off all routes):
10. Gate braziers: paint_decor DECOR_BRAZIER at (63, 18) and (63, 25) —
    the throat corners, off the 6-wide passage.
11. Camp watch-torches (DECOR_TORCH1) flanking the openings:
    north camp (17, 12) and (27, 12); south camp (17, 31) and (27, 31).
12. Cook-fire braziers: (26, 10) north camp NE corner; (18, 37) south
    camp SW corner (both clear of the mage/elf footprints and the camp
    openings). The wagon camp gets NO brazier — its corridor is the
    spine battle lane and stays clean of blocking decor.
13. scatter_boulders(w, 0, 0, 0, 15, 43, 25) — sparse west-margin scree.
14. E7-style ambience (scatter_decor, non-blocking):
    - old bones on the ash: `(0,0)-(59,43)` mod 21 {Ash}, thickening in
      the approach channel `(54,15)-(63,28)` mod 9 {Ash} — companies have
      died at this gate before.
    - road pebbles `(2,19)-(63,24)` mod 11 {Path}.
    - cinder-grit `(16,2)-(48,41)` mod 23 {Ash} around the camps.

#### ARMIES (team 2 — the creditor companies + the city's gate wards)

All floor 0. 48 livings + 2 generators; MAXOBS ledger: 48 livings +
2 gens + 10 markers + 11 treasures + 2 exits = 73 objects — ample
headroom under 150 for the two lvl-3 trickles.

| # | team | family | count | level | guard | placement | delay | spec.dis |
|---|------|--------|-------|-------|-------|-----------|-------|----------|
| 1 | 2 | SOLDIER (foot companies) | 10 | 5+i%2 | no | wagon corridor: (37,19),(39,19),(41,19),(43,19),(37,24),(39,24),(41,24),(43,24),(39,21),(41,22) | 0 | no |
| 2 | 2 | ARCHER (their pickets) | 4 | 5 | YES | corridor mouths: (35,19),(35,24),(45,19),(45,24) | 0 | no |
| 3 | 2 | BARBARIAN (horse companies) | 6 | 6+i%2 | no | north camp: (18,6),(21,6),(25,6),(18,9),(21,9),(22,11) | 0 | no |
| 4 | 2 | ELF (the hired bows) | 6 | 5+i%2 | no | south camp: (18,33),(21,33),(24,33),(18,36),(21,36),(24,36) | 0 | no |
| 5 | 2 | MAGE (the hired casters) | 2 | 6 | YES | south camp east side: (26,33),(26,36) | 0 | no |
| 6 | 2 | GOLEM (the city's gate wards, cast from the warm metal) | 2 | 9 | YES | approach channel: (61,20),(61,23) | 0 | no |
| 7 | 2 | BIG_ORC (door-muscle) | 4 | 7 | YES | tower feet + passage: (58,16),(58,27),(66,20),(66,23) | 0 | no |
| 8 | 2 | SOLDIER (the second watch) | 8 | 6 | no | camp rears: (17,2),(20,2),(23,2),(26,2),(17,41),(20,41),(23,41),(26,41) | 350 | no |
| 9 | 2 | THIEF (cut-purses — they come for OUR chest) | 6 | 5 | no | west margin, behind the crew: (10,10),(14,13),(12,6),(10,34),(14,30),(12,38) | 700 | no |

Guard notes: rows 2/5/6/7 hold posts via npc_flags bit1 (start-as-guard).
The golem pair leaves only 1-wide seams in the approach channel — a 2x2
crew cannot slip the wards; kill-all means they must fall anyway. The gate
fight is the level's boss beat.

Generators:

| family | team | level | floor/cell | note |
|--------|------|-------|------------|------|
| FAMILY_TENT (skeletons) | 2 | 3 | 0 / (24, 8) — 4x4 inside north camp (24..27, 8..11) | camp trickle |
| FAMILY_TOWER (mages) | 2 | 3 | 0 / (21, 39) — 4x4 behind the south palisade (21..24, 39..42) | camp trickle |

Footing: every placement on ash/path; nothing on lava/wall/tower cells;
all 2x2 footprints checked pairwise non-overlapping (incl. the TENT 4x4 vs
barbarians and the TOWER 4x4 vs the south wall at y38).

#### HEROES / NAMED

None placed. The company's own names stay in the ledger this level; Kettle
is 4/9/18 only (skeleton cast list). The Sergeant never deploys.

#### START MARKERS (10; lead FIRST; every anchor 2x2 with shoulder room)

West edge, wedge on the road, point east:
1. **(6, 21)** — LEAD, on the road under the banner
2. (4, 18)   3. (4, 24)
4. (2, 15)   5. (2, 21)   6. (2, 27)
7. (8, 17)   8. (8, 25)
9. (4, 12)   10. (4, 30)

(Delayed thieves at x 10..14 are >=2 tiles clear of every anchor; they are
dormant until 700 and wake with a flash behind the crew's line.)

#### TREASURE (every camp has a pay chest; all of it warm)

- North camp: GOLD_BAR (26,6),(26,7); SILVER_BAR (17,11); DRUMSTICK (19,11)
- Wagon camp: GOLD_BAR (44,21),(44,22)
- South camp: GOLD_BAR (17,37),(27,37); SILVER_BAR (17,32); DRUMSTICK (19,32)
- Road cache before the wards: MAGIC_POTION (54,21)

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (67, 21) | **18** — through the gate, into the Warm Mint |
| 0 | (1, 26)  | **16** — backtrack, the road back to the Frozen Ford (withdraw = declining the reckoning) |

Walkable route: lead (6,21) → road y21-22 → wagon corridor (8 wide) →
road band between the runnels → approach channel (60..63, 19..24, through
the golem wards) → gate passage (64..69, 19..24) → (67,21). Backtrack is
5 tiles from the lead anchor, offset off the road; no marker overlaps it.

#### BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, ash season. Every company (33)
the year stiffed is camped at the (33)
foundry gates, howling for pay.   (31)
So are we. The line forms behind  (32)
our banner. Collect at the gate;  (32)
the coin inside is still warm.    (30)
```

Warm-coin thread: line 6 (and the golem wards ARE the warm metal, per the
fiction note — the briefing keeps it to coin, the terrain shows the melt).

#### BALANCE NOTES / calibration

Curve position: crew power 8 entering (the meta table pins 17 at 8; THE
RECKONING assumes the winter contracts are done). Level type: **kill-all**.

Battle shape: the wagon corridor is the spine fight (10 soldiers + 4
picket archers); the flank camps commit as the crew passes their openings
(no guard on rows 3/4, so barbarians/elves stream out once alerted). At
tick 350 the second watch (8 soldiers) folds in from the camp rears —
timed to land while the corridor fight is still live. At 700 the six
cut-purses wake BEHIND the crew, on the crew's own treasure line — the
rear is never safe on Settlement's eve. The gate ward pair (lvl-9 golems +
door-muscle) is a deliberate wall: kill-all keeps it honest, and the two
generators must be torn out (they don't hold level_done, but they flood
if ignored — TENT/TOWER at lvl 3 are trickles, not floods, per the F4
generator-trim lesson).

Calibration gates (kill-all, curve 8; bracket sweep {7,8,9} x seeds
{42,1337,2025} x rosters {4-soldier A, 8-mixed B}):
- 8-mixed @8 reaches level_done==1 within 6000 ticks on >=2/3 seeds,
  AND 3/3 @9. @7 may fail. 4-soldier roster recorded as the floor.
- Wave sanity: both delayed groups must arrive before the battle is
  decided in curve runs (team-2 alive > 12 at tick 700 expected; if the
  camps melt faster, deepen rows 1/3/4 one level before touching delays).
- No generator flooding: team-2 alive at 6000 in LOST runs stays < 60
  (lvl-3 trickle pin).

300-tick smoke (stand-in crew 4x lvl 8 on markers 1-4, 3 seeds):
- Team 0 not extinct; team-2 alive <= 40 (corridor fight underway).
- All 8 guards (archers/mages/golems/big orcs) alive at 300 — nothing
  reaches the gate early; the boss beat keeps.
- Delayed spawns round-trip: census 14 dormant at tick 300 (8@350 not yet
  woken at the 300 checkpoint, 6@700), and they hold level_done open.
- Static pins: floors=1, grid 70x44, 10 markers, team2 livings 48 (±3),
  2 generators, exits dests {18, 16}, footing audit clean, fall-line
  audit clean (no PIX_AIR on the level at all).
- Recipe C reachability: A* lead (6,21) → (67,21) and → (1,26) non-empty;
  every living + both generators A*-reachable from the lead marker
  (empty allowlist — the palisade camps all have openings, the wall
  passage is carved).

### Level 18 — "The Warm Mint" (CAMPAIGN FINALE)

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

#### TERRAIN PLAN

Interior level: no weather. BRAZIER/FLAME decor everywhere is the finale's
signature — every door, stair, and hall is fire-lit, and the lava channels
carry the cycled-flame band through all three floors.

##### Floor 0 — the gatehall and the casting floor

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

##### Floor 1 — the vault floor (collapsing into the melt)

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

##### Floor 2 — the crucible floor

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

##### Stair summary (self-check: >=1 aligned pair per boundary)
- boundary 0→1: (57, 21)
- boundary 1→2: (9, 21)

#### THE CLIMB ROUTE (provable, on foot, from the lead marker)

(6,21) gatehall → east doors (18,10..13 or 18,26..29) through the
creditors' push → casting floor bands past the furnace-men and the
channel-gap elementals → east door (52,19..24) → golem-warded stair
chamber → UP (57,21) → floor-1 landing → vault hall, threading holes and
melt pools, past the heap wards → west doors → counting rooms → UP (9,21)
→ floor-2 west landing → north or south walk (each held by a lvl-9 fire
elemental) → the dais, The Founder, and the master ledger. Self-check
must A*-solve lead → both stair cells and lead → both exits.

#### ARMIES

##### Team 1 — the creditor companies (28 livings; the campaign's only
team-1 use — hostile to the mint AND to us; the war "rages below")

All floor 0.

| # | family | count | level | guard | placement | delay | spec.dis |
|---|--------|-------|-------|-------|-----------|-------|----------|
| 1 | SOLDIER | 10 | 6 | no | gatehall, pressing the doors: (11,6),(14,6),(11,10),(14,10),(11,15),(14,15),(11,28),(14,28),(11,33),(14,33) | 0 | no |
| 2 | BARBARIAN | 4 | 7 | no | at the door mouths: (16,11),(16,33),(13,12),(13,31) | 0 | no |
| 3 | ARCHER | 4 | 6 | no | rear rank: (5,6),(5,10),(5,32),(5,36) | 0 | no |
| 4 | THIEF | 4 | 6 | no | already looting the casting floor: (24,12),(40,16),(30,28),(44,30) | 0 | no |
| 5 | SOLDIER | 6 | 7 | no | more companies force the doors behind us — NW/SW corners: (3,3),(6,3),(9,3),(3,39),(6,39),(9,39) | 600 | no |

##### Team 2 — the mint (33 livings + 1 generator)

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

#### HEROES / NAMED (team 0)

| name | chars | family | level | floor | cell | guard | delay |
|------|-------|--------|-------|-------|------|-------|-------|
| Kettle | 6 | SOLDIER | 8 | 0 | (3, 33) | YES | 0 |

(FAMILY_SOLDIER, matching his 4 and 9 placements — the quartermaster
keeps one face all year.) Kettle holds the door pocket beside the
backtrack exit — "Kettle holds the door" is literal: ACT_GUARD keeps
him at the west wall, anchoring whoever falls back, and the team-1 push
breaks on the crew before it reaches him. Protect-OPTIONAL: his death is a ledger line, not
a mission failure (no SAVE_ALL, no bit2 — see type_bits above).

#### START MARKERS (10; lead FIRST; 2x2 anchors, all clear of t1 ranks)

Gatehall west pocket, wedge pointing at the doors:
1. **(6, 21)** — LEAD
2. (4, 18)   3. (4, 24)
4. (8, 18)   5. (8, 24)
6. (6, 15)   7. (6, 27)
8. (4, 15)   9. (4, 27)
10. (8, 21)

The creditors' nearest rank stands at x11 — the brawl starts immediately
but nobody deploys in contact.

#### TREASURE (the season's pay, heaped where the vault is breaking)

Floor 1 — four heaps, one golem ward each (12 GOLD_BAR, 6 SILVER_BAR):
- Heap 1 (NW): GOLD (25,5),(26,6),(27,5); SILVER (25,7)
- Heap 2 (NE): GOLD (44,14),(45,15),(46,14); SILVER (44,16)
- Heap 3 (SW): GOLD (23,30),(24,31),(25,30); SILVER (23,32)
- Heap 4 (SE): GOLD (41,36),(42,37),(43,36); SILVER (41,38)
- Loose: SILVER (57,25) at the landing; SILVER (12,10) counting room
- DRUMSTICK (12,12),(12,30) — the clerks' suppers, still warm too
- INVULNERABLE_POTION (7,19) — beside the floor-1 stair, for the summit
Floor 0: MAGIC_POTION (33,14) on the casting floor (band1, off the gap).

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 2 | (55, 21) | **19** — THE MASTER LEDGER: reading the book settles the season; on to Settlement Day |
| 0 | (2, 36)  | **17** — backtrack, the doors behind us (withdraw = declining to collect; 17 is always completed by now, so this is the withdraw prompt, never a completion) |

The dais exit sits 3 tiles east of The Founder's guard post — the ledger
is read over his body or not at all. CAN_EXIT means a ghost-ahead player
could in principle dodge past; the two rim elementals and the 3-wide
walks make that a real gamble, and that is accepted finale design.

#### BRIEFING (6 lines, each <=33 chars — counted)

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

#### BALANCE NOTES / calibration

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

### Level 19 — "Settlement Day" (EPILOGUE — full circle, loops to 1)

- id: 19
- title: `Settlement Day` (14 bytes, <=30 OK)
- type_bits: **0** (kill-all, then walk to the winter-quarters exit).
  PROTECTED-BIT PLAN: none — no named NPCs are placed, so no bit2 anywhere.
- floors: 1
- grid: **44 x 30** (x 0..43, y 0..29) — deliberately tiny; dessert
- par_value: 2
- time_bonus_limit: 2500

Fiction: back at the Weycombe dikes where the year began (level 1 "Mud
Pay"). Rich or broke, the company clears its OWN dike one last time —
the granary job again, on the house — then walks into winter quarters.
The exit loops to level 1: next spring, the ledger opens again.

FULL-CIRCLE NOTE for the level-1 designer: the callback is THEMATIC, not
byte-level. This map is an independent tiny dike scene (level 1's map is
authored concurrently and larger); if a shared `paint_weycombe_motifs`
helper turns out practical (granary shell + dike path + marsh belt), take
it, but do not block either level on it. The briefing's "first thaw
again" line is the load-bearing callback.

#### TERRAIN PLAN (floor 0 only)

init_world(level, 1, 44, 30) — base fill grass.

Pre-smooth:
1. North treeline: paint_rect(0, 0, 10, 2, PIX_TREE_M1).
2. Floodwater pool (the winter melt): paint_rect(6, 24, 12, 28, PIX_WATER1).
3. The granary: paint_rect(18, 4, 26, 10, PIX_WALL2).
4. The winter quarters (the company's house), east:
   paint_rect(34, 8, 40, 13, PIX_WALL2).

smooth_world(w), then post-smooth:
5. Interiors + doors (pavement over wall makes the doorway):
   - granary: paint_pavement(19, 5, 25, 9); door paint_pavement(22, 10, 22, 10)
   - house: paint_pavement(35, 9, 39, 12); door paint_pavement(37, 13, 37, 13)
6. The dike (a worn embankment path, west-east):
   paint_path(2, 18, 41, 19).
7. Lanes: granary lane paint_path(22, 11, 22, 17); house lane
   paint_path(37, 14, 37, 17).
8. The marsh belt (paint_marsh checker, PIX_MARSH1/2 — walkable bog),
   south of the dike, parted around the pool:
   - paint_marsh(0, 22, 43, 23) — the long fringe under the dike
   - paint_marsh(0, 24, 5, 29) — west of the pool
   - paint_marsh(13, 24, 43, 29) — east of the pool

Decor (after armies; blocking decor off the dike, lanes, and doors):
9. Door torches (DECOR_TORCH1): granary (21,11),(23,11); house
   (36,14),(38,14) — flanking the lanes, never on them.
10. A single homecoming brazier by the house door: DECOR_BRAZIER (40,14)
    — lit for the crew's return.
11. Ambience (scatter_decor, non-blocking):
    - marsh bones `(0,22)-(43,29)` mod 17 {Marsh} — what the flood left
    - dike pebbles `(2,16)-(41,21)` mod 9 {Path}
    - hedgerow shrubs under the treeline `(0,3)-(12,8)` mod 13 {Grass}
    - scatter_boulders(w, 0, 12, 0, 43, 3, 27) — sparse north-field stones

#### ARMIES (team 2 — what the flood left, one last time)

16 livings, 0 generators. All floor 0.

| # | team | family | count | level | guard | placement | delay | spec.dis |
|---|------|--------|-------|-------|-------|-----------|-------|----------|
| 1 | 2 | MEDIUM_SLIME | 4 | 3 | no | the marsh: (6,22),(16,25),(26,23),(34,26) | 0 | no |
| 2 | 2 | ORC (the dike vermin, as in level 1) | 6 | 2+i%2 | no | on and under the dike: (12,18),(16,19),(20,18),(28,18),(24,15),(32,19) | 0 | no |
| 3 | 2 | ORC | 2 | 3 | YES | inside the granary, gnawing the grain: (20,7),(24,7) | 0 | no |
| 4 | 2 | MEDIUM_SLIME (the marsh gives one more push at dusk) | 4 | 3 | no | marsh edge: (14,26),(22,26),(30,26),(38,25) | 300 | no |

Footing: slimes on marsh (walkable), orcs on path/grass/pavement; nothing
on water, walls, or trees. Every 2x2 footprint pairwise clear of markers,
treasure, and doors.

#### HEROES / NAMED

None placed — deliberately. Kettle's placements are 4/9/18 (skeleton cast
list) and the epilogue belongs to the crew alone; the briefing keeps the
company voices present. No SAVE_ALL, nothing protected, nothing to lose
but the afternoon.

#### START MARKERS (8; lead FIRST; 2x2 anchors with shoulder room)

The crew walks in from the west road, on the dike:
1. **(4, 18)** — LEAD, on the dike path
2. (2, 15)   3. (2, 21)
4. (6, 15)   5. (6, 21)
6. (8, 18)   7. (4, 14)
8. (4, 21)

Nearest enemy is the orc at (12,18) — 4 tiles east of the lead's
footprint edge; the fight starts on the dike, as it did in spring.

#### TREASURE

- DRUMSTICK (21,6),(23,8) — the granary, stocked this time
- GOLD_BAR (35,10),(39,11) — the season's pay on the winter-quarters
  table (what survived the mint)
- MAGIC_POTION (13,26) — the pool fringe cache, where Weycombe always
  keeps one

#### EXITS

| floor | cell | destination |
|-------|------|-------------|
| 0 | (37, 10) | **1** — the winter quarters; the ledger closes, and next spring it opens on Mud Pay again (the campaign loop) |

The exit sits INSIDE the house (interior 35..39 x 9..12, door at 37,13),
between the pay bars — collecting the season and ending it are the same
walk. NO backtrack exit, deliberately: the Warm Mint is burning behind
the company and the skeleton graph ends the year here; the loop-to-1 exit
is the only door (westlands-26 precedent for a terminal loop level).

Walkable route: lead (4,18) → dike y18-19 → house lane (37,14..17) →
door (37,13) → (37,10). Granary loop via lane (22,11..17) and door
(22,10). Marsh is walkable everywhere the slimes sit.

#### BRIEFING (6 lines, each <=33 chars — counted)

```
Ledger, first thaw again. Home.   (31)
The season paid, the book square. (33)
One warm coin nailed over the     (29)
door, so we remember the year.    (30)
The dike crawls. One last job,    (30)
on the house. Then winter beer.   (31)
```

Warm-coin thread: lines 3-4 — the mystery's coda: one coin kept, not
spent, nailed up as a warning and a trophy.

#### BALANCE NOTES / calibration

Curve position: crew power 8-9 (dessert — the epilogue NEVER spikes; the
curve table pins it as such). Level type: **kill-all**.

Battle shape: a victory lap with a pulse. Six lvl-2/3 dike vermin meet
the crew on the embankment; four marsh slimes ooze up from the south;
at tick 300 four more wake at the marsh edge (a token NEXT WAVE beat so
the HUD's last use is the year's smallest fight). The two granary orcs
are ACT_GUARD door-squatters — the last "boss" of the campaign is two
rats in the grain, on purpose.

Calibration gates (kill-all, dessert rules — stricter than the standard
kill gate because it must never wall anyone off from the loop):
- 8-mixed clears (level_done==1 within 6000 ticks) 3/3 seeds at crew 9,
  3/3 at crew 8, AND 3/3 at crew 7 (curve-2: even a mauled crew limping
  out of the mint finishes the year).
- 4-soldier floor roster clears 3/3 at crew 8 (westlands-25/26 precedent:
  dessert levels clear on the pessimistic floor too).
- Expected clear ticks 400-1200; if any seed runs past 2500, the marsh
  slime wave placement has a pathing dead zone — fix geometry, not levels.

300-tick smoke (stand-in crew 4x lvl 8 on markers 1-4, 3 seeds):
- Team 0 not extinct (trivially); team-2 alive <= 8 by tick 300 (the
  dike sweep is fast).
- The 2 granary guards alive at 300 (guard, indoors — the beat waits for
  the player's walk-in, as with the westlands-25 chief).
- Delayed round-trip: 4 dormant at 300 boundary (wake tick 300 —
  assert dormant at tick 299 / awake by 350), holding level_done open.
- Static pins: floors=1, grid 44x30, 8 markers, team2 livings 16 (exact
  — small enough to pin without tolerance), 0 generators, exit dest {1},
  footing audit clean, no PIX_AIR anywhere.
- Recipe C: A* lead (4,18) → exit (37,10) non-empty; every living
  reachable from the lead marker (empty allowlist — granary and house
  doors are carved, marsh is walkable).
- Weather: 0 snow tiles — spring thaw; the Snow roll must stay possible=
  no (assert the forced-snow threshold is not tripped).
