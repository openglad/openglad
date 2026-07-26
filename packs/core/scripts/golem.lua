-- core:beast (golem, FAMILY_GOLEM) — behavior hooks transliterated from
-- family_golem.cpp. Cookbook (docs/lua-classpacks-design.md §3) applies.
-- The descriptor .name is "BEAST"; "core:beast" resolves to FAMILY_GOLEM
-- (id 18), the first BEAST-named family in registry scan order (giant
-- skeleton and tower1 share the display name but later ids).

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 18.0, 5.0, 7.0, 4.0)
end

og.register_hooks("living", "core:beast", {
  set_difficulty = set_difficulty,
})
