-- core:magic_shield and core:boomerang — orbiting-guard effects
-- transliterated from src/gameplay/families/effect_family_shield.cpp.
--
-- Cookbook (docs/lua-classpacks-design.md §3): worldx/worldy/damage/
-- hitpoints/stepsize are C++ floats, so every coordinate and hitpoint sum is
-- one og.f* call; the orbit table below is the constant `orbit_table` from
-- effect.cpp's orbit_offset() (constants in scripts are allowed by R6 — it
-- carries no sim state).

local C = og.C

-- effect.cpp: orbit_offset(int drawcycle, float& xd, float& yd)
-- 16-step circle of integer offsets (all exactly representable in float).
local ORBIT_X = { 0, -9, -17, -22, -24, -22, -17, -9,
                  0,   9,  17,  22,  24,  22,  17,  9 }
local ORBIT_Y = { -24, -22, -17, -9, 0, 9, 17, 22,
                   24,  22,  17,  9, 0, -9, -17, -22 }

-- Lua arrays are 1-based; the C++ index is drawcycle % 16.
local function orbit_offset(drawcycle)
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
    local w = weapons[i]
    self:s_set_hitpoints(og.fsub(self:s_hitpoints(), w:damage()))
    w:set_dead(1)
    w:death()
  end

  local foes = og.find_foes_in_range("ob", self:sizex(), self)
  for i = 1, #foes do
    local w = foes[i]
    self:s_set_hitpoints(og.fsub(self:s_hitpoints(), w:damage()))
    self:attack(w)
    self:set_dead(0)
  end

  local expired = false
  if self:s_hitpoints() > 0 then
    local lifetime = self:lifetime()
    self:set_lifetime(lifetime - 1)
    expired = lifetime < 0
  end
  if self:s_hitpoints() <= 0 or expired then
    self:set_dead(1)
    self:death()
  end
end

-- magic_shield_on_act: a fixed-radius shield that orbits its owner.
local function magic_shield_on_act(self)
  local owner = self:owner()
  if not owner or owner:dead() ~= 0 then
    self:set_dead(1)
    self:death()
    return true
  end
  local xd, yd = orbit_offset(self:drawcycle())
  self:center_on(owner)
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))

  guard_tail(self, self:sizex())
  return true
end

-- boomerang_on_act: same guard, but the orbit radius grows with drawcycle
-- ((drawcycle+4)/48 of the base circle) and the blade dies at drawcycle 254.
local function boomerang_on_act(self)
  local owner = self:owner()
  if not owner
      or owner:dead() ~= 0
      or self:drawcycle() > 253 then
    self:set_dead(1)
    self:death()
    return true
  end
  local xd, yd = orbit_offset(self:drawcycle())
  local orbit_scale = self:drawcycle() + 4
  xd = og.fmul(xd, orbit_scale)
  xd = og.fdiv(xd, 48)
  yd = og.fmul(yd, orbit_scale)
  yd = og.fdiv(yd, 48)
  self:center_on(owner)
  self:setworldxy(og.fadd(self:worldx(), xd), og.fadd(self:worldy(), yd))

  guard_tail(self, self:sizex() * 2)
  return true
end

og.register_hooks("fx", "core:magic_shield", {
  on_act = magic_shield_on_act,
})

og.register_hooks("fx", "core:boomerang", {
  on_act = boomerang_on_act,
})
