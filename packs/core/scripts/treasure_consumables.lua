-- core drumstick + five potions — on_eat hooks (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C

-- Shared potion tail: the message is emitted only for a player-controlled
-- eater (user() != -1); the potion always dies.
local function notify_potion_consume(self, eater, name)
  if eater:user() ~= -1 then
    og.emit_notification(string.format("Potion of %s(%d)!", name, self.level))
  end
  self:set_dead(1)
end

-- drumstick_on_eat: heals a wounded eater by base + rand(base), where
-- base = heal_per_level * level.
local function drumstick_on_eat(self, eater)
  if eater.hp >= eater.max_hp then
    return true
  end
  -- one draw, og.rand0: the C++ rng returns 0 at level 0 without advancing
  local base = og.tuning(self).heal_per_level * self.level
  local roll = og.rand0(base)
  local amount = og.i16(base + roll)  -- C++ `const short amount`
  -- hp is a C++ float: per-op rounding
  eater.hp = og.fadd(eater.hp, amount)
  if eater.hp > eater.max_hp then
    eater.hp = eater.max_hp
  end
  self:do_heal_effects(nil, eater, amount)
  self:set_dead(1)
  og.emit_sound(C.SOUND_EAT)
  return true
end

-- magic_potion_on_eat: tops the pool off, then overfills it per level.
local function magic_potion_on_eat(self, eater)
  if eater.magicpoints < eater.max_magicpoints then
    eater.magicpoints = eater.max_magicpoints
  end
  -- magicpoints is a C++ float: per-op rounding
  eater.magicpoints = og.fadd(
    eater.magicpoints, og.tuning(self).mana_overfill_per_level * self.level)
  notify_potion_consume(self, eater, "Mana")
  return true
end

-- flight_potion_on_eat: no effect (and no consumption) on a natural flier.
local function flight_potion_on_eat(self, eater)
  if not eater:s_query_bit_flags(C.BIT_FLYING) then
    eater:set_flight_left(eater:flight_left()
                          + og.tuning(self).duration_per_level * self.level)
    notify_potion_consume(self, eater, "Flight")
  end
  return true
end

-- invulnerable_potion_on_eat: likewise inert for the already-invincible.
local function invulnerable_potion_on_eat(self, eater)
  if not eater:s_query_bit_flags(C.BIT_INVINCIBLE) then
    eater:set_invulnerable_left(
      eater:invulnerable_left()
      + og.tuning(self).duration_per_level * self.level)
    notify_potion_consume(self, eater, "Invulnerability")
  end
  return true
end

local function invis_potion_on_eat(self, eater)
  eater:set_invisibility_left(
    eater:invisibility_left()
    + og.tuning(self).duration_per_level * self.level)
  notify_potion_consume(self, eater, "Invisibility")
  return true
end

local function speed_potion_on_eat(self, eater)
  eater:set_speed_bonus_left(
    eater:speed_bonus_left()
    + og.tuning(self).duration_per_level * self.level)
  eater:set_speed_bonus(self.level)
  notify_potion_consume(self, eater, "Speed")
  return true
end

og.register_hooks("treasure", "core:drumstick", {
  on_eat = drumstick_on_eat,
})

og.register_hooks("treasure", "core:magic_potion", {
  on_eat = magic_potion_on_eat,
})

og.register_hooks("treasure", "core:flight_potion", {
  on_eat = flight_potion_on_eat,
})

og.register_hooks("treasure", "core:invulnerable_potion", {
  on_eat = invulnerable_potion_on_eat,
})

og.register_hooks("treasure", "core:invis_potion", {
  on_eat = invis_potion_on_eat,
})

og.register_hooks("treasure", "core:speed_potion", {
  on_eat = speed_potion_on_eat,
})
