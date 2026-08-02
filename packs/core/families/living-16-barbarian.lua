-- core:barbarian — boulder-toss special (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local WEAP_BOULDER = assert(og.family_id("weapon", "core:boulder"))

local function do_special(self)
  if self:busy() > 0 then
    return false
  end
  local shot = self:fire()
  -- failed somehow? !?!
  if not shot then
    return false
  end
  local boulder = og.add_ob("weapon", WEAP_BOULDER)
  if not boulder then
    return false
  end
  boulder:set_floor(shot:floor())  -- boulder rolls on the thrower's floor
  boulder:center_on(shot)
  boulder:set_owner(self)
  boulder.level = self.level
  boulder:set_lastx(shot:lastx())
  boulder:set_lasty(shot:lasty())
  -- Set our boulder's speed and extra damage ..
  if self:has_guy() then
    -- guy strength is a short: strength / 7 is INTEGER division in the C++,
    -- then the int joins 1.0f in a single float add.
    boulder:set_stepsize(1 + self:g_strength() // 7)
    -- damage is a C++ float and strength/5 is FLOAT division: per-op
    -- rounding.
    boulder.damage = og.fadd(boulder:damage(),
                             og.fdiv(self:g_strength(), 5.0))
  else
    boulder:set_stepsize(self.level * 2)
    -- damage is a C++ float: per-op rounding.
    boulder.damage = og.fadd(boulder:damage(), self.level)
  end
  if boulder:stepsize() < 1 then
    boulder:set_stepsize(1)
  end
  if boulder:stepsize() > 15 then
    boulder:set_stepsize(15)
  end
  if boulder:lasty() > 0 then
    boulder:set_lasty(boulder:stepsize())
  elseif boulder:lasty() < 0 then
    boulder:set_lasty(-boulder:stepsize())
  end
  if boulder:lastx() > 0 then
    boulder:set_lastx(boulder:stepsize())
  elseif boulder:lastx() < 0 then
    boulder:set_lastx(-boulder:stepsize())
  end
  -- Legacy sentinel: 5000 means "explode this boulder on impact."
  if self:current_special() == 2 then
    boulder:set_skip_exit(5000)
  else
    boulder:set_skip_exit(0)
  end
  shot.dead = 1
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(og.fadd(self:busy(), 1.0),
                      self:current_special() * 5)
  return true
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

og.family("living", {
  id = "core:barbarian",
  wire_id = 16,
  name = "BARBARIAN",
  short_name = "BARBAR.",
  stats  = { strength = 14, dexterity = 5, constitution = 14,
             intelligence = 8, armor = 8, level = 1 },
  combat = { hp = 150, melee_damage = 25, stepsize = 3,
             fire_delay = 5.5, fire_mp_cost = 2 },
  costs  = { hire = 350,
             train = { strength = 5, dexterity = 35, constitution = 5,
                       intelligence = 35, armor = 50, level = 200 } },
  specials = {
    { id = "hurl_boulder",      name = "HURL BOULDER",      mp_cost = 20 },
    { id = "exploding_boulder", name = "EXPLODING BOULDER", mp_cost = 30 },
    default_cast = do_special,
  },
  default_weapon = "core:hammer",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 0.5,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOMEONE DIED",
  sprite = "barby.png",
  animation = "standard",
  ai_line_of_sight = 12,
  description = "Barbarians are powerful   \nand resist some magic     \ndamage, but have more will\nthan skill. They are tough,\ntending to bash their way \nthrough trouble with heavy\niron hammers.             \nSpecial: Hurl Boulder",
  names = { "Thor", "Conan", "Beowulf", "Cronus", "Pallas", "Atlas",
            "Prometheus", "Titan" },
  playable = true,
  playable_order = 1,
  glyph = "B",
  glyph_ascii = "B",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  level_up = level_up,
})
