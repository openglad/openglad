# concept — provenance

GENERATED CAMPAIGN. `scen/`, `pix/`, `campaign.yaml` and `icon.png` are
regenerated wholesale by `tools/concept_mapgen`
(`scripts/generate_concept_campaign.sh`) — do not hand-edit them; port
changes into the generator. Every generated scen carries the
`SCEN_TYPE_GENERATED` bit in its `.fss` header (the level editor warns on
open), and the CI `campaign-drift` job reruns the generator and fails on
any diff.

`packs/` is hand-authored — edit freely. The generator reads it (staged
for the self-checks) but never rewrites it.

Dev-only: composed into `build/<preset>/builtin-dev/concept.glad` for tests,
tooling and screenshots; never installed, packaged or preloaded into the web
build (#240).

This file is repo documentation: `scripts/make_glad.py` excludes README.md
from the composed `.glad` archive.
