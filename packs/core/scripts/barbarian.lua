-- core:barbarian — behavior hooks transliterated from family_barbarian.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local WEAP_BOULDER = og.family_id("weapon", "core:boulder")

local function do_special(self)
  if self:busy() > 0 then
    return false
  end
  local newob = self:fire()
  if not newob then
    return false
  end
  local alive = og.add_ob("weapon", WEAP_BOULDER)
  if not alive then
    return false
  end
  alive:set_floor(newob:floor())  -- boulder rolls on the thrower's floor (A8)
  alive:center_on(newob)
  alive:set_owner(self)
  alive:s_set_level(self:s_level())
  alive:set_lastx(newob:lastx())
  alive:set_lasty(newob:lasty())
  if self:has_guy() then
    -- guy strength is a short: strength / 7 is INTEGER division in the C++,
    -- then the int joins 1.0f in a single float add.
    alive:set_stepsize(og.fadd(1.0, og.div(self:g_strength(), 7)))
    alive:set_damage(og.fadd(alive:damage(),
                             og.fdiv(self:g_strength(), 5.0)))
  else
    alive:set_stepsize(og.fmul(self:s_level(), 2.0))
    alive:set_damage(og.fadd(alive:damage(), self:s_level()))
  end
  if alive:stepsize() < 1 then
    alive:set_stepsize(1)
  end
  if alive:stepsize() > 15 then
    alive:set_stepsize(15)
  end
  if alive:lasty() > 0 then
    alive:set_lasty(alive:stepsize())
  elseif alive:lasty() < 0 then
    alive:set_lasty(-alive:stepsize())
  end
  if alive:lastx() > 0 then
    alive:set_lastx(alive:stepsize())
  elseif alive:lastx() < 0 then
    alive:set_lastx(-alive:stepsize())
  end
  if self:current_special() == 2 then
    alive:set_skip_exit(5000)
  else
    alive:set_skip_exit(0)
  end
  newob:set_dead(1)
  self:set_busy(og.fadd(og.fadd(self:busy(), 1.0),
                        og.fmul(self:current_special(), 5.0)))
  return true
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 12, 3, 12, 4, 1)
end

og.register_hooks("living", "core:barbarian", {
  do_special = do_special,
  level_up = level_up,
})
