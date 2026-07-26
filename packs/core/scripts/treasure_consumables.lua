-- core treasure consumables — on_eat hooks transliterated from
-- src/gameplay/families/treasure_family_consumables.cpp (the drumstick plus
-- the five potions). Cookbook (docs/lua-classpacks-design.md §3) applies:
-- hitpoints/magicpoints are C++ floats so every sum is one og.fadd, the
-- short casts ride the field setters, and the drumstick's single rand draw
-- keeps its exact position in the stream.

local C = og.C

-- notify_potion_consume(): the shared tail of every potion (C++
-- notify_potion_consume). The message is emitted only for a
-- player-controlled eater (user() != -1); the potion always dies.
local function notify_potion_consume(self, eater, name)
  if eater:user() ~= -1 then
    og.emit_notification(string.format("Potion of %s(%d)!", name,
                                       self:s_level()))
  end
  self:set_dead(1)
end

-- drumstick_on_eat: heals a wounded eater by 10*level + rand(10*level).
local function drumstick_on_eat(self, eater)
  if eater:s_hitpoints() >= eater:s_max_hitpoints() then
    return true
  end
  -- C++: 10*level + rng_.next((uint32)(10*level)). rng_.next(0) yields 0
  -- WITHOUT advancing the stream while og.rand(0) raises, so the bound is
  -- guarded to keep the draw count identical at level 0.
  local base = 10 * self:s_level()
  local roll = 0
  if base > 0 then
    roll = og.rand(base)
  end
  local amount = og.i16(base + roll)  -- C++ `const short amount`
  eater:s_set_hitpoints(og.fadd(eater:s_hitpoints(), amount))
  if eater:s_hitpoints() > eater:s_max_hitpoints() then
    eater:s_set_hitpoints(eater:s_max_hitpoints())
  end
  self:do_heal_effects(nil, eater, amount)
  self:set_dead(1)
  og.emit_sound(C.SOUND_EAT)
  return true
end

-- magic_potion_on_eat: tops the pool off, then overfills it by 50*level.
local function magic_potion_on_eat(self, eater)
  if eater:s_magicpoints() < eater:s_max_magicpoints() then
    eater:s_set_magicpoints(eater:s_max_magicpoints())
  end
  eater:s_set_magicpoints(og.fadd(eater:s_magicpoints(),
                                  50 * self:s_level()))
  notify_potion_consume(self, eater, "Mana")
  return true
end

-- flight_potion_on_eat: no effect (and no consumption) on a natural flier.
local function flight_potion_on_eat(self, eater)
  if not eater:s_query_bit_flags(C.BIT_FLYING) then
    eater:set_flight_left(eater:flight_left() + 150 * self:s_level())
    notify_potion_consume(self, eater, "Flight")
  end
  return true
end

-- invulnerable_potion_on_eat: likewise inert for the already-invincible.
local function invulnerable_potion_on_eat(self, eater)
  if not eater:s_query_bit_flags(C.BIT_INVINCIBLE) then
    eater:set_invulnerable_left(eater:invulnerable_left()
                                + 150 * self:s_level())
    notify_potion_consume(self, eater, "Invulnerability")
  end
  return true
end

local function invis_potion_on_eat(self, eater)
  eater:set_invisibility_left(eater:invisibility_left()
                              + 150 * self:s_level())
  notify_potion_consume(self, eater, "Invisibility")
  return true
end

local function speed_potion_on_eat(self, eater)
  eater:set_speed_bonus_left(eater:speed_bonus_left() + 50 * self:s_level())
  eater:set_speed_bonus(self:s_level())
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
