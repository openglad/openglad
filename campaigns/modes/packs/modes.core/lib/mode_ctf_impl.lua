-- CTF rules + AI director — the shipped C++ engine (ctf.cpp / ctf_ai.cpp) transcribed onto mode vars: init/strip/bot-squads, the flag state machine, control points, win check, role partition (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Deliberate deviations from the C++ are ONLY the blessed set (modes.md
-- §4.9 D1-D6 + DECISIONS D1/D2/D10): post-act hook position, absolute-tick
-- cadence, bot-placement RNG order, the kept 1-indexed SCORES line, the CP
-- respawn-speedup applied at schedule time, anchor placement in on_respawn
-- with a mode-var cursor (no teleport fallback there), and the engine win
-- latch (in-place flush revive) behind og.declare_winner.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local match = og.use("mode_match")
local strip = og.use("mode_strip")
local levels = og.use("mode_levels")
local items = og.use("mode_items")

-- Mode-private slot map (8-63; header 0-7 is mode_core.SLOT). The item
-- slots take the last two frees: CTF's var budget is now FULL.
-- Flag state is DERIVED, exactly the C++ enum: Carried while a carrier id
-- is banked, Dropped while the return countdown runs, else AtHome.
local S = {
  CAPTURE_LIMIT = 8,
  RESPAWN_TICKS = 9,
  TEAM_COUNT = 10,
  TEAM_MASK = 11,
  CP_COUNT = 12,
  ANCHOR_CURSOR = 13, -- the D1 rotation cursor (replaces respawn_serial)
  CAPTURES = 14, -- 14..17, +team
  FLAG_ENTITY = 18, -- 18..21, 0 = team authors no flag
  FLAG_CARRIER = 22, -- 22..25, carrier entity id, 0 = not carried
  FLAG_RETURN = 26, -- 26..29, dropped-return countdown, 0 = not dropped
  FLAG_HOME = 30, -- 30..33, packed (x, y)
  FLAG_POS = 34, -- 34..37, packed current (x, y), == carrier pos while carried
  CP_ENTITY = 38, -- 38..41
  CP_OWNER1 = 42, -- 42..45, owner team + 1, 0 = neutral
  CP_PROGRESS = 46, -- 46..49
  CP_PROG_TEAM1 = 50, -- 50..53, contending team + 1, 0 = none
  CP_PULSE_AT = 54, -- 54..57, absolute world tick of the next pulse
  CP_POS = 58, -- 58..61, packed (x, y)
  ITEM_CURSOR = 62, -- mode_items pad rotation cursor
  ITEM_LAST = 63, -- mode_items last-spawn tick (seeded at init)
}

-- Tuning. Values are LAW (shipped C++ behavior, ctf_state.h).
local T = {
  capture_score = 400,
  cp_score = 50,
  cp_capture_ticks = 36,
  cp_radius_px = 48, -- radius_tiles(3) * GRID_SIZE
  cp_pulse_period = 300,
  cp_pulse_radius = 96,
  cp_speed_ticks = 60,
  flag_return_ticks = 360,
  respawn_ticks = 120,
  time_limit_ticks = 14400,
  capture_limit = 3,
  ai_cadence = 15,
  defend_radius = 96,
  -- Respawning pickups (lib/mode_items; NOT ctf_state.h law): fallback
  -- interval when the manifest row carries none — 25 s sustains both
  -- offense and defense over the 20-min cap.
  item_interval = 300,
}

local BOT_SQUAD = { "core:soldier", "core:archer", "core:elf", "core:mage", "core:thief" }

-- Per-team / per-CP slot access.
local function fget(base, i)
  return og.mode_get(base + i)
end

local function fset(base, i, v)
  og.mode_set(base + i, v)
end

local function active_mask()
  return og.mode_get(S.TEAM_MASK)
end

local function ctf_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.CTF
end

-- ---------------------------------------------------------------------------
-- Flag state
-- ---------------------------------------------------------------------------

local function flag_present(team)
  return fget(S.FLAG_ENTITY, team) ~= 0
end

local function flag_carried(team)
  return fget(S.FLAG_CARRIER, team) ~= 0
end

local function flag_dropped(team)
  if flag_carried(team) then
    return false
  end
  return fget(S.FLAG_RETURN, team) ~= 0
end

local function flag_at_home(team)
  if flag_carried(team) then
    return false
  end
  return fget(S.FLAG_RETURN, team) == 0
end

-- The flag entity keeps its obmap registration for the whole match (only
-- walker::death removes entries, and flags never die post-init), so setxy
-- alone keeps the index current — the C++ ensure_obmap_registration call
-- after placement was pure invariant enforcement here.
local function send_flag_home(team)
  local home = fget(S.FLAG_HOME, team)
  fset(S.FLAG_CARRIER, team, 0)
  fset(S.FLAG_RETURN, team, 0)
  fset(S.FLAG_POS, team, home)
  local fe = og.find_by_id(fget(S.FLAG_ENTITY, team))
  if fe ~= nil then
    fe:set_ignore(0)
    fe:setxy(core.pos_x(home), core.pos_y(home))
  end
end

-- Drops a flag at an explicit point: the carrier's feet for the classic
-- drops, the departure point for the teleport rule. carrier (non-nil)
-- supplies the footprint for the terrain probe.
local function drop_flag_at(team, carrier, drop_x, drop_y)
  -- Stranding rule: a drop tile that fails the terrain probe (water, wall)
  -- returns the flag home instantly instead of dropping it out of reach.
  if not og.query_grid_passable(drop_x, drop_y, carrier) then
    send_flag_home(team)
    core.announce(og.team_color_name(team) .. " FLAG RETURNED!", C.SOUND_TELEPORT)
    return
  end
  fset(S.FLAG_CARRIER, team, 0)
  fset(S.FLAG_RETURN, team, T.flag_return_ticks)
  fset(S.FLAG_POS, team, core.pos_pack(drop_x, drop_y))
  local fe = og.find_by_id(fget(S.FLAG_ENTITY, team))
  if fe ~= nil then
    fe:set_ignore(0)
    fe:setxy(drop_x, drop_y)
  end
  core.announce(og.team_color_name(team) .. " FLAG DROPPED!", C.SOUND_YO)
end

local function drop_flag(team, carrier)
  -- No corpse to validate a drop tile against: send the flag home.
  if carrier == nil then
    send_flag_home(team)
    core.announce(og.team_color_name(team) .. " FLAG RETURNED!", C.SOUND_TELEPORT)
    return
  end
  drop_flag_at(team, carrier, carrier:xpos(), carrier:ypos())
end

-- Carried flags ride their carrier; invalid carriers (gone, dead, or
-- charm-flipped onto the flag's own team) drop the flag. A carrier that
-- SELF-teleported this tick leaves every carried flag behind at the
-- departure point (the banked FLAG_POS still holds the pre-blink spot);
-- map teleporter pads never stamp the marker, so pad rides carry through.
local function run_carried_flags()
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if flag_present(team) then
      if flag_carried(team) then
        local carrier = og.find_by_id(fget(S.FLAG_CARRIER, team))
        if carrier == nil then
          drop_flag(team, carrier)
        elseif carrier:dead() ~= 0 or carrier:team_num() == team then
          drop_flag(team, carrier)
        elseif carrier:last_self_teleport_tick() == og.world_tick() then
          local pos = fget(S.FLAG_POS, team)
          drop_flag_at(team, carrier, core.pos_x(pos), core.pos_y(pos))
        else
          fset(S.FLAG_POS, team, core.pos_pack(carrier:xpos(), carrier:ypos()))
          local fe = og.find_by_id(fget(S.FLAG_ENTITY, team))
          if fe ~= nil then
            fe:setxy(carrier:xpos(), carrier:ypos() - 8)
          end
        end
      end
    end
  end
end

-- Dropped flags count down and auto-return home.
local function run_dropped_flags()
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if flag_present(team) then
      if flag_dropped(team) then
        local left = fget(S.FLAG_RETURN, team)
        if left > 0 then
          left = left - 1
          fset(S.FLAG_RETURN, team, left)
        end
        if left == 0 then
          send_flag_home(team)
          core.announce(og.team_color_name(team) .. " FLAG RETURNED!", C.SOUND_TELEPORT)
        end
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- Respawn (engine queue; Lua owns eligibility and placement)
-- ---------------------------------------------------------------------------

local function team_owns_cp(team)
  local cp_count = og.mode_get(S.CP_COUNT)
  for i = 0, cp_count - 1 do
    if fget(S.CP_OWNER1, i) - 1 == team then
      return true
    end
  end
  return false
end

-- CP ownership speeds the owner's respawns. Blessed simplification
-- (DECISIONS D10): the delay halves at schedule time instead of the C++
-- 2-per-tick countdown burn — no mid-countdown rate change.
local function schedule_ctf_respawn(w)
  local team = w:real_team_num()
  if team == 255 then
    team = w:team_num()
  end
  local ticks = og.mode_get(S.RESPAWN_TICKS)
  if team_owns_cp(team) then
    ticks = og.max(1, og.div(ticks, 2))
  end
  og.respawn_schedule(w, ticks)
end

-- The pre-sweep corpse scan (run_death_scan): player corpses persist and
-- retry every tick, AI corpses are seen exactly once. Walkers with a live
-- owner (summons, generator spawns) are the generator's business — the
-- orphan-adoption quirk (owner nulled on the owner's death) is preserved.
local function run_death_scan(obs)
  local mask = active_mask()
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() ~= 0 then
      if w:order() == C.ORDER_LIVING then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          if core.mask_has(mask, team) then
            local owned = false
            if not w:has_guy() then
              owned = w:owner() ~= nil
            end
            if not owned then
              if not og.respawn_pending(w) then
                schedule_ctf_respawn(w)
              end
            end
          end
        end
      end
    end
  end
end

-- Anchor rotation (DECISIONS D1): the mode-var cursor + eat-free probes,
-- the flag home as fallback. Respawn placement is deterministic and
-- zero-RNG — a fully blocked walker stays at its engine revive spot.
-- Init-time bot placement passes allow_teleport (the ONE RNG draw the CTF
-- Lua may make, modes.md §4.9-D3, matching the C++ place_at_anchor
-- fallback chain).
local function place_at_anchor(w, team, allow_teleport)
  local final_x = -1
  local final_y = 0
  if team >= 0 then
    if team < C.SCORE_TEAM_COUNT then
      local count = og.respawn_anchor_count(team)
      for _ = 1, count do
        local cursor = og.mode_get(S.ANCHOR_CURSOR)
        og.mode_set(S.ANCHOR_CURSOR, cursor + 1)
        local ax, ay = og.respawn_anchor(team, og.mod(cursor, count))
        if og.spawn_spot_clear(w, ax, ay) then
          final_x = ax
          final_y = ay
          break
        end
      end
      if final_x < 0 then
        if flag_present(team) then
          local home = fget(S.FLAG_HOME, team)
          local hx = core.pos_x(home)
          local hy = core.pos_y(home)
          if og.spawn_spot_clear(w, hx, hy) then
            final_x = hx
            final_y = hy
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

-- Engine handoff: the timer fire revived the walker in place (players) or
-- respawned the recorded body (AI) — this hook applies the CTF anchor
-- placement on top.
local function on_respawn(ent)
  if not ctf_active() then
    return
  end
  place_at_anchor(ent, ent:team_num(), false)
end

-- ---------------------------------------------------------------------------
-- Control points
-- ---------------------------------------------------------------------------

-- Majority occupancy accumulates progress for the dominant team, owner
-- dominance decays it symmetrically, and owned points pulse a localized
-- speed bonus to nearby owner-team livings. livings is this tick's cached
-- live-Living census (no phase between the census and here changes it).
local function run_control_points(livings)
  local mask = active_mask()
  local cp_count = og.mode_get(S.CP_COUNT)
  for i = 0, cp_count - 1 do
    local pos = fget(S.CP_POS, i)
    local cx = core.pos_x(pos)
    local cy = core.pos_y(pos)

    local present = { 0, 0, 0, 0 }
    local total = 0
    for k = 1, #livings do
      local w = livings[k]
      local team = w:team_num()
      if team < C.SCORE_TEAM_COUNT then
        if core.mask_has(mask, team) then
          local dx = w:xpos() - cx
          local dy = w:ypos() - cy
          if dx * dx + dy * dy <= T.cp_radius_px * T.cp_radius_px then
            present[team + 1] = present[team + 1] + 1
            total = total + 1
          end
        end
      end
    end

    -- Majority-occupancy contender: the strongest team contests only with
    -- a strict majority of ALL live walkers in the disc; ties and balanced
    -- brawls hold the meter.
    local strongest = -1
    local strongest_count = 0
    for team = 0, C.SCORE_TEAM_COUNT - 1 do
      if present[team + 1] > strongest_count then
        strongest_count = present[team + 1]
        strongest = team
      end
    end
    local contender = -1
    if strongest >= 0 then
      if strongest_count * 2 > total then
        contender = strongest
      end
    end

    local owner = fget(S.CP_OWNER1, i) - 1
    if contender >= 0 and contender ~= owner then
      if fget(S.CP_PROG_TEAM1, i) ~= contender + 1 then
        fset(S.CP_PROG_TEAM1, i, contender + 1)
        fset(S.CP_PROGRESS, i, 0)
      end
      local progress = fget(S.CP_PROGRESS, i) + 1
      fset(S.CP_PROGRESS, i, progress)
      if progress >= T.cp_capture_ticks then
        fset(S.CP_OWNER1, i, contender + 1)
        fset(S.CP_PROGRESS, i, 0)
        fset(S.CP_PROG_TEAM1, i, 0)
        local ce = og.find_by_id(fget(S.CP_ENTITY, i))
        if ce ~= nil then
          ce:set_team_num(contender)
        end
        og.award_score(contender, T.cp_score)
        core.announce(og.team_color_name(contender) .. " TAKES WAYPOINT!", C.SOUND_MONEY)
      end
    elseif contender >= 0 then
      -- Owner dominance decays accrued progress one step per tick; the
      -- meter clears its contending team only when it drains to zero.
      local progress = fget(S.CP_PROGRESS, i)
      if progress > 0 then
        progress = progress - 1
        fset(S.CP_PROGRESS, i, progress)
        if progress == 0 then
          fset(S.CP_PROG_TEAM1, i, 0)
        end
      end
    end
    -- Contested (no strict majority) or empty: hold the meter.

    local owner_now = fget(S.CP_OWNER1, i) - 1
    if owner_now >= 0 then
      if og.world_tick() >= fget(S.CP_PULSE_AT, i) then
        for k = 1, #livings do
          local w = livings[k]
          if w:team_num() == owner_now then
            local dx = w:xpos() - cx
            local dy = w:ypos() - cy
            if dx * dx + dy * dy <= T.cp_pulse_radius * T.cp_pulse_radius then
              -- speed bonus is a C++ float: 1.0 is exact.
              w:set_speed_bonus(1.0)
              w:set_speed_bonus_left(T.cp_speed_ticks)
            end
          end
        end
        fset(S.CP_PULSE_AT, i, og.world_tick() + T.cp_pulse_period)
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- Win check
-- ---------------------------------------------------------------------------

-- Capture limit first; the time limit picks the leader (captures, then
-- m_score, then smaller team index). The engine latch behind
-- og.declare_winner revives pending corpses before winner_is_player.
-- The clock resolves HERE, from the manifest row, rather than being banked
-- at init the way every other mode does it: CTF's mode-var band is full
-- (ITEM_LAST = 63), so there is no slot to bank it in. The rule is still
-- the shared one -- only the storage differs -- and both the row and the
-- world's requested limit are replicated, so peers agree tick for tick.
local function run_win_check(level_tick, row)
  local mask = active_mask()
  local limit = og.mode_get(S.CAPTURE_LIMIT)
  local winner = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if winner < 0 then
      if core.mask_has(mask, team) then
        if fget(S.CAPTURES, team) >= limit then
          winner = team
        end
      end
    end
  end
  if winner < 0 then
    if level_tick >= match.resolve_time_limit(row, T.time_limit_ticks) then
      for team = 0, C.SCORE_TEAM_COUNT - 1 do
        if core.mask_has(mask, team) then
          if winner < 0 then
            winner = team
          elseif fget(S.CAPTURES, team) > fget(S.CAPTURES, winner) then
            winner = team
          elseif fget(S.CAPTURES, team) == fget(S.CAPTURES, winner) then
            if og.team_score(team) > og.team_score(winner) then
              winner = team
            end
          end
        end
      end
    end
  end
  if winner >= 0 then
    core.declare_team_win(winner)
  end
end

-- ---------------------------------------------------------------------------
-- HUD / observability
-- ---------------------------------------------------------------------------

-- Per active team: HUD row = the capture score line, beacon = the enemy
-- carrying this team's flag (cleared while it is home or on the ground).
local function update_hud()
  local mask = active_mask()
  local limit = og.mode_get(S.CAPTURE_LIMIT)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      core.hud_score_line(team, team, fget(S.CAPTURES, team), limit)
      if flag_carried(team) then
        og.set_beacon(team, og.find_by_id(fget(S.FLAG_CARRIER, team)), team)
      else
        og.set_beacon(team, nil)
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- AI director (ctf_ai.cpp transcription; zero RNG, oblist order)
-- ---------------------------------------------------------------------------

local function leader_is_flag_entity(leader)
  local id = og.entity_id(leader)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local fid = fget(S.FLAG_ENTITY, team)
    if fid ~= 0 then
      if fid == id then
        return true
      end
    end
  end
  return false
end

local function carries_enemy_flag(team, wid)
  for t = 0, C.SCORE_TEAM_COUNT - 1 do
    if t ~= team then
      if flag_present(t) then
        if fget(S.FLAG_CARRIER, t) == wid then
          return true
        end
      end
    end
  end
  return false
end

-- Role partition per team, in oblist order:
--   CARRIER      holds an enemy flag: leader = own flag entity (suppresses
--                the tick auto-foe backstop), COMMAND_GOTO own flag home.
--   INTERCEPTOR  own flag carried: the 2 nearest unassigned chase the
--                enemy carrier (set_foe + COMMAND_SEARCH).
--   RETRIEVER    own flag dropped: the nearest unassigned runs to it.
--   DEFENDER     first ceil(remaining/3): GOTO home when beyond the leash.
--   ATTACKER     the rest: GOTO the nearest unsecured enemy flag's current
--                position, under the preemption discipline.
--   ESCORT       every 2nd would-be attacker while the team has a carrier:
--                leader = lead carrier, COMMAND_FOLLOW.
local function run_team_director(livings, team)
  local members = {}
  local team_live = 0
  for k = 1, #livings do
    if livings[k]:team_num() == team then
      team_live = team_live + 1
    end
    if ai.is_directable(livings[k], team) then
      members[#members + 1] = livings[k]
    end
  end
  if #members == 0 then
    return
  end

  local own_present = flag_present(team)
  local own_home = fget(S.FLAG_HOME, team)
  local home_x = core.pos_x(own_home)
  local home_y = core.pos_y(own_home)

  local assigned = {}
  for i = 1, #members do
    assigned[i] = false
  end

  -- CARRIERS: run the held flags home.
  local lead_carrier = nil
  for i = 1, #members do
    if carries_enemy_flag(team, og.entity_id(members[i])) then
      assigned[i] = true
      if lead_carrier == nil then
        lead_carrier = members[i]
      end
      if own_present then
        local fe = og.find_by_id(fget(S.FLAG_ENTITY, team))
        if fe ~= nil then
          members[i]:set_leader(fe)
        end
        ai.issue_front(members[i], C.COMMAND_GOTO, 60, home_x, home_y)
      end
    end
  end

  -- INTERCEPTORS: own flag stolen — the two nearest unassigned members
  -- chase the enemy carrier through the classic foe-search machinery.
  if own_present then
    if flag_carried(team) then
      local enemy_carrier = og.find_by_id(fget(S.FLAG_CARRIER, team))
      if enemy_carrier ~= nil then
        if enemy_carrier:dead() == 0 then
          for _ = 1, 2 do
            local best = ai.nearest_unassigned(members, assigned, enemy_carrier:xpos(), enemy_carrier:ypos())
            if best ~= nil then
              assigned[best] = true
              local interceptor = members[best]
              ai.clear_stale_leader(interceptor, leader_is_flag_entity)
              interceptor:set_foe(enemy_carrier)
              ai.issue_front(interceptor, C.COMMAND_SEARCH, 120, 0, 0)
            end
          end
        end
      end
    end
  end

  -- RETRIEVER: own flag on the ground — the nearest unassigned member
  -- runs to it (a friendly touch returns it home instantly).
  if own_present then
    if flag_dropped(team) then
      local pos = fget(S.FLAG_POS, team)
      local drop_x = core.pos_x(pos)
      local drop_y = core.pos_y(pos)
      local best = ai.nearest_unassigned(members, assigned, drop_x, drop_y)
      if best ~= nil then
        assigned[best] = true
        local retriever = members[best]
        ai.clear_stale_leader(retriever, leader_is_flag_entity)
        ai.issue_front(retriever, C.COMMAND_GOTO, 45, drop_x, drop_y)
      end
    end
  end

  local remaining = {}
  for i = 1, #members do
    if not assigned[i] then
      remaining[#remaining + 1] = members[i]
    end
  end

  -- DEFENDERS: the first ceil(remaining/3) members hold the flag home,
  -- leashed back whenever they stray past the defend radius. R1:
  -- og.div(n + 2, 3) is the C ceiling. A SOLE free member attacks
  -- instead (matched-teams D38, general to every whittled squad): the
  -- ceiling pinned a permanent lone defender, so a 1v1 could never
  -- produce a director-driven capture — and the reactive arms above
  -- (carrier, interceptor, retriever) already supply defense at n = 1.
  local defender_count = og.div(#remaining + 2, 3)
  -- Desperation applies only when the sole free member IS the whole
  -- team: with a teammate carrying the enemy flag the free member
  -- guards the bank spot the carrier needs (banking requires our flag
  -- home), and with a live human teammate the bot holds home while the
  -- human fights on (is_directable excludes players, so #remaining
  -- undercounts the team — both review findings on the first cut).
  if #remaining == 1 and lead_carrier == nil then
    if team_live <= #members then
      defender_count = 0
    end
  end
  for i = 1, defender_count do
    local defender = remaining[i]
    ai.clear_stale_leader(defender, leader_is_flag_entity)
    if own_present then
      if ai.dist_to(defender, home_x, home_y) > T.defend_radius then
        ai.issue_front(defender, C.COMMAND_GOTO, 45, home_x, home_y)
      end
    end
  end

  -- ATTACKERS and ESCORTS: while the team has a carrier, every 2nd
  -- would-be attacker escorts it instead. Attackers head for the nearest
  -- unsecured enemy flag's current position — chasing a flag someone else
  -- stole converges on its carrier.
  local attacker_index = 0
  for i = defender_count + 1, #remaining do
    local attacker = remaining[i]
    local escort_slot = false
    if lead_carrier ~= nil then
      escort_slot = og.mod(attacker_index, 2) == 1
    end
    attacker_index = attacker_index + 1

    if escort_slot then
      attacker:set_foe(nil)
      attacker:set_leader(lead_carrier)
      ai.issue_front(attacker, C.COMMAND_FOLLOW, 100, 0, 0)
    else
      ai.clear_stale_leader(attacker, leader_is_flag_entity)
      if ai.may_preempt(attacker:s_front_command()) then
        local target_team = -1
        local target_distance = 0
        for t = 0, C.SCORE_TEAM_COUNT - 1 do
          if t ~= team then
            if flag_present(t) then
              -- A flag secured by the own team (a teammate carries it) is
              -- not an attack target.
              local secured = false
              if flag_carried(t) then
                local flag_carrier = og.find_by_id(fget(S.FLAG_CARRIER, t))
                if flag_carrier ~= nil then
                  secured = flag_carrier:team_num() == team
                end
              end
              if not secured then
                local pos = fget(S.FLAG_POS, t)
                local distance = ai.dist_to(attacker, core.pos_x(pos), core.pos_y(pos))
                if target_team < 0 then
                  target_team = t
                  target_distance = distance
                elseif distance < target_distance then
                  target_team = t
                  target_distance = distance
                end
              end
            end
          end
        end
        if target_team >= 0 then
          local pos = fget(S.FLAG_POS, target_team)
          ai.issue_front(attacker, C.COMMAND_GOTO, 60, core.pos_x(pos), core.pos_y(pos))
        end
      end
    end
  end
end

local function run_director(livings)
  local mask = active_mask()
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      run_team_director(livings, team)
    end
  end
end

-- ---------------------------------------------------------------------------
-- Init (transcribes ctf_initialize_for_level)
-- ---------------------------------------------------------------------------

-- The decide fold (#218 staged lobby — the old on_mode_plan body,
-- verbatim, now a local consumed by on_mode_init directly): the
-- first-flag-per-team fold over the raw flag rows — out-of-range and
-- surplus rows decide nothing (the apply scan below performs those
-- kills), and the first KEPT flag with a stats level above 1 sets the
-- per-map capture limit. Pure over the census inputs; only the inputs
-- decide.
local function decide(level, inputs)
  local authored_mask = 0
  local map_capture_limit = 0
  for k = 1, #inputs.flags do
    local flag = inputs.flags[k]
    if flag.team < C.SCORE_TEAM_COUNT then
      if not core.mask_has(authored_mask, flag.team) then
        authored_mask = core.mask_add(authored_mask, flag.team)
        if map_capture_limit == 0 then
          if flag.level > 1 then
            map_capture_limit = flag.level
          end
        end
      end
    end
  end
  -- Under TROOPS:OWN/FAIR the lobby request wins the COUNT — roster flag
  -- teams plus authored backfill (issue #218), TEAMS: Auto = the authored
  -- flag-team count (2026-08-18 directive); under TROOPS:ALL the raw
  -- requested count over the authored flag teams (no manifest default —
  -- the verified per-mode Auto asymmetry).
  local mask, starts, matched, matched_size =
      match.activation(inputs, authored_mask, 0)
  -- Deliberately NOT match.resolve_limit: the middle term here is the
  -- MAP's own flag count (derived from the board that was just read), not
  -- a manifest field, so there is no row[field] for the shared ladder to
  -- look up. Same order, different second reader.
  local limit = T.capture_limit
  if inputs.score_limit > 0 then
    limit = inputs.score_limit
  elseif map_capture_limit > 0 then
    limit = map_capture_limit
  end
  local teams = match.fills(inputs, mask, {
    matched = matched,
    matched_size = matched_size,
  })
  local reason = nil
  if not starts then
    reason = "ctf: fewer than two flag teams"
  end
  return {
    mode = "CTF",
    starts = starts,
    reason = reason,
    authored_mask = authored_mask,
    active_mask = mask,
    matched = matched,
    matched_size = matched_size,
    score_limit = og.clamp(limit, 1, 255),
    seeded_squads = false,
    teams = teams,
  }
end

-- Init (runs once, at staging). Raising reports the failed-init shape
-- (fewer than two flag teams): the engine latches inactive and classic
-- rules own the level — the surplus-flag kills already made stand,
-- exactly like the C++ early return.
local function on_mode_init(level)
  -- The census + the decide fold FIRST, over the pre-kill world (the fold
  -- and the kill scan implement the same first-flag-per-team rule over
  -- the same fx order, so they agree by construction).
  local inputs = match.census_inputs()
  local decision = decide(level, inputs)
  -- Scan authored flags and control points (fxlist, list order) — pure
  -- EXECUTION of the decision's fold: the same first-flag-per-team rule
  -- kills the surplus and banks FLAG_ENTITY/HOME/POS; the domain and the
  -- capture limit were already decided in the fold over the same rows.
  -- The shipped maps author wire bytes 13/14, which this pack's families
  -- claim outright since the core CTF retirement.
  local flag_family = og.family_id("treasure", "modes:flag")
  local point_family = og.family_id("treasure", "modes:waypoint")
  local authored_mask = 0
  local cp_count = 0
  local fxlist = og.fxlist()
  for k = 1, #fxlist do
    local e = fxlist[k]
    if e:dead() == 0 then
      if e:order() == C.ORDER_TREASURE then
        local fam = e:family()
        local is_flag = fam == flag_family
        local is_point = fam == point_family
        if is_flag then
          local team = e:team_num()
          if team >= C.SCORE_TEAM_COUNT then
            e:set_dead(1)
          elseif core.mask_has(authored_mask, team) then
            e:set_dead(1) -- one flag per team
          else
            authored_mask = core.mask_add(authored_mask, team)
            fset(S.FLAG_ENTITY, team, og.entity_id(e))
            local home = core.pos_pack(e:xpos(), e:ypos())
            fset(S.FLAG_HOME, team, home)
            fset(S.FLAG_POS, team, home)
          end
        elseif is_point then
          if cp_count >= 4 then
            e:set_dead(1)
          else
            fset(S.CP_ENTITY, cp_count, og.entity_id(e))
            fset(S.CP_POS, cp_count, core.pos_pack(e:xpos(), e:ypos()))
            cp_count = cp_count + 1
          end
        end
      end
    end
  end
  og.mode_set(S.CP_COUNT, cp_count)

  -- The fewer-than-two refusal is raised HERE, after the surplus-flag
  -- kills above, so a failed init leaves them standing exactly like the
  -- C++ early return. Activation is the decide fold's; anchors were
  -- scanned engine-side before this hook (dead markers included).
  if not decision.starts then
    error(decision.reason)
  end
  local obs = og.oblist()
  -- FAIR banking (the fold decided matched-ness and the headcount; the
  -- power TARGET is measured here, D24) before any squad spawn reads it.
  match.bank_match_target(decision, obs)
  local mask = decision.active_mask
  og.mode_set(S.TEAM_MASK, mask)
  og.mode_set(S.TEAM_COUNT, core.mask_count(mask))

  -- Consume the active teams' start markers (their anchor positions are
  -- already recorded) so the marker entities cannot block their own
  -- respawn anchors.
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

  -- Strip inactive-team entities: surplus flags, livings, generators and
  -- markers. Player roster walkers (myguy) are never stripped. (The C++
  -- also zeroed the stripped teams' anchor counts; nothing reads an
  -- inactive team's anchors here, so the read-only surface suffices.)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(authored_mask, team) then
      if not core.mask_has(mask, team) then
        local fe = og.find_by_id(fget(S.FLAG_ENTITY, team))
        if fe ~= nil then
          fe:set_dead(1)
        end
        fset(S.FLAG_ENTITY, team, 0)
        fset(S.FLAG_HOME, team, 0)
        fset(S.FLAG_POS, team, 0)
      end
    end
  end
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
        local keep = false
        if team < C.SCORE_TEAM_COUNT then
          keep = core.mask_has(mask, team)
        end
        if not keep then
          if not w:has_guy() then
            w:set_dead(1)
          end
        end
      end
    end
  end

  -- Roster-only armies on request (the shared rule set — lib/mode_strip).
  -- Runs before the bot-squad census below so backfill sees the post-strip
  -- world; CTF fields no generators, so it keeps none.
  strip.strip_authored_troops(nil)

  -- The capture limit is the decision's (explicit request > per-map flag
  -- level > default, clamped); the respawn delay resolves here.
  og.mode_set(S.CAPTURE_LIMIT, decision.score_limit)
  local respawn_ticks = og.match_setting("respawn_ticks")
  if respawn_ticks <= 0 then
    respawn_ticks = T.respawn_ticks
  end
  og.mode_set(S.RESPAWN_TICKS, respawn_ticks)

  -- Bot squads where the decision said so (the empty active teams).
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local fill = decision.teams[team + 1].fill
    if fill == "bots" or fill == "matched" then
      -- The one shared squad spawner (matched-teams D16), with CTF's own
      -- placer so blocked anchors still fall back to the flag-home
      -- square before the teleport draw.
      match.spawn_bots(team, BOT_SQUAD, S.ANCHOR_CURSOR, place_at_anchor)
    end
  end

  og.set_mode_name("CTF")
  og.mode_set(S.ITEM_LAST, og.world_tick())
  og.mode_set(core.SLOT.PHASE, 1)
  -- The activation latch (ctf.active): written LAST, so the family hooks
  -- observe a fully initialized match or nothing.
  og.mode_set(core.SLOT.MODE_ID, core.MODE.CTF)
  core.announce("CAPTURE THE FLAG! TO " .. og.mode_get(S.CAPTURE_LIMIT), C.SOUND_CHARGE)
end

-- ---------------------------------------------------------------------------
-- Per-tick phases (post-act; engine already ran the respawn timers and
-- owns the win-latch short-circuit)
-- ---------------------------------------------------------------------------

local function on_mode_tick(level, tick)
  local obs = og.oblist()
  run_death_scan(obs)
  local livings = {}
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        livings[#livings + 1] = w
      end
    end
  end
  run_carried_flags()
  run_dropped_flags()
  run_control_points(livings)
  if og.mod(og.world_tick(), T.ai_cadence) == 0 then
    run_director(livings)
  end
  items.run(levels.levels[level], S.ITEM_CURSOR, S.ITEM_LAST, T.item_interval)
  run_win_check(tick, levels.levels[level])
  update_hud()
end

-- ---------------------------------------------------------------------------
-- Flag touch (modes:flag on_eat — the C++ ctf_on_flag_touch rules)
-- ---------------------------------------------------------------------------

local function flag_on_eat(self, eater)
  if not ctf_active() then
    return true
  end
  if self:dead() ~= 0 then
    return true
  end
  if eater:dead() ~= 0 then
    return true
  end
  if eater:order() ~= C.ORDER_LIVING then
    return true
  end
  -- A flag touch fired while the eater is mid-self-teleport is not a real
  -- touch: the blink's destination probe dispatches eat_me while the eater
  -- still stands at the departure point, so honoring it would bank a
  -- capture or relocate a flag across the blink.
  if eater:last_self_teleport_tick() == og.world_tick() then
    return true
  end
  local eater_team = eater:team_num()
  if eater_team >= C.SCORE_TEAM_COUNT then
    return true
  end
  if not core.mask_has(active_mask(), eater_team) then
    return true
  end

  local flag_team = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if flag_present(team) then
      if fget(S.FLAG_ENTITY, team) == og.entity_id(self) then
        flag_team = team
      end
    end
  end
  if flag_team < 0 then
    return true
  end

  if eater_team == flag_team then
    if flag_dropped(flag_team) then
      send_flag_home(flag_team)
      core.announce(og.team_color_name(flag_team) .. " FLAG RETURNED!", C.SOUND_TELEPORT)
    elseif flag_at_home(flag_team) then
      -- Touching the own flag at home banks every carried enemy flag.
      local captured = 0
      for team = 0, C.SCORE_TEAM_COUNT - 1 do
        if team ~= flag_team then
          if flag_present(team) then
            if fget(S.FLAG_CARRIER, team) == og.entity_id(eater) then
              send_flag_home(team)
              captured = captured + 1
            end
          end
        end
      end
      if captured > 0 then
        local captures = fget(S.CAPTURES, flag_team) + captured
        fset(S.CAPTURES, flag_team, captures)
        og.award_score(flag_team, captured * T.capture_score)
        -- The 1-indexed team number is a KEPT inconsistency (behavior
        -- freeze, modes.md §4.9-D4).
        core.announce("TEAM " .. (flag_team + 1) .. " SCORES! " .. captures .. "/" .. og.mode_get(S.CAPTURE_LIMIT), C.SOUND_MONEY)
      end
    end
  elseif not flag_carried(flag_team) then
    -- Enemy touch on a home or dropped flag: pick it up. Announce only
    -- when the flag leaves home — regrab scrums over a dropped flag would
    -- otherwise ping-pong TAKEN lines every touch.
    local taken_from_home = flag_at_home(flag_team)
    fset(S.FLAG_CARRIER, flag_team, og.entity_id(eater))
    fset(S.FLAG_RETURN, flag_team, 0)
    fset(S.FLAG_POS, flag_team, core.pos_pack(eater:xpos(), eater:ypos()))
    self:set_ignore(1)
    self:setxy(eater:xpos(), eater:ypos() - 8)
    if taken_from_home then
      core.announce(og.team_color_name(flag_team) .. " FLAG TAKEN!", C.SOUND_YO)
    end
  end
  return true
end

-- Control points are occupancy-driven (run_control_points); touching one
-- decides nothing. The dead-code no-op is preserved (modes.md §4.9-D5).
local function waypoint_on_eat(self, eater)
  return true
end

return {
  S = S,
  T = T,
  on_mode_init = on_mode_init,
  on_mode_tick = on_mode_tick,
  on_respawn = on_respawn,
  flag_on_eat = flag_on_eat,
  waypoint_on_eat = waypoint_on_eat,
}
