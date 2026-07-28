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

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    -- some rocks (normal)
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
  elseif sp == 2 then
    -- more rocks, and bouncing
    -- magicpoints is a C++ float: per-op rounding.
    self.magicpoints = og.fadd(self.magicpoints, 3 * self:s_weapon_cost())
    for i = 1, 2 do
      local rock = self:fire()
      if not rock then
        return false
      end
      rock:set_lineofsight(rock:lineofsight() * 3 // 2)
      rock:set_do_bounce(1)
      -- lastx/lasty are C++ floats: per-op rounding.
      rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
      rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
    end
  elseif sp == 3 then
    -- magicpoints is a C++ float: per-op rounding.
    self.magicpoints = og.fadd(self.magicpoints, 4 * self:s_weapon_cost())
    for i = 1, 3 do
      local rock = self:fire()
      if not rock then
        return false
      end
      rock:set_lineofsight(rock:lineofsight() * 2)
      rock:set_do_bounce(1)
      -- lastx/lasty are C++ floats: per-op rounding.
      rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
      rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
    end
  else
    -- C++ switch: case 4 and default share the mega-rocks branch
    -- magicpoints is a C++ float: per-op rounding.
    self.magicpoints = og.fadd(self.magicpoints, 5 * self:s_weapon_cost())
    for i = 1, 4 do
      local rock = self:fire()
      if not rock then
        return false
      end
      rock:set_lineofsight(rock:lineofsight() * 5 // 2)
      rock:set_do_bounce(1)
      -- lastx/lasty are C++ floats: per-op rounding.
      rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))
      rock:set_lasty(og.fmul(rock:lasty(), next_spread_multiplier()))
    end
  end
  return true
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 6, 9, 6, 8, 1)
end

og.register_hooks("living", "core:elf", {
  do_special = do_special,
  level_up = level_up,
})
