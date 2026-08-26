-- Free For All rules + AI director — 16-fighter band reseat over mode_fighters, per-fighter frag ledger with the ascending-index tiebreak, rotated 4-pool respawns, leader HUD/beacon, repair-only steering with endgame focus (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Rule spec: docs/ffa-design.md §2, §3, §5, §6 (decisions D2/D3/D12/D18/
-- D19). Every deployed character is reseated onto a unique band byte at
-- init — sim-native mutual hostility falls out of is_friendly's
-- team-equality rule — and frags ride the ModeState vars, never m_score.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local match = og.use("mode_match")
local levels = og.use("mode_levels")
local items = og.use("mode_items")
local fighters = og.use("mode_fighters")

-- Mode-private slot map (header 0-7 is mode_core.SLOT).
local S = {
  FIGHTER_COUNT = 8,
  SCORE_LIMIT = 9,
  DEADLINE = 10, -- level-tick deadline (manifest time_limit)
  ITEM_CURSOR = 11, -- mode_items pad rotation cursor
  ITEM_LAST = 12, -- mode_items last-spawn tick (seeded at init)
  ANCHOR_CURSOR = 13, -- rotated pool + anchor cursor (one counter for both)
  BAND_BITMAP = 14, -- 16-bit assignment bitmap, bit c = slot c taken
  FLAGS = 15, -- reserved: announce/phase flags
  FRAGS = 16, -- 16..31, +color index (suicides decrement; may go negative)
  IDS = 32, -- 32..47, +color index: fighter entity id, 0 = free slot
  -- 48..63 reserved
}

-- Tuning (manifest score_limit/time_limit override the defaults; the
-- match_setting requests override the manifest).
local T = {
  score_limit = 15,
  time_limit_ticks = 7200,
  respawn_ticks = 90, -- D18: between TDM's 120 and Mutant's 60
  default_fighters = 8, -- bot-fill target when the row carries none
  ai_cadence = 15,
  focus_margin = 3, -- endgame focus + leader beacon threshold
  name_budget = 17, -- 25-char line budget minus the widest prefix
  -- Respawning pickups (lib/mode_items): fallback interval when the
  -- manifest row carries none — 15 s, deathmatch attrition pace.
  item_interval = 180,
  bot_roster = { "core:soldier", "core:archer", "core:elf", "core:mage", "core:thief" },
}

local function ffa_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.FFA
end

local function frags_of(c)
  return og.mode_get(S.FRAGS + c)
end

local function add_frags(c, delta)
  og.mode_set(S.FRAGS + c, frags_of(c) + delta)
end

local function respawn_delay()
  local delay = og.match_setting("respawn_ticks")
  if delay <= 0 then
    delay = T.respawn_ticks
  end
  return delay
end

-- Leader and runner-up color indices (-1 when absent): highest frags,
-- ties to the LOWEST color index (ascending scan, strictly-greater
-- replacement) — the D3 tiebreak and the timeout ladder in one place.
local function standings(bitmap)
  local first = -1
  local second = -1
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if fighters.band_has(bitmap, c) then
      if first < 0 then
        first = c
      elseif frags_of(c) > frags_of(first) then
        second = first
        first = c
      elseif second < 0 then
        second = c
      elseif frags_of(c) > frags_of(second) then
        second = c
      end
    end
  end
  return first, second
end

-- Fighter c's display name: the character name (D9 — the existing g_name
-- binding), else the band color name for unnamed fighters and bots;
-- clipped so every "PREFIX name n" line fits the 25-char budget.
local function fighter_name(c)
  local name = ""
  local w = og.find_by_id(og.mode_get(S.IDS + c))
  if w ~= nil then
    if w:has_guy() then
      name = w:g_name()
    end
  end
  if #name == 0 then
    name = og.team_color_name(fighters.band_byte(c))
  end
  return string.sub(name, 1, T.name_budget)
end

-- ---------------------------------------------------------------------------
-- Init
-- ---------------------------------------------------------------------------

-- The decide fold (the team modes' discipline, lineup review L1): the
-- fighter target and the count the fill will reach, pure over the
-- census — the deployed list and the row. A band below two fighters is
-- refused HERE, before any world write, so the kept post-refusal world
-- (which GO adopts under classic rules) is exactly the authored one.
local function decide(level, deployed_count)
  local row = levels.levels[level]
  local target = T.default_fighters
  if row ~= nil then
    if row.fighters ~= nil then
      target = row.fighters
    end
  end
  target = og.clamp(target, 0, C.FFA_TEAM_COUNT)
  local planned = fighters.planned_count(deployed_count, target)
  local starts = planned >= 2
  local reason = nil
  if not starts then
    reason = "ffa: fewer than two fighters"
  end
  return {
    starts = starts,
    reason = reason,
    target = target,
  }
end

local function on_mode_init(level)
  local obs = og.oblist()
  local roster = fighters.enumerate(obs)
  local decision = decide(level, #roster)
  if not decision.starts then
    error(decision.reason)
  end
  local deployed = fighters.deploy(roster, C.FFA_TEAM_COUNT)
  fighters.assign(deployed, S.IDS, S.BAND_BITMAP)
  -- All four marker clusters are consumed (they are position pools here,
  -- not identities) and the whole authored score-range cast — livings and
  -- generators without a roster guy — retires: an empty active mask over
  -- the TDM strip semantics. Wildlife (bytes 4-7) is arena identity and
  -- stays; the reseated fighters are already out of the score range.
  match.consume_markers(obs, 15)
  match.strip_inactive_teams(obs, 0)

  -- Resolve config: explicit request > manifest row > defaults, per field.
  local row = levels.levels[level]
  local limit = match.resolve_limit(row, "score_limit",
                                    og.match_setting("score_limit"),
                                    T.score_limit)
  og.mode_set(S.SCORE_LIMIT, og.clamp(limit, 1, 255))
  local time_limit = match.resolve_time_limit(row, T.time_limit_ticks)
  og.mode_set(S.DEADLINE, time_limit)

  -- Scatter the deployed fighters over the rotated pools so seat-team
  -- clusters never start stacked (init-time placement may teleport — the
  -- blessed RNG fallback).
  for k = 1, #deployed do
    fighters.place_rotated(deployed[k], S.ANCHOR_CURSOR, true)
  end

  -- Bot fill to the decision's target (the count it reaches was decided
  -- above; nothing below can refuse).
  local count = fighters.fill_bots(#deployed, decision.target, S.IDS,
                                   S.BAND_BITMAP, S.ANCHOR_CURSOR,
                                   T.bot_roster)
  og.mode_set(S.FIGHTER_COUNT, count)

  og.set_mode_name("FFA")
  og.mode_set(S.ITEM_LAST, og.world_tick())
  og.mode_set(core.SLOT.PHASE, 1)
  -- The activation latch: MODE_ID written LAST (it also keeps the init
  -- bot spawns out of the on_entity_spawn adoption arm).
  og.mode_set(core.SLOT.MODE_ID, core.MODE.FFA)
  core.announce("FREE FOR ALL", C.SOUND_CHARGE)
  core.announce(count .. " FIGHTERS ENTER", C.SOUND_CHARGE)
end

-- ---------------------------------------------------------------------------
-- Frag scoring + respawn eligibility (the killer channel)
-- ---------------------------------------------------------------------------

local function on_entity_death(ent, killer, killer_team)
  if not ffa_active() then
    return
  end
  if ent:order() ~= C.ORDER_LIVING then
    return
  end
  local bitmap = og.mode_get(S.BAND_BITMAP)
  local victim_team = ent:team_num()
  local victim_fights = false
  if victim_team >= C.FFA_TEAM_BASE then
    if victim_team < C.FFA_TEAM_BASE + C.FFA_TEAM_COUNT then
      victim_fights = fighters.band_has(bitmap,
                                        victim_team - C.FFA_TEAM_BASE)
    end
  end

  -- Respawn eligibility mirrors the ledger: band fighters that own their
  -- lives come back (scheduled synchronously so the seat-retention arm
  -- sees already_scheduled on the death tick); everything else — wildlife,
  -- summons, generator spawns — never respawns, so scrub the fresh stain.
  local eligible = victim_fights
  if eligible then
    eligible = match.owns_its_life(ent)
  end
  if eligible then
    if not og.respawn_pending(ent) then
      og.respawn_schedule(ent, respawn_delay())
    end
  else
    core.scrub_corpse(ent)
  end

  -- Frag ledger, TDM semantics band-shifted (killer resolution rides the
  -- 48-tick stamp; the stamped TEAM survives a mutual kill, so the killer
  -- handle is never consulted — generator and summon kills credit the
  -- owner-chain root automatically). No-score arms: non-band victims,
  -- owned victims, environment/stale stamps, non-band killers.
  if not victim_fights then
    return
  end
  if not match.owns_its_life(ent) then
    return
  end
  if killer_team < C.FFA_TEAM_BASE then
    return
  end
  if killer_team >= C.FFA_TEAM_BASE + C.FFA_TEAM_COUNT then
    return
  end
  if not fighters.band_has(bitmap, killer_team - C.FFA_TEAM_BASE) then
    return
  end
  if killer_team == victim_team then
    -- Suicide shape (a charm-flipped victim dying to its own byte): the
    -- slot's frag count decrements (may go negative).
    add_frags(victim_team - C.FFA_TEAM_BASE, -1)
    return
  end
  add_frags(killer_team - C.FFA_TEAM_BASE, 1)
end

-- ---------------------------------------------------------------------------
-- AI director (repair-only steering + endgame focus; zero RNG)
-- ---------------------------------------------------------------------------

local function run_director(livings, bitmap)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local first = standings(bitmap)
  local leader = nil
  if first >= 0 then
    if frags_of(first) >= limit - T.focus_margin then
      leader = og.find_by_id(og.mode_get(S.IDS + first))
      if leader ~= nil then
        if leader:dead() ~= 0 then
          leader = nil
        end
      end
    end
  end
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if fighters.band_has(bitmap, c) then
      local member = og.find_by_id(og.mode_get(S.IDS + c))
      if member ~= nil then
        local team = fighters.band_byte(c)
        if ai.is_directable(member, team) then
          if not fighters.foe_is_fighter(member:foe(), team, bitmap) then
            local target = fighters.nearest_fighter(livings, team, bitmap,
                                                    member:xpos(),
                                                    member:ypos())
            if target ~= nil then
              member:set_foe(target)
              ai.issue_front(member, C.COMMAND_SEARCH, 120, 0, 0)
            end
          elseif leader ~= nil then
            if c ~= first then
              if ai.may_preempt(member:s_front_command()) then
                member:set_foe(leader)
                ai.issue_front(member, C.COMMAND_SEARCH, 120, 0, 0)
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

local function declare_fighter_win(c)
  local byte = fighters.band_byte(c)
  local line = "WINNER: " .. fighter_name(c)
  core.announce(line, C.SOUND_CHARGE)
  -- The winner line replaces the leader row and outlives the match — a
  -- decided match runs no more Lua, so nothing overwrites it.
  og.set_hud_line(0, line, byte)
  og.declare_winner(byte)
end

local function run_win_check(bitmap, level_tick)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local winner = -1
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if winner < 0 then
      if fighters.band_has(bitmap, c) then
        if frags_of(c) >= limit then
          winner = c
        end
      end
    end
  end
  if winner < 0 then
    if level_tick >= og.mode_get(S.DEADLINE) then
      winner = standings(bitmap)
    end
  end
  if winner >= 0 then
    declare_fighter_win(winner)
  end
end

-- HUD budget (4x25 replicated lines): the two leaders and the goal row.
-- "1ST " (4) + name (<= 17) + " " + frags (<= 3 digits) = 25 worst case.
-- The beacon marks the leader once it is within focus_margin of the limit
-- (Mutant-style endgame drama).
local function update_hud(bitmap)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local first, second = standings(bitmap)
  if first >= 0 then
    og.set_hud_line(0, "1ST " .. fighter_name(first) .. " " .. frags_of(first),
                    fighters.band_byte(first))
  end
  if second >= 0 then
    og.set_hud_line(1, "2ND " .. fighter_name(second) .. " " .. frags_of(second),
                    fighters.band_byte(second))
  end
  og.set_hud_line(2, "GOAL " .. limit)
  local beacon = nil
  if first >= 0 then
    if frags_of(first) >= limit - T.focus_margin then
      beacon = og.find_by_id(og.mode_get(S.IDS + first))
    end
  end
  if beacon ~= nil then
    if beacon:dead() ~= 0 then
      beacon = nil
    end
  end
  if beacon ~= nil then
    og.set_beacon(0, beacon, fighters.band_byte(first))
  else
    og.set_beacon(0, nil)
  end
end

-- ---------------------------------------------------------------------------
-- Per-tick phases (post-act)
-- ---------------------------------------------------------------------------

local function on_mode_tick(level, tick)
  local obs = og.oblist()
  -- Durable origin marking (mode_match.owns_its_life's other half): a
  -- summon orphaned by its owner's death stays out of the frag ledger
  -- and the respawn queue.
  match.mark_owned_lives(obs)
  fighters.adopt_new(obs, S.IDS, S.FRAGS, S.BAND_BITMAP)
  local bitmap = og.mode_get(S.BAND_BITMAP)
  og.mode_set(S.FIGHTER_COUNT, fighters.band_count(bitmap))
  fighters.schedule_dead(S.IDS, S.BAND_BITMAP, respawn_delay())
  if og.mod(og.world_tick(), T.ai_cadence) == 0 then
    fighters.renormalize(S.IDS, S.BAND_BITMAP)
    local livings = {}
    for k = 1, #obs do
      local w = obs[k]
      if w:dead() == 0 then
        if w:order() == C.ORDER_LIVING then
          livings[#livings + 1] = w
        end
      end
    end
    run_director(livings, bitmap)
  end
  items.run(levels.levels[level], S.ITEM_CURSOR, S.ITEM_LAST, T.item_interval)
  -- HUD before the win check: the WINNER line written by a same-tick win
  -- must be the one that sticks.
  update_hud(bitmap)
  run_win_check(bitmap, tick)
end

-- Mid-join adoption rides the spawn dispatch AND the per-tick sweep. The
-- engine attaches a roster guy AFTER add_ob returns (create_team_walker),
-- so the just-spawned walker never reads has_guy here — this arm adopts
-- earlier joiners immediately and the on_mode_tick sweep lands this one
-- next tick.
local function on_entity_spawn(_ent)
  if not ffa_active() then
    return
  end
  fighters.adopt_new(og.oblist(), S.IDS, S.FRAGS, S.BAND_BITMAP)
end

-- Engine handoff: the timer fire revived the walker in place — re-assert
-- the slot's band byte (D12: revive_player_walker clears real_team_num to
-- 255 but skips the team restore for band bytes, so a fighter that died
-- charmed would otherwise keep the charmer's byte), then reposition over
-- the rotated pools.
local function on_respawn(ent)
  if not ffa_active() then
    return
  end
  local c = fighters.slot_of(og.entity_id(ent), S.IDS)
  if c ~= nil then
    ent:set_team_num(fighters.band_byte(c))
  end
  fighters.place_rotated(ent, S.ANCHOR_CURSOR, false)
end

return {
  S = S,
  T = T,
  on_mode_init = on_mode_init,
  on_mode_tick = on_mode_tick,
  on_entity_death = on_entity_death,
  on_entity_spawn = on_entity_spawn,
  on_respawn = on_respawn,
}
