-- Team Deathmatch rules + AI director — frag scoring over the killer channel, timeout ladder with the lowest-byte tiebreak, generator spawn caps via fire_frequency toggling, hunt-nearest steering (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Rule spec: modes.md §3 as amended by DECISIONS D6/D9/D16 and the Phase
-- IIb contract — score_limit/time_limit come from the manifest (20/7200),
-- frags award 1 m_score point each, the director runs at cadence 15, and
-- the timeout tiebreak is: leading KILLS, ties to the LOWEST team byte.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local caps = og.use("mode_caps")
local match = og.use("mode_match")
local levels = og.use("mode_levels")
local strip = og.use("mode_strip")

-- Mode-private slot map (header 0-7 is mode_core.SLOT).
local S = {
  TEAM_MASK = 8,
  SCORE_LIMIT = 9,
  TIME_LIMIT = 10,
  RESPAWN_TICKS = 11,
  ANCHOR_CURSOR = 12,
  KILLS = 13, -- 13..16, +team (teamkills decrement; may go negative)
}

-- Tuning (manifest score_limit/time_limit override the defaults; the
-- match_setting score request overrides the manifest).
local T = {
  score_limit = 20,
  time_limit_ticks = 7200,
  kill_score = 1, -- m_score per frag (og.award_score)
  respawn_ticks = 120,
  ai_cadence = 15,
  focus_margin = 5,
  -- A paused generator carries authored fire_frequency + this offset, so
  -- the authored value is always recoverable from the entity itself (R6:
  -- no Lua state between ticks).
  gen_pause_offset = 30000,
  bot_squad = { "core:soldier", "core:archer", "core:elf", "core:mage", "core:thief" },
}

local function tdm_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.TDM
end

local function active_mask()
  return og.mode_get(S.TEAM_MASK)
end

local function kills_of(team)
  return og.mode_get(S.KILLS + team)
end

local function add_kills(team, delta)
  og.mode_set(S.KILLS + team, kills_of(team) + delta)
end

-- ---------------------------------------------------------------------------
-- Init
-- ---------------------------------------------------------------------------

local function on_mode_init(level)
  local obs = og.oblist()
  local authored_mask = match.census_mask(obs)
  local mask = core.activate_teams(authored_mask, og.match_setting("team_count"))
  if core.mask_count(mask) < 2 then
    error("tdm: fewer than two teams")
  end
  og.mode_set(S.TEAM_MASK, mask)
  match.consume_markers(obs, mask)
  match.strip_inactive_teams(obs, mask)
  -- Roster-only armies on request (shared rule set), before the bot-squad
  -- census below so backfill sees the post-strip world.
  strip.strip_authored_troops(mask, nil)

  -- Resolve config: explicit request > manifest row > defaults, per field.
  local row = levels.levels[level]
  local limit = match.resolve_limit(row, "score_limit",
                                    og.match_setting("score_limit"),
                                    T.score_limit)
  og.mode_set(S.SCORE_LIMIT, og.clamp(limit, 1, 255))
  local time_limit = match.resolve_limit(row, "time_limit", 0,
                                         T.time_limit_ticks)
  og.mode_set(S.TIME_LIMIT, time_limit)
  local respawn_ticks = og.match_setting("respawn_ticks")
  if respawn_ticks <= 0 then
    respawn_ticks = T.respawn_ticks
  end
  og.mode_set(S.RESPAWN_TICKS, respawn_ticks)

  -- Bot squads for active teams that field no livings.
  local per_team = { 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          per_team[team + 1] = per_team[team + 1] + 1
        end
      end
    end
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if per_team[team + 1] == 0 then
        match.spawn_bots(team, T.bot_squad, S.ANCHOR_CURSOR)
      end
    end
  end

  og.set_mode_name("TDM")
  og.mode_set(core.SLOT.PHASE, 1)
  -- The activation latch: MODE_ID written LAST.
  og.mode_set(core.SLOT.MODE_ID, core.MODE.TDM)
  core.announce("DEATHMATCH! FIRST TO " .. og.mode_get(S.SCORE_LIMIT), C.SOUND_CHARGE)
end

-- ---------------------------------------------------------------------------
-- Frag scoring + respawn eligibility (the killer channel)
-- ---------------------------------------------------------------------------

local function on_entity_death(ent, killer, killer_team)
  if not tdm_active() then
    return
  end
  if ent:order() ~= C.ORDER_LIVING then
    return
  end
  local mask = active_mask()
  local victim_team = ent:team_num()
  local victim_scores = false
  if victim_team < C.SCORE_TEAM_COUNT then
    victim_scores = core.mask_has(mask, victim_team)
  end

  -- Respawn eligibility mirrors the scan; ineligible bodies (wildlife,
  -- neutral teams, generator spawns, summons) never respawn — scrub the
  -- fresh stain under them (D6).
  local eligible = victim_scores
  if eligible then
    eligible = match.owns_its_life(ent)
  end
  if eligible then
    if not og.respawn_pending(ent) then
      og.respawn_schedule(ent, og.mode_get(S.RESPAWN_TICKS))
    end
  else
    core.scrub_corpse(ent)
  end

  -- Frag ledger. No-score arms: environment/suicide (nil killer or self),
  -- non-score victims (wildlife/neutral), owned victims (summons and
  -- generator spawns), killers outside the active mask.
  if killer == nil then
    return
  end
  if not victim_scores then
    return
  end
  if not match.owns_its_life(ent) then
    return
  end
  if killer == ent then
    return
  end
  if killer_team < 0 then
    return
  end
  if killer_team >= C.SCORE_TEAM_COUNT then
    return
  end
  if not core.mask_has(mask, killer_team) then
    return
  end
  if killer_team == victim_team then
    -- Teamkill: the team's frag count decrements (may go negative).
    add_kills(victim_team, -1)
    return
  end
  add_kills(killer_team, 1)
  og.award_score(killer_team, T.kill_score)
end

-- ---------------------------------------------------------------------------
-- Generator spawn caps (D5 mechanism, D16 arena identity)
-- ---------------------------------------------------------------------------

-- Live-spawn census per team byte. Wildlife bytes (at or beyond the score
-- range) count every live member — nothing else plays there. Score bytes
-- count only generator-owned livings, so players and bots never starve
-- their own team's generator (the shipped score-team caps are tents,
-- which keep owner; a clear_owner family on a score team would evade its
-- cap — no shipped map authors that).
local function spawn_census(obs)
  local spawned = { 0, 0, 0, 0, 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_LIVING then
        local team = w:team_num()
        if team < 8 then
          local counted = false
          if team >= C.SCORE_TEAM_COUNT then
            counted = true
          else
            local own = w:owner()
            if own ~= nil then
              counted = own:order() == C.ORDER_GENERATOR
            end
          end
          if counted then
            spawned[team + 1] = spawned[team + 1] + 1
          end
        end
      end
    end
  end
  return spawned
end

-- Toggles every capped team's generators between the authored
-- fire_frequency and authored + gen_pause_offset (the marker is the
-- entity's own field, so the authored value is always recoverable — R6).
-- fire_frequency only lands in busy AT fire time, so the marker alone
-- would leak one spawn and then jam busy with the inflated cooldown;
-- the same-pass busy top-up is the actual fire gate (init_fire refuses
-- while busy > 0) and keeps the inflated value from ever landing.
-- caps is the manifest's team-keyed table (integer keys WITH HOLES —
-- index directly, never iterate).
local function run_generator_caps(obs, caps)
  local spawned = spawn_census(obs)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_GENERATOR then
        local team = w:team_num()
        local cap = nil
        if team < 8 then
          cap = caps[team]
        end
        if cap ~= nil then
          local freq = w:fire_frequency()
          local paused = freq >= T.gen_pause_offset
          if spawned[team + 1] >= cap then
            if not paused then
              w:set_fire_frequency(og.fadd(freq, T.gen_pause_offset))
            end
            if w:busy() < T.ai_cadence + 1 then
              w:set_busy(T.ai_cadence + 1)
            end
          elseif paused then
            w:set_fire_frequency(og.fsub(freq, T.gen_pause_offset))
          end
        end
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- AI director (hunt-nearest steering; zero RNG, oblist order)
-- ---------------------------------------------------------------------------

-- Per team, in oblist order: repair members with broken foes onto the
-- nearest enemy, and under endgame focus (some enemy team within
-- focus_margin of the limit) retarget every preemptable member at the
-- leader's nearest member. Foes are livings, so no leaders are parked
-- (the auto-foe backstop is harmless here) — mode_ai discipline otherwise.
local function run_director(livings, mask)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local focus_team = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if kills_of(team) >= limit - T.focus_margin then
        if focus_team < 0 then
          focus_team = team
        end
      end
    end
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      for k = 1, #livings do
        local member = livings[k]
        if ai.is_directable(member, team) then
          local needs_foe = not match.foe_scores(member:foe(), mask, team)
          if needs_foe then
            local target = match.nearest_enemy(livings, mask, team, member:xpos(), member:ypos(), -1)
            if target ~= nil then
              member:set_foe(target)
              ai.issue_front(member, C.COMMAND_SEARCH, 120, 0, 0)
            end
          elseif focus_team >= 0 then
            if focus_team ~= team then
              if ai.may_preempt(member:s_front_command()) then
                local target = match.nearest_enemy(livings, mask, team, member:xpos(), member:ypos(), focus_team)
                if target ~= nil then
                  member:set_foe(target)
                  ai.issue_front(member, C.COMMAND_SEARCH, 120, 0, 0)
                end
              end
            end
          end
        end
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- Win / timeout / HUD
-- ---------------------------------------------------------------------------

local function run_win_check(mask, level_tick)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local winner = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if winner < 0 then
      if core.mask_has(mask, team) then
        if kills_of(team) >= limit then
          winner = team
        end
      end
    end
  end
  if winner < 0 then
    if level_tick >= og.mode_get(S.TIME_LIMIT) then
      winner = match.timeout_leader(mask, kills_of)
    end
  end
  if winner >= 0 then
    core.declare_team_win(winner)
  end
end

local function update_hud(mask)
  local limit = og.mode_get(S.SCORE_LIMIT)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      core.hud_score_line(team, team, kills_of(team), limit)
    end
  end
end

-- ---------------------------------------------------------------------------
-- Per-tick phases (post-act)
-- ---------------------------------------------------------------------------

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

local function on_mode_tick(level, tick)
  local obs = og.oblist()
  local mask = active_mask()
  mark_owned_lives(obs)
  match.schedule_dead(obs, mask, og.mode_get(S.RESPAWN_TICKS))
  if og.mod(og.world_tick(), T.ai_cadence) == 0 then
    local livings = {}
    for k = 1, #obs do
      local w = obs[k]
      if w:dead() == 0 then
        if w:order() == C.ORDER_LIVING then
          livings[#livings + 1] = w
        end
      end
    end
    run_director(livings, mask)
    local row = levels.levels[level]
    if row ~= nil then
      if row.spawn_caps ~= nil then
        run_generator_caps(obs, row.spawn_caps)
      end
    end
  end
  run_win_check(mask, tick)
  update_hud(mask)
end

-- Engine handoff: the timer fire revived the walker in place — apply the
-- anchor-cursor placement on top.
local function on_respawn(ent)
  if not tdm_active() then
    return
  end
  match.place_at_anchor(ent, ent:team_num(), S.ANCHOR_CURSOR, false)
end

return {
  S = S,
  T = T,
  on_mode_init = on_mode_init,
  on_mode_tick = on_mode_tick,
  on_entity_death = on_entity_death,
  on_respawn = on_respawn,
}
