-- Scenario-troops strip, shared by all five modes: one rule set for the "strip roster teams" and "strip everything authored" settings so the modes cannot drift apart.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")

-- og.match_setting("strip_troops") states.
local KEEP = 0
local STRIP_TEAMS = 1
local STRIP_ALL = 2

-- True for the entity kinds the strip removes. Everything else authored on
-- the map (treasure, flags, waypoints, markers, fx) is untouched.
local function is_troop(order, keep_generators)
  if order == C.ORDER_LIVING then
    return true
  end
  if order ~= C.ORDER_GENERATOR then
    return false
  end
  -- Onslaught's generators ARE the board, not troops.
  return not keep_generators
end

-- Which score teams field at least one roster walker. Only these teams lose
-- their authored troops under STRIP_TEAMS.
local function roster_teams(obs)
  local has = { false, false, false, false }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          local team = w:team_num()
          if team < C.SCORE_TEAM_COUNT then
            has[team + 1] = true
          end
        end
      end
    end
  end
  return has
end

-- Retire one walker. The corpse moves off the score-team range BEFORE it is
-- marked dead so a same-tick death scan cannot see it as a score-team
-- casualty and respawn it.
local function retire(w)
  w:set_team_num(C.SCORE_TEAM_COUNT)
  w:set_dead(1)
end

-- STRIP_TEAMS: per ACTIVE score team that fields a roster walker, remove the
-- authored livings and generators on that team. Wildlife and neutral teams
-- (>= SCORE_TEAM_COUNT) are kept; this is the versus rule.
local function strip_roster_teams(obs, mask)
  local has_roster = roster_teams(obs)
  local removed = 0
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if is_troop(w:order(), false) then
        if not w:has_guy() then
          local team = w:team_num()
          if team < C.SCORE_TEAM_COUNT then
            if core.mask_has(mask, team) then
              if has_roster[team + 1] then
                retire(w)
                removed = removed + 1
              end
            end
          end
        end
      end
    end
  end
  return removed
end

-- STRIP_ALL: remove every authored living and generator with no roster guy,
-- on ANY team — wildlife included. Groups bring their own fighters, so
-- "ALL" means all. Onslaught passes keep_generators so its foundries stay.
local function strip_everything(obs, keep_generators)
  local removed = 0
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if is_troop(w:order(), keep_generators) then
        if not w:has_guy() then
          retire(w)
          removed = removed + 1
        end
      end
    end
  end
  return removed
end

-- The entry point every mode impl calls from on_mode_init, immediately after
-- its inactive-team strip and BEFORE its empty-team census (so bot backfill
-- sees the post-strip world). opts.keep_generators is set by Onslaught only.
-- Returns how many entities were retired.
local function strip_authored_troops(mask, opts)
  local setting = og.match_setting("strip_troops")
  if setting == KEEP then
    return 0
  end
  local obs = og.oblist()
  if setting == STRIP_ALL then
    local keep_generators = false
    if opts ~= nil then
      keep_generators = opts.keep_generators == true
    end
    return strip_everything(obs, keep_generators)
  end
  if setting ~= STRIP_TEAMS then
    return 0
  end
  return strip_roster_teams(obs, mask)
end

return {
  KEEP = KEEP,
  STRIP_TEAMS = STRIP_TEAMS,
  STRIP_ALL = STRIP_ALL,
  is_troop = is_troop,
  roster_teams = roster_teams,
  strip_authored_troops = strip_authored_troops,
}
