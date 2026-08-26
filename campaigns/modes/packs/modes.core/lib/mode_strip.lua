-- Scenario-troops strip, shared by all six modes: one rule set for the per-team MAP UNITS box so the modes cannot drift apart.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local match = og.use("mode_match")

-- True for the entity kinds the strip removes. Everything else authored on
-- the map (treasure, flags, waypoints, markers, fx) is untouched.
-- Generators follow the same box as the livings (amendment B4) —
-- Onslaught's foundries included: unchecking a team's MAP UNITS box in
-- Onslaught removes that side's board, and the fills rows drop the team
-- with it.
local function is_troop(order)
  if order == C.ORDER_LIVING then
    return true
  end
  return order == C.ORDER_GENERATOR
end

-- Retire one walker. The corpse moves off the score-team range BEFORE it is
-- marked dead so a same-tick death scan cannot see it as a score-team
-- casualty and respawn it.
local function retire(w)
  w:set_team_num(C.SCORE_TEAM_COUNT)
  w:set_dead(1)
end

-- The per-team strip (amendment B4 — the old TROOPS: OWN strip, now keyed
-- by each team's MAP UNITS box): every authored living and generator with
-- no roster guy on a score team whose box reads OFF is retired. Wildlife
-- (teams at or beyond the score range) has no box and always stands —
-- the one deliberate narrowing against the retired global OWN strip,
-- which took the wildlife too.
local function strip_team_units(obs, fielded)
  local removed = 0
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if is_troop(w:order()) then
        if not w:has_guy() then
          local team = w:team_num()
          if team < C.SCORE_TEAM_COUNT then
            if not fielded[team + 1] then
              retire(w)
              removed = removed + 1
            end
          end
        end
      end
    end
  end
  return removed
end

-- The entry point every mode impl calls from on_mode_init, immediately after
-- its inactive-team strip and BEFORE its empty-team census (so bot backfill
-- sees the post-strip world). Reads the four MAP UNITS boxes through
-- mode_match's one knob seam. Returns how many entities were retired.
local function strip_authored_troops()
  local fielded = {}
  local any_off = false
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    fielded[team + 1] = match.map_units_fielded(team)
    if not fielded[team + 1] then
      any_off = true
    end
  end
  if not any_off then
    return 0
  end
  return strip_team_units(og.oblist(), fielded)
end

return {
  is_troop = is_troop,
  retire = retire,
  strip_authored_troops = strip_authored_troops,
}
