# imaginations — provenance

GENERATED CAMPAIGN. `scen/`, `pix/`, `campaign.yaml` and `icon.png` are
regenerated wholesale by `tools/imaginations_mapgen`
(`scripts/generate_imaginations_campaign.sh`) — do not hand-edit them;
port changes into the generator. Every generated scen carries the
`SCEN_TYPE_GENERATED` bit in its `.fss` header (the level editor warns on
open), and the CI `campaign-drift` job reruns the generator and fails on
any diff.

This file is repo documentation: `scripts/make_glad.py` excludes README.md
from the composed `.glad` archive.

## The dream-log

Imaginations is the community's kids-design-it campaign: each scenario is
a level idea a young player described, built faithfully and framed as a
page of one shared "dream-log". The narrative voice for every briefing is
the dream-log itself — "From the dream-log of a young commander: …" — so
levels from different kids read as one growing book of battle dreams.

Level ideas are transcribed as-submitted into the level's design comment
in `tools/imaginations_mapgen/main.cpp`; submitters stay anonymous unless
their family asks otherwise (add credits to `contributors:` in the
generator's `write_campaign_yaml`).

## Level ledger

| scen | title | submitted idea (condensed) |
|------|-------|----------------------------|
| 1 | The Raspberry Isle | "We start at the edges of the island, at every corner. The sea is all around us. The enemies start in the middle, in a castle, near the water. We must run at them right away." (64x64; double-walled castle — bailey + keep — 19 posted foes, 3 college generators, two timed reinforcement waves, reef atolls, landing piers, bramble groves. Rebalanced 2026-08-11 down from the statue-era 25 when the hold-post wake bug was fixed and the garrison started genuinely fighting.) |
| 2 | The Castle of Gold | Our own yellow castle in a faerie war. Four strongrooms of gold inside, each behind a row of doors; one of us keeps the castle and every gold bar taken wakes six ghosts for our side at the Ghost Tent, the last bar seven. Enemies are faeries only, pouring in from the map's edges; they must carry twelve keys between them to open the gate, and nobody teleports into the castle. (64x64; 3-thick castle ring with a 3-door gate, four 8x8 strongrooms with 3-door rows, two rivers with five bridges each, a grove, a field camp, a counting house, two teleporter pairs; 27 faeries — 4 awake, waves at ticks 400/900/1500, the Fae Queen and four knights at 2200; exit on the hall dais.) |
| 3 | The Asteroid Castle | Our tall castle on an asteroid in space, with spikes all around its walls; bad guys think it is their castle, come close and get caught. The Big One sits on a floating rock and must be made to fall; the key is up there on the other floor, and the big door is the way out. A purple man is on our side. (64x64, 2 floors. Floor 0: the rounded asteroid on black space, the castle on the west rim with walls 2 thick and a 3-wide east gate, a 2-wide band of jagged spikes around the walls, the crown V, the big door (three doors at key 3) sealing the exit; yard posts, the pen in the southeast, craters, stardust drifts that bring the snow. Floor 1: sky, and the striped Balloon Rock over a pit of space, reached by a 3-wide stair. 22 foes: 10 posted, the Big One on the rock, and 11 raiders in three waves at ticks 300/800/1400, the last led by Black Dash. The Purple Man holds the ground by the gate.) |

Growth rule: new ideas append new scens (2, 3, …). Each level's exit
leads to the next; the newest level's walk-out exit loops home to 1, and
when its successor lands the generator retargets that exit to the new
scen. `first_level` stays 1. Today: 1 → 2 → 3 → 1.

The Castle of Gold's beats live in
`packs/imaginations.dreams/scripts/gold_castle.lua`:
- Gold: the script overrides `core:gold_bar` on_eat for the whole
  campaign. Off scen 2, and for the counting-house bar on it, the body is
  the classic one (team 0 or a roster character banks). A castle bar
  banks only for a keeper (a saved character or a player-driven walker),
  so allied ghosts floating through the strongrooms leave the gold alone,
  and wakes six allied ghosts at the tent (seven for the last bar). New
  ghosts stop at 110 livings.
- Keys: the gate's three doors are team 0 (the crew and its ghosts walk
  through their own gate) at key level 30, which no key opens. The script
  (a `core:key` on_eat override plus a check every 100 ticks) drops them
  when the living faeries carry shore keys 1..12 between them; a bearer's
  death takes its keys out of the count. The strongroom doors are neutral
  (team 4) at key 13, the castle key by the keeper's start.
- No teleporting in: while the gate stands, any hostile inside the
  castle, and any of ours landing a teleport there, is thrown out onto
  the causeway.
- Stand-down: faeries fly the rivers but not walls, so a hunting faerie
  within two tiles of the castle ring or the counting house is posted
  every 50 ticks; the engine's sight test re-wakes it. Out in the field
  the waves keep hunting. (The stand-down loop itself is shared by every
  level: `packs/imaginations.dreams/lib/dream_posts.lua`; each level
  passes the test for which hunters it posts.) `og.register_hooks` on `core:gold_bar` and
  `core:key` may come from ONE chunk only; a later level that needs
  either shares that dispatcher.

The Asteroid Castle's beats live in
`packs/imaginations.dreams/scripts/asteroid.lua`:
- Spikes: jagged ground is impassable to walkers AND flyers, so the band
  around the walls is a barrier the raiders pile up against. Every ten
  ticks the script bites (12 hp, a flash and a clang) every awake hostile
  on floor 0 whose centre tile is on or beside a spike. The crew is never
  bitten. The gate apron is spike-free.
- The Big One: on the rock (floor 1) his sight is held at 0, so he never
  throws. He wakes when any of the crew comes within nine tiles (any
  floor). Awake on the rock, his orders are replaced every tick by one
  straight step toward the nearest of the crew: the hunt AI's path search
  would walk him round to the stairs, because it knows the sky is not a
  floor. Every foe he can want is south or west of the rock, so the
  straight walk takes him off the deck and he falls one floor (the first
  floor of a fall is free). Landed under open sky he is the Fallen One:
  90 hp, 24 damage, step 4, sight 7. Down the stairs instead he is the
  Stomper: 180 hp, 40 damage, step 6. The engine resets a walker's step to
  its family's every act, so the slow pace is a speed bonus the script
  re-arms every tick. The name carries the state.
- Stand-down: the shared loop posts every hostile hunter every 50 ticks,
  except the Big One while he is on the rock.
- Narration at ticks 80, 260, 301 (first gust), 801 (falling stars), 1401
  (Black Dash's band) and 1500 (the key); the last bad guy's death lands a
  rainbow arc of flashes over the castle.
- The key (key 3, under the rock's mast on floor 1) and the big door are
  native: one walker holding key 3 opens the door for everyone.

Engine rule for moat/water-ring layouts (learned in playtest): place NO
roaming (ACT_RANDOM) foes whose straight line to the crew crosses
water — the hunt AI beelines with no pathfinding and jitters in place
against the moat edge ("stuck spinning" to the player). Every placed
foe on such a map is a POSTED guard (ambush wake); roamers are legal
only where their chase ground is open (generator spawns on the crew's
side of the water are fine). Corollary learned the hard way: a posted
ENEMY must never carry the builders' hold-post flag. og::mapgen's
place_living stamps hold-post on every team<=1 guard (the allied-escort
rule), and a held post NEVER wakes — walker::act_guard skips the
ACT_RANDOM conversion and its undirected fire command dies on the
facing gate, so the whole garrison sat in the open twitching at an
adjacent crew, forever. The generator clears the flag on every hostile
guard (the builders document the per-caller override) and self-checks
that no hostile guard holds post; the level script additionally caps
hostile sight at seven tiles (the isle's sea-mist) so the wake keeps
the staged beats instead of an island-wide swarm.

Two more engine rules from the epic rework:
- NO standable ground across open water. Mages and skeletons
  self-teleport when pressed; a walkable reef islet let the Sea Wizard
  himself strand the kill-all across the sea (observed in an unattended
  sim). Off-island decoration must be tree atolls or plain water, and
  the boss carries specials_disabled so his escape trick cannot
  softlock the throne fight.
- Gates 3 tiles wide on every wall ring. The hunt AI funnels by
  wall-sliding; two-wide mouths on a double-walled castle starved the
  funnel and unattended runs stalled short of the 6000-tick budget.

## The dream log (the campaign's camp and book)

`packs/imaginations.dreams/scripts/dream_log.lua` registers the campaign's
Base Camp composition and its scripted picker (issues #206, #207,
docs/basecamp-zones-design.md, docs/campaign-scripting-design.md). The camp
is the log: one line of its own voice ("Every dream can be dreamed
again."), then a docket of `kind = "level"` rows, one per scen, then the
plain company roster — so a dream already had can be picked again from the
screen the player lives on, which is what the kids asked for.

The book page (THE DREAM LOG) is the log's spare face, not a second room a
player visits: a book opens from a camp page row, this camp composes none,
so nothing opens it while the camp composes. It exists for the case where
the camp does not — a malformed or over-budget composition makes the engine
build a door onto the book's root page instead — and it lists the same rows
from the same builder, so the spare cannot drift from the face in use.

The rows are found rather than listed: `og.campaign_scenario_title(id)`
answers "" for an id the campaign does not ship, so a new scen shows up the
day its level lands. Labels are left to the engine (it fills them from the
scen header), and the note re-derives from the save on every fetch —
`dreamed` for a cleared dream, `tonight?` for the campaign cursor, `not
yet` otherwise. Nothing here spends gold, keeps campaign state, locks a
hero or swears an oath: every roster capability stays on.

The two faces scan different distances because they hold different amounts.
A book page holds 24 entries and clips past them; the camp zone holds 16
action rows in total and **refuses** a composition that overruns them. Each
face scans as far as it can hold, so a camp that is ever refused falls back
to a book door listing further than the camp could.

Sixteen dreams is where the camp stops growing, and that is a decision
deferred, not solved: at dream 17 the camp keeps composing but stops
listing the newest dream, so it stops showing `tonight?` the moment the
cursor moves there. Somebody has to choose then — page the docket, or let
the camp carry the newest 16 and leave the rest to the book. The unit test
asserts the two faces list the same dreams, so it goes red at dream 17 and
the choice cannot be shipped past.

`tests/unit/test_imaginations_dream_log.cpp` drives both faces over the
shipped archive and pins the ledger above: when a new scen lands, that test
goes red until its id joins both the table and the test's own list.

## Difficulty curve (campaign_meta)

Fresh teams are the audience: every level must be clearable by a new
save's level-1 crew (crew power 1 in the longseason README's terms).

| level | crew power entering | calibration floor (8-mixed, tick 600, min over seeds 42/1337/2025) |
|-------|---------------------|--------------------------------------------------------------------|
| 1 | 1 | 7 (measured 8/7/8 on 2026-08-11; pinned in tests/unit/test_imaginations_calibration.cpp) |
| 2 | 1 | 8 (measured 8/8/8 on 2026-10-26; only the four tick-0 faeries are awake by tick 600) |
| 3 | 1 | 6 (measured 8/8/6 on 2026-10-26; the first gust wakes at 300) |

Gate (kill-all): 8-mixed at crew 1 reaches `level_done == 1` within 6000
ticks on 3/3 seeds. Measured with `scripts/imaginations_playtest.sh` on
2026-08-11 (post-wake-fix geometry: 64x64, bailey + keep, 19 posted
foes + 3 generators + 2 timed waves, sea-mist sight cap, level-2 Sea
Wizard): PASS — the 8-mixed gate roster cleared all foes on every seed
between ticks 870 and 996 with 7-8 of 8 alive. The isle stays a gentle
opener despite its size: the drama lives in the staged assault —
causeway sentries, the bailey, the keep, the throne, and the delayed-spawn
reliefs at ticks 500/800 — not in attrition. (Design history: a
mid-court garrison variant wiped the 4-soldier stand-in — staggered
posts fixed it; walkable reef islets let teleporting foes strand the
kill-all — tree atolls fixed it; two-wide gates stalled unattended
runs — three-wide gates fixed it; and when the hold-post wake bug fell,
the statue-era balance had to fall with it: 25 foes, six wave units and
a level-4 boss all beat a fresh crew that now had to FIGHT for the
castle — trimmed to 19/3/level-2, measured back to a fresh-team win.)

The Castle of Gold, measured with `scripts/imaginations_playtest.sh
--levels 2` on 2026-10-26 under the shipped defaults: PASS. The 8-mixed
gate roster reached `level_done == 1` on 3/3 seeds at crew 1 (ticks
2522/2810/2987, 7 of 8 alive) and on 3/3 at crew 2 (2573/2832/2937, 6-8
alive). The war runs as long as its waves: the Fae Queen's wave wakes at
2200, so no run can clear before then. The 4-soldier stand-in (not the
gate) cleared 1/3 seeds at crew 1 and 1/3 at crew 2: soldiers are melee
only against a flying, freezing enemy, and the stand-in AI only rarely takes
the castle gold that wakes the ghosts.

The Asteroid Castle, measured with `scripts/imaginations_playtest.sh
--levels 3` on 2026-10-26 under the shipped defaults: PASS. The 8-mixed
gate roster reached `level_done == 1` on 3/3 seeds at crew 1 (ticks
1785/1894/2021, 5-8 of 8 alive) and on 3/3 at crew 2 (1614/1630/1679, 7-8
alive). The Big One fell off the rock on every run; Black Dash's band
(wakes at 1400) sets the floor on the clear time. The 4-soldier stand-in
(not the gate) cleared 3/3 seeds at crew 1 (1-4 alive) and 3/3 at crew 2
(3-4 alive).
