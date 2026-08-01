-- core:soldier — charge, boomerang, whirlwind, disarm (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local ai = og.use("ai")
local FX_BOOMERANG = assert(og.family_id("fx", "core:boomerang"))

local function charge(self)
  if self:s_forward_blocked() then
    return false
  end
  -- lastx/lasty are C++ floats: one fdiv then one C trunc, per axis
  self:s_add_command(C.COMMAND_RUSH, 3,
    og.trunc(og.fdiv(self:lastx(), self:stepsize())),
    og.trunc(og.fdiv(self:lasty(), self:stepsize())))
  og.emit_sound(C.SOUND_CHARGE)
  return true
end

local function throw_boomerang(self)
  local t = og.tuning(self)
  -- og.summon_configured applies these keys in exactly the legacy order:
  -- ani_type, lifetime, hp_add, max_hp_from_hp, damage_add
  local boomerang = og.summon_configured(self, "fx", FX_BOOMERANG, {
    -- The old code used ani_type = 1 only as a dummy non-zero value.
    ani_type = 1,
    lifetime = t.boomerang_lifetime_base
      + self.level * t.boomerang_lifetime_per_level,
    hp_add = self.level * t.boomerang_hp_per_level,
    max_hp_from_hp = true,
    damage_add = self.level * t.boomerang_damage_per_level,
  })
  if not boomerang then
    return false
  end
  return true
end

local function whirlwind(self)
  -- can't do while attacking, etc.
  if self:busy() ~= 0 then
    return false
  end
  -- busy is a C++ float: per-op rounding
  self:set_busy(og.fadd(self:busy(), 8.0))
  self:set_curdir(-1)
  self:set_lastx(0)
  self:set_lasty(0)
  self:s_add_command(C.COMMAND_WALK, 1, 0, -1)
  self:s_add_command(C.COMMAND_WALK, 1, 1, -1)
  self:s_add_command(C.COMMAND_WALK, 1, 1, 0)
  self:s_add_command(C.COMMAND_WALK, 1, 1, 1)
  self:s_add_command(C.COMMAND_WALK, 1, 0, 1)
  self:s_add_command(C.COMMAND_WALK, 1, -1, 1)
  self:s_add_command(C.COMMAND_WALK, 1, -1, 0)
  self:s_add_command(C.COMMAND_WALK, 1, -1, -1)

  local t = og.tuning(self)
  local foes = og.foes_in_range(
    self, t.whirlwind_range_base + self.level * t.whirlwind_range_per_level)
  for i = 1, #foes do
    local foe = foes[i]
    local dx = og.sign(foe:xpos() - self:xpos())
    local dy = og.sign(foe:ypos() - self:ypos())
    -- attack() draws from the RNG; the shove must stay after it
    self:attack(foe)
    foe:s_force_command(C.COMMAND_WALK, 8, dx, dy)
  end
  return true
end

local function disarm(self)
  if self:busy() ~= 0 then
    return false
  end
  -- can't do this if no frontal enemy
  if not self:s_forward_blocked() then
    return false
  end

  local found = 0
  local foes = og.foes_in_range(self, og.tuning(self).disarm_range)
  for i = 1, #foes do
    local foe = foes[i]
    -- two draws in one comparison: parity adjudicated LEFT-first here
    -- (as at the thief and orc sites). og.rand, not rand0: living levels
    -- are >= 1, so n <= 0 would be a real bug worth the loud error.
    if og.rand(self.level) >= og.rand(foe.level) then
      -- busy is a C++ float: per-op rounding
      foe:set_busy(og.fadd(foe:busy(), 6 * (self.level - foe.level + 1)))
    end
    found = 1
  end

  if found == 0 then
    return false
  end
  og.emit_sound(C.SOUND_CHARGE)
  if self.team == 0 or self:has_guy() then
    og.emit_notification("Fighter Disarmed Enemy!")
  end
  -- busy is a C++ float: per-op rounding
  self:set_busy(og.fadd(self:busy(), 5.0))
  return true
end

local function on_fire_weapon(self, weapon)
  if self:order() ~= C.ORDER_LIVING then
    return true
  end
  -- Jonathan Dearborn's 2013 fix keeps melee available while the returning
  -- blade is away; only a ranged release consumes weapons_left.
  if self:weapons_left() <= 0 then
    -- Give back the magic it cost, since we didn't throw it.
    -- magicpoints is a C++ float: per-op rounding
    self.magicpoints = og.fadd(self.magicpoints, self:s_weapon_cost())
    weapon:set_dead(1)
    return false
  end
  self:set_weapons_left(self:weapons_left() - 1)
  return true
end

local function on_create(self)
  if self:order() == C.ORDER_LIVING then
    self:set_weapons_left((self.level + 1) // 2)
  end
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 13.0, 8.0, 5.0, 2.0)
  -- level is a hook argument with unproven range: og.div keeps C trunc
  self:set_weapons_left(og.div(level + 1, 2))
end

og.register_hooks("living", "core:soldier", {
  specials = {
    [1] = charge,
    [2] = throw_boomerang,
    [3] = whirlwind,
    [4] = disarm,
  },
  -- Historical charge window: about one to three grid squares.
  check_special_ai = ai.foe_in_window(20, 75),  -- fixed per-tick AI gate
  on_fire_weapon = on_fire_weapon,
  on_create = on_create,
  set_difficulty = set_difficulty,
})
