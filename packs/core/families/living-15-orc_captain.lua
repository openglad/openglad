-- core:orc_captain — howl, eat corpse, hook blade / knife fan, war banner / warband (cookbook: docs/lua-classpacks-design.md §3).
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
    { id = "howl",       name = "HOWL",       mp_cost = 25, new_kit = true, cast = kc.howl,       ai = kc.ai_howl },
    { id = "eat_corpse", name = "EAT CORPSE", mp_cost = 20, new_kit = true, cast = kc.eat_corpse, ai = kc.ai_eat },
    { id = "hook",       name = "HOOK BLADE", mp_cost = 40, new_kit = true, alternate = { name = "KNIFE FAN", mp_cost = 30 }, cast = kc.hook_or_fan,       ai = kc.ai_hook },
    { id = "banner",     name = "WAR BANNER", mp_cost = 80, new_kit = true, alternate = { name = "WARBAND" },                 cast = kc.banner_or_warband, ai = kc.ai_banner },
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
  description = "Orc captains are stronger and smarter than the basic orc. They throw blades across the battlefield.",
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

  tuning = {
    -- HOWL, EAT CORPSE (lib/orc_specials.lua): the orc's five keys, same values
    yell_stun_base = 10,
    yell_level_roll_mult = 10,
    yell_con_roll_mult = 10,
    corpse_eat_range = 24,
    corpse_heal_per_level = 5,
    -- HOOK BLADE: the blade spirals out from hook_start px, growing
    -- hook_growth px a tick to hook_reach px, then back in (hook_ticks in
    -- all, out and back); guard hp; a snagged foe is reeled hook_reel_step
    -- px a tick for at most hook_reel_max ticks, then stunned hook_stun more
    hook_ticks = 48,
    hook_hp = 30,
    hook_start = 12,
    hook_growth = 2,
    hook_reach = 60,
    hook_reel_step = 8,
    hook_reel_max = 8,
    hook_stun = 12,
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
