-- core:exit + core:teleporter — exit pad and warp pad on_eat (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- The exit pad reads and writes campaign progression state through its own
-- bindings:
--   * og.scenario_title("scen<N>")     get_scenario_title (title lookup;
--                                      "none" when no reader is installed,
--                                      the same answer a failed read gives)
--   * og.world_can_exit_whenever()     world.type & TYPE_CAN_EXIT_WHENEVER
--   * og.level_completed(level)        world.completed_levels.count(level)
--   * og.current_scenario()            world.current_scenario
--   * og.set_withdraw_request(level)   world.withdraw_requested/withdraw_level
--   * og.emit_exit_confirmation(...)   EventKind::RequestExitConfirmation
--   * og.emit_withdraw_to_level(level) EventKind::WithdrawToLevel
-- can_exit_now/can_withdraw are C++ locals computed before the branch; Lua's
-- and/or short-circuits, which is safe here because every one of those reads
-- is pure (no sim writes, no draws).

local C = og.C
local FX_FLASH = assert(og.family_id("fx", "core:flash"))
local gf = og.use("gameplay_fixes")
local function format_exit_prompt(exit_name, is_withdraw)
  -- rule: The exit pad's prompt reads "Withdraw to <name>?" on the withdraw path and "Exit to
  --   <name>?" otherwise.
  -- tests: SemExitPad.the_prompt_reads_withdraw_on_the_withdraw_path_and_exit_otherwise
  if is_withdraw then
    return string.format("Withdraw to %s?", exit_name)
  end
  return string.format("Exit to %s?", exit_name)
end

-- exit_on_eat: the level-exit pad. Offers the exit prompt when the level is
-- clear, the withdraw prompt when it is not but the destination is already
-- earned, and otherwise says why nothing happened.
local function exit_on_eat(self, eater)
  -- Pure prefix (no sim writes): identical accept/reject as the C++.
  -- rule: An exit pad ignores an eater that touches it from inside its own act (re-entrant touch).
  -- tests: RuntimeCoveragePaths.treasure_navigation_early_returns_and_withdraw_decline
  -- tests: RuntimeCoveragePaths.treasure_exit_and_teleporter_navigation_paths
  if eater:in_act() then
    return true
  end
  -- rule: Only a player-controlled walker triggers an exit pad, and at most once per skip_exit
  --   throttle (10 ticks) while it stands there.
  -- tests: RuntimeCoveragePaths.treasure_navigation_early_returns_and_withdraw_decline
  -- tests: SimWorldHeadless.exit_with_foes_alive_says_foes_remain
  -- tests: RuntimeCoveragePaths.treasure_exit_and_teleporter_navigation_paths
  if eater:act_type() ~= C.ACT_CONTROL or eater:skip_exit() > 1 then
    return true
  end

  eater:set_skip_exit(10)
  -- See if there are any enemies left ...
  local foes_left
  -- rule: With every foe gone the exit pad offers the plain "Exit to <name>?" prompt for its
  --   destination (checked before the withdraw path).
  -- tests: parity:treasure_exit_open_prompt_scen99
  -- tests: RuntimeCoveragePaths.treasure_exit_and_teleporter_navigation_paths
  -- tests: SimWorldHeadless.exit_with_foes_alive_says_foes_remain
  if og.level_done() == 0 then
    foes_left = 1
  else
    foes_left = 0
  end
  -- Get the name of our exit..
  local exit_name = og.scenario_title(string.format("scen%d", self.level))
  -- rule: An exit to a scenario with no readable title is named "Level <N>".
  -- tests: SemExitPad.an_exit_with_no_title_is_named_by_its_level_number
  if exit_name == "none" then
    exit_name = string.format("Level %d", self.level)
  end

  local destination_level = self.level
  -- buffers's 2004 SCEN_TYPE_CAN_EXIT exception survives under its current
  -- name: flagged scenarios deliberately bypass the "foes left" gate.
  -- rule: A scenario flagged CAN_EXIT_WHENEVER lets the player exit with foes still standing, with
  --   the normal Exit prompt (with Gameplay Fixes off, after a withdraw to a won destination: below).
  -- tests: SemExitPad.a_can_exit_whenever_level_offers_the_exit_with_foes_standing, SemExitPad.a_can_exit_whenever_level_withdraws_first_to_a_won_level_with_the_fixes_off_and_exits_with_them_on, SemExitPad.the_free_exit_bit_counts_whatever_other_type_bits_the_level_carries
  local can_exit_now = foes_left == 0 or og.world_can_exit_whenever()
  -- rule: With foes still alive, an exit to an already completed level withdraws: it sets the
  --   withdraw request, prompts "Withdraw to <name>?" (b=1) and emits WithdrawToLevel - but never
  --   from a replay of an already completed level. A CAN_EXIT_WHENEVER level withdraws first only with
  --   Gameplay Fixes off (the old game; the server asks Exit after a decline); on, it asks Exit only.
  -- tests: parity:level_withdraw_scen99, parity:treasure_exit_pickup_scen99, RuntimeCoveragePaths.treasure_navigation_early_returns_and_withdraw_decline
  -- tests: SimWorldHeadless.replay_of_a_completed_level_cannot_withdraw, SemExitPad.a_can_exit_whenever_level_withdraws_first_to_a_won_level_with_the_fixes_off_and_exits_with_them_on
  local can_withdraw = og.level_completed(destination_level)
      and not og.level_completed(og.current_scenario())
  local withdraw_first = can_withdraw and foes_left ~= 0
  withdraw_first = withdraw_first and not gf.on()
  can_exit_now = can_exit_now and not withdraw_first
  -- Exit path: no foe left, or a free-exit level (with the setting off, unless it withdraws first).
  -- rule: With every foe gone, or on a CAN_EXIT_WHENEVER level that does not withdraw first, the
  --   exit pad offers the plain "Exit to <name>?" prompt for its destination.
  -- tests: parity:treasure_exit_open_prompt_scen99
  -- tests: RuntimeCoveragePaths.treasure_exit_and_teleporter_navigation_paths
  -- tests: SimWorldHeadless.exit_with_foes_alive_says_foes_remain
  if can_exit_now then
    og.emit_exit_confirmation(format_exit_prompt(exit_name, false),
                              destination_level, 0)
    return true
  end

  -- Withdraw path: enemies still present, destination level already
  -- completed, current scenario not yet completed. Abort the current level
  -- and revert to the autosave, then jump to the exit's destination level.
  -- rule: With foes still alive, an exit to an already completed level withdraws. Here: `if
  --   can_withdraw`.
  -- tests: parity:level_withdraw_scen99, parity:treasure_exit_pickup_scen99
  -- tests: RuntimeCoveragePaths.treasure_navigation_early_returns_and_withdraw_decline
  -- tests: SimWorldHeadless.replay_of_a_completed_level_cannot_withdraw
  if can_withdraw then
    og.set_withdraw_request(destination_level)
    og.emit_exit_confirmation(format_exit_prompt(exit_name, true),
                              destination_level, 1)
    og.emit_withdraw_to_level(destination_level)
  else  -- rule: Foes alive and the destination unearned: the pad tells the eater "Foes remain!"
    --   (30-tick toast to its seat), throttled by skip_exit.
    -- tests: SimWorldHeadless.exit_with_foes_alive_says_foes_remain
    -- Neither exitable nor withdrawable: foes still stand and the
    -- destination is unearned. Say why; the set_skip_exit(10) throttle
    -- above already bounds the cadence while the player stands on the pad.
    og.emit_notification("Foes remain!", 30, eater)
  end
  return true
end

-- teleporter_on_eat: pad-to-pad warp. The pad remembers its partner in
-- leader() once found; a blocked destination bounces the eater back.
local function teleporter_on_eat(self, eater)
  -- rule: A warp pad fires only for an eater not throttled (skip_exit <= 1) and within 21 px of the
  --   pad's centre; an eater standing at the very centre (< 4 px) with a pending skip is held
  --   (skip_exit = 8) instead of warped back.
  -- tests: RuntimeCoveragePaths.treasure_navigation_early_returns_and_withdraw_decline
  -- tests: RuntimeCoveragePaths.treasure_exit_and_teleporter_navigation_paths
  -- tests: GameplayFixesWalk.a_walk_that_eats_a_teleporter_does_not_shove_the_ally_it_left
  if eater:skip_exit() > 1 then
    return true
  end
  -- how far away?
  local dist = self:distance_to_ob_center(eater)
  if dist > 21 then
    return true
  end
  if dist < 4 and eater:skip_exit() ~= 0 then
    eater:set_skip_exit(8)
    return true
  end
  -- If we're close enough, teleport ..
  eater:set_skip_exit(eater:skip_exit() + 20)
  local target
  -- rule: A warp pad remembers the partner it found (leader) and warps to it every time; with no
  --   partner pad of the same group, the eater only gets the skip_exit bump (which still throttles
  --   a co-located exit pad).
  -- tests: RuntimeCoveragePaths.treasure_exit_and_teleporter_navigation_paths
  -- tests: GameplayFixesWalk.a_walk_that_eats_a_teleporter_does_not_shove_the_ally_it_left
  -- tests: SemWarpPad.a_warp_pad_keeps_its_remembered_partner_classic_and_a_lone_pad_only_throttles
  if not self:leader() then
    target = self:find_teleport_target()
  else
    target = self:leader()
  end
  if not target then
    return true
  end
  self:set_leader(target)
  eater:center_on(target)
  -- rule: A warp whose destination is blocked bounces the eater back onto the departure pad (no
  --   flash).
  -- tests: GameplayFixesWalk.a_walk_that_eats_a_teleporter_does_not_shove_the_ally_it_left
  -- tests: SemWarpPad.a_blocked_warp_bounces_back_onto_the_departure_pad
  if not og.query_passable(eater:xpos(), eater:ypos(), eater) then
    eater:center_on(self)
    return true
  end
  -- Now do special effects
  local flash = og.add_ob("fx", FX_FLASH)
  -- plumbing: add_ob flash nil guard
  if flash then
    flash.ani_type = C.ANI_EXPAND_8
    flash:set_floor(self:floor())  -- flash at the departure pad
    flash:center_on(self)
  end
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:exit        on_eat = navigation.exit_on_eat
--   core:teleporter  on_eat = navigation.teleporter_on_eat
return {
  exit_on_eat = exit_on_eat,
  teleporter_on_eat = teleporter_on_eat,
}
