-- core lib: lineup — the match machinery every campaign shares (docs/lineup-design.md C1): the f power metric and census, the difficulty-tuple model, the D22 matched solver and plan, the FILL wheel and MAP UNITS box reads, squad sizing/placement/spawn, the per-team strip and the applied-fact banking. Mode-specific rules (masks, refusals, hard-shape caps, band modes, announces) stay in the modes campaign, which consumes THIS module via og.use("core:lineup") so no rule lives twice (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

-- Absolute value. The sandbox has no math library; the solver's argmin and
-- the modes' geometry share this one spelling (mode_core re-exports it).
local function iabs(v)
  -- plumbing: iabs: integer absolute value helper for the solver's miss
  if v < 0 then
    return -v
  end
  return v
end

-- ---------------------------------------------------------------------------
-- The FILL power model (docs/matched-teams-design.md §4-§5, lineup B2/B3)
-- ---------------------------------------------------------------------------

-- Mode-var slots. The design's per-team MATCHED_LEVEL/MATCHED_UP footprint
-- (D20) does not fit repo reality: CTF and Onslaught use every mode-private
-- slot 8..63 and Basketball leaves only 63, so the solved plan is PACKED
-- into the shared header band (slots 0-7, mode-neutral by convention:
-- MATCHED owns 2-5, mode_anchors' squad seed owns 6 (#235), and
-- basketball's item cursor owns 7 (#225) — the band is now full)
-- instead. TARGET is the capped REFERENCE — the weakest human team's
-- f-sum, amendment B3 (0 =
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
-- The per-team FILL wheel and MAP UNITS box (docs/lineup-design.md B1-B4)
-- ---------------------------------------------------------------------------

-- The fill_N knob's scale, ONE scale on every layer (lobby_state.h
-- kFillNone / kFillWeak / kFillFair / kFillStrong / kFillBrutal —
-- amendment B2 as renumbered by E1): FILL is the matched solver with a
-- multiplier, and the engine stores and clamps the CODE alone — this
-- table is the only copy of what each code is worth, keyed by the raw
-- value og.match_setting("fill_N") answers. 0 = NONE, the stored default
-- on every map (no squad on this team, ever — it deliberately has no
-- FILL_PERCENT row), then the four spawnable wheel codes in wheel order:
-- 1 = WEAK (×0.75), 2 = FAIR (×1), 3 = STRONG (×1.25), 4 = BRUTAL
-- (×1.5). E1 retired the DEFAULT/explicit distinction (D1's separate
-- default code and the C8 per-team resolver with it): a stored 0 IS
-- NONE, everywhere, and only a turned wheel fields anything.
local FILL_NONE = 0
local FILL_WEAK = 1
local FILL_FAIR = 2
local FILL_STRONG = 3
local FILL_BRUTAL = 4
local FILL_PERCENT = { [1] = 75, [2] = 100, [3] = 125, [4] = 150 }

-- The map_units_N box (amendment B4): 0 = the map's own authored units on
-- that team are fielded (the default), 1 = they are not (the old TROOPS
-- strip, per team — generators follow the same box).
local MAP_UNITS_ON = 0

-- The per-team knob reads. N is the 1-based team, so the suffix is
-- team + 1 — the ONE place either name is spelled in pack Lua.
local function fill_knob(team)
  return og.match_setting("fill_" .. (team + 1))
end

local function map_units_knob(team)
  return og.match_setting("map_units_" .. (team + 1))
end

-- Does a knob forbid a squad outright? NONE does (amendment B8: no other
-- wheel value refuses anything), and so does any code without a
-- multiplier row — junk reads NONE (E2: it is the storage default, it is
-- where the engine's clamp lands a negative, and it promises no squad
-- the level will not field). The band path (mode_fighters) asks the same
-- question of team 1's knob.
local function squad_off(knob)
  return FILL_PERCENT[knob] == nil
end

-- The knob's multiplier in integer percent. Every apply seam gates on
-- squad_off first, so the row exists by construction — a nil here is a
-- seam that skipped its gate, and the arithmetic erroring loudly is the
-- right failure.
local function fill_percent(knob)
  return FILL_PERCENT[knob]
end

-- Are team t's map-shipped units fielded? The box read the per-team strip
-- (strip_authored_troops below) applies — non-zero reads as OFF, so a
-- crafted value can never field units the host's box hid.
local function map_units_fielded(team)
  return map_units_knob(team) == MAP_UNITS_ON
end

-- The room a hard shape leaves beside a team's occupants (lineup review
-- L2): a squad riding an occupied team is sized to cap minus that team's
-- roster, never below zero — three humans on basketball's five-a-side
-- court get two allies, five get none. No hard shape (nil) = no bound.
-- One rule for fills' count (over the census roster) and the spawn seam
-- (over the live has_guy count, the same number in the staged world).
local function squad_room(cap, roster)
  -- rule: A squad riding an occupied team under a hard shape (basketball's five-a-side) gets cap
  --   minus that team's fielded headcount, never below zero; no hard shape means no bound.
  -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
  if cap == nil then
    return nil
  end
  return og.max(cap - roster, 0)
end

-- The SHAPE of one squad: how many bodies it fields, at what percent of
-- the reference, and against what baseline the solve divides (issue
-- #305). ONE helper, so the decision (mode_match.fills) and the apply
-- (spawn_bots below) can never disagree about a count.
--
-- The baseline is the headcount rule (D34/D39): a generated squad never
-- outnumbers the roster it was measured against, so the count is
-- min(H, #families) -- soldier-first for the standard tables (D35) --
-- inside the room a hard shape leaves. H = 0 (no census ran, or no human
-- power anywhere) keeps the whole table, so the legacy arm and the
-- direct spawn-probe arms stay byte-identical.
--
-- On the BALL games only (the mode's shape carries bodies = true; the
-- modes campaign's lib/mode_shape.lua is the one table that says which
-- games those are), and on the EMPTY-team arm only, each wheel step
-- above FAIR buys one more BODY at roughly one human's power instead of
-- 25 % more power on the same bodies:
--
--   H    WEAK       FAIR        STRONG          BRUTAL
--   1    1 @ 75 %   1 @ 100 %   2 @ 100 % each  3 @ 100 % each
--   2    2 @ 75 %   2 @ 100 %   3               4
--   3    3 @ 75 %   3 @ 100 %   4               5
--   4    4 @ 75 %   4 @ 100 %   5               5 @ 125 %
--   >=5  5 @ 75 %   5 @ 100 %   5 @ 125 %       5 @ 150 %
--   0    the legacy difficulty squad of five, unchanged
--
-- A step the shape cannot absorb as a body (the five-family table, or
-- the room beside the occupants) falls back to today's power step, so
-- the wheel is never a no-op and a full roster plays byte-identically.
-- One wheel step per absorbed body, read off the table's own spacing
-- (FILL_PERCENT[knob] - FILL_PERCENT[knob - 1]) -- never a second
-- constant.
--
-- Allies (a squad beside a company) and troops (a squad beside standing
-- map units) keep the baseline count (D14): #305 is about the opponent
-- the reporter faces, and B3's "allies field the gap" is a ruling of its
-- own. Their callers pass fielded_is_empty = false.
--
-- Returns count, pct, base. Every caller gates on squad_off first, so
-- FILL_PERCENT[knob] is non-nil by contract (fill_percent above); room
-- is nil where no hard shape bounds the squad.
local function squad_shape(knob, headcount, room, table_size,
                           fielded_is_empty, bodies_allowed)
  local base = table_size
  -- rule: A squad fields min(H, table) bodies (whole table with no census), inside any hard-shape
  --   room; on ball games' EMPTY teams each wheel step above FAIR buys one more body at the
  --   baseline per-fighter power (pct reduced one step per absorbed body) instead of +25 % power,
  --   falling back to the power step when the table or room cannot absorb a body.
  -- tests: StagedRules.lineup_squad_shape_table
  -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
  if headcount > 0 then
    base = og.min(headcount, table_size)
  end
  if room ~= nil then
    base = og.min(base, room)
  end
  local count = base
  local steps = knob - FILL_FAIR
  local buys_bodies = bodies_allowed and fielded_is_empty
  if buys_bodies then
    -- rule: A squad fields min(H, table) bodies (whole table with no census), inside any hard-shape
    --   room.
    -- tests: StagedRules.lineup_squad_shape_table
    -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
    if steps > 0 then
      -- rule: A squad fields min(H, table) bodies (whole table with no census), inside any
      --   hard-shape room.
      -- tests: StagedRules.lineup_squad_shape_table
      -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
      if headcount > 0 then
        count = og.min(headcount + steps, table_size)
        -- rule: A squad fields min(H, table) bodies (whole table with no census), inside any
        --   hard-shape room.
        -- tests: StagedRules.lineup_squad_shape_table
        -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
        if room ~= nil then
          count = og.min(count, room)
        end
      end
    end
  end
  local absorbed = count - base
  local pct = FILL_PERCENT[knob]
  if absorbed > 0 then
    pct = pct - (FILL_PERCENT[knob] - FILL_PERCENT[knob - 1]) * absorbed
  end
  return count, pct, base
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
--   core:orc      packs/core/families/living-14-orc.lua:10
--   core:#18      packs/core/families/living-18-beast.lua:8 (GOLEM)
--   core:cleric   packs/core/families/living-05-cleric.lua:285
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

-- The default squad table (D35, soldier-first): the five-bot fill every
-- shipped squad seam starts from. The modes campaign shuffles a seeded
-- permutation of this table (mode_anchors #235); the classic lineup stage
-- fields it in this exact order.
local BOT_SQUAD = { "core:soldier", "core:archer", "core:elf", "core:mage", "core:thief" }

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
  -- rule: A family with no difficulty tuple row is priced with the engine default 11/11/4/2 per
  --   level.
  -- tests: ModesTdm.matched_power_metric_and_solver_arms
  if row == nil then
    row = TUPLE_DEFAULT
  end
  local hp = base.hp + row.hp * level * level
  local mp = base.mp + row.mp * level * level
  local dmg = base.dmg + row.dmg * level
  local armor = base.armor + og.trunc(row.armor * level * level)
  return stat_power(hp, mp, armor, dmg, base.sp, base.ff, level)
end

-- The human census (amendment B3 — D11's mean is retired): per-team
-- f-sums over the live has_guy Livings (the plan's roster predicate —
-- every g_/stat read stays behind the has_guy guard), and the REFERENCE:
-- the weakest human team's f-sum, over every team fielding at least one
-- has_guy Living. Returns (reference, per-team sums); reference = 0 means
-- no human power anywhere (the legacy-formula arm). The headcount half of
-- the census (the D34 min-roster rule) lives in the DECISION alone
-- (the modes' activation fold) — no residual twin survives here.
local function census_power(obs)
  local sums = { 0, 0, 0, 0 }
  -- plumbing: census_power oblist scan; rule below
  for k = 1, #obs do
    local w = obs[k]
    -- rule: Human power is the per-team sum of f over live roster characters (has_guy livings) on
    --   score teams; guyless livings, the dead and wildlife teams add nothing.
    -- tests: ModesTdm.matched_census_single_team_sum_ignores_guyless_livings
    -- tests: ModesTdm.matched_census_weakest_reference_and_heart_value_rank
    -- tests: SemLineup.the_power_census_counts_only_live_heroes_on_score_teams
    if w:dead() == 0 then
      -- rule: Human power is the per-team sum of f over live roster characters (has_guy livings) on
      --   score teams; guyless livings, the dead and wildlife teams add nothing. Here:
      --   `order==LIVING`.
      -- tests: ModesTdm.matched_census_single_team_sum_ignores_guyless_livings
      -- tests: ModesTdm.matched_census_weakest_reference_and_heart_value_rank
      -- tests: SemLineup.the_power_census_counts_only_live_heroes_on_score_teams
      if w:order() == C.ORDER_LIVING then
        -- rule: Human power is the per-team sum of f over live roster characters (has_guy livings)
        --   on score teams; guyless livings, the dead and wildlife teams add nothing. Here:
        --   `has_guy`.
        -- tests: ModesTdm.matched_census_single_team_sum_ignores_guyless_livings
        -- tests: ModesTdm.matched_census_weakest_reference_and_heart_value_rank
        -- tests: SemLineup.the_power_census_counts_only_live_heroes_on_score_teams
        if w:has_guy() then
          local team = w:team_num()
          -- rule: Human power is the per-team sum of f over live roster characters (has_guy
          --   livings) on score teams; guyless livings, the dead and wildlife teams add nothing.
          --   Here: `team < SCORE_TEAM_COUNT`.
          -- tests: ModesTdm.matched_census_single_team_sum_ignores_guyless_livings
          -- tests: ModesTdm.matched_census_weakest_reference_and_heart_value_rank
          -- tests: SemLineup.the_power_census_counts_only_live_heroes_on_score_teams
          if team < C.SCORE_TEAM_COUNT then
            sums[team + 1] = sums[team + 1] + walker_power(w)
          end
        end
      end
    end
  end
  local reference = 0
  -- plumbing: census_power reference scan over teams; rule below
  for t = 1, C.SCORE_TEAM_COUNT do
    -- rule: The match REFERENCE is the weakest human team's f-sum among teams that field any human
    --   power; 0 means no human power anywhere (legacy formula).
    -- tests: ModesTdm.matched_census_single_team_sum_ignores_guyless_livings
    -- tests: ModesTdm.matched_census_weakest_reference_and_heart_value_rank
    -- tests: ModesTdm.matched_power_metric_and_solver_arms
    if sums[t] > 0 then
      -- rule: The match REFERENCE is the weakest human team's f-sum among teams that field any
      --   human power; 0 means no human power anywhere (legacy formula). Here: `reference == 0`.
      -- tests: ModesTdm.matched_census_single_team_sum_ignores_guyless_livings
      -- tests: ModesTdm.matched_census_weakest_reference_and_heart_value_rank
      -- tests: ModesTdm.matched_power_metric_and_solver_arms
      if reference == 0 then
        reference = sums[t]
      elseif sums[t] < reference then
        reference = sums[t]
      end
    end
  end
  return reference, sums
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
  -- plumbing: solve_matched_levels prediction/sum/scan loops; rule below
  for i = 1, n do
    local row = {}
    -- plumbing: solve_matched_levels prediction/sum/scan loops; rule below
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
    -- plumbing: solve_matched_levels prediction/sum/scan loops; rule below
    for i = 1, n do
      p = p + pred[i][level]
    end
    local up_max = n - 1
    -- rule: The matched solver picks the uniform level L (1..9) plus k first members one level up
    --   that comes closest to the target power, ties going to the lower L then lower k; L9 admits
    --   no upgrades; a target below B(1) or above B(9) clamps (and reports LIMIT).
    -- tests: ModesTdm.matched_power_metric_and_solver_arms
    if level == 9 then
      up_max = 0
    end
    -- plumbing: solve_matched_levels prediction/sum/scan loops; rule above
    for up = 0, up_max do
      -- rule: The matched solver picks the uniform level L (1..9) plus k first members one level up
      --   that comes closest to the target power, ties going to the lower L then lower k. Here: `up
      --   > 0`.
      -- tests: ModesTdm.matched_power_metric_and_solver_arms
      if up > 0 then
        p = p + pred[up][level + 1] - pred[up][level]
      end
      local miss = iabs(p - target)
      local better = best_miss < 0
      -- rule: The matched solver picks the uniform level L (1..9) plus k first members one level up
      --   that comes closest to the target power, ties going to the lower L then lower k. Here:
      --   `better = best_miss < 0`.
      -- tests: ModesTdm.matched_power_metric_and_solver_arms
      if not better then
        better = miss < best_miss
      end
      -- rule: The matched solver picks the uniform level L (1..9) plus k first members one level up
      --   that comes closest to the target power, ties going to the lower L then lower k. Here:
      --   `miss < best_miss`.
      -- tests: ModesTdm.matched_power_metric_and_solver_arms
      if better then
        best_level = level
        best_up = up
        best_miss = miss
      end
    end
  end
  local clamped = target < b1
  -- rule: The matched solver picks the uniform level L (1..9) plus k first members one level up
  --   that comes closest to the target power, ties going to the lower L then lower k. Here: `target
  --   > b9`.
  -- tests: ModesTdm.matched_power_metric_and_solver_arms
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

-- The applied lineup facts, banked for the preview label (amendment B7).
-- The mode-private band is spent to the last slot in three of the five
-- mask modes, so the facts CO-TENANT the shared MATCHED.ANNOUNCED slot:
-- its ones digit stays the announce latch (0/1/2), and the digits above
-- it pack one base-100 code per team (PLAN's packing, shifted one
-- decimal digit up). code = 0 when no squad of this team's fielded
-- anybody (the pane names no fill word), else THE STORED CODE OF A SQUAD
-- THAT SPAWNED (E4) — 1 = WEAK, 2 = FAIR, 3 = STRONG, 4 = BRUTAL. NONE
-- never spawns and never banks, so 0 is unambiguously "this team banked
-- nothing" and no bias or resolution ever shifts a code. APPLIED, not
-- requested: a squad that spawned nothing banks nothing (review R4).
-- C++ twin: picker_common.cpp kModeVarLineupFacts (lineup_fact_code /
-- lineup_fact_fill — 1..4 accepted, 0 and junk answer no fill word).
local function lineup_code(team)
  return og.mod(og.div(og.mode_get(MATCHED.ANNOUNCED),
                       10 * PLAN_BASE[team + 1]), 100)
end

local function bank_lineup_facts(team, fill)
  local code = fill
  local held = lineup_code(team)
  -- rule: The applied fill code of each team is banked once into the ANNOUNCED slot's upper digits
  --   (re-banking the same code changes nothing), leaving the announce latch digit alone.
  -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
  -- tests: SemLineup.re_banking_the_same_fill_code_leaves_the_announced_slot_alone
  if code == held then
    return
  end
  local v = og.mode_get(MATCHED.ANNOUNCED)
  og.mode_set(MATCHED.ANNOUNCED,
              v + (code - held) * 10 * PLAN_BASE[team + 1])
end

-- Apply-side half of the census split (D15/D24, amendment B3): each
-- mode's decide fold settled whether any human roster stands
-- (decision.matched) and the headcount (decision.matched_size, the D34
-- min-roster rule), and every mode's on_mode_init banks them here; the
-- power REFERENCE — the weakest human team's f-sum — stays an init-time
-- measurement because bot strength is measure-and-solve by design (D24).
-- Latched through MATCHED.TARGET so mid-match spawns never re-census the
-- live battle. The has_guy census is immune to the map-units strip
-- (authored troops carry no guy record) and the roster is already in the
-- oblist (spawn_team_from_save precedes init).
local function bank_match_target(decision, obs)
  -- rule: The match target (weakest human reference, capped) and headcount are latched once at init
  --   when a human roster stands; later inits and mid-match spawns never re-census.
  -- tests: ModesTdm.matched_power_metric_and_solver_arms
  -- tests: ModesTdm.matched_solo_fresh_squad_fields_an_even_rival
  -- tests: ModesTdm.matched_spawn_bots_legacy_arm_is_unchanged
  if not decision.matched then
    return
  end
  if og.mode_get(MATCHED.TARGET) ~= 0 then
    return
  end
  local reference = census_power(obs)
  og.mode_set(MATCHED.TARGET, og.min(reference, TARGET_CAP))
  og.mode_set(MATCHED.SIZE, decision.matched_size)
end

-- The legacy session-difficulty level formula (50/100/200 percent ->
-- L1/L2/L3), the level source of every unsolved squad (amendment B3: no
-- human power anywhere -> this formula, whatever the wheel says short of
-- NONE).
local function difficulty_level()
  return og.max(1, og.div(og.match_setting("difficulty"), 100) + 1)
end

-- Per-member spawn level (§5.4): a stored plan answers L (or L + 1 for the
-- first k members); an unsolved team takes the legacy session-difficulty
-- formula, byte-identical to the pre-matched spawner.
local function bot_level_for(team, index)
  local code = plan_code(team)
  -- rule: A squad member's level is the stored plan's L (L+1 for the first k members); a team with
  --   no plan takes the legacy session-difficulty level (difficulty/100 + 1).
  -- tests: ModesTdm.matched_power_metric_and_solver_arms
  -- tests: ModesTdm.matched_spawn_bots_legacy_arm_is_unchanged
  if code == 0 then
    return difficulty_level()
  end
  local level = og.div(code, 10)
  if index <= og.mod(code, 10) then
    return level + 1
  end
  return level
end

-- The init-time census (#218 staged lobby): the identical inputs shape the
-- retired C++ plan census produced (match_plan.cpp), rebuilt from the LIVE
-- world at the top of every on_mode_init. Under staging init runs ONCE per
-- stage in a REAL world — the census IS the world, so no marshaling layer
-- survives. Live NON-DORMANT livings split by has_guy, live non-dormant
-- generators, per-team human f-sums (teams[t].power — the fills rows
-- decide the allies gap from the inputs alone), the engine
-- anchor counts (banked by mode_stage_init before this hook, dead markers
-- included) and the request knobs the staging rules consult: the score
-- limit and the eight lineup knobs. TEAMS (team_count, amendment A1/A3)
-- and TROOPS (strip_troops, amendment B5) are retired: both fields are
-- inert engine-side and read by nobody here — the per-team FILL wheel
-- and MAP UNITS box are their successors. The other knobs
-- (respawn_ticks, time_limit) are read straight from og.match_setting
-- where they are used -- a request collected here and consumed nowhere
-- is a field that rots.
-- The flag rows are MODE data (the modes campaign's own treasure family),
-- so the modes' census wrapper appends inputs.flags itself; this core
-- half starts the list empty.
-- The dormancy carve-out matches the C++ staged report census
-- (picker_common.cpp): delayed-spawn walkers are outside snapshot capture,
-- so a team the census counted but the pane could not see would activate
-- with a fill nobody rendered.
local function census_inputs()
  local inputs = {
    score_limit = og.match_setting("score_limit"),
    teams = {},
    flags = {},
    fill = {},
    map_units = {},
  }
  -- plumbing: census_inputs team and oblist loops; rule below
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    inputs.teams[team + 1] = {
      anchors = og.respawn_anchor_count(team),
      roster = 0,
      npcs = 0,
      generators = 0,
      power = 0,
    }
    -- The eight lineup knobs (B2/B4), one pair per team, indexed like
    -- inputs.teams: the fill rules consult both, the spawn seam
    -- and the strip re-read them from the same replicated fields.
    inputs.fill[team + 1] = fill_knob(team)
    inputs.map_units[team + 1] = map_units_knob(team)
  end
  local obs = og.oblist()
  for k = 1, #obs do
    local w = obs[k]
    -- Dormant (delayed-spawn) walkers are skipped alongside dead ones: they
    -- are excluded from snapshot capture, so the C++ staged report censuses
    -- the non-dormant world only. Counting them here would activate a team
    -- and bank a fill the preview pane renders as absent (#218).
    -- rule: The staging census counts only live, non-dormant walkers on score teams: has_guy
    --   livings as roster (with power), guyless livings as npcs, generators as generators; dormant
    --   (delayed-spawn) walkers, the dead and wildlife are invisible.
    -- tests: StagedRules.dormant_troops_are_uncensused_like_the_staged_report
    -- tests: StagedRules.onslaught_domain_and_generator_fills
    -- tests: SemLineup.the_staging_census_counts_heroes_npcs_and_generators_on_score_teams_only
    if w:dead() == 0 and not w:dormant() then
      local team = w:team_num()
      -- rule: The staging census counts only live, non-dormant walkers on score teams. Here: `team
      --   < SCORE_TEAM_COUNT`.
      -- tests: StagedRules.dormant_troops_are_uncensused_like_the_staged_report
      -- tests: StagedRules.onslaught_domain_and_generator_fills
      -- tests: SemLineup.the_staging_census_counts_heroes_npcs_and_generators_on_score_teams_only
      if team < C.SCORE_TEAM_COUNT then
        local trow = inputs.teams[team + 1]
        local order = w:order()
        -- rule: The staging census counts only live, non-dormant walkers on score teams. Here:
        --   `order==LIVING`.
        -- tests: StagedRules.dormant_troops_are_uncensused_like_the_staged_report
        -- tests: StagedRules.onslaught_domain_and_generator_fills
        -- tests: SemLineup.the_staging_census_counts_heroes_npcs_and_generators_on_score_teams_only
        if order == C.ORDER_LIVING then
          -- rule: The staging census counts only live, non-dormant walkers on score teams. Here:
          --   `has_guy`.
          -- tests: StagedRules.dormant_troops_are_uncensused_like_the_staged_report
          -- tests: StagedRules.onslaught_domain_and_generator_fills
          -- tests: SemLineup.the_staging_census_counts_heroes_npcs_and_generators_on_score_teams_only
          if w:has_guy() then
            trow.roster = trow.roster + 1
            trow.power = trow.power + walker_power(w)
          else
            trow.npcs = trow.npcs + 1
          end
        elseif order == C.ORDER_GENERATOR then
          trow.generators = trow.generators + 1
        end
      end
    end
  end
  return inputs
end

-- The L1 ring cell at index i (0 .. 4*r-1) of tile radius r, clockwise
-- from due north — the deterministic ring-walk idiom shared with the
-- ball games' landing-legality re-spot and the classic stage placer.
local function ring_offset(i, r)
  local edge = og.div(i, r)
  local k = og.mod(i, r)
  -- rule: Ring cell i of tile radius r walks the L1 ring clockwise from due north (N->E->S->W
  --   edges).
  -- tests: SemLineup.a_ring_walks_clockwise_from_due_north
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
  -- rule: Respawn/fill placement rotates through the team's anchors from the mode-var cursor
  --   (advancing it for every probe) and takes the first clear one; walkers off the score range get
  --   no anchor.
  -- tests: ModesTdm.dead_hero_revives_at_team_anchor_with_cursor_rotation
  -- tests: SemLineup.a_team_off_the_score_range_gets_no_anchor
  if team >= 0 then
    -- rule: Respawn/fill placement rotates through the team's anchors from the mode-var cursor
    --   (advancing it for every probe) and takes the first clear one. Here: `team <
    --   SCORE_TEAM_COUNT`.
    -- tests: ModesTdm.dead_hero_revives_at_team_anchor_with_cursor_rotation
    -- tests: SemLineup.a_team_off_the_score_range_gets_no_anchor
    if team < C.SCORE_TEAM_COUNT then
      local count = og.respawn_anchor_count(team)
      -- plumbing: rotation and ring-walk loops; rules above and below
      for _ = 1, count do
        local cursor = og.mode_get(cursor_slot)
        og.mode_set(cursor_slot, cursor + 1)
        local ax, ay = og.respawn_anchor(team, og.mod(cursor, count))
        -- rule: Respawn/fill placement rotates through the team's anchors from the mode-var cursor
        --   (advancing it for every probe) and takes the first clear one. Here:
        --   `spawn_spot_clear(anchor)`.
        -- tests: ModesTdm.dead_hero_revives_at_team_anchor_with_cursor_rotation
        -- tests: SemLineup.a_team_off_the_score_range_gets_no_anchor
        if og.spawn_spot_clear(w, ax, ay) then
          final_x = ax
          final_y = ay
          break
        end
      end
      -- rule: When every anchor is camped, placement walks rings of radius 1..3 tiles around the
      --   anchors (radius-major, anchor order, cell order) and takes the first clear in-map tile,
      --   RNG-free, so a camped spawn line never stalls a respawn.
      -- tests: ModesSoccer.camped_anchors_cannot_stall_a_respawn
      -- unreachable: query_grid_passable refuses x<0 or y<0 first
      --   (src/gameplay/game_world.cpp), so the px>=0/py>=0 guard is redundant and no test can
      --   tell it from its removal; candidate for a defensive-guard row
      if final_x < 0 then
        -- Ring fallback, RNG-free and probe-eats-safe like the rotation
        -- above. Rings cap at 3 tiles: past that the caller's own
        -- fallback (engine revive-in-place, or the blocked-fire retry
        -- cadence) is the honest answer.
        -- plumbing: rotation and ring-walk loops; rules above
        for r = 1, 3 do
          -- plumbing: rotation and ring-walk loops; rules above
          for a = 0, count - 1 do
            local ax, ay = og.respawn_anchor(team, a)
            -- plumbing: rotation and ring-walk loops; rules above
            for i = 0, 4 * r - 1 do
              -- rule: When every anchor is camped, placement walks rings of radius 1..3 tiles
              --   around the anchors (radius-major, anchor order, cell order) and takes the first
              --   clear in-map tile, RNG-free, so a camped spawn line never stalls a respawn.
              -- tests: ModesSoccer.camped_anchors_cannot_stall_a_respawn
              if final_x < 0 then
                local dx, dy = ring_offset(i, r)
                local px = ax + dx * 16
                local py = ay + dy * 16
                -- rule: When every anchor is camped, placement walks rings of radius 1..3 tiles
                --   around the anchors (radius-major, anchor order, cell order) and takes the first
                --   clear in-map tile, RNG-free, so a camped spawn line never stalls a respawn.
                -- tests: ModesSoccer.camped_anchors_cannot_stall_a_respawn
                if px >= 0 and py >= 0 then
                  -- rule: When every anchor is camped, placement walks rings of radius 1..3 tiles
                  --   around the anchors (radius-major, anchor order, cell order) and takes the
                  --   first clear in-map tile, RNG-free, so a camped spawn line never stalls a
                  --   respawn.
                  -- tests: ModesSoccer.camped_anchors_cannot_stall_a_respawn
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
  -- rule: When no anchor or ring tile is clear, an init-time placement falls back to the random
  --   teleport draw; a respawn placement (no teleport) reports failure so the caller's own fallback
  --   runs.
  -- tests: SemLineup.a_walled_in_anchor_fails_a_respawn_and_teleports_an_init_placement
  if allow_teleport then
    return w:teleport()
  end
  return false
end

-- The families a team's squad actually fields (amendment B2): the first
-- `count` entries of the mode's own stock table, the whole table when the
-- count covers it. Pure -- squad_shape above owns every rule about what
-- the count IS (the D34/D39 headcount prefix and the #305 bodies alike),
-- so neither this prefix nor its callers can grow an arithmetic of their
-- own. NONE never reaches here: spawn_bots returns first.
local function squad_prefix(families, count)
  -- rule: A squad fields the first count families of its table (the whole table when count covers
  --   it), soldier-first.
  -- tests: StagedRules.tdm_matched_fill_truncates_to_the_headcount
  -- tests: ModesTdm.matched_size_latch_truncates_every_spawn_arm
  -- tests: ModesTdm.matched_spawn_bots_legacy_arm_is_unchanged
  if count >= #families then
    return families
  end
  local prefix = {}
  -- plumbing: squad_prefix copy loop
  for k = 1, count do
    prefix[k] = families[k]
  end
  return prefix
end

-- The FILL solve target (amendment B3), nil where no solve governs: an
-- occupied team fields ALLIES, so it targets the gap — the strongest
-- other team's human f-sum minus its own, scaled by the wheel — and a
-- gap at or below zero is NO SQUAD (nil; the old B(1) clamp is retired
-- for allies). An empty team targets the weakest human team's f-sum
-- times the wheel. No human power anywhere = nil = the legacy formula.
-- count/base is squad_shape's body ratio (#305): a squad standing in for
-- a smaller baseline asks for proportionally more total power, so each
-- body lands on the baseline's own per-fighter figure. It is 1 in every
-- shape that came before this rule, and the formula then reduces to
-- today's div(P * pct, 100) exactly (the floor is invariant under a
-- common positive factor), so there is no fast-path twin.
local function fill_target(reference, sums, team, pct, count, base)
  -- rule: The FILL solve target: no human power -> no solve (legacy); a full hard shape (base 0) ->
  --   no squad; an empty team targets reference * pct * count/base; an occupied team fields ALLIES
  --   aimed at the strongest other team's sum minus its own, scaled, and a gap at or below zero
  --   fields nobody.
  -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
  -- tests: ModesTdm.lineup_spawn_seam_allies_no_gap_spawns_nothing
  -- tests: StagedRules.basketball_allies_fill_the_room_left
  if reference <= 0 then
    return nil
  end
  -- A full hard shape leaves no body to solve for, and no baseline to
  -- divide by: the same "spawns nothing, banks nothing" an empty squad
  -- produced before (review R4).
  if base <= 0 then
    return nil
  end
  if sums[team + 1] <= 0 then
    return og.div(reference * pct * count, 100 * base)
  end
  local best = 0
  -- plumbing: fill_target scan over other teams; rule above
  for t = 1, C.SCORE_TEAM_COUNT do
    -- rule: The FILL solve target: no human power -> no solve (legacy). Here: `t ~= team`.
    -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
    -- tests: ModesTdm.lineup_spawn_seam_allies_no_gap_spawns_nothing
    -- tests: StagedRules.basketball_allies_fill_the_room_left
    if t ~= team + 1 then
      -- rule: The FILL solve target: no human power -> no solve (legacy). Here: `sums[t] > best`.
      -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
      -- tests: ModesTdm.lineup_spawn_seam_allies_no_gap_spawns_nothing
      -- tests: StagedRules.basketball_allies_fill_the_room_left
      if sums[t] > best then
        best = sums[t]
      end
    end
  end
  local target = og.div((best - sums[team + 1]) * pct * count,
                        100 * base)
  -- rule: The FILL solve target: no human power -> no solve (legacy). Here: `target <= 0`.
  -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
  -- tests: ModesTdm.lineup_spawn_seam_allies_no_gap_spawns_nothing
  -- tests: StagedRules.basketball_allies_fill_the_room_left
  if target <= 0 then
    return nil
  end
  return target
end

-- Squad bots carry stats bit 65536 (the next free bit above the modes'
-- generator spawn mark) so the staged VIEW LEVEL census can tell BOT
-- SQUAD / MATCHED BOTS from authored map troops on any client — bit_flags
-- rides the entity snapshot, so provenance is networked-exact on every
-- mirror. Set at the one squad choke point (add_squad_member below: init
-- squads, wiped-team revives AND the classic stage). The C++ reader pins
-- the value (picker_common kBotMarkBit); mode_caps re-exports both names.
local BOT_MARK_BIT = 65536

local function mark_bot(w)
  w:s_set_bit_flags(BOT_MARK_BIT, 1)
end

local function add_squad_member(team, family_name)
  local fam = og.family_id("living", family_name) --[[@as integer]] -- caller tables name core families; a nil would be a load bug and add_ob erroring loudly is the right failure
  local w = og.add_ob("living", fam)
  -- plumbing: add_ob nil (world full): member skipped
  if w == nil then
    return nil
  end
  w:set_team_num(team)
  w:set_real_team_num(255)
  mark_bot(w)
  return w
end

local function place_member(w, team, cursor_slot, placer)
  -- rule: A mode with its own placer (CTF's anchor rotation with flag-home fallback) places squad
  --   members its way; otherwise the shared anchor rotation with teleport fallback.
  -- tests: ModesCtf.blocked_anchor_bot_fill_falls_back_to_flag_home
  -- tests: SemLineup.a_squad_with_no_placer_stands_on_the_anchors_in_member_order
  if placer ~= nil then
    placer(w, team, true)
    return
  end
  place_at_anchor(w, team, cursor_slot, true)
end

-- The D24 measure-and-solve arm: spawn the real squad, read the spawn-time
-- stats back as the model bases (no guessed constants ship — D13), solve
-- against the target, persist the plan, then level each member
-- exactly once (s_set_level BEFORE set_difficulty, both once per walker
-- object — D14). Placement stays in member order, before the leveling
-- pass; neither placement nor the probe reads levels.
-- announce is the modes campaign's one-shot TEAMS MATCHED seam (the R3
-- latch lives with the announce, mode-side): called with the solve's
-- clamped flag after a squad actually spawned. nil — the classic stage,
-- which announces nothing — skips it. Returns the number of members
-- actually spawned.
local function spawn_matched_bots(team, families, cursor_slot, placer,
                                  target, announce)
  local members = {}
  local bases = {}
  -- plumbing: spawn_matched_bots member spawn and leveling loops
  for k = 1, #families do
    local w = add_squad_member(team, families[k])
    -- plumbing: nil member skipped; no member spawned returns 0 (feeds the bank-only-if-spawned
    --   rule below)
    if w ~= nil then
      members[#members + 1] = w
      bases[#bases + 1] = { family = families[k], stats = measured_base(w) }
      place_member(w, team, cursor_slot, placer)
    end
  end
  -- plumbing: nil member skipped; no member spawned returns 0 (feeds the bank-only-if-spawned rule
  --   below)
  if #members == 0 then
    return 0
  end
  local level, up, clamped = solve_matched_levels(target, bases)
  store_plan(team, level, up)
  -- plumbing: spawn_matched_bots member spawn and leveling loops
  for k = 1, #members do
    local w = members[k]
    local member_level = bot_level_for(team, k)
    w:s_set_level(member_level)
    w:set_difficulty(member_level)
  end
  -- rule: A solved squad announces TEAMS MATCHED (or LIMIT on a clamp) only through the mode's
  --   announce seam; the classic stage and wiped-team backstops solve silently.
  -- tests: ModesTdm.matched_spawn_bots_measures_solves_and_announces
  -- tests: ModesTdm.matched_spawn_bots_low_clamp_announces_the_limit
  -- tests: ModesTdm.matched_backstop_solve_stores_plan_but_stays_silent
  if announce ~= nil then
    announce(clamped)
  end
  return #members
end

-- The live fielded headcount on a team — the spawn seam's half of
-- squad_room, mirroring the fills rows' two occupied arms exactly (in
-- the staged world the two walks count the same walkers): the has_guy
-- roster where one stands (the company/allies arm), else the standing
-- guy-less livings (the troops arm, D3's mode twin — a hard shape
-- prices only the room the fielded map units leave). The strips run
-- before every squad spawn, so a retired unit is dead here and counts
-- for nothing.
local function live_fielded_on(obs, team)
  local roster = 0
  local units = 0
  -- plumbing: live_fielded_on oblist scan; rule below
  for k = 1, #obs do
    local w = obs[k]
    -- rule: A team's fielded headcount for the hard-shape room is its live roster heroes when any
    --   stand, else its live guyless map units.
    -- tests: StagedRules.basketball_allies_fill_the_room_left
    -- tests: StagedRules.basketball_caps_the_squad_beside_the_troops
    -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
    if w:dead() == 0 then
      -- rule: A team's fielded headcount for the hard-shape room is its live roster heroes when any
      --   stand, else its live guyless map units. Here: `LIVING`.
      -- tests: StagedRules.basketball_allies_fill_the_room_left
      -- tests: StagedRules.basketball_caps_the_squad_beside_the_troops
      -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
      if w:order() == C.ORDER_LIVING then
        -- rule: A team's fielded headcount for the hard-shape room is its live roster heroes when
        --   any stand, else its live guyless map units. Here: `team == team`.
        -- tests: StagedRules.basketball_allies_fill_the_room_left
        -- tests: StagedRules.basketball_caps_the_squad_beside_the_troops
        -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
        if w:team_num() == team then
          -- rule: A team's fielded headcount for the hard-shape room is its live roster heroes when
          --   any stand, else its live guyless map units. Here: `has_guy`.
          -- tests: StagedRules.basketball_allies_fill_the_room_left
          -- tests: StagedRules.basketball_caps_the_squad_beside_the_troops
          -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
          if w:has_guy() then
            roster = roster + 1
          else
            units = units + 1
          end
        end
      end
    end
  end
  -- rule: A team's fielded headcount for the hard-shape room is its live roster heroes when any
  --   stand, else its live guyless map units. Here: `roster > 0`.
  -- tests: StagedRules.basketball_allies_fill_the_room_left
  -- tests: StagedRules.basketball_caps_the_squad_beside_the_troops
  -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
  if roster > 0 then
    return roster
  end
  return units
end

-- Fields one bot per family for a team's FILL squad, over the
-- SIZE-truncated prefix of the caller's table (D34/D39 — every caller
-- keeps its signature; the headcount rule lives inside this one seam, so
-- init fills, the wiped-team backstops, the mutant seats and the classic
-- stage all honor it identically). The lineup knobs (B2-B4) are consulted
-- HERE, the one squad seam, so every caller obeys them alike:
-- NONE fields nothing; every other wheel value is the matched solver
-- with that value's multiplier (FILL_PERCENT, the one table). Level
-- source (one rule for every scripted spawn): a stored plan wins; else
-- the solve against fill_target over a fresh census — the weakest human
-- team's f-sum times the wheel for an empty team, the gap for an
-- occupied one (allies; a gap at or below zero fields NOBODY) — and the
-- solved plan is stored so every respawn reproduces it; else (no human
-- power anywhere) the legacy session-difficulty formula, which stores no
-- plan. s_set_level + set_difficulty exactly once per walker, the D14
-- discipline. The applied fill code is banked (bank_lineup_facts) only
-- when at least one member spawned (review R4).
-- placer is an optional placement override, placer(w, team,
-- allow_teleport) (matched-teams D16): CTF passes its own anchor
-- rotation, which falls back to the flag-home square before the teleport
-- draw; nil keeps the rotation over cursor_slot. shape is the caller's
-- squad shape, nil or { cap = n|nil, bodies = bool }: cap is the hard
-- shape (basketball's 5v5) and bodies arms the #305 body rule on the
-- empty-team arm (squad_shape above). announce is the modes' solved-
-- squad signal, handed through to spawn_matched_bots (nil = silent).
-- The knob is read HERE, raw (E1: the stored code IS the fill — 0 is
-- NONE and fields nothing, no resolution intervenes), so every caller
-- obeys the wheel through one read.
local function spawn_bots(team, families, cursor_slot, placer, shape,
                          announce)
  local knob = fill_knob(team)
  -- rule: A FILL wheel at NONE (the default) or any code off the wheel fields no squad on that
  --   team, from every caller.
  -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
  -- tests: StagedRules.fill_none_removes_the_squad_fair_fields
  if squad_off(knob) then
    return
  end
  local obs = og.oblist()
  local fielded = live_fielded_on(obs, team)
  local cap = nil
  -- plumbing: shape nil guard reading the hard cap
  if shape ~= nil then
    cap = shape.cap
  end
  -- rule: The #305 body rule is armed only when the mode's shape says bodies (the ball games);
  --   every other caller keeps power-step squads (see above).
  -- tests: StagedRules.staged_world_strong_fields_h_plus_one_on_ball_modes
  -- tests: StagedRules.squad_shape_agrees_with_mode_shape
  local bodies_allowed = shape ~= nil and shape.bodies == true
  local count, pct, base = squad_shape(knob, og.mode_get(MATCHED.SIZE),
                                       squad_room(cap, fielded),
                                       #families, fielded == 0,
                                       bodies_allowed)
  local squad = squad_prefix(families, count)
  -- rule: A stored plan wins (every respawn reproduces it); else with human power the squad is
  --   measured and solved and the plan stored; with no human power anywhere the legacy
  --   session-difficulty levels apply and no plan is stored.
  -- tests: ModesTdm.matched_spawn_bots_planned_arm_applies_stored_plan
  -- tests: ModesTdm.matched_spawn_bots_measures_solves_and_announces
  -- tests: ModesTdm.matched_spawn_bots_legacy_arm_is_unchanged
  -- tests: ClassicLineupTest.no_humans_takes_the_legacy_formula_and_no_plan
  if plan_code(team) == 0 then
    local reference, sums = census_power(obs)
    -- rule: A stored plan wins (every respawn reproduces it). Here: `reference > 0`.
    -- tests: ModesTdm.matched_spawn_bots_planned_arm_applies_stored_plan
    -- tests: ModesTdm.matched_spawn_bots_measures_solves_and_announces
    -- tests: ModesTdm.matched_spawn_bots_legacy_arm_is_unchanged
    -- tests: ClassicLineupTest.no_humans_takes_the_legacy_formula_and_no_plan
    if reference > 0 then
      local target = fill_target(reference, sums, team, pct, count,
                                 base)
      -- rule: Allies with no power gap to close (or a full hard shape) spawn no squad at all and
      --   bank nothing.
      -- tests: ModesTdm.lineup_spawn_seam_allies_no_gap_spawns_nothing
      if target == nil then
        -- Allies with no gap to close (B3): no squad at all.
        return
      end
      target = og.min(target, TARGET_CAP)
      -- Banked only for a squad that actually spawned (review R4): a
      -- full hard shape spawns nothing and banks nothing.
      -- rule: A team's applied fill code is banked only when at least one member actually spawned
      --   (solved or legacy arm).
      -- tests: StagedRules.basketball_allies_fill_the_room_left
      -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
      if spawn_matched_bots(team, squad, cursor_slot, placer, target,
                            announce) > 0
      then
        bank_lineup_facts(team, knob)
      end
      return
    end
  end
  local spawned = 0
  -- plumbing: legacy-arm member spawn loop
  for k = 1, #squad do
    local w = add_squad_member(team, squad[k])
    -- plumbing: add_ob nil member skipped
    if w ~= nil then
      local level = bot_level_for(team, k)
      w:s_set_level(level)
      w:set_difficulty(level)
      place_member(w, team, cursor_slot, placer)
      spawned = spawned + 1
    end
  end
  -- rule: A team's applied fill code is banked only when at least one member actually spawned
  --   (solved or legacy arm). Here: `spawned > 0`.
  -- tests: StagedRules.basketball_allies_fill_the_room_left
  -- tests: ModesTdm.lineup_fill_target_percent_table_and_knobbed_spawns
  if spawned > 0 then
    bank_lineup_facts(team, knob)
  end
end

-- ---------------------------------------------------------------------------
-- The per-team MAP UNITS strip (amendment B4)
-- ---------------------------------------------------------------------------

-- True for the entity kinds the strip removes. Everything else authored on
-- the map (treasure, flags, waypoints, markers, fx) is untouched.
-- Generators follow the same box as the livings (amendment B4) —
-- Onslaught's foundries included: unchecking a team's MAP UNITS box in
-- Onslaught removes that side's board, and the fills rows drop the team
-- with it.
local function is_troop(order)
  -- rule: The MAP UNITS strip removes authored livings and generators only (Onslaught foundries
  --   included); treasure, flags, markers and fx stay.
  -- tests: ModesStrip.is_troop_classifies_orders_with_no_generator_exception
  if order == C.ORDER_LIVING then
    return true
  end
  return order == C.ORDER_GENERATOR
end

-- Retire one walker. The corpse moves off the score-team range BEFORE it is
-- marked dead so a same-tick death scan cannot see it as a score-team
-- casualty and respawn it. Retiring marks dead, NEVER erases: the lazy
-- lineup-stage arm's completion guard (game_world.cpp) relies on the strip
-- only ever shrinking the live population, not the oblist itself.
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
  -- plumbing: strip scans; rules below
  for k = 1, #obs do
    local w = obs[k]
    -- rule: With a team's MAP UNITS box off, every live authored living/generator without a roster
    --   guy on that score team is retired (moved off the score range, then marked dead, never
    --   erased); heroes and wildlife always stand.
    -- tests: ModesStrip.one_box_off_strips_exactly_that_team
    -- tests: ModesStrip.all_boxes_off_strip_every_score_team_but_never_wildlife
    -- tests: ModesStrip.stripped_entities_leave_the_score_team_range
    if w:dead() == 0 then
      -- rule: With a team's MAP UNITS box off, every live authored living/generator without a
      --   roster guy on that score team is retired (moved off the score range, then marked dead,
      --   never erased). Here: `is_troop`.
      -- tests: ModesStrip.one_box_off_strips_exactly_that_team
      -- tests: ModesStrip.all_boxes_off_strip_every_score_team_but_never_wildlife
      -- tests: ModesStrip.stripped_entities_leave_the_score_team_range
      if is_troop(w:order()) then
        -- rule: With a team's MAP UNITS box off, every live authored living/generator without a
        --   roster guy on that score team is retired (moved off the score range, then marked dead,
        --   never erased). Here: `not has_guy`.
        -- tests: ModesStrip.one_box_off_strips_exactly_that_team
        -- tests: ModesStrip.all_boxes_off_strip_every_score_team_but_never_wildlife
        -- tests: ModesStrip.stripped_entities_leave_the_score_team_range
        if not w:has_guy() then
          local team = w:team_num()
          -- rule: With a team's MAP UNITS box off, every live authored living/generator without a
          --   roster guy on that score team is retired (moved off the score range, then marked
          --   dead, never erased). Here: `team < SCORE_TEAM_COUNT`.
          -- tests: ModesStrip.one_box_off_strips_exactly_that_team
          -- tests: ModesStrip.all_boxes_off_strip_every_score_team_but_never_wildlife
          -- tests: ModesStrip.stripped_entities_leave_the_score_team_range
          if team < C.SCORE_TEAM_COUNT then
            -- rule: With a team's MAP UNITS box off, every live authored living/generator without a
            --   roster guy on that score team is retired (moved off the score range, then marked
            --   dead, never erased). Here: `not fielded[team]`.
            -- tests: ModesStrip.one_box_off_strips_exactly_that_team
            -- tests: ModesStrip.all_boxes_off_strip_every_score_team_but_never_wildlife
            -- tests: ModesStrip.stripped_entities_leave_the_score_team_range
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

-- The strip entry point: every mode impl calls it from on_mode_init,
-- immediately after its inactive-team strip and BEFORE its empty-team
-- census (so bot backfill sees the post-strip world), and the classic
-- lineup stage calls it before its fill pass. Reads the four MAP UNITS
-- boxes through the one knob seam above. Returns how many entities were
-- retired; all boxes on returns 0 without touching the world.
local function strip_authored_troops()
  local fielded = {}
  local any_off = false
  -- plumbing: strip scans; rules above and below
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    fielded[team + 1] = map_units_fielded(team)
    -- rule: With every MAP UNITS box on the strip touches nothing; any non-zero (junk) box value
    --   reads OFF.
    -- tests: ModesStrip.boxes_on_leave_every_authored_entity_alone
    -- tests: ModesStrip.junk_box_values_read_as_off
    if not fielded[team + 1] then
      any_off = true
    end
  end
  -- rule: With every MAP UNITS box on the strip touches nothing; any non-zero (junk) box value
  --   reads OFF. Here: `not any_off`.
  -- tests: ModesStrip.boxes_on_leave_every_authored_entity_alone
  -- tests: ModesStrip.junk_box_values_read_as_off
  if not any_off then
    return 0
  end
  return strip_team_units(og.oblist(), fielded)
end

return {
  iabs = iabs,
  MATCHED = MATCHED,
  FILL_NONE = FILL_NONE,
  FILL_WEAK = FILL_WEAK,
  FILL_FAIR = FILL_FAIR,
  FILL_STRONG = FILL_STRONG,
  FILL_BRUTAL = FILL_BRUTAL,
  FILL_PERCENT = FILL_PERCENT,
  MAP_UNITS_ON = MAP_UNITS_ON,
  BOT_SQUAD = BOT_SQUAD,
  BOT_MARK_BIT = BOT_MARK_BIT,
  fill_knob = fill_knob,
  map_units_knob = map_units_knob,
  squad_off = squad_off,
  fill_percent = fill_percent,
  map_units_fielded = map_units_fielded,
  squad_room = squad_room,
  squad_shape = squad_shape,
  squad_prefix = squad_prefix,
  fill_target = fill_target,
  stat_power = stat_power,
  measured_base = measured_base,
  walker_power = walker_power,
  predicted_power = predicted_power,
  census_power = census_power,
  solve_matched_levels = solve_matched_levels,
  difficulty_level = difficulty_level,
  bot_level_for = bot_level_for,
  bank_lineup_facts = bank_lineup_facts,
  bank_match_target = bank_match_target,
  census_inputs = census_inputs,
  ring_offset = ring_offset,
  place_at_anchor = place_at_anchor,
  mark_bot = mark_bot,
  spawn_bots = spawn_bots,
  is_troop = is_troop,
  retire = retire,
  strip_authored_troops = strip_authored_troops,
}
