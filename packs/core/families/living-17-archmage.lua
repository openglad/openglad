-- core:archmage — teleport/marker, heartburst/chain, summons, mind control (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- rng order: hit_response rand(3)/rand(2); illusion tier rand(3/5/7/9);
-- mind-control rand(20)/rand(8) + charm_duration draw.
-- Unlike the Mage twin, marker notifications here are NOT gated on
-- user() ~= -1 — an intentional divergence between the twins.

local C = og.C
local lc = og.use("living_common")
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local FX_MARKER = assert(og.family_id("fx", "core:marker"))
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))
local FX_CHAIN = assert(og.family_id("fx", "core:chain"))
local LIVING_ELF = assert(og.family_id("living", "core:elf"))
local LIVING_SOLDIER = assert(og.family_id("living", "core:soldier"))
local LIVING_ARCHER = assert(og.family_id("living", "core:archer"))
local LIVING_ORC = assert(og.family_id("living", "core:orc"))
local LIVING_SKELETON = assert(og.family_id("living", "core:skeleton"))
local LIVING_DRUID = assert(og.family_id("living", "core:druid"))
local LIVING_CLERIC = assert(og.family_id("living", "core:cleric"))
local LIVING_ELEMENTAL = assert(og.family_id("living", "core:elemental"))
local LIVING_ORC_CAPTAIN = assert(og.family_id("living", "core:orc_captain"))

-- Gameplay Fixes bot gates: TELEPORT keeps the mage's rule (flee when nobody
-- is within 110 px, or more than three foes are); the summons count living
-- foes within the hit response's own 200 px.
local TELEPORT_GATE_RANGE = 110
local SUMMON_GATE_RANGE = 200

-- rule: Every weapon an archmage fires drains a twentieth of the MP it has left after the shot's
--   cost, capped at SHOT_DRAIN_CAP (50, so the cap binds only above 1000 MP), and adds the drained
--   amount to that shot's damage.
-- tests: FamilyBehaviors.archmage_weapon_damage_boost
-- tests: SemArchmage.an_archmage_shot_drains_at_most_fifty_magic_into_its_damage
local function on_fire_weapon(self, weapon)
  -- ArchMage gets 1/20th of 'extra' magic for more damage.
  -- The drain cap binds only above 1000 MP; /20 is one float division.
  local extra = og.fdiv(self.magicpoints, 20)
  extra = og.min(extra, C.SHOT_DRAIN_CAP)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fsub(self.magicpoints, extra)
  -- damage is a C++ float: per-op rounding.
  weapon.damage = og.fadd(weapon:damage(), extra)
  return true
end

-- Fires every tick per archmage: bonus viewing periodically based on level.
local function on_act_living(self)
  local period
  -- rule: An archmage's magical sight (view_all) grows by one every (40 - level) ticks of its own
  --   act counter, every tick from level 40.
  -- tests: FamilyBehaviors.archmage_on_act_low_level_periodic_gate
  -- tests: SemArchmage.classic_view_all_grows_every_act_from_level_40_and_restarts_its_cadence_at_the_drawcycle_wrap
  if self.level >= 40 then
    period = 1
  else
    period = 40 - self.level
  end
  -- Keep C remainder semantics explicit for drawcycle.
  if og.mod(self:drawcycle(), period) == 0 then
    self:set_view_all(self:view_all() + 1)
  end
end

-- SUMMON ELEMENTAL only when the spare pool is at least the slot's own
-- price (the surcharge then halves a pool of 500 or more); else the image.
-- With the Gameplay Fixes setting on, a bot's summon gate asks this instead
-- of keeping the coin.
local function summon_shift(self)
  -- rule: Gameplay Fixes: a bot archmage picks SUMMON ELEMENTAL only when the pool left after the
  --   slot-3 price still covers that price again (1000+ MP for the 500 slot); otherwise SUMMON
  --   IMAGE.
  -- tests: parity:archmage_hit_stale_shift_fixes_scen99
  -- tests: GameplayFixesBots.archmage_summon_wants_a_foe_and_the_pool_for_an_elemental_with_the_setting_on
  if lc.spare_mp(self, 3) >= self:s_special_cost(3) then
    self:set_shifter_down(1)
  else
    self:set_shifter_down(0)
  end
end

-- The slot-2 bot gate (defined below): the hit response asks it too, and is
-- defined first, so the name is declared here.
local ai_burst_or_chain

-- The trampoline passes stats->controller() (the stats' owner walker) as
-- self, so the C++ body's `controller` IS self and `stats->` maps to s_*.
local function hit_response(self, foe)
  self.busy = 0 -- yes, this is a cheat

  local possible = {}
  -- plumbing: builds the hit response's possible[] table over slots 0..(L+2)/3; rule row
  --   below
  for i = 0, (self.level + 2) // 3 do
    -- rule: A struck archmage can only answer with a special its level unlocks ((L+2)/3 slots) and
    --   its MP covers.
    -- tests: PackLuaArchmage.a_struck_archmage_answers_with_chain_lightning
    -- tests: SemArchmage.a_struck_archmage_never_answers_with_a_slot_its_level_locks_or_its_magic_cannot_pay
    if i < C.NUM_SPECIALS
        and self.magicpoints >= self:s_special_cost(i) then
      possible[i] = 1
    end
  end

  local threshold
  -- rule: A struck hero archmage flees (teleports) below 60% health; a monster archmage is braver
  --   and waits until 3/8.
  -- tests: StatsHitResponseTeleport.an_archmage_struck_again_mid_teleport_casts_nothing
  -- tests: GameplayFixesArchmageHit.a_struck_bot_below_the_threshold_still_teleports
  -- tests: SemArchmage.a_struck_hero_archmage_flees_below_three_fifths_and_a_monster_only_below_three_eighths
  if self:has_guy() then
    -- Player characters flee at 60% health.
    -- max_hp is a C++ float: per-op rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 5.0)
  else
    -- Enemies are braver :> — they wait until 3/8 health.
    -- max_hp is a C++ float: per-op rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 8.0)
  end

  -- C++ &&-chain short-circuit preserved: rand(3) is drawn only when the
  -- hitpoint and possible[1] conditions already hold.
  -- rule: A struck archmage under its flee threshold teleports away two times in three (rand(3)
  --   drawn only when hurt and TELEPORT affordable); otherwise it turns on its attacker.
  -- tests: FamilyBehaviors.archmage_hit_response_threshold_and_retarget_branches
  -- tests: GameplayFixesArchmageHit.a_struck_bot_below_the_threshold_still_teleports
  -- tests: StatsHitResponseTeleport.stats_hit_response_mage_and_archmage_teleport_branches
  -- tests: GameplayFixesArchmageHit.a_struck_bot_bursts_once_and_never_summons
  if self.hp < threshold and possible[1]
      and og.rand(3) ~= 0 then
    self.current_special = 1
    self:set_shifter_down(0)
    self.busy = 0
    -- rule: An archmage struck again while already mid-teleport casts nothing more (no second
    --   teleport).
    -- tests: StatsHitResponseTeleport.an_archmage_struck_again_mid_teleport_casts_nothing
    local _ = lc.mid_teleport(self) or self:special()  -- struck mid-teleport: no second cast
  else
    -- rule: A struck archmage that is not already fighting its attacker takes the attacker as its
    --   foe, and the attacker takes it back; its range memory resets.
    -- tests: FamilyBehaviors.archmage_hit_response_threshold_and_retarget_branches
    -- tests: PackLuaArchmage.a_struck_archmage_answers_with_chain_lightning
    -- tests: SemArchmage.a_struck_archmage_already_fighting_its_attacker_keeps_its_range_memory
    if self:foe() ~= foe then
      self.foe = foe
      foe.foe = self
      self:s_set_current_distance(15000)
      self:s_set_last_distance(15000)
    end
    -- rule: Gameplay Fixes: a struck bot archmage answers only with HEARTBURST or CHAIN LIGHTNING,
    --   chosen by the slot-2 gate (coin drawn first as classic did), at most once; it never summons
    --   from the hit response. A seat-driven archmage keeps the classic response.
    -- tests: GameplayFixesArchmageHit.a_struck_bot_bursts_once_and_never_summons
    -- tests: GameplayFixesArchmageHit.a_struck_bot_with_no_target_in_reach_casts_nothing
    -- tests: GameplayFixesArchmageHit.a_seated_archmage_keeps_its_shift_key
    -- tests: parity:archmage_bot_struck_fixes_scen99
    -- tests: SemArchmage.a_struck_bot_archmage_that_cannot_pay_heartburst_answers_nothing_with_gameplay_fixes
    if gf.on() and not self:seat_driven() then
      -- Gameplay Fixes: a struck bot answers with HEARTBURST or CHAIN
      -- LIGHTNING by the slot-2 gate, once; SUMMON IMAGE only by its own
      -- gate on an act tick; never a second cast on a stale slot. The coin
      -- is the classic slot-2 coin, drawn here as the classic order drew it.
      -- rule: Gameplay Fixes: a struck bot archmage answers only with HEARTBURST or CHAIN
      --   LIGHTNING, chosen by the slot-2 gate (coin drawn first as classic did), at most once.
      -- tests: GameplayFixesArchmageHit.a_struck_bot_bursts_once_and_never_summons
      -- tests: GameplayFixesArchmageHit.a_struck_bot_with_no_target_in_reach_casts_nothing
      -- tests: GameplayFixesArchmageHit.a_seated_archmage_keeps_its_shift_key
      -- tests: parity:archmage_bot_struck_fixes_scen99
      -- tests: SemArchmage.a_struck_bot_archmage_that_cannot_pay_heartburst_answers_nothing_with_gameplay_fixes
      if possible[2] then
        self:set_shifter_down(og.rand(2))
        self.current_special = 2
        -- rule: Gameplay Fixes: a struck bot archmage answers only with HEARTBURST or CHAIN
        --   LIGHTNING, chosen by the slot-2 gate (coin drawn first as classic did), at most once.
        -- tests: GameplayFixesArchmageHit.a_struck_bot_bursts_once_and_never_summons
        -- tests: GameplayFixesArchmageHit.a_struck_bot_with_no_target_in_reach_casts_nothing
        -- tests: GameplayFixesArchmageHit.a_seated_archmage_keeps_its_shift_key
        -- tests: parity:archmage_bot_struck_fixes_scen99
        -- tests: SemArchmage.a_struck_bot_archmage_that_cannot_pay_heartburst_answers_nothing_with_gameplay_fixes
        if ai_burst_or_chain(self) then
          self:special()
        end
      end
      return
    end
    local _, foe_count = og.find_foes_in_range("ob", 200, self)
    -- rule: The classic order (the setting off, or a seat-driven archmage with it on): a struck
    --   archmage with a foe within 200 px first tries SUMMON (slot 3) with whatever Shift was left
    --   over (stale coin: image or elemental), then slot 2.
    -- tests: GameplayFixesArchmageHit.a_struck_bot_with_no_target_in_reach_casts_nothing
    -- tests: GameplayFixesArchmageHit.a_struck_bot_bursts_once_and_never_summons
    -- tests: GameplayFixesArchmageHit.a_seated_archmage_keeps_its_shift_key
    -- tests: GameplayFixesArchmageHit.a_struck_bot_below_the_threshold_still_teleports
    -- tests: SemArchmage.classic_a_struck_archmage_tries_slot_2_even_after_a_summon_that_stands
    if foe_count ~= 0 then
      -- can we summon illusion?
      -- rule: The classic order: a struck archmage with a foe within 200 px first tries SUMMON
      --   (slot 3) when it can pay it, with whatever Shift was left over (stale coin).
      -- tests: GameplayFixesArchmageHit.a_struck_bot_with_no_target_in_reach_casts_nothing
      -- tests: GameplayFixesArchmageHit.a_struck_bot_bursts_once_and_never_summons
      -- tests: GameplayFixesArchmageHit.a_seated_archmage_keeps_its_shift_key
      -- tests: GameplayFixesArchmageHit.a_struck_bot_below_the_threshold_still_teleports
      -- tests: SemArchmage.classic_a_struck_archmage_tries_slot_2_even_after_a_summon_that_stands
      if possible[3] then
        self.current_special = 3
        -- rule: Classic: the summon's answer is never read (the old game's walker::special answers
        --   0 on every path), so the reply goes on to slot 2. Gameplay Fixes (a seat-driven
        --   archmage): a summon that worked ends the reply.
        -- tests: SemArchmage.classic_a_struck_archmage_tries_slot_2_even_after_a_summon_that_stands
        -- tests: SemArchmage.with_gameplay_fixes_a_seated_archmages_summon_that_stands_ends_its_reply
        -- tests: GameplayFixesArchmageHit.a_seated_archmage_keeps_its_shift_key
        -- tests: GameplayFixesArchmageHit.a_struck_bot_bursts_once_and_never_summons
        if self:special() and gf.on() then
          return
        end
      end
      -- heartburst, chain lightning, etc.
      -- rule: Then slot 2 when the archmage can pay it (Classic: after any summon, one that stood
      --   included; Gameplay Fixes, a seat-driven archmage: only after a summon that failed):
      --   CHAIN LIGHTNING half the time, HEARTBURST otherwise or when the chain fails.
      -- tests: SemArchmage.classic_a_struck_archmage_tries_slot_2_even_after_a_summon_that_stands
      -- tests: SemArchmage.with_gameplay_fixes_a_seated_archmages_summon_that_stands_ends_its_reply
      -- tests: SemArchmageClassicSlot2.classic_a_struck_archmage_heartbursts_once_and_a_failed_burst_casts_nothing
      -- tests: SemArchmageClassicSlot2.classic_a_struck_archmage_whose_chain_fails_falls_through_to_a_heartburst_attempt
      if possible[2] then
        -- The old source called the follow-up "then leave! :)", but never
        -- switched to teleport: it repeats slot 2 as a normal heartburst.
        -- rule: A struck archmage paying slot 2 draws a coin (rand(2)): CHAIN LIGHTNING on a
        --   non-zero coin, HEARTBURST on zero; a chain that fails falls through to HEARTBURST.
        -- tests: SemArchmageClassicSlot2.classic_a_struck_archmages_chain_is_never_followed_by_a_second_cast
        -- tests: SemArchmageClassicSlot2.classic_a_struck_archmage_heartbursts_once_and_a_failed_burst_casts_nothing
        -- tests: SemArchmageClassicSlot2.classic_a_struck_archmage_whose_chain_fails_falls_through_to_a_heartburst_attempt
        -- tests: SemArchmageClassicSlot2.with_gameplay_fixes_a_seated_archmage_chains_and_then_heartbursts_when_it_can_still_pay_a_teleport
        -- tests: SemArchmageClassicSlot2.with_gameplay_fixes_a_seated_archmage_heartbursts_twice_and_a_failed_burst_casts_nothing
        if og.rand(2) ~= 0 then
          self:set_shifter_down(1)
          self.current_special = 2
          -- rule: Classic: the chain's answer is never read (the old game's walker::special answers
          --   0), so the reply goes on to the HEARTBURST attempt, which the chain's busy refuses.
          --   Gameplay Fixes (a seat-driven archmage): a chain that worked ends the reply, after
          --   the follow-up below.
          -- tests: SemArchmageClassicSlot2.classic_a_struck_archmages_chain_is_never_followed_by_a_second_cast
          -- tests: SemArchmageClassicSlot2.with_gameplay_fixes_a_seated_archmage_chains_and_then_heartbursts_when_it_can_still_pay_a_teleport
          -- tests: SemArchmageClassicSlot2.classic_a_struck_archmage_whose_chain_fails_falls_through_to_a_heartburst_attempt
          if self:special() and gf.on() then
            self:set_shifter_down(0)
            -- rule: Gameplay Fixes (a seat-driven archmage; the classic reply never comes here):
            --   after a chain that worked, an archmage that can still pay a TELEPORT casts AGAIN at
            --   once - but slot 2 is still selected, so the 'then leave' follow-up is a HEARTBURST,
            --   not a teleport.
            -- tests: SemArchmageClassicSlot2.with_gameplay_fixes_a_seated_archmage_chains_and_then_heartbursts_when_it_can_still_pay_a_teleport
            -- tests: SemArchmageClassicSlot2.classic_a_struck_archmages_chain_is_never_followed_by_a_second_cast
            -- tests: SemArchmage.with_gameplay_fixes_a_seated_archmages_summon_that_stands_ends_its_reply
            if self.magicpoints >= self:s_special_cost(1) then
              self.busy = 0
              self:special()
            end
            return
          end
        end
        self:set_shifter_down(0)
        self.current_special = 2
        -- rule: Classic: the HEARTBURST's answer is never read (the old game's walker::special
        --   answers 0), so a burst that worked is cast once and the reply ends. Gameplay Fixes (a
        --   seat-driven archmage): a burst that worked ends the reply after the follow-up below.
        -- tests: SemArchmageClassicSlot2.classic_a_struck_archmage_heartbursts_once_and_a_failed_burst_casts_nothing
        -- tests: SemArchmage.classic_a_struck_archmage_tries_slot_2_even_after_a_summon_that_stands
        -- tests: SemArchmageClassicSlot2.with_gameplay_fixes_a_seated_archmage_heartbursts_twice_and_a_failed_burst_casts_nothing
        -- tests: SemArchmage.with_gameplay_fixes_a_seated_archmages_summon_that_stands_ends_its_reply
        if self:special() and gf.on() then
          -- rule: Gameplay Fixes (a seat-driven archmage; the classic reply never comes here):
          --   after a HEARTBURST that worked, an archmage that can still pay a TELEPORT casts slot
          --   2 AGAIN at once, so the 'then leave' follow-up is a second HEARTBURST, not a
          --   teleport.
          -- tests: SemArchmageClassicSlot2.with_gameplay_fixes_a_seated_archmage_heartbursts_twice_and_a_failed_burst_casts_nothing
          -- tests: SemArchmage.with_gameplay_fixes_a_seated_archmages_summon_that_stands_ends_its_reply
          -- tests: SemArchmageClassicSlot2.classic_a_struck_archmage_heartbursts_once_and_a_failed_burst_casts_nothing
          if self.magicpoints >= self:s_special_cost(1) then
            self.busy = 0
            self:special()
          end
          return
        end
      end
    end
  end
end

-- Gameplay Fixes bot gates for slots 1-3 (slot 4 keeps its rest gate). Off,
-- each answers true, which is what a row with no `ai` answered.
local function ai_teleport(self)
  -- rule: Classic: a bot archmage teleports on every TELEPORT roll. Gameplay Fixes: it teleports
  --   (the mage's rule) only when nobody is within 110 px or more than three foes are, never
  --   mid-teleport; with nobody near it plants a TELEPORT MARKER instead (Shift 1) when it has no
  --   marker standing, a hero has 75 INT, the pool after the price still covers HEARTBURST, and it
  --   is not busy.
  -- tests: GameplayFixesBots.archmage_teleport_takes_the_mage_rule_with_the_setting_on
  -- tests: GameplayFixesBots.archmage_marker_is_placed_once_where_the_archmage_will_return
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  -- tests: SemArchmage.a_crowded_bot_archmage_flees_with_a_plain_teleport_never_a_marker
  if not gf.on() then
    return true
  end
  local _, foe_count = og.find_foes_in_range("ob", TELEPORT_GATE_RANGE, self)
  -- The TELEPORT MARKER (the Shift arm) is no longer the coin's: with nobody
  -- near, mark where the archmage stands when no marker of its own stands,
  -- a hero has the cast's 75 INT, and half the pool left after the price
  -- still covers HEARTBURST. Written after the engine's coin.
  local mark = foe_count < 1 and G.marker_pays(self, FX_MARKER, 2, 75)
  if mark and not lc.is_busy(self) then
    self:set_shifter_down(1)
  else
    self:set_shifter_down(0)
  end
  local yes = foe_count < 1 or foe_count > 3
  return yes and not lc.mid_teleport(self)
end

-- CHAIN LIGHTNING wants a foe inside its radius, HEARTBURST inside its own:
-- the coin's arm is kept when it has a target, swapped when only the other
-- has one, refused when neither has.
ai_burst_or_chain = function(self)
  -- rule: Gameplay Fixes: a bot's slot-2 cast keeps the coin's arm when that arm has a foe in its
  --   radius (heartburst 80+2L, chain 200+5L+2L), swaps to the other arm when only it has one, and
  --   is refused when neither has a foe or the archmage is busy.
  -- tests: GameplayFixesBots.archmage_burst_or_chain_takes_the_arm_with_a_target_with_the_setting_on
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  -- tests: SemArchmage.a_bot_archmage_whose_chain_coin_has_only_a_burst_target_swaps_to_heartburst
  -- deferred: #453 the missing arm is asserted (tuning override); the row stays on #453: the gate's
  --   chain radius ignores the hero formula the cast uses
  if not gf.on() then
    return true
  end
  -- The cast's own guard: it refuses a busy archmage.
  if lc.is_busy(self) then
    return false
  end
  local t = og.tuning(self)
  local bonus = t.radius_bonus_per_level * self.level
  local _, in_burst = og.find_foes_in_range("ob", t.burst_radius_base + bonus, self)
  local _, in_chain = og.find_foes_in_range("ob",
    t.chain_radius_base + t.chain_radius_per_level * self.level + bonus, self)
  if self:shifter_down() ~= 0 then
    -- rule: Gameplay Fixes: a bot's slot-2 cast keeps the coin's arm when that arm has a foe in its
    --   radius (heartburst 80+2L, chain 200+5L+2L), swaps to the other arm when only it has one,
    --   and is refused when neither has a foe or the archmage is busy.
    -- tests: GameplayFixesBots.archmage_burst_or_chain_takes_the_arm_with_a_target_with_the_setting_on
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    -- tests: SemArchmage.a_bot_archmage_whose_chain_coin_has_only_a_burst_target_swaps_to_heartburst
    if in_chain >= 1 then
      return true
    end
    self:set_shifter_down(0)
    return in_burst >= 1
  end
  if in_burst >= 1 then
    return true
  end
  if in_chain >= 1 then
    self:set_shifter_down(1)
    return true
  end
  return false
end

-- SUMMON ELEMENTAL (the Shift arm, by the pool: summon_shift) wants a living
-- foe within 200 px. SUMMON IMAGE, a phantom with one hit point, is a decoy:
-- it pays before the blows land, when at least two living foes within
-- 200 px are hunting the archmage and none is within a blow of it yet (at
-- blows HEARTBURST is the cheaper answer), its health is above the flee
-- threshold (below it TELEPORT is the answer), no phantom of its own
-- stands, and the pool after the 500 still covers a HEARTBURST.
local function ai_summon(self)
  -- rule: Gameplay Fixes: a bot's SUMMON ELEMENTAL (pool-chosen) wants a living foe within 200 px;
  --   its SUMMON IMAGE is a decoy cast before blows land: two or more foes within 200 px hunting
  --   it, none at blows yet, its health above the flee threshold (60% hero, 3/8 monster), no
  --   phantom of its own standing, and a pool after the 500 that still covers HEARTBURST.
  -- tests: GameplayFixesBots.archmage_summon_wants_a_foe_and_the_pool_for_an_elemental_with_the_setting_on
  -- tests: GameplayFixesBots.archmage_summon_image_wants_two_hunters_not_yet_at_blows_and_a_pool_left
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  -- tests: parity:archmage_bot_summon_no_foe_fixes_scen99
  -- tests: parity:archmage_bot_image_one_foe_fixes_scen99
  if not gf.on() then
    return true
  end
  -- The cast's own guard: it refuses a busy archmage.
  if lc.is_busy(self) then
    return false
  end
  summon_shift(self)
  if self:shifter_down() ~= 0 then
    return G.foes_within(self, 1, SUMMON_GATE_RANGE, false)
  end
  local hurt
  if self:has_guy() then
    hurt = G.hp_below(self, 3, 5)
  else
    hurt = G.hp_below(self, 3, 8)
  end
  if hurt then
    return false
  end
  if G.owned_phantom(self) ~= nil then
    return false
  end
  if #G.foes_at_blows(self) > 0 then
    return false
  end
  if lc.spare_mp(self, 3) < self:s_special_cost(2) then
    return false
  end
  return #G.hunters(self, SUMMON_GATE_RANGE) >= 2
end

-- MIND CONTROL wants a foe the control can take (never charmed, not a hero
-- a seat drives, not above the archmage's level) inside its reach; the rest
-- holds the slot on both values (lc.gate_with_rest).
local function ai_mind_control(self)
  -- rule: Gameplay Fixes: MIND CONTROL wants a foe it can take inside its reach: never charmed, not
  --   a seated hero, not above the archmage's level (the rest gate wraps it on both values).
  -- tests: GameplayFixesBots.archmage_mind_control_wants_a_charmable_foe_in_reach
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  -- tests: parity:archmage_bot_mind_control_hero_fixes_scen99
  if not gf.on() then
    return true
  end
  -- The cast's own guard: it refuses a busy archmage.
  if lc.is_busy(self) then
    return false
  end
  local t = og.tuning(self)
  local foes = og.find_foes_in_range("ob",
    t.mind_control_range_base + t.mind_control_range_per_level * self.level, self)
  return gf.nearest_charmable(self, foes, self.level) ~= nil
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 6, 4, 16, 1)
end

local function teleport(self)
  -- rule: TELEPORT unshifted sends the archmage out (ANI_TELE_OUT, teleport sound) unless it is
  --   already teleporting; Shift places/replaces a marker instead.
  -- tests: WalkerSpecials.archmage_teleport_drives_simulation
  -- tests: StatsHitResponseTeleport.stats_hit_response_mage_and_archmage_teleport_branches
  -- tests: GameplayFixesArchmageHit.a_struck_bot_below_the_threshold_still_teleports
  -- tests: StatsHitResponseTeleport.an_archmage_struck_again_mid_teleport_casts_nothing
  -- tests: SemArchmage.an_archmage_already_teleporting_cannot_teleport_again
  if lc.mid_teleport(self) then
    return false, "SPECIAL BUSY"
  end
  if self:shifter_down() == 0 then
    og.emit_positional_sound(self, C.SOUND_TELEPORT)
    self.ani_type = C.ANI_TELE_OUT
    self:set_cycle(0)
    return true
  end
  -- leave/remove a marker
  -- rule: Placing a TELEPORT MARKER refuses a busy archmage and a hero with under 75 intelligence
  --   (monsters have no INT requirement), at no cost.
  -- tests: WalkerSpecials.archmage_marker_needs_75_int_then_chain_and_summon_are_one_each
  -- tests: FamilyBehaviors.archmage_handle_teleport_and_special_guards
  -- tests: SimInputUnit.intelligence_refusals_use_one_private_cue
  -- tests: parity:archmage_teleport_marker_scen99
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  if self:has_guy() and self:g_intelligence() < 75 then
    return false, "75 INT REQUIRED"
  end
  -- Remove this caster's old marker, if present. (The C++ sets
  -- generic = 1 here but never tests it: a new marker is always
  -- placed below.)
  local obs = og.oblist()
  -- plumbing: oblist scan for this caster's live marker; rule row below
  for i = 1, #obs do
    local ob = obs[i]
    -- rule: An archmage has at most one marker: placing a new one retires its own previous live
    --   marker (and only its own), costing 8 extra busy ticks.
    -- tests: PackLuaArchmage.a_second_teleport_marker_retires_the_first
    -- tests: WalkerSpecials.archmage_marker_needs_75_int_then_chain_and_summon_are_one_each
    -- tests: SemArchmage.a_new_marker_retires_only_its_own_casters_old_marker
    if ob
        and ob:order() == C.ORDER_FX
        and ob:family() == FX_MARKER
        and ob:owner() == self
        and ob:dead() == 0 then
      ob.dead = 1
      ob:death()
      -- rule: Marker messages ('(Old Marker Removed)', 'Teleport Marker Placed', '(N Uses)') are
      --   told only on the player's side or for a hero, addressed to the caster; unlike the mage
      --   they are not gated on user() (any team-0 bot also announces).
      -- tests: PackLuaArchmage.a_second_teleport_marker_retires_the_first
      -- tests: parity:archmage_teleport_marker_scen99
      -- tests: SemArchmage.an_enemy_archmage_places_and_replaces_its_marker_in_silence
      if self.team == 0 or self:has_guy() then
        og.emit_notification("(Old Marker Removed)", 0, self)
      end
      -- busy is a C++ float: per-op rounding.
      self.busy = og.fadd(self:busy(), 8.0)
      break
    end
  end
  local marker = og.add_ob("fx", FX_MARKER)
  -- plumbing: add_ob failure for the marker: 'COULD NOT CREATE MARKER' (old marker already
  --   retired).
  if not marker then
    return false, "COULD NOT CREATE MARKER"
  end
  marker:set_owner(self)
  marker:set_floor(self:floor())  -- marker stays on the caster's floor
  marker:center_on(self)
  -- rule: A marker's number of uses is a hero's intelligence / 33, a monster's level / 4 + 1.
  -- tests: WalkerSpecials.archmage_marker_needs_75_int_then_chain_and_summon_are_one_each
  -- tests: WalkerSpecials.archmage_teleport_drives_simulation
  -- tests: SemArchmage.a_monster_archmages_marker_holds_a_quarter_of_its_level_plus_one_uses
  if self:has_guy() then
    marker.lifetime = self:g_intelligence() // 33
  else
    marker.lifetime = self.level // 4 + 1
  end
  marker.ani_type = C.ANI_SPIN  -- the C++ wrote a raw 2 past the 16-row table
  -- rule: Marker messages ('(Old Marker Removed)', 'Teleport Marker Placed', '(N Uses)') are told
  --   only on the player's side or for a hero, addressed to the caster.
  -- tests: PackLuaArchmage.a_second_teleport_marker_retires_the_first
  -- tests: parity:archmage_teleport_marker_scen99
  -- tests: SemArchmage.an_enemy_archmage_places_and_replaces_its_marker_in_silence
  if self.team == 0 or self:has_guy() then
    og.emit_notification("Teleport Marker Placed", 0, self)
    og.emit_notification(string.format("(%d Uses)", marker:lifetime()), 0, self)
  end
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 8.0)
  -- Marker surcharge: half the MP left after the cast's own cost.
  lc.halve_mp_surcharge(self, self:current_special())
  return true
end

local function burst_or_chain(self)
  -- heartburst / chain lightning
  -- rule: A busy archmage cannot cast HEARTBURST / CHAIN LIGHTNING.
  -- tests: WalkerSpecials.archmage_heartburst_drives_simulation
  -- tests: SemArchmage.a_busy_archmage_cannot_heartburst_or_chain
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  local radius
  -- rule: HEARTBURST reaches foes within 80 + 2L px; CHAIN LIGHTNING acquires within 200 + Int/2 +
  --   2L for a hero, 200 + 5L + 2L for a monster.
  -- tests: SemArchmage.heartburst_reaches_80_plus_2l_and_a_monsters_chain_200_plus_7l
  -- tests: SemArchmage.a_heros_chain_reaches_200_plus_half_its_intelligence_plus_2l
  if self:shifter_down() ~= 0 then
    -- rule: HEARTBURST reaches foes within 80 + 2L px; CHAIN LIGHTNING acquires within 200 + Int/2
    --   + 2L for a hero, 200 + 5L + 2L for a monster.
    -- tests: SemArchmage.heartburst_reaches_80_plus_2l_and_a_monsters_chain_200_plus_7l
    -- tests: SemArchmage.a_heros_chain_reaches_200_plus_half_its_intelligence_plus_2l
    if self:has_guy() then
      -- Int/2 stays a formula: a guy-stat conversion, not a tuning knob.
      radius = t.chain_radius_base + self:g_intelligence() // 2
    else
      radius = t.chain_radius_base + self.level * t.chain_radius_per_level
    end
  else
    radius = t.burst_radius_base
  end
  local foes, foe_count = og.find_foes_in_range(
    "ob", radius + t.radius_bonus_per_level * self.level, self)
  -- rule: HEARTBURST / CHAIN with no foe in reach fail free.
  -- tests: WalkerSpecials.archmage_heartburst_drives_simulation
  -- tests: SemArchmage.heartburst_or_chain_with_nobody_in_reach_fails_free
  if foe_count == 0 then
    return false, "NO FOE IN RANGE"
  end
  -- rule: HEARTBURST splits its MP pool evenly over every foe in reach, one magical explosion
  --   centred on each (on that foe's floor), spending the pool share per foe; CHAIN LIGHTNING
  --   spends the pool once on a single bolt.
  -- tests: WalkerSpecials.archmage_chain_lightning_spawns_one_leashed_bolt
  -- tests: SemArchmage.heartburst_splits_its_pool_evenly_into_one_explosion_on_each_foe_in_reach
  if self:shifter_down() == 0 then
    -- normal heartburst
    local pool = lc.mp_pool_damage(self, 2)
    -- Preserve C truncation if a malformed state produces a negative pool.
    pool = og.div(pool, foe_count)
    -- rule: A hero's HEARTBURST counts one shot per foe it bursts; its CHAIN LIGHTNING counts one
    --   shot however many it hits (accuracy can exceed 100%).
    -- tests: SemArchmage.a_heros_heartburst_counts_a_shot_per_foe_and_its_chain_counts_one
    if self:has_guy() then
      self:g_set_total_shots(self:g_total_shots() + foe_count)
      self:g_set_scen_shots(self:g_scen_shots() + foe_count)
    end
    -- busy is a C++ float: per-op rounding.
    self.busy = og.fadd(self:busy(), 5.0)
    -- plumbing: one explosion per foe; rule row above
    for i = 1, #foes do
      local foe = foes[i]
      local burst = og.summon(self, "fx", FX_EXPLOSION)
      -- plumbing: og.summon failure mid-burst: 'COULD NOT MAKE EXPLOSION' (earlier explosions
      --   stand, their MP spent).
      if not burst then
        return false, "COULD NOT MAKE EXPLOSION"
      end
      burst:s_set_bit_flags(C.BIT_MAGICAL, 1)
      burst.damage = pool
      -- Burst materializes on the acquired foe; taking its floor keeps
      -- blast damage on that floor.
      burst:set_floor(foe:floor())
      burst:center_on(foe)
      og.emit_sound(C.SOUND_EXPLODE)
      burst.ani_type = C.ANI_EXPLODE
      burst:s_set_bit_flags(C.BIT_MAGICAL, 1)  -- (set twice in the C++)
      burst:set_skip_exit(100)
      -- magicpoints is a C++ float: per-op rounding.
      self.magicpoints = og.fsub(self.magicpoints, pool)
    end
    return true
  end
  -- chain lightning
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 5.0)
  -- rule: A hero's HEARTBURST counts one shot per foe it bursts; its CHAIN LIGHTNING counts one
  --   shot however many it hits (accuracy can exceed 100%).
  -- tests: SemArchmage.a_heros_heartburst_counts_a_shot_per_foe_and_its_chain_counts_one
  if self:has_guy() then
    -- One cast is one shot even when the chain hits several foes, so the
    -- old accuracy counter can get above 100% :)
    self:g_set_total_shots(self:g_total_shots() + 1)
    self:g_set_scen_shots(self:g_scen_shots() + 1)
  end
  local bolt = og.summon(self, "fx", FX_CHAIN)
  -- plumbing: og.summon failure for the chain bolt: 'COULD NOT CREATE CHAIN' (busy already added).
  if not bolt then
    return false, "COULD NOT CREATE CHAIN"
  end
  -- The initial bolt and its MP charge cap only above roughly 1280 MP.
  local pool = lc.mp_pool_damage(self, 2)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fsub(self.magicpoints, pool)
  bolt.damage = pool
  local best_dist = 30000
  -- plumbing: first-strike search over the foes; rule row below
  for i = 1, #foes do
    local foe = foes[i]
    -- Chain lightning cannot arc through solid floors: only same-floor
    -- foes are valid first strikes.
    -- rule: CHAIN LIGHTNING's first strike is the nearest foe (centre distance) on the archmage's
    --   own floor; it cannot arc through floors.
    -- tests: WalkerSpecials.archmage_chain_lightning_spawns_one_leashed_bolt
    -- tests: SemArchmage.chain_lightning_strikes_first_the_nearest_foe_on_its_own_floor
    -- deferred: #455 a nearer upstairs foe is skipped (asserted); the leaderless other-floor-only
    --   bolt is #455 and is not pinned
    if foe:floor() == self:floor() then
      local dist = self:distance_to_ob_center(foe)
      -- rule: CHAIN LIGHTNING's first strike is the nearest foe (centre distance) on the archmage's
      --   own floor; it cannot arc through floors.
      -- tests: WalkerSpecials.archmage_chain_lightning_spawns_one_leashed_bolt
      -- tests: SemArchmage.chain_lightning_strikes_first_the_nearest_foe_on_its_own_floor
      if best_dist > dist then
        best_dist = dist
        bolt:set_leader(foe)
      end
    end
  end
  return true
end

local function summon_image(self)
  -- summon image / elemental
  -- rule: A busy archmage cannot summon.
  -- tests: WalkerSpecials.archmage_summon_image_drives_simulation
  -- tests: WalkerSpecials.archmage_true_summon_places_one_owned_fire_elemental
  -- tests: SemArchmage.a_busy_archmage_cannot_summon
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: SUMMON with Shift calls a real fire elemental; unshifted it conjures a one-hit-point
  --   phantom of a random class.
  -- tests: WalkerSpecials.archmage_true_summon_places_one_owned_fire_elemental
  -- tests: parity:archmage_summon_elemental_scen99
  -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
  -- tests: WalkerSpecials.archmage_summon_image_drives_simulation
  if self:shifter_down() ~= 0 then
    -- true summoning
    local t = og.tuning(self)
    -- rule: A hero needs 150 intelligence for the true summon; monsters do not.
    -- tests: FamilyBehaviors.archmage_special_case2_case3_case4_guard_branches
    -- tests: SimInputUnit.intelligence_refusals_use_one_private_cue
    -- tests: WalkerSpecials.archmage_true_summon_places_one_owned_fire_elemental
    -- tests: PackLuaArchmage.a_placed_true_summon_pays_half_the_spare_pool_on_both_values
    if self:has_guy() and self:g_intelligence() < t.summon_int_req then
      return false, string.format("%d INT REQUIRED", t.summon_int_req)
    end
    -- rule: The true summon also takes half the MP left after its price. Classic: taken before the
    --   elemental is placed, so a summon with nowhere to stand still pays it. Gameplay Fixes: taken
    --   only once the elemental stands.
    -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_is_cancelled
    -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_costs_nothing_with_gameplay_fixes
    -- tests: PackLuaArchmage.a_placed_true_summon_pays_half_the_spare_pool_on_both_values
    -- tests: parity:archmage_summon_no_room_scen99, parity:archmage_summon_no_room_fixes_scen99
    if not gf.on() then
      lc.halve_mp_surcharge(self, 3)
    end
    -- First make the guy we'd summon, at least physically
    local elemental = og.add_ob("living", LIVING_ELEMENTAL)
    -- plumbing: add_ob failure for the elemental: 'COULD NOT SUMMON' (classic surcharge already
    --   taken).
    if not elemental then
      return false, "COULD NOT SUMMON"
    end
    -- Set the caster's floor before placement probes and setxy so both use
    -- the right floor.
    elemental:set_floor(self:floor())
    elemental:set_summoned(true)  -- ammunition: never a SAVE_ALL loss
    -- Placement scan: i outer, j inner, skip (0,0), and skip the rest of
    -- the grid once placed (the C++ `generic` latch) — query_passable is
    -- never probed again after a success.
    local placed = false
    -- plumbing: 3x3 placement scan around the caster; rule row below
    for i = -1, 1 do
      -- plumbing: 3x3 placement scan around the caster; rule row below
      for j = -1, 1 do
        -- rule: The elemental stands in the first passable of the eight neighbouring body-sized
        --   cells, scanned column by column from the west (i outer -1..1, j inner -1..1, skipping
        --   the caster's own cell); it is level (L+1)/2, the caster's team, owned, with
        --   elemental_lifetime(L). With no free cell the summon is cancelled.
        -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_is_cancelled
        -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_costs_nothing_with_gameplay_fixes
        -- tests: SemArchmage.a_summoned_elemental_stands_in_the_first_free_cell_from_the_west_column
        if not ((i == 0 and j == 0)
                or placed) then
          local testx = self:xpos() + (elemental:sizex() + 1) * i
          local testy = self:ypos() + (elemental:sizey() + 1) * j
          -- rule: The elemental stands in the first passable of the eight neighbouring body-sized
          --   cells, scanned column by column from the west (i outer -1..1, j inner -1..1, skipping
          --   the caster's own cell).
          -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_is_cancelled
          -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_costs_nothing_with_gameplay_fixes
          -- tests: SemArchmage.a_summoned_elemental_stands_in_the_first_free_cell_from_the_west_column
          if og.query_passable(testx, testy, elemental) then
            placed = true
            elemental:setxy(testx, testy)
            elemental.level = (self.level + 1) // 2
            elemental:set_difficulty(elemental.level)
            elemental.team = self.team
            elemental:set_owner(self)
            elemental.lifetime = og.elemental_lifetime(self.level)
          end
        end
      end
    end
    -- rule: The elemental stands in the first passable of the eight neighbouring body-sized cells,
    --   scanned column by column from the west (i outer -1..1, j inner -1..1, skipping the caster's
    --   own cell).
    -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_is_cancelled
    -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_costs_nothing_with_gameplay_fixes
    -- tests: SemArchmage.a_summoned_elemental_stands_in_the_first_free_cell_from_the_west_column
    if not placed then
      elemental.dead = 1
      return false, "NO ROOM TO SUMMON"
    end
    -- rule: The true summon also takes half the MP left after its price.
    -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_is_cancelled
    -- tests: PackLuaArchmage.a_true_summon_with_nowhere_to_stand_costs_nothing_with_gameplay_fixes
    -- tests: PackLuaArchmage.a_placed_true_summon_pays_half_the_spare_pool_on_both_values
    -- tests: parity:archmage_summon_no_room_scen99, parity:archmage_summon_no_room_fixes_scen99
    if gf.on() then
      -- Gameplay Fixes: the surcharge is paid once the elemental stands.
      lc.halve_mp_surcharge(self, 3)
    end
    -- Summoning takes lots of time :)
    -- busy is a C++ float: per-op rounding.
    self.busy = og.fadd(self:busy(), 15.0)
    return true
  end
  -- illusion summoning: tier by post-cost MP; draw counts per tier are
  -- exact (0/1/1/1/1 draws for the 5 tiers).
  local mp_pool = lc.spare_mp(self, 3)
  local person
  -- rule: The phantom's class widens with the magic left after the 500 price: under 100 always an
  --   elf (no draw); under 250 elf/soldier/archer; under 500 adds orc/skeleton; under 1000 adds
  --   druid/cleric; 1000+ adds elemental and orc captain; one rand draw for every tier but the
  --   first.
  -- tests: WalkerSpecials.archmage_illusion_family_follows_the_magic_pool_tier
  -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
  if mp_pool < 100 then
    person = LIVING_ELF
  elseif mp_pool < 250 then
    local tier_roll = og.rand(3)
    -- rule: The phantom's class widens with the magic left after the 500 price.
    -- tests: WalkerSpecials.archmage_illusion_family_follows_the_magic_pool_tier
    -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    else  -- C++ default (unreachable) -- plumbing: C++ switch default arms: og.rand(n) returns
      --   0..n-1, so tier_roll never exceeds the listed cases
      person = LIVING_SOLDIER
    end
  elseif mp_pool < 500 then
    local tier_roll = og.rand(5)
    -- rule: The phantom's class widens with the magic left after the 500 price.
    -- tests: WalkerSpecials.archmage_illusion_family_follows_the_magic_pool_tier
    -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    elseif tier_roll == 3 then
      person = LIVING_ORC
    elseif tier_roll == 4 then
      person = LIVING_SKELETON
    else  -- C++ default (unreachable) -- plumbing: C++ switch default arms: og.rand(n) returns
      --   0..n-1, so tier_roll never exceeds the listed cases
      person = LIVING_ARCHER
    end
  elseif mp_pool < 1000 then
    local tier_roll = og.rand(7)
    -- rule: The phantom's class widens with the magic left after the 500 price.
    -- tests: WalkerSpecials.archmage_illusion_family_follows_the_magic_pool_tier
    -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    elseif tier_roll == 3 then
      person = LIVING_ORC
    elseif tier_roll == 4 then
      person = LIVING_SKELETON
    elseif tier_roll == 5 then
      person = LIVING_DRUID
    elseif tier_roll == 6 then
      person = LIVING_CLERIC
    else  -- C++ default (unreachable) -- plumbing: C++ switch default arms: og.rand(n) returns
      --   0..n-1, so tier_roll never exceeds the listed cases
      person = LIVING_ARCHER
    end
  -- our maximum possible, insert before if needed
  else
    local tier_roll = og.rand(9)
    -- rule: The phantom's class widens with the magic left after the 500 price.
    -- tests: WalkerSpecials.archmage_illusion_family_follows_the_magic_pool_tier
    -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
    if tier_roll == 0 then
      person = LIVING_ELF
    elseif tier_roll == 1 then
      person = LIVING_SOLDIER
    elseif tier_roll == 2 then
      person = LIVING_ARCHER
    elseif tier_roll == 3 then
      person = LIVING_ORC
    elseif tier_roll == 4 then
      person = LIVING_SKELETON
    elseif tier_roll == 5 then
      person = LIVING_DRUID
    elseif tier_roll == 6 then
      person = LIVING_CLERIC
    elseif tier_roll == 7 then
      person = LIVING_ELEMENTAL
    elseif tier_roll == 8 then
      person = LIVING_ORC_CAPTAIN
    else  -- C++ default (unreachable) -- plumbing: C++ switch default arms: og.rand(n) returns
      --   0..n-1, so tier_roll never exceeds the listed cases
      person = LIVING_ARCHER
    end
  end
  -- Now make the guy we'd summon, at least physically
  local phantom = og.add_ob("living", person)
  -- plumbing: add_ob failure for the phantom: 'COULD NOT MAKE ILLUSION'.
  if not phantom then
    return false, "COULD NOT MAKE ILLUSION"
  end
  -- Place the illusion beside the caster on the caster's floor.
  phantom:set_floor(self:floor())
  -- Named "Phantom" below, but conjured ammunition must never fail a
  -- SAVE_ALL mission when it expires.
  phantom:set_summoned(true)
  local placed = false
  -- plumbing: 3x3 placement scan for the phantom; rule row below
  for i = -1, 1 do
    -- plumbing: 3x3 placement scan for the phantom; rule row below
    for j = -1, 1 do
      -- rule: The phantom stands in the first free neighbouring cell (same scan order as the
      --   elemental) as a level (L+2)/3, caster-team, owned decoy with image_lifetime(L), 1 max hp
      --   and 0 hp, no armour, magical, named 'Phantom', sharing the caster's foe; with no free
      --   cell the summon is cancelled.
      -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
      -- tests: WalkerSpecials.archmage_summon_image_drives_simulation
      -- tests: SemArchmage.a_summoned_image_shares_its_casters_foe_and_with_no_room_is_cancelled
      if not ((i == 0 and j == 0)
              or placed) then
        local testx = self:xpos() + (phantom:sizex() + 1) * i
        local testy = self:ypos() + (phantom:sizey() + 1) * j
        -- rule: The phantom stands in the first free neighbouring cell (same scan order as the
        --   elemental) as a level (L+2)/3, caster-team, owned decoy with image_lifetime(L), 1 max
        --   hp and 0 hp, no armour, magical, named 'Phantom', sharing the caster's foe.
        -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
        -- tests: WalkerSpecials.archmage_summon_image_drives_simulation
        -- tests: SemArchmage.a_summoned_image_shares_its_casters_foe_and_with_no_room_is_cancelled
        if og.query_passable(testx, testy, phantom) then
          placed = true
          phantom:setxy(testx, testy)
          phantom.level = (self.level + 2) // 3
          phantom:set_difficulty(phantom.level)
          phantom.team = self.team
          phantom:set_owner(self)
          phantom.lifetime = og.image_lifetime(self.level)
          phantom.max_hp = 1
          phantom.hp = 0
          phantom:s_set_armor(0)
          -- just to help out ..
          phantom.foe = self:foe()
          phantom:s_set_bit_flags(C.BIT_MAGICAL, 1)
          phantom:s_set_name("Phantom")
        end
      end
    end
  end
  -- rule: The phantom stands in the first free neighbouring cell (same scan order as the elemental)
  --   as a level (L+2)/3, caster-team, owned decoy with image_lifetime(L), 1 max hp and 0 hp, no
  --   armour, magical, named 'Phantom', sharing the caster's foe.
  -- tests: PackLuaArchmage.the_illusion_summoned_widens_with_the_magic_behind_it
  -- tests: WalkerSpecials.archmage_summon_image_drives_simulation
  -- tests: SemArchmage.a_summoned_image_shares_its_casters_foe_and_with_no_room_is_cancelled
  if not placed then
    phantom.dead = 1
    return false, "NO ROOM TO SUMMON"
  end
  -- Summoning takes lots of time :)
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 15.0)
  return true
end

local function mind_control(self)
  -- rule: MIND CONTROL refuses a busy archmage and needs a foe within 80 + 4L px; both refusals are
  --   free.
  -- tests: WalkerSpecials.archmage_mind_control_drives_simulation
  -- tests: SemArchmage.mind_control_refuses_a_busy_archmage_and_one_with_nobody_in_reach_free
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  local mp_after_base_cost = lc.spare_mp(self, self:current_special())
  local foes, foe_count = og.find_foes_in_range(
    "ob",
    t.mind_control_range_base + t.mind_control_range_per_level * self.level,
    self)
  if foe_count < 1 then
    return false, "NO FOE IN RANGE"
  end
  local controlled = 0
  local budget = mp_after_base_cost + 10
  -- Gameplay Fixes: a hero a seat drives resists, as Possess's does.
  local fixes = gf.on()
  -- plumbing: foe iteration under the MP budget; rule rows below
  for i = 1, #foes do
    -- rule: MIND CONTROL takes foes while its budget lasts: the first is covered by the base price,
    --   each further one costs 10 MP of the spare pool; when the budget is gone the rest are left
    --   alone.
    -- tests: WalkerSpecials.archmage_mind_control_stats_name_path
    -- tests: SemArchmage.mind_control_takes_one_foe_on_the_price_and_one_more_per_10_spare
    if budget < 10 then
      break
    end
    local foe = foes[i]
    -- never been charmed
    -- rule: MIND CONTROL takes only never-charmed living foes with no charm running (and, with
    --   Gameplay Fixes, not a seated hero); others are skipped without spending budget.
    -- tests: GameplayFixesBots.mind_control_skips_a_seated_hero_with_the_setting_on
    -- tests: parity:archmage_bot_mind_control_hero_scen99
    -- tests: parity:archmage_bot_mind_control_hero_fixes_scen99
    -- tests: SemArchmage.mind_control_skips_the_charmed_and_generators_without_spending
    if gf.charmable(foe, fixes) and foe:charm_left() <= 10 then
      budget = budget - 10
      local level_edge = self.level - foe.level
      -- C++ `generic < 0 || !rng.next(20)` (generic = the level edge) —
      -- short-circuit preserved: rand(20) is drawn only when the edge
      -- is >= 0.
      local confused
      -- rule: A foe above the archmage's level always resists (no draw) and an equal/lower one
      --   resists 1 in 20; a resisting foe goes berserk onto a random team (rand(8)) for
      --   charm_duration(edge) ticks; a controlled foe joins the archmage's team with no foe, for
      --   charm_duration(edge) ticks. Either way its real team is remembered.
      -- tests: PackLuaArchmage.an_undominated_victim_goes_berserk_rather_than_joining
      -- tests: PackLuaArchmage.a_higher_level_berserk_lasts_25_ticks
      -- tests: PackLuaArchmage.the_berserk_draw_count_is_one_on_both_values
      -- tests: WalkerSpecials.archmage_mind_control_drives_simulation
      -- tests: parity:archmage_mind_control_team_flip_scen99
      -- tests: SemArchmage.an_equal_level_foe_that_rolls_zero_resists_mind_control_and_goes_berserk
      if level_edge < 0 then
        confused = true
      else
        confused = (og.rand(20) == 0)
      end
      if confused then
        -- Resisted proper control: berserk onto a random team.
        foe:set_real_team_num(foe.team)
        local colour = og.rand(8)
        -- rule: Classic: a berserk foe's random colour can land on the archmage's own team, handing
        --   it over anyway. Gameplay Fixes: berserk always means someone else's side (own colour
        --   becomes the next one, 7 wraps to 0).
        -- tests: PackLuaArchmage.an_undominated_victim_never_joins_the_caster
        -- tests: PackLuaArchmage.the_berserk_draw_count_is_one_on_both_values
        -- tests: parity:archmage_mind_control_berserk_scen99
        -- tests: parity:archmage_mind_control_berserk_fixes_scen99
        if gf.on() and colour == self.team then
          -- Gameplay Fixes: berserk means someone else's side, never ours.
          colour = og.mod(colour + 1, 8)
        end
        foe.team = colour
        foe:set_charm_left(og.charm_duration(level_edge))
      else
        foe:set_real_team_num(foe.team)
        foe.team = self.team
        -- allow choice of new foe
        foe.foe = nil
        foe:set_charm_left(og.charm_duration(level_edge))
      end
      controlled = controlled + 1
    end
  end
  -- rule: MIND CONTROL with foes in reach but none it can take fails free.
  -- tests: GameplayFixesBots.mind_control_skips_a_seated_hero_with_the_setting_on
  -- tests: parity:archmage_bot_mind_control_hero_fixes_scen99
  -- tests: WalkerSpecials.archmage_mind_control_stats_name_path
  if controlled == 0 then
    return false, "NO VALID TARGET"
  end
  og.emit_notification(string.format(
    "%s has controlled %d men",
    og.entity_display_name(self, "ArchMage"), controlled))
  -- Target budget starts at mp_after_base_cost + 10, so the first
  -- control is covered by base special cost and extras cost 10 MP each.
  -- rule: Each foe controlled beyond the first costs 10 MP more, never more than the pool left
  --   after the base price.
  -- tests: WalkerSpecials.archmage_mind_control_stats_name_path
  -- tests: SemArchmage.mind_control_takes_one_foe_on_the_price_and_one_more_per_10_spare
  if mp_after_base_cost > 0 and controlled > 1 then
    local spend = (controlled - 1) * 10
    spend = og.min(spend, mp_after_base_cost)
    -- magicpoints is a C++ float: per-op rounding.
    self.magicpoints = og.fsub(self.magicpoints, spend)
  end
  -- busy is a C++ float: per-op rounding.
  self.busy = og.fadd(self:busy(), 10.0)
  return true
end

og.family("living", {
  id = "core:archmage",
  wire_id = 17,
  name = "ARCHMAGE",
  short_name = og.NIL,
  stats  = { strength = 4, dexterity = 6, constitution = 4,
             intelligence = 16, armor = 5, level = 1 },
  combat = { hp = 150, melee_damage = 8, stepsize = 3,
             fire_delay = 1, fire_mp_cost = 12 },
  costs  = { hire = 450,
             train = { strength = 30, dexterity = 20, constitution = 25,
                       intelligence = 7, armor = 55, level = 200 } },
  specials = {
    { id = "teleport",     name = "TELEPORT",     mp_cost = 10,  alternate = { name = "TELEPORT MARKER", detail = "A return point, 75 Int.", icon = "icons/teleport_marker.png" }, cast = teleport, ai = ai_teleport, detail = "Any mage can teleport randomly away easily.", icon = "icons/teleport.png" },
    { id = "heartburst",   name = "HEARTBURST",   mp_cost = 80,  alternate = { name = "CHAIN LIGHTNING", detail = "Hops from foe to foe.", icon = "icons/chain_lightning.png" }, cast = burst_or_chain, ai = ai_burst_or_chain, detail = "Burst your enemies into flame around you.", icon = "icons/heartburst.png" },
    { id = "summon_image", name = "SUMMON IMAGE", mp_cost = 500, alternate = { name = "SUMMON ELEMENTAL", detail = "A daemon fed with SP.", icon = "icons/summon_elemental.png" }, cast = summon_image, ai = ai_summon, detail = "Summon an illusionary ally to fight for you.", icon = "icons/summon_image.png" },
    { id = "mind_control", name = "MIND CONTROL", mp_cost = 150, cast = lc.with_rest(mind_control, 4, "charm_rest", "MIND CONTROL"), ai = lc.gate_with_rest(ai_mind_control, 4), detail = "Convert nearby foes to your team, for a time. Rests 20 s.", icon = "icons/mind_control.png" },
  },
  default_weapon = "core:fireball",
  flags = {},
  init_ani_type = 0,
  init_max_magicpoints = 0,
  leaves_bloodspot = true,
  magic_damage_modifier = 1,
  is_stationary = false,
  has_returning_weapon = false,
  is_undead = false,
  promotes_to = og.NIL,
  promotion_level_req = 0,
  death_message = "SOMEONE DIED",
  sprite = "archmage.png",
  animation = "mage",
  ai_line_of_sight = 10,
  description = "An Archmage takes the learnings of the Magi one step further, possessing extraordinary firepower at the expense of physical weakness.",
  names = { "Gandalf", "Saruman", "Radagast", "Alatar", "Pallando",
            "Raistlin", "Fizban", "Mordenkainen", "Merlin", "Harry",
            "Manannan", "Mordack", "Jace" },
  playable = false,
  playable_order = 999,
  glyph = "M",
  glyph_ascii = "M",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- True summon: myguy Intelligence requirement
    summon_int_req = 150,
    -- Mind control reach (px): base + per-level
    mind_control_range_base = 80,
    mind_control_range_per_level = 4,
    -- Chain-lightning / heartburst acquisition radii (px); both gain
    -- radius_bonus_per_level per caster level on top
    chain_radius_base = 200,
    chain_radius_per_level = 5,
    burst_radius_base = 80,
    radius_bonus_per_level = 2,
    -- MIND CONTROL rests this many ticks between casts, whatever the New Specials setting
    charm_rest = 240,
  },

  hit_response = hit_response,
  level_up = level_up,
  on_act_living = on_act_living,
  on_fire_weapon = on_fire_weapon,
  handle_teleport = lc.handle_teleport,
})
