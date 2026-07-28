-- core:elf — rock-spread specials (cookbook: docs/lua-classpacks-design.md §3).

-- next_spread_multiplier: the legacy code picks its RNG source as
--   src = cosmetic_rng_override() != nullptr ? *cosmetic_rng_override() : rng
-- i.e. the parity harness's libc-rand cosmetic stream when installed
-- (mirroring master's dual-RNG-stream rock spread), else the gameplay RNG.
-- og.cosmetic_rand is exactly that selector, so the draw stays out of the
-- schema rng_state observable-hash under the harness — byte-clean in both
-- og_test_parity and branch-vs-master diff_dumps.py audits.
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

-- The three bouncing volleys (cases 2, 3, 4-and-default) share one shape,
-- parameterized over chunk-load constants (R6-safe closure). los_num spells
-- each legacy lineofsight multiplier as `* los_num // 2`; case 3's legacy
-- `* 2` is `* 4 // 2`, identical over Lua integers.
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
    [1] = some_rocks,
    [2] = bounce_volley(3, 2, 3),
    [3] = bounce_volley(4, 3, 4),
    default = bounce_volley(5, 4, 5),  -- case 4 and every unmapped slot
  },
  level_up = level_up,
})
