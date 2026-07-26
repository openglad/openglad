-- core treasure navigation — on_eat hooks transliterated from
-- src/gameplay/families/treasure_family_navigation.cpp (exit pad and
-- teleporter pad). Cookbook (docs/lua-classpacks-design.md §3) applies.
--
-- core:teleporter is fully live.
--
-- MISSING BINDINGS (core:exit only). The exit pad reads and writes campaign
-- progression state that no og.* binding reaches yet:
--   * get_scenario_title("scen<N>")          navigation.cpp:54  (title lookup)
--   * world.type & GameWorld::TYPE_CAN_EXIT_WHENEVER
--                                            navigation.cpp:63
--   * world.completed_levels.count(level)    navigation.cpp:25 (via :64-65)
--   * world.current_scenario                 navigation.cpp:65
--   * world.withdraw_requested / withdraw_level
--                                            navigation.cpp:87-88
--   * emit_event_text(EventKind::RequestExitConfirmation, prompt, level, flag)
--                                            navigation.cpp:72-76, 89-93
--   * emit_event(EventKind::WithdrawToLevel, level, 0)
--                                            navigation.cpp:94-96
-- The FIRST of those (the title lookup) sits AFTER the hook's first
-- mutation (eater->set_skip_exit(10), navigation.cpp:45), so the guard is
-- raised at branch entry — right after the pure early-return prefix and
-- before set_skip_exit — and R9 hands the whole dispatch back to the still
-- registered C++ exit_on_eat with the sim untouched. The body below the
-- guard is the finished transliteration, ready to go live once the
-- bindings land.

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

  -- MISSING-binding guard, pre-side-effect (see the header note).
  og.MISSING_scenario_title(string.format("scen%d", self:s_level()))

  eater:set_skip_exit(10)
  -- See if there are any enemies left ...
  local guys_here
  if og.level_done() == 0 then
    guys_here = 1
  else
    guys_here = 0
  end
  -- Get the name of our exit..
  local exitname =
      og.MISSING_scenario_title(string.format("scen%d", self:s_level()))
  if exitname == "none" then
    exitname = string.format("Level %d", self:s_level())
  end

  local destination_level = og.i16(self:s_level())
  local can_exit_now = guys_here == 0 or og.MISSING_world_can_exit_whenever()
  local can_withdraw = og.MISSING_level_completed(destination_level)
      and not og.MISSING_level_completed(og.MISSING_current_scenario())

  -- Exit path: all enemies dead, or scenario allows free exit. Checked
  -- BEFORE the withdraw path so that CAN_EXIT_WHENEVER levels show the
  -- normal "Exit to X?" dialog instead of "Withdraw to X?".
  if can_exit_now then
    og.MISSING_emit_exit_confirmation(format_exit_prompt(exitname, false),
                                      destination_level, 0)
    return true
  end

  -- Withdraw path: enemies still present, destination level already
  -- completed, current scenario not yet completed. Abort the current level
  -- and revert to the autosave, then jump to the exit's destination level.
  if can_withdraw then
    og.MISSING_set_withdraw_request(destination_level)
    og.MISSING_emit_exit_confirmation(format_exit_prompt(exitname, true),
                                      destination_level, 1)
    og.MISSING_emit_withdraw_to_level(destination_level)
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
