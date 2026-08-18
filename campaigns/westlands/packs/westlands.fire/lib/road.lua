-- westlands.fire lib: road — the pack-local mirror of the shipped forward exits, in the fiction's words (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Pure data for the camp docket (og.use("road")). One row per shipped level:
--   escort  true on the levels the Bearer walks (the shipped SAVE_ALL set)
--   fwd     forward exits: { level, label, note } — every row states its
--           stake in plain words (label <= 24, note <= 20)
-- Only FORWARD edges live here: the camp offers the road ahead and nothing
-- else, so a backtrack exit is never a row. test_westlands_fire.cpp holds the
-- table to the shipped .fss level by level in both directions — every offered
-- road is a real exit, and every shipped exit the camp does not offer is the
-- way back to a camp that offers this one — and mirrors `escort` against the
-- level's SAVE_ALL bit.
local M = {}

M.ROAD = {
  [1] = {
    escort = false,
    fwd = { { level = 2, label = "THE FOREST ROAD", note = "the road out" } },
  },
  [2] = {
    escort = true,
    fwd = {
      { level = 3, label = "THE LAST FORD", note = "hold the crossing" },
    },
  },
  [3] = {
    escort = true,
    fwd = {
      { level = 4, label = "THE HIDDEN REFUGE", note = "cover, and a plan" },
    },
  },
  [4] = {
    escort = true,
    fwd = {
      { level = 5, label = "THE HIGH PASS", note = "south, over snow" },
      { level = 7, label = "THE FROZEN WALL", note = "a plea, a pay chest" },
    },
  },
  [5] = {
    escort = true,
    fwd = {
      { level = 6, label = "UNDER THE MOUNTAIN", note = "the mountain gate" },
    },
  },
  [6] = {
    escort = true,
    fwd = {
      { level = 8, label = "THE BRIDGE", note = "the straight road" },
      { level = 9, label = "THE LOST DELVE", note = "gold, and a price" },
    },
  },
  [7] = {
    escort = false,
    fwd = { { level = 5, label = "THE HIGH PASS", note = "south, over snow" } },
  },
  [8] = {
    escort = true,
    fwd = {
      { level = 10, label = "THE GOLDEN WOOD", note = "quiet, and watched" },
    },
  },
  [9] = {
    escort = true,
    fwd = { { level = 8, label = "THE BRIDGE", note = "back to the road" } },
  },
  [10] = {
    escort = true,
    fwd = {
      { level = 11, label = "THE GREAT RIVER", note = "the crossing east" },
    },
  },
  [11] = {
    escort = true,
    fwd = { { level = 12, label = "THE FALLS", note = "where roads part" } },
  },
  [12] = {
    escort = true,
    fwd = {
      { level = 13, label = "THE WAR ROAD", note = "the feint, west" },
      { level = 19, label = "THE BURDEN'S ROAD", note = "the marsh road" },
    },
  },
  [13] = {
    escort = false,
    fwd = {
      { level = 14, label = "THE WIZARD'S VALE", note = "war road, north" },
    },
  },
  [14] = {
    escort = false,
    fwd = {
      { level = 15, label = "THE DEEPING WALL", note = "hold the wall" },
    },
  },
  [15] = {
    escort = false,
    fwd = {
      { level = 16, label = "THE WHITE CITY", note = "the city musters" },
    },
  },
  [16] = {
    escort = false,
    fwd = { { level = 17, label = "THE BLACK GATE", note = "the last gate" } },
  },
  [17] = {
    escort = false,
    fwd = {
      { level = 24, label = "THE MOUNTAIN OF FIRE", note = "the summit" },
    },
  },
  [19] = {
    escort = true,
    fwd = {
      { level = 20, label = "THE CROSSROADS", note = "burden road, east" },
    },
  },
  [20] = {
    escort = true,
    fwd = {
      { level = 21, label = "THE PASS OF THE SPIDER", note = "the high path" },
    },
  },
  [21] = {
    escort = true,
    fwd = {
      { level = 22, label = "THE TOWER OF THE MOON", note = "the tower road" },
    },
  },
  [22] = {
    escort = true,
    fwd = {
      { level = 23, label = "THE ASH PLAINS", note = "ash, and no cover" },
    },
  },
  [23] = {
    escort = true,
    fwd = {
      { level = 24, label = "THE MOUNTAIN OF FIRE", note = "the summit" },
    },
  },
  [24] = {
    escort = true,
    fwd = {
      { level = 25, label = "THE SCOURING", note = "home, and war" },
      { level = 26, label = "THE GREY SHIPS", note = "the grey sea" },
    },
  },
  [25] = {
    escort = false,
    fwd = { { level = 26, label = "THE GREY SHIPS", note = "the grey sea" } },
  },
  [26] = {
    escort = false,
    fwd = { { level = 1, label = "THE VALE AGAIN", note = "the road begins" } },
  },
}

return M
