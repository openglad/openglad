-- Scenario-troops strip, shared by all six modes: the one rule set for the per-team MAP UNITS box. The implementation moved to the shared core pack (docs/lineup-design.md C1 — the classic lineup stage applies the identical strip), so this module is the modes' name for it: a re-export, kept so every impl and probe binds the same surface it always did.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local lineup = og.use("core:lineup")

return {
  is_troop = lineup.is_troop,
  retire = lineup.retire,
  strip_authored_troops = lineup.strip_authored_troops,
}
