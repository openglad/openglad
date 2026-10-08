-- core lib: effect_hook_blade — the orc captain's chained blade: a tight, fast orbit that eats incoming shots and drags the first foe it touches to the captain's feet (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local kc = og.use("kit_captain")
local hits = og.use("effect_common").hits

-- This lib's own copy of the 16-step orbit circle (radius 24) that
-- lib/effect_shield.lua turns the shield and the boomerang on; the blade
-- scales it down to the captain's hook_radius.
local ORBIT_X = { 0, -9, -17, -22, -24, -22, -17, -9,
                  0,   9,  17,  22,  24,  22,  17,  9 }
local ORBIT_Y = { -24, -22, -17, -9, 0, 9, 17, 22,
                   24,  22,  17,  9, 0, -9, -17, -22 }

-- Two steps of the circle per tick: twice the shield's angular speed.
local function hook_orbit_offset(drawcycle, radius)
  local idx = og.mod(drawcycle * 2, 16) + 1
  local xd = og.fdiv(og.fmul(ORBIT_X[idx], radius), 24)
  local yd = og.fdiv(og.fmul(ORBIT_Y[idx], radius), 24)
  return xd, yd
end

local function retire(self)
  self.dead = 1
  self:death()
end

-- The first living foe on the blade's floor whose box the blade touches.
local function touched_foe(self)
  local foes = og.find_foes_in_range("ob", 64, self)
  for i = 1, #foes do
    local w = foes[i]
    local living = w:order() == C.ORDER_LIVING
    local same_floor = w:floor() == self:floor()
    if living and same_floor then
      if hits(self:xpos(), self:ypos(), self:sizex(), self:sizey(),
              w:xpos(), w:ypos(), w:sizex(), w:sizey()) then
        return w
      end
    end
  end
  return nil
end

local M = {}

-- core:hook_blade on_act.
function M.on_act(self)
  local owner = self:owner()
  if not owner or owner:dead() ~= 0 then
    retire(self)
    return true
  end
  local lifetime = self:lifetime()
  if lifetime < 1 then
    retire(self)
    return true
  end
  self.lifetime = lifetime - 1

  local t = og.tuning(owner)
  local xd, yd = hook_orbit_offset(self:drawcycle(), t.hook_radius)
  self:center_on(owner)  -- each step starts at the captain; offsets do not accumulate
  -- worldx/worldy are C++ floats: per-op rounding.
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))
  self:set_frame(og.mod(self:drawcycle(), 8))

  -- While it spins it is a shield: foe shots that close in are cut down,
  -- each costing the blade its damage in hitpoints (the boomerang's guard).
  local weapons = og.find_foe_weapons_in_range("weap", self:sizex() * 2, self)
  for i = 1, #weapons do
    local weapon = weapons[i]
    -- hp is a C++ float: per-op rounding.
    self.hp = og.fsub(self.hp, weapon:damage())
    weapon.dead = 1
    weapon:death()
  end
  if self.hp <= 0 then
    retire(self)
    return true
  end

  local foe = touched_foe(self)
  if foe then
    kc.snag(self, owner, foe)
    retire(self)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:hook_blade  on_act = effect_hook_blade.on_act
return M
