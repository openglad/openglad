# org.openglad.modes — provenance

GENERATED CAMPAIGN. `scen/`, `pix/`, `campaign.yaml` and `icon.png` are
regenerated wholesale by `tools/modes_mapgen`
(`scripts/generate_modes_campaign.sh`) — do not hand-edit them; port
changes into the generator. Every generated scen carries the
`SCEN_TYPE_GENERATED` bit in its `.fss` header (the level editor warns on
open), and the CI `campaign-drift` job reruns the generator and fails on
any diff.

`packs/` is hand-authored — edit freely. The generator reads it (staging,
manifest refresh) but never rewrites it, with one exception:
`packs/org.openglad.modes.core/lib/mode_levels.lua` is itself generated
from the level tables and refreshed by the generator.

This file is repo documentation: `scripts/make_glad.py` excludes README.md
from the composed `.glad` archive.
