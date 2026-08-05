# The Long Season — original campaign skeleton (Fable's design, draft 1)

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

## SPRING — the flood contracts (marsh/water, gentle start)
- 1 "Mud Pay" — clear drowned vermin (slimes/wolves-as-orcs lvl1) from the
  Weycombe granary dikes. Ledger: "Took the granary job. Pay is grain."
- 2 "The Ferry Right" — hold a flooded causeway (marsh decor, water) against
  river bandits so the toll-ferry runs. First odd coin appears in pay.
- 3 "Saltmere Bell" — OPTIONAL contract: dive the sunken bell-tower
  (multifloor: tower top above water, flooded floor below) for the temple.
- 4 "The Assessor" — escort the crown assessor through bandit country
  (SAVE_ALL named NPC "The Assessor", the campaign's first protect job).
  He pays in the warm coin and won't say who minted it. HUB EXIT CHOICE.

## SUMMER — the war contracts (fields/sieges; the company gets a name)
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

## AUTUMN — the under contracts (mines/catacombs, multifloor)
- 10 "The Ledger Debt" — the company's OWN debt is bought by the Foundry;
  forced contract: clear the Undermill (2-floor mill over race channels).
- 11 "Cold Seams" — deep mine sweep; the seams are warm to the touch
  (lava veins visual, first hint the metal is wrong). OPTIONAL branch 12.
- 12 "The Old Count's Vault" — OPTIONAL: crack a dead lord's vault
  (catacombs, bones decor, giant skeletons, big treasure, guard puzzles).
- 13 "The Smelter's Road" — escort ore wagons down the switchbacks under
  ambush (autumn = first snow decor dusting; guards + pursuit waves).

## WINTER — the pass contracts (snow, blizzards)
- 14 "The Long Toll" — hold the Grey Tolls pass IN WINTER (snow + Snow
  weather; the toll fort from 7 if taken — callback briefing either way).
- 15 "Wolf Winter" — relieve a snowed-in village (wolf packs, delayed
  waves at dusk; SAVE_ALL the village reeve who knows the mint's ledgers).
- 16 "The Frozen Ford" — cross the iced river as it breaks (water/ice=snow
  tiles over water pattern, collapsing route = CAN_EXIT run level).

## THE RECKONING — the foundry-city (ash/lava finale arc)
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
