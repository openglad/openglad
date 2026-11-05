-- core:magic_shield + core:boomerang — orbiting guard effects (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The ORBIT_X/ORBIT_Y tables are pure constants (R6-clean: no sim state).

local C = og.C

-- Ported from the former effect.cpp orbit_offset(drawcycle, &xd, &yd); this is the only copy now.
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

-- The shield and the boomerang share this tail: foe shots inside the guard
-- radius are destroyed, each costing the guard its damage in hitpoints (solid
-- scenery, a bone wall or war banner, is passed by), foes inside the body radius
-- are attacked, and the guard expires on hitpoints or lifetime. One local helper:
-- the two C++ bodies were identical from here down but for the weapon-scan range.
local function guard_tail(self, weapon_range)
  local weapons = og.find_foe_weapons_in_range("weap", weapon_range, self)
  -- plumbing: iterates foe weapons in range; rule below
  for i = 1, #weapons do
    local weapon = weapons[i]
    -- rule: A guard destroys each foe shot in its radius (shield: its size, boomerang: twice) for its damage in hp; solid scenery is passed by.
    -- tests: EffectMorePaths.effect_magic_shield_absorbs_hostile_weapon_and_strikes_foe, parity:shield_projectile_absorb_scen99
    -- tests: EffectMorePaths.effect_boomerang_orbit_absorbs_hostile_weapon_and_strikes_foe, EffectAct.magic_shield_orbit_step_and_ageing
    -- tests: SemShield.a_guard_passes_a_foes_bone_wall_and_war_banner_by
    if not weapon:blocks_placement() then
      self.hp = og.fsub(self.hp, weapon:damage())  -- hp is a C++ float: per-op rounding.
      weapon.dead = 1
      weapon:death()
    end
  end

  local foes = og.find_foes_in_range("ob", self:sizex(), self)
  -- rule: A guard strikes every foe inside its body radius each tick, paying the foe's own damage
  --   in hp per foe, and survives its own strike (dead reset to 0 after each attack).
  -- tests: EffectMorePaths.effect_magic_shield_absorbs_hostile_weapon_and_strikes_foe
  -- tests: EffectMorePaths.effect_batch3_guard_dies_when_collisions_drain_its_hitpoints
  -- tests: EffectAct.magic_shield_orbit_step_and_ageing
  for i = 1, #foes do
    local foe = foes[i]
    -- hp is a C++ float: per-op rounding.
    self.hp = og.fsub(self.hp, foe:damage())
    -- attack() looks pure but draws from the gameplay stream.
    self:attack(foe)
    self.dead = 0
  end

  local expired = false
  -- rule: A guard dies when its hp is spent or its lifetime has run out; it ages only while it
  --   still has hp, and it expires on the act AFTER its count passes 0 (lifetime read before the
  --   decrement < 0), i.e. it lives lifetime+1 acts.
  -- tests: EffectAct.magic_shield_orbit_step_and_ageing
  -- tests: EffectMorePaths.effect_batch3_guard_dies_when_collisions_drain_its_hitpoints
  -- tests: SemShield.classic_a_guard_expires_on_the_act_after_its_count_passes_zero
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
  -- rule: A magic shield dies with its caster; while the caster lives it orbits him on the 16-step
  --   radius-24 circle, one step per drawcycle.
  -- tests: EffectAct.magic_shield_no_owner, EffectAct.magic_shield_orbit_step_and_ageing
  -- tests: PackLuaShield.the_orbit_is_the_sixteen_step_circle_from_due_north
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
  -- rule: A boomerang dies with its thrower and at drawcycle 254 (Zardus's 2002 cap so the byte
  --   counter cannot wrap it back onto its owner); until then it spirals out at (drawcycle+4)/48 of
  --   the 24-px circle.
  -- tests: EffectAct.boomerang_expired, EffectAct.boomerang_orbit_radius_scales_with_drawcycle
  -- tests: EffectAct.boomerang_spirals_outward_as_the_sim_ticks
  -- tests: SemShield.classic_a_boomerang_dies_with_its_thrower
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
