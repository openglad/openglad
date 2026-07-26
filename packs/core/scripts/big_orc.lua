-- core:orc_captain — behavior hooks transliterated from family_big_orc.cpp.
-- (Descriptor .name "ORC CAPTAIN" → family id core:orc_captain.) Cookbook
-- (docs/lua-classpacks-design.md §3) applies. level_up is the only non-null
-- callback in the C++ descriptor.

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

og.register_hooks("living", "core:orc_captain", {
  level_up = level_up,
})
