-- core pack: lineup stage — the mode-less on_lineup_stage step (docs/lineup-design.md C2-C4): per-team MAP UNITS strip and FILL squads on classic levels, registered for every level (cookbook: docs/lua-classpacks-design.md §3).
-- The classic-level twin of the modes' decide folds, registered with the
-- -1 wildcard; a campaign overrides per level id (the level-hook seam).
-- The engine dispatches on_lineup_stage only where NO mode owns the level:
-- MatchStage step 8b on staged worlds, the tick-1 lazy arm on un-staged
-- ones (game_world.cpp), so preview == launch on classic campaigns.
--
-- THE CLASSIC RULES (the rulings this file settles, recorded in the design
-- doc's "As built: W6-A" as corrected by the D-series and Amendment 4 —
-- E1 supersedes C8 and the D1 scale: the stored 0 IS NONE, there is no
-- default to resolve):
--   * the MAP UNITS box strips a team's authored units exactly as in the
--     modes (one strip implementation, lineup.strip_authored_troops).
--   * A TURNED WHEEL ALWAYS FIELDS (D2/D3): any code but NONE fields a
--     solved squad on its team — BESIDE standing map troops (D3: the
--     troops are not the answer to a turned wheel), on a ships-empty
--     authored team, and on a team the map does not author at all (D2:
--     the squad turns the team on — hostile to all, ordinary walkers).
--     The one team shape that never fields here is a team with a
--     deployed roster fighter: allies are a match-mode rule.
--   * NONE = NO SQUAD (E1/E3): the stored 0 is NONE on every team, so an
--     untouched wheel is the map's own value: nothing (C3: the
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
  -- plumbing: Initialises one empty census row per score team (0..3).
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
  -- plumbing: Census walk over oblist; rules in lineup_stage.lua:64, :66, :69, :76.
  for k = 1, #obs do
    local w = obs[k]
    -- rule: The classic lineup census counts only live, awake walkers: dead bodies and dormant
    --   (not-yet-woken) walkers neither form a team's centroid nor mark it as having a roster or a
    --   start marker.
    -- tests: ClassicLineupTest.traded_units_become_a_fair_squad_at_their_centroid
    -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
    -- tests: SemLineupStage.the_census_counts_only_live_awake_walkers
    if w:dead() == 0 and not w:dormant() then
      local team = w:team_num()
      -- rule: Only the four score teams take part in the classic lineup; wildlife and other teams
      --   numbered 4+ are never censused, never anchor a squad and never count as an existing team
      --   for the farthest-region rule (they still count as hostiles, see :139).
      -- tests: ClassicLineupTest.traded_units_become_a_fair_squad_at_their_centroid
      -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
      -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
      if team < C.SCORE_TEAM_COUNT then
        local row = teams[team + 1]
        local order = w:order()
        -- rule: A team's start marker is the FIRST live team-marker special for that team in list
        --   order; later markers of the same team are ignored for anchoring.
        -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
        -- tests: SemLineupStage.the_first_start_marker_of_a_team_anchors_its_squad
        if order == C.ORDER_SPECIAL then
          -- rule: Part of the rule above that begins "A team's start marker is the FIRST live
          --   team-marker ..."
          -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
          -- tests: SemLineupStage.the_first_start_marker_of_a_team_anchors_its_squad
          if w:family() == C.FAMILY_RESERVED_TEAM then
            -- rule: Part of the rule above that begins "A team's start marker is the FIRST live
            --   team-marker ..."
            -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
            -- tests: SemLineupStage.the_first_start_marker_of_a_team_anchors_its_squad
            if row.mx < 0 then
              row.mx = w:xpos()
              row.my = w:ypos()
            end
          end
        elseif lineup.is_troop(order) then  -- rule: Living walkers and generators are a team's
          --   troops; those carrying a guy (deployed roster heroes) count as the team's roster,
          --   guy-less ones as its authored units. Only authored units form the squad anchor; both
          --   count toward the team's centroid for the farthest-region rule.
          -- tests: ClassicLineupTest.traded_units_become_a_fair_squad_at_their_centroid
          -- tests: SemLineupStage.items_and_effects_on_a_team_are_not_its_units
          -- tests: SemLineupStage.a_team_with_a_deployed_fighter_fields_no_squad
          -- rule: Part of the rule above that begins "Living walkers and generators are a team's
          --   troops; those ..."
          -- tests: ClassicLineupTest.traded_units_become_a_fair_squad_at_their_centroid
          -- tests: SemLineupStage.items_and_effects_on_a_team_are_not_its_units
          -- tests: SemLineupStage.a_team_with_a_deployed_fighter_fields_no_squad
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
  -- rule: A FILL squad anchors at its team's authored units' centroid (grid-snapped to 16 px;
  --   standing or box-retired units alike) when the team authors any, else at the team's start
  --   marker; a team with neither is site-less (-1,-1).
  -- tests: ClassicLineupTest.traded_units_become_a_fair_squad_at_their_centroid
  -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
  -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
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
  -- plumbing: Per-team centroid list; rule in lineup_stage.lua:118.
  for t = 1, C.SCORE_TEAM_COUNT do
    local row = teams[t]
    local bodies = row.units + row.roster
    -- rule: For the site-less squad's farthest-region rule, a team 'exists' where it has any troops
    --   (units or roster, measured at their grid-snapped centroid) or else a start marker (measured
    --   at the marker); a team with nothing does not exist.
    -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
    -- tests: SemLineupStage.a_team_with_nothing_adds_no_centroid
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
  -- plumbing: Hostile list walk; rule in lineup_stage.lua:137.
  for k = 1, #obs do
    local w = obs[k]
    -- rule: For the site-less squad's spawn safety, a hostile is any live, awake living or
    --   generator not on the squad's team — wildlife and other teams included, the squad's own team
    --   excluded; units stripped by the MAP UNITS box are already dead and no longer count.
    -- tests: ClassicLineupTest.explicit_fair_fields_a_safe_squad_on_unauthored_ground
    -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
    -- tests: SemLineupStage.a_dormant_or_dead_walker_or_an_item_is_no_hostile
    if w:dead() == 0 and not w:dormant() then
      -- rule: Part of the rule above that begins "For the site-less squad's spawn safety, a hostile
      --   is ..."
      -- tests: ClassicLineupTest.explicit_fair_fields_a_safe_squad_on_unauthored_ground
      -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
      -- tests: SemLineupStage.a_dormant_or_dead_walker_or_an_item_is_no_hostile
      if lineup.is_troop(w:order()) then
        -- rule: Part of the rule above that begins "For the site-less squad's spawn safety, a
        --   hostile is ..."
        -- tests: ClassicLineupTest.explicit_fair_fields_a_safe_squad_on_unauthored_ground
        -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
        -- tests: SemLineupStage.a_dormant_or_dead_walker_or_an_item_is_no_hostile
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
  -- plumbing: Hostile adjacency scan; rule in lineup_stage.lua:158.
  for k = 1, #hostiles do
    local h = hostiles[k]
    local dx = lineup.iabs(px - h.x)
    local dy = lineup.iabs(py - h.y)
    -- rule: A spawn cell is hostile-adjacent when it is a hostile's own tile or one of its eight
    --   neighbours (Chebyshev distance <= 1 tile); a site-less squad never takes such a cell when
    --   another is available.
    -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
    -- tests: ClassicLineupTest.explicit_fair_fields_a_safe_squad_on_unauthored_ground
    -- tests: SemLineupStage.a_hostile_two_tiles_away_does_not_refuse_a_cell
    if dx <= 16 then
      -- rule: Part of the rule above that begins "A spawn cell is hostile-adjacent when it is a
      --   ..."
      -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
      -- tests: ClassicLineupTest.explicit_fair_fields_a_safe_squad_on_unauthored_ground
      -- tests: SemLineupStage.a_hostile_two_tiles_away_does_not_refuse_a_cell
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
    -- plumbing: classic_reach widen() list walk; rule in lineup_stage.lua:179.
    for k = 1, #list do
      local w = list[k]
      -- rule: The site-less anchor search only scans the authored extent: from (0,0) to the largest
      --   x and largest y of any entity (oblist and fxlist) plus a six-tile (96 px) margin; open
      --   ground beyond that is never chosen.
      -- tests: SemLineupStage.the_siteless_scan_reaches_only_the_authored_extent
      if w:xpos() > reach_x then
        reach_x = w:xpos()
      end
      -- rule: Part of the rule above that begins "The site-less anchor search only scans the
      --   authored extent: ..."
      -- tests: SemLineupStage.the_siteless_scan_reaches_only_the_authored_extent
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
  -- rule: On a map with no existing team at all, a site-less squad has no region to flee and is
  --   scattered by the bounded safe-teleport draw instead.
  -- tests: SemLineupStage.a_map_with_no_team_scatters_the_siteless_squad_by_teleport
  -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
  if #centroids == 0 then
    return -1, -1
  end
  local reach_x, reach_y = classic_reach()
  local best_x = -1
  local best_y = -1
  local best_score = -1
  local py = 0
  -- plumbing: Row-major 32-px lattice scan; rules in lineup_stage.lua:216 and :222.
  while py <= reach_y do
    local px = 0
    -- plumbing: Row-major 32-px lattice scan; rules in lineup_stage.lua:216 and :222.
    while px <= reach_x do
      local score = -1
      -- plumbing: Per-centroid distance; rule in lineup_stage.lua:216.
      for c = 1, #centroids do
        local dx = px - centroids[c].x
        local dy = py - centroids[c].y
        local d2 = dx * dx + dy * dy
        -- rule: A lattice cell's score is its squared distance to the NEAREST existing team
        --   centroid.
        -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
        if score < 0 then
          score = d2
        elseif d2 < score then
          score = d2
        end
      end
      -- rule: The site-less squad anchor is the first cell in row-major order, on a two-tile
      --   lattice, that is farthest from every existing team (max of min distance), not
      --   hostile-adjacent and clear for the probing member; ties keep the earlier cell. No
      --   admissible cell answers (-1,-1).
      -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
      -- tests: SemLineupStage.a_blocked_or_hostile_adjacent_best_cell_is_passed_over
      -- tests: SemLineupStage.with_no_admissible_cell_the_siteless_squad_teleports
      if score > best_score then
        -- rule: Part of the rule above that begins "The site-less squad anchor is the first cell in
        --   ..."
        -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
        -- tests: SemLineupStage.a_blocked_or_hostile_adjacent_best_cell_is_passed_over
        -- tests: SemLineupStage.with_no_admissible_cell_the_siteless_squad_teleports
        if not hostile_adjacent(hostiles, px, py) then
          -- rule: Part of the rule above that begins "The site-less squad anchor is the first cell
          --   in ..."
          -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
          -- tests: SemLineupStage.a_blocked_or_hostile_adjacent_best_cell_is_passed_over
          -- tests: SemLineupStage.with_no_admissible_cell_the_siteless_squad_teleports
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
  -- rule: Each squad member takes the anchor tile if it is clear (and, for a site-less squad, not
  --   hostile-adjacent); otherwise the first clear (and safe) cell on the diamond rings of radius
  --   1, 2, 3 tiles around the anchor in the ring_offset order; cells at negative coordinates are
  --   skipped.
  -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
  -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
  -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
  if ax >= 0 then
    -- rule: Part of the rule above that begins "Each squad member takes the anchor tile if it ..."
    -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
    -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
    -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
    if og.spawn_spot_clear(w, ax, ay) then
      -- rule: Part of the rule above that begins "Each squad member takes the anchor tile if it
      --   ..."
      -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
      -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
      -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
      if hostiles == nil or not hostile_adjacent(hostiles, ax, ay) then
        w:setxy(ax, ay)
        return true
      end
    end
    -- rule: Part of the rule above that begins "Each squad member takes the anchor tile if it ..."
    -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
    -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
    -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
    for r = 1, 3 do
      -- rule: Part of the rule above that begins "Each squad member takes the anchor tile if it
      --   ..."
      -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
      -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
      -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
      for i = 0, 4 * r - 1 do
        local dx, dy = lineup.ring_offset(i, r)
        local px = ax + dx * 16
        local py = ay + dy * 16
        -- rule: Part of the rule above that begins "Each squad member takes the anchor tile if it
        --   ..."
        -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
        -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
        -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
        if px >= 0 and py >= 0 then
          -- rule: Part of the rule above that begins "Each squad member takes the anchor tile if it
          --   ..."
          -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
          -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
          -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
          if og.spawn_spot_clear(w, px, py) then
            -- rule: Part of the rule above that begins "Each squad member takes the anchor tile if
            --   it ..."
            -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
            -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
            -- tests: SemLineupStage.a_sited_member_takes_the_anchor_then_the_ring_in_order
            if hostiles == nil or not hostile_adjacent(hostiles, px, py) then
              w:setxy(px, py)
              return true
            end
          end
        end
      end
    end
  end
  -- rule: A placer that is not allowed the teleport draw gives up when the anchor ring is full.
  -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
  -- unreachable: the only caller, lineup.lua place_member, always passes allow_teleport = true
  --   (triage unreachable)
  if not allow_teleport then
    return false
  end
  -- rule: A sited squad member whose anchor ring is full is placed by one teleport draw anywhere
  --   legal.
  -- tests: ClassicLineupTest.blocked_anchor_falls_back_to_the_teleport_draw
  -- tests: SemLineupStage.a_siteless_member_redraws_an_unsafe_landing_and_keeps_the_first_safe_one
  if hostiles == nil then
    return w:teleport()
  end
  local landed = false
  -- plumbing: Up to eight seeded teleport draws; rule in lineup_stage.lua:278.
  for _ = 1, 8 do
    -- rule: A site-less squad member that has to teleport re-draws up to eight times and keeps the
    --   first landing that is not hostile-adjacent; if none of the eight is safe, the last
    --   successful landing stands (a placed squad beats a refusal).
    -- tests: SemLineupStage.a_siteless_member_redraws_an_unsafe_landing_and_keeps_the_first_safe_one
    -- tests: SemLineupStage.a_siteless_member_with_eight_unsafe_landings_stays_on_the_last
    -- tests: SemLineupStage.a_siteless_member_whose_teleports_all_fail_stays_where_it_spawned
    if w:teleport() then
      landed = true
      -- rule: Part of the rule above that begins "A site-less squad member that has to teleport
      --   re-draws ..."
      -- tests: SemLineupStage.a_siteless_member_redraws_an_unsafe_landing_and_keeps_the_first_safe_one
      -- tests: SemLineupStage.a_siteless_member_with_eight_unsafe_landings_stays_on_the_last
      -- tests: SemLineupStage.a_siteless_member_whose_teleports_all_fail_stays_where_it_spawned
      if not hostile_adjacent(hostiles, og.div(w:xpos(), 16) * 16,
                              og.div(w:ypos(), 16) * 16) then
        return true
      end
    end
  end
  return landed
end

-- Should team's row field a FILL squad? The classic fill rule from the
-- header comment, one decision per team, pure over the census row: a
-- turned wheel always fields (D2/D3 — beside troops, on a marker team,
-- on unauthored ground) unless it is NONE (E1: the stored 0, the default
-- everywhere) or a roster fighter stands.
local function classic_wants_squad(row, knob)
  -- rule: On a classic level a team fields a FILL squad only when its wheel is turned (anything but
  --   NONE) and no deployed roster hero stands on it: NONE fields nothing, and a team with a human
  --   fighter never gets bot allies (allies are a match-mode rule).
  -- tests: ClassicLineupTest.default_none_keeps_a_ships_empty_team_empty
  -- tests: ClassicLineupTest.the_default_never_adds_a_squad_beside_anything
  -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
  -- tests: SemLineupStage.a_team_with_a_deployed_fighter_fields_no_squad
  if lineup.squad_off(knob) then
    return false
  end
  return row.roster == 0
end

-- The stage step. The C3 fast path returns before any world read: with
-- every box on and every wheel at NONE (E1: the stored 0, the default on
-- every map) nothing below could strip or spawn, so the all-default
-- stage costs eight knob reads and is a proven byte no-op (every parity
-- scenario rides this arm). NONE banks no fact either (E4: NONE never
-- spawns and never banks), so the untouched world writes nothing.
local function stage_level(level)
  local touched = false
  -- plumbing: Fast-path knob scan over teams 0..3; rule in lineup_stage.lua:317.
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    -- rule: With every MAP UNITS box on and every FILL wheel at NONE the classic lineup stage does
    --   nothing at all — no strip, no spawn, no RNG draw, no byte of the world changes; any one
    --   turned control runs the full census/strip/fill pass.
    -- tests: ClassicLineupTest.all_default_stage_is_a_byte_noop_on_gladiator
    -- tests: ClassicLineupTest.gladiator_map_units_off_strips_one_team
    -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
    if not lineup.map_units_fielded(team) then
      touched = true
    end
    -- rule: Part of the rule above that begins "With every MAP UNITS box on and every FILL ..."
    -- tests: ClassicLineupTest.all_default_stage_is_a_byte_noop_on_gladiator
    -- tests: ClassicLineupTest.gladiator_map_units_off_strips_one_team
    -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
    if not lineup.squad_off(lineup.fill_knob(team)) then
      touched = true
    end
  end
  -- rule: Part of the rule above that begins "With every MAP UNITS box on and every FILL ..."
  -- tests: ClassicLineupTest.all_default_stage_is_a_byte_noop_on_gladiator
  -- tests: ClassicLineupTest.gladiator_map_units_off_strips_one_team
  -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
  if not touched then
    return
  end
  local teams = classic_census(og.oblist())
  lineup.strip_authored_troops()
  -- plumbing: Per-team fill pass; rules in lineup_stage.lua:295, :329, :339.
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local row = teams[team + 1]
    local knob = lineup.fill_knob(team)
    -- rule: Part of the rule above that begins "On a classic level a team fields a FILL ..."
    -- tests: ClassicLineupTest.default_none_keeps_a_ships_empty_team_empty
    -- tests: ClassicLineupTest.the_default_never_adds_a_squad_beside_anything
    -- tests: ClassicLineupTest.fill_strong_spawns_a_solved_squad_on_a_marker_team
    -- tests: SemLineupStage.a_team_with_a_deployed_fighter_fields_no_squad
    if classic_wants_squad(row, knob) then
      local ax, ay = classic_anchor(row)
      local hostiles = nil
      local centroids = nil
      -- rule: Only a team with no authored units and no start marker uses the spawn-safety rules
      --   (farthest region, never adjacent to a hostile); a sited squad may spawn next to enemies.
      -- tests: ClassicLineupTest.hostile_crowd_pushes_the_siteless_squad_clear
      -- tests: ClassicLineupTest.explicit_fair_fields_a_safe_squad_on_unauthored_ground
      -- tests: SemLineupStage.a_sited_squad_may_spawn_beside_a_hostile
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
        -- rule: A site-less squad picks its anchor once, using its first member as the probe; every
        --   later member rings around that same anchor.
        -- tests: SemLineupStage.a_siteless_squad_anchors_farthest_from_every_team_and_rings_there
        if not sited then
          sited = true
          ax, ay = farthest_open_spot(w, centroids, hostiles)
        end
        return classic_place(w, ax, ay, allow_teleport, hostiles)
      end
      -- The one squad seam (solver reference per B3: the weakest human
      -- team's f-sum; no humans -> the legacy difficulty formula). No
      -- cursor slot — the classic placer never reads one — no announce:
      -- TEAMS MATCHED is match-mode vocabulary. The seam re-reads the
      -- stored knob itself (E1: the code is the fill, nothing resolves).
      lineup.spawn_bots(team, lineup.BOT_SQUAD, nil, placer, nil, nil)
    end
  end
end

og.register_level_hooks(-1, { on_lineup_stage = stage_level })
