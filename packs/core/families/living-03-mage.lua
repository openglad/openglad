-- core:mage — teleport/marker, starburst, freeze time, wave, heartburst (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
-- rng: the mage's own hooks draw nothing directly.

local C = og.C
local lc = og.use("living_common")
local gf = og.use("gameplay_fixes")
local G = og.use("gates")
local FX_MARKER = assert(og.family_id("fx", "core:marker"))
local FX_EXPLOSION = assert(og.family_id("fx", "core:explosion"))
local WEAPON_WAVE = assert(og.family_id("weapon", "core:wave"))

-- og::combat::kStarburstAddCap (include/openglad/core/combat_math.h),
-- pure constant with no og.C binding: per-fireball damage add cap.
local STARBURST_ADD_CAP = 40

-- Gameplay Fixes: FREEZE TIME's cue is the fight the allies are in, not the
-- mage's own danger (that is TELEPORT's business at 15 magic). The 500 are
-- worth spending when the time stop hands allies free blows: three or more
-- foes within a blow of allies, or an ally at blows that is under a quarter
-- of its health. Distance to the mage is not consulted, and the same cue
-- serves a mage on any team (off the player's team the freeze grants its
-- own side bonus rounds instead).
-- The fight is the library's (G.allies_fight: foe-major, so a foe at
-- blows with two allies counts once; living foes only, a tent is nobody's
-- fight); draw-free, finders and positions only.
local function freeze_wanted(self)
  local engaged, hurt = G.allies_fight(self)
  -- rule: Gameplay Fixes bot FREEZE TIME is wanted when the mage's allies are in a fight worth
  --   stopping time for: three or more living foes within a blow of allies (each foe counted once
  --   however many allies it touches), or an ally at blows that is under a quarter of its health.
  --   Distance to the mage itself is not consulted.
  -- tests: GameplayFixesBots.mage_freeze_time_waits_for_three_foes_at_blows_with_allies_with_the_setting_on
  -- tests: GameplayFixesBots.mage_freeze_time_leaves_a_crowd_at_the_mage_to_teleport_with_the_setting_on
  -- tests: GameplayFixesBots.mage_freeze_time_counts_foes_once_and_takes_a_dying_ally_as_the_cue_with_the_setting_on
  return engaged >= 3 or hurt
end

-- Gameplay Fixes: the classic rule below was written for TELEPORT (flee
-- when alone or swamped) and applied to every slot. With the setting on
-- each slot asks when it pays off:
--   1 TELEPORT keeps the classic rule; the TELEPORT MARKER (the Shift arm)
--     is no longer the coin's: with nobody within 110 px the mage marks
--     where it stands when no marker of its own stands, a hero has the INT
--     the cast asks, and half the pool left after the price still covers
--     WARP SPACE, so its wander teleports go home to it. The Shift is
--     written after the engine's coin, which is still drawn.
--   2 WARP SPACE is a point-blank ring (its fireballs crawl one pixel a
--     tick): two living foes inside it, or one the mage does not face.
--   3 FREEZE TIME waits for the allies' fight (freeze_wanted above).
--   4 ENERGY WAVE runs along the fire heading through walls: a living foe
--     within 110 px that the mage faces.
--   5 HEARTBURST wants a foe inside its own reach.
local function fixed_gate(self, foe_count)
  local slot = self:current_special()
  -- rule: Bot TELEPORT keeps the classic rule with the setting on: teleport away when no foe is
  --   within 110 px or more than three are (swamped); Gameplay Fixes adds: never while already
  --   mid-teleport (the cast would refuse SPECIAL BUSY).
  -- tests: GameplayFixesBots.mage_teleport_keeps_the_classic_rule_on_both_values
  -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
  if slot == 1 then
    local t = og.tuning(self)
    -- rule: Bot TELEPORT keeps the classic rule with the setting on.
    -- tests: GameplayFixesBots.mage_teleport_keeps_the_classic_rule_on_both_values
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    local yes = foe_count < 1 or foe_count > 3
    -- rule: Gameplay Fixes bot mage drops a TELEPORT MARKER (the Shift arm) only when nobody is
    --   within 110 px, it has no marker of its own standing, a hero has the 75 INT, half the pool
    --   left after the price still covers WARP SPACE, and it is not busy; otherwise TELEPORT is
    --   unshifted. The engine's Shift coin is still drawn first and then overwritten.
    -- tests: GameplayFixesBots.mage_marker_is_placed_once_where_the_mage_will_return
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    -- tests: parity:mage_bot_marker_home_fixes_scen99
    local mark = foe_count < 1 and G.marker_pays(self, FX_MARKER, 2, t.marker_int_req)
    if mark and not lc.is_busy(self) then
      self:set_shifter_down(1)
    else
      self:set_shifter_down(0)
    end
    -- rule: Bot TELEPORT keeps the classic rule with the setting on.
    -- tests: GameplayFixesBots.mage_teleport_keeps_the_classic_rule_on_both_values
    -- tests: GameplayFixesBots.a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on
    return yes and not lc.mid_teleport(self)
  end
  -- rule: Gameplay Fixes bot WARP SPACE (8 fireballs crawling 1 px a tick, range 7 + the spare-pool
  --   add/3) pays for two living foes inside that point-blank ring or one the mage does not face.
  -- tests: GameplayFixesBots.mage_warp_space_wants_a_crowd_or_a_foe_behind_in_its_point_blank_ring
  -- tests: parity:mage_bot_warp_two_at_blows_fixes_scen99
  if slot == 2 then
    -- each bolt's range add, as starburst computes it from the spare pool
    local add = og.min(og.div(lc.spare_mp(self, 2), 15), STARBURST_ADD_CAP) // 3
    return G.ring_pays(self, G.UNIT_STEP, 7, add)
  end
  -- rule: Bot FREEZE TIME with the setting on uses the allies'-fight cue (above), not the classic
  --   alone/swamped rule.
  -- tests: GameplayFixesBots.mage_freeze_time_waits_for_three_foes_at_blows_with_allies_with_the_setting_on
  -- tests: GameplayFixesBots.mage_freeze_time_leaves_a_crowd_at_the_mage_to_teleport_with_the_setting_on
  -- tests: GameplayFixesBots.mage_freeze_time_counts_foes_once_and_takes_a_dying_ally_as_the_cue_with_the_setting_on
  if slot == 3 then
    return freeze_wanted(self)
  end
  -- rule: Gameplay Fixes bot ENERGY WAVE wants its nearest living foe within 110 px faced along the
  --   fire heading, and the fireball's pad clear (the wave rides a fired bolt).
  -- tests: GameplayFixesBots.mage_energy_wave_wants_a_faced_foe_within_110
  -- tests: GameplayFixesBots.mage_energy_wave_refuses_a_pad_on_a_wall
  -- tests: parity:mage_bot_wave_pad_body_fixes_scen99
  if slot == 4 then
    local foe = G.nearest_living_foe(self, 110, false)
    -- rule: Gameplay Fixes bot ENERGY WAVE wants its nearest living foe within 110 px faced along
    --   the fire heading, and the fireball's pad clear (the wave rides a fired bolt).
    -- tests: GameplayFixesBots.mage_energy_wave_wants_a_faced_foe_within_110
    -- tests: GameplayFixesBots.mage_energy_wave_refuses_a_pad_on_a_wall
    -- tests: parity:mage_bot_wave_pad_body_fixes_scen99
    local faced = foe ~= nil and G.faces(self, foe)
    -- rule: Gameplay Fixes bot ENERGY WAVE wants its nearest living foe within 110 px faced along
    --   the fire heading, and the fireball's pad clear (the wave rides a fired bolt).
    -- tests: GameplayFixesBots.mage_energy_wave_wants_a_faced_foe_within_110
    -- tests: GameplayFixesBots.mage_energy_wave_refuses_a_pad_on_a_wall
    -- tests: parity:mage_bot_wave_pad_body_fixes_scen99
    return faced and G.pad_clear(self)
  end
  -- HEARTBURST: slot 5 and every unmapped slot (the family's default cast)
  local t = og.tuning(self)
  local _, in_reach = og.find_foes_in_range("ob",
    t.heartburst_range_base + t.heartburst_range_per_level * self.level, self)
  -- rule: Gameplay Fixes bot HEARTBURST wants at least one foe inside its own reach (80 + 2*level
  --   px), the same finder the cast uses.
  -- tests: GameplayFixesBots.mage_heartburst_wants_a_foe_in_its_own_reach_with_the_setting_on
  return in_reach >= 1
end

local function check_special_ai(self)
  -- count_foes_in_range(self, 110) == the howmany out-param of the binding's world->find_foes_in_range.
  local _, foe_count = og.find_foes_in_range("ob", 110, self)
  -- rule: Classic mage bot casts ANY slot only when alone (no foe within 110 px) or swamped (more
  --   than three): the TELEPORT rule applied to every slot. Gameplay Fixes dispatches to the
  --   per-slot gate. Both wrapped by gate_with_rest(…, 3): a resting FREEZE TIME is never picked.
  -- tests: GameplayFixesBots.mage_teleport_keeps_the_classic_rule_on_both_values
  -- tests: GameplayFixesBots.mage_warp_space_wants_a_crowd_or_a_foe_behind_in_its_point_blank_ring
  -- tests: GameplayFixesBots.mage_freeze_time_leaves_a_crowd_at_the_mage_to_teleport_with_the_setting_on
  -- tests: GameplayFixesBots.mage_energy_wave_wants_a_faced_foe_within_110
  -- tests: GameplayFixesBots.mage_heartburst_wants_a_foe_in_its_own_reach_with_the_setting_on
  -- tests: KitRests.bot_gates_hold_a_resting_special
  if gf.on() then
    return fixed_gate(self, foe_count)
  end
  -- TP if away from guys .. or too many enemies!
  return foe_count < 1 or foe_count > 3
end

-- The trampoline passes stats->controller() (the stats' owner walker) as
-- self, so the C++ body's `controller` IS self and `stats->` maps to s_*.
local function hit_response(self, foe)
  local threshold
  -- rule: A struck mage teleports to safety below a health threshold: 3/5 of max hp for a hero (has
  --   a guy), 3/8 for a bot ('enemy mages are braver').
  -- tests: StatsHitResponseTeleport.stats_hit_response_mage_and_archmage_teleport_branches
  -- tests: StatsHitResponseTeleport.a_mage_struck_again_mid_teleport_casts_nothing
  -- tests: SemMage.a_bot_mage_flees_only_below_three_eighths_and_a_hero_below_three_fifths
  if self:has_guy() then
    -- shim kept: max_hp is a C++ float: per-op float rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 5.0)
  else
    -- Enemy mages are "braver :>" than player characters: 3/8 vs 3/5 HP.
    -- shim kept: max_hp is a C++ float: per-op float rounding.
    threshold = og.fdiv(og.fmul(3.0, self.max_hp), 8.0)
  end

  -- Determine which specials we can do (by level and sp) ..
  local possible = {}
  -- rule: The struck mage may only escape-teleport if it can afford TELEPORT (15 MP); the
  --   possible-specials scan is by level tier and MP. (Only possible[1] is consumed; level >= 1
  --   always reaches slot 1.)
  -- tests: StatsHitResponseTeleport.a_mage_struck_again_mid_teleport_casts_nothing
  -- tests: SemMage.a_struck_mage_that_cannot_pay_for_teleport_retargets_instead
  for i = 0, (self.level + 2) // 3 do
    -- rule: The struck mage may only escape-teleport if it can afford TELEPORT (15 MP); the
    --   possible-specials scan is by level tier and MP.
    -- tests: StatsHitResponseTeleport.a_mage_struck_again_mid_teleport_casts_nothing
    -- tests: SemMage.a_struck_mage_that_cannot_pay_for_teleport_retargets_instead
    if i < C.NUM_SPECIALS
        and self.magicpoints >= self:s_special_cost(i) then
      possible[i] = 1
    end
  end

  -- rule: Below the threshold with 15 MP a struck mage forces TELEPORT (slot 1, shifter 0 so never
  --   the marker, busy forced to 0) and does not retarget; otherwise a hit from a new attacker
  --   makes the mage and the attacker target each other and resets distances to 15000.
  -- tests: StatsHitResponseTeleport.stats_hit_response_mage_and_archmage_teleport_branches
  -- tests: StatsHitResponseTeleport.a_mage_struck_again_mid_teleport_casts_nothing
  -- tests: SemMage.a_repeat_hit_from_the_same_foe_leaves_the_mages_pursuit_alone
  if self.hp < threshold and possible[1] then
    -- teleport to safety: shifter_down 0 means TELEPORT, not the marker,
    -- and busy = 0 force-allows us to special
    self.current_special = 1
    self:set_shifter_down(0)
    self.busy = 0
    -- rule: A mage struck again while already mid-teleport casts nothing (no second TELEPORT, no MP
    --   spent, no refusal message).
    -- tests: StatsHitResponseTeleport.a_mage_struck_again_mid_teleport_casts_nothing
    local _ = lc.mid_teleport(self) or self:special()  -- struck mid-teleport: no second cast
  else
    -- we're hit by a new enemy
    -- rule: Same rule as above else-arm: only a NEW attacker retargets the mage and the attacker
    --   onto each other.
    -- tests: StatsHitResponseTeleport.stats_hit_response_mage_and_archmage_teleport_branches
    -- tests: SemMage.a_repeat_hit_from_the_same_foe_leaves_the_mages_pursuit_alone
    if self:foe() ~= foe then
      self:set_foe(foe)
      foe:set_foe(self)
      self:s_set_current_distance(15000)
      self:s_set_last_distance(15000)
    end
  end
end

local function set_difficulty(self, level)
  og.apply_difficulty_scaling(self, level, 7.0, 14.0, 3.0, 0.5)
end

local function level_up(guy, level_diff)
  og.apply_level_up(guy, level_diff, 4, 6, 4, 16, 1)
end

local function teleport(self)
  -- rule: TELEPORT and TELEPORT MARKER refuse 'SPECIAL BUSY' while the mage is mid-teleport
  --   (ANI_TELE_OUT/IN).
  -- tests: WalkerSpecials.mage_teleport_drives_simulation
  -- tests: SemMage.a_mage_mid_teleport_cannot_teleport_again
  if lc.mid_teleport(self) then
    return false, "SPECIAL BUSY"
  end
  -- rule: Unshifted TELEPORT plays SOUND_TELEPORT and starts the tele-out animation; the engine's
  --   handle_teleport then moves the mage to a random spot (or to its marker). Shifted is the
  --   marker path.
  -- tests: WalkerSpecials.mage_teleport_drives_simulation
  -- tests: WalkerSpecials.mage_marker_recast_replaces_the_owned_marker
  -- tests: SemMage.an_unshifted_teleport_plays_the_teleport_sound_and_starts_the_fade_out
  if self:shifter_down() == 0 then
    og.emit_positional_sound(self, C.SOUND_TELEPORT)
    self.ani_type = C.ANI_TELE_OUT
    self:set_cycle(0)
    return true
  end
  -- leave/remove a marker
  -- rule: Placing a TELEPORT MARKER refuses 'SPECIAL BUSY' while busy > 0.
  -- tests: WalkerSpecials.mage_marker_recast_replaces_the_owned_marker
  -- tests: SemMage.a_busy_mage_cannot_lay_a_marker
  if lc.is_busy(self) then
    return false, "SPECIAL BUSY"
  end
  local t = og.tuning(self)
  -- rule: Only a hero needs 75 INT to place a TELEPORT MARKER (refused '75 INT REQUIRED', free, no
  --   notification); bots place markers freely.
  -- tests: PackLuaMage.the_marker_flow_talks_only_to_the_casting_seat
  -- tests: SimInputUnit.intelligence_refusals_use_one_private_cue
  -- tests: WalkerSpecials.mage_marker_recast_replaces_the_owned_marker
  -- tests: parity:mage_bot_marker_home_fixes_scen99
  if self:has_guy() and self:g_intelligence() < t.marker_int_req then
    return false, string.format("%d INT REQUIRED", t.marker_int_req)
  end
  -- Remove a marker, if present
  local obs = og.oblist()
  -- rule: A mage keeps at most one marker: placing a new one first kills its own live marker
  --   (another mage's marker, a dead one or a non-marker FX is untouched) and adds 8 busy for the
  --   removal.
  -- tests: WalkerSpecials.mage_marker_recast_replaces_the_owned_marker
  -- tests: SemMage.laying_a_first_marker_leaves_every_other_mark_alone
  for i = 1, #obs do
    local ob = obs[i]
    -- rule: A mage keeps at most one marker.
    -- tests: WalkerSpecials.mage_marker_recast_replaces_the_owned_marker
    -- tests: SemMage.laying_a_first_marker_leaves_every_other_mark_alone
    if ob
        and ob:order() == C.ORDER_FX
        and ob:family() == FX_MARKER
        and ob:owner() == self
        and ob:dead() == 0 then
      ob.dead = 1
      ob:death()
      -- rule: The '(Old Marker Removed)' notice goes only to a seated caster (user ~= -1) on the
      --   player team or a hero, addressed to that caster's seat. Same rule as below.
      -- tests: PackLuaMage.the_marker_flow_talks_only_to_the_casting_seat
      -- tests: SemMage.an_unseated_mage_retires_its_marker_without_a_notice
      if (self.team == 0 or self:has_guy())
          and self:user() ~= -1 then
        og.emit_notification("(Old Marker Removed)", 0, self)
      end
      -- shim kept: busy is a C++ float: per-op float rounding.
      self:set_busy(og.fadd(self:busy(), 8.0))
      break
    end
  end
  -- The C++ resets its generic to 0 after the removal loop ("force new
  -- placement, for now"), so a marker is always placed here.
  local marker = og.add_ob("fx", FX_MARKER)
  -- plumbing: og.add_ob returned nil (object cap): 'COULD NOT CREATE MARKER'.
  if not marker then
    return false, "COULD NOT CREATE MARKER"
  end
  marker:set_owner(self)
  marker:set_floor(self:floor())  -- marker stays on the caster's floor
  marker:center_on(self)
  -- rule: A marker's number of uses is INT/33 for a hero, level/4 + 1 for a bot.
  -- tests: WalkerSpecials.mage_marker_recast_replaces_the_owned_marker
  -- tests: SemMage.a_bot_marker_holds_a_quarter_of_the_level_plus_one_uses
  if self:has_guy() then
    marker.lifetime = self:g_intelligence() // 33
  else
    marker.lifetime = self.level // 4 + 1
  end
  marker.ani_type = C.ANI_SPIN
  -- rule: Same rule as above: 'Teleport Marker Placed' and '(N Uses)' go only to the seated
  --   caster's seat.
  -- tests: PackLuaMage.the_marker_flow_talks_only_to_the_casting_seat
  -- tests: SemMage.a_bot_placing_a_marker_announces_nothing
  if (self.team == 0 or self:has_guy())
      and self:user() ~= -1 then
    og.emit_notification("Teleport Marker Placed", 0, self)
    og.emit_notification(string.format("(%d Uses)", marker:lifetime()), 0, self)
  end
  -- shim kept: busy is a C++ float: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), 8.0))
  -- Marker surcharge: half the MP left after the cast's own cost.
  lc.halve_mp_surcharge(self, self:current_special())
  return true
end

local function starburst(self)
  -- shim kept (both): the C++ parks the float aim in int temps: C truncation.
  local saved_aim_x = og.trunc(self:lastx())
  local saved_aim_y = og.trunc(self:lasty())
  local dmg_bonus = lc.spare_mp(self, self:current_special())
  -- rule: WARP SPACE spends spare magic: with MP left after its 60 price each fireball gains damage
  --   min(spare/15, 40) and range +that/3, and the mage pays that bonus once more; with no spare
  --   the bolts are plain.
  -- tests: SemMage.warp_space_puts_the_spare_magic_into_every_bolt
  if dmg_bonus > 0 then
    -- The per-fireball cap binds only above 660 MP; lineofsight add/3
    -- inherits that bound. Keep the original C truncation explicit.
    dmg_bonus = og.min(og.div(dmg_bonus, 15), STARBURST_ADD_CAP)
    -- shim kept: magicpoints is a C++ float: per-op float rounding.
    self.magicpoints = og.fsub(self.magicpoints, dmg_bonus)
  else
    dmg_bonus = 0
  end
  -- shim kept: magicpoints is a C++ float: per-op float rounding.
  self.magicpoints = og.fadd(self.magicpoints, 8 * self:s_weapon_cost())
  -- Now face each direction and fire ..
  -- rule: WARP SPACE fires one fireball along each of the 8 compass headings (never the null
  --   heading) and restores the mage's facing afterwards; weapon costs are pre-refunded (8 x wc).
  -- tests: WalkerSpecials.mage_warp_starburst_drives_simulation, parity:mage_starburst_ring_scen99
  -- tests: SemMage.warp_space_fires_all_eight_headings_and_restores_the_facing
  for i = -1, 1 do
    -- rule: WARP SPACE fires one fireball along each of the 8 compass headings (never the null
    --   heading) and restores the mage's facing afterwards.
    -- tests: WalkerSpecials.mage_warp_starburst_drives_simulation
    -- tests: parity:mage_starburst_ring_scen99
    -- tests: SemMage.warp_space_fires_all_eight_headings_and_restores_the_facing
    for j = -1, 1 do
      -- rule: WARP SPACE fires one fireball along each of the 8 compass headings (never the null
      --   heading) and restores the mage's facing afterwards.
      -- tests: WalkerSpecials.mage_warp_starburst_drives_simulation
      -- tests: parity:mage_starburst_ring_scen99
      -- tests: SemMage.warp_space_fires_all_eight_headings_and_restores_the_facing
      if i ~= 0 or j ~= 0 then
        self:set_lastx(i)
        self:set_lasty(j)
        local bolt = self:fire()
        -- rule: A WARP SPACE bolt whose pad is blocked is simply missing; the cast still succeeds
        --   and the other headings fire.
        -- tests: WalkerSpecials.mage_warp_starburst_drives_simulation
        -- tests: SemMage.a_warp_space_bolt_with_a_walled_pad_is_missing_and_the_cast_stands
        if bolt then
          -- shim kept: damage is a C++ float: per-op float rounding.
          bolt:set_damage(og.fadd(bolt:damage(), dmg_bonus))
          -- bonus/3 keeps the original C truncation explicit.
          bolt:set_lineofsight(bolt:lineofsight() + og.div(dmg_bonus, 3))
          -- rule: WARP SPACE fireballs crawl: each non-zero velocity component is normalised to
          --   +-1, so a bolt moves exactly 1 px per tick per axis (the ring is point-blank).
          -- tests: SemMage.warp_space_bolts_crawl_one_pixel_per_axis
          if bolt:lastx() ~= 0.0 then
            -- shim kept: lastx is a C++ float: per-op float rounding.
            bolt:set_lastx(og.fdiv(bolt:lastx(), math.abs(bolt:lastx())))
          end
          if bolt:lasty() ~= 0.0 then
            -- shim kept: lasty is a C++ float: per-op float rounding.
            bolt:set_lasty(og.fdiv(bolt:lasty(), math.abs(bolt:lasty())))
          end
        end
      end
    end
  end
  -- Restore old facing
  self:set_lastx(saved_aim_x)
  self:set_lasty(saved_aim_y)
  return true
end

local function freeze_time(self)
  -- Who stops time: with Gameplay Fixes on, a Mage on the player's team (world.my_team); off (the
  -- old game), one on team 0 or any hero. The stop is global; anyone else grants its side bonus_rounds.
  local t = og.tuning(self)
  -- shim kept: the C++ compares (uint8)my_team() against the u8 team field.
  -- rule: A mage that stops time (Gameplay Fixes on: one on the local player's team; off, the old
  --   game: one on team 0 or a hero) banks world enemy_freeze += 20 + 11*level, palette 1
  --   (EVENT_SET_PALETTE). Any other mage grants its own side bonus rounds instead: min(5 + 2*level,
  --   50) to itself and every friend on the map, with a 'TIME IS FROZEN! (N rounds)' notice. Wrapped
  --   by with_rest: 360-tick rest on both setting values.
  -- tests: WalkerSpecials.mage_freeze_time_drives_simulation, KitRests.freeze_time_rests_thirty_seconds
  -- tests: WalkerSpecials.mage_freeze_time_off_team_grants_bonus_rounds_not_a_global_stop
  -- tests: PackLuaMage.an_enemy_freeze_time_banks_bonus_rounds_for_its_own_side, PackLuaMage.an_enemy_freeze_time_is_capped_at_fifty_rounds
  -- tests: SemMage.a_player_team_freeze_time_banks_the_stop_and_asks_for_the_blue_palette, SemMage.a_hero_mage_off_the_red_team_stops_time_with_the_fixes_off_and_grants_rounds_with_them_on
  -- tests: SemMage.a_mage_on_a_player_team_other_than_red_grants_rounds_with_the_fixes_off_and_stops_time_with_them_on
  local fixed_stop = gf.on() and self.team == og.u8(og.my_team())
  local classic_stop = not gf.on() and (self.team == 0
                                        or self:has_guy())
  if fixed_stop or classic_stop then
    og.set_enemy_freeze(og.enemy_freeze()
                        + t.freeze_base + t.freeze_per_level * self.level)
    og.set_palette(1)
    og.emit_event(C.EVENT_SET_PALETTE, 1)
  else
    local rounds = og.min(
      t.bonus_rounds_base + t.bonus_rounds_per_level * self.level,
      t.bonus_rounds_cap)
    og.emit_notification(
      string.format("TIME IS FROZEN! (%d rounds)", rounds), 2)
    og.emit_event(C.EVENT_REQUEST_REDRAW)
    self:set_bonus_rounds(self:bonus_rounds() + rounds)
    local friends = og.find_friends_in_range("ob", 30000, self)
    -- rule: Off-team FREEZE TIME bonus rounds go to every friend of the caster anywhere on the map
    --   (30000 px).
    -- tests: PackLuaMage.an_enemy_freeze_time_banks_bonus_rounds_for_its_own_side
    -- tests: WalkerSpecials.mage_freeze_time_off_team_grants_bonus_rounds_not_a_global_stop
    for i = 1, #friends do
      local w = friends[i]
      -- plumbing: nil entry guard in the friends list.
      if w then
        w:set_bonus_rounds(w:bonus_rounds() + rounds)
      end
    end
  end
  return true
end

local function energy_wave(self)
  local bolt = self:fire()
  -- rule: ENERGY WAVE rides a fired fireball: if the fireball cannot be fired the cast fails 'COULD
  --   NOT FIRE'; otherwise a WAVE weapon owned by the mage, at the mage's level, takes the bolt's
  --   place and heading and the bolt is removed.
  -- tests: WalkerSpecials.mage_energy_wave_drives_simulation
  -- tests: WalkerSpecials.mage_wave_is_one_and_heartburst_is_one_per_foe
  -- tests: SemMage.energy_wave_replaces_its_fireball_and_fails_when_none_fires
  if not bolt then
    return false, "COULD NOT FIRE"
  end
  local wave = og.add_ob("weapon", WEAPON_WAVE)
  -- plumbing: og.add_ob('weapon', WAVE) nil: 'COULD NOT CREATE WAVE' (object cap).
  if not wave then
    return false, "COULD NOT CREATE WAVE"
  end
  wave:set_floor(bolt:floor())  -- wave rides the caster's floor
  wave:center_on(bolt)
  wave:set_owner(self)
  wave.level = self.level
  wave:set_lastx(bolt:lastx())
  wave:set_lasty(bolt:lasty())
  bolt.dead = 1
  return true
end

local function heartburst(self)
  local t = og.tuning(self)
  local foes, foe_count = og.find_foes_in_range(
    "ob",
    t.heartburst_range_base + t.heartburst_range_per_level * self.level,
    self)
  -- rule: HEARTBURST with no foe inside 80 + 2*level px is refused free 'NO FOE IN RANGE'.
  -- tests: WalkerSpecials.mage_heartburst_drives_simulation
  -- tests: WalkerSpecials.mage_wave_is_one_and_heartburst_is_one_per_foe
  -- tests: SemMage.heartburst_with_no_foe_in_reach_is_refused_for_free
  if foe_count == 0 then
    return false, "NO FOE IN RANGE"
  end
  local share = lc.mp_pool_damage(self, 5)
  -- shim kept: the share can be negative: C trunc, not Lua floor.
  local damage = og.div(share, foe_count)
  -- rule: A hero's HEARTBURST counts one shot per foe it bursts in total_shots and scen_shots
  --   (accuracy stats); bots keep no stats.
  -- tests: SemMage.a_hero_heartburst_counts_one_shot_per_foe
  if self:has_guy() then
    self:g_set_total_shots(self:g_total_shots() + foe_count)
    self:g_set_scen_shots(self:g_scen_shots() + foe_count)
  end
  -- shim kept: busy is a C++ float: per-op float rounding.
  self:set_busy(og.fadd(self:busy(), 5.0))
  -- rule: HEARTBURST detonates one magical explosion on each foe in reach (on the foe's floor),
  --   each dealing an equal share of half the spare pool (trunc(min(spare/2,cap)/foe_count)); the
  --   mage pays each share from its MP, takes 5 busy, and its own blast marker skip_exit=100 spares
  --   the caster.
  -- tests: WalkerSpecials.mage_heartburst_drives_simulation
  -- tests: WalkerSpecials.mage_wave_is_one_and_heartburst_is_one_per_foe
  -- tests: SemMage.heartburst_bursts_each_foe_for_an_equal_share_of_half_the_spare_pool
  for i = 1, #foes do
    local foe = foes[i]
    local burst = og.summon(self, "fx", FX_EXPLOSION)
    -- plumbing: og.summon fx nil (object cap): 'COULD NOT MAKE EXPLOSION' mid-loop.
    if not burst then
      return false, "COULD NOT MAKE EXPLOSION"
    end
    burst.damage = damage
    -- Heartburst bursts materialize ON each acquired foe, so they take
    -- that target's floor; the blast itself only damages same-floor
    -- walkers (explosion_on_death floor filter).
    burst:set_floor(foe:floor())
    burst:center_on(foe)
    og.emit_sound(C.SOUND_EXPLODE)
    burst.ani_type = C.ANI_EXPLODE
    burst:s_set_bit_flags(C.BIT_MAGICAL, 1)
    -- The legacy dummy field says "don't hurt caster" here.
    burst:set_skip_exit(100)
    -- shim kept: magicpoints is a C++ float: per-op float rounding.
    self.magicpoints = og.fsub(self.magicpoints, damage)
  end
  return true
end

-- promotion_new_level (mage_promotion_level) is descriptor data, not one of
-- the registrable living hooks — it stays on the C++ descriptor.

og.family("living", {
  id = "core:mage",
  wire_id = 3,
  name = "MAGE",
  short_name = og.NIL,
  stats  = { strength = 4, dexterity = 6, constitution = 4,
             intelligence = 16, armor = 5, level = 1 },
  combat = { hp = 90, melee_damage = 4, stepsize = 2,
             fire_delay = 4, fire_mp_cost = 5 },
  costs  = { hire = 450,
             train = { strength = 20, dexterity = 15, constitution = 16,
                       intelligence = 6, armor = 50, level = 200 } },
  specials = {
    { id = "teleport",    name = "TELEPORT",    mp_cost = 15,  alternate = { name = "TELEPORT MARKER", detail = "A return point, 75 Int.", icon = "icons/teleport_marker.png" }, cast = teleport, detail = "Any mage can teleport randomly away easily.", icon = "icons/teleport.png" },
    { id = "warp_space",  name = "WARP SPACE",  mp_cost = 60,  cast = starburst, detail = "Twist the fabric of space around you to deal death to enemies.", icon = "icons/warp_space.png" },
    { id = "freeze_time", name = "FREEZE TIME", mp_cost = 500, cast = lc.with_rest(freeze_time, 3, "freeze_rest", "FREEZE TIME"), detail = "Freeze all but your team and kill with ease. Rests 30 s.", icon = "icons/freeze_time.png" },
    { id = "energy_wave", name = "ENERGY WAVE", mp_cost = 70,  cast = energy_wave, detail = "Send a growing ripple of energy through walls and foes.", icon = "icons/energy_wave.png" },
    { id = "heartburst",  name = "HEARTBURST",  mp_cost = 100, detail = "Burst your enemies into flame. More magic means a bigger effect.", icon = "icons/heartburst.png" },
    default_cast = heartburst,  -- case 5 and every unmapped slot
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
  promotes_to = "core:archmage",
  promotion_level_req = 6,
  death_message = "MAGE DIED",
  sprite = "mage.png",
  animation = "mage",
  ai_line_of_sight = 7,
  description = "Mages are slow, can't stand much damage, and are horrible at hand-to-hand combat, but their magical fireballs pack a big punch.",
  names = { "Gandalf", "Saruman", "Radagast", "Alatar", "Pallando",
            "Raistlin", "Fizban", "Mordenkainen", "Merlin", "Harry",
            "Manannan", "Mordack", "Jace" },
  playable = true,
  playable_order = 4,
  glyph = "m",
  glyph_ascii = "m",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Teleport marker: myguy Intelligence requirement
    marker_int_req = 75,
    -- Freeze time on the player's team: enemy_freeze += base + per_level*level
    freeze_base = 20,
    freeze_per_level = 11,
    -- Freeze time elsewhere: bonus rounds = min(base + per_level*level, cap)
    bonus_rounds_base = 5,
    bonus_rounds_per_level = 2,
    bonus_rounds_cap = 50,
    -- Heartburst acquisition radius (px): base + per-level
    heartburst_range_base = 80,
    heartburst_range_per_level = 2,
    -- FREEZE TIME rests this many ticks between casts, whatever the New Specials setting
    freeze_rest = 360,
  },

  -- rule: A bot mage never chooses FREEZE TIME (slot 3) while its 360-tick freeze_rest is still
  --   running, on both Gameplay Fixes / New Specials values; its other slots stay free during the
  --   rest, and slot 3 is free again once the rest ends. Bot-side twin of the cast-side with_rest
  --   on FREEZE TIME (above); the gate mechanism itself lives in
  --   packs/core/lib/living_common.lua (other chunk).
  -- tests: KitRests.bot_gates_hold_a_resting_special
  check_special_ai = lc.gate_with_rest(check_special_ai, 3),
  hit_response = hit_response,
  set_difficulty = set_difficulty,
  level_up = level_up,
  handle_teleport = lc.handle_teleport,
})
