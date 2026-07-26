-- core:bomb and core:explosion — the thief's bomb and the blast it leaves,
-- transliterated from src/gameplay/families/effect_family_bomb.cpp.
--
-- Cookbook (docs/lua-classpacks-design.md §3): damage is a C++ float so the
-- friendly/self-damage reductions go through og.fdiv; every integer `/`
-- (blast range, knockback sign, shove length, tile centre) uses og.div.

local C = og.C
local FX_EXPLOSION = og.family_id("fx", "core:explosion")

-- effect.cpp: compute_explosion_range(int32 level, short skip_exit)
local function compute_explosion_range(level, skip_exit)
  local range = og.i32(level * 4)
  if skip_exit > 0 then range = 0 end
  if range > 96 then range = 96 end
  if range < 16 then range = 16 end
  return range
end

-- bomb_on_death: the bomb itself carries no blast; dying it spawns the
-- EXPLOSION effect centred on itself, inheriting owner/level/damage.
local function bomb_on_death(self)
  if not self:owner() or self:owner():dead() ~= 0 then
    self:set_owner(self)
  end
  og.emit_sound(C.SOUND_EXPLODE)
  local newob = og.add_ob("fx", FX_EXPLOSION)
  if not newob then return true end
  newob:set_owner(self:owner())
  newob:s_set_hitpoints(0)
  newob:s_set_level(self:owner():s_level())
  newob:set_ani_type(C.ANI_EXPLODE)
  newob:set_floor(self:floor())  -- detonate on the bomb's floor (A8)
  newob:center_on(self)
  newob:set_damage(self:damage())
  return true
end

-- explosion_on_death: the blast proper. Everything in range on our floor is
-- shoved away from the centre and struck — the owner at quarter damage,
-- allies at half, everyone else in full.
local function explosion_on_death(self)
  if not self:owner() or self:owner():dead() ~= 0 then
    self:set_owner(self)
  end
  local generic = compute_explosion_range(self:owner():s_level(),
                                          self:skip_exit())
  local foelist, howmany = og.find_in_range("ob", 15 + generic, self)

  -- Emit DamageTile event instead of calling screen directly
  og.emit_event(C.EVENT_DAMAGE_TILE,
                self:xpos() + og.div(self:sizex(), 2),
                self:ypos() + og.div(self:sizey(), 2))
  if howmany < 1 then
    return false
  end

  for i = 1, #foelist do
    local w = foelist[i]
    -- An explosion never blasts through solid floors: only same-floor
    -- walkers take damage (A8). find_in_range is deliberately floor-blind,
    -- so the damage loop filters instead.
    if w and w:dead() == 0
        and w:floor() == self:floor()
        and w:order() ~= C.ORDER_TREASURE
        and w:order() ~= C.ORDER_FX
        and (self:skip_exit() == 0 or w ~= self:owner()) then
      local xdelta = w:xpos() - self:xpos()
      if xdelta ~= 0 then
        xdelta = og.div(xdelta, math.abs(xdelta))
      end
      local ydelta = w:ypos() - self:ypos()
      if ydelta ~= 0 then
        ydelta = og.div(ydelta, math.abs(ydelta))
      end
      generic = 2 + og.div(self:owner():s_level(), 15)
      if generic > 8 then generic = 8 end
      w:s_force_command(C.COMMAND_WALK, generic, xdelta, ydelta)
      if w == self:owner() then
        local damage = self:damage()
        self:set_damage(og.fdiv(damage, 4.0))
        self:attack(w)
        self:set_damage(damage)
      elseif self:owner():dead() == 0 and self:owner():is_friendly(w) then
        local damage = self:damage()
        self:set_damage(og.fdiv(damage, 2.0))
        self:attack(w)
        self:set_damage(damage)
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
