# org.openglad.longseason — provenance

GENERATED CAMPAIGN. `scen/`, `pix/`, `campaign.yaml` and `icon.png` are
regenerated wholesale by `tools/longseason_mapgen`
(`scripts/generate_longseason_campaign.sh`) — do not hand-edit them; port
changes into the generator. Every generated scen carries the
`SCEN_TYPE_GENERATED` bit in its `.fss` header (the level editor warns on
open), and the CI `campaign-drift` job reruns the generator and fails on
any diff.

This file is repo documentation: `scripts/make_glad.py` excludes README.md
from the composed `.glad` archive.
