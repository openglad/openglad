-- Shared match toolkit — manifest row adapter, team census, the TROOPS:OWN roster activation all six modes apply, marker consumption, anchor-cursor placement, bot fielding with the TROOPS: FAIR power model, dead-competitor scheduling, timeout ladder (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local caps = og.use("mode_caps")
local strip = og.use("mode_strip")

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

-- ---------------------------------------------------------------------------
-- TROOPS: FAIR power model (docs/matched-teams-design.md §4-§5)
-- ---------------------------------------------------------------------------

-- Mode-var slots. The design's per-team MATCHED_LEVEL/MATCHED_UP footprint
-- (D20) does not fit repo reality: CTF and Onslaught use every mode-private
-- slot 8..63 and Basketball leaves only 63, so the solved plan is PACKED
-- into the shared header band (slots 0-7, mode-neutral by convention:
-- MATCHED owns 2-5, mode_anchors' squad seed owns 6 (#235), 7 is free)
-- instead. TARGET is the capped mean human-team f (0 =
-- census not run, or no human power — both mean "legacy"); PLAN carries one
-- base-100 code per team, code = L * 10 + k (0 = unsolved, L in 1..9,
-- k in 0..4, max packed value 94 * 1010101 = 94,949,494 < 2^31); ANNOUNCED
-- latches the one-shot init announcement (0 none, 1 normal, 2 limit);
-- SIZE latches the census headcount H (D34: one human team = its live
-- has_guy headcount, several = the MIN of the per-team counts; 0 = census
-- not run, or no human power) — the spawn seam truncates every generated
-- squad to its table's first min(SIZE, #families) members (D39).
local MATCHED = {
  TARGET = 2,
  PLAN = 3,
  ANNOUNCED = 4,
  SIZE = 5,
}

-- D20: any census total past this cap is far beyond B(9) and solves to the
-- uniform-L9 clamp regardless, so capping the STORED value loses nothing
-- while keeping the int32 mode var safe from maxed infinite-gold rosters.
local TARGET_CAP = 1073741824

local PLAN_BASE = { 1, 100, 10000, 1000000 }

-- Difficulty tuples — a COPY of pack data (D13), guarded by the model-pin
-- test in test_modes_tdm.cpp, which iterates EVERY row here. The table
-- carries a row for every core family that declares an
-- og.apply_difficulty_scaling hook, so any core-family roster a mode may
-- ever name is priced correctly; the default row is genuinely correct for
-- every hookless family (the engine falls back to 11/11/4/2 —
-- living.cpp, living::set_difficulty default formula). A NON-core pack
-- family with its own hook would need a row here before a matched roster
-- may name it — the fallback would silently mis-model it. Sources:
--   core:soldier  packs/core/families/living-00-soldier.lua:133
--   core:archer   packs/core/families/living-02-archer.lua:82
--   core:mage     packs/core/families/living-03-mage.lua:64
--   core:orc      packs/core/families/living-14-orc.lua:88
--   core:#18      packs/core/families/living-18-beast.lua:8 (BEAST)
--   core:cleric   packs/core/families/living-05-cleric.lua:290
--   core:druid    packs/core/families/living-13-druid.lua:145
--   core:elf / core:thief declare no hook (living-01-elf.lua,
--   living-11-thief.lua) and take the engine default.
-- hp/mp/armor scale L*L, damage scales L (guy.cpp apply_difficulty_scaling).
local TUPLE_DEFAULT = { hp = 11, mp = 11, dmg = 4, armor = 2 }
local TUPLE = {
  ["core:soldier"] = { hp = 13, mp = 8, dmg = 5, armor = 2 },
  ["core:archer"] = { hp = 11, mp = 12, dmg = 4, armor = 1 },
  ["core:mage"] = { hp = 7, mp = 14, dmg = 3, armor = 0.5 },
  ["core:elf"] = TUPLE_DEFAULT,
  ["core:thief"] = TUPLE_DEFAULT,
  ["core:orc"] = { hp = 14, mp = 7, dmg = 6, armor = 3 },
  ["core:#18"] = { hp = 18, mp = 5, dmg = 7, armor = 4 },
  ["core:cleric"] = { hp = 9, mp = 12, dmg = 4, armor = 0.5 },
  ["core:druid"] = { hp = 9, mp = 12, dmg = 4, armor = 0.5 },
}

-- The f core over already-truncated integer stats (§4.1): offense
-- throughput times survivable pool, +60 flooring zero-offense walkers.
local function stat_power(hp, mp, armor, dmg, sp, ff, level)
  local ed = og.div(dmg * (level + 3), 4)
  local rate = og.div(120, ff)
  local off = ed * rate + 5 * sp
  local ehp = hp + 4 * armor + og.div(mp, 2)
  return og.div(ehp * (off + 60), 60)
end

-- The §4.1 truncation discipline: every stat getter pushes a FLOAT (the
-- guy bonuses are float divisions; og.div/og.mod would raise "number has
-- no integer representation"), so every read truncates at the boundary.
-- The fire-frequency floor keeps RATE finite on degenerate stats.
local function measured_base(w)
  return {
    hp = og.trunc(w:s_max_hitpoints()),
    mp = og.trunc(w:s_max_magicpoints()),
    armor = og.trunc(w:s_armor()),
    dmg = og.trunc(w:damage()),
    sp = og.trunc(w:stepsize()),
    ff = og.max(1, og.trunc(w:fire_frequency())),
  }
end

-- f(walker): the power metric (§4.1), integer arithmetic over truncated
-- reads. s_level is the one integer-typed getter.
local function walker_power(w)
  local base = measured_base(w)
  return stat_power(base.hp, base.mp, base.armor, base.dmg, base.sp,
                    base.ff, w:s_level())
end

-- pred_i(L): the family difficulty tuple applied to a MEASURED base inside
-- f (§4.3). hp/mp scale L*L, damage L, armor L*L — og.trunc floors the
-- mage's fractional 0.5 * L * L armor term (the walker keeps the fraction;
-- the model floors it, absorbed by the model-pin band). stepsize and
-- fire_frequency are untouched by difficulty scaling.
local function predicted_power(family, base, level)
  local row = TUPLE[family]
  if row == nil then
    row = TUPLE_DEFAULT
  end
  local hp = base.hp + row.hp * level * level
  local mp = base.mp + row.mp * level * level
  local dmg = base.dmg + row.dmg * level
  local armor = base.armor + og.trunc(row.armor * level * level)
  return stat_power(hp, mp, armor, dmg, base.sp, base.ff, level)
end

-- The human census (D11/D15): mean of the human team f-sums, where a human
-- team fields at least one live has_guy Living (the own_roster_activation
-- predicate — every g_/stat read stays behind the has_guy guard). Returns
-- (T, per-team sums, H); T = 0 means no human power server-side. H is the
-- headcount the same walk counts (D34): one human team = its live has_guy
-- count, several = the MIN across them — the guarantee is per-human-team,
-- and only min keeps every survivor un-outnumbered on the backstop path.
local function census_power(obs)
  local sums = { 0, 0, 0, 0 }
  local counts = { 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          local team = w:team_num()
          if team < C.SCORE_TEAM_COUNT then
            sums[team + 1] = sums[team + 1] + walker_power(w)
            counts[team + 1] = counts[team + 1] + 1
          end
        end
      end
    end
  end
  local total = 0
  local teams = 0
  local size = 0
  for t = 1, C.SCORE_TEAM_COUNT do
    if sums[t] > 0 then
      total = total + sums[t]
      teams = teams + 1
      if size == 0 then
        size = counts[t]
      elseif counts[t] < size then
        size = counts[t]
      end
    end
  end
  if teams == 0 then
    return 0, sums, 0
  end
  return og.div(total, teams), sums, size
end

-- The D22 solver: single argmin of |P(L, k) - T| over the FULL reachable
-- set {L in 1..9, k in 0..n-1, L = 9 forces k = 0}, ties to lower L then
-- lower k (ascending scan with strictly-smaller replacement). P(L, k) =
-- B(L) plus the first k members upgraded one level. Returns
-- (level, upgrades, clamped) where clamped reports T outside [B(1), B(9)]
-- (the §7 LIMIT announce condition).
local function solve_matched_levels(target, bases)
  local n = #bases
  local pred = {}
  for i = 1, n do
    local row = {}
    for level = 1, 9 do
      row[level] = predicted_power(bases[i].family, bases[i].stats, level)
    end
    pred[i] = row
  end
  local b1 = 0
  local b9 = 0
  for i = 1, n do
    b1 = b1 + pred[i][1]
    b9 = b9 + pred[i][9]
  end
  local best_level = 1
  local best_up = 0
  local best_miss = -1
  for level = 1, 9 do
    local p = 0
    for i = 1, n do
      p = p + pred[i][level]
    end
    local up_max = n - 1
    if level == 9 then
      up_max = 0
    end
    for up = 0, up_max do
      if up > 0 then
        p = p + pred[up][level + 1] - pred[up][level]
      end
      local miss = core.iabs(p - target)
      local better = best_miss < 0
      if not better then
        better = miss < best_miss
      end
      if better then
        best_level = level
        best_up = up
        best_miss = miss
      end
    end
  end
  local clamped = target < b1
  if target > b9 then
    clamped = true
  end
  return best_level, best_up, clamped
end

local function plan_code(team)
  return og.mod(og.div(og.mode_get(MATCHED.PLAN), PLAN_BASE[team + 1]), 100)
end

local function store_plan(team, level, up)
  local plan = og.mode_get(MATCHED.PLAN)
  local code = level * 10 + up
  plan = plan + (code - plan_code(team)) * PLAN_BASE[team + 1]
  og.mode_set(MATCHED.PLAN, plan)
end

-- Census-at-init (D15): runs from own_roster_activation — the one shared
-- call every mode makes in on_mode_init — only when the lobby requested
-- TROOPS: FAIR (the scenario-troops sentinel, D25/D29), and latches through
-- MATCHED.TARGET so mid-match spawns never re-census the live battle (D24).
-- The has_guy census is immune to the troops strip (authored troops carry
-- no guy record) and the roster is already in the oblist
-- (spawn_team_from_save precedes the first tick).
local function record_match_target(obs)
  local _, matched = core.team_count_request()
  if not matched then
    return
  end
  if og.mode_get(MATCHED.TARGET) ~= 0 then
    return
  end
  local target, _, size = census_power(obs)
  og.mode_set(MATCHED.TARGET, og.min(target, TARGET_CAP))
  og.mode_set(MATCHED.SIZE, size)
end

-- Per-member spawn level (§5.4): a stored plan answers L (or L + 1 for the
-- first k members); an unsolved team takes the legacy session-difficulty
-- formula (50/100/200 percent -> L1/L2/L3), byte-identical to the
-- pre-matched spawner.
local function bot_level_for(team, index)
  local code = plan_code(team)
  if code == 0 then
    return og.max(1, og.div(og.match_setting("difficulty"), 100) + 1)
  end
  local level = og.div(code, 10)
  if index <= og.mod(code, 10) then
    return level + 1
  end
  return level
end

-- Authored score-team census: a team is authored when it fields a live
-- Living or any respawn anchors (the engine anchor scan ran before
-- on_mode_init and includes dead markers).
local function census_mask(obs)
  local mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if og.respawn_anchor_count(team) > 0 then
      mask = core.mask_add(mask, team)
    end
  end
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          mask = core.mask_add(mask, team)
        end
      end
    end
  end
  return mask
end

-- TROOPS:OWN activation, the shared ruling all six modes apply: under OWN
-- (and FAIR, which bundles it) the deployed rosters seed the match.
-- Returns the replacement active mask, or nil when the standard requested
-- activation stands (TROOPS:ALL, or an all-bot match with no rosters
-- anywhere). Precedence, in evaluation order:
--
--   zero roster teams         nil — a bot match keeps today's shape (the
--                             lobby count / manifest default clamps via
--                             activate_teams at the call site).
--   explicit lobby count N    roster teams PLUS authored non-roster teams
--                             in index order until the mask holds
--                             clamp(N, 2, 4) teams; a roster already at
--                             or past the count stays exactly the roster
--                             (max semantics — a deployed side is never
--                             stripped to satisfy a count). Issue #218.
--   Auto, >= 2 roster teams   exactly those teams — init manufactures no
--                             extra sides. The authored domain still
--                             clamps (Soccer two on a two-goal pitch, four
--                             on FOURSQUARE): a roster seated outside it
--                             cannot score there and does not count, so a
--                             three-roster group on a two-team pitch
--                             activates the first two in index order.
--   Auto, one roster team     that team plus the FIRST authored non-roster
--                             team in index order — a solo group needs an
--                             opponent, and the mode's own empty-team
--                             census behind the strip backfills exactly
--                             that one side.
local function own_roster_activation(authored_mask, obs)
  -- The TROOPS: FAIR census (D15) rides this call — the one walk of the
  -- oblist every mode's on_mode_init shares — so activation and matching
  -- can never disagree about which teams are human. It self-gates on the
  -- matched request and runs before the TROOPS:ALL early return — which
  -- never fires under FAIR, because the sentinel (3) is itself above KEEP:
  -- matching implies strip-on (D26/D29).
  record_match_target(obs)
  if og.match_setting("strip_troops") <= strip.KEEP then
    return nil
  end
  local roster = 0
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          local team = w:team_num()
          if team < C.SCORE_TEAM_COUNT then
            if core.mask_has(authored_mask, team) then
              roster = core.mask_add(roster, team)
            end
          end
        end
      end
    end
  end
  local count = core.mask_count(roster)
  if count == 0 then
    return nil
  end
  -- The lobby TEAMS count wins (issue #218): an EXPLICIT numeric request
  -- fields that many sides — TROOPS: OWN/FAIR decide squad COMPOSITION,
  -- never the number of teams. Roster teams all stay (a deployed human
  -- side is never stripped to satisfy a count); authored non-roster teams
  -- backfill in index order, and each mode's empty-team census behind the
  -- strip fields their squads. TEAMS: Auto keeps the roster shape below
  -- (D26 adjudicated Auto only; the count override is the 2026-08-17
  -- maintainer ruling recorded in matched-teams-design.md).
  local requested = core.team_count_request()
  if requested > 0 then
    local target = og.clamp(requested, 2, C.SCORE_TEAM_COUNT)
    local mask = roster
    for team = 0, C.SCORE_TEAM_COUNT - 1 do
      if core.mask_count(mask) < target then
        if core.mask_has(authored_mask, team) then
          mask = core.mask_add(mask, team)
        end
      end
    end
    return mask
  end
  if count >= 2 then
    return roster
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(authored_mask, team) then
      if not core.mask_has(roster, team) then
        return core.mask_add(roster, team)
      end
    end
  end
  -- A solo roster on a map authoring nobody else: hand back the lone team
  -- and let the mode's fewer-than-two check report the shape.
  return roster
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

-- The L1 ring cell at index i (0 .. 4*r-1) of tile radius r, clockwise
-- from due north — the deterministic ring-walk idiom shared with the
-- ball games' landing-legality re-spot.
local function ring_offset(i, r)
  local edge = og.div(i, r)
  local k = og.mod(i, r)
  if edge == 0 then
    return k, k - r
  elseif edge == 1 then
    return r - k, k
  elseif edge == 2 then
    return -k, r - k
  end
  return k - r, -k
end

-- Anchor rotation placement (DECISIONS D1): the mode-var cursor +
-- probe-eats-safe checks. Deterministic and zero-RNG on the respawn path;
-- init-time bot placement passes allow_teleport (the one blessed RNG
-- fallback, same as the CTF port). When every anchor is blocked — a
-- camped spawn line must never stall respawns — a bounded ring walk
-- around each anchor in index order takes the first clear tile instead.
local function place_at_anchor(w, team, cursor_slot, allow_teleport)
  local final_x = -1
  local final_y = 0
  if team >= 0 then
    if team < C.SCORE_TEAM_COUNT then
      local count = og.respawn_anchor_count(team)
      for _ = 1, count do
        local cursor = og.mode_get(cursor_slot)
        og.mode_set(cursor_slot, cursor + 1)
        local ax, ay = og.respawn_anchor(team, og.mod(cursor, count))
        if og.spawn_spot_clear(w, ax, ay) then
          final_x = ax
          final_y = ay
          break
        end
      end
      if final_x < 0 then
        -- Ring fallback, RNG-free and probe-eats-safe like the rotation
        -- above. Rings cap at 3 tiles: past that the caller's own
        -- fallback (engine revive-in-place, or the blocked-fire retry
        -- cadence) is the honest answer.
        for r = 1, 3 do
          for a = 0, count - 1 do
            local ax, ay = og.respawn_anchor(team, a)
            for i = 0, 4 * r - 1 do
              if final_x < 0 then
                local dx, dy = ring_offset(i, r)
                local px = ax + dx * 16
                local py = ay + dy * 16
                if px >= 0 and py >= 0 then
                  if og.spawn_spot_clear(w, px, py) then
                    final_x = px
                    final_y = py
                  end
                end
              end
            end
          end
        end
      end
    end
  end
  if final_x >= 0 then
    w:setxy(final_x, final_y)
    return true
  end
  if allow_teleport then
    return w:teleport()
  end
  return false
end

-- The headcount rule (D34/D39): a generated squad never outnumbers the
-- roster it was measured against. A latched SIZE truncates the mode's
-- squad table to its first min(SIZE, #families) members — soldier-first
-- for the standard tables (D35); SIZE = 0 (no census, or no human power)
-- keeps the full table, so the legacy arm and the direct spawn-probe arms
-- stay byte-identical.
local function matched_families(families)
  local size = og.mode_get(MATCHED.SIZE)
  if size <= 0 then
    return families
  end
  if size >= #families then
    return families
  end
  local prefix = {}
  for k = 1, size do
    prefix[k] = families[k]
  end
  return prefix
end

local function add_squad_member(team, family_name)
  local fam = og.family_id("living", family_name) --[[@as integer]] -- caller tables name core families; a nil would be a load bug and add_ob erroring loudly is the right failure
  local w = og.add_ob("living", fam)
  if w == nil then
    return nil
  end
  w:set_team_num(team)
  w:set_real_team_num(255)
  return w
end

local function place_member(w, team, cursor_slot, placer)
  if placer ~= nil then
    placer(w, team, true)
    return
  end
  place_at_anchor(w, team, cursor_slot, true)
end

-- The one-shot in-game signal (D23/§7): fired only while on_mode_init is
-- still running (MODE_ID is written LAST by every impl, so it reads 0
-- exactly during init), latched in a mode var, LIMIT when the first solve
-- clamped at either end. Mid-match D24 backstop solves stay silent.
local function announce_matched(clamped)
  if og.mode_get(core.SLOT.MODE_ID) ~= 0 then
    return
  end
  if og.mode_get(MATCHED.ANNOUNCED) ~= 0 then
    return
  end
  if clamped then
    core.announce("TEAMS MATCHED (LIMIT)", C.SOUND_CHARGE)
    og.mode_set(MATCHED.ANNOUNCED, 2)
    return
  end
  core.announce("TEAMS MATCHED", C.SOUND_CHARGE)
  og.mode_set(MATCHED.ANNOUNCED, 1)
end

-- The D24 measure-and-solve arm: spawn the real squad, read the spawn-time
-- stats back as the model bases (no guessed constants ship — D13), solve
-- against the stored target, persist the plan, then level each member
-- exactly once (s_set_level BEFORE set_difficulty, both once per walker
-- object — D14). Placement stays in member order, before the leveling
-- pass; neither placement nor the probe reads levels.
local function spawn_matched_bots(team, families, cursor_slot, placer)
  local members = {}
  local bases = {}
  for k = 1, #families do
    local w = add_squad_member(team, families[k])
    if w ~= nil then
      members[#members + 1] = w
      bases[#bases + 1] = { family = families[k], stats = measured_base(w) }
      place_member(w, team, cursor_slot, placer)
    end
  end
  if #members == 0 then
    return
  end
  local target = og.mode_get(MATCHED.TARGET)
  local level, up, clamped = solve_matched_levels(target, bases)
  store_plan(team, level, up)
  for k = 1, #members do
    local w = members[k]
    local member_level = bot_level_for(team, k)
    w:s_set_level(member_level)
    w:set_difficulty(member_level)
  end
  announce_matched(clamped)
end

-- Fields one bot per family for an active team that authored no livings,
-- over the SIZE-truncated prefix of the caller's table (D34/D39 — every
-- caller keeps its signature; the headcount rule lives inside this one
-- seam, so init fills, the wiped-team backstops and the mutant seats all
-- honor it identically). Level source (§5.4, one rule for every scripted
-- spawn): a stored matched plan wins; else a stored match target takes
-- the D24 measure-and-solve arm; else the legacy session-difficulty
-- formula, byte-identical to the pre-matched spawner. placer is an
-- optional placement override, placer(w, team, allow_teleport)
-- (matched-teams D16): CTF passes its own anchor rotation, which falls
-- back to the flag-home square before the teleport draw; nil keeps
-- mode_match's rotation over cursor_slot.
local function spawn_bots(team, families, cursor_slot, placer)
  local squad = matched_families(families)
  if plan_code(team) == 0 then
    if og.mode_get(MATCHED.TARGET) > 0 then
      spawn_matched_bots(team, squad, cursor_slot, placer)
      return
    end
  end
  for k = 1, #squad do
    local w = add_squad_member(team, squad[k])
    if w ~= nil then
      local level = bot_level_for(team, k)
      w:s_set_level(level)
      w:set_difficulty(level)
      place_member(w, team, cursor_slot, placer)
    end
  end
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
local function revive_wiped_teams(anchors, mask, ticks, cursor_slot)
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
          anchors.spawn_bot_squad(team, cursor_slot)
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
  MATCHED = MATCHED,
  measured_base = measured_base,
  walker_power = walker_power,
  predicted_power = predicted_power,
  census_power = census_power,
  solve_matched_levels = solve_matched_levels,
  bot_level_for = bot_level_for,
  census_mask = census_mask,
  own_roster_activation = own_roster_activation,
  consume_markers = consume_markers,
  strip_inactive_teams = strip_inactive_teams,
  place_at_anchor = place_at_anchor,
  spawn_bots = spawn_bots,
  owns_its_life = owns_its_life,
  mark_owned_lives = mark_owned_lives,
  schedule_dead = schedule_dead,
  revive_wiped_teams = revive_wiped_teams,
  foe_scores = foe_scores,
  nearest_enemy = nearest_enemy,
  timeout_leader = timeout_leader,
}
