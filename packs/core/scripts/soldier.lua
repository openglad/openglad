-- core:soldier — charge, boomerang, whirlwind, disarm (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local ai = og.use("ai")
local FX_BOOMERANG = og.family_id("fx", "core:boomerang")

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
  -- og.summon_configured applies these keys in exactly the legacy order:
  -- ani_type, lifetime, hp_add, max_hp_from_hp, damage_add
  local boomerang = og.summon_configured(self, "fx", FX_BOOMERANG, {
    ani_type = 1,
    lifetime = 30 + self.level * 12,
    hp_add = self.level * 12,
    max_hp_from_hp = true,
    damage_add = self.level * 4,
  })
  if not boomerang then
    return false
  end
  return true
end

local function whirlwind(self)
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

  local foes = og.foes_in_range(self, 32 + self.level * 2)
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
  if not self:s_forward_blocked() then
    return false
  end

  local found = 0
  local foes = og.foes_in_range(self, 28)
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
  if self:weapons_left() <= 0 then
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
  check_special_ai = ai.foe_in_window(20, 75),
  on_fire_weapon = on_fire_weapon,
  on_create = on_create,
  set_difficulty = set_difficulty,
})
