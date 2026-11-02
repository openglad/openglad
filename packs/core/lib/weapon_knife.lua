-- core:knife — returning-blade spawn on death (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
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
  -- rule: A knife with no owner at all just dies (no returning blade).
  -- unreachable: weap::act re-owns a null or dead owner's knife to itself before it can die
  --   (weap.cpp:66-70); classic divergence on the unreached arm: the old game (weap.cpp:143-145 at
  --   7ff1654a) spawns a knife_back for a NULL owner, the current Lua returns false
  if not owner then
    return false  -- no special handling (C++: owner_fd == nullptr)
  end
  -- rule: A knife thrown by a family with a returning weapon (the soldier) comes back: on death it
  --   spawns a knife_back fx owned by the thrower, centred on the knife, on its floor, with its
  --   velocity, stepsize and damage, playing the attack animation. Other families' knives just
  --   drop. Quirk: a knife whose thrower died is re-owned by itself (family byte 0 ==
  --   FAMILY_SOLDIER), so an orphaned knife always returns, owned by the dead knife.
  -- tests: WeapBehavior.weap_death_knife_soldier_owner, WeapBehavior.weap_death_knife_non_soldier
  -- tests: SemLibsKnife.a_classic_orphaned_knife_returns_owned_by_itself
  if not og.family_flag("living", owner:family(), "has_returning_weapon") then
    return false  -- no special handling
  end

  local blade = og.add_ob("fx", FX_KNIFE_BACK)
  -- plumbing: add_ob('fx', core:knife_back) failure guard.
  if not blade then
    return true
  end
  blade:set_owner(self:owner())
  -- Return flight starts on the knife's floor.
  blade:set_floor(self:floor())
  blade:center_on(self)
  blade:set_lastx(self:lastx())
  blade:set_lasty(self:lasty())
  blade:set_stepsize(self:stepsize())
  blade.ani_type = C.ANI_ATTACK
  blade.damage = self:damage()
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:knife  on_death = knife.on_death
return {
  on_death = on_death,
}
