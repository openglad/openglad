-- core:magic_shield + core:boomerang — orbiting guard effects (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The ORBIT_X/ORBIT_Y tables are pure constants (R6-clean: no sim state).

local C = og.C

-- effect.cpp: orbit_offset(int drawcycle, float& xd, float& yd)
-- 16-step circle of integer offsets (all exactly representable in float).
local ORBIT_X = { 0, -9, -17, -22, -24, -22, -17, -9,
                  0,   9,  17,  22,  24,  22,  17,  9 }
local ORBIT_Y = { -24, -22, -17, -9, 0, 9, 17, 22,
                   24,  22,  17,  9, 0, -9, -17, -22 }

-- Lua arrays are 1-based; the C++ index is drawcycle % 16.
local function orbit_offset(drawcycle)
  -- og.mod kept: operand subtype not provable to the audit.
  local idx = og.mod(drawcycle, 16) + 1
  return ORBIT_X[idx], ORBIT_Y[idx]
end

-- The shield and the boomerang share this tail: foe weapons inside the guard
-- radius are destroyed (each costing the guard its damage in hitpoints), foes
-- inside the body radius are attacked, and the guard expires on hitpoints or
-- lifetime. Kept as one local helper because the two C++ bodies are literally
-- identical from here down apart from the weapon-scan range.
local function guard_tail(self, weapon_range)
  local weapons = og.find_foe_weapons_in_range("ob", weapon_range, self)
  for i = 1, #weapons do
    local weapon = weapons[i]
    -- hp is a C++ float: per-op rounding.
    self.hp = og.fsub(self.hp, weapon:damage())
    weapon.dead = 1
    weapon:death()
  end

  local foes = og.find_foes_in_range("ob", self:sizex(), self)
  for i = 1, #foes do
    local foe = foes[i]
    -- hp is a C++ float: per-op rounding.
    self.hp = og.fsub(self.hp, foe:damage())
    -- attack() looks pure but draws from the gameplay stream.
    self:attack(foe)
    self.dead = 0
  end

  local expired = false
  if self.hp > 0 then
    local lifetime = self:lifetime()
    self.lifetime = lifetime - 1
    expired = lifetime < 0
  end
  if self.hp <= 0 or expired then
    self.dead = 1
    self:death()
  end
end

-- magic_shield_on_act: a fixed-radius shield that orbits its owner.
local function magic_shield_on_act(self)
  local owner = self:owner()
  if not owner or owner:dead() ~= 0 then
    self.dead = 1
    self:death()
    return true
  end
  local xd, yd = orbit_offset(self:drawcycle())
  self:center_on(owner)
  -- worldx/worldy are C++ floats: per-op rounding.
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))

  guard_tail(self, self:sizex())
  return true
end

-- boomerang_on_act: same guard, but the orbit radius grows with drawcycle
-- ((drawcycle+4)/48 of the base circle) and the blade dies at drawcycle 254.
local function boomerang_on_act(self)
  local owner = self:owner()
  -- Zardus's 2002 fix: the >253 guard prevents the byte-sized drawcycle
  -- wrapping a long-lived boomerang back onto its owner. It deliberately
  -- caps the ability instead of widening that legacy counter.
  if not owner
      or owner:dead() ~= 0
      or self:drawcycle() > 253 then
    self.dead = 1
    self:death()
    return true
  end
  local xd, yd = orbit_offset(self:drawcycle())
  local orbit_scale = self:drawcycle() + 4
  -- The (drawcycle+4)/48 scale is a C++ float chain: per-op rounding.
  xd = og.fmul(xd, orbit_scale)
  xd = og.fdiv(xd, 48)
  yd = og.fmul(yd, orbit_scale)
  yd = og.fdiv(yd, 48)
  self:center_on(owner)  -- each arc starts at the owner; offsets do not accumulate
  -- worldx/worldy are C++ floats: per-op rounding.
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))

  guard_tail(self, self:sizex() * 2)
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:magic_shield  on_act = shield.magic_shield_on_act
--   core:boomerang     on_act = shield.boomerang_on_act
return {
  magic_shield_on_act = magic_shield_on_act,
  boomerang_on_act = boomerang_on_act,
}
