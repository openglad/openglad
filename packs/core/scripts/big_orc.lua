-- core:orc_captain — level_up only (cookbook: docs/lua-classpacks-design.md §3).
-- (Descriptor .name "ORC CAPTAIN" → family id core:orc_captain.)

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

og.register_hooks("living", "core:orc_captain", {
  level_up = level_up,
})
