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
  empty name field. `living::set_difficulty` scales armor quadratically
  (`armor += 2*level^2`) while damage reduction is a linear `armor/2`
  floored at "always do at least 1 damage", so at 128 armor he took
  exactly one point from every hit a mid-campaign party could land — the
  reporter measured him as tougher than the level's other 78 livings
  combined. He is now level 6 (armor 72, still the toughest single unit
  in the level and level with Lord Jakarta's own master of assassins) and
  named `Saffron`, which sets `BIT_NAMED` so the level's boss draws with
  the named outline instead of looking like a guild mook. Only the level
  short and the 12-byte name field of that one record changed; every
  other byte of the file is the original.
