-- core drumstick + five potions — on_eat hooks (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

-- Shared potion tail: the message is emitted only for a player-controlled
-- eater (user() != -1), into that seat's view alone; the potion always dies.
local function notify_potion_consume(self, eater, name)
  -- rule: A potion's "Potion of <X>(<level>)!" line goes only to a seated eater's own view; a bot
  --   eats silently; the potion is consumed either way.
  -- tests: parity:treasure_magic_potion_pickup_scen99, parity:treasure_invis_potion_pickup_scen99
  -- tests: TreasureEat.magic_potion
  -- tests: SemTreasureConsumables.a_bot_drinks_a_potion_silently_a_seated_eater_is_told
  if eater:user() ~= -1 then
    og.emit_notification(string.format("Potion of %s(%d)!", name, self.level), 0, eater)
  end
  self:set_dead(1)
end

-- drumstick_on_eat: heals a wounded eater by base + rand(base), where
-- base = heal_per_level * level.
local function drumstick_on_eat(self, eater)
  -- rule: A drumstick is left untouched by an eater at full health (no heal, no sound, stays
  --   alive).
  -- tests: parity:treasure_drumstick_fullhp_noconsume_scen99, TreasureEat.drumstick_full_hp
  -- tests: TreasureEat.drumstick, parity:treasure_drumstick_pickup_scen99
  if eater.hp >= eater.max_hp then
    return true
  end
  -- one draw, og.rand0: the C++ rng returns 0 at level 0 without advancing
  local base = og.tuning(self).heal_per_level * self.level
  local roll = og.rand0(base)
  local amount = og.i16(base + roll)  -- C++ `const short amount`
  -- hp is a C++ float: per-op rounding
  eater.hp = og.fadd(eater.hp, amount)
  -- rule: A drumstick heals no higher than max hp.
  -- tests: TreasureEat.drumstick, SemTreasureConsumables.a_drumstick_heals_no_higher_than_max_hp
  if eater.hp > eater.max_hp then
    eater.hp = eater.max_hp
  end
  self:do_heal_effects(nil, eater, amount)
  self:set_dead(1)
  og.emit_positional_sound(self, C.SOUND_EAT)
  return true
end

-- magic_potion_on_eat: tops the pool off, then overfills it per level.
local function magic_potion_on_eat(self, eater)
  -- rule: A mana potion first tops the pool up to max (never lowers an overfilled pool), then
  --   overfills it by mana_overfill_per_level * level.
  -- tests: SemTreasureConsumables.a_mana_potion_tops_up_then_overfills_and_never_lowers_a_pool
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
  -- rule: A natural flyer gains nothing from a flight potion and leaves it on the ground; anyone
  --   else gains duration_per_level*level flight ticks and consumes it.
  -- tests: parity:treasure_flight_potion_flier_noconsume_scen99
  -- tests: PackLuaTreasure.a_potion_that_cannot_help_its_eater_is_left_untouched
  -- tests: TreasureEat.flight_potion
  if not eater:s_query_bit_flags(C.BIT_FLYING) then
    eater:set_flight_left(eater:flight_left()
                          + og.tuning(self).duration_per_level * self.level)
    notify_potion_consume(self, eater, "Flight")
  end
  return true
end

-- invulnerable_potion_on_eat: likewise inert for the already-invincible.
local function invulnerable_potion_on_eat(self, eater)
  -- rule: An already-invincible walker gains nothing from an invulnerability potion and leaves it;
  --   anyone else gains duration_per_level*level ticks and consumes it.
  -- tests: PackLuaTreasure.a_potion_that_cannot_help_its_eater_is_left_untouched
  -- tests: TreasureEat.invulnerable_potion
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

-- The declarations that reference this module (packs/core/families/):
--   core:drumstick            on_eat = consumables.drumstick_on_eat
--   core:magic_potion         on_eat = consumables.magic_potion_on_eat
--   core:flight_potion        on_eat = consumables.flight_potion_on_eat
--   core:invulnerable_potion  on_eat = consumables.invulnerable_potion_on_eat
--   core:invis_potion         on_eat = consumables.invis_potion_on_eat
--   core:speed_potion         on_eat = consumables.speed_potion_on_eat
return {
  drumstick_on_eat = drumstick_on_eat,
  magic_potion_on_eat = magic_potion_on_eat,
  flight_potion_on_eat = flight_potion_on_eat,
  invulnerable_potion_on_eat = invulnerable_potion_on_eat,
  invis_potion_on_eat = invis_potion_on_eat,
  speed_potion_on_eat = speed_potion_on_eat,
}
