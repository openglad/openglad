-- core:#20 (tower1) — no hooks (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The original loader called tower1 "not *really* a living"; its death
-- switch added "neither do towers" when suppressing bloodspots. Stationary
-- and bloodspot behavior now live in descriptor data.
--
-- This stationary shooter is driven entirely by descriptor data and generic
-- walker/living code, so there is nothing to register here.
-- og.register_hooks with an empty hook table is a load error, so this file
-- deliberately makes no registration call.
--
-- The registry name "BEAST" is shared with ids 18 and 19. core:beast falls
-- back to the first name match (the golem); core:#20 is tower1's exact id.

og.family("living", {
  id = "core:#20",
  wire_id = 20,
  name = "BEAST",
  short_name = og.NIL,
  stats  = { strength = 12, dexterity = 6, constitution = 12,
             intelligence = 8, armor = 6, level = 1 },
  combat = { hp = 130, melee_damage = 0, stepsize = 0,
             fire_delay = 5, fire_mp_cost = 2 },
  costs  = { hire = 0,
             train = { strength = 0, dexterity = 0, constitution = 0,
                       intelligence = 0, armor = 0, level = 0 } },
  specials = {},
  default_weapon = "core:arrow",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = false,
  magic_damage_modifier = 1,
  is_stationary = true,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOMEONE DIED",
  sprite = "towersm1.png",
  animation = "static",
  ai_line_of_sight = 10,
  description = og.NIL,
  names = {},
  playable = false,
  playable_order = 999,
  glyph = "Y",
  glyph_ascii = "Y",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
})
