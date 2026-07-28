-- core:knife_back — thrown blade walks back to its owner, probing hits (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local WEAP_KNIFE = assert(og.family_id("weapon", "core:knife"))

-- knife_back_on_act: walk the blade back toward its thrower one stepsize at a
-- time, probing each step with a throwaway knife so the return trip still
-- hits whatever it passes through; on arrival the thrower gets the shot back.
local function on_act(self)
  local owner = self:owner()
  if not owner or owner:dead() ~= 0 then
    self:set_dead(1)
    return true
  end
  local distance = self:distance_to_ob(owner)
  if distance > 10 then
    local xd, yd = 0, 0
    if owner:xpos() > self:xpos() then
      xd = og.min(self:stepsize(), owner:xpos() - self:xpos())
    elseif owner:xpos() < self:xpos() then
      xd = og.max(owner:xpos() - self:xpos(), -self:stepsize())
    end
    if owner:ypos() > self:ypos() then
      yd = og.min(self:stepsize(), owner:ypos() - self:ypos())
    elseif owner:ypos() < self:ypos() then
      yd = og.max(owner:ypos() - self:ypos(), -self:stepsize())
    end
    -- worldx/worldy are C++ floats: per-op rounding
    self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
    local probe = og.add_ob("weapon", WEAP_KNIFE)
    if not probe then
      self:set_ani_type(C.ANI_WALK)
      self:set_dead(1)
      return true
    end
    probe:set_damage(self:damage())
    probe:set_owner(owner)
    probe.team = self.team
    probe:set_death_called(1)  -- the probe must not spawn its own knife_back
    probe:set_floor(self:floor())  -- collision probe on our floor (A8)
    probe:setworldxy(self:worldx(), self:worldy())
    -- xd/yd may be the float stepsize: the probe point sums in C float.
    -- query_object_passable draws (obmap miss roll), and attack draws too.
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

og.register_hooks("fx", "core:knife_back", {
  on_act = on_act,
})
