-- core:orc — yell stun, corpse eating (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The shareware-era source labelled the Orc its "registered monster."

local ai = og.use("ai")
-- HOWL and EAT CORPSE live in lib/orc_specials.lua: the orc captain casts them too.
local orc = og.use("orc_specials")

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 14.0, 7.0, 6.0, 3.0)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

-- Orc promotion is descriptor data, not a registrable living hook.

og.family("living", {
  id = "core:orc",
  wire_id = 14,
  name = "ORC",
  short_name = og.NIL,
  stats  = { strength = 18, dexterity = 8, constitution = 16,
             intelligence = 5, armor = 11, level = 1 },
  combat = { hp = 140, melee_damage = 23, stepsize = 3,
             fire_delay = 7, fire_mp_cost = 2 },
  costs  = { hire = 300,
             train = { strength = 6, dexterity = 15, constitution = 5,
                       intelligence = 40, armor = 50, level = 200 } },
  specials = {
    { id = "howl",       name = "HOWL",       mp_cost = 25, cast = orc.yell },
    { id = "eat_corpse", name = "EAT CORPSE", mp_cost = 20 },
    default_cast = orc.eat_corpse,
  },
  default_weapon = "core:rock",
  flags = { "NO_RANGED" },
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = "core:orc_captain",
  promotion_level_req = 5,
  death_message = "ORC DIED",
  sprite = "orc.png",
  animation = "standard",
  ai_line_of_sight = 20,
  description = "Orcs are a basic 'grunt'; strong and hard to hurt, they don't do much more than inflict pain. Orcs can't attack at range.\n\nSpecial: Howl",
  names = { "Grom", "Thrull", "Vernix", "Lanugo", "Grok", "Horde", "Grog",
            "Krosh" },
  playable = true,
  playable_order = 9,
  glyph = "o",
  glyph_ascii = "o",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Yell stun: og.max(0, stun_base + rand0(level*level_roll_mult)
    --                       - rand0(con*con_roll_mult))
    yell_stun_base = 10,
    yell_level_roll_mult = 10,
    yell_con_roll_mult = 10,
    -- Corpse eating: reach gate (px) and heal per corpse level
    corpse_eat_range = 24,
    corpse_heal_per_level = 5,
  },

  check_special_ai = ai.foe_within(130),  -- fixed per-tick AI range
  set_difficulty = set_difficulty,
  level_up = level_up,
})
