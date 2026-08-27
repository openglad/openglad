-- core lib: lineup — the match machinery every campaign shares (docs/lineup-design.md C1): the f power metric and census, the difficulty-tuple model, the D22 matched solver and plan, the FILL wheel and MAP UNITS box reads, squad sizing/placement/spawn, the per-team strip and the applied-fact banking. Mode-specific rules (masks, refusals, hard-shape caps, band modes, announces) stay in the modes campaign, which consumes THIS module via og.use("core:lineup") so no rule lives twice (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

-- Absolute value. The sandbox has no math library; the solver's argmin and
-- the modes' geometry share this one spelling (mode_core re-exports it).
local function iabs(v)
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
-- kFillDefault / kFillNone / kFillWeak / kFillFair / kFillStrong /
-- kFillBrutal — amendment B2 as renumbered by D1): FILL is the matched
-- solver with a multiplier, and the engine stores and clamps the CODE
-- alone — this table is the only copy of what each code is worth, keyed
-- by the raw value og.match_setting("fill_N") answers. 0 = the stored
-- DEFAULT (never a multiplier of its own: it resolves per team through
-- resolved_fill below, so it deliberately has no FILL_PERCENT row), then
-- the five explicit wheel codes in wheel order: 1 = NONE (no squad on
-- this team, ever), 2 = WEAK (×0.75), 3 = FAIR (×1), 4 = STRONG (×1.25),
-- 5 = BRUTAL (×1.5). D1 gave FAIR its own code: while it shared 0 with
-- the default, no player could choose FAIR on a team whose default
-- resolved NONE.
local FILL_DEFAULT = 0
local FILL_NONE = 1
local FILL_WEAK = 2
local FILL_FAIR = 3
local FILL_STRONG = 4
local FILL_BRUTAL = 5
local FILL_PERCENT = { [1] = 0, [2] = 75, [3] = 100, [4] = 125, [5] = 150 }

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

-- Does a knob forbid a squad outright? NONE alone does (amendment B8: no
-- other wheel value refuses anything). The band path (mode_fighters
-- band_knob) asks the same question of team 1's knob.
local function squad_off(knob)
  return knob == FILL_NONE
end

-- The knob's multiplier in integer percent. The engine clamps the knob to
-- [0, kMaxFill]; a value this table does not carry (a crafted world var)
-- degrades to FAIR rather than erroring.
local function fill_percent(knob)
  return FILL_PERCENT[knob] or 100
end

-- The fill a knob APPLIES: a code off the wheel degrades to FAIR, so the
-- banked fact (and the byte image of the staged world with it) is
-- identical to the FAIR stage a junk value behaves as. Since D1 the
-- stored DEFAULT (0) is off the wheel too: a 0 that reaches an apply
-- seam unresolved is a crafted value, and it degrades like any other.
local function applied_fill(knob)
  if FILL_PERCENT[knob] == nil then
    return FILL_FAIR
  end
  return knob
end

-- Are team t's map-shipped units fielded? The box read the per-team strip
-- (strip_authored_troops below) applies — non-zero reads as OFF, so a
-- crafted value can never field units the host's box hid.
local function map_units_fielded(team)
  return map_units_knob(team) == MAP_UNITS_ON
end

-- The C8 presence fold: does this team have anything the resolved default
-- may stand on? `row` carries the team's censused counts — authored units
-- (livings and generators alike), start markers, engine respawn anchors,
-- deployed roster fighters and lobby seats. A key the caller's census does
-- not gather is simply absent and reads 0, so the classic stage (which has
-- no seat visibility — at any launch GO can pass, a seat implies a
-- deployed fighter on its team, the M4 refusal) and the C++ band query
-- (which adds the seat axis) ask the ONE question through one spelling.
local function team_present(row)
  if (row.units or 0) > 0 then
    return true
  end
  if (row.generators or 0) > 0 then
    return true
  end
  if (row.markers or 0) > 0 then
    return true
  end
  if (row.anchors or 0) > 0 then
    return true
  end
  if (row.roster or 0) > 0 then
    return true
  end
  return (row.seats or 0) > 0
end

-- THE resolver (docs/lineup-design.md C8, renumbered by D1), the one home
-- of the rule every surface renders: the stored DEFAULT (FILL_DEFAULT, 0)
-- resolves per team — the EXPLICIT FAIR (3) where the team has any
-- presence, the explicit NONE (1) where it has none — so the page never
-- advertises a fill the placement rule would refuse anyway, and the
-- answer is always a wheel code (the default itself is not an answer).
-- An explicit wheel value is stored as itself and returned unchanged; a
-- junk code included: the degrade-to-FAIR rule (applied_fill) predates
-- the resolution and stays an EXPLICIT fair, because a crafted value is
-- a value, not the default.
local function resolved_fill(knob, row)
  if knob ~= FILL_DEFAULT then
    return knob
  end
  if team_present(row) then
    return FILL_FAIR
  end
  return FILL_NONE
end

-- The room a hard shape leaves beside a team's occupants (lineup review
-- L2): a squad riding an occupied team is sized to cap minus that team's
-- roster, never below zero — three humans on basketball's five-a-side
-- court get two allies, five get none. No hard shape (nil) = no bound.
-- One rule for fills' count (over the census roster) and the spawn seam
-- (over the live has_guy count, the same number in the staged world).
local function squad_room(cap, roster)
  if cap == nil then
    return nil
  end
  return og.max(cap - roster, 0)
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
  local reference = 0
  for t = 1, C.SCORE_TEAM_COUNT do
    if sums[t] > 0 then
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
      local miss = iabs(p - target)
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

-- The applied lineup facts, banked for the preview label (amendment B7).
-- The mode-private band is spent to the last slot in three of the five
-- mask modes, so the facts CO-TENANT the shared MATCHED.ANNOUNCED slot:
-- its ones digit stays the announce latch (0/1/2), and the digits above
-- it pack one base-100 code per team (PLAN's packing, shifted one
-- decimal digit up). code = 0 when no squad of this team's fielded
-- anybody (the pane names no fill word), else THE APPLIED FILL CODE
-- ITSELF — 1 = NONE (banked only where a stored default RESOLVED to it:
-- the explicit NONE fields nothing and stays unbanked), 2 = WEAK,
-- 3 = FAIR, 4 = STRONG, 5 = BRUTAL. The old +1 bias is retired with D1:
-- no explicit code is 0 any more, so 0 is unambiguously "this team
-- banked nothing" without a shift. A team whose stored default resolved
-- banks the code it resolved TO, never the stored 0. APPLIED, not
-- requested: a squad that spawned nothing banks nothing (review R4).
-- C++ twin: picker_common.cpp kModeVarLineupFacts (lineup_fact_code /
-- lineup_fact_fill — the bias constant is deleted on that side too).
local function lineup_code(team)
  return og.mod(og.div(og.mode_get(MATCHED.ANNOUNCED),
                       10 * PLAN_BASE[team + 1]), 100)
end

local function bank_lineup_facts(team, fill)
  local code = fill
  local held = lineup_code(team)
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
    if w:dead() == 0 and not w:dormant() then
      local team = w:team_num()
      if team < C.SCORE_TEAM_COUNT then
        local trow = inputs.teams[team + 1]
        local order = w:order()
        if order == C.ORDER_LIVING then
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

-- The families a team's squad actually fields (amendment B2): the mode's
-- own stock table through the headcount rule, then the caller's hard
-- shape. NONE never reaches here — spawn_bots returns first.
local function squad_families(families, cap)
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

-- The FILL solve target (amendment B3), nil where no solve governs: an
-- occupied team fields ALLIES, so it targets the gap — the strongest
-- other team's human f-sum minus its own, scaled by the wheel — and a
-- gap at or below zero is NO SQUAD (nil; the old B(1) clamp is retired
-- for allies). An empty team targets the weakest human team's f-sum
-- times the wheel. No human power anywhere = nil = the legacy formula.
local function fill_target(reference, sums, team, pct)
  if reference <= 0 then
    return nil
  end
  if sums[team + 1] <= 0 then
    return og.div(reference * pct, 100)
  end
  local best = 0
  for t = 1, C.SCORE_TEAM_COUNT do
    if t ~= team + 1 then
      if sums[t] > best then
        best = sums[t]
      end
    end
  end
  local target = og.div((best - sums[team + 1]) * pct, 100)
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
  if w == nil then
    return nil
  end
  w:set_team_num(team)
  w:set_real_team_num(255)
  mark_bot(w)
  return w
end

local function place_member(w, team, cursor_slot, placer)
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
  for k = 1, #families do
    local w = add_squad_member(team, families[k])
    if w ~= nil then
      members[#members + 1] = w
      bases[#bases + 1] = { family = families[k], stats = measured_base(w) }
      place_member(w, team, cursor_slot, placer)
    end
  end
  if #members == 0 then
    return 0
  end
  local level, up, clamped = solve_matched_levels(target, bases)
  store_plan(team, level, up)
  for k = 1, #members do
    local w = members[k]
    local member_level = bot_level_for(team, k)
    w:s_set_level(member_level)
    w:set_difficulty(member_level)
  end
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
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        if w:team_num() == team then
          if w:has_guy() then
            roster = roster + 1
          else
            units = units + 1
          end
        end
      end
    end
  end
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
-- draw; nil keeps the rotation over cursor_slot. cap is the
-- caller's hard shape (basketball's 5v5). announce is the modes' solved-
-- squad signal, handed through to spawn_matched_bots (nil = silent).
-- resolved is the caller's ALREADY-RESOLVED wheel code where the caller
-- ran the C8 resolution itself (the classic stage — the one caller with
-- site-less, presence-less teams); nil makes this seam resolve the raw
-- knob itself, so a stored default can never reach the multiplier table
-- unresolved (D1: the seam reads the resolved value, never the raw 0).
-- The fallback's presence row is the caller's own decision: this seam
-- is only ever invoked for a team its deciding fold (the mode fills
-- rows, the wiped-team backstops — every one gated on the mode's active
-- mask) already ruled fields a squad, and an active mode team is
-- authored by the mode's own domain, so the implied row is an authored
-- one and the default resolves the explicit FAIR — the pre-D1 reading
-- of a stored 0 at this seam, now spelled through the ONE resolver.
local function spawn_bots(team, families, cursor_slot, placer, cap, announce,
                          resolved)
  local knob = resolved
  if knob == nil then
    knob = resolved_fill(fill_knob(team), { units = 1 })
  end
  if squad_off(knob) then
    return
  end
  local obs = og.oblist()
  local squad = squad_families(families,
                               squad_room(cap, live_fielded_on(obs, team)))
  if plan_code(team) == 0 then
    local reference, sums = census_power(obs)
    if reference > 0 then
      local target = fill_target(reference, sums, team, fill_percent(knob))
      if target == nil then
        -- Allies with no gap to close (B3): no squad at all.
        return
      end
      target = og.min(target, TARGET_CAP)
      -- Banked only for a squad that actually spawned (review R4): a
      -- full hard shape spawns nothing and banks nothing.
      if spawn_matched_bots(team, squad, cursor_slot, placer, target,
                            announce) > 0
      then
        bank_lineup_facts(team, applied_fill(knob))
      end
      return
    end
  end
  local spawned = 0
  for k = 1, #squad do
    local w = add_squad_member(team, squad[k])
    if w ~= nil then
      local level = bot_level_for(team, k)
      w:s_set_level(level)
      w:set_difficulty(level)
      place_member(w, team, cursor_slot, placer)
      spawned = spawned + 1
    end
  end
  if spawned > 0 then
    bank_lineup_facts(team, applied_fill(knob))
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

-- The strip entry point: every mode impl calls it from on_mode_init,
-- immediately after its inactive-team strip and BEFORE its empty-team
-- census (so bot backfill sees the post-strip world), and the classic
-- lineup stage calls it before its fill pass. Reads the four MAP UNITS
-- boxes through the one knob seam above. Returns how many entities were
-- retired; all boxes on returns 0 without touching the world.
local function strip_authored_troops()
  local fielded = {}
  local any_off = false
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    fielded[team + 1] = map_units_fielded(team)
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
  iabs = iabs,
  MATCHED = MATCHED,
  FILL_DEFAULT = FILL_DEFAULT,
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
  applied_fill = applied_fill,
  map_units_fielded = map_units_fielded,
  team_present = team_present,
  resolved_fill = resolved_fill,
  squad_room = squad_room,
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
