-- core:soldier — behavior hooks transliterated from family_soldier.cpp.
-- Cookbook (docs/lua-classpacks-design.md §3) applies: og.div/og.mod for
-- integer /%, og.f* for float ops, setters narrow like the C++ field types,
-- og.rand preserves RNG call order.

local C = og.C
local FX_BOOMERANG = og.family_id("fx", "core:boomerang")

local function do_special(self)
  local sp = self:current_special()
  if sp == 1 then
    -- charge enemy
    if not self:s_forward_blocked() then
      self:s_add_command(C.COMMAND_RUSH, 3,
        og.trunc(og.fdiv(self:lastx(), self:stepsize())),
        og.trunc(og.fdiv(self:lasty(), self:stepsize())))
      og.emit_sound(C.SOUND_CHARGE)
    else
      return false
    end
  elseif sp == 2 then
    -- boomerang
    local newob = og.summon(self, "fx", FX_BOOMERANG)
    if not newob then return false end
    newob:set_ani_type(1)
    newob:set_lifetime(30 + self:s_level() * 12)
    newob:s_set_hitpoints(og.fadd(newob:s_hitpoints(),
                                  og.fmul(self:s_level(), 12.0)))
    newob:s_set_max_hitpoints(newob:s_hitpoints())
    newob:set_damage(og.fadd(newob:damage(), og.fmul(self:s_level(), 4.0)))
  elseif sp == 3 then
    -- whirlwind attack
    if self:busy() ~= 0 then return false end
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

    local foes = og.foes_in_range(self, 32 + self:s_level() * 2)
    for i = 1, #foes do
      local w = foes[i]
      local tempx = w:xpos() - self:xpos()
      if tempx ~= 0 then
        tempx = og.div(tempx, math.abs(tempx))
      end
      local tempy = w:ypos() - self:ypos()
      if tempy ~= 0 then
        tempy = og.div(tempy, math.abs(tempy))
      end
      self:attack(w)
      w:s_force_command(C.COMMAND_WALK, 8, tempx, tempy)
    end
  elseif sp == 4 then
    -- disarm opponent
    if self:busy() ~= 0 then return false end
    if not self:s_forward_blocked() then return false end

    local generic = 0
    local foes = og.foes_in_range(self, 28)
    for i = 1, #foes do
      local w = foes[i]
      if og.rand(self:s_level()) >= og.rand(w:s_level()) then
        w:set_busy(og.fadd(w:busy(),
                           og.fmul(6.0, self:s_level() - w:s_level() + 1)))
      end
      generic = 1
    end

    if generic ~= 0 then
      og.emit_sound(C.SOUND_CHARGE)
      if self:team_num() == 0 or self:has_guy() then
        og.emit_notification("Fighter Disarmed Enemy!")
      end
      self:set_busy(og.fadd(self:busy(), 5.0))
    else
      return false
    end
  end
  return true
end

local function check_special_ai(self)
  local foe = self:foe()
  if foe then
    local distance = self:distance_to_ob(foe)
    return distance < 75 and distance > 20
  end
  self:set_foe(og.find_near_foe(self))
  foe = self:foe()
  if not foe then return false end
  local distance = self:distance_to_ob(foe)
  return distance < 75 and distance > 20
end

local function on_fire_weapon(self, weapon)
  if self:order() ~= C.ORDER_LIVING then return true end
  if self:weapons_left() <= 0 then
    self:s_set_magicpoints(og.fadd(self:s_magicpoints(),
                                   self:s_weapon_cost()))
    weapon:set_dead(1)
    return false
  end
  self:set_weapons_left(self:weapons_left() - 1)
  return true
end

local function on_create(self)
  if self:order() == C.ORDER_LIVING then
    self:set_weapons_left(og.div(self:s_level() + 1, 2))
  end
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 13.0, 8.0, 5.0, 2.0)
  self:set_weapons_left(og.div(level + 1, 2))
end

og.register_hooks("living", "core:soldier", {
  do_special = do_special,
  check_special_ai = check_special_ai,
  on_fire_weapon = on_fire_weapon,
  on_create = on_create,
  set_difficulty = set_difficulty,
})
