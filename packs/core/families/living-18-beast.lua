-- core:beast (golem) — set_difficulty only (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The descriptor .name is "BEAST"; "core:beast" resolves to FAMILY_GOLEM
-- (id 18), the first BEAST-named family in registry scan order (giant
-- skeleton and tower1 share the display name but later ids).

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 18.0, 5.0, 7.0, 4.0)
end

og.family("living", {
  id = "core:#18",
  wire_id = 18,
  name = "BEAST",
  short_name = og.NIL,
  stats  = { strength = 12, dexterity = 6, constitution = 12,
             intelligence = 8, armor = 6, level = 1 },
  combat = { hp = 300, melee_damage = 60, stepsize = 8,
             fire_delay = 9, fire_mp_cost = 2 },
  costs  = { hire = 0,
             train = { strength = 0, dexterity = 0, constitution = 0,
                       intelligence = 0, armor = 0, level = 0 } },
  specials = {},
  default_weapon = "core:boulder",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOMEONE DIED",
  sprite = "golem1.png",
  animation = "standard",
  ai_line_of_sight = 20,
  description = og.NIL,
  names = {},
  playable = false,
  playable_order = 999,
  glyph = "G",
  glyph_ascii = "G",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  set_difficulty = set_difficulty,
})
