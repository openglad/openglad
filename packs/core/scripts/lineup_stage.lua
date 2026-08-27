-- core pack: lineup stage — the mode-less on_lineup_stage step (docs/lineup-design.md C2-C4): per-team MAP UNITS strip and FILL squads on classic levels, registered for every level (cookbook: docs/lua-classpacks-design.md §3).
-- The classic-level twin of the modes' decide folds, registered with the
-- -1 wildcard; a campaign overrides per level id (the level-hook seam).
-- The engine dispatches on_lineup_stage only where NO mode owns the level:
-- MatchStage step 8b on staged worlds, the tick-1 lazy arm on un-staged
-- ones (game_world.cpp), so preview == launch on classic campaigns.
--
-- THE CLASSIC RULES (the rulings this file settles, recorded in the design
-- doc's "As built: W6-A" as corrected by the D-series — D2 and D3
-- supersede W6-A's "may not turn on an unauthored team" and W5-A's
-- "troops-only teams get no squad"):
--   * the MAP UNITS box strips a team's authored units exactly as in the
--     modes (one strip implementation, lineup.strip_authored_troops).
--   * AN EXPLICIT WHEEL VALUE ALWAYS FIELDS (D2/D3): any explicit code
--     but NONE fields a solved squad on its team — BESIDE standing map
--     troops (D3: the troops are not the answer to a turned wheel), on a
--     ships-empty authored team, and on a team the map does not author
--     at all (D2: the squad turns the team on — hostile to all, ordinary
--     walkers). The one team shape that never fields here is a team with
--     a deployed roster fighter: allies are a match-mode rule.
--   * THE STORED DEFAULT NEVER ADDS A SQUAD BESIDE ANYTHING: a default
--     that resolves FAIR fields only where the box traded the team's
--     authored units away ("turn the box off to trade them for a solved
--     squad"); on a troops-fielded team it stays squadless — only an
--     explicit choice puts bots beside troops — and on a ships-empty or
--     unauthored team it is the map's own value: nothing (C3: the
--     all-default stage writes nothing, draws nothing, spawns nothing).
--   * no refusals, ever (C4): NONE or an emptied side simply means fewer
--     enemies, and the campaign's own win logic governs. Spawned squads
--     are ordinary walkers (no owner, bot-marked) — remaining-foes
--     counting, XP and drops see them like any authored enemy.
--   * PLACEMENT RULE: each squad anchors at its troops' centroid
--     (grid-snapped; retired or standing alike) when the team authors
--     units, else at its start marker; members take the anchor tile,
--     then the mode placer's blocked-cell discipline — the deterministic
--     clockwise ring walk, radius 1..3 — then the blessed teleport draw.
--     A SITE-LESS team (D2: no units, no marker) anchors at the walkable
--     spot farthest from every existing team's centroid and never lands
--     a member adjacent to a hostile — the rule lives with
--     classic_anchor/classic_place below.
--   * THE RESOLVED DEFAULT (C8/D1): a stored FILL of 0 is the default,
--     not an explicit FAIR — it resolves per team through the lib's ONE
--     rule (lineup.resolved_fill): the explicit FAIR where the team has
--     any presence (units, generators, markers, anchors or a deployed
--     fighter), the explicit NONE where it has none. The resolved value
--     is what the pass executes and what the facts bank; explicit wheel
--     values are untouched.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local lineup = og.use("lineup")

-- Pre-strip census for the classic rules: per team, the authored (guy-less)
-- unit count with position sums for the centroid, the deployed roster
-- count with its own position sums (the D2 farthest-region rule measures
-- every existing team by where it stands, roster included), and the first
-- start marker. Dormant walkers are skipped like the staged census (they
-- are outside snapshot capture).
local function classic_census(obs)
  local teams = {}
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    teams[team + 1] = {
      units = 0,
      sx = 0,
      sy = 0,
      roster = 0,
      rsx = 0,
      rsy = 0,
      mx = -1,
      my = -1,
    }
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
            row.rsx = row.rsx + w:xpos()
            row.rsy = row.rsy + w:ypos()
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

-- The team's squad anchor: the team's troops' centroid (grid-snapped so
-- the ring walk stays on tile corners) when it authors units — standing
-- or box-retired alike, so a D3 squad walks on BESIDE its troops — else
-- the start marker. A site-less team (D2: no units, no marker) answers
-- (-1, -1) and takes the farthest-region rule below.
local function classic_anchor(row)
  if row.units > 0 then
    local ax = og.div(og.div(row.sx, row.units), 16) * 16
    local ay = og.div(og.div(row.sy, row.units), 16) * 16
    return ax, ay
  end
  return row.mx, row.my
end

-- The existing teams' centroids for the D2 farthest-region rule: a team
-- exists where it has any units, roster fighters or a start marker, and
-- its centroid is where those troops stand (grid-snapped; a marker-only
-- team is measured at its marker). Computed from the STAGED world's
-- census rows — squads this same pass fields on earlier teams shift no
-- centroid, so the rule is a pure function of the authored world.
local function classic_centroids(teams)
  local centroids = {}
  for t = 1, C.SCORE_TEAM_COUNT do
    local row = teams[t]
    local bodies = row.units + row.roster
    if bodies > 0 then
      local cx = og.div(og.div(row.sx + row.rsx, bodies), 16) * 16
      local cy = og.div(og.div(row.sy + row.rsy, bodies), 16) * 16
      centroids[#centroids + 1] = { x = cx, y = cy }
    elseif row.mx >= 0 then
      centroids[#centroids + 1] = { x = row.mx, y = row.my }
    end
  end
  return centroids
end

-- The live hostiles a D2 squad must not spawn against: every live,
-- non-dormant living or generator off the squad's own team — wildlife
-- included, it attacks anything. Grid-snapped, gathered fresh AFTER the
-- strip so a box-retired unit no longer scares the placer.
local function classic_hostiles(obs, team)
  local hostiles = {}
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 and not w:dormant() then
      if lineup.is_troop(w:order()) then
        if w:team_num() ~= team then
          hostiles[#hostiles + 1] = {
            x = og.div(w:xpos(), 16) * 16,
            y = og.div(w:ypos(), 16) * 16,
          }
        end
      end
    end
  end
  return hostiles
end

-- Is the tile at (px, py) adjacent to a hostile (D2's spawn-safety rule)?
-- Adjacent = the hostile's tile or any of its eight neighbours.
local function hostile_adjacent(hostiles, px, py)
  for k = 1, #hostiles do
    local h = hostiles[k]
    local dx = lineup.iabs(px - h.x)
    local dy = lineup.iabs(py - h.y)
    if dx <= 16 then
      if dy <= 16 then
        return true
      end
    end
  end
  return false
end

-- How far the D2 region scan reaches: the authored extent — the farthest
-- entity on either list, plus a six-tile margin. The engine exposes no
-- map dimensions to scripts, and spawn_spot_clear refuses every
-- off-map cell, so an over-estimate costs probes, not correctness; a
-- walkable region past every authored entity's margin is outside the
-- scan by design (the work stays bounded).
local function classic_reach()
  local reach_x = 0
  local reach_y = 0
  local function widen(list)
    for k = 1, #list do
      local w = list[k]
      if w:xpos() > reach_x then
        reach_x = w:xpos()
      end
      if w:ypos() > reach_y then
        reach_y = w:ypos()
      end
    end
  end
  widen(og.oblist())
  widen(og.fxlist())
  return reach_x + 96, reach_y + 96
end

-- The D2 site-less anchor: deterministic from the match seed alone —
-- scan the reachable tiles on a two-tile lattice in row-major order and
-- keep the first cell that maximises the MINIMUM squared distance to
-- every existing team's centroid, is walkable for the probing member
-- and is not adjacent to a hostile. No centroids (a wholly empty map)
-- or no admissible cell answers (-1, -1), which sends the members to
-- the bounded safe-teleport scatter in classic_place.
local function farthest_open_spot(w, centroids, hostiles)
  if #centroids == 0 then
    return -1, -1
  end
  local reach_x, reach_y = classic_reach()
  local best_x = -1
  local best_y = -1
  local best_score = -1
  local py = 0
  while py <= reach_y do
    local px = 0
    while px <= reach_x do
      local score = -1
      for c = 1, #centroids do
        local dx = px - centroids[c].x
        local dy = py - centroids[c].y
        local d2 = dx * dx + dy * dy
        if score < 0 then
          score = d2
        elseif d2 < score then
          score = d2
        end
      end
      if score > best_score then
        if not hostile_adjacent(hostiles, px, py) then
          if og.spawn_spot_clear(w, px, py) then
            best_x = px
            best_y = py
            best_score = score
          end
        end
      end
      px = px + 32
    end
    py = py + 32
  end
  return best_x, best_y
end

-- One member onto the board: the anchor tile, then the mode placer's ring
-- discipline (lineup.ring_offset, radius 1..3, first clear tile), then the
-- teleport fallback iff the caller allows the draw. hostiles (the D2
-- site-less path only) arms the spawn-safety rule: every probe also
-- refuses a hostile-adjacent cell, and the teleport tail becomes the
-- bounded safety re-probe — up to eight seeded draws, the first safe
-- landing wins, and the last landing stands when none is (a placed squad
-- beats a refusal, C4).
local function classic_place(w, ax, ay, allow_teleport, hostiles)
  if ax >= 0 then
    if og.spawn_spot_clear(w, ax, ay) then
      if hostiles == nil or not hostile_adjacent(hostiles, ax, ay) then
        w:setxy(ax, ay)
        return true
      end
    end
    for r = 1, 3 do
      for i = 0, 4 * r - 1 do
        local dx, dy = lineup.ring_offset(i, r)
        local px = ax + dx * 16
        local py = ay + dy * 16
        if px >= 0 and py >= 0 then
          if og.spawn_spot_clear(w, px, py) then
            if hostiles == nil or not hostile_adjacent(hostiles, px, py) then
              w:setxy(px, py)
              return true
            end
          end
        end
      end
    end
  end
  if not allow_teleport then
    return false
  end
  if hostiles == nil then
    return w:teleport()
  end
  local landed = false
  for _ = 1, 8 do
    if w:teleport() then
      landed = true
      if not hostile_adjacent(hostiles, og.div(w:xpos(), 16) * 16,
                              og.div(w:ypos(), 16) * 16) then
        return true
      end
    end
  end
  return landed
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
-- header comment, one decision per team, pure over the census row. raw
-- is the stored knob, knob its C8 resolution: an EXPLICIT wheel value
-- always fields (D2/D3 — beside troops, on a marker team, on unauthored
-- ground) unless it is NONE or a roster fighter stands; the stored
-- DEFAULT fields only the box-trade (units authored, box off), never a
-- squad beside anything — only an explicit choice adds bots beside
-- troops, and a default on a ships-empty or unauthored team is the
-- map's own value.
local function classic_wants_squad(row, raw, knob, fielded)
  if lineup.squad_off(knob) then
    return false
  end
  if row.roster > 0 then
    return false
  end
  if raw ~= lineup.FILL_DEFAULT then
    return true
  end
  if row.units > 0 then
    return not fielded
  end
  return false
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
    if knob ~= lineup.FILL_DEFAULT then
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
    -- C8/D1: the stored default resolves per team — the explicit FAIR
    -- with any presence, the explicit NONE with none — and the RESOLVED
    -- value is what this pass executes.
    local knob = lineup.resolved_fill(raw, presence_row(row, team))
    if raw == lineup.FILL_DEFAULT and knob == lineup.FILL_NONE then
      -- A resolved NONE is a decision this stage made, so it banks like
      -- an applied fill (C8: the facts render the resolved value) — the
      -- one exception to R4's spawned-only rule, because "no squad, by
      -- resolution" IS what was applied. An explicit NONE stays unbanked
      -- (explicit wheel values are unchanged), and the all-default stage
      -- never reaches this line: the fast path above already returned.
      lineup.bank_lineup_facts(team, lineup.FILL_NONE)
    end
    if classic_wants_squad(row, raw, knob, lineup.map_units_fielded(team)) then
      local ax, ay = classic_anchor(row)
      local hostiles = nil
      local centroids = nil
      if ax < 0 then
        -- D2, the site-less team: the anchor is chosen on the first
        -- member (spawn_spot_clear probes a real walker), farthest from
        -- every existing team, spawn-safe; its squadmates ring around
        -- it under the same safety rule.
        hostiles = classic_hostiles(og.oblist(), team)
        centroids = classic_centroids(teams)
      end
      local sited = ax >= 0
      local placer = function(w, t, allow_teleport)
        if not sited then
          sited = true
          ax, ay = farthest_open_spot(w, centroids, hostiles)
        end
        return classic_place(w, ax, ay, allow_teleport, hostiles)
      end
      -- The one squad seam (solver reference per B3: the weakest human
      -- team's f-sum; no humans -> the legacy difficulty formula). No
      -- cursor slot — the classic placer never reads one — no announce:
      -- TEAMS MATCHED is match-mode vocabulary — and the knob rides in
      -- already resolved, so the seam never re-reads the raw default.
      lineup.spawn_bots(team, lineup.BOT_SQUAD, nil, placer, nil, nil, knob)
    end
  end
end

og.register_level_hooks(-1, { on_lineup_stage = stage_level })
