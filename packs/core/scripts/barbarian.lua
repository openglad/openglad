-- core:barbarian — boulder-toss special (cookbook: docs/lua-classpacks-design.md §3).

local WEAP_BOULDER = og.family_id("weapon", "core:boulder")

local function do_special(self)
  if self:busy() > 0 then
    return false
  end
  local shot = self:fire()
  if not shot then
    return false
  end
  local boulder = og.add_ob("weapon", WEAP_BOULDER)
  if not boulder then
    return false
  end
  boulder:set_floor(shot:floor())  -- boulder rolls on the thrower's floor (A8)
  boulder:center_on(shot)
  boulder:set_owner(self)
  boulder.level = self.level
  boulder:set_lastx(shot:lastx())
  boulder:set_lasty(shot:lasty())
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

og.register_hooks("living", "core:barbarian", {
  do_special = do_special,
  level_up = level_up,
})
