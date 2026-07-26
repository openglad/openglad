-- core treasure navigation — on_eat hooks transliterated from
-- src/gameplay/families/treasure_family_navigation.cpp (exit pad and
-- teleporter pad). Cookbook (docs/lua-classpacks-design.md §3) applies.
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
local FX_FLASH = og.family_id("fx", "core:flash")

local function format_exit_prompt(exitname, is_withdraw)
  if is_withdraw then
    return string.format("Withdraw to %s?", exitname)
  end
  return string.format("Exit to %s?", exitname)
end

-- exit_on_eat: the level-exit pad. Offers the exit prompt when the level is
-- clear, the withdraw prompt when it is not but the destination is already
-- earned, and otherwise says why nothing happened.
local function exit_on_eat(self, eater)
  -- Pure prefix (no sim writes): identical accept/reject as the C++.
  if eater:in_act() then return true end
  if eater:act_type() ~= C.ACT_CONTROL or eater:skip_exit() > 1 then
    return true
  end

  eater:set_skip_exit(10)
  -- See if there are any enemies left ...
  local guys_here
  if og.level_done() == 0 then
    guys_here = 1
  else
    guys_here = 0
  end
  -- Get the name of our exit..
  local exitname = og.scenario_title(string.format("scen%d", self:s_level()))
  if exitname == "none" then
    exitname = string.format("Level %d", self:s_level())
  end

  local destination_level = og.i16(self:s_level())
  local can_exit_now = guys_here == 0 or og.world_can_exit_whenever()
  local can_withdraw = og.level_completed(destination_level)
      and not og.level_completed(og.current_scenario())

  -- Exit path: all enemies dead, or scenario allows free exit. Checked
  -- BEFORE the withdraw path so that CAN_EXIT_WHENEVER levels show the
  -- normal "Exit to X?" dialog instead of "Withdraw to X?".
  if can_exit_now then
    og.emit_exit_confirmation(format_exit_prompt(exitname, false),
                              destination_level, 0)
    return true
  end

  -- Withdraw path: enemies still present, destination level already
  -- completed, current scenario not yet completed. Abort the current level
  -- and revert to the autosave, then jump to the exit's destination level.
  if can_withdraw then
    og.set_withdraw_request(destination_level)
    og.emit_exit_confirmation(format_exit_prompt(exitname, true),
                              destination_level, 1)
    og.emit_withdraw_to_level(destination_level)
  else
    -- Neither exitable nor withdrawable: foes still stand and the
    -- destination is unearned. Say why; the set_skip_exit(10) throttle
    -- above already bounds the cadence while the player stands on the pad.
    og.emit_notification("Foes remain!", 30)
  end
  return true
end

-- teleporter_on_eat: pad-to-pad warp. The pad remembers its partner in
-- leader() once found; a blocked destination bounces the eater back.
local function teleporter_on_eat(self, eater)
  if eater:skip_exit() > 1 then
    return true
  end
  local distance = self:distance_to_ob_center(eater)  -- how far away?
  if distance > 21 then
    return true
  end
  if distance < 4 and eater:skip_exit() ~= 0 then
    eater:set_skip_exit(8)
    return true
  end
  -- If we're close enough, teleport ..
  eater:set_skip_exit(eater:skip_exit() + 20)
  local target
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
  if not og.query_passable(eater:xpos(), eater:ypos(), eater) then
    eater:center_on(self)
    return true
  end
  -- Now do special effects
  local flash = og.add_ob("fx", FX_FLASH)
  if flash then
    flash:set_ani_type(C.ANI_EXPAND_8)
    flash:set_floor(self:floor())  -- flash at the departure pad (A8)
    flash:center_on(self)
  end
  return true
end

og.register_hooks("treasure", "core:exit", {
  on_eat = exit_on_eat,
})

og.register_hooks("treasure", "core:teleporter", {
  on_eat = teleporter_on_eat,
})
