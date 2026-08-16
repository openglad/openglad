-- westlands.fire lib: spawns — the fixed spawn tiles every decision consequence lands on (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Pure data for scripts/consequences.lua (og.use("spawns")). Every tile is
-- fixed pack data — no og.rand, no runtime probing — chosen against the
-- shipped grids and validated by test_westlands_fire.cpp (footing at the
-- spawn footprint) and by westlands_mapgen's self_check_fire_script.
-- Coordinates are TILES (og.C.GRID_SIZE pixels each); livings stand 2x2
-- tiles, treasures 1x1.
local M = {}

-- Level 15, The Deeping Wall: the Watch's wages come home as swords.
-- Inside the wall, flanking the processional way at the gate's back —
-- clear of the courtyard start markers, the keep, and the treehouse
-- muster at (35,9). { floor, tile_x, tile_y, level, name }.
M.WATCH = {
  { floor = 0, x = 39, y = 9, level = 6, name = "Wall-Warden" },
  { floor = 0, x = 34, y = 11, level = 5, name = "Watchman" },
  { floor = 0, x = 43, y = 11, level = 5, name = "Watchman" },
}

-- Level 11, The Great River: the counted hoard's debt keeps pace on the
-- water — two ghosts over the river off the far (east) bank, clear of the
-- fords, the isles and the treasure shoals. Ghosts fly; the risen Riders
-- set the placed-on-water precedent.
M.WRAITHS = {
  { floor = 0, x = 40, y = 20 },
  { floor = 0, x = 40, y = 28 },
}
M.WRAITH_LEVEL = 6
M.WRAITH_NAME = "Gold-Wraith"

-- The pack-train's drop tiles, one cluster per hard-road level, beside
-- that level's lead start marker: slots 1-3 take one drumstick per
-- provision tier, slot 4 the tier-2 magic potion.
M.PROVISIONS = {
  [13] = { floor = 0, tiles = { { 76, 23 }, { 77, 23 }, { 76, 26 }, { 77, 26 } } },
  [14] = { floor = 0, tiles = { { 29, 44 }, { 30, 44 }, { 29, 45 }, { 30, 45 } } },
  [15] = { floor = 1, tiles = { { 36, 20 }, { 37, 20 }, { 36, 21 }, { 37, 21 } } },
  [16] = { floor = 0, tiles = { { 46, 24 }, { 47, 24 }, { 46, 25 }, { 47, 25 } } },
  [17] = { floor = 0, tiles = { { 43, 22 }, { 43, 23 }, { 43, 26 }, { 43, 27 } } },
  [19] = { floor = 0, tiles = { { 12, 7 }, { 13, 7 }, { 12, 8 }, { 13, 8 } } },
  [20] = { floor = 0, tiles = { { 16, 24 }, { 17, 24 }, { 16, 25 }, { 17, 25 } } },
  [21] = { floor = 0, tiles = { { 11, 20 }, { 12, 20 }, { 11, 21 }, { 12, 21 } } },
  [22] = { floor = 0, tiles = { { 27, 44 }, { 28, 44 }, { 31, 44 }, { 32, 44 } } },
  [23] = { floor = 0, tiles = { { 4, 23 }, { 5, 23 }, { 4, 24 }, { 5, 24 } } },
}

-- Level 26, The Grey Ships: the quay tile where The Pilgrim waits, on the
-- pier pavement between the gangplank torches and the strand.
M.PILGRIM = { floor = 0, x = 10, y = 17 }
M.PILGRIM_LEVEL = 10
M.PILGRIM_NAME = "The Pilgrim"

return M
