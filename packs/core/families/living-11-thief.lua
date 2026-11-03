-- core:thief — bomb, cloak, taunt/charm, poison cloud (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- The shareware-era source marked Drop Bomb "unregistered", while Cloak
-- and Taunt were "Registered."

local C = og.C
local lc = og.use("living_common")
local FX_BOMB = assert(og.family_id("fx", "core:bomb"))
local FX_CLOUD = assert(og.family_id("fx", "core:cloud"))
local kt = og.use("kit_thief")
local FX_MINE = assert(og.family_id("fx", "core:mine"))
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local function check_special_ai(self)
  -- plumbing: slot dispatch of check_special_ai (slot 1 bomb/mine, slot 3 taunt/charm, else
  --   cloak/cloud); see below
  if self:current_special() == 1 then
    -- rule: With either Gameplay Fixes or New Specials on, a thief bot's DROP BOMB roll is judged
    --   by blast geometry (reach by level, no ally in the blast) before any classic rule; with both
    --   off the classic distance gate alone decides.
    -- tests: KitThief.ai_off_answers_the_classic_gate
    -- tests: GameplayFixesBots.thief_mine_waits_for_a_hunter_past_the_blast
    if gf.on() or og.match_setting("new_specials") ~= 0 then
      local reach = G.blast_reach(self.level)
      local clear = G.blast_clear(self, self, reach, false)
      -- rule: New Specials: a thief bot lays a MINE (Shift 1) only when a foe within 130 px is
      --   hunting it from beyond the bomb's blast, it has fewer than three mines down, no ally is
      --   in the blast, and it can pay the mine's 70; otherwise Shift goes to 0 and the bomb rule
      --   decides.
      -- tests: GameplayFixesBots.thief_mine_waits_for_a_hunter_past_the_blast
      -- tests: KitThief.ai_mine_fires_for_a_hunter_past_the_blast_when_mana_allows
      if og.match_setting("new_specials") ~= 0 then
        -- New Specials: a MINE pays where a foe will walk: a foe past the
        -- blast and within 130 px that is hunting the thief, fewer than
        -- three mines down, no ally in the blast, and the 70 in hand.
        -- Never the coin.
        local hunter = G.hunted_by(self, 130)
        local mines = G.owned(self, C.ORDER_FX, FX_MINE)
        -- rule: New Specials: a thief bot lays a MINE (Shift 1) only when a foe within 130 px is
        --   hunting it from beyond the bomb's blast, it has fewer than three mines down, no ally is
        --   in the blast, and it can pay the mine's 70.
        -- tests: GameplayFixesBots.thief_mine_waits_for_a_hunter_past_the_blast
        -- tests: KitThief.ai_mine_fires_for_a_hunter_past_the_blast_when_mana_allows
        if clear and hunter
            and self:distance_to_ob(hunter) > reach
            and mines < 3
            and G.can_pay(self, 1, true) then
          self:set_shifter_down(1)
          return true
        end
        self:set_shifter_down(0)
      end
      -- rule: Gameplay Fixes: a thief bot drops a bomb only with a living foe inside the bomb's
      --   blast reach and no ally in it; the Shift is forced to 0 (bomb).
      -- tests: GameplayFixesBots.thief_bomb_wants_a_foe_inside_its_blast_and_no_ally
      -- tests: parity:thief_bot_bomb_far_fixes_scen99
      if gf.on() then
        -- Gameplay Fixes: a bomb pays with a foe inside its blast and no
        -- ally in it; the Shift is 0 whenever the setting is on (a MINE
        -- not in play is cast as the bomb anyway).
        self:set_shifter_down(0)
        -- rule: Gameplay Fixes: a thief bot drops a bomb only with a living foe inside the bomb's
        --   blast reach and no ally in it; the Shift is forced to 0 (bomb).
        -- tests: GameplayFixesBots.thief_bomb_wants_a_foe_inside_its_blast_and_no_ally
        -- tests: parity:thief_bot_bomb_far_fixes_scen99
        return G.foes_within(self, 1, reach, true) and clear
      end
    end
    -- Drop-bomb AI uses this fixed per-tick range.
    local foe = self:foe()
    -- rule: Classic thief bomb gate: with a foe, the bomb is refused only while the foe is 36..129
    --   px away (point blank or 130+ px both bomb); with no foe it bombs when three or more foes
    --   stand within 110 px.
    -- tests: FamilyBehaviors.thief_batch3_check_special_ai_matrix
    -- tests: PackLuaThief.the_bomb_ai_allows_a_foe_outside_the_refusal_band
    -- tests: GameplayFixesBots.thief_bomb_wants_a_foe_inside_its_blast_and_no_ally
    -- tests: parity:thief_bot_bomb_far_scen99
    if foe then
      local distance = self:distance_to_ob(foe)
      -- about 6 squares max, 2 min
      -- rule: Classic thief bomb gate: with a foe, the bomb is refused only while the foe is
      --   36..129 px away (point blank or 130+ px both bomb).
      -- tests: FamilyBehaviors.thief_batch3_check_special_ai_matrix
      -- tests: PackLuaThief.the_bomb_ai_allows_a_foe_outside_the_refusal_band
      -- tests: GameplayFixesBots.thief_bomb_wants_a_foe_inside_its_blast_and_no_ally
      -- tests: parity:thief_bot_bomb_far_scen99
      if distance < 130 and distance > 35 then
        return false
      end
    else
      local _, foe_count = og.find_foes_in_range("ob", 110, self)
      return foe_count >= 3
    end
    return true -- fallthrough for foe case when distance is acceptable
  elseif self:current_special() == 3 then
    local t = og.tuning(self)
    -- rule: Gameplay Fixes: a busy thief bot never rolls TAUNT/CHARM (the casts refuse a busy
    --   thief).
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    if gf.on() then
      -- TAUNT and CHARM refuse a busy thief (the casts' guard).
      -- rule: Gameplay Fixes: a busy thief bot never rolls TAUNT/CHARM (the casts refuse a busy
      --   thief).
      -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
      if lc.is_busy(self) then
        return false
      end
      -- Gameplay Fixes: CHARM only with a foe the thief can take inside
      -- charm reach, else TAUNT; the coin is drawn either way.
      local reach = t.charm_range_base + t.charm_range_per_level * self.level
      local foes = og.find_foes_in_range("ob", reach, self)
      -- rule: Gameplay Fixes: a thief bot CHARMs (Shift 1) only a never-charmed, non-seated living
      --   foe at or below its level inside charm reach (16+4L); otherwise it TAUNTs (Shift 0), and
      --   only with a living foe inside taunt reach (80+4L) AND an ally there too.
      -- tests: GameplayFixesThiefBot.charms_an_equal_level_foe_in_reach
      -- tests: GameplayFixesThiefBot.taunts_a_foe_it_cannot_charm
      -- tests: GameplayFixesThiefBot.taunts_a_seated_hero_and_a_charmed_foe
      -- tests: GameplayFixesThiefBot.taunts_a_foe_beyond_charm_reach
      -- tests: GameplayFixesBots.thief_taunt_wants_a_foe_and_an_ally_in_reach
      -- tests: parity:thief_bot_charm_or_taunt_fixes_scen99
      if gf.nearest_charmable(self, foes, self.level) then
        self:set_shifter_down(1)
      else
        self:set_shifter_down(0)
        -- Gameplay Fixes: TAUNT wants a living foe in its reach and an ally
        -- there too: foes pulled onto a thief nobody backs is the thief's
        -- own danger, bought for 100. A tent cannot follow the thief.
        reach = t.taunt_range_base + t.taunt_range_per_level * self.level
        -- rule: Gameplay Fixes: a thief bot CHARMs (Shift 1) only a never-charmed, non-seated
        --   living foe at or below its level inside charm reach (16+4L).
        -- tests: GameplayFixesThiefBot.charms_an_equal_level_foe_in_reach
        -- tests: GameplayFixesThiefBot.taunts_a_foe_it_cannot_charm
        -- tests: GameplayFixesThiefBot.taunts_a_seated_hero_and_a_charmed_foe
        -- tests: GameplayFixesThiefBot.taunts_a_foe_beyond_charm_reach
        -- tests: GameplayFixesBots.thief_taunt_wants_a_foe_and_an_ally_in_reach
        -- tests: parity:thief_bot_charm_or_taunt_fixes_scen99
        if not G.foes_within(self, 1, reach, false) then
          return false
        end
        return G.allies_within(self, 1, reach)
      end
    end
    local range
    -- rule: Classic: a thief bot casts slot 3 whenever at least one foe is in the reach of the arm
    --   the engine coin chose (taunt 80+4L, charm 16+4L).
    -- tests: FamilyBehaviors.thief_batch3_check_special_ai_matrix
    if self:shifter_down() == 0 then
      range = t.taunt_range_base + t.taunt_range_per_level * self.level
    else
      range = t.charm_range_base + t.charm_range_per_level * self.level
    end
    local _, foe_count = og.find_foes_in_range("ob", range, self)
    return foe_count >= 1
  elseif gf.on() then  -- rule: Classic: CLOAK and POISON CLOUD have no bot gate (cast on every
    --   roll); with Gameplay Fixes each asks for its payoff.
    -- tests: FamilyBehaviors.thief_batch3_check_special_ai_matrix
    -- tests: parity:thief_bot_cloud_alone_scen99
    -- tests: GameplayFixesBots.thief_cloak_wants_a_hunter_and_no_cloak_left
    -- tests: GameplayFixesBots.thief_poison_cloud_wants_two_foes_at_blows_and_no_cloud
    -- rule: Gameplay Fixes: a thief bot CLOAKs only when no cloak is left on it and a foe within
    --   130 px is hunting it.
    -- tests: GameplayFixesBots.thief_cloak_wants_a_hunter_and_no_cloak_left
    if self:current_special() == 2 then
      -- Gameplay Fixes: CLOAK wants a foe hunting the thief and no cloak
      -- left.
      -- rule: Gameplay Fixes: a thief bot CLOAKs only when no cloak is left on it and a foe within
      --   130 px is hunting it.
      -- tests: GameplayFixesBots.thief_cloak_wants_a_hunter_and_no_cloak_left
      if self:invisibility_left() ~= 0 then
        return false
      end
      return G.hunted_by(self, 130) ~= nil
    end
    -- Gameplay Fixes: POISON CLOUD wants two living foes at blows and no
    -- cloud of the thief's alive, and the thief not busy (the cast's guard).
    -- rule: Gameplay Fixes: a thief bot releases POISON CLOUD only when not busy, with no cloud of
    --   its own alive, and two or more living foes at blows with it.
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    -- tests: GameplayFixesBots.thief_poison_cloud_wants_two_foes_at_blows_and_no_cloud
    -- tests: parity:thief_bot_cloud_alone_fixes_scen99
    if lc.is_busy(self) or G.owned(self, C.ORDER_FX, FX_CLOUD) > 0 then
      return false
    end
    return #G.foes_at_blows(self) >= 2
  end
  return true -- default: go for it
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 12, 4, 8, 1)
end

local function drop_bomb(self)
  local bomb = og.add_ob("fx", FX_BOMB)
  -- plumbing: add_ob failure creating the bomb (object cap): cast fails 'COULD NOT CREATE BOMB'.
  if not bomb then
    return false, "COULD NOT CREATE BOMB"
  end
  bomb.ani_type = C.ANI_BOMB
  -- rule: A hero's bomb counts as one shot in its total and scenario shot tallies (accuracy stats);
  --   a monster thief keeps no tally.
  -- tests: SemThief.a_heros_bomb_counts_one_shot_and_a_monster_keeps_no_tally
  if self:has_guy() then
    self:g_set_total_shots(self:g_total_shots() + 1)
    self:g_set_scen_shots(self:g_scen_shots() + 1)
  end
  bomb.damage = og.combat.bomb_damage(self.level)
  bomb:set_floor(self:floor()) -- bomb is armed on the thief's floor
  bomb:setxy(self:xpos() + self:sizex() // 2
               - bomb:sizex() // 2,
             self:ypos() + self:sizey() // 2
               - bomb:sizey() // 2)
  bomb:set_owner(self)
  -- An AI thief flees its own armed bomb.
  -- rule: An AI thief flees its own freshly armed bomb: it is forced to walk 20 steps along a
  --   random direction (two og.rand(3) draws, x first); a still roll becomes a step right. A
  --   player-driven thief is not moved.
  -- tests: SemThief.an_ai_thief_flees_its_own_bomb_and_a_player_thief_does_not
  if self:user() == -1 then
    local flee_dx = og.rand(3) - 1
    local flee_dy = og.rand(3) - 1
    -- rule: An AI thief flees its own freshly armed bomb: it is forced to walk 20 steps along a
    --   random direction (two og.rand(3) draws, x first); a still roll becomes a step right.
    -- tests: SemThief.an_ai_thief_flees_its_own_bomb_and_a_player_thief_does_not
    if flee_dx == 0 and flee_dy == 0 then
      flee_dx = 1
    end
    self:s_force_command(C.COMMAND_WALK, 20, flee_dx, flee_dy)
  end
  return true
end

local function cloak(self)
  local t = og.tuning(self)
  local cur = self:invisibility_left()
  local gain = t.cloak_base + og.rand(t.cloak_roll_span) * self.level
  self:set_invisibility_left(og.combat.cloak_total(cur, gain))
  return true
end

local function taunt_or_charm(self)
  local t = og.tuning(self)
  -- rule: THIEF slot 3 is TAUNT unshifted and CHARM OPPONENT with Shift held.
  -- tests: WalkerSpecials.thief_taunt_drives_simulation
  -- tests: WalkerSpecials.thief_charm_drives_simulation
  -- tests: FamilyBehaviors.family_batch4_soldier_orc_thief_edge_callbacks
  -- tests: GameplayFixes.a_thief_bot_taunts_instead_of_a_doomed_charm
  if self:shifter_down() == 0 then
    -- normal taunt
    -- rule: A busy thief cannot taunt.
    -- tests: FamilyBehaviors.thief_batch3_special_taunt_charm_and_poison_paths
    -- tests: WalkerSpecials.thief_taunt_drives_simulation
    if lc.is_busy(self) then
      return false, "SPECIAL BUSY"
    end
    local foes = og.foes_in_range(
      self, t.taunt_range_base + t.taunt_range_per_level * self.level)
    -- rule: Classic: a taunt with nobody in reach still shouts and costs its 100 MP. Gameplay
    --   Fixes: a taunt nobody hears is refused for free.
    -- tests: parity:thief_taunt_no_foe_scen99, parity:thief_taunt_no_foe_fixes_scen99
    -- tests: GameplayFixesThief.taunt_refuses_with_no_foe_when_on
    -- tests: WalkerSpecials.thief_taunt_drives_simulation
    if gf.on() and #foes == 0 then
      -- Gameplay Fixes: a taunt nobody hears costs nothing.
      return false, "NO FOE IN RANGE"
    end
    -- plumbing: iterates foes in taunt reach; rule rows below
    for i = 1, #foes do
      local foe = foes[i]
      -- Two RNG draws in one C++ expression; drawn left (self) first.
      -- (Bounds lean on the engine invariant level >= 1; the bare og.rand
      -- is the loud tripwire if that ever breaks.)
      local my_roll = og.rand(self.level)
      local foe_roll = og.rand(foe.level)
      -- rule: A taunt takes each foe in reach whose level roll (rand(foe level)) does not beat the
      --   thief's (rand(thief level), drawn first): a taunted foe targets the thief and follows it
      --   as leader.
      -- tests: WalkerSpecials.thief_taunt_drives_simulation
      -- tests: SemThief.a_taunt_takes_only_the_foes_whose_roll_does_not_beat_the_thiefs
      if my_roll >= foe_roll then
        foe:set_foe(self)
        -- "A hack, yeah": taunted foes also follow the thief as leader.
        foe:set_leader(self)
        -- rule: A taunted bot is also forced to FOLLOW the thief for 10+rand(L) steps; a
        --   player-controlled foe (ACT_CONTROL) only has its target and leader changed, its
        --   movement stays the player's.
        -- tests: WalkerSpecials.thief_taunt_drives_simulation
        -- tests: SemThief.a_taunted_player_foe_keeps_its_own_movement
        if foe:act_type() ~= C.ACT_CONTROL then
          foe:s_force_command(C.COMMAND_FOLLOW,
                              10 + og.rand(self.level), 0, 0)
        end
      end
    end
    og.emit_notification(og.entity_display_name(self, "THIEF")
                           .. ": 'Nyah Nyah!'")
    -- shim kept: busy is a C++ float: per-op float rounding.
    self:set_busy(og.fadd(self:busy(), 2.0))
    return true
  end
  -- charm opponent
  -- rule: A busy thief cannot charm.
  -- tests: FamilyBehaviors.thief_batch3_special_taunt_charm_and_poison_paths
  -- tests: WalkerSpecials.thief_charm_drives_simulation
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local foes, foe_count = og.find_foes_in_range(
    "ob", t.charm_range_base + t.charm_range_per_level * self.level, self)
  -- rule: CHARM needs a foe (living or generator) within 16+4L px; with none the cast fails free on
  --   both values.
  -- tests: FamilyBehaviors.thief_batch3_special_taunt_charm_and_poison_paths
  -- tests: WalkerSpecials.thief_charm_drives_simulation
  if foe_count < 1 then
    return false, "NO FOE IN RANGE"
  end
  local fixes = gf.on()
  local candidates = foes
  -- rule: Classic: CHARM takes the first charmable foe in list order. Gameplay Fixes: it takes the
  --   nearest charmable foe (any level), and a seated hero is never charmable.
  -- tests: GameplayFixesThief.charm_takes_the_nearest_foe_when_on
  -- tests: GameplayFixesThief.charm_skips_a_seated_hero_when_on
  if fixes then
    -- Gameplay Fixes: the nearest foe the thief can take, not the first
    -- in list order.
    candidates = {}
    local nearest = gf.nearest_charmable(self, foes, nil)
    -- rule: Classic: CHARM takes the first charmable foe in list order.
    -- tests: GameplayFixesThief.charm_takes_the_nearest_foe_when_on
    -- tests: GameplayFixesThief.charm_skips_a_seated_hero_when_on
    if nearest then
      candidates[1] = nearest
    end
  end
  local handled = 0
  local resisted = 0
  -- plumbing: candidate loop stops after the first handled target (one charm per cast); rule rows
  --   below
  for i = 1, #candidates do
    -- plumbing: candidate loop stops after the first handled target (one charm per cast); rule rows
    --   below
    if handled ~= 0 then
      break
    end
    local target = candidates[i]
    -- The old charm_left <= 10 gate was disabled; real_team_num is the
    -- remaining charm-history gate.
    -- rule: Only a never-charmed (real_team 255) living-order foe can be charmed (a tent cannot);
    --   with Gameplay Fixes a seated hero also cannot. An uncharmable foe is skipped.
    -- tests: GameplayFixesThief.charm_skips_a_seated_hero_when_on
    -- tests: GameplayFixesThief.charm_takes_the_npc_beside_a_seated_hero_when_on
    -- tests: parity:thief_bot_charm_hero_scen99
    if gf.charmable(target, fixes) then
      local level_diff = self.level - target.level
      -- rule: A foe above the thief's level always resists a charm (no draw); an equal or lower foe
      --   resists 1 time in 20. A resisted charm gives the foe a free attack on the thief; a charm
      --   flips the foe to the thief's team for a soft-kneed 75+25x(level diff) ticks, remembering
      --   its real team.
      -- tests: FamilyBehaviors.family_batch4_soldier_orc_thief_edge_callbacks
      -- tests: FamilyBehaviors.thief_batch3_special_taunt_charm_and_poison_paths
      -- tests: WalkerSpecials.thief_charm_drives_simulation, parity:thief_charm_opponent_scen99
      -- tests: GameplayFixesThief.charm_draws_once_on_both_values
      -- tests: SemThief.an_equal_level_foe_resists_a_charm_one_time_in_twenty
      if level_diff < 0 or og.rand(20) == 0 then
        -- A resisted charm gives the enemy a free attack.
        target:set_foe(self)
        target:attack(self)
        resisted = 1
      else
        target:set_real_team_num(target.team)
        target.team = self.team
        -- rule: A charmed foe drops its quarry: if it was the thief's own target it gets no foe
        --   (picks anew); otherwise it takes the thief's current target.
        -- tests: WalkerSpecials.thief_charm_drives_simulation
        -- tests: SemThief.a_charmed_foe_takes_the_thiefs_target_unless_it_was_that_target
        if self:foe() == target then
          target:set_foe(nil)
        else
          target:set_foe(self:foe())
        end
        -- The soft-knee formula is identity through level diff 12. Its
        -- knee and ceiling are engine combat policy, not pack tuning.
        target:set_charm_left(og.soften(
          t.charm_duration_base + level_diff * t.charm_duration_per_diff,
          375, 490))
        resisted = 0
      end
      handled = handled + 1
    end
  end
  -- rule: A charm with foes in reach but none charmable fails free with 'NO VALID TARGET'.
  -- tests: GameplayFixesThief.charm_skips_a_seated_hero_when_on
  -- tests: WalkerSpecials.thief_charm_drives_simulation
  if handled == 0 then
    return false, "NO VALID TARGET"
  end
  local message
  -- rule: The charm's notification says whether it took or was resisted.
  -- tests: GameplayFixes.a_thief_bot_taunts_instead_of_a_doomed_charm
  -- tests: GameplayFixes.a_driven_hero_resists_a_bot_charm
  if resisted ~= 0 then
    message = og.entity_display_name(self, "Thief")
                .. " failed to charm!"
  else
    message = og.entity_display_name(self, "Thief")
                .. " charmed an opponent!"
  end
  og.emit_notification(message)
  -- shim kept: busy is a C++ float: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), 10.0))
  return true
end

local function poison_cloud(self)
  -- rule: A busy thief cannot release POISON CLOUD.
  -- tests: FamilyBehaviors.thief_batch3_special_taunt_charm_and_poison_paths
  -- tests: WalkerSpecials.thief_poison_cloud_drives_simulation
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  local cloud = og.summon(self, "fx", FX_CLOUD)
  -- plumbing: og.summon failure for the cloud (object cap): 'COULD NOT CREATE CLOUD'.
  if not cloud then
    return false, "COULD NOT CREATE CLOUD"
  end
  -- shim kept: busy is a C++ float: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), 5.0))
  cloud:set_ignore(1)
  cloud.lifetime =
    t.cloud_lifetime_base + t.cloud_lifetime_per_level * self.level
  cloud:set_invisibility_left(t.cloud_invisibility)
  cloud.ani_type = C.ANI_SPIN
  cloud.damage = self.level
  return true
end

og.family("living", {
  id = "core:thief",
  wire_id = 11,
  name = "THIEF",
  short_name = og.NIL,
  stats  = { strength = 9, dexterity = 12, constitution = 12,
             intelligence = 10, armor = 5, level = 1 },
  combat = { hp = 75, melee_damage = 12, stepsize = 5,
             fire_delay = 5, fire_mp_cost = 1 },
  costs  = { hire = 400,
             train = { strength = 15, dexterity = 6, constitution = 9,
                       intelligence = 10, armor = 50, level = 200 } },
  specials = {
    { id = "drop_bomb",    name = "DROP BOMB",    mp_cost = 35,  alternate = { name = "MINE", mp_cost = 70, new_kit = true, detail = "Waits unseen for a foe.", icon = "icons/mine.png" }, cast = kt.bomb_or_mine(drop_bomb), detail = "Leave a burning bomb to explode and hurt the unwary, friend or foe!", icon = "icons/drop_bomb.png" },
    { id = "cloak",        name = "CLOAK",        mp_cost = 125, cast = cloak, detail = "Cloak yourself in the shadows, slipping past your enemies.", icon = "icons/cloak.png" },
    { id = "taunt_enemy",  name = "TAUNT ENEMY",  mp_cost = 100, alternate = { name = "CHARM OPPONENT", detail = "One foe fights for you.", icon = "icons/charm_opponent.png" }, cast = taunt_or_charm, detail = "Beckon your enemies to you with jeers, and confuse their attack.", icon = "icons/taunt_enemy.png" },
    { id = "poison_cloud", name = "POISON CLOUD", mp_cost = 150, detail = "Release a cloud of poisonous gas to roam at will and sicken your foes.", icon = "icons/poison_cloud.png" },
    default_cast = poison_cloud,  -- case 4 and every unmapped slot
  },
  default_weapon = "core:knife",
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
  death_message = "THIEF KILLED",
  sprite = "thief.png",
  animation = "standard",
  ai_line_of_sight = 10,
  description = "Thieves are fast, though not so potent as the soldier. Thieves can throw small blades rapidly and damage whole groups of enemies with their bombs.",
  names = { "Shinobi", "Dismas", "Shadow", "Stabby", "Swiftstrike", "Scourge",
            "Rogue" },
  playable = true,
  playable_order = 7,
  glyph = "t",
  glyph_ascii = "t",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Taunt / charm reach (px): base + per-level
    taunt_range_base = 80,
    taunt_range_per_level = 4,
    charm_range_base = 16,
    charm_range_per_level = 4,
    -- Cloak: invisibility gain = base + rand(roll_span)*level
    cloak_base = 20,
    cloak_roll_span = 20,
    -- Charm duration fed to the engine soft-knee: base + per level-diff
    charm_duration_base = 75,
    charm_duration_per_diff = 25,
    -- Poison cloud: lifetime base + per-level, starting invisibility
    cloud_lifetime_base = 40,
    cloud_lifetime_per_level = 3,
    cloud_invisibility = 10,
  },

  check_special_ai = check_special_ai,
  level_up = level_up,
})
