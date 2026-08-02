-- core:orc_captain — level_up only (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- (Descriptor .name "ORC CAPTAIN" → family id core:orc_captain.)

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

og.family("living", {
  id = "core:orc_captain",
  wire_id = 15,
  name = "ORC CAPTAIN",
  short_name = "ORC CAP.",
  stats  = { strength = 18, dexterity = 8, constitution = 16,
             intelligence = 5, armor = 11, level = 1 },
  combat = { hp = 180, melee_damage = 28, stepsize = 3,
             fire_delay = 6, fire_mp_cost = 2 },
  costs  = { hire = 1000,
             train = { strength = 6, dexterity = 15, constitution = 5,
                       intelligence = 40, armor = 50, level = 200 } },
  specials = {},
  default_weapon = "core:knife",
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
  sprite = "orc2.png",
  animation = "standard",
  ai_line_of_sight = 25,
  description = "Orcs captains are stronger\nand smarter than the basic\norc.  They throw blades   \nacross the battlefield to \ndeal damage from afar.",
  names = { "Grom", "Thrull", "Vernix", "Lanugo", "Grok", "Horde", "Grog",
            "Krosh" },
  playable = false,
  playable_order = 999,
  glyph = "O",
  glyph_ascii = "O",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  level_up = level_up,
})
