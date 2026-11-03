-- core lib: kit_elemental — the fire elemental's New Specials: IMMOLATE, METEOR RAIN, REKINDLE / SUPERNOVA (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local G = og.use("gates")
local km = og.use("kit_marker")
local foes_touching = og.use("effect_ember").foes_touching
local FX_EMBER = assert(og.family_id("fx", "core:ember"))
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))
local FX_HIT = assert(og.family_id("fx", "core:hit"))

-- While IMMOLATE burns the elemental wears KIT_CHANNEL, and the engine
-- prices EVERY slot at 0 for a walker wearing it (so the quenching press is
-- free once the drain has eaten the mana). The other specials therefore
-- refuse while the fire burns, or they would cast for nothing.
local BURNING = "QUENCH IMMOLATE FIRST"

-- Bot-gate ranges are code constants: the gates run on every AI roll and
-- every hit, and a per-call og.tuning read there costs instructions
-- (lib/ai.lua says why).
local IMMOLATE_GATE_RANGE = 40
local IMMOLATE_GATE_MANA = 60
local RAIN_GATE_RANGE = 160
local RAIN_GATE_CLUSTER = 60
local RAIN_ALLY_ZONE = 71
local NOVA_GATE_RANGE = 48

local M = {}

local function burning(self)
  return (self:kit_state() & C.KIT_CHANNEL) ~= 0
end

-- Ends a burning IMMOLATE's channel on the elemental (no-op otherwise). The
-- dying elemental calls it too, so its parting starburst is never refused.
function M.end_channel(self)
  -- rule: Ending IMMOLATE clears the channel mark on the elemental; it is a no-op when not burning.
  --   The dying elemental ends it so its parting starburst is never refused.
  -- tests: KitElemental.dying_while_immolating_still_fires_the_parting_starburst
  -- tests: KitElemental.second_press_ends_it_free_after_the_latch
  -- tests: SemLibsElemental.ending_immolate_on_an_elemental_that_is_not_burning_changes_no_kit_bit
  if burning(self) then
    self:set_kit_state(self:kit_state() & ~C.KIT_CHANNEL)
  end
end

-- The quench: the channel ends on the owner, then the marker dies.
local function quench(owner, marker)
  M.end_channel(owner)
  km.finish(marker)
end

-- A flame-lick flash on the elemental (the first of the hit rows; no draw).
local function hit_flash(self)
  local flash = og.add_ob("fx", FX_HIT)
  -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
  --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova blast and
  --   ring blast are skipped or refused
  if flash then
    flash:set_owner(self)
    flash.team = self.team
    flash:set_floor(self:floor())
    flash:center_on(self)
    flash.ani_type = C.ANI_ATTACK
  end
end

-- Living foes on the owner's floor whose box touches the owner's box grown
-- by 4 px burn. The one that last struck the elemental, standing in the
-- fire, is a melee attacker and burns double (hit_response cannot do this:
-- it never fires for a player-driven victim).
local function burn_contacts(marker, owner, t)
  local foes = foes_touching(owner, 4)
  local striker = owner:last_attacker_id()
  -- rule: IMMOLATE burns every living foe on the elemental's floor touching its box grown by 4 px
  --   for immolate_contact + level each tick; the foe that last struck it burns double.
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  for i = 1, #foes do
    local foe = foes[i]
    local burn = t.immolate_contact + owner.level
    -- rule: IMMOLATE burns every living foe on the elemental's floor touching its box grown by 4 px
    --   for immolate_contact + level each tick; the foe that last struck it burns double. Here:
    --   `entity_id(foe) == striker`.
    -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
    if og.entity_id(foe) == striker then
      burn = burn * 2
    end
    marker.damage = burn
    marker:attack(foe)
  end
end

-- True when one of the owner's live embers already lies on the spot an
-- ember of its size would drop on now (bottom centre of the owner's box).
local function ember_underfoot(owner)
  local obs = og.oblist()
  -- rule: A burning elemental drops an ember at its feet (bottom centre) every ember_step ticks,
  --   but never a second one on a spot where one of its own still burns: standing still keeps one
  --   ember under it.
  -- tests: KitElemental.standing_burner_keeps_one_ember_underfoot
  -- tests: KitElemental.embers_burn_crossers_then_die
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  -- tests: SemLibsElemental.a_burning_elementals_own_ember_on_another_floor_does_not_count_as_underfoot
  for i = 1, #obs do
    local ob = obs[i]
    -- rule: A burning elemental drops an ember at its feet (bottom centre) every ember_step ticks,
    --   but never a second one on a spot where one of its own still burns.
    -- tests: KitElemental.standing_burner_keeps_one_ember_underfoot
    -- tests: KitElemental.embers_burn_crossers_then_die
    -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
    -- tests: SemLibsElemental.a_burning_elementals_own_ember_on_another_floor_does_not_count_as_underfoot
    if ob:family() == FX_EMBER
        and ob:order() == C.ORDER_FX
        and ob:dead() == 0
        and ob:owner() == owner then
      local x = owner:xpos() + og.div(owner:sizex() - ob:sizex(), 2)
      local y = owner:ypos() + owner:sizey() - ob:sizey()
      -- rule: A burning elemental drops an ember at its feet (bottom centre) every ember_step
      --   ticks, but never a second one on a spot where one of its own still burns.
      -- tests: KitElemental.standing_burner_keeps_one_ember_underfoot
      -- tests: KitElemental.embers_burn_crossers_then_die
      -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
      -- tests: SemLibsElemental.a_burning_elementals_own_ember_on_another_floor_does_not_count_as_underfoot
      local on_spot = ob:xpos() == x and ob:ypos() == y
      -- rule: A burning elemental drops an ember at its feet (bottom centre) every ember_step
      --   ticks, but never a second one on a spot where one of its own still burns.
      -- tests: KitElemental.standing_burner_keeps_one_ember_underfoot
      -- tests: KitElemental.embers_burn_crossers_then_die
      -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
      -- tests: SemLibsElemental.a_burning_elementals_own_ember_on_another_floor_does_not_count_as_underfoot
      if on_spot and ob:floor() == owner:floor() then
        return true
      end
    end
  end
  return false
end

-- A burning footprint at the owner's feet (bottom centre of its box), unless
-- one still burns there: a standing elemental keeps one ember under it
-- instead of piling them up.
local function drop_ember(owner, t)
  -- rule: A burning elemental drops an ember at its feet (bottom centre) every ember_step ticks,
  --   but never a second one on a spot where one of its own still burns.
  -- tests: KitElemental.standing_burner_keeps_one_ember_underfoot
  -- tests: KitElemental.embers_burn_crossers_then_die
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  -- tests: SemLibsElemental.a_burning_elementals_own_ember_on_another_floor_does_not_count_as_underfoot
  if ember_underfoot(owner) then
    return
  end
  local ember = og.add_ob("fx", FX_EMBER)
  -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
  --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova blast and
  --   ring blast are skipped or refused
  if ember then
    ember:set_owner(owner)
    ember.team = owner.team
    ember:set_floor(owner:floor())
    ember.lifetime = t.ember_ticks
    ember.damage = t.ember_damage + og.div(owner.level, 2)
    ember:set_frame(0)
    ember:setxy(owner:xpos() + og.div(owner:sizex() - ember:sizex(), 2),
                owner:ypos() + owner:sizey() - ember:sizey())
  end
end

-- IMMOLATION marker: drains the owner's mana each tick, burns what touches
-- it, drops an ember every ember_step ticks; goes out when the owner dies,
-- the mana runs dry or immolate_max ticks pass. After each drain its clock
-- is cut to what the mana left can pay for (one more tick for every
-- immolate_drain, and the tick that finds the pool dry), so the countdown
-- a player sees is the tick the fire really goes out.
local function immolation_act(marker)
  local owner = marker:owner()
  -- rule: IMMOLATE drains immolate_drain MP a tick; it goes out when the elemental is gone, when
  --   the pool cannot pay the next tick, or after immolate_max ticks; its clock is cut to what the
  --   mana left buys.
  -- tests: KitElemental.immolate_at_low_mana_goes_out
  -- tests: KitElemental.immolation_clock_reads_what_the_mana_buys
  -- tests: KitElemental.immolate_burns_out_after_its_longest_burn
  -- tests: KitElemental.dying_while_immolating_still_fires_the_parting_starburst
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  if not owner then
    km.finish(marker)
    return
  end
  local t = og.tuning(owner)
  if owner.magicpoints < t.immolate_drain then
    quench(owner, marker)
    return
  end
  marker.lifetime = marker:lifetime() - 1
  if marker:lifetime() <= 0 then
    quench(owner, marker)
    return
  end
  -- magicpoints is a C++ float: per-op rounding.
  owner.magicpoints = og.fsub(owner.magicpoints, t.immolate_drain)
  local paid = og.div(og.trunc(owner.magicpoints), t.immolate_drain) + 1
  marker.lifetime = og.min(marker:lifetime(), paid)
  marker:center_on(owner)
  burn_contacts(marker, owner, t)
  if og.mod(marker:lifetime(), t.ember_step) == 0 then
    drop_ember(owner, t)
  end
end

-- One meteor: a fire explosion somewhere in the rain's square, x then y.
-- skip_exit makes it the archmage heartburst's magical blast: the 16 px
-- range, and the caster is never caught in its own rain.
local function strike(marker, owner, t)
  local blast = og.add_ob("fx", FX_EXPLOSION)
  -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
  --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova blast and
  --   ring blast are skipped or refused
  if blast then
    local dx = og.rand(2 * t.rain_spread + 1) - t.rain_spread
    local dy = og.rand(2 * t.rain_spread + 1) - t.rain_spread
    blast:set_owner(owner)
    blast.team = owner.team
    blast.level = marker.level
    blast.damage = marker:damage()
    blast:set_floor(marker:floor())
    blast:center_on(marker)
    blast:setxy(blast:xpos() + dx, blast:ypos() + dy)
    blast.ani_type = C.ANI_EXPLODE
    blast:s_set_bit_flags(C.BIT_FIRE, 1)
    blast:set_skip_exit(100)
    og.emit_sound(C.SOUND_EXPLODE)
  end
end

-- METEOR_RAIN marker: sits on the target spot and strikes every
-- rain_cadence ticks until its lifetime runs out or its caster dies.
local function meteor_rain_act(marker)
  local owner = marker:owner()
  -- rule: METEOR RAIN strikes a fire explosion within rain_spread of its spot every rain_cadence
  --   (6) ticks for rain_ticks and stops when its caster dies.
  -- tests: KitElemental.meteor_rain_strikes_the_target_area_every_six_ticks
  -- tests: KitElemental.meteor_rain_finds_the_nearest_foe_and_stops_with_its_caster
  -- tests: SemLibsElemental.every_meteor_rain_strike_plays_one_explosion_sound
  if not owner then
    km.finish(marker)
    return
  end
  local t = og.tuning(owner)
  marker.lifetime = marker:lifetime() - 1
  if og.mod(marker:lifetime(), t.rain_cadence) == 0 then
    strike(marker, owner, t)
  end
  if marker:lifetime() <= 0 then
    km.finish(marker)
  end
end

km.register(km.IMMOLATION, immolation_act)
km.register(km.METEOR_RAIN, meteor_rain_act)

-- STARBURST (slot 1) is the classic cast, refused only while IMMOLATE burns.
function M.starburst(classic)
  return function(self)
    -- rule: While IMMOLATE burns (every slot priced 0), STARBURST, METEOR RAIN and
    --   REKINDLE/SUPERNOVA refuse 'QUENCH IMMOLATE FIRST' instead of casting for nothing.
    -- tests: KitElemental.a_burning_elemental_casts_nothing_else_for_free
    -- tests: KitElemental.meteor_rain_strikes_the_target_area_every_six_ticks
    -- tests: KitElemental.rekindle_heals_and_refuses_at_full
    if burning(self) then
      return false, BURNING
    end
    return classic(self)
  end
end

-- IMMOLATE (slot 2): lights the fire, or (a second press after the latch,
-- free because the channel prices it at 0) puts it out.
function M.immolate(self)
  local t = og.tuning(self)
  local marker = km.find(self, km.IMMOLATION)
  -- rule: IMMOLATE is a toggle: the first press lights it (KIT_CHANNEL on), a second press inside
  --   the 10-tick latch is refused IMMOLATE SETTLING, after it quenches for free.
  -- tests: KitElemental.second_press_ends_it_free_after_the_latch
  -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
  if marker then
    -- rule: IMMOLATE is a toggle: the first press lights it (KIT_CHANNEL on), a second press inside
    --   the 10-tick latch is refused IMMOLATE SETTLING, after it quenches for free. Here: `age <
    --   kit_latch`.
    -- tests: KitElemental.second_press_ends_it_free_after_the_latch
    -- tests: KitElemental.immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers
    if km.age(marker) < t.kit_latch then
      return false, "IMMOLATE SETTLING"
    end
    quench(self, marker)
    return true
  end
  marker = km.spawn(self, km.IMMOLATION, t.immolate_max)
  -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
  --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova blast and
  --   ring blast are skipped or refused
  if not marker then
    return false, "COULD NOT IGNITE"
  end
  self:set_kit_state(self:kit_state() | C.KIT_CHANNEL)
  return true
end

-- The foe METEOR RAIN falls on: alive, on our floor and within rain_range.
local function rain_target(self, foe, t)
  -- rule: METEOR RAIN falls only on a foe that is alive, on the caster's floor and within
  --   rain_range (160).
  -- tests: KitElemental.meteor_rain_passes_over_a_dead_foe
  -- tests: KitElemental.meteor_rain_strikes_the_target_area_every_six_ticks
  if not foe then
    return nil
  end
  if foe:dead() ~= 0 then
    return nil
  end
  if foe:floor() ~= self:floor() then
    return nil
  end
  if self:distance_to_ob(foe) > t.rain_range then
    return nil
  end
  return foe
end

-- METEOR RAIN (slot 3): our foe, or the nearest one, gets a rain marker.
function M.meteor_rain(self)
  -- rule: While IMMOLATE burns (every slot priced 0), STARBURST, METEOR RAIN and REKINDLE/SUPERNOVA
  --   refuse 'QUENCH IMMOLATE FIRST' instead of casting for nothing.
  -- tests: KitElemental.a_burning_elemental_casts_nothing_else_for_free
  -- tests: KitElemental.meteor_rain_strikes_the_target_area_every_six_ticks
  -- tests: KitElemental.rekindle_heals_and_refuses_at_full
  if burning(self) then
    return false, BURNING
  end
  local t = og.tuning(self)
  local target = rain_target(self, self:foe(), t)
  -- rule: METEOR RAIN targets the caster's current foe when valid, else the nearest foe; with
  --   neither it refuses NO TARGET IN RANGE for free.
  -- tests: KitElemental.meteor_rain_finds_the_nearest_foe_and_stops_with_its_caster
  -- tests: KitElemental.meteor_rain_passes_over_a_dead_foe
  -- tests: KitElemental.meteor_rain_strikes_the_target_area_every_six_ticks
  if not target then
    target = rain_target(self, og.find_near_foe(self), t)
  end
  if not target then
    return false, "NO TARGET IN RANGE"
  end
  local marker = km.spawn(self, km.METEOR_RAIN, t.rain_ticks)
  -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
  --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova blast and
  --   ring blast are skipped or refused
  if not marker then
    return false, "COULD NOT CALL METEORS"
  end
  marker:set_floor(target:floor())
  marker:center_on(target)
  marker.level = self.level
  marker.damage = t.rain_damage_base + self.level * 2
  return true
end

local function rekindle(self, t)
  -- rule: REKINDLE heals rekindle_hp + 4/level (clamped to max) and refuses at full health for
  --   free.
  -- tests: KitElemental.rekindle_heals_and_refuses_at_full
  -- tests: SemLibsElemental.a_rekindle_heal_plays_one_heal_sound_and_the_refusal_none
  if self.hp >= self.max_hp then
    return false, "ALREADY AT FULL HEALTH"
  end
  self:heal_clamped(t.rekindle_hp + self.level * 4)
  hit_flash(self)
  og.emit_sound(C.SOUND_HEAL)
  return true
end

-- One fire blast of SUPERNOVA's, centred on `at` and moved by (dx, dy).
local function nova_blast(self, at, dx, dy)
  local blast = og.add_ob("fx", FX_EXPLOSION)
  -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
  --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova blast and
  --   ring blast are skipped or refused
  if blast then
    blast:set_owner(self)
    blast.team = self.team
    -- The caster is gone when the blast goes off: the nova mark makes the
    -- blast spare its own team half by team colour (effect_bomb.lua).
    blast:set_kit_state(blast:kit_state() | C.KIT_NOVA)
    blast:set_floor(self:floor())
    blast:center_on(at)
    blast:setxy(blast:xpos() + dx, blast:ypos() + dy)
    blast.ani_type = C.ANI_EXPLODE
    blast:s_set_bit_flags(C.BIT_FIRE, 1)
  end
  return blast
end

-- SUPERNOVA: every hit point goes into one blast (its own level, so its
-- range, doubles the elemental's), and eight blasts of half its damage go
-- off in a ring nova_ring_px out, so the nova reaches past one blast's
-- range. Each ring blast carries the main blast's level: the elemental is
-- dead when they go off, so each blast reads its own level for its range.
-- Then the elemental dies and on_death fires the parting starburst on top.
local function supernova(self, t)
  local blast = nova_blast(self, self, 0, 0)
  -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
  --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova blast and
  --   ring blast are skipped or refused
  if blast then
    blast.level = og.min(self.level * 2, 24)
    -- hp is a C++ float: per-op rounding.
    blast.damage = og.fadd(og.fdiv(og.fmul(self.hp, 3), 2), self.level * t.nova_per_level)
    local ring_damage = og.div(og.trunc(blast:damage()), 2)
    -- rule: SUPERNOVA turns all the elemental's health into one blast (1.5*hp + 15/level, level
    --   doubled up to 24) plus eight half-damage blasts in a ring nova_ring_px out; then it dies
    --   and its parting starburst follows.
    -- tests: KitElemental.supernova_spends_all_health_blasts_a_ring_and_the_starburst_follows
    for i = -1, 1 do
      -- rule: SUPERNOVA turns all the elemental's health into one blast (1.5*hp + 15/level, level
      --   doubled up to 24) plus eight half-damage blasts in a ring nova_ring_px out.
      -- tests: KitElemental.supernova_spends_all_health_blasts_a_ring_and_the_starburst_follows
      for j = -1, 1 do
        -- rule: SUPERNOVA turns all the elemental's health into one blast (1.5*hp + 15/level, level
        --   doubled up to 24) plus eight half-damage blasts in a ring nova_ring_px out.
        -- tests: KitElemental.supernova_spends_all_health_blasts_a_ring_and_the_starburst_follows
        if i ~= 0 or j ~= 0 then
          local ring = nova_blast(self, blast, i * t.nova_ring_px, j * t.nova_ring_px)
          -- plumbing: og.add_ob / km.spawn nil when the world is full: flash, ember, meteor strike,
          --   immolation marker (COULD NOT IGNITE), rain marker (COULD NOT CALL METEORS), nova
          --   blast and ring blast are skipped or refused
          if ring then
            ring.level = blast.level
            ring.damage = ring_damage
          end
        end
      end
    end
  end
  og.emit_sound(C.SOUND_EXPLODE)
  self.hp = 0
  self.dead = 1
  self:death()
  return true
end

-- REKINDLE / SUPERNOVA (slot 4). The whole slot is new, so a plain shift
-- test is enough: with the setting off neither arm is in play.
function M.rekindle_or_supernova(self)
  -- rule: While IMMOLATE burns (every slot priced 0), STARBURST, METEOR RAIN and REKINDLE/SUPERNOVA
  --   refuse 'QUENCH IMMOLATE FIRST' instead of casting for nothing.
  -- tests: KitElemental.a_burning_elemental_casts_nothing_else_for_free
  -- tests: KitElemental.meteor_rain_strikes_the_target_area_every_six_ticks
  -- tests: KitElemental.rekindle_heals_and_refuses_at_full
  if burning(self) then
    return false, BURNING
  end
  local t = og.tuning(self)
  -- rule: Slot 4 is REKINDLE unshifted and SUPERNOVA shifted.
  -- tests: KitElemental.supernova_spends_all_health_blasts_a_ring_and_the_starburst_follows
  -- tests: KitElemental.rekindle_heals_and_refuses_at_full
  if self:shifter_down() ~= 0 then
    return supernova(self, t)
  end
  return rekindle(self, t)
end

-- Bot gates. The setting guard is first and answers true, so with the
-- setting off a gate draws and changes nothing (check_special() && !rng(3)
-- would skip a draw on false).
-- IMMOLATE burns what touches the elemental: light it with a living foe
-- at blows and the mana to burn; while it burns, cast again (the quench)
-- once no living foe is within IMMOLATE_GATE_RANGE, so the drain stops
-- paying for nothing. The foe search within the same range is kept on the
-- lighting arm for its draw and its foe.
function M.ai_immolate(self)
  -- rule: A bot elemental lights IMMOLATE with a living foe at blows and more than 60 MP (its
  --   classic-style foe search within 40 runs for the draw and the foe); while burning it casts
  --   again (quench) only once no living foe is within 40 on its floor.
  -- tests: KitElemental.ai_immolate_fires_when_a_foe_is_close_and_mana_is_high
  -- tests: GameplayFixesBots.elemental_immolate_lights_at_blows_and_quenches_alone
  -- tests: KitElemental.ai_off_answers_the_classic_gate
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if burning(self) then
    return not G.foes_within(self, 1, IMMOLATE_GATE_RANGE, true)
  end
  if self.magicpoints <= IMMOLATE_GATE_MANA then
    return false
  end
  og.check_special_ai_distance(self, IMMOLATE_GATE_RANGE)
  return #G.foes_at_blows(self) >= 1
end

-- Our foe within 160 with at least one more of our foes within 60 of it.
function M.ai_meteor(self)
  -- rule: A bot elemental calls METEOR RAIN on its foe within 160 when at least one more foe stands
  --   within 60 of it, and never when one of its allies stands inside the strike box (71 px each
  --   axis of the target).
  -- tests: KitElemental.ai_meteor_fires_on_a_cluster_in_range
  -- tests: KitElemental.ai_meteor_holds_fire_over_an_ally
  -- tests: KitElemental.ai_meteor_holds_fire_over_a_far_corner_ally
  -- tests: KitElemental.ai_meteor_fires_with_the_ally_clear
  -- tests: KitElemental.ai_off_answers_the_classic_gate
  -- tests: SemLibsElemental.a_friendly_tent_in_the_strike_box_does_not_hold_a_bot_meteor_rain
  -- deferred: #487 not #458 (burning() unasked): the ally scan and the foe cluster ignore floors,
  --   though the rain only hits its own floor (effect_bomb.lua), so an ally on another floor
  --   holds it; the cluster also counts an enemy tent (raw find_foes_in_range, unlike G.living_foes
  --   everywhere else)
  if og.match_setting("new_specials") == 0 then
    return true
  end
  if not og.check_special_ai_distance(self, RAIN_GATE_RANGE) then
    return false
  end
  local target = self:foe()
  local foes = og.find_foes_in_range("ob", RAIN_GATE_RANGE + RAIN_GATE_CLUSTER, self)
  local clustered = 0
  for i = 1, #foes do
    -- rule: A bot elemental calls METEOR RAIN on its foe within 160 when at least one more foe
    --   stands within 60 of it, and never when one of its allies stands inside the strike box (71
    --   px each axis of the target).
    -- tests: KitElemental.ai_meteor_fires_on_a_cluster_in_range
    -- tests: KitElemental.ai_meteor_holds_fire_over_an_ally
    -- tests: KitElemental.ai_meteor_holds_fire_over_a_far_corner_ally
    -- tests: KitElemental.ai_meteor_fires_with_the_ally_clear
    -- tests: KitElemental.ai_off_answers_the_classic_gate
    -- tests: SemLibsElemental.a_friendly_tent_in_the_strike_box_does_not_hold_a_bot_meteor_rain
    if foes[i]:distance_to_ob(target) <= RAIN_GATE_CLUSTER then
      clustered = clustered + 1
    end
  end
  -- An ally standing in the rain (the strike lands within rain_spread of
  -- the target on each axis and the blast reaches 31 px) holds the fire.
  -- The scan must reach the whole box: the target is under 160 away and
  -- a box corner is 71 + 71 Manhattan beyond it.
  local friends = og.find_friends_in_range("ob", RAIN_GATE_RANGE + 2 * RAIN_ALLY_ZONE, self)
  for i = 1, #friends do
    local dx = math.abs(friends[i]:xpos() - target:xpos())
    local dy = math.abs(friends[i]:ypos() - target:ypos())
    -- rule: A bot elemental calls METEOR RAIN on its foe within 160 when at least one more foe
    --   stands within 60 of it, and never when one of its allies stands inside the strike box (71
    --   px each axis of the target).
    -- tests: KitElemental.ai_meteor_fires_on_a_cluster_in_range
    -- tests: KitElemental.ai_meteor_holds_fire_over_an_ally
    -- tests: KitElemental.ai_meteor_holds_fire_over_a_far_corner_ally
    -- tests: KitElemental.ai_meteor_fires_with_the_ally_clear
    -- tests: KitElemental.ai_off_answers_the_classic_gate
    -- tests: SemLibsElemental.a_friendly_tent_in_the_strike_box_does_not_hold_a_bot_meteor_rain
    if dx <= RAIN_ALLY_ZONE and dy <= RAIN_ALLY_ZONE then
      return false
    end
  end
  return clustered >= 2
end

-- SUPERNOVA below a fifth of our health with two foes close; otherwise
-- REKINDLE below half, when the elemental can pay it (the engine keeps the
-- slot for SUPERNOVA's 10 on a smaller pool and does not check again).
function M.ai_rekindle(self)
  -- rule: A bot elemental goes SUPERNOVA (shift 1) below a fifth of its health with two bodies the
  --   finder counts as foes within 48; otherwise REKINDLE (shift 0) below half health when it can
  --   pay REKINDLE's 40; else holds.
  -- tests: KitElemental.ai_rekindle_heals_when_hurt_and_novas_when_cornered
  -- tests: GameplayFixesBots.elemental_rekindle_is_not_forced_on_a_pool_that_cannot_pay_it
  -- tests: KitElemental.ai_off_answers_the_classic_gate
  if og.match_setting("new_specials") == 0 then
    return true
  end
  -- max_hp is a C++ float: per-op rounding.
  if self.hp < og.fdiv(self.max_hp, 5.0) then
    local _, close = og.find_foes_in_range("ob", NOVA_GATE_RANGE, self)
    -- rule: A bot elemental goes SUPERNOVA (shift 1) below a fifth of its health with two bodies
    --   the finder counts as foes within 48. Here: `close >= 2`.
    -- tests: KitElemental.ai_rekindle_heals_when_hurt_and_novas_when_cornered
    -- tests: GameplayFixesBots.elemental_rekindle_is_not_forced_on_a_pool_that_cannot_pay_it
    -- tests: KitElemental.ai_off_answers_the_classic_gate
    if close >= 2 then
      self:set_shifter_down(1)
      return true
    end
  end
  self:set_shifter_down(0)
  -- max_hp is a C++ float: per-op rounding.
  if self.hp >= og.fdiv(self.max_hp, 2.0) then
    return false
  end
  return G.can_pay(self, 4, false)
end

-- The declarations that reference this module (packs/core/families/):
--   core:elemental  default_cast = ke.starburst(do_special); cast = ke.immolate,
--                   ke.meteor_rain, ke.rekindle_or_supernova; ai = ke.ai_immolate,
--                   ke.ai_meteor, ke.ai_rekindle; its on_death calls ke.end_channel
return M
