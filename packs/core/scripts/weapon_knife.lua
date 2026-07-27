-- core:knife — weapon behavior hooks transliterated from
-- weapon_family_knife.cpp. Cookbook (docs/lua-classpacks-design.md §3)
-- applies.
--
-- The C++ gate reads the knife OWNER's living-family descriptor —
-- get_family_descriptor(owner->family())->has_returning_weapon — which is
-- og.family_flag("living", family_byte, "has_returning_weapon") here. A nil
-- owner is the C++ `owner_fd == nullptr` case: no special handling.

local C = og.C
local FX_KNIFE_BACK = og.family_id("fx", "core:knife_back")

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

  local newob = og.add_ob("fx", FX_KNIFE_BACK)
  if not newob then
    return true
  end
  newob:set_owner(self:owner())
  -- return flight starts on the knife's floor (A8)
  newob:set_floor(self:floor())
  newob:center_on(self)
  newob:set_lastx(self:lastx())
  newob:set_lasty(self:lasty())
  newob:set_stepsize(self:stepsize())
  newob:set_ani_type(C.ANI_ATTACK)
  newob:set_damage(self:damage())
  return true
end

og.register_hooks("weapon", "core:knife", {
  on_death = on_death,
})
