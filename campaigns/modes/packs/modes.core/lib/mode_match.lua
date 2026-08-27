-- Shared match toolkit — manifest row adapter, the staged-init census (census_inputs) and the shared activation/fill rules all five mask modes apply at on_mode_init, marker consumption, dead-competitor scheduling, timeout ladder (cookbook: docs/lua-classpacks-design.md §3). The match MACHINERY — power metric, census, solver, plan, knobs, squad spawn, strip and fact banking — lives in the shared core pack (docs/lineup-design.md C1) and is consumed here via the pack-qualified og.use; this module keeps only the mode-specific rules (masks, refusals, announces, flag rows) and re-exports the core names its consumers bind, so no rule lives twice.
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local caps = og.use("mode_caps")
local lineup = og.use("core:lineup")

-- The core-lib names this module leans on locally (the shared MATCHED
-- header slots and the wheel vocabulary — one scale, one home).
local MATCHED = lineup.MATCHED
local FILL_DEFAULT = lineup.FILL_DEFAULT
local MAP_UNITS_ON = lineup.MAP_UNITS_ON

-- The D9 level-id band the manifest may populate. rows_for scans it by
-- DIRECT indexing: the manifest is a map keyed by level id with holes, and
-- the sandbox has no pairs — so the adapter, not the caller, owns the band.
local FIRST_LEVEL_ID = 300
local LAST_LEVEL_ID = 899

-- Adapts og.use("mode_levels") to the row array core.register_mode wants
-- ({ id, mode } per populated manifest id, ascending).
local function rows_for(levels)
  local rows = {}
  for id = FIRST_LEVEL_ID, LAST_LEVEL_ID do
    local row = levels.levels[id]
    if row ~= nil then
      rows[#rows + 1] = { id = id, mode = row.mode }
    end
  end
  return rows
end

-- Manifest limit resolution, one field at a time: an explicit match request
-- wins, then the row's own field, then the mode default. The guards are
-- per FIELD (Soccer's shape) so a row that omits a limit falls back instead
-- of erroring on a nil compare. The shipped generator emits both fields for
-- every row, so this is belt-and-braces for hand-written and future rows.
local function resolve_limit(row, field, requested, fallback)
  if requested > 0 then
    return requested
  end
  if row == nil then
    return fallback
  end
  local value = row[field]
  if value == nil then
    return fallback
  end
  if value <= 0 then
    return fallback
  end
  return value
end

-- The match clock, for every mode that has one. The lobby's TIME LIMIT knob
-- (0 = "the map's own value") beats the manifest row's time_limit, which
-- beats the mode's own default -- the same ladder as every other limit, so
-- the knob's name is spelled exactly once in all of the mode Lua.
-- The value is read once and is fixed for the level's lifetime everywhere
-- except CTF, whose mode-var band is full and which therefore re-resolves
-- at the comparison; both shapes read the same replicated world field, so
-- peers agree either way.
-- NOTE: unrelated to the .glad level field also called time_limit -- that
-- one is the score/par BONUS clock (GameWorld::time_bonus_limit).
local function resolve_time_limit(row, fallback)
  return resolve_limit(row, "time_limit", og.match_setting("time_limit"),
                       fallback)
end

-- The refusal REASON digit, banked ABOVE the four team codes in the same
-- shared slot: 10^9 is the last decimal digit an int32 slot holds beside
-- the latch and the four base-100 codes (1 + 4 * 2 = 9 digits below it).
-- 1 = a band mode (FFA/mutant) refused for want of fighters, which the
-- staged report renders as FEWER THAN 2 FIGHTERS; 0 = the team modes'
-- sentence, and what every world that banks nothing reads as. The band
-- decide fold writes it BEFORE it raises — the one world write a refusal
-- makes, so host and joiner mirrors render the identical line instead of
-- parsing the free-text script error. C++ twin: picker_common.cpp
-- kLineupRefusalBase / lineup_refusal_code. Refusals are match-mode rules
-- (C4: classic levels never refuse), so the digit lives here, not in the
-- core lib.
local REFUSAL_BASE = 1000000000

local function bank_refusal_fighters()
  local v = og.mode_get(MATCHED.ANNOUNCED)
  if og.mod(og.div(v, REFUSAL_BASE), 10) == 1 then
    return
  end
  og.mode_set(MATCHED.ANNOUNCED, v + REFUSAL_BASE)
end

-- ---------------------------------------------------------------------------
-- The staged-init census and the shared activation/fill rules
-- ---------------------------------------------------------------------------

-- The staged-init census (#218): the core half builds the inputs shape —
-- teams, knobs, score limit — and this wrapper appends the raw flag rows
-- in fx order (out-of-range and surplus included — the fold decides).
-- Flags are MODE data (the modes campaign's own treasure family), which is
-- why the scan lives here and not in the core lib.
local function census_inputs()
  local inputs = lineup.census_inputs()
  local flag_family = og.family_id("treasure", "modes:flag")
  local fxlist = og.fxlist()
  for k = 1, #fxlist do
    local e = fxlist[k]
    if e:dead() == 0 then
      if e:order() == C.ORDER_TREASURE then
        if e:family() == flag_family then
          inputs.flags[#inputs.flags + 1] = {
            team = e:team_num(),
            level = e:s_level(),
          }
        end
      end
    end
  end
  return inputs
end

-- Team activation over the census inputs, the shared ruling all five mask
-- modes apply (issue #218, amended 2026-08-26 by lineup B1-B4): the
-- ONE copy of the rule, consumed by each mode's decide fold at the top
-- of on_mode_init (only the inputs decide). "A team is on when anything
-- is on it" — a seat (whose team always carries a deployed fighter: GO
-- refuses a seat without one, M4), a deployed roster member, fielded map
-- units, or a FILL squad. The mask is built in two steps:
--
--   1. The map's own value    the caller's auto_default over the authored
--                             domain (the manifest row.teams for
--                             soccer/basketball/onslaught; 0 — "every
--                             authored team" — for CTF/TDM, the verified
--                             per-mode Auto asymmetry). For a team the
--                             map ships EMPTY this decides whether a
--                             FILL squad backfills it; a base team the
--                             boxes and the wheel leave with NOTHING
--                             standing is dropped one step later, by the
--                             fills rows (the lineup_mask narrowing
--                             every decide fold adopts).
--   2. plus the occupied      every AUTHORED team with a deployed roster
--                             is on, and so is every authored team with
--                             FIELDED MAP UNITS — authored npcs or
--                             generators whose box is on (B4: checked
--                             units play, whatever the manifest count
--                             says — the successor of the 2026-08-18
--                             "as many teams as the map actually has"
--                             directive). A roster outside the authored
--                             domain (no anchors, no flag — nowhere to
--                             spawn or score) still fights under classic
--                             rules but never activates, the
--                             pre-amendment truth.
--
-- A solo roster on a map authoring nobody else comes back alone and reads
-- starts = false; a map narrowed below two teams by the fills rows
-- likewise (the mode's own refusal sentence, recounted by the fold).
--
-- Returns (active_mask, starts, matched, matched_size): matched reports a
-- deployed roster anywhere — the predicted nonzero census, so the FILL
-- solver has a reference to solve against (no roster degrades every
-- squad to the legacy formula); matched_size is the D34 headcount rule —
-- one roster team = its deployed count, several = the MIN across them,
-- authored or not (mirroring the census, which prices every has_guy
-- walker on a score team).
local function activation(inputs, authored_mask, auto_default)
  local matched = false
  local matched_size = 0
  for t = 1, C.SCORE_TEAM_COUNT do
    local n = inputs.teams[t].roster
    if n > 0 then
      matched = true
      if matched_size == 0 then
        matched_size = n
      elseif n < matched_size then
        matched_size = n
      end
    end
  end
  local unit_boxes = inputs.map_units or {}
  local base = core.activate_teams(authored_mask, auto_default)
  local mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local on = core.mask_has(base, team)
    if core.mask_has(authored_mask, team) then
      local trow = inputs.teams[team + 1]
      if trow.roster > 0 then
        on = true
      elseif (unit_boxes[team + 1] or MAP_UNITS_ON) == MAP_UNITS_ON then
        if trow.npcs > 0 then
          on = true
        elseif trow.generators > 0 then
          on = true
        end
      end
    end
    if on then
      mask = core.mask_add(mask, team)
    end
  end
  return mask, core.mask_count(mask) >= 2, matched, matched_size
end

-- Per-team fill rows: what fields each team once the apply has consumed
-- markers, stripped and backfilled — precomputed from the same counts and
-- power sums the strips and spawn seams reduce to (a box-off team's
-- authored units do not stand, lineup.strip_authored_troops), so the
-- apply spawns exactly where a row says a squad walks on. opts:
--   no_bots           (onslaught D17) never field a squad
--   matched, matched_size   activation's answers (the solver seam)
--   squad_cap         the mode's hard shape (basketball's 5v5, review
--                     R2): no squad row counts past it
-- Returns (teams, wants_bots, mask): teams is the decision's [1..4] row
-- array ({active, fill, count, squad, squad_count}); wants_bots reports
-- any squad row (squad classes are drawn at spawn — mode_anchors
-- squad_code); mask is active_mask minus the teams the knobs left with
-- nothing standing (B4: "a team is active when anything is on it"), so
-- every consumer of the decision — TEAM_MASK, strips, win ladders, the
-- starts recheck — sees the narrowed truth. A row's count is what the
-- team will FIELD: a company row with an allies squad beside it counts
-- roster + squad (squad_count is the squad alone), sized by squad_room,
-- and a troops row with a squad beside it counts npcs + squad the same
-- way; row.squad carries the banked-fact code — since D1 the applied
-- fill code itself — of the squad the apply will spawn, nil where none
-- will.
--
-- The FILL squad rows (amendment B2/B3), decided from the inputs alone:
--   empty team, humans anywhere   "matched" — solved against
--                                 reference × multiplier, sized by the
--                                 headcount rule
--   empty team, no humans         "bots" — the legacy difficulty squad
--   occupied team (allies)        squad beside the company iff
--                                 (strongest other f-sum − own f-sum)
--                                 × multiplier > 0, sized by the
--                                 headcount rule in the room the hard
--                                 shape leaves
--   troops standing, DEFAULT      no squad — the map's own units are the
--                                 team's fill (turn the box off to trade
--                                 them for a solved squad)
--   troops standing, explicit     a solved squad walks on BESIDE the
--   non-NONE                      npcs (D3's classic gate, mirrored):
--                                 troops carry no guy, so the target is
--                                 the empty-team arm's, and a hard shape
--                                 prices only the room beside the units
--   NONE                          no squad, whatever else stands
local function fills(inputs, active_mask, opts)
  -- Every shipped squad table fields five bots (D35 soldier-first); a
  -- solved squad truncates to the headcount prefix (D39).
  local squad_size = 5
  local no_bots = false
  local matched = false
  local matched_size = 0
  local squad_cap = nil
  if opts ~= nil then
    no_bots = opts.no_bots == true
    matched = opts.matched == true
    matched_size = opts.matched_size or 0
    squad_cap = opts.squad_cap
  end
  local fill_knobs = inputs.fill or {}
  local unit_boxes = inputs.map_units or {}
  -- The weakest human team's f-sum (B3's reference) and the strongest,
  -- from the census powers — the fills twin of census_power's fold, over
  -- the same numbers.
  local reference = 0
  for t = 1, C.SCORE_TEAM_COUNT do
    local p = inputs.teams[t].power or 0
    if p > 0 then
      if reference == 0 then
        reference = p
      elseif p < reference then
        reference = p
      end
    end
  end
  local teams = {}
  local wants_bots = false
  local mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local row = inputs.teams[team + 1]
    local active = core.mask_has(active_mask, team)
    local fill = "empty"
    local count = 0
    -- The RAW stored code survives beside the resolution: the troops arm
    -- below is the one place a mode row distinguishes "the player turned
    -- the wheel" from "the default resolved" (D3's classic gate).
    local raw = fill_knobs[team + 1] or FILL_DEFAULT
    local knob = raw
    if active then
      -- C8/D1: a stored default resolves HERE, through the lib's ONE
      -- rule, before any squad decision reads it. A team in the active
      -- mask is authored by the mode's own domain — the mask already
      -- folded flags, anchors and foundries — so the activation IS the
      -- presence row and the default resolves the explicit FAIR, the
      -- pre-D1 behaviour; an explicit code is itself. Inactive teams
      -- never reach a squad decision, so their default never resolves.
      knob = lineup.resolved_fill(knob, { units = 1, roster = row.roster })
    end
    local units_on = (unit_boxes[team + 1] or MAP_UNITS_ON) == MAP_UNITS_ON
    local squad = nil
    local squad_count = 0
    if active then
      local troops_stand = row.npcs > 0
      if not units_on then
        troops_stand = false
      end
      local gens_stand = row.generators > 0
      if not units_on then
        gens_stand = false
      end
      -- The squad size a headcount-ruled solve fields (B2): the mode's
      -- stock table truncated to the min human roster, then to the room
      -- the hard shape leaves beside this team's occupants (R2).
      local solved_size = squad_size
      if matched_size > 0 then
        solved_size = og.min(matched_size, squad_size)
      end
      local room = lineup.squad_room(squad_cap, row.roster)
      if room ~= nil then
        solved_size = og.min(solved_size, room)
      end
      if row.roster > 0 then
        -- Allies (B3): a company's squad targets the gap to the
        -- strongest other team, scaled by the wheel; a gap at or below
        -- zero fields nobody. The gap is decided from the census powers
        -- so the row's count IS the fielded count.
        if not no_bots and not lineup.squad_off(knob) then
          local best = 0
          for t = 1, C.SCORE_TEAM_COUNT do
            if t ~= team + 1 then
              local p = inputs.teams[t].power or 0
              if p > best then
                best = p
              end
            end
          end
          local target = og.div((best - (row.power or 0))
                                  * lineup.fill_percent(knob), 100)
          if target > 0 then
            squad_count = solved_size
          end
          if squad_count > 0 then
            -- D1: the banked fact IS the applied wheel code (the +1
            -- bias retired with the default's own storage code).
            squad = lineup.applied_fill(knob)
            wants_bots = true
          end
        end
        fill = "company"
        count = row.roster + squad_count
      elseif troops_stand then
        -- Fielded map units are the team's fill (B4) and the stored
        -- DEFAULT stays squadless beside them — but an EXPLICIT non-NONE
        -- wheel value fields a solved squad beside the npcs (D3's classic
        -- gate, mirrored here for the mode maps). Troops carry no guy, so
        -- the seam's solve prices the empty-team arm (weakest human x m),
        -- and the hard shape leaves only the room beside the standing
        -- units (R2 — the spawn seam's fielded count is the same walk).
        fill = "troops"
        count = row.npcs
        local explicit = raw ~= FILL_DEFAULT
        if no_bots then
          explicit = false
        end
        if explicit and not lineup.squad_off(knob) then
          local troop_size = solved_size
          local troop_room = lineup.squad_room(squad_cap, row.npcs)
          if troop_room ~= nil then
            troop_size = og.min(troop_size, troop_room)
          end
          if troop_size > 0 then
            squad_count = troop_size
            -- D1: the banked fact IS the applied wheel code.
            squad = lineup.applied_fill(knob)
            wants_bots = true
            count = row.npcs + squad_count
          end
        end
      elseif no_bots then
        if gens_stand then
          fill = "generators"
          count = row.generators
        else
          active = false
        end
      elseif lineup.squad_off(knob) then
        -- NONE (B8): the squad today's rule would field is suppressed.
        -- Standing generators still put the team on; one left with
        -- nothing at all drops out of the mask below.
        if gens_stand then
          fill = "generators"
          count = row.generators
        else
          active = false
        end
      else
        -- The FILL squad on an empty team (B2/B3): solved against
        -- reference × multiplier where any human roster stands, the
        -- legacy difficulty squad where none does — spawn_bots' twin
        -- arms over the same census.
        if reference > 0 then
          fill = "matched"
          count = solved_size
        else
          fill = "bots"
          count = squad_size
          if squad_cap ~= nil then
            count = og.min(count, squad_cap)
          end
        end
        -- count is never zero here: the legacy squad is five, a solved
        -- size is at least min(1, headcount) and every shipped hard shape
        -- leaves an empty team its whole cap. The banked fact IS the
        -- applied wheel code (D1: the +1 bias is retired).
        squad = lineup.applied_fill(knob)
        wants_bots = true
        squad_count = count
      end
    end
    if active then
      mask = core.mask_add(mask, team)
    end
    teams[team + 1] = {
      active = active,
      fill = fill,
      count = count,
      squad = squad,
      squad_count = squad_count,
    }
  end
  return teams, wants_bots, mask
end

-- Does a decision row call for a squad spawn? The one predicate every
-- mode's apply loop asks (amendment B2/B3): the empty-team fills, or an
-- allies squad riding a company row (row.squad).
local function wants_squad(row)
  if row.squad ~= nil then
    return true
  end
  if row.fill == "bots" then
    return true
  end
  return row.fill == "matched"
end

-- Consume the active teams' start markers (anchor positions are already
-- banked engine-side) so the marker entities cannot block their own
-- respawn anchors.
local function consume_markers(obs, mask)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_SPECIAL then
        if w:family() == C.FAMILY_RESERVED_TEAM then
          local team = w:team_num()
          if team < C.SCORE_TEAM_COUNT then
            if core.mask_has(mask, team) then
              w:set_dead(1)
            end
          end
        end
      end
    end
  end
end

-- Strip score-range entities of teams left out of the active mask.
-- Deliberately narrower than the CTF strip: teams at or beyond
-- SCORE_TEAM_COUNT (wildlife, neutral generators) are arena identity and
-- stay. Roster walkers are never stripped.
local function strip_inactive_teams(obs, mask)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      local order = w:order()
      local strippable = false
      if order == C.ORDER_LIVING then
        strippable = true
      elseif order == C.ORDER_GENERATOR then
        strippable = true
      elseif order == C.ORDER_SPECIAL then
        strippable = w:family() == C.FAMILY_RESERVED_TEAM
      end
      if strippable then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          if not core.mask_has(mask, team) then
            if not w:has_guy() then
              w:set_dead(1)
            end
          end
        end
      end
    end
  end
end

-- The one-shot in-game signal (D23/§7): fired only while on_mode_init is
-- still running (MODE_ID is written LAST by every impl, so it reads 0
-- exactly during init), latched in a mode var, LIMIT when the first solve
-- clamped at either end. Mid-match D24 backstop solves stay silent.
-- Announces are match-mode vocabulary (the classic stage passes no
-- callback and stays silent), so the latch lives here, beside the words.
local function announce_matched(clamped)
  if og.mode_get(core.SLOT.MODE_ID) ~= 0 then
    return
  end
  -- The latch is the slot's ONES digit alone — the digits above it are
  -- the co-tenant lineup facts (lineup.bank_lineup_facts), so the read
  -- and both writes stay inside that digit.
  if og.mod(og.mode_get(MATCHED.ANNOUNCED), 10) ~= 0 then
    return
  end
  if clamped then
    core.announce("TEAMS MATCHED (LIMIT)", C.SOUND_CHARGE)
    og.mode_set(MATCHED.ANNOUNCED, og.mode_get(MATCHED.ANNOUNCED) + 2)
    return
  end
  core.announce("TEAMS MATCHED", C.SOUND_CHARGE)
  og.mode_set(MATCHED.ANNOUNCED, og.mode_get(MATCHED.ANNOUNCED) + 1)
end

-- The mode spelling of the one squad seam: the core spawner with the
-- TEAMS MATCHED announce threaded in (every mode caller keeps the old
-- five-argument signature; the announce rule stays mode-side).
local function spawn_bots(team, families, cursor_slot, placer, cap)
  lineup.spawn_bots(team, families, cursor_slot, placer, cap,
                    announce_matched)
end

-- "Owns its life": may this corpse come back, and do its deaths count?
-- A roster walker always does. An AI does only when nothing else made it:
-- a live owner disqualifies it, and so does the durable origin mark, because
-- clear_stale_cross_refs nulls owner() the tick the OWNER dies. Without the
-- mark a dead tent's orphaned spawns are indistinguishable from the
-- init-time bots, which is exactly how a score-team generator map turned
-- into an endless frag fountain at the anchors.
local function owns_its_life(w)
  if w:has_guy() then
    return true
  end
  if w:owner() ~= nil then
    return false
  end
  return not caps.is_marked_spawn(w)
end

-- Durable origin marking, the other half of match.owns_its_life. Generator
-- spawns are marked at birth by the shared customize_spawn; everything else
-- that is owned (summons, raised undead) is marked here, on every tick it is
-- still owned, so the mark is already in place when its owner dies and
-- clear_stale_cross_refs nulls the link. Idempotent bit write, zero RNG.
local function mark_owned_lives(obs)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:owner() ~= nil then
          caps.mark_spawn(w)
        end
      end
    end
  end
end

-- The pre-sweep corpse scan: schedules every dead score-team Living that
-- owns its life and is not already queued. Runs every tick so schedule
-- refusals and silent set_dead corpses retry, exactly like the CTF port's
-- scan.
local function schedule_dead(obs, mask, delay)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() ~= 0 then
      if w:order() == C.ORDER_LIVING then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          if core.mask_has(mask, team) then
            if owns_its_life(w) then
              if not og.respawn_pending(w) then
                og.respawn_schedule(w, delay)
              end
            end
          end
        end
      end
    end
  end
end

-- The neutral-reset backstop (soccer's design §6.3 kickoff rule, shared):
-- an active team with zero live Livings comes back at every re-spot,
-- whatever the difficulty submenu says — respawn
-- Off cannot strand a team out of a ball game. Roster (guy) corpses
-- persist in the oblist and are scheduled for revival (on_respawn places
-- them at the team anchors); summoned corpses (owner, no guy) stay down.
-- The engine sweeps unscheduled AI corpses at the end of their death tick,
-- so a wiped bot side whose bodies are already gone is reprovisioned with
-- a fresh squad — the same spawn path init uses for empty active teams —
-- unless revives are already in flight. `anchors` is the caller's
-- mode_anchors module: og.use is load-time only and rejects cycles, and
-- mode_anchors already uses THIS module, so the score-team read arrives
-- as an argument rather than an import.
local function revive_wiped_teams(anchors, mask, ticks, cursor_slot, cap)
  local obs = og.oblist()
  local live = { 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          live[team + 1] = live[team + 1] + 1
        end
      end
    end
  end
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() ~= 0 then
      if w:order() == C.ORDER_LIVING then
        local team = anchors.score_team(w)
        if team < C.SCORE_TEAM_COUNT then
          if core.mask_has(mask, team) then
            if live[team + 1] == 0 then
              local eligible = true
              if not w:has_guy() then
                eligible = w:owner() == nil
              end
              if eligible then
                if not og.respawn_pending(w) then
                  og.respawn_schedule(w, ticks)
                end
              end
            end
          end
        end
      end
    end
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if live[team + 1] == 0 then
        if og.respawn_pending_count(team) == 0 then
          anchors.spawn_bot_squad(team, cursor_slot, cap)
        end
      end
    end
  end
end

-- A foe worth having: a live Living on an active enemy team. The engine's
-- pre-act backstop (find_far_foe) fills empty foes before any post-act
-- director runs, and it happily hands out wildlife — so director repair
-- arms treat nil, dead AND non-scoring foes as broken.
local function foe_scores(foe, mask, team)
  if foe == nil then
    return false
  end
  if foe:dead() ~= 0 then
    return false
  end
  local foe_team = foe:team_num()
  if foe_team == team then
    return false
  end
  if foe_team >= C.SCORE_TEAM_COUNT then
    return false
  end
  return core.mask_has(mask, foe_team)
end

-- Nearest live enemy Living on an ACTIVE team, by Manhattan distance with
-- ties to the earlier oblist slot. Wildlife (bytes at or beyond the score
-- range) is never a target — its kills score nothing. wanted_team >= 0
-- narrows the hunt to that team (the TDM endgame focus).
local function nearest_enemy(livings, mask, team, x, y, wanted_team)
  local best = nil
  local best_distance = 0
  for k = 1, #livings do
    local w = livings[k]
    local candidate_team = w:team_num()
    local wanted = candidate_team ~= team
    if wanted then
      wanted = candidate_team < C.SCORE_TEAM_COUNT
    end
    if wanted then
      wanted = core.mask_has(mask, candidate_team)
    end
    if wanted then
      if wanted_team >= 0 then
        wanted = candidate_team == wanted_team
      end
    end
    if wanted then
      local distance = ai.dist_to(w, x, y)
      if best == nil then
        best = w
        best_distance = distance
      elseif distance < best_distance then
        best = w
        best_distance = distance
      end
    end
  end
  return best
end

-- Timeout leader, the shared three-rung ladder (modes.md §8.2, identical
-- everywhere): mode metric, then m_score, then the LOWEST team byte
-- (ascending scan with strictly-greater replacement). Soccer, Onslaught
-- and CTF spell the same ladder inline over their own metrics.
local function timeout_leader(mask, score_of)
  local winner = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if winner < 0 then
        winner = team
      elseif score_of(team) > score_of(winner) then
        winner = team
      elseif score_of(team) == score_of(winner) then
        if og.team_score(team) > og.team_score(winner) then
          winner = team
        end
      end
    end
  end
  return winner
end

return {
  rows_for = rows_for,
  resolve_limit = resolve_limit,
  resolve_time_limit = resolve_time_limit,
  MATCHED = MATCHED,
  FILL_DEFAULT = FILL_DEFAULT,
  FILL_FAIR = lineup.FILL_FAIR,
  FILL_NONE = lineup.FILL_NONE,
  FILL_PERCENT = lineup.FILL_PERCENT,
  squad_off = lineup.squad_off,
  fill_percent = lineup.fill_percent,
  applied_fill = lineup.applied_fill,
  resolved_fill = lineup.resolved_fill,
  map_units_fielded = lineup.map_units_fielded,
  difficulty_level = lineup.difficulty_level,
  bank_refusal_fighters = bank_refusal_fighters,
  squad_room = lineup.squad_room,
  fill_target = lineup.fill_target,
  stat_power = lineup.stat_power,
  measured_base = lineup.measured_base,
  walker_power = lineup.walker_power,
  predicted_power = lineup.predicted_power,
  census_power = lineup.census_power,
  solve_matched_levels = lineup.solve_matched_levels,
  bot_level_for = lineup.bot_level_for,
  census_inputs = census_inputs,
  activation = activation,
  fills = fills,
  wants_squad = wants_squad,
  bank_match_target = lineup.bank_match_target,
  consume_markers = consume_markers,
  strip_inactive_teams = strip_inactive_teams,
  place_at_anchor = lineup.place_at_anchor,
  spawn_bots = spawn_bots,
  owns_its_life = owns_its_life,
  mark_owned_lives = mark_owned_lives,
  schedule_dead = schedule_dead,
  revive_wiped_teams = revive_wiped_teams,
  foe_scores = foe_scores,
  nearest_enemy = nearest_enemy,
  timeout_leader = timeout_leader,
}
