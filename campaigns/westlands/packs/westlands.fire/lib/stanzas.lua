-- westlands.fire lib: stanzas — the fire's narration, march labels and ledger prose tables (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Pure data for scripts/fire.lua (og.use("stanzas")). Every line is <= 38
-- glyphs (the camp's text budget), every march label <= 24 and every note
-- <= 20; test_westlands_fire.cpp sweeps the budgets over every camp state.
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

-- What the road has behind it, newest first (first completed wins). The
-- milestone line belongs to the OPEN ROAD, so there is no Falls entry: past
-- the Falls the camp speaks in front lines until both roads are walked, and
-- "none turn back" is the swearing window's own promise.
M.MILESTONE = {
  { level = 24, line = "The mountain is behind you." },
  { level = 6, line = "The mountain gate shut behind you." },
  { level = 3, line = "The ford is held and crossed." },
}

-- The full-circle line (level 26 completed: 26 loops to 1).
M.LOOP_LINE = "The vale again. The fire remembers."

-- The Falls: the swearing window, where the company divides.
M.FALLS = {
  divide = "The Falls. The company must divide.",
  swear = "Swear WAR or BURDEN. None turn back.",
  unsworn = "Unsworn blades wait at the Falls.",
  thin_war = "One sword alone rides to war.",
  thin_burden = "One sword alone guards the Bearer.",
}

-- The two fronts: the march row of each road level, and where each column
-- stands tonight. WAR_LABEL/BURDEN_LABEL are compressed to the 24-glyph row
-- budget and deliberately differ from the road graph's own place names.
M.FRONT = {
  WAR_LABEL = {
    [13] = "RIDE WEST: THE PLAINS",
    [14] = "WEST: THE WIZARD'S VALE",
    [15] = "WEST: THE DEEPING WALL",
    [16] = "WEST: THE WHITE CITY",
    [17] = "WEST: THE BLACK GATE",
  },
  BURDEN_LABEL = {
    [19] = "GO EAST: THE MARSHES",
    [20] = "EAST: THE CROSSROADS",
    [21] = "EAST: THE SPIDER PASS",
    [22] = "EAST: THE MOON TOWER",
    [23] = "EAST: THE ASH PLAINS",
  },
  WAR_AT = {
    [13] = "War rides out onto the Plains.",
    [14] = "War camps at the Wizard's Vale.",
    [15] = "War holds the Deeping Wall.",
    [16] = "War musters in the White City.",
    [17] = "War stands at the Black Gate.",
  },
  BURDEN_AT = {
    [19] = "The Bearer walks into the marsh.",
    [20] = "The Bearer rests at the Crossroads.",
    [21] = "The Bearer nears the Spider Pass.",
    [22] = "The Bearer climbs to the Moon Tower.",
    [23] = "The Bearer crosses the Ash Plains.",
  },
  WAR_DONE = "The war is fought. The west waits.",
  BURDEN_DONE = "The burden is borne. The east waits.",
}

-- The lines the fire speaks when the split is not the tidy case.
M.WARN = {
  no_oath = "No road is sworn. The fire waits.",
  ashes_west = "The west fire is ashes. All ride.",
  ashes_east = "The east fire is ashes. All ride.",
  not_come = "The Bearer is not yet come.",
  no_march = "No sword is deployed. None march.",
  bearer_stake = "The Bearer must live tomorrow.",
  bearer_walks = "The Bearer walks with you.",
  hoard = "The hoard weighs on the wagons.",
  dark = "The road is dark.",
}

M.REUNION = {
  "The fronts meet under the mountain.",
  "The company is whole again.",
}

-- The oath column's refusals: where a hero actually is, in the fiction's
-- words, plus the reason the column itself stops cycling.
M.LOCK = {
  war = "ON THE WAR ROAD",
  burden = "WITH THE BEARER",
  unsworn = "WAITS AT THE FALLS",
  mountain = "WAITS AT THE MOUNTAIN",
  frozen = "The Falls parted the company.",
}

-- The quartermaster's standing page lines: the moral economy's rule, once,
-- so the offer rows can stay terse.
M.STORES = {
  "He deals in wages, bread, and weight.",
  "What is paid is repaid, in kind.",
  "Packs put bread on the hard roads.",
}

-- The frozen split closes the hiring board, and the shelf says why: a blade
-- bought after the Falls could swear no road and walk none.
M.STORES_NO_HIRE = "No new blades until the roads meet."

-- The provision tiers, spoken (no float ever reaches a sim-visible
-- string; these are the only numerals the fire utters about the packs).
M.TIER_WORDS = { "one", "two", "three" }

return M
