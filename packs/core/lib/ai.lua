-- core lib: ai — parameterized check_special_ai shapes, closures over constants only (R6-safe) (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local M = {}

-- Cast when the acquired-or-found foe is within `dist`: the engine's
-- shared acquire-foe + distance gate.
function M.foe_within(dist)
  return function(self)
    return og.check_special_ai_distance(self, dist)
  end
end

-- Cast when the current-or-found foe sits inside the (near, far) distance
-- window. Acquisition assigns self.foe exactly like the ladder prelude
-- did; with no foe to be found the gate is closed.
--
-- Both builders here take PLAIN CONSTANTS, deliberately: these gates run
-- every AI act tick, and a per-call og.tuning read here blew the
-- instruction budget on the generator scenarios (+23.8% on bones).
-- Gate-only ranges are therefore code
-- constants; only ranges a special body shares read og.tuning (at the
-- family's own check_special_ai, where the special keeps the single
-- source of truth).
function M.foe_in_window(near, far)
  return function(self)
    local foe = self:foe()
    -- rule: A classic window gate (the soldier's check_special_ai) with no current foe first
    --   acquires the nearest foe and KEEPS it as the walker's foe; when there is still no foe to be
    --   found the gate is closed (no special).
    -- tests: SemLibsAi.a_classic_window_gate_keeps_the_nearest_foe_it_finds
    -- tests: SemLibsAi.a_classic_window_gate_with_nobody_to_find_stays_closed
    -- tests: SemLibsAi.a_classic_window_gate_answers_by_the_foe_it_already_holds
    if not foe then
      self:set_foe(og.find_near_foe(self))
      foe = self:foe()
    end
    -- rule: Part of the rule above that begins "A classic window gate (the soldier's
    --   check_special_ai) with no ..."
    -- tests: SemLibsAi.a_classic_window_gate_keeps_the_nearest_foe_it_finds
    -- tests: SemLibsAi.a_classic_window_gate_with_nobody_to_find_stays_closed
    -- tests: SemLibsAi.a_classic_window_gate_answers_by_the_foe_it_already_holds
    if not foe then
      return false
    end
    local distance = self:distance_to_ob(foe)
    -- rule: The soldier's classic special gate fires only for a foe strictly inside the open window
    --   (near, far) = (20, 75) px of distance_to_ob (corner-to-corner Manhattan); a foe at exactly
    --   20 or 75, nearer, or farther gets no special.
    -- tests: GameplayFixesBots.soldier_charge_wants_a_foe_in_the_rush
    -- tests: GameplayFixesBots.soldier_whirlwind_wants_two_targets_inside_the_spin
    -- tests: SemLibsAi.the_classic_soldier_window_is_open_strictly_between_20_and_75_px
    return distance < far and distance > near
  end
end

return M
