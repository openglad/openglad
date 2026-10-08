-- core:orc_captain — howl and eat corpse, hook blade, hurl orc, war banner (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- (Descriptor .name "ORC CAPTAIN" → family id core:orc_captain.)
local kc = og.use("kit_captain")
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
  -- New Specials: every slot is new_kit, so with the setting off the
  -- captain has no specials, exactly as before the setting existed.
  specials = {
    { id = "howl",   name = "HOWL",       mp_cost = 25, new_kit = true, alternate = { name = "EAT CORPSE", mp_cost = 20 }, cast = kc.howl_or_eat,       ai = kc.ai_howl },
    { id = "hook",   name = "HOOK BLADE", mp_cost = 40, new_kit = true, alternate = { name = "KNIFE FAN",  mp_cost = 30 }, cast = kc.hook_or_fan,       ai = kc.ai_hook },
    { id = "hurl",   name = "HURL ORC",   mp_cost = 50, new_kit = true, alternate = { name = "SHOVE",      mp_cost = 30 }, cast = kc.hurl_or_shove,     ai = kc.ai_hurl },
    { id = "banner", name = "WAR BANNER", mp_cost = 80, new_kit = true, alternate = { name = "WARBAND" },                  cast = kc.banner_or_warband, ai = kc.ai_banner },
  },
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
  description = "Orc captains are stronger and smarter than the basic orc. They throw blades across the battlefield.\n\nSpecial: Howl / Eat Corpse, Hook Blade, Hurl Orc, War Banner",
  names = { "Grom", "Thrull", "Vernix", "Lanugo", "Grok", "Horde", "Grog",
            "Krosh" },
  playable = true,
  playable_order = 15,
  glyph = "O",
  glyph_ascii = "O",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- HOWL / EAT CORPSE (lib/orc_specials.lua): the orc's five keys, same values
    yell_stun_base = 10,
    yell_level_roll_mult = 10,
    yell_con_roll_mult = 10,
    corpse_eat_range = 24,
    corpse_heal_per_level = 5,
    -- HOOK BLADE: orbit lifetime, guard hp, orbit radius (px), drag walk, stun
    hook_ticks = 48,
    hook_hp = 30,
    hook_radius = 16,
    hook_drag_ticks = 6,
    hook_stun = 12,
    -- HURL ORC: grab reach, throw range, impact radius (px), damage, stun
    hurl_adjacent = 20,
    hurl_range = 120,
    hurl_radius = 24,
    hurl_damage_per_level = 6,
    hurl_stun = 10,
    -- SHOVE: reach (px), tiles of push, stun
    shove_reach = 40,
    shove_tiles = 2,
    shove_stun = 10,
    -- WAR BANNER: plant reach (squared px), hp, lifetime; the aura's own
    -- numbers live on core:war_banner, which outlives its captain
    banner_plant_sq = 1024,
    banner_hp_base = 120,
    banner_hp_per_level = 10,
    banner_ticks = 900,
    -- WARBAND: grunts at level 10, one more every N levels, lifetime, the
    -- inward edge scan (tiles) and the cap on live grunts
    warband_base = 2,
    warband_per_levels = 3,
    warband_lifetime = 400,
    warband_scan_tiles = 12,
    warband_cap = 6,
  },

  level_up = level_up,
})
