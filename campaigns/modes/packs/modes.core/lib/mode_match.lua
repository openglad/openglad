-- Shared match toolkit — manifest row adapter, the staged-init census (census_inputs) and the shared activation/fill rules all five mask modes apply at on_mode_init, marker consumption, anchor-cursor placement, bot fielding with the TROOPS: FAIR power model, dead-competitor scheduling, timeout ladder (cookbook: docs/lua-classpacks-design.md §3).
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

-- ---------------------------------------------------------------------------
-- TROOPS: FAIR power model (docs/matched-teams-design.md §4-§5)
-- ---------------------------------------------------------------------------

-- Mode-var slots. The design's per-team MATCHED_LEVEL/MATCHED_UP footprint
-- (D20) does not fit repo reality: CTF and Onslaught use every mode-private
-- slot 8..63 and Basketball leaves only 63, so the solved plan is PACKED
-- into the shared header band (slots 0-7, mode-neutral by convention:
-- MATCHED owns 2-5, mode_anchors' squad seed owns 6 (#235), and
-- basketball's item cursor owns 7 (#225) — the band is now full)
-- instead. TARGET is the capped mean human-team f (0 =
-- census not run, or no human power — both mean "legacy"); PLAN carries one
-- base-100 code per team, code = L * 10 + k (0 = unsolved, L in 1..9,
-- k in 0..4, max packed value 94 * 1010101 = 94,949,494 < 2^31); ANNOUNCED
-- latches the one-shot init announcement in its ONES digit (0 none, 1
-- normal, 2 limit) and packs the per-team applied lineup facts in the
-- digits above it (bank_lineup_facts below — the band is full, so the
-- slot is co-tenanted);
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

-- ---------------------------------------------------------------------------
-- Per-team bot squads (docs/lineup-design.md §3.2-§3.3)
-- ---------------------------------------------------------------------------

-- The campaign's bot-squad presets. The bot_squad_N knob's ordinal is the
-- index + 2 (0 = AUTO and 1 = NONE are engine-owned and never named
-- here); ids are the 6-char upper-case cycler faces the lineup hook
-- registers verbatim (scripts/campaign_picker.lua). Families name core
-- pack ids, all present in packs/core/families. FAIR carries no
-- families: it is the matched solver over the mode's own squad table
-- (§3.2). An optional `count` field replaces the matched-headcount rule
-- for that preset (cycling past the family list); the shipped five
-- state none.
local BOT_PRESETS = {
  { id = "BALANC",
    families = { "core:soldier", "core:archer", "core:elf", "core:mage",
                 "core:thief" } },
  { id = "CASTER",
    families = { "core:mage", "core:mage", "core:cleric", "core:elf",
                 "core:archer" } },
  { id = "BRUTES",
    families = { "core:soldier", "core:soldier", "core:barbarian",
                 "core:orc", "core:soldier" } },
  { id = "SKIRMS",
    families = { "core:thief", "core:elf", "core:archer", "core:thief",
                 "core:elf" } },
  { id = "FAIR" },
}

-- The per-team knob reads (lineup §3.1): bot_squad_N is 0 = AUTO,
-- 1 = NONE, 2.. = preset ordinal; bot_level_N is 0 = AUTO, 1..9. N is
-- the 1-based team, so the suffix is team + 1.
local function squad_knob(team)
  return og.match_setting("bot_squad_" .. (team + 1))
end

local function level_knob(team)
  return og.match_setting("bot_level_" .. (team + 1))
end

-- The preset a knob ordinal names, or nil — AUTO, NONE, and any ordinal
-- past the table (a stale save's knob degrades to AUTO, never errors:
-- the engine clamp is [0, 1 + kMaxBotPresets] regardless of this list).
local function preset_for(knob)
  if knob < 2 then
    return nil
  end
  return BOT_PRESETS[knob - 1]
end

-- The preset's family list for a knob, or nil (AUTO/NONE/FAIR/off-table
-- all keep the caller's own roster). The band path's seam (mode_fighters
-- fill_bots) as well as the mask spawn below.
local function preset_families(knob)
  local preset = preset_for(knob)
  if preset == nil then
    return nil
  end
  return preset.families
end

-- Pure squad shaping for a preset with families (lineup §3.2, one rule
-- for fills' count and the spawn seam's table): sized by preset.count
-- (which outranks the headcount rule and cycles past the family list),
-- else by the matched headcount (0 = the full list), then capped by the
-- caller's hard shape (basketball's 5v5). Exported for the rule probes.
local function preset_squad(preset, headcount, cap)
  local n = #preset.families
  local size = n
  if preset.count ~= nil then
    size = preset.count
  elseif headcount > 0 then
    size = og.min(headcount, n)
  end
  if cap ~= nil then
    size = og.min(size, cap)
  end
  local squad = {}
  for k = 1, size do
    squad[k] = preset.families[og.mod(k - 1, n) + 1]
  end
  return squad
end

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
-- team fields at least one live has_guy Living (the plan's roster
-- predicate — every g_/stat read stays behind the has_guy guard). Returns
-- (T, per-team sums); T = 0 means no human power server-side. The
-- headcount half of the old census (H, the D34 min-roster rule) now lives
-- in the DECISION alone (activation's matched_size below) — no residual
-- twin of that rule survives here.
local function census_power(obs)
  local sums = { 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          local team = w:team_num()
          if team < C.SCORE_TEAM_COUNT then
            sums[team + 1] = sums[team + 1] + walker_power(w)
          end
        end
      end
    end
  end
  local total = 0
  local teams = 0
  for t = 1, C.SCORE_TEAM_COUNT do
    if sums[t] > 0 then
      total = total + sums[t]
      teams = teams + 1
    end
  end
  if teams == 0 then
    return 0, sums
  end
  return og.div(total, teams), sums
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

-- The applied lineup facts, banked for the preview label (lineup §3.4).
-- The mode-private band is spent to the last slot in three of the five
-- mask modes, so the facts CO-TENANT the shared MATCHED.ANNOUNCED slot:
-- its ones digit stays the announce latch (0/1/2), and the digits above
-- it pack one base-100 code per team (PLAN's packing, shifted one
-- decimal digit up), code = applied preset ordinal * 10 + explicit
-- level, both single digits by their clamps. APPLIED, not requested: an
-- unregistered ordinal or a degraded FAIR banks 0, so the pane never
-- names a squad that did not spawn. Nothing is written while every code
-- is zero — an all-AUTO world keeps the slot byte-identical. C++ twin:
-- picker_common.cpp kModeVarLineupFacts.
local function lineup_code(team)
  return og.mod(og.div(og.mode_get(MATCHED.ANNOUNCED),
                       10 * PLAN_BASE[team + 1]), 100)
end

local function bank_lineup_facts(team, ordinal, level)
  local code = ordinal * 10 + level
  local held = lineup_code(team)
  if code == held then
    return
  end
  local v = og.mode_get(MATCHED.ANNOUNCED)
  og.mode_set(MATCHED.ANNOUNCED,
              v + (code - held) * 10 * PLAN_BASE[team + 1])
end

-- Apply-side half of the FAIR census split (D15/D24): each mode's decide
-- fold settled matched-ness and the headcount (decision.matched_size, the
-- D34 min-roster rule), and every mode's on_mode_init banks them here; the
-- power TARGET stays an init-time measurement because bot strength is
-- measure-and-solve by design (D24). Latched through MATCHED.TARGET so
-- mid-match spawns never re-census the live battle. The has_guy census is
-- immune to the troops strip (authored troops carry no guy record) and the
-- roster is already in the oblist (spawn_team_from_save precedes init).
local function bank_match_target(decision, obs)
  if not decision.matched then
    return
  end
  if og.mode_get(MATCHED.TARGET) ~= 0 then
    return
  end
  local target = census_power(obs)
  og.mode_set(MATCHED.TARGET, og.min(target, TARGET_CAP))
  og.mode_set(MATCHED.SIZE, decision.matched_size)
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

-- ---------------------------------------------------------------------------
-- The staged-init census and the shared activation/fill rules
-- ---------------------------------------------------------------------------

-- The init-time census (#218 staged lobby): the identical inputs shape the
-- retired C++ plan census produced (match_plan.cpp), rebuilt from the LIVE
-- world at the top of every on_mode_init. Under staging init runs ONCE per
-- stage in a REAL world — the census IS the world, so no marshaling layer
-- survives. Live NON-DORMANT livings split by has_guy, live non-dormant
-- generators, raw flag rows in fx order (out-of-range and surplus included
-- — the fold decides), the engine anchor counts (banked by mode_stage_init
-- before this hook, dead markers included) and the three request knobs the
-- staging rules consult. The other knobs (respawn_ticks, time_limit) are
-- read straight from og.match_setting where they are used -- a request
-- collected here and consumed nowhere is a field that rots.
-- The dormancy carve-out matches the C++ staged report census
-- (picker_common.cpp): delayed-spawn walkers are outside snapshot capture,
-- so a team the census counted but the pane could not see would activate
-- with a fill nobody rendered.
local function census_inputs()
  local inputs = {
    team_count = og.match_setting("team_count"),
    strip_troops = og.match_setting("strip_troops"),
    score_limit = og.match_setting("score_limit"),
    teams = {},
    flags = {},
    bot_squad = {},
    bot_level = {},
  }
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    inputs.teams[team + 1] = {
      anchors = og.respawn_anchor_count(team),
      roster = 0,
      npcs = 0,
      generators = 0,
    }
    -- The eight lineup knobs (§3.1), one pair per team, indexed like
    -- inputs.teams: the fill rules below consult the squad knob, the
    -- spawn seam re-reads both from the same replicated fields.
    inputs.bot_squad[team + 1] = squad_knob(team)
    inputs.bot_level[team + 1] = level_knob(team)
  end
  local obs = og.oblist()
  for k = 1, #obs do
    local w = obs[k]
    -- Dormant (delayed-spawn) walkers are skipped alongside dead ones: they
    -- are excluded from snapshot capture, so the C++ staged report censuses
    -- the non-dormant world only. Counting them here would activate a team
    -- and bank a fill the preview pane renders as absent (#218).
    if w:dead() == 0 and not w:dormant() then
      local team = w:team_num()
      if team < C.SCORE_TEAM_COUNT then
        local trow = inputs.teams[team + 1]
        local order = w:order()
        if order == C.ORDER_LIVING then
          if w:has_guy() then
            trow.roster = trow.roster + 1
          else
            trow.npcs = trow.npcs + 1
          end
        elseif order == C.ORDER_GENERATOR then
          trow.generators = trow.generators + 1
        end
      end
    end
  end
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
-- modes apply (issue #218): the ONE copy of the rule, consumed by each
-- mode's decide fold at the top of on_mode_init (only the inputs decide).
-- Precedence, in evaluation order:
--
--   TROOPS: ALL               the call site's requested activation stands:
--                             the lobby count, at TEAMS: Auto the caller's
--                             auto_default (the manifest row.teams for
--                             soccer/basketball/onslaught; 0 — "every
--                             authored team" — for CTF/TDM, the verified
--                             per-mode Auto asymmetry), clamped over the
--                             authored domain.
--   OWN/FAIR, any roster      ONE rule for every TEAMS value AND every
--   shape (empty included)    roster shape: the request is the explicit
--                             lobby count N, or at TEAMS: Auto the
--                             AUTHORED team count — the zero sentinel
--                             means "as many teams as the map actually
--                             has" (2026-08-18 maintainer directive,
--                             matched-teams-design.md). Roster teams all
--                             stay; authored non-roster teams backfill in
--                             index order until the mask holds
--                             clamp(request, 2, 4) teams; a roster already
--                             at or past the count stays exactly the
--                             roster (max semantics — a deployed side is
--                             never stripped to satisfy a count). An
--                             all-bot match (zero rosters anywhere)
--                             backfills from empty, so its mask equals the
--                             plain activation clamp of the same request.
--                             A solo roster on a map authoring nobody else
--                             comes back alone and reads starts = false.
--
-- Returns (active_mask, starts, matched, matched_size): matched reports a
-- TROOPS: FAIR request with a deployed roster anywhere (the predicted
-- nonzero census — FAIR with no roster degrades to the legacy squads);
-- matched_size is the D34 headcount rule — one roster team = its deployed
-- count, several = the MIN across them, authored or not (mirroring the
-- census, which prices every has_guy walker on a score team).
local function activation(inputs, authored_mask, auto_default)
  local matched = false
  local matched_size = 0
  if inputs.strip_troops == core.MATCHED_TROOPS then
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
  end
  if inputs.strip_troops <= strip.KEEP then
    local requested = inputs.team_count
    if requested <= 0 then
      requested = auto_default
    end
    local mask = core.activate_teams(authored_mask, requested)
    return mask, core.mask_count(mask) >= 2, matched, matched_size
  end
  local roster_mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if inputs.teams[team + 1].roster > 0 then
      if core.mask_has(authored_mask, team) then
        roster_mask = core.mask_add(roster_mask, team)
      end
    end
  end
  local requested = inputs.team_count
  if requested <= 0 then
    requested = core.mask_count(authored_mask)
  end
  local target = og.clamp(requested, 2, C.SCORE_TEAM_COUNT)
  local mask = roster_mask
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_count(mask) < target then
      if core.mask_has(authored_mask, team) then
        mask = core.mask_add(mask, team)
      end
    end
  end
  return mask, core.mask_count(mask) >= 2, matched, matched_size
end

-- Per-team fill rows: what fields each team once the apply has consumed
-- markers, stripped and backfilled — precomputed from the same counts the
-- strips reduce to (under strip-on only has_guy walkers survive,
-- mode_strip; under KEEP an active team keeps roster + npcs), so the
-- apply spawns exactly where a row says "bots"/"matched". opts:
--   keep_generators   (onslaught) generators survive the strip
--   no_bots           (onslaught D17) never field a squad
--   matched, matched_size   activation's answers (the FAIR seam)
--   squad_cap         the mode's hard shape (basketball's 5v5, lineup
--                     §3.2): no squad row counts past it
-- Returns (teams, wants_bots, mask): teams is the decision's [1..4] row
-- array ({active, fill, count, squad}); wants_bots reports any squad row
-- (squad classes are drawn at spawn — mode_anchors squad_code); mask is
-- active_mask minus the teams the NONE knob emptied outright (lineup
-- §3.2: "a team is on when anything is on it"), so every consumer of the
-- decision — TEAM_MASK, strips, win ladders, the starts recheck — sees
-- the narrowed truth.
local function fills(inputs, active_mask, opts)
  -- Every shipped squad table fields five bots (D35 soldier-first); a
  -- matched squad truncates to the headcount prefix (D39).
  local squad_size = 5
  local keep_generators = false
  local no_bots = false
  local matched = false
  local matched_size = 0
  local squad_cap = nil
  if opts ~= nil then
    keep_generators = opts.keep_generators == true
    no_bots = opts.no_bots == true
    matched = opts.matched == true
    matched_size = opts.matched_size or 0
    squad_cap = opts.squad_cap
  end
  local squad_knobs = inputs.bot_squad or {}
  local strip_on = inputs.strip_troops > strip.KEEP
  local teams = {}
  local wants_bots = false
  local mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local row = inputs.teams[team + 1]
    local active = core.mask_has(active_mask, team)
    local fill = "empty"
    local count = 0
    local knob = squad_knobs[team + 1] or 0
    local preset = preset_for(knob)
    local squad = nil
    if active then
      local troops_stand = row.npcs > 0
      if strip_on then
        troops_stand = false
      end
      local gens_stand = row.generators > 0
      if strip_on then
        if not keep_generators then
          gens_stand = false
        end
      end
      if preset ~= nil then
        -- I3 as amended by lineup §3.2 (matched-teams-design.md I3): a
        -- preset squad applies to ANY active team, occupied or not —
        -- the row keeps its occupancy fill and `squad` carries the
        -- ordinal the apply spawns beside it. Onslaught's no_bots (D17)
        -- still outranks the knob.
        if not no_bots then
          squad = knob
          wants_bots = true
        end
      end
      if row.roster > 0 then
        fill = "company"
        count = row.roster
      elseif troops_stand then
        fill = "troops"
        count = row.npcs
      elseif no_bots then
        if gens_stand then
          fill = "generators"
          count = row.generators
        end
      elseif knob == 1 then
        -- NONE (lineup §3.2): the squad today's rule would field is
        -- suppressed. Generators still put the team on; one left with
        -- nothing at all drops out of the mask below.
        if gens_stand then
          fill = "generators"
          count = row.generators
        else
          active = false
        end
      else
        -- The FAIR -> legacy degrade rides here: matched with a zero
        -- headcount (no roster anywhere -> predicted TARGET 0) is the
        -- plain difficulty squad, exactly what spawn_bots does at
        -- MATCHED.TARGET == 0.
        fill = "bots"
        count = squad_size
        wants_bots = true
        if matched then
          if matched_size > 0 then
            fill = "matched"
            count = og.min(matched_size, squad_size)
          end
        end
        if preset ~= nil and preset.families ~= nil then
          -- A preset squad sizes from its own table (lineup §3.2), the
          -- same one rule the spawn seam applies (preset_squad).
          local headcount = 0
          if fill == "matched" then
            headcount = matched_size
          end
          count = #preset_squad(preset, headcount, squad_cap)
        elseif squad_cap ~= nil then
          count = og.min(count, squad_cap)
        end
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
    }
  end
  return teams, wants_bots, mask
end

-- Does a decision row call for a squad spawn? The one predicate every
-- mode's apply loop asks (lineup §3.2): the classic empty-team fills, or
-- a preset riding an occupied team (row.squad).
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

-- The families a team's squad actually fields (lineup §3.2): a preset
-- with families replaces the caller's table; AUTO, FAIR and any
-- off-table ordinal keep the caller's table through the headcount rule.
-- cap is the caller's hard shape. NONE never reaches here — spawn_bots
-- returns first.
local function squad_families(knob, families, cap)
  local preset = preset_for(knob)
  if preset ~= nil and preset.families ~= nil then
    return preset_squad(preset, og.mode_get(MATCHED.SIZE), cap)
  end
  local squad = matched_families(families)
  if cap ~= nil then
    if #squad > cap then
      local capped = {}
      for k = 1, cap do
        capped[k] = squad[k]
      end
      squad = capped
    end
  end
  return squad
end

-- The FAIR preset's solve target (lineup §3.2, maintainer ruling of
-- 2026-08-25): an occupied team fields ALLIES, so it targets the gap —
-- the best other team's human f-sum minus its own — which may come out
-- at or below zero (the solver's B(1) clamp then answers the weakest
-- squad, announced as LIMIT). An empty team keeps the D11 mean.
local function fair_target(mean, sums, team)
  if sums[team + 1] <= 0 then
    return mean
  end
  local best = 0
  for t = 1, C.SCORE_TEAM_COUNT do
    if t ~= team + 1 then
      if sums[t] > best then
        best = sums[t]
      end
    end
  end
  return best - sums[team + 1]
end

local function add_squad_member(team, family_name)
  local fam = og.family_id("living", family_name) --[[@as integer]] -- caller tables name core families; a nil would be a load bug and add_ob erroring loudly is the right failure
  local w = og.add_ob("living", fam)
  if w == nil then
    return nil
  end
  w:set_team_num(team)
  w:set_real_team_num(255)
  caps.mark_bot(w)
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
  -- The latch is the slot's ONES digit alone — the digits above it are
  -- the co-tenant lineup facts (bank_lineup_facts), so the read and both
  -- writes stay inside that digit.
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

-- The D24 measure-and-solve arm: spawn the real squad, read the spawn-time
-- stats back as the model bases (no guessed constants ship — D13), solve
-- against the stored target, persist the plan, then level each member
-- exactly once (s_set_level BEFORE set_difficulty, both once per walker
-- object — D14). Placement stays in member order, before the leveling
-- pass; neither placement nor the probe reads levels.
local function spawn_matched_bots(team, families, cursor_slot, placer,
                                  target)
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
  if target == nil then
    target = og.mode_get(MATCHED.TARGET)
  end
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
-- honor it identically). The lineup knobs (§3.2) are consulted HERE, the
-- one squad seam, so init fills and the wiped-team backstop obey them
-- alike: NONE fields nothing; a preset's families replace the caller's
-- (squad_families above); an explicit bot_level is stored as the team's
-- plan so the leveling below and every later respawn reproduce it —
-- s_set_level + set_difficulty exactly once per walker, the D14
-- discipline. Level source otherwise (§5.4, one rule for every scripted
-- spawn): a stored matched plan wins; else the FAIR preset solves
-- against its own target (fair_target over a fresh census — LOCAL, never
-- banked, so other teams' AUTO squads stay legacy); else a stored match
-- target takes the D24 measure-and-solve arm; else the legacy
-- session-difficulty formula, byte-identical to the pre-matched spawner.
-- placer is an optional placement override, placer(w, team,
-- allow_teleport) (matched-teams D16): CTF passes its own anchor
-- rotation, which falls back to the flag-home square before the teleport
-- draw; nil keeps mode_match's rotation over cursor_slot. cap is the
-- caller's hard shape (basketball's 5v5).
local function spawn_bots(team, families, cursor_slot, placer, cap)
  local knob = squad_knob(team)
  if knob == 1 then
    return
  end
  local squad = squad_families(knob, families, cap)
  local preset = preset_for(knob)
  local wants_level = level_knob(team)
  if wants_level > 0 then
    if plan_code(team) ~= wants_level * 10 then
      store_plan(team, wants_level, 0)
    end
  end
  local applied = 0
  if preset ~= nil then
    applied = knob
  end
  local fair = preset ~= nil and preset.families == nil
  if plan_code(team) == 0 then
    local target = nil
    if fair then
      local mean, sums = census_power(og.oblist())
      if mean > 0 then
        target = og.min(fair_target(mean, sums, team), TARGET_CAP)
      end
    end
    if target == nil then
      if og.mode_get(MATCHED.TARGET) > 0 then
        target = og.mode_get(MATCHED.TARGET)
      end
    end
    if target ~= nil then
      spawn_matched_bots(team, squad, cursor_slot, placer, target)
      bank_lineup_facts(team, applied, 0)
      return
    end
  end
  if fair then
    -- FAIR stays a banked fact only where a solve governs the squad — a
    -- refill under a stored solve plan reproduces it, so the fact
    -- stands. An explicit level (AUTO-equivalent squad at that level)
    -- and the no-human-power degrade both bank AUTO, so the pane never
    -- claims a solve that did not run.
    if wants_level > 0 then
      applied = 0
    elseif plan_code(team) == 0 then
      applied = 0
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
  bank_lineup_facts(team, applied, wants_level)
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
  BOT_PRESETS = BOT_PRESETS,
  preset_families = preset_families,
  preset_squad = preset_squad,
  fair_target = fair_target,
  stat_power = stat_power,
  measured_base = measured_base,
  walker_power = walker_power,
  predicted_power = predicted_power,
  census_power = census_power,
  solve_matched_levels = solve_matched_levels,
  bot_level_for = bot_level_for,
  census_inputs = census_inputs,
  activation = activation,
  fills = fills,
  wants_squad = wants_squad,
  bank_match_target = bank_match_target,
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
