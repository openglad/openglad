-- longseason.ledger lib: ledger_data — the book's pure data and bit math: the exit-graph mirror, per-level coin flavor, and the fixed vetted spawn tiles (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Every table here mirrors the SHIPPED package (campaigns/longseason/scen)
-- and is pinned against it by tests/unit/test_longseason_ledger.cpp — a
-- future mapgen regeneration that moves layouts turns into a red test, not
-- a broken spawn. Regeneration checklist: campaigns/longseason/README.md.

local M = {}

M.LEVEL_COUNT = 19
M.FIRST_LEVEL = 1

-- The consequence sites (level ids).
M.FAIR_LEVEL = 9
M.TOLL_LEVEL = 14
M.MINT_LEVEL = 18
M.SETTLEMENT_LEVEL = 19

-- Coin-bearing pay: one warm coin per completed level 2..18 (level 1 pays
-- in grain, level 19 is the settlement itself).
M.FIRST_COIN_LEVEL = 2
M.LAST_COIN_LEVEL = 18

-- The Carried: one ally per four kept coins, capped.
M.CARRIED_PER_KEPT = 4
M.CARRIED_CAP = 3

-- BIT[L] = 2^L, the coin mask bit for level L (int32-safe: 2^19 = 524288).
M.BIT = {
  2, 4, 8, 16, 32, 64, 128, 256, 512, 1024,
  2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288,
}

-- Exit-graph mirror of the shipped exit walkers (destination sets, sorted
-- ascending; the shipped order differs but the SET is what the docket
-- derives from).
M.EXITS = {
  { 2 },
  { 1, 3, 4 },
  { 2, 4 },
  { 3, 5 },
  { 4, 6 },
  { 5, 7, 8 },
  { 6, 8 },
  { 6, 9 },
  { 8, 10 },
  { 9, 11 },
  { 10, 12, 13 },
  { 11, 13 },
  { 11, 14 },
  { 13, 15 },
  { 14, 16 },
  { 15, 17 },
  { 16, 18 },
  { 17, 19 },
  { 1 },
}

-- The kind of work each row is, in docket voice.
M.TYPE_NOTE = {
  "kill work", "hold work", "kill work", "kill work", "kill work",
  "kill work", "hold work", "kill work", "hold work", "kill work",
  "kill work", "kill work", "walk out", "hold work", "kill work",
  "walk out", "kill work", "walk out", "kill work",
}

-- The stake a protected-escort job puts on the row BEFORE the player goes:
-- exactly the two levels the package gives an npc_flags bit-2 walker (the
-- Assessor on 4, the Reeve on 15). A lose condition the docket does not
-- state is a lose condition the player meets by losing.
M.PROTECTED_NOTE = {}
M.PROTECTED_NOTE[4] = "he must not fall"
M.PROTECTED_NOTE[15] = "the Reeve must live"

-- The three optional contracts.
local OPTIONAL = { 3, 7, 12 }

function M.is_optional(lvl)
  for i = 1, #OPTIONAL do
    if OPTIONAL[i] == lvl then
      return true
    end
  end
  return false
end

-- Per-level warm-coin flavor for the ritual page (levels 2..18; 1 and 19
-- hold placeholders and are never shown — level 1 pays in grain and the
-- settlement nails its coin over the door).
M.COIN_LINE = {
  "Pay was grain and a dry roof.",
  "One coin in the ferry pay came warm.",
  "The bell silver was cold. One was not.",
  "He paid on the spot, in the warm coin.",
  "Both purses paid. Both came up warm.",
  "The hay war paid in warm coin again.",
  "A cut of the tolls, in the same coin.",
  "The paymaster's chest came back warm.",
  "The fair's take sat warm in the box.",
  "The advance was warm coin. Again.",
  "Ore from the seams, warm as the coin.",
  "The grave-coin is warm as our pay.",
  "The smelter wanted his cut of it.",
  "Toll coin, warm to the last penny.",
  "The Reeve kept a sample. For evidence.",
  "The pay chest melted its own footing.",
  "Every camp paid. All of it warm.",
  "Struck from the city's own bones.",
  "One coin, nailed over the door.",
}

-- Counts spelled small, as the bible writes them. The table runs to
-- nineteen because the settlement counts JOBS (all nineteen levels), not
-- just the seventeen coin-bearing ones.
local COUNT_WORD = {
  "none", "one", "two", "three", "four", "five", "six", "seven", "eight",
  "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
  "sixteen", "seventeen", "eighteen", "nineteen",
}

function M.count_word(n)
  if n < 0 then
    return COUNT_WORD[1]
  end
  if n >= #COUNT_WORD then
    return COUNT_WORD[#COUNT_WORD]
  end
  return COUNT_WORD[n + 1]
end

-- Coin-mask bit test: bit L of mask, via og.div/og.mod only.
function M.has_bit(mask, lvl)
  return og.mod(og.div(mask, M.BIT[lvl]), 2) == 1
end

-- Kept-or-spent coin count over the coin levels.
function M.popcount_coins(mask)
  local count = 0
  for lvl = M.FIRST_COIN_LEVEL, M.LAST_COIN_LEVEL do
    if M.has_bit(mask, lvl) then
      count = count + 1
    end
  end
  return count
end

-- The oldest unresolved coin (lowest completed coin level with neither bit),
-- 0 when the book is square.
function M.oldest_unresolved(kept, spent, completed_mask)
  for lvl = M.FIRST_COIN_LEVEL, M.LAST_COIN_LEVEL do
    if M.has_bit(completed_mask, lvl) then
      local resolved = M.has_bit(kept, lvl)
      if M.has_bit(spent, lvl) then
        resolved = true
      end
      if not resolved then
        return lvl
      end
    end
  end
  return 0
end

-- The docket's accessible set, mirroring get_accessible_levels: the first
-- level, the cursor, every completed level, and the exits of completed
-- levels. Ascending, deduplicated.
function M.accessible_levels(completed_mask, cursor)
  local open = {}
  for lvl = 1, M.LEVEL_COUNT do
    open[lvl] = false
  end
  open[M.FIRST_LEVEL] = true
  if cursor >= 1 and cursor <= M.LEVEL_COUNT then
    open[cursor] = true
  end
  for lvl = 1, M.LEVEL_COUNT do
    if M.has_bit(completed_mask, lvl) then
      open[lvl] = true
      local exits = M.EXITS[lvl]
      for i = 1, #exits do
        open[exits[i]] = true
      end
    end
  end
  local result = {}
  for lvl = 1, M.LEVEL_COUNT do
    if open[lvl] then
      table.insert(result, lvl)
    end
  end
  return result
end

-- ---------------------------------------------------------------------------
-- Fixed spawn tiles, vetted against the shipped package (standable plain
-- ground, no entity overlap at load; probe: test_longseason_ledger.cpp).
-- Tile coordinates; pixels are tile * og.C.GRID_SIZE.
-- ---------------------------------------------------------------------------

-- Crate delivery: up to ten spots ringing each level's lead start marker
-- (1-4 = meal, 1-8 = crate meals, 9-10 = the strong crate's silver).
M.SUPPLY = {
  { floor = 0, tiles = { { 16, 18 }, { 17, 18 }, { 18, 18 }, { 19, 18 }, { 16, 19 }, { 20, 19 }, { 16, 20 }, { 20, 20 }, { 16, 21 }, { 20, 21 } } },
  { floor = 0, tiles = { { 48, 18 }, { 51, 18 }, { 48, 19 }, { 48, 20 }, { 52, 20 }, { 48, 21 }, { 52, 21 }, { 48, 22 }, { 51, 22 }, { 47, 17 } } },
  { floor = 0, tiles = { { 10, 41 }, { 7, 43 }, { 11, 43 }, { 7, 44 }, { 11, 44 }, { 10, 45 }, { 6, 40 }, { 7, 40 }, { 10, 40 }, { 11, 40 } } },
  { floor = 0, tiles = { { 38, 19 }, { 39, 19 }, { 42, 19 }, { 38, 20 }, { 42, 20 }, { 42, 21 }, { 42, 22 }, { 38, 23 }, { 39, 23 }, { 40, 23 } } },
  { floor = 0, tiles = { { 4, 18 }, { 5, 18 }, { 7, 20 }, { 3, 21 }, { 7, 21 }, { 5, 22 }, { 6, 22 }, { 3, 17 }, { 4, 17 }, { 8, 17 } } },
  { floor = 0, tiles = { { 2, 27 }, { 3, 27 }, { 6, 30 }, { 4, 31 }, { 5, 31 }, { 1, 26 }, { 2, 26 }, { 3, 26 }, { 1, 27 }, { 1, 28 } } },
  { floor = 0, tiles = { { 22, 29 }, { 23, 29 }, { 20, 30 }, { 24, 30 }, { 20, 31 }, { 24, 31 }, { 24, 32 }, { 22, 33 }, { 24, 33 }, { 24, 28 } } },
  { floor = 0, tiles = { { 49, 54 }, { 52, 54 }, { 48, 55 }, { 52, 55 }, { 48, 56 }, { 52, 56 }, { 48, 57 }, { 52, 57 }, { 48, 58 }, { 49, 58 } } },
  { floor = 0, tiles = { { 30, 14 }, { 28, 16 }, { 32, 16 }, { 28, 17 }, { 32, 17 }, { 29, 18 }, { 30, 18 }, { 31, 18 }, { 27, 13 }, { 28, 13 } } },
  { floor = 0, tiles = { { 30, 4 }, { 31, 4 }, { 28, 6 }, { 32, 6 }, { 28, 7 }, { 32, 7 }, { 30, 8 }, { 31, 8 }, { 27, 3 }, { 30, 3 } } },
  { floor = 1, tiles = { { 13, 22 }, { 14, 22 }, { 15, 22 }, { 11, 23 }, { 15, 23 }, { 15, 24 }, { 15, 25 }, { 11, 26 }, { 12, 26 }, { 13, 26 } } },
  { floor = 0, tiles = { { 6, 20 }, { 7, 20 }, { 8, 20 }, { 9, 20 }, { 10, 20 }, { 6, 21 }, { 10, 21 }, { 10, 22 }, { 10, 23 }, { 6, 24 } } },
  { floor = 0, tiles = { { 22, 6 }, { 23, 6 }, { 24, 6 }, { 25, 6 }, { 26, 6 }, { 22, 7 }, { 26, 7 }, { 26, 8 }, { 26, 9 }, { 22, 10 } } },
  { floor = 0, tiles = { { 29, 28 }, { 30, 28 }, { 31, 28 }, { 31, 29 }, { 31, 30 }, { 31, 31 }, { 29, 32 }, { 30, 32 }, { 31, 32 }, { 26, 27 } } },
  { floor = 0, tiles = { { 28, 33 }, { 29, 33 }, { 30, 33 }, { 31, 33 }, { 32, 33 }, { 28, 34 }, { 32, 34 }, { 28, 35 }, { 32, 35 }, { 28, 36 } } },
  { floor = 0, tiles = { { 7, 18 }, { 8, 18 }, { 9, 18 }, { 10, 18 }, { 7, 19 }, { 11, 19 }, { 11, 20 }, { 11, 21 }, { 7, 22 }, { 8, 22 } } },
  { floor = 0, tiles = { { 6, 19 }, { 7, 19 }, { 8, 19 }, { 4, 20 }, { 8, 20 }, { 4, 21 }, { 8, 21 }, { 4, 22 }, { 8, 22 }, { 4, 23 } } },
  { floor = 0, tiles = { { 6, 19 }, { 7, 19 }, { 4, 20 }, { 8, 20 }, { 4, 21 }, { 4, 22 }, { 4, 23 }, { 5, 23 }, { 6, 23 }, { 7, 23 } } },
  { floor = 0, tiles = { { 4, 16 }, { 5, 16 }, { 2, 17 }, { 6, 17 }, { 2, 18 }, { 6, 18 }, { 2, 19 }, { 6, 19 }, { 2, 20 }, { 3, 20 } } },
}

-- Ashfall Fair (9): the round returned — two townsfolk flanking the
-- strongroom doorway Kettle holds at (30,14), and two meals inside.
M.FAIR = {
  floor = 0,
  helpers = { { 29, 15, "Stallkeeper" }, { 31, 15, "Potboy" } },
  helper_level = 2,
  food = { { 29, 13 }, { 31, 13 } },
}

-- The Long Toll (14): the Collectors, ON the west toll road (the proven
-- approach lane, paint_path x1..23 y29..30), far from the courtyard hold.
M.COLLECTORS = {
  floor = 0,
  tiles = { { 6, 29 }, { 8, 30 } },
  level = 4,
  team = 2,
  name = "Collector",
}

-- The Warm Mint (18): the Carried stand up in the gatehall west pocket,
-- against the west shell beside Kettle's post at (3,21).
M.CARRIED = {
  floor = 0,
  tiles = { { 2, 19 }, { 2, 21 }, { 2, 23 } },
  level = 4,
  name = "The Carried",
}

-- Settlement Day (19): the coin nailed over the winter-quarters door.
M.DOOR_COIN = { floor = 0, tile = { 37, 13 } }

return M
