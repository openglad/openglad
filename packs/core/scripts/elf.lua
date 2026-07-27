-- core:elf — behavior hooks transliterated from family_elf.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.
--
-- elf_rng() (gameplay override if installed, else world rng) is og.rand:
-- the binding routes to world->rng_, which is the same stream in both
-- production and the TESTING sim-override path.

-- next_spread_multiplier (family_elf.cpp): the C++ picks its RNG source as
--   src = cosmetic_rng_override() != nullptr ? *cosmetic_rng_override() : rng
-- i.e. the parity harness's libc-rand cosmetic stream when installed
-- (mirroring master's dual-RNG-stream rock spread), else the gameplay RNG.
-- og.cosmetic_rand is exactly that selector, so the draw stays out of the
-- schema rng_state observable-hash under the harness — byte-clean in both
-- og_test_parity and branch-vs-master diff_dumps.py audits.
local function next_spread_multiplier()
  local draw = og.cosmetic_rand(101)
  return og.fadd(0.8, og.fdiv(og.fmul(0.4, draw), 100.0))
end

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    -- some rocks (normal)
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   og.fmul(2.0, self:s_weapon_cost())))
    local fireob = self:fire()
    if not fireob then
      return false
    end
    fireob:set_lastx(og.fmul(fireob:lastx(), next_spread_multiplier()))
    fireob:set_lasty(og.fmul(fireob:lasty(), next_spread_multiplier()))
    fireob = self:fire()
    if not fireob then
      return false
    end
    fireob:set_lastx(og.fmul(fireob:lastx(), next_spread_multiplier()))
    fireob:set_lasty(og.fmul(fireob:lasty(), next_spread_multiplier()))
  elseif sp == 2 then
    -- more rocks, and bouncing
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   og.fmul(3.0, self:s_weapon_cost())))
    for i = 1, 2 do
      local fireob = self:fire()
      if not fireob then
        return false
      end
      fireob:set_lineofsight(og.div(fireob:lineofsight() * 3, 2))
      fireob:set_do_bounce(1)
      fireob:set_lastx(og.fmul(fireob:lastx(), next_spread_multiplier()))
      fireob:set_lasty(og.fmul(fireob:lasty(), next_spread_multiplier()))
    end
  elseif sp == 3 then
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   og.fmul(4.0, self:s_weapon_cost())))
    for i = 1, 3 do
      local fireob = self:fire()
      if not fireob then
        return false
      end
      fireob:set_lineofsight(fireob:lineofsight() * 2)
      fireob:set_do_bounce(1)
      fireob:set_lastx(og.fmul(fireob:lastx(), next_spread_multiplier()))
      fireob:set_lasty(og.fmul(fireob:lasty(), next_spread_multiplier()))
    end
  else
    -- C++ switch: case 4 and default share the mega-rocks branch
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   og.fmul(5.0, self:s_weapon_cost())))
    for i = 1, 4 do
      local fireob = self:fire()
      if not fireob then
        return false
      end
      fireob:set_lineofsight(og.div(fireob:lineofsight() * 5, 2))
      fireob:set_do_bounce(1)
      fireob:set_lastx(og.fmul(fireob:lastx(), next_spread_multiplier()))
      fireob:set_lasty(og.fmul(fireob:lasty(), next_spread_multiplier()))
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
