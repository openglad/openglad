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

Growth rule: new ideas append new scens (2, 3, …). The newest level's
walk-out exit loops home to 1; when its successor lands, the generator
retargets that exit to the new scen. `first_level` stays 1.

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

Gate (kill-all): 8-mixed at crew 1 reaches `level_done == 1` within 6000
ticks on 3/3 seeds. Measured with `scripts/imaginations_playtest.sh` on
2026-08-11 (post-wake-fix geometry: 64x64, bailey + keep, 19 posted
foes + 3 generators + 2 timed waves, sea-mist sight cap, level-2 Sea
Wizard): PASS — the 8-mixed gate roster cleared all foes on every seed
between ticks 870 and 996 with 7-8 of 8 alive. The isle stays a gentle
opener despite its size: the drama lives in the staged assault —
causeway sentries, the bailey, the keep, the throne, and the NEXT-WAVE
reliefs at ticks 500/800 — not in attrition. (Design history: a
mid-court garrison variant wiped the 4-soldier stand-in — staggered
posts fixed it; walkable reef islets let teleporting foes strand the
kill-all — tree atolls fixed it; two-wide gates stalled unattended
runs — three-wide gates fixed it; and when the hold-post wake bug fell,
the statue-era balance had to fall with it: 25 foes, six wave units and
a level-4 boss all beat a fresh crew that now had to FIGHT for the
castle — trimmed to 19/3/level-2, measured back to a fresh-team win.)
