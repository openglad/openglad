-- core:faerie — level_up only (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 12, 4, 8, 1)
end

og.family("living", {
  id = "core:faerie",
  wire_id = 7,
  name = "FAERIE",
  short_name = og.NIL,
  stats  = { strength = 3, dexterity = 8, constitution = 3,
             intelligence = 14, armor = 2, level = 1 },
  combat = { hp = 75, melee_damage = 5, stepsize = 4,
             fire_delay = 9, fire_mp_cost = 2 },
  costs  = { hire = 450,
             train = { strength = 25, dexterity = 6, constitution = 12,
                       intelligence = 8, armor = 50, level = 200 } },
  specials = {},
  default_weapon = "core:sprinkle",
  flags = { "FLYING", "ANIMATE" },
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "FAERIE POPPED",
  sprite = "faerie.png",
  animation = "standard",
  ai_line_of_sight = 8,
  description = "The faerie are small,     \nflying above friends and  \nenemies alike unnoticed.  \nAlthough they are delicate\nand easily destroyed,     \nfaeries can sprinkle a    \nmagic powder which freezes\ntheir enemies.",
  names = { "Tink", "Gem", "Glitter", "Jewel", "Blossom", "Ruby", "Muffin",
            "Flutter", "Sparkle", "Sprint", "Sprite", "Eve", "Twinkle",
            "Violet", "Daisy", "Lily" },
  playable = true,
  playable_order = 13,
  glyph = "f",
  glyph_ascii = "f",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  level_up = level_up,
})
