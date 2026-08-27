-- core pack: lineup stage — the mode-less on_lineup_stage step (docs/lineup-design.md C2-C4): per-team MAP UNITS strip and FILL squads on classic levels, registered for every level (cookbook: docs/lua-classpacks-design.md §3).
-- The classic-level twin of the modes' decide folds, registered with the
-- -1 wildcard; a campaign overrides per level id (the level-hook seam).
-- The engine dispatches on_lineup_stage only where NO mode owns the level:
-- MatchStage step 8b on staged worlds, the tick-1 lazy arm on un-staged
-- ones (game_world.cpp), so preview == launch on classic campaigns.
--
-- THE CLASSIC RULES (the rulings this file settles, recorded in the design
-- doc's "As built: W6-A"):
--   * activation = the teams that exist on the map (authored units or a
--     start marker) plus seats/roster. A FILL squad may NOT turn on a team
--     the map does not author — classic maps have no anchors, so an
--     unauthored colour has nowhere to stand.
--   * the MAP UNITS box strips a team's authored units exactly as in the
--     modes (one strip implementation, lineup.strip_authored_troops).
--   * a FILL squad walks on only where an authored team ends up EMPTY:
--     units traded away by the box (any wheel value but NONE — FAIR
--     included, "turn the box off to trade them for a solved squad"), or a
--     ships-empty authored team whose wheel names an explicit non-FAIR
--     value. FAIR on a ships-empty team is the map's own value — nothing
--     (C3: the all-default stage writes nothing, draws nothing, spawns
--     nothing). A team with a deployed roster fields no allies here — the
--     allies gap is a match-mode rule.
--   * no refusals, ever (C4): NONE or an emptied side simply means fewer
--     enemies, and the campaign's own win logic governs. Spawned squads
--     are ordinary walkers (no owner, bot-marked) — remaining-foes
--     counting, XP and drops see them like any authored enemy.
--   * PLACEMENT RULE: each squad anchors at the retired units' centroid
--     (grid-snapped) when the team authored units, else at its start
--     marker; members take the anchor tile, then the mode placer's
--     blocked-cell discipline — the deterministic clockwise ring walk,
--     radius 1..3 — then the blessed teleport draw.
--   * THE RESOLVED DEFAULT (C8): a stored FILL of 0 is the default, not
--     an explicit FAIR — it resolves per team through the lib's ONE rule
--     (lineup.resolved_fill): FAIR where the team has any presence
--     (units, generators, markers, anchors or a deployed fighter), NONE
--     where it has none. The resolved value is what the pass executes
--     and what the facts bank; explicit wheel values are untouched.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local lineup = og.use("lineup")

-- Pre-strip census for the classic rules: per team, the authored (guy-less)
-- unit count with position sums for the centroid, the deployed roster
-- count, and the first start marker. Dormant walkers are skipped like the
-- staged census (they are outside snapshot capture).
local function classic_census(obs)
  local teams = {}
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    teams[team + 1] = { units = 0, sx = 0, sy = 0, roster = 0, mx = -1, my = -1 }
  end
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 and not w:dormant() then
      local team = w:team_num()
      if team < C.SCORE_TEAM_COUNT then
        local row = teams[team + 1]
        local order = w:order()
        if order == C.ORDER_SPECIAL then
          if w:family() == C.FAMILY_RESERVED_TEAM then
            if row.mx < 0 then
              row.mx = w:xpos()
              row.my = w:ypos()
            end
          end
        elseif lineup.is_troop(order) then
          if w:has_guy() then
            row.roster = row.roster + 1
          else
            row.units = row.units + 1
            row.sx = row.sx + w:xpos()
            row.sy = row.sy + w:ypos()
          end
        end
      end
    end
  end
  return teams
end

-- The team's squad anchor: authored units' centroid (grid-snapped so the
-- ring walk stays on tile corners), else the start marker. Total over the
-- rows classic_wants_squad admits: an authored team has units or a marker,
-- so the marker arm is the honest tail, never a fallback past it.
local function classic_anchor(row)
  if row.units > 0 then
    local ax = og.div(og.div(row.sx, row.units), 16) * 16
    local ay = og.div(og.div(row.sy, row.units), 16) * 16
    return ax, ay
  end
  return row.mx, row.my
end

-- One member onto the board: the anchor tile, then the mode placer's ring
-- discipline (lineup.ring_offset, radius 1..3, first clear tile), then the
-- teleport fallback iff the caller allows the draw.
local function classic_place(w, ax, ay, allow_teleport)
  if og.spawn_spot_clear(w, ax, ay) then
    w:setxy(ax, ay)
    return true
  end
  for r = 1, 3 do
    for i = 0, 4 * r - 1 do
      local dx, dy = lineup.ring_offset(i, r)
      local px = ax + dx * 16
      local py = ay + dy * 16
      if px >= 0 and py >= 0 then
        if og.spawn_spot_clear(w, px, py) then
          w:setxy(px, py)
          return true
        end
      end
    end
  end
  if not allow_teleport then
    return false
  end
  return w:teleport()
end

-- The C8 presence row over a classic census row: the census gathers the
-- authored units (generators folded in by is_troop), the deployed roster
-- and the first live start marker; the engine anchor count covers markers
-- the level bootstrap consumed (its scan counts dead ones too). Seats are
-- invisible here by design — at any launch GO can pass, a seat implies a
-- deployed fighter on its team (the M4 refusal), so the roster stands in.
local function presence_row(row, team)
  local markers = 0
  if row.mx >= 0 then
    markers = 1
  end
  return {
    units = row.units,
    markers = markers,
    anchors = og.respawn_anchor_count(team),
    roster = row.roster,
  }
end

-- Should team's row field a FILL squad? The classic fill rule from the
-- header comment, one decision per team, pure over the census row.
local function classic_wants_squad(row, knob, fielded)
  local authored = row.units > 0
  if not authored then
    authored = row.mx >= 0
  end
  if not authored then
    return false
  end
  if row.roster > 0 then
    return false
  end
  if row.units > 0 and fielded then
    return false
  end
  if lineup.squad_off(knob) then
    return false
  end
  if row.units == 0 and knob == lineup.FILL_FAIR then
    return false
  end
  return true
end

-- The stage step. The C3 fast path returns before any world read: with
-- every box on and no wheel past FAIR/NONE nothing below could strip or
-- spawn, so the all-default stage costs eight knob reads and is a proven
-- byte no-op (every parity scenario rides this arm). C8 keeps that proof
-- intact: the resolution maps the default onto FAIR or NONE only, and
-- neither value is a touch — so an all-default world resolves without a
-- single write or draw, and the resolved-NONE fact below banks only on a
-- stage something ELSE already made run.
local function stage_level(level)
  local touched = false
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if not lineup.map_units_fielded(team) then
      touched = true
    end
    local knob = lineup.fill_knob(team)
    if knob ~= lineup.FILL_FAIR then
      if not lineup.squad_off(knob) then
        touched = true
      end
    end
  end
  if not touched then
    return
  end
  local teams = classic_census(og.oblist())
  lineup.strip_authored_troops()
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local row = teams[team + 1]
    local raw = lineup.fill_knob(team)
    -- C8: the stored default resolves per team — FAIR with any presence,
    -- NONE with none — and the RESOLVED value is what this pass executes.
    local knob = lineup.resolved_fill(raw, presence_row(row, team))
    if raw == lineup.FILL_FAIR and knob == lineup.FILL_NONE then
      -- A resolved NONE is a decision this stage made, so it banks like
      -- an applied fill (C8: the facts render the resolved value) — the
      -- one exception to R4's spawned-only rule, because "no squad, by
      -- resolution" IS what was applied. An explicit NONE stays unbanked
      -- (explicit wheel values are unchanged), and the all-default stage
      -- never reaches this line: the fast path above already returned.
      lineup.bank_lineup_facts(team, lineup.FILL_NONE)
    end
    if classic_wants_squad(row, knob, lineup.map_units_fielded(team)) then
      local ax, ay = classic_anchor(row)
      local placer = function(w, t, allow_teleport)
        return classic_place(w, ax, ay, allow_teleport)
      end
      -- The one squad seam (solver reference per B3: the weakest human
      -- team's f-sum; no humans -> the legacy difficulty formula). No
      -- cursor slot — the classic placer never reads one — and no
      -- announce: TEAMS MATCHED is match-mode vocabulary.
      lineup.spawn_bots(team, lineup.BOT_SQUAD, nil, placer, nil)
    end
  end
end

og.register_level_hooks(-1, { on_lineup_stage = stage_level })
