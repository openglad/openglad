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

og.register_hooks("living", "core:elf", {
  specials = {
    -- some rocks (normal)
    rocks = some_rocks,
    -- more rocks, and bouncing
    -- we get 50% longer, too!
    bouncing_rocks = bounce_volley(3, 2, 3),
    -- get double distance
    lots_of_rocks = bounce_volley(4, 3, 4),
    -- we get 150% longer, too!
    default = bounce_volley(5, 4, 5),  -- case 4 and every unmapped slot
  },
  level_up = level_up,
})
