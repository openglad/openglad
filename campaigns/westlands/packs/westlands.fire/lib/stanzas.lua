-- westlands.fire lib: stanzas — the fire's narration and ledger prose tables (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Pure data for scripts/fire.lua (og.use("stanzas")). Every line is <= 38
-- glyphs (the mission-book line budget the generator self-check enforces).
local M = {}

-- The camp line, by where the road stands (the campaign cursor). Ranges
-- are scanned in order, first match wins; 18 is the campaign's own gap.
M.CAMP = {
  { min = 1, max = 3, line = "Cold camp. No fire past the ford." },
  { min = 4, max = 7, line = "The Bearer sleeps. The watch is set." },
  { min = 8, max = 12, line = "The fire is low. The road is long." },
  { min = 13, max = 17, line = "War-fires answer from the hills." },
  { min = 19, max = 23, line = "A guarded flame in the wet dark." },
  { min = 24, max = 26, line = "The last fire before the summit." },
}
M.CAMP_FALLBACK = "The fire is banked. The road waits."

-- The full-circle line and title (level 26 completed: 26 loops to 1).
M.LOOP_LINE = "The vale again. The fire remembers."
M.LOOP_TITLE = "THE FIRE REMEMBERS"

-- The provision tiers, spoken (no float ever reaches a sim-visible
-- string; these are the only numerals the fire utters about the packs).
M.TIER_WORDS = { "one", "two", "three" }

return M
