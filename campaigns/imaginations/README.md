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
| 1 | The Raspberry Isle | "We start at the edges of the island, at every corner. The sea is all around us. The enemies start in the middle, in a castle, near the water. We must run at them right away." |

Growth rule: new ideas append new scens (2, 3, …). The newest level's
walk-out exit loops home to 1; when its successor lands, the generator
retargets that exit to the new scen. `first_level` stays 1.

Engine rule for moat/water-ring layouts (learned in playtest): place NO
roaming (ACT_RANDOM) foes whose straight line to the crew crosses
water — the hunt AI beelines with no pathfinding and jitters in place
against the moat edge ("stuck spinning" to the player). Every placed
foe on such a map is a POSTED guard (ambush wake); roamers are legal
only where their chase ground is open (generator spawns on the crew's
side of the water are fine).

## Difficulty curve (campaign_meta)

Fresh teams are the audience: every level must be clearable by a new
save's level-1 crew (crew power 1 in the longseason README's terms).

| level | crew power entering | calibration floor (8-mixed, tick 600, min over seeds 42/1337/2025) |
|-------|---------------------|--------------------------------------------------------------------|
| 1 | 1 | 8 (measured 8/8/8 on 2026-08-10; pinned in tests/unit/test_imaginations_calibration.cpp) |

Gate (kill-all): 8-mixed at crew 1 reaches `level_done == 1` within 6000
ticks on >= 2/3 seeds, and 3/3 at crew 2. Measured with
`scripts/imaginations_playtest.sh` on 2026-08-10 (final posted-garrison
geometry with the shore rim and field copses): PASS — all 12 bracket
runs (both rosters x crew {1, 2} x 3 seeds) cleared between ticks 646
and 2140 with zero stand-in crew deaths on every run. The isle is
intentionally a gentle opener; the drama lives in the layout, not the
attrition. (An earlier mid-court garrison variant wiped the 4-soldier
stand-in on one seed — the dais-bodyguard split into a gate fight then
a throne fight is what restored the fresh-team margin.)
