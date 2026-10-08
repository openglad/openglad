-- core:faerie — level_up, and the New Specials blink/swap, glimmer, hasten and wish (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
local kf = og.use("kit_faerie")
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
  specials = {
    { id = "blink",   name = "BLINK",   mp_cost = 8,   new_kit = true, alternate = { name = "SWAP", mp_cost = 20 }, cast = kf.blink_or_swap, ai = kf.ai_blink },
    { id = "glimmer", name = "GLIMMER", mp_cost = 24,  new_kit = true, cast = kf.glimmer, ai = kf.ai_glimmer },
    { id = "hasten",  name = "HASTEN",  mp_cost = 30,  new_kit = true, alternate = { name = "HASTE SELF" }, cast = kf.hasten, ai = kf.ai_hasten },
    { id = "wish",    name = "WISH",    mp_cost = 100, new_kit = true, cast = kf.wish, ai = kf.ai_wish },
  },
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
  description = "Faeries are small, flying above friends and enemies alike unnoticed. Delicate and easily destroyed, they sprinkle a magic powder which freezes their enemies.\n\nSpecial: Blink / Swap, Glimmer, Hasten, Wish",
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

  tuning = {
    -- Blink: hop range = base + per_level * level (px, same floor)
    blink_base = 24,
    blink_per_level = 6,
    -- Swap: reach (px) to the nearest walker in sight, ally or foe
    swap_range = 80,
    -- Hasten: reach (px) to an ally; the speed potion's timer (ticks) and bonus (px/tick)
    hasten_reach = 40,
    hasten_ticks = 150,
    hasten_bonus = 2,
    -- Wish: reach (px) to a fallen ally's stain; radius (px) of the full heal
    wish_range = 120,
    wish_heal_radius = 96,
  },

  level_up = level_up,
})
