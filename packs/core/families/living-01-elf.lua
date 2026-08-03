-- core:elf — rock-spread specials (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- Jonathan Dearborn added this spread in 2013 so stacked rocks separate
-- visibly in flight.
-- The original jitter came from libc rand, separate from gameplay RNG.
-- og.cosmetic_rand selects that compatibility stream when installed and
-- otherwise uses the deterministic gameplay stream.
local function next_spread_multiplier()
  local spread_roll = og.cosmetic_rand(101)
  -- 0.8 + 0.4*roll/100 is a float chain: per-op rounding.
  return og.fadd(0.8, og.fdiv(og.fmul(0.4, spread_roll), 100.0))
end

local function some_rocks(self)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fadd(self.magicpoints, 2 * self:s_weapon_cost())
  local rock = self:fire()
  if not rock then
    return false
  end
  -- lastx/lasty are C++ floats: per-op rounding.
  rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
  rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
  rock = self:fire()
  if not rock then
    return false
  end
  -- lastx/lasty are C++ floats: per-op rounding.
  rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
  rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
  return true
end

-- The three bouncing volleys share one immutable shape. los_num spells each
-- old line-of-sight multiplier as `* los_num // 2`; case 3's `* 2` is
-- therefore `* 4 // 2`.
local function bounce_volley(cost_mult, count, los_num)
  return function(self)
    -- magicpoints is a C++ float: per-op rounding.
    self.magicpoints = og.fadd(self.magicpoints,
                               cost_mult * self:s_weapon_cost())
    for i = 1, count do
      local rock = self:fire()
      if not rock then
        return false
      end
      rock:set_lineofsight(rock:lineofsight() * los_num // 2)
      rock:set_do_bounce(1)
      -- lastx/lasty are C++ floats: per-op rounding.
      rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
      rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
    end
    return true
  end
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 6, 9, 6, 8, 1)
end

og.family("living", {
  id = "core:elf",
  wire_id = 1,
  name = "ELF",
  short_name = og.NIL,
  stats  = { strength = 5, dexterity = 14, constitution = 5,
             intelligence = 12, armor = 8, level = 1 },
  combat = { hp = 75, melee_damage = 12, stepsize = 4,
             fire_delay = 5, fire_mp_cost = 1 },
  costs  = { hire = 150,
             train = { strength = 25, dexterity = 6, constitution = 12,
                       intelligence = 8, armor = 50, level = 200 } },
  specials = {
    -- some rocks (normal)
    { id = "rocks",          name = "ROCKS",          mp_cost = 10, cast = some_rocks },
    -- more rocks, and bouncing
    -- we get 50% longer, too!
    { id = "bouncing_rocks", name = "BOUNCING ROCKS", mp_cost = 20, cast = bounce_volley(3, 2, 3) },
    -- get double distance
    { id = "lots_of_rocks",  name = "LOTS OF ROCKS",  mp_cost = 30, cast = bounce_volley(4, 3, 4) },
    { id = "mega_rocks",     name = "MEGA ROCKS",     mp_cost = 40 },
    -- we get 150% longer, too!
    default_cast = bounce_volley(5, 4, 5),  -- case 4 and every unmapped slot
  },
  default_weapon = "core:rock",
  flags = { "FORESTWALK" },
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "ELF KILLED",
  sprite = "elf.png",
  animation = "standard",
  ai_line_of_sight = 8,
  description = "Elves are small and weak, \nbut are harder to hit than\nmost classes. Alone of all\nthe classes, elves possess\nthe 'ForestWalk' ability. \n\nSpecial: Rocks",
  names = { "Legolas", "Took", "Elrond", "Tanis", "Acorn", "Lightfoot",
            "Treewee" },
  playable = true,
  playable_order = 2,
  glyph = "e",
  glyph_ascii = "e",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  level_up = level_up,
})
