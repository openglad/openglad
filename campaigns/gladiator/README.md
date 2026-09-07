# gladiator — provenance

HAND-AUTHORED CAMPAIGN. This tree is the canonical, editable source of the
classic Gladiator campaign — edit it directly (openscen or otherwise); no
generator owns it, its scens carry no `SCEN_TYPE_GENERATED` bit, and the
CI `campaign-drift` job never touches it.

This file is repo documentation: `scripts/make_glad.py` excludes README.md
from the composed `.glad` archive.

## Edits from the 2002 original

The scenario files are the 2002 import unless listed here.

- **scen17 "THE CITY OF NUTHRAM", object record #77** (issue #266). The
  team-3 thief authored at world (1168,1152) shipped at level 8 with an
  empty name field, so `BIT_NAMED` never got set and the level's boss drew
  like a guild mook. He is now named `Saffron`; only the 12-byte name
  field of that one record changed, every other byte of the file is the
  original, and his authored **level 8 stands**. (The bug-sweep branch
  briefly shipped him at level 6: under the 2013 damage clamp a level-8
  thief's 128 armor turned every hit into exactly 1 point, and the level
  byte looked like the only fix. It was not — the engine's damage
  reduction now returns the 2002 roll's expectation, ~8 per 45-point blow
  on him, so the nerf was reverted. See `docs/GAMEPLAY_FIXES_FROM_CLASSIC.md`.)
