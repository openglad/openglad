-- core:knife_back — the returning-blade effect, transliterated from
-- src/gameplay/families/effect_family_knife_back.cpp.
--
-- Cookbook (docs/lua-classpacks-design.md §3): xpos/ypos are shorts while
-- stepsize/worldx/worldy/damage are C++ floats, so the step deltas are built
-- with plain comparisons (exact in double) but every world-coordinate sum and
-- the damage quartering go through og.f*.

local C = og.C
local WEAP_KNIFE = og.family_id("weapon", "core:knife")

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
      if (owner:xpos() - self:xpos()) > self:stepsize() then
        xd = self:stepsize()
      else
        xd = owner:xpos() - self:xpos()
      end
    elseif owner:xpos() < self:xpos() then
      if (self:xpos() - owner:xpos()) > self:stepsize() then
        xd = -self:stepsize()
      else
        xd = owner:xpos() - self:xpos()
      end
    end
    if owner:ypos() > self:ypos() then
      if (owner:ypos() - self:ypos()) > self:stepsize() then
        yd = self:stepsize()
      else
        yd = owner:ypos() - self:ypos()
      end
    elseif owner:ypos() < self:ypos() then
      if (self:ypos() - owner:ypos()) > self:stepsize() then
        yd = -self:stepsize()
      else
        yd = owner:ypos() - self:ypos()
      end
    end
    self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
    local newob = og.add_ob("weapon", WEAP_KNIFE)
    if not newob then
      self:set_ani_type(C.ANI_WALK)
      self:set_dead(1)
      return true
    end
    newob:set_damage(self:damage())
    newob:set_owner(owner)
    newob:set_team_num(self:team_num())
    newob:set_death_called(1)  -- to ensure no spawning of more ..
    newob:set_floor(self:floor())  -- collision probe on our floor (A8)
    newob:setworldxy(self:worldx(), self:worldy())
    if not og.query_object_passable(og.fadd(self:xpos(), xd),
                                    og.fadd(self:ypos(), yd), newob) then
      newob:attack(newob:collide_ob())
      self:set_damage(og.fdiv(self:damage(), 4.0))
    end
    newob:set_dead(1)
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
