-- core:faerie — level_up only (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 12, 4, 8, 1)
end

og.register_hooks("living", "core:faerie", {
  level_up = level_up,
})
