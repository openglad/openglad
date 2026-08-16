-- westlands.fire lib: road — the pack-local mirror of the shipped 47-exit graph, in the fiction's words (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Pure data for THE ROAD page (og.use("road")). One row per shipped level:
--   here  the place name spoken at the fire (<= 24 glyphs)
--   fwd   forward exits: { level, label, note? } — notes only at the four
--         authored forks, as the briefings named them (label <= 24,
--         note <= 20)
--   back  backtrack exits: { level, label }
-- The table mirrors tools/westlands_mapgen's authored exit graph edge for
-- edge and never invents one; test_westlands_fire.cpp asserts the mirror
-- against the shipped .fss exits for every level.
local M = {}

M.ROAD = {
  [1] = {
    here = "THE QUIET VALE",
    fwd = { { level = 2, label = "THE FOREST ROAD" } },
    back = {},
  },
  [2] = {
    here = "THE FOREST ROAD",
    fwd = { { level = 3, label = "THE LAST FORD" } },
    back = { { level = 1, label = "BACK TO THE VALE" } },
  },
  [3] = {
    here = "THE LAST FORD",
    fwd = { { level = 4, label = "THE HIDDEN REFUGE" } },
    back = { { level = 2, label = "BACK TO THE FOREST ROAD" } },
  },
  [4] = {
    here = "THE HIDDEN REFUGE",
    fwd = {
      { level = 5, label = "THE HIGH PASS", note = "south, over snow" },
      { level = 7, label = "THE FROZEN WALL", note = "the north plea" },
    },
    back = { { level = 3, label = "BACK TO THE FORD" } },
  },
  [5] = {
    here = "THE HIGH PASS",
    fwd = { { level = 6, label = "UNDER THE MOUNTAIN" } },
    back = { { level = 4, label = "BACK TO THE REFUGE" } },
  },
  [6] = {
    here = "UNDER THE MOUNTAIN",
    fwd = {
      { level = 8, label = "THE BRIDGE" },
      { level = 9, label = "THE LOST DELVE", note = "gold uncounted" },
    },
    back = {},
  },
  [7] = {
    here = "THE FROZEN WALL",
    fwd = { { level = 5, label = "THE HIGH PASS" } },
    back = {},
  },
  [8] = {
    here = "THE BRIDGE OF SHADOW",
    fwd = { { level = 10, label = "THE GOLDEN WOOD" } },
    back = {},
  },
  [9] = {
    here = "THE LOST DELVE",
    fwd = { { level = 8, label = "THE BRIDGE" } },
    back = { { level = 6, label = "BACK TO THE HOARD" } },
  },
  [10] = {
    here = "THE GOLDEN WOOD",
    fwd = { { level = 11, label = "THE GREAT RIVER" } },
    back = { { level = 8, label = "BACK TO THE BRIDGE" } },
  },
  [11] = {
    here = "THE GREAT RIVER",
    fwd = { { level = 12, label = "THE FALLS" } },
    back = { { level = 10, label = "BACK TO THE WOOD" } },
  },
  [12] = {
    here = "THE FALLS",
    fwd = {
      { level = 13, label = "THE WAR ROAD", note = "the horns of war" },
      { level = 19, label = "THE BURDEN'S ROAD", note = "the marsh road" },
    },
    back = {},
  },
  [13] = {
    here = "THE PLAINS",
    fwd = { { level = 14, label = "THE WIZARD'S VALE" } },
    back = { { level = 12, label = "BACK TO THE FALLS" } },
  },
  [14] = {
    here = "THE WIZARD'S VALE",
    fwd = { { level = 15, label = "THE DEEPING WALL" } },
    back = { { level = 13, label = "BACK TO THE PLAINS" } },
  },
  [15] = {
    here = "THE DEEPING WALL",
    fwd = { { level = 16, label = "THE WHITE CITY" } },
    back = { { level = 14, label = "BACK TO THE VALE" } },
  },
  [16] = {
    here = "THE WHITE CITY",
    fwd = { { level = 17, label = "THE BLACK GATE" } },
    back = { { level = 15, label = "BACK TO THE WALL" } },
  },
  [17] = {
    here = "THE BLACK GATE",
    fwd = { { level = 24, label = "THE MOUNTAIN OF FIRE" } },
    back = { { level = 16, label = "BACK TO THE CITY" } },
  },
  [19] = {
    here = "THE DEAD MARSHES",
    fwd = { { level = 20, label = "THE CROSSROADS" } },
    back = { { level = 12, label = "BACK TO THE FALLS" } },
  },
  [20] = {
    here = "THE CROSSROADS",
    fwd = { { level = 21, label = "THE PASS OF THE SPIDER" } },
    back = { { level = 19, label = "BACK TO THE MARSHES" } },
  },
  [21] = {
    here = "THE PASS OF THE SPIDER",
    fwd = { { level = 22, label = "THE TOWER OF THE MOON" } },
    back = { { level = 20, label = "BACK TO THE CROSSROADS" } },
  },
  [22] = {
    here = "THE TOWER OF THE MOON",
    fwd = { { level = 23, label = "THE ASH PLAINS" } },
    back = { { level = 21, label = "BACK TO THE PASS" } },
  },
  [23] = {
    here = "THE ASH PLAINS",
    fwd = { { level = 24, label = "THE MOUNTAIN OF FIRE" } },
    back = { { level = 22, label = "BACK TO THE TOWER" } },
  },
  [24] = {
    here = "THE MOUNTAIN OF FIRE",
    fwd = {
      { level = 25, label = "THE SCOURING", note = "home, and war" },
      { level = 26, label = "THE GREY SHIPS", note = "the grey sea" },
    },
    back = {},
  },
  [25] = {
    here = "THE SCOURING",
    fwd = { { level = 26, label = "THE GREY SHIPS" } },
    back = { { level = 24, label = "BACK TO THE SUMMIT" } },
  },
  [26] = {
    here = "THE GREY SHIPS",
    fwd = { { level = 1, label = "THE VALE AGAIN" } },
    back = {},
  },
}

return M
