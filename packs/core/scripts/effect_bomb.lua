-- core:bomb + core:explosion — the thief's bomb and its blast (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))

-- effect.cpp: compute_explosion_range(int32 level, short skip_exit)
local function compute_explosion_range(level, skip_exit)
  -- shim kept: narrows to int32 like the C++ destination.
  local range = og.i32(level * 4)
  if skip_exit > 0 then
    -- The original called this "magical, ie mage": magical blasts collapse
    -- to the 16-pixel minimum instead of keeping the caster's level range.
    range = 0
  end
  range = og.clamp(range, 16, 96)
  return range
end

-- bomb_on_death: the bomb itself carries no blast; dying it spawns the
-- EXPLOSION effect centred on itself, inheriting owner/level/damage.
local function bomb_on_death(self)
  if not self:owner() or self:owner():dead() ~= 0 then
    self:set_owner(self)
  end
  og.emit_sound(C.SOUND_EXPLODE)
  local blast = og.add_ob("fx", FX_EXPLOSION)
  if not blast then
    return true
  end
  blast:set_owner(self:owner())
  blast.hp = 0
  blast.level = self:owner().level
  blast.ani_type = C.ANI_EXPLODE
  blast:set_floor(self:floor())  -- detonate on the bomb's floor
  blast:center_on(self)
  blast.damage = self:damage()
  return true
end

-- explosion_on_death: the blast proper. Everything in range on our floor is
-- shoved away from the centre and struck — the owner at quarter damage,
-- allies at half, everyone else in full.
local function explosion_on_death(self)
  if not self:owner() or self:owner():dead() ~= 0 then
    self:set_owner(self)
  end
  local range = compute_explosion_range(self:owner().level,
                                        self:skip_exit())
  local nearby, nearby_count = og.find_in_range("ob", 15 + range, self)

  -- DamageTile goes out as an event (the sim never touches the screen).
  og.emit_event(C.EVENT_DAMAGE_TILE,
                self:xpos() + self:sizex() // 2,
                self:ypos() + self:sizey() // 2)
  if nearby_count < 1 then
    return false
  end

  for i = 1, #nearby do
    local w = nearby[i]
    -- An explosion never blasts through solid floors: only same-floor
    -- walkers take damage. find_in_range is deliberately floor-blind,
    -- so the damage loop filters instead.
    if w and w:dead() == 0
        and w:floor() == self:floor()
        and w:order() ~= C.ORDER_TREASURE
        and w:order() ~= C.ORDER_FX
        and (self:skip_exit() == 0
             or w ~= self:owner()) then
      local dx = og.sign(w:xpos() - self:xpos())
      local dy = og.sign(w:ypos() - self:ypos())
      local shove = og.min(2 + self:owner().level // 15, 8)
      w:s_force_command(C.COMMAND_WALK, shove, dx, dy)
      if w == self:owner() then
        local full_damage = self:damage()
        -- shim kept: damage is a C++ float: the quarter cut rounds through float.
        self.damage = og.fdiv(full_damage, 4.0)
        self:attack(w)
        self.damage = full_damage
      elseif self:owner():dead() == 0 and self:owner():is_friendly(w) then
        local full_damage = self:damage()
        -- shim kept: damage is a C++ float: the half cut rounds through float.
        self.damage = og.fdiv(full_damage, 2.0)
        self:attack(w)
        self.damage = full_damage
      else
        self:attack(w)
      end
    end
  end
  return true
end

og.register_hooks("fx", "core:bomb", {
  on_death = bomb_on_death,
})

og.register_hooks("fx", "core:explosion", {
  on_death = explosion_on_death,
})
