-- core:knife — returning-blade spawn on death (cookbook: docs/lua-classpacks-design.md §3).
--
-- The returning gate reads the knife OWNER's living-family descriptor:
-- og.family_flag("living", owner:family(), "has_returning_weapon"). A nil
-- owner is the C++ `owner_fd == nullptr` case: no special handling.

local C = og.C
local FX_KNIFE_BACK = assert(og.family_id("fx", "core:knife_back"))

-- knife_on_death: a knife thrown by a returning-weapon family (soldier)
-- spawns the FX knife_back that flies back to its owner.
local function on_death(self)
  local owner = self:owner()
  if not owner then
    return false  -- no special handling (C++: owner_fd == nullptr)
  end
  if not og.family_flag("living", owner:family(), "has_returning_weapon") then
    return false  -- no special handling
  end

  local blade = og.add_ob("fx", FX_KNIFE_BACK)
  if not blade then
    return true
  end
  blade:set_owner(self:owner())
  -- return flight starts on the knife's floor (A8)
  blade:set_floor(self:floor())
  blade:center_on(self)
  blade:set_lastx(self:lastx())
  blade:set_lasty(self:lasty())
  blade:set_stepsize(self:stepsize())
  blade.ani_type = C.ANI_ATTACK
  blade.damage = self:damage()
  return true
end

og.register_hooks("weapon", "core:knife", {
  on_death = on_death,
})
