-- core:#19 (giant skeleton) — no hooks (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The original death switch spared only ghost/skeleton/tower1 a bloodspot;
-- the descriptor extraction classed this family with those undead, and
-- leaves_bloodspot: false carries that as descriptor data.
-- The family is otherwise descriptor-driven: og.register_hooks with an
-- empty hook table is a load error, and registering a hook here would add
-- behavior it never had. Deliberately a no-op chunk.

og.family("living", {
  id = "core:#19",
  wire_id = 19,
  name = "BEAST",
  short_name = og.NIL,
  stats  = { strength = 12, dexterity = 6, constitution = 12,
             intelligence = 8, armor = 6, level = 1 },
  combat = { hp = 300, melee_damage = 60, stepsize = 8,
             fire_delay = 7, fire_mp_cost = 2 },
  costs  = { hire = 0,
             train = { strength = 0, dexterity = 0, constitution = 0,
                       intelligence = 0, armor = 0, level = 0 } },
  specials = {},
  default_weapon = "core:boulder",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = false,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOMEONE DIED",
  sprite = "gs1.png",
  animation = "giant_skeleton",
  ai_line_of_sight = 20,
  description = og.NIL,
  names = {},
  playable = false,
  playable_order = 999,
  glyph = "K",
  glyph_ascii = "K",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
})
