-- core:cleric — heal/mace, raise skeleton/ghost, turn undead, resurrect (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local lc = og.use("living_common")
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local FX_MAGIC_SHIELD = assert(og.family_id("fx", "core:magic_shield"))
local LIVING_SKELETON = assert(og.family_id("living", "core:skeleton"))
local LIVING_GHOST = assert(og.family_id("living", "core:ghost"))

-- og::combat::kMaceLifeCap (mystic mace lifetime bound; binds above 738 MP).
local MACE_LIFE_CAP = 468

-- The INT a TURN UNDEAD needs. A hero below it is refused "60 INT REQUIRED"
-- (and pays 5 busy); with the fixes on the bot gate asks the same question,
-- so it never picks a cast its hero cannot make.
local function lacks_turn_int(self, t)
  -- rule: Only a hero needs 60 INT to TURN UNDEAD; a bot cleric always has the INT. Shared by the
  --   cast (below) and the fixed bot gate (below).
  -- tests: GameplayFixesClericBot.turns_with_an_undead_foe_in_reach
  -- tests: GameplayFixesClericBot.raises_when_its_hero_lacks_the_int_to_turn
  -- tests: PackLuaCleric.int_refusals_return_reasons_without_emitting_notifications
  if not self:has_guy() then
    return false
  end
  return self:g_intelligence() < t.turn_undead_int_req
end

-- Shared turn-undead block: the case-2 and case-3 shifter_down bodies
-- were token-identical. Returns false where the legacy code returned
-- false out of do_special; true = the ladder's `return true`.
local function do_turn_undead(self)
  -- rule: TURN UNDEAD refuses 'SPECIAL BUSY' while busy > 0 (free).
  -- tests: PackLuaCleric.int_refusals_return_reasons_without_emitting_notifications
  -- tests: SemCleric.turn_undead_is_refused_for_free_while_busy
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  -- rule: A hero below 60 INT is refused '60 INT REQUIRED' and still pays 5 busy (no MP); no
  --   notification is emitted by the pack.
  -- tests: PackLuaCleric.int_refusals_return_reasons_without_emitting_notifications
  -- tests: SimInputUnit.intelligence_refusals_use_one_private_cue
  -- tests: FamilyCleric.r15_turn_undead_destroys_a_hostile_undead_in_range
  if lacks_turn_int(self, t) then
    -- busy is a C++ float: per-op rounding
    self:set_busy(og.fadd(self:busy(), 5.0))
    return false, string.format("%d INT REQUIRED", t.turn_undead_int_req)
  end
  -- Returns -1 when there are no candidates, -2 (the Gameplay Fixes
  -- setting on) when none of them is undead, otherwise the number destroyed.
  local turned = self:turn_undead(gf.turn_range(self, t), self.level)
  -- rule: TURN UNDEAD (reach 4*level px; with Gameplay Fixes never under 60 px) refuses free 'NO
  --   FOE IN RANGE' when no foe is in reach. Classic: foes in reach but none undead is a paid
  --   success that turns nobody (turned 0); Gameplay Fixes refuses it free 'NO UNDEAD IN RANGE'
  --   (-2).
  -- tests: GameplayFixesCleric.turn_undead_reaches_sixty_px_when_on
  -- tests: GameplayFixesCleric.turn_undead_refuses_without_undead_when_on
  -- tests: WalkerSpecials.turn_undead_without_undead_is_refused_with_gameplay_fixes_on
  -- tests: FamilyCleric.r15_turn_undead_destroys_a_hostile_undead_in_range
  -- tests: parity:cleric_turn_no_undead_scen99
  if turned == -1 then
    return false, "NO FOE IN RANGE"
  end
  if turned == -2 then
    return false, "NO UNDEAD IN RANGE"
  end
  -- rule: A hero who turns at least one undead gains experience (exp_from_action 'turn_undead', n)
  --   and gets the '<name> turned N undead.' notice; a bot or a hero who turned none gains nothing.
  -- tests: FamilyCleric.r15_turn_undead_destroys_a_hostile_undead_in_range
  -- tests: FamilyBehaviors.cleric_turn_undead_special2_and_3_grant_three_exp_per_kill
  -- tests: GameplayFixes.turn_undead_reaches_60_px
  -- tests: SemCleric.only_a_hero_who_turned_an_undead_is_rewarded_and_announced
  if self:has_guy() and turned ~= 0 then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, nil, "turn_undead", turned))
    -- rule: A hero who turns at least one undead gains experience (exp_from_action 'turn_undead',
    --   n) and gets the '<name> turned N undead.' notice.
    -- tests: FamilyCleric.r15_turn_undead_destroys_a_hostile_undead_in_range
    -- tests: FamilyBehaviors.cleric_turn_undead_special2_and_3_grant_three_exp_per_kill
    -- tests: GameplayFixes.turn_undead_reaches_60_px
    -- tests: SemCleric.only_a_hero_who_turned_an_undead_is_rewarded_and_announced
    if self.team == 0 or self:has_guy() then
      og.emit_notification(
        string.format("%s turned %d undead.", self:g_name(), turned))
    end
  end
  og.emit_positional_sound(self, C.SOUND_HEAL)
  return true
end

local corpses = og.use("corpses")
local function nearby_corpse(self, range, body)  -- body: the family to raise; nil for RESURRECT
  -- rule: Gameplay Fixes T: a raise/resurrect picks the nearest bloodstain within reach whose risen
  --   body (the family to raise; any for RESURRECT) can actually stand at a free spot on or beside
  --   the stain, draw-free and side-effect-free; a blocked near stain no longer hides a usable far
  --   one, a corpse against a wall or in a one-tile corridor can be raised, and the caster standing
  --   on it does not block it. Classic path is below.
  -- tests: GameplayFixesClericRaise.reporter_elf_against_a_wall_is_raised_with_the_setting_on
  -- tests: GameplayFixesClericRaise.reporter_one_tile_corridor
  -- tests: parity:cleric_raise_blocked_corpse_fixes_scen99
  if gf.on() then  -- Gameplay Fixes T: where the body can stand (lib/corpses.lua)
    return corpses.raise_spot(self, range, body)
  end
  local blood = og.find_nearest_blood(self)  -- the classic: the stain's own box, then the range
  -- rule: Classic corpse search: only the single nearest bloodstain counts; refused 'NO CORPSE
  --   NEARBY' if none, 'CORPSE IS BLOCKED' if its box is not passable (checked before range, with
  --   the loud passable probe), 'CORPSE TOO FAR' if it is not within reach; otherwise the body
  --   rises at the stain's corner.
  -- tests: PackLuaCleric.corpse_refusals_distinguish_absence_blockage_and_range
  -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
  -- tests: GameplayFixesClericRaise.a_blocked_near_stain_does_not_hide_a_usable_far_one
  -- tests: GameplayFixesClericRaise.the_fixed_probe_eats_nothing
  -- tests: parity:cleric_raise_blocked_corpse_scen99, parity:undead_no_corpse_raise_scen99
  if not blood then
    return nil, "NO CORPSE NEARBY"
  end
  local passable = og.query_passable(blood:xpos(), blood:ypos(), blood)
  if not passable then
    return nil, "CORPSE IS BLOCKED"
  end
  if self:distance_to_ob(blood) < range then
    return blood, nil, blood:xpos(), blood:ypos()  -- the spot is the corner, as always
  end
  return nil, "CORPSE TOO FAR"
end

local function heal_or_mace(self)
  local t = og.tuning(self)
  -- rule: Slot 1 unshifted is HEAL, shifted is the MYSTIC MACE.
  -- tests: FamilyCleric.r11_heal_and_mace_paths
  -- tests: WalkerSpecials.cleric_mystic_mace_low_int_and_success_paths
  if self:shifter_down() == 0 then
    -- normal heal
    local friends, friend_count =
      og.find_friends_in_range("ob", t.heal_range, self)
    -- no friends, so don't charge us
    -- rule: HEAL with no ally within 60 px is refused free 'NO ALLY IN RANGE'.
    -- tests: FamilyCleric.heal_refusal_distinguishes_absent_and_healthy_allies
    if friend_count == 0 then
      return false, "NO ALLY IN RANGE"
    end
    local healed, reason = 0, "NO ALLY NEEDS HEALING"
    -- rule: HEAL treats every ally within 60 px below full health, each with its own rolled amount
    --   and price; allies at full health are skipped and cost nothing.
    -- tests: FamilyCleric.heal_refusal_distinguishes_absent_and_healthy_allies
    -- tests: PackLuaCleric.a_heal_restores_a_wounded_friend_and_spends_magic
    -- tests: parity:special_cleric_heal_ally_scen99
    for i = 1, #friends do
      local ally = friends[i]
      -- rule: HEAL treats every ally within 60 px below full health, each with its own rolled
      --   amount and price; allies at full health are skipped and cost nothing.
      -- tests: FamilyCleric.heal_refusal_distinguishes_absent_and_healthy_allies
      -- tests: PackLuaCleric.a_heal_restores_a_wounded_friend_and_spends_magic
      -- tests: parity:special_cleric_heal_ally_scen99
      if ally.hp < ally.max_hp then
        -- og.heal_amount draws the same rng compute_heal_amount drew;
        -- its mp argument is the C++ (int) cast of the float pool.
        local amount, cost =
          og.heal_amount(og.trunc(self.magicpoints), self.level)
        -- mp_cost is a heal's floor; cost prices only the pool-scaled part, so cost 0 lands base + level*5.
        -- Didn't heal any for this guy
        -- rule: A heal whose rolled strength is zero or less stops the whole heal loop ('HEAL TOO
        --   WEAK'); if nobody was healed yet that is the refusal reason.
        -- tests: FamilyCleric.zero_strength_heal_explains_why_a_wounded_ally_was_not_healed
        -- tests: PackLuaCleric.a_level_zero_caster_below_four_mp_heals_nobody
        if amount <= 0 then
          reason = "HEAL TOO WEAK"
          break
        end
        local before = ally.hp
        -- hp and magicpoints are C++ floats: per-op rounding
        ally.hp = og.fadd(ally.hp, amount)
        local landed = amount
        -- rule: Classic HEAL overheals: an ally's hp can exceed its max, and the floating number
        --   and the experience reward count the full rolled amount. Gameplay Fixes clamps hp at max
        --   and shows/rewards only what landed; the mana is still fully spent.
        -- tests: GameplayFixesCleric.heal_stops_at_full_health_when_on
        -- tests: GameplayFixesCleric.heal_reward_and_number_are_what_landed
        -- tests: GameplayFixesCleric.heal_below_full_health_is_unchanged
        -- tests: parity:special_cleric_heal_ally_fixes_scen99
        if gf.on() and ally.hp > ally.max_hp then
          -- Gameplay Fixes: never above full health; the number shown and
          -- the reward are what really landed. The mana is still spent.
          ally.hp = ally.max_hp
          landed = og.trunc(og.fsub(ally.max_hp, before))
        end
        self.magicpoints = og.fsub(self.magicpoints, cost)
        -- rule: A hero cleric gains experience per ally healed (exp_from_action 'heal', landed
        --   amount); a bot gains none.
        -- tests: GameplayFixesCleric.heal_reward_and_number_are_what_landed
        -- tests: FamilyCleric.heal_refusal_distinguishes_absent_and_healthy_allies
        if self:has_guy() then
          self:g_set_exp(self:g_exp() +
                         og.exp_from_action(self, ally, "heal", landed))
        end
        healed = healed + 1
        self:do_heal_effects(self, ally, landed)
      end
    end
    -- everyone was healthy; don't charge us
    -- A zero-strength heal also reaches this refusal.
    -- rule: If every ally in range was already at full health (or the heal was too weak) HEAL is
    --   refused free with that reason ('NO ALLY NEEDS HEALING' / 'HEAL TOO WEAK').
    -- tests: FamilyCleric.heal_refusal_distinguishes_absent_and_healthy_allies
    -- tests: FamilyCleric.zero_strength_heal_explains_why_a_wounded_ally_was_not_healed
    -- tests: SemCleric.a_refused_heal_is_silent
    if healed == 0 then
      return false, reason
    end
    local message
    -- rule: The heal notice says 'Cleric healed 1 man!' for one ally and 'Cleric healed N men!' for
    --   several.
    -- tests: GameLoop.zz_capture_cleric_low_magic_heal, GameplayFixes.heal_stops_at_full_health
    -- tests: ConceptCampaignTest.clinic_rig_heals_a_thin_pool_patient_through_the_engine
    -- tests: SemCleric.the_heal_notice_counts_one_man_or_several_men
    if healed == 1 then
      message = "Cleric healed 1 man!"
    else
      message = string.format("Cleric healed %d men!", healed)
    end
    -- rule: The heal notice is shown only for a player-team or hero cleric; an enemy bot's heal is
    --   silent. Same rule as living-00-soldier.lua.
    -- tests: ConceptCampaignTest.clinic_rig_heals_a_thin_pool_patient_through_the_engine
    -- tests: GameLoop.zz_capture_cleric_low_magic_heal, GameplayFixes.heal_stops_at_full_health
    -- tests: SemCleric.an_enemy_bot_heal_lands_without_a_notice
    if self.team == 0 or self:has_guy() then
      og.emit_notification(message)
    end
    og.emit_positional_sound(self, C.SOUND_HEAL)
    return true
  end
  -- mystic mace
  -- rule: MYSTIC MACE refuses 'SPECIAL BUSY' while busy > 0.
  -- tests: WalkerSpecials.cleric_mystic_mace_low_int_and_success_paths
  -- tests: FamilyCleric.r15_the_slot_price_is_the_floor_of_a_heal
  -- tests: SemCleric.mystic_mace_is_refused_while_busy_and_summons_nothing
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: Only a hero needs 50 INT for the MYSTIC MACE ('50 INT REQUIRED', free); bots always may.
  -- tests: WalkerSpecials.cleric_mystic_mace_low_int_and_success_paths
  -- tests: PackLuaCleric.int_refusals_return_reasons_without_emitting_notifications
  if self:has_guy() and self:g_intelligence() < t.mace_int_req then
    -- only players get this
    return false, string.format("%d INT REQUIRED", t.mace_int_req)
  end
  -- rule: A hero's MYSTIC MACE counts one shot in total_shots and scen_shots.
  -- tests: FamilyCleric.r14_lines_110_132_160_heal_plural_and_mystic_mace_branches
  -- tests: GameplayFixesBots.cleric_shoved_heals_with_the_setting_on
  -- tests: SemCleric.a_hero_mystic_mace_counts_one_shot
  if self:has_guy() then
    self:g_set_total_shots(self:g_total_shots() + 1)
    self:g_set_scen_shots(self:g_scen_shots() + 1)
  end
  -- All okay, let's summon!
  local mace = og.summon(self, "fx", FX_MAGIC_SHIELD)
  -- plumbing: og.summon MAGIC_SHIELD nil (object cap): 'COULD NOT CREATE MACE'.
  if not mace then
    return false, "COULD NOT CREATE MACE"
  end
  -- The old code used ani_type = 1 only as a dummy non-zero value.
  mace:set_ani_type(1)
  -- spare MP funds the mace; the spare can be negative: og.div is C
  -- trunc, not Lua floor
  local spare_mp = og.div(lc.spare_mp(self, self:current_special()), 2)
  local life = og.min(t.mace_life_base + spare_mp, MACE_LIFE_CAP)
  mace:set_lifetime(life)
  -- hp is a C++ float: per-op rounding; og.div halves as C trunc again
  mace.hp = og.fadd(mace.hp, og.div(spare_mp, 2))
  -- damage is a C++ float: the quarter is a genuine float division
  mace:set_damage(og.fadd(mace:damage(), og.fdiv(spare_mp, 4.0)))
  -- magicpoints and busy are C++ floats: per-op rounding
  -- "Remove those excess magic points :>" — the original mace surcharge.
  self.magicpoints = og.fsub(self.magicpoints, spare_mp)
  self:set_busy(og.fadd(self:busy(), 5.0))
  return true
end

local function raise_skeleton(self)
  -- rule: Slot 2 shifted is TURN UNDEAD; unshifted RAISE UNDEAD raises a skeleton. Same rule as
  --   below.
  -- tests: FamilyCleric.r15_turn_undead_destroys_a_hostile_undead_in_range
  -- tests: PackLuaCleric.int_refusals_return_reasons_without_emitting_notifications
  -- tests: WalkerSpecials.family_special_sweep_outcomes_with_gameplay_fixes_on
  -- tests: FamilyCleric.r11_turn_and_raise_paths
  if self:shifter_down() ~= 0 then
    return do_turn_undead(self)
  end
  -- raise skeleton at the nearest bloodstain
  local blood, reason, sx, sy = nearby_corpse(self, og.tuning(self).raise_skeleton_range, LIVING_SKELETON)
  -- rule: RAISE UNDEAD needs a raisable corpse within 60 px (nearby_corpse); otherwise refused free
  --   with the corpse reason. On success a skeleton of the cleric's team, level rand0(cleric
  --   level)+1 (difficulty-scaled), owned by the cleric, rises at the spot (on the stain's floor),
  --   the stain is consumed, and a hero gains exp.
  -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
  -- tests: PackLuaCleric.corpse_refusals_distinguish_absence_blockage_and_range
  -- tests: FamilyCleric.r11_turn_and_raise_paths
  -- tests: SemCleric.a_raised_skeleton_rises_at_one_to_the_cleric_level
  if not blood then
    return false, reason
  end
  local life = og.combat.skeleton_lifetime(self.level)
  local alive = self:do_summon(LIVING_SKELETON, life)
  -- plumbing: do_summon nil: 'COULD NOT RAISE SKELETON' (object cap).
  if not alive then
    return false, "COULD NOT RAISE SKELETON"
  end
  alive.team = self.team
  alive.level = og.rand0(self.level) + 1
  alive:set_difficulty(alive.level)
  alive:set_floor(blood:floor())  -- rise at the bloodstain's floor
  alive:setxy(sx, sy)
  alive:set_owner(self)
  blood:set_dead(1)
  -- rule: A hero gains exp for raising a skeleton (exp_from_action 'raise_skeleton'); bots do not.
  -- tests: FamilyCleric.r15_raise_and_resurrect_consume_blood_and_pay_their_own_exp
  -- tests: GameplayFixesClericRaise.the_fixed_probe_eats_nothing
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, alive, "raise_skeleton", 0))
  end
  return true
end

-- Raise ghosts ..
local function raise_ghost(self)
  -- rule: Slot 3 shifted is TURN UNDEAD (identical to slot 2's); unshifted RAISE GHOST. Same rule
  --   as above.
  -- tests: FamilyBehaviors.cleric_turn_undead_special2_and_3_grant_three_exp_per_kill
  -- tests: WalkerSpecials.family_special_sweep_outcomes_with_gameplay_fixes_on
  -- tests: FamilyCleric.r12_ghost_raise_and_resurrect_penalty_paths
  if self:shifter_down() ~= 0 then
    -- turn undead (identical block to special 2)
    return do_turn_undead(self)
  end
  -- raise ghost at the nearest bloodstain
  local blood, reason, sx, sy = nearby_corpse(self, og.tuning(self).raise_ghost_range, LIVING_GHOST)
  -- rule: RAISE GHOST needs a raisable corpse within 30 px; a ghost (level rand0(L)+1, cleric's
  --   team, owned) rises at the spot and the stain is consumed. Same shape as above.
  -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
  -- tests: FamilyCleric.r12_ghost_raise_and_resurrect_penalty_paths
  -- tests: PackLuaCleric.a_ghost_rises_from_a_bloodstain_in_reach, parity:cleric_raise_ghost_scen99
  if not blood then
    return false, reason
  end
  local life = og.combat.ghost_raise_lifetime(self.level)
  local alive = self:do_summon(LIVING_GHOST, life)
  -- plumbing: do_summon nil: 'COULD NOT CREATE GHOST'.
  if not alive then
    return false, "COULD NOT CREATE GHOST"
  end
  alive.level = og.rand0(self.level) + 1
  alive:set_difficulty(alive.level)
  alive.team = self.team
  alive:set_floor(blood:floor())  -- rise at the bloodstain's floor
  alive:setxy(sx, sy)
  alive:set_owner(self)
  blood:set_dead(1)
  -- rule: A hero gains exp for raising a ghost; bots do not. Same rule as above.
  -- tests: FamilyCleric.r12_ghost_raise_and_resurrect_penalty_paths
  -- tests: GameplayFixesClericRaise.the_fixed_probe_eats_nothing
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, alive, "raise_ghost", 0))
  end
  return true
end

-- Resurrect our guys ..
local function resurrect(self)
  local t = og.tuning(self)
  local blood, reason, sx, sy = nearby_corpse(self, t.resurrect_range, nil)
  -- rule: RESURRECT needs a corpse within 30 px (any family; nearby_corpse body nil), refused free
  --   otherwise.
  -- tests: GameplayFixesClericRaise.resurrect_takes_the_nearest_usable_stain_whatever_its_team
  -- tests: GameplayFixesClericRaise.refusals_spend_nothing_on_both_values
  if not blood then
    return false, reason
  end
  local alive
  -- rule: RESURRECT on a friendly corpse restores the dead walker's old family with its transferred
  --   stats at half max health on the corpse's team (heal effect shows the half); on a foe's corpse
  --   it raises a ghost for the cleric's team instead (lifetime 200, level rand0(L)+1, owned).
  --   Either way it rises on the stain's floor at the spot and the stain is consumed.
  -- tests: FamilyCleric.r11_resurrect_friendly_and_hostile_paths
  -- tests: PackLuaCleric.resurrecting_a_friendly_corpse_restores_its_family
  -- tests: parity:cleric_resurrect_friendly_scen99
  -- tests: WalkerSpecials.cleric_resurrect_raises_an_enemy_corpse_as_a_ghost
  -- tests: FamilyCleric.r14_resurrect_revives_friendly_blood_and_enslaves_hostile_blood
  -- tests: SemCleric.a_foe_corpse_resurrects_as_a_two_hundred_tick_ghost
  if self:is_friendly(blood) then
    -- normal resurrection
    -- Friendly resurrect: restore the corpse's pre-bloodstain family
    -- at half health.
    alive = og.add_ob("living", blood:s_old_family())
    -- plumbing: og.add_ob living nil: 'COULD NOT RESURRECT'.
    if not alive then
      return false, "COULD NOT RESURRECT"
    end
    -- restore our old values ..
    blood:transfer_stats(alive)
    -- max_hp is a C++ float: fdiv is a genuine float half
    alive.hp = og.fdiv(alive.max_hp, 2.0)
    -- the heal marker carries the same half, int16-narrowed (C++ short)
    local half = og.i16(og.trunc(og.fdiv(alive.max_hp, 2.0)))
    self:do_heal_effects(self, alive, half)
    alive.team = blood.team
    -- take some EXP away as penalty if we're a player
    -- rule: A hero who resurrects a friend pays an experience penalty (exp_from_action
    --   'resurrect_penalty', stored as an unsigned short: a negative value wraps to +65536),
    --   floored at zero experience; then the generic 'resurrect' reward is added (below).
    -- tests: FamilyCleric.r15_raise_and_resurrect_consume_blood_and_pay_their_own_exp
    -- tests: FamilyCleric.r14_resurrect_revives_friendly_blood_and_enslaves_hostile_blood
    -- tests: FamilyCleric.r12_ghost_raise_and_resurrect_penalty_paths
    -- tests: FamilyCleric.r11_resurrect_friendly_and_hostile_paths
    -- tests: FamilyBehaviors.cleric_resurrect_penalty_underflow_clamps_to_zero
    -- tests: GameplayFixesClericRaise.the_fixed_probe_eats_nothing
    -- tests: SemCleric.a_resurrect_penalty_past_a_short_wraps_back_to_its_unsigned_value
    if self:has_guy() then
      -- C++ stores the short return into an unsigned short (modular).
      local exp_loss =
        og.exp_from_action(self, blood, "resurrect_penalty", 0)
      -- rule: A hero who resurrects a friend pays an experience penalty (exp_from_action
      --   'resurrect_penalty', stored as an unsigned short.
      -- tests: FamilyCleric.r15_raise_and_resurrect_consume_blood_and_pay_their_own_exp
      -- tests: FamilyCleric.r14_resurrect_revives_friendly_blood_and_enslaves_hostile_blood
      -- tests: FamilyCleric.r12_ghost_raise_and_resurrect_penalty_paths
      -- tests: FamilyCleric.r11_resurrect_friendly_and_hostile_paths
      -- tests: FamilyBehaviors.cleric_resurrect_penalty_underflow_clamps_to_zero
      -- tests: GameplayFixesClericRaise.the_fixed_probe_eats_nothing
      -- tests: SemCleric.a_resurrect_penalty_past_a_short_wraps_back_to_its_unsigned_value
      if exp_loss < 0 then
        exp_loss = exp_loss + 65536
      end
      -- rule: A hero who resurrects a friend pays an experience penalty (exp_from_action
      --   'resurrect_penalty', stored as an unsigned short.
      -- tests: FamilyCleric.r15_raise_and_resurrect_consume_blood_and_pay_their_own_exp
      -- tests: FamilyCleric.r14_resurrect_revives_friendly_blood_and_enslaves_hostile_blood
      -- tests: FamilyCleric.r12_ghost_raise_and_resurrect_penalty_paths
      -- tests: FamilyCleric.r11_resurrect_friendly_and_hostile_paths
      -- tests: FamilyBehaviors.cleric_resurrect_penalty_underflow_clamps_to_zero
      -- tests: GameplayFixesClericRaise.the_fixed_probe_eats_nothing
      -- tests: SemCleric.a_resurrect_penalty_past_a_short_wraps_back_to_its_unsigned_value
      if self:g_exp() >= exp_loss then
        self:g_set_exp(self:g_exp() - exp_loss)
      else
        self:g_set_exp(0)
      end
    end
  else
    -- raise an opponent as undead
    alive = self:do_summon(LIVING_GHOST, t.resurrect_ghost_lifetime)
    -- plumbing: do_summon ghost nil: 'COULD NOT CREATE GHOST'.
    if not alive then
      return false, "COULD NOT CREATE GHOST"
    end
    alive.team = self.team
    alive.level = og.rand0(self.level) + 1
    alive:set_difficulty(alive.level)
    alive:set_owner(self)
  end
  alive:set_floor(blood:floor())  -- return at the bloodstain's floor
  alive:setxy(sx --[[@as number]], sy --[[@as number]])  -- a stain always comes with its spot
  blood:set_dead(1)
  -- rule: A hero gains the 'resurrect' exp reward for either resurrect arm; bots do not.
  -- tests: FamilyCleric.r12_ghost_raise_and_resurrect_penalty_paths
  -- tests: FamilyCleric.r15_raise_and_resurrect_consume_blood_and_pay_their_own_exp
  -- tests: GameplayFixesClericRaise.the_fixed_probe_eats_nothing
  if self:has_guy() then
    self:g_set_exp(self:g_exp() +
                   og.exp_from_action(self, alive, "resurrect", 0))
  end
  return true
end

-- Gameplay Fixes: the bot's slot 1. HEAL when an ally in reach is below
-- its full health; else the MYSTIC MACE when it pays: a living foe within
-- 48 px (the mace orbits on a 24 px circle), no mace of this cleric out, the
-- pool at least half full (the classic condition), a hero's INT and not busy
-- (the cast refuses SPECIAL BUSY, fix W). The Shift is written after the coin.
local function heal_or_mace_wanted(self, t)
  -- rule: Gameplay Fixes bot cleric slot 1 HEALs (shifter 0) whenever an ally within 60 px is below
  --   full health.
  -- tests: GameplayFixesBots.cleric_heal_wants_a_hurt_ally_within_60
  if G.hurt_ally(self, t.heal_range) then
    self:set_shifter_down(0)
    return true
  end
  -- rule: The fixed bot only maces with at least half its pool (the classic mace condition).
  -- tests: GameplayFixesBots.cleric_mace_wants_a_foe_at_its_orbit_and_no_mace_out
  if self.magicpoints < og.fdiv(self.max_magicpoints, 2) then
    return false
  end
  -- rule: The fixed bot gate never picks a mace its hero lacks the INT for.
  -- tests: GameplayFixesBots.cleric_mace_wants_a_foe_at_its_orbit_and_no_mace_out
  if self:has_guy() and self:g_intelligence() < t.mace_int_req then
    return false
  end
  -- rule: The fixed bot maces (shifter 1) only when not busy, with no mace of its own already out
  --   and a living foe within 48 px (twice the 24 px orbit) on its floor.
  -- tests: GameplayFixesBots.cleric_mace_wants_a_foe_at_its_orbit_and_no_mace_out
  -- tests: GameplayFixesBots.cleric_mace_waits_while_busy_with_the_setting_on
  -- tests: parity:cleric_bot_mace_alone_fixes_scen99
  if not lc.is_busy(self) and G.owned(self, C.ORDER_FX, FX_MAGIC_SHIELD) == 0
      and G.foes_within(self, 1, 48, true) then
    self:set_shifter_down(1)
    return true
  end
  return false
end

local function check_special_ai(self)
  -- rule: Classic bot cleric on slot 1 HEALs whenever any ally is within 60 px (even if nobody is
  --   hurt: the cast then refuses free), else maces with at least half its pool (no foe needed: it
  --   maces alone), else waits. Gameplay Fixes uses heal_or_mace_wanted.
  -- tests: GameplayFixesBots.cleric_heal_wants_a_hurt_ally_within_60
  -- tests: GameplayFixesBots.cleric_mace_wants_a_foe_at_its_orbit_and_no_mace_out
  -- tests: FamilyCleric.r11_check_ai_and_heal_fail_paths, parity:cleric_bot_mace_alone_scen99
  if self:current_special() == 1 then -- healing
    -- rule: Classic bot cleric on slot 1 HEALs whenever any ally is within 60 px (even if nobody is
    --   hurt.
    -- tests: GameplayFixesBots.cleric_heal_wants_a_hurt_ally_within_60
    -- tests: GameplayFixesBots.cleric_mace_wants_a_foe_at_its_orbit_and_no_mace_out
    -- tests: FamilyCleric.r11_check_ai_and_heal_fail_paths, parity:cleric_bot_mace_alone_scen99
    if gf.on() then
      return heal_or_mace_wanted(self, og.tuning(self))
    end
    local _, friend_count = og.find_friends_in_range(
      "ob", og.tuning(self).heal_range, self)
    -- other than ourselves?
    -- rule: Classic bot cleric on slot 1 HEALs whenever any ally is within 60 px (even if nobody is
    --   hurt.
    -- tests: GameplayFixesBots.cleric_heal_wants_a_hurt_ally_within_60
    -- tests: GameplayFixesBots.cleric_mace_wants_a_foe_at_its_orbit_and_no_mace_out
    -- tests: FamilyCleric.r11_check_ai_and_heal_fail_paths, parity:cleric_bot_mace_alone_scen99
    if friend_count > 0 then
      self:set_shifter_down(0) -- we're HEALING
      return true
    -- max_magicpoints is a C++ float: fdiv is a genuine float half
    elseif self.magicpoints >= og.fdiv(self.max_magicpoints, 2) then
      self:set_shifter_down(1) -- do mace
      return true
    end
    return false
  end
  -- rule: Gameplay Fixes bot cleric on RAISE UNDEAD/RAISE GHOST TURNs (shifter 1) only when it has
  --   the INT, is not busy, and an undead foe is within its turn reach (>= 60 px); otherwise it
  --   raises (shifter 0). The engine's Shift coin is drawn first and overwritten. Classic: slots
  --   2-4 always true with the coin's Shift.
  -- tests: GameplayFixesClericBot.turns_with_an_undead_foe_in_reach
  -- tests: GameplayFixesClericBot.raises_with_nothing_to_turn_in_reach
  -- tests: GameplayFixesClericBot.raises_when_its_hero_lacks_the_int_to_turn
  -- tests: GameplayFixesClericBot.leaves_the_resurrect_shift_and_maces_a_foe_in_orbit
  -- tests: GameplayFixesClericBot.the_coin_is_drawn_and_then_overridden
  -- tests: GameplayFixesBots.cleric_turn_undead_waits_while_busy_with_the_setting_on
  if gf.on() then
    local slot = self:current_special()
    -- rule: Gameplay Fixes bot cleric on RAISE UNDEAD/RAISE GHOST TURNs (shifter 1) only when it
    --   has the INT, is not busy, and an undead foe is within its turn reach (>= 60 px).
    -- tests: GameplayFixesClericBot.turns_with_an_undead_foe_in_reach
    -- tests: GameplayFixesClericBot.raises_with_nothing_to_turn_in_reach
    -- tests: GameplayFixesClericBot.raises_when_its_hero_lacks_the_int_to_turn
    -- tests: GameplayFixesClericBot.leaves_the_resurrect_shift_and_maces_a_foe_in_orbit
    -- tests: GameplayFixesClericBot.the_coin_is_drawn_and_then_overridden
    -- tests: GameplayFixesBots.cleric_turn_undead_waits_while_busy_with_the_setting_on
    if slot == 2 or slot == 3 then
      -- Gameplay Fixes: TURN UNDEAD only with something to turn in reach,
      -- the INT to cast it and not busy (fix W); else the raise (which
      -- refuses free when there is no corpse).
      local t = og.tuning(self)
      local reach = gf.turn_range(self, t)
      -- rule: Gameplay Fixes bot cleric on RAISE UNDEAD/RAISE GHOST TURNs (shifter 1) only when it
      --   has the INT, is not busy, and an undead foe is within its turn reach (>= 60 px).
      -- tests: GameplayFixesClericBot.turns_with_an_undead_foe_in_reach
      -- tests: GameplayFixesClericBot.raises_with_nothing_to_turn_in_reach
      -- tests: GameplayFixesClericBot.raises_when_its_hero_lacks_the_int_to_turn
      -- tests: GameplayFixesClericBot.leaves_the_resurrect_shift_and_maces_a_foe_in_orbit
      -- tests: GameplayFixesClericBot.the_coin_is_drawn_and_then_overridden
      -- tests: GameplayFixesBots.cleric_turn_undead_waits_while_busy_with_the_setting_on
      local can_turn = not lacks_turn_int(self, t) and not lc.is_busy(self)
      -- rule: Gameplay Fixes bot cleric on RAISE UNDEAD/RAISE GHOST TURNs (shifter 1) only when it
      --   has the INT, is not busy, and an undead foe is within its turn reach (>= 60 px).
      -- tests: GameplayFixesClericBot.turns_with_an_undead_foe_in_reach
      -- tests: GameplayFixesClericBot.raises_with_nothing_to_turn_in_reach
      -- tests: GameplayFixesClericBot.raises_when_its_hero_lacks_the_int_to_turn
      -- tests: GameplayFixesClericBot.leaves_the_resurrect_shift_and_maces_a_foe_in_orbit
      -- tests: GameplayFixesClericBot.the_coin_is_drawn_and_then_overridden
      -- tests: GameplayFixesBots.cleric_turn_undead_waits_while_busy_with_the_setting_on
      if can_turn and self:undead_foes_in_range(reach) > 0 then
        self:set_shifter_down(1)
      else
        self:set_shifter_down(0)
      end
      -- rule: Gameplay Fixes bot RAISE UNDEAD (60 px, skeleton) / RAISE GHOST (30 px, ghost) wants
      --   a corpse it can actually raise and a living foe within 130 px for the risen to fight.
      -- tests: GameplayFixesBots.cleric_raise_undead_wants_a_corpse_and_a_foe
      -- tests: GameplayFixesBots.cleric_raise_ghost_wants_a_corpse_within_30_and_a_foe
      -- tests: GameplayFixesBots.cleric_raise_undead_wants_a_corpse_it_can_raise
      if self:shifter_down() == 0 then
        -- Gameplay Fixes: the raise wants the corpse it would raise and a
        -- living foe within 130 px for the risen to fight.
        local range, body = t.raise_skeleton_range, LIVING_SKELETON
        -- rule: Gameplay Fixes bot RAISE UNDEAD (60 px, skeleton) / RAISE GHOST (30 px, ghost)
        --   wants a corpse it can actually raise and a living foe within 130 px for the risen to
        --   fight.
        -- tests: GameplayFixesBots.cleric_raise_undead_wants_a_corpse_and_a_foe
        -- tests: GameplayFixesBots.cleric_raise_ghost_wants_a_corpse_within_30_and_a_foe
        -- tests: GameplayFixesBots.cleric_raise_undead_wants_a_corpse_it_can_raise
        if slot == 3 then
          range, body = t.raise_ghost_range, LIVING_GHOST
        end
        -- rule: Gameplay Fixes bot RAISE UNDEAD (60 px, skeleton) / RAISE GHOST (30 px, ghost)
        --   wants a corpse it can actually raise and a living foe within 130 px for the risen to
        --   fight.
        -- tests: GameplayFixesBots.cleric_raise_undead_wants_a_corpse_and_a_foe
        -- tests: GameplayFixesBots.cleric_raise_ghost_wants_a_corpse_within_30_and_a_foe
        -- tests: GameplayFixesBots.cleric_raise_undead_wants_a_corpse_it_can_raise
        if corpses.raise_spot(self, range, body) == nil then
          return false
        end
        return G.foes_within(self, 1, 130, false)
      end
    elseif slot == 4 then  -- rule: Gameplay Fixes bot RESURRECT wants the nearest raisable corpse
      --   within 30 px to be friendly (a foe's stain is RAISE GHOST's job at a third of the price).
      -- tests: GameplayFixesBots.cleric_resurrect_wants_a_friendly_corpse_within_30
      -- tests: GameplayFixesBots.cleric_resurrect_wants_a_friendly_corpse_it_can_raise
      -- tests: parity:cleric_bot_resurrect_foe_stain_scen99
      -- Gameplay Fixes: RESURRECT wants a friendly corpse in reach (a foe's
      -- stain is RAISE GHOST's job, at a third of the price).
      local b = corpses.raise_spot(self, og.tuning(self).resurrect_range, nil)
      -- rule: Gameplay Fixes bot RESURRECT wants the nearest raisable corpse within 30 px to be
      --   friendly (a foe's stain is RAISE GHOST's job at a third of the price).
      -- tests: GameplayFixesBots.cleric_resurrect_wants_a_friendly_corpse_within_30
      -- tests: GameplayFixesBots.cleric_resurrect_wants_a_friendly_corpse_it_can_raise
      -- tests: parity:cleric_bot_resurrect_foe_stain_scen99
      return b ~= nil and self:is_friendly(b)
    end
  end
  return true
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 9.0, 12.0, 4.0, 0.5)
end

local function on_shoved(self)
  self:set_current_special(1) -- healing
  -- rule: A shoved cleric (an ally pushing at its side) switches to slot 1 and casts at once.
  --   Classic keeps whatever Shift the last coin left, so a shove can fire the MYSTIC MACE;
  --   Gameplay Fixes always HEALs.
  -- tests: GameplayFixesBots.cleric_shoved_heals_with_the_setting_on
  if gf.on() then
    -- Gameplay Fixes: a shove is an ally at the cleric's side: HEAL, never
    -- the mace the last coin may have left down.
    self:set_shifter_down(0)
  end
  self:special()
end

local function customize_weapon(self, weapon)
  weapon:set_ani_type(C.ANI_GLOWGROW)
  weapon:set_lifetime(weapon:lifetime() + og.combat.glow_bonus(self.level))
end

og.family("living", {
  id = "core:cleric",
  wire_id = 5,
  name = "CLERIC",
  short_name = og.NIL,
  stats  = { strength = 6, dexterity = 7, constitution = 6,
             intelligence = 14, armor = 7, level = 1 },
  combat = { hp = 120, melee_damage = 12, stepsize = 2,
             fire_delay = 7.5, fire_mp_cost = 8 },
  costs  = { hire = 400,
             train = { strength = 15, dexterity = 15, constitution = 9,
                       intelligence = 6, armor = 50, level = 200 } },
  specials = {
    { id = "heal",         name = "HEAL",         mp_cost = 2,   alternate = { name = "MYSTIC MACE", detail = "Orbiting mace, 50 Int.", icon = "icons/mystic_mace.png" }, cast = heal_or_mace, detail = "Heal nearby friends with all your SP.", icon = "icons/heal.png" },
    { id = "raise_undead", name = "RAISE UNDEAD", mp_cost = 20,  alternate = { name = "TURN UNDEAD", detail = "Turning needs 60 Int.", icon = "icons/turn_undead.png" }, cast = raise_skeleton, detail = "Raise the gore of any victim to a skeleton.", icon = "icons/raise_undead.png" },
    { id = "raise_ghost",  name = "RAISE GHOST",  mp_cost = 50,  alternate = { name = "TURN UNDEAD", detail = "Turning needs 60 Int.", icon = "icons/turn_undead.png" }, cast = raise_ghost, detail = "A more powerful raise, you can now get ghosts to fly and wail.", icon = "icons/raise_ghost.png" },
    { id = "resurrect",    name = "RESURRECT",    mp_cost = 150, detail = "Restores dead friends to life, or enemies to undead. Beware: this uses your own EXP to cast!", icon = "icons/resurrect.png" },
    default_cast = resurrect,  -- case 4 and every unmapped slot
  },
  default_weapon = "core:glow",
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
  death_message = "CLERIC DIED",
  sprite = "cleric.png",
  animation = "standard",
  ai_line_of_sight = 4,
  description = "Clerics, like mages, are slow, but have a stronger hand-to-hand attack. Clerics possess abilities related to healing and interaction with the dead.",
  names = { "Tuck", "Brother", "Pater", "Drake", "Friar", "Francis",
            "John Paul", "Medic" },
  playable = true,
  playable_order = 5,
  glyph = "c",
  glyph_ascii = "c",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Heal: ally acquisition radius (px), shared by the special and its AI gate
    heal_range = 60,
    -- Turn undead: range = per_level * level (px); myguy Int requirement
    turn_undead_range_per_level = 4,
    turn_undead_int_req = 60,
    -- Mystic mace: myguy Int requirement; lifetime base under the engine cap
    mace_int_req = 50,
    mace_life_base = 100,
    -- Corpse-raise reach (px) and the hostile-resurrect ghost lifetime
    raise_skeleton_range = 60,
    raise_ghost_range = 30,
    resurrect_range = 30,
    resurrect_ghost_lifetime = 200,
    -- Turn undead with the Gameplay Fixes setting on: the reach never falls
    -- under this (px), RAISE UNDEAD's own reach
    turn_undead_range_min = 60,
  },

  check_special_ai = check_special_ai,
  set_difficulty = set_difficulty,
  on_shoved = on_shoved,
  customize_weapon = customize_weapon,
})
