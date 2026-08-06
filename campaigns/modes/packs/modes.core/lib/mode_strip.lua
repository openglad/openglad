-- Scenario-troops strip, shared by all six modes: one rule set for the "bring your own fighters" setting so the modes cannot drift apart.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

-- og.match_setting("strip_troops") states. KEEP is the level exactly as
-- authored (the menus call it "TROOPS: ALL"); OWN drops the whole authored
-- cast so the groups fight with their own fighters ("TROOPS: OWN").
-- The menus only ever write these two, but a save or a peer from a build
-- with the retired middle state can hand us a 1 -- anything above KEEP is
-- read as OWN.
local KEEP = 0
local OWN = 2

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

-- Retire one walker. The corpse moves off the score-team range BEFORE it is
-- marked dead so a same-tick death scan cannot see it as a score-team
-- casualty and respawn it.
local function retire(w)
  w:set_team_num(C.SCORE_TEAM_COUNT)
  w:set_dead(1)
end

-- OWN: remove every authored living and generator with no roster guy, on ANY
-- team -- wildlife included. Groups bring their own fighters, so nothing the
-- level ships survives. Onslaught passes keep_generators so its foundries
-- stay.
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
local function strip_authored_troops(opts)
  if og.match_setting("strip_troops") <= KEEP then
    return 0
  end
  local keep_generators = false
  if opts ~= nil then
    keep_generators = opts.keep_generators == true
  end
  return strip_everything(og.oblist(), keep_generators)
end

return {
  KEEP = KEEP,
  OWN = OWN,
  is_troop = is_troop,
  strip_authored_troops = strip_authored_troops,
}
