-- core:knife_back — thrown blade walks back to its owner, probing hits (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local WEAP_KNIFE = assert(og.family_id("weapon", "core:knife"))

-- knife_back_on_act: walk the blade back toward its thrower one stepsize at a
-- time, probing each step with a throwaway knife so the return trip still
-- hits whatever it passes through; on arrival the thrower gets the shot back.
local function on_act(self)
  local owner = self:owner()
  -- rule: A returning blade whose thrower died simply expires and refunds nothing.
  -- tests: PackLuaKnifeBack.a_blade_whose_thrower_died_simply_expires
  -- tests: SemKnifeBack.a_returning_knife_flies_home_until_it_is_within_10_px
  if not owner or owner:dead() ~= 0 then
    self:set_dead(1)
    return true
  end
  local distance = self:distance_to_ob(owner)
  -- rule: A thrown knife flies back to its thrower one step a tick; within 10 px (corner Manhattan)
  --   it is caught and the thrower gets that throw back (weapons_left + 1).
  -- tests: PackLuaKnifeBack.a_blade_that_reaches_its_thrower_returns_the_shot
  -- tests: SemKnifeBack.a_returning_knife_flies_home_until_it_is_within_10_px
  if distance > 10 then
    local xd, yd = 0, 0
    -- rule: A returning knife steps at most stepsize per axis toward its thrower's corner and never
    --   overshoots; aligned axes do not move.
    -- tests: SemKnifeBack.a_returning_knife_steps_at_most_its_stepsize_per_axis_and_never_overshoots
    if owner:xpos() > self:xpos() then
      xd = og.min(self:stepsize(), owner:xpos() - self:xpos())
    elseif owner:xpos() < self:xpos() then
      xd = og.max(owner:xpos() - self:xpos(), -self:stepsize())
    end
    -- rule: Part of the rule above that begins "A returning knife steps at most stepsize per axis
    --   ..."
    -- tests: SemKnifeBack.a_returning_knife_steps_at_most_its_stepsize_per_axis_and_never_overshoots
    if owner:ypos() > self:ypos() then
      yd = og.min(self:stepsize(), owner:ypos() - self:ypos())
    elseif owner:ypos() < self:ypos() then
      yd = og.max(owner:ypos() - self:ypos(), -self:stepsize())
    end
    -- worldx/worldy are C++ floats: per-op rounding
    self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
    local probe = og.add_ob("weapon", WEAP_KNIFE)
    -- rule: A returning knife that cannot create its collision probe (world full) drops out of the
    --   air (dies) without refunding the throw.
    -- tests: SemKnifeBack.a_returning_knife_that_cannot_make_its_probe_drops_without_a_refund
    if not probe then
      self:set_ani_type(C.ANI_WALK)
      self:set_dead(1)
      return true
    end
    probe:set_damage(self:damage())
    probe:set_owner(owner)
    probe.team = self.team
    probe:set_death_called(1)  -- the probe must not spawn its own knife_back
    probe:set_floor(self:floor())  -- collision probe on our floor
    probe:setworldxy(self:worldx(), self:worldy())
    -- xd/yd may be the float stepsize: the probe point sums in C float.
    -- query_object_passable draws (obmap miss roll), and attack draws too.
    -- rule: A returning knife still cuts whatever it passes through on the way home: an object in
    --   its next step is struck at the knife's damage (by a throwaway probe knife on its floor and
    --   team), and each strike quarters the knife's remaining damage.
    -- tests: SemKnifeBack.a_returning_knife_cuts_what_it_passes_and_quarters_its_damage
    if not og.query_object_passable(og.fadd(self:xpos(), xd),
                                    og.fadd(self:ypos(), yd), probe) then
      probe:attack(probe:collide_ob() --[[@as og.Walker]]) -- a false object-passable query recorded this collider
      -- damage is a C++ float: fdiv is a genuine float quarter
      self:set_damage(og.fdiv(self:damage(), 4.0))
    end
    probe:set_dead(1)
  else
    owner:set_weapons_left(owner:weapons_left() + 1)
    self:set_ani_type(C.ANI_WALK)
    self:set_dead(1)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:knife_back  on_act = knife_back.on_act
return {
  on_act = on_act,
}
