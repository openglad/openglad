-- Mutant rules + AI director — FFA-to-mutant phase machine over the damage gate, crown/transfer/revert, idempotent buffs + HP decay with kill-heal, beacon lifecycle, hunt/cull steering (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Rule spec: modes.md §7 as amended by DECISIONS D4 — competitors keep
-- their authored FFA teams 0-3 (cap 4), the one-way damage matrix is the
-- on_damage gate (non-mutant against non-mutant deals nothing), and the
-- herd-team/charm-suppression/Bottom-Feeder machinery is cut. Scoring:
-- +1 per kill made as the Mutant, +1 for killing the Mutant; FFA-phase
-- kills only decide who mutates. The timeout tiebreak is the shared
-- three-rung ladder (mode metric, m_score, lowest team byte).
--
-- ACCEPTED: cross-phase attribution inside the 48-tick window. The engine
-- resolves a death's killer from the combat stamp while it is fresher than
-- og::sim::kKillAttributionTicks (48), and that window can straddle a crown
-- or a revert — a competitor chipped during FFA that dies seconds later in
-- the mutant phase scores for the chipper under mutant-phase rules. It is
-- deterministic (the same stamp on every peer), bounded to four seconds, and
-- arguably the fair reading: the blow that mattered landed. The phase is not
-- stamped alongside the attacker, and adding a phase to the stamp would mean
-- a C++ wire field for a four-second edge case, so this is left as is.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local match = og.use("mode_match")
local levels = og.use("mode_levels")

-- Mode-private slot map (header 0-7 is mode_core.SLOT; header PHASE is
-- the phase machine: 1 FFA, 2 MUTANT).
local S = {
  TEAM_MASK = 8,
  SCORE_LIMIT = 9,
  TIME_LIMIT = 10,
  RESPAWN_TICKS = 11,
  ANCHOR_CURSOR = 12,
  MUTANT_ENTITY = 13, -- entity id; 0 = no mutant (FFA)
  MUTANT_TEAM1 = 14, -- competitor team + 1; 0 = none
  MUTANT_BASE_DAMAGE = 15, -- pre-buff damage, x256 fixed point (D16)
  SCORE = 16, -- 16..19, +team
}

local PHASE_FFA = 1
local PHASE_MUTANT = 2

-- Tuning (modes.md §7.9 minus the D4 cuts; manifest score_limit and
-- time_limit override the defaults, the match_setting request overrides
-- the manifest).
local T = {
  score_limit = 10,
  time_limit_ticks = 7200,
  kill_score = 1, -- m_score per scoring kill (og.award_score)
  respawn_ticks = 60,
  invis_ticks = 100,
  speed_bonus = 1.0,
  speed_ticks = 30,
  dmg_mult = 2.0,
  decay_period = 12, -- ticks between decay steps (1 s)
  decay_hp = 1.0,
  kill_heal = 15,
  teleport_range = 160, -- px; consumed by the pack teleport overrides
  ai_cadence = 15,
  cull_radius = 120,
  -- The mutant mark, visible to observers without a mode-var read
  -- (16384 is the first bit past og.C.BIT_LAST's sentinel range).
  mutant_bit = 16384,
  -- One bot per empty competitor team, family by team byte.
  bot_roster = { "core:soldier", "core:archer", "core:elf", "core:thief" },
}

local function mutant_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.MUTANT
end

local function active_mask()
  return og.mode_get(S.TEAM_MASK)
end

local function score_of(team)
  return og.mode_get(S.SCORE + team)
end

local function add_score(team, delta)
  og.mode_set(S.SCORE + team, score_of(team) + delta)
end

-- Owner-chain root (the damage gate needs it: on_damage's attacker is the
-- one-hop-resolved walker, so a summon arrives as itself). Bounded walk —
-- owner chains are shallow and acyclic, the bound is a tripwire.
local function chain_root(w)
  local root = w
  for _ = 1, 8 do
    local own = root:owner()
    if own == nil then
      return root
    end
    root = own
  end
  return root
end

local function is_mutant(w)
  local id = og.mode_get(S.MUTANT_ENTITY)
  if id == 0 then
    return false
  end
  return og.entity_id(w) == id
end

-- ---------------------------------------------------------------------------
-- Crown / de-mutate / revert
-- ---------------------------------------------------------------------------

-- All buffs are refreshed, never accumulated: damage recomputes from the
-- banked base every application, so a double crown or a tick top-up can
-- never compound.
--
-- The timed buffs are a FLOOR, not an assignment. run_mutant_upkeep calls
-- this every tick, so a flat write would erase a potion the Mutant picked up
-- mid-reign (a speed potion adds duration and sets the bonus to its own
-- level) one tick after drinking it. og.max keeps whichever is stronger, so
-- pickups stack on top and a plain reign is unchanged: the crown value is
-- restored the moment the pickup decays past it.
--
-- damage stays a plain recompute from the bank on purpose — the bank is what
-- makes a double crown non-compounding, and de_mutate restores exactly it.
local function apply_buffs(w)
  w:set_invisibility_left(og.max(w:invisibility_left(), T.invis_ticks))
  w:set_speed_bonus(og.max(w:speed_bonus(), T.speed_bonus))
  w:set_speed_bonus_left(og.max(w:speed_bonus_left(), T.speed_ticks))
  local base = og.fdiv(og.mode_get(S.MUTANT_BASE_DAMAGE), 256.0)
  w.damage = og.fmul(base, T.dmg_mult)
end

local function crown(w)
  local team = w:team_num()
  og.mode_set(S.MUTANT_ENTITY, og.entity_id(w))
  og.mode_set(S.MUTANT_TEAM1, team + 1)
  og.mode_set(S.MUTANT_BASE_DAMAGE, og.trunc(og.fmul(w:damage(), 256.0)))
  w:s_set_bit_flags(T.mutant_bit, 1)
  apply_buffs(w)
  og.mode_set(core.SLOT.PHASE, PHASE_MUTANT)
  og.set_beacon(0, w, team)
  core.announce(og.team_color_name(team) .. " IS THE MUTANT!", C.SOUND_CHARGE)
end

-- Strips the mutant state off the current holder (posthumously fine: the
-- corpse respawns as a plain competitor) and clears the beacon.
local function de_mutate()
  local w = og.find_by_id(og.mode_get(S.MUTANT_ENTITY))
  if w ~= nil then
    w.damage = og.fdiv(og.mode_get(S.MUTANT_BASE_DAMAGE), 256.0)
    w:s_set_bit_flags(T.mutant_bit, 0)
    w:set_invisibility_left(0)
    w:set_speed_bonus_left(0)
  end
  og.mode_set(S.MUTANT_ENTITY, 0)
  og.mode_set(S.MUTANT_TEAM1, 0)
  og.mode_set(S.MUTANT_BASE_DAMAGE, 0)
  og.set_beacon(0, nil)
end

-- Pool revert: no heir — everyone is hostile again and the next first
-- blood re-mutates.
local function revert_to_ffa()
  de_mutate()
  og.mode_set(core.SLOT.PHASE, PHASE_FFA)
  core.announce("THE MUTANT IS NO MORE!", C.SOUND_TELEPORT)
end

-- ---------------------------------------------------------------------------
-- The damage gate (D4)
-- ---------------------------------------------------------------------------

-- Mutant phase only: a hit whose attacker root AND target root are both
-- non-mutant livings deals nothing. Everyone against the Mutant and the
-- Mutant against everyone stay normal, and both sides' summons inherit
-- the matrix through the root walk. Environment damage (nil or
-- non-living-rooted attackers, non-living targets) always lands.
local function on_damage(target, attacker, amount)
  if og.mode_get(core.SLOT.PHASE) ~= PHASE_MUTANT then
    return nil
  end
  if attacker == nil then
    return nil
  end
  local attacker_root = chain_root(attacker)
  if attacker_root:order() ~= C.ORDER_LIVING then
    return nil
  end
  local target_root = chain_root(target)
  if target_root:order() ~= C.ORDER_LIVING then
    return nil
  end
  if is_mutant(attacker_root) then
    return nil
  end
  if is_mutant(target_root) then
    return nil
  end
  return false
end

-- ---------------------------------------------------------------------------
-- Death: transfer machine + respawn eligibility
-- ---------------------------------------------------------------------------

local function on_entity_death(ent, killer, killer_team)
  if not mutant_active() then
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

  -- Everyone respawns (Lua eligibility — the engine death scan does not
  -- run on scripted levels); summons and other owned walkers do not, so
  -- scrub the stain under them (D6).
  local eligible = victim_scores
  if eligible then
    if not ent:has_guy() then
      eligible = ent:owner() == nil
    end
  end
  if eligible then
    if not og.respawn_pending(ent) then
      og.respawn_schedule(ent, og.mode_get(S.RESPAWN_TICKS))
    end
  else
    core.scrub_corpse(ent)
  end

  -- A competitor death: a score-team living that owns its life.
  local victim_competes = victim_scores
  if victim_competes then
    victim_competes = ent:owner() == nil
  end
  local killer_scores = false
  if killer_team >= 0 then
    if killer_team < C.SCORE_TEAM_COUNT then
      killer_scores = core.mask_has(mask, killer_team)
    end
  end

  local phase = og.mode_get(core.SLOT.PHASE)
  if phase == PHASE_FFA then
    -- First blood crowns the killer. Environment deaths (nil killer),
    -- suicides and summon deaths decide nothing, and a killer who died
    -- with its victim cannot wear the crown.
    if not victim_competes then
      return
    end
    if killer == nil then
      return
    end
    if killer == ent then
      return
    end
    if not killer_scores then
      return
    end
    if killer:dead() ~= 0 then
      return
    end
    crown(killer)
    return
  end
  if phase ~= PHASE_MUTANT then
    return
  end

  if is_mutant(ent) then
    -- The Mutant fell. A competitor kill scores +1 for the killer team
    -- (a mutual kill keeps the team credit even when the killer handle
    -- is gone — the D3 recency channel resolves the team) and a LIVE
    -- killer inherits the mutancy plus the kill-heal; anything else
    -- (environment, decay, suicide) reverts the pool to FFA.
    local scored = false
    if killer_scores then
      if killer == nil then
        scored = true
      elseif killer ~= ent then
        scored = true
      end
    end
    if scored then
      add_score(killer_team, 1)
      og.award_score(killer_team, T.kill_score)
    end
    de_mutate()
    local heir = nil
    if killer ~= nil then
      if killer ~= ent then
        if killer_scores then
          if killer:dead() == 0 then
            heir = killer
          end
        end
      end
    end
    if heir ~= nil then
      crown(heir)
      heir:heal_clamped(T.kill_heal)
    else
      og.mode_set(core.SLOT.PHASE, PHASE_FFA)
      core.announce("THE MUTANT IS NO MORE!", C.SOUND_TELEPORT)
    end
    return
  end

  -- A non-mutant competitor fell: only the Mutant's kills score, and
  -- every kill feeds the decay clock back some blood.
  if not victim_competes then
    return
  end
  if killer == nil then
    return
  end
  if not is_mutant(killer) then
    return
  end
  local mutant_team = og.mode_get(S.MUTANT_TEAM1) - 1
  add_score(mutant_team, 1)
  og.award_score(mutant_team, T.kill_score)
  killer:heal_clamped(T.kill_heal)
end

-- ---------------------------------------------------------------------------
-- Per-tick upkeep: buffs, beacon, HP decay
-- ---------------------------------------------------------------------------

local function run_mutant_upkeep()
  local id = og.mode_get(S.MUTANT_ENTITY)
  local mut = nil
  if id ~= 0 then
    mut = og.find_by_id(id)
  end
  local gone = mut == nil
  if not gone then
    gone = mut:dead() ~= 0
  end
  if gone then
    -- A kill that bypassed walker::death (a bare dead-flag write) left a
    -- stale crown; revert deterministically. Real deaths already ran the
    -- transfer machine before this phase.
    revert_to_ffa()
    return
  end
  apply_buffs(mut)
  og.set_beacon(0, mut, mut:team_num())
  if og.mod(og.world_tick(), T.decay_period) == 0 then
    mut.hp = og.fsub(mut.hp, T.decay_hp)
    if mut.hp <= 0 then
      -- Decay can kill. Run the real death so the transfer machine sees
      -- it: with no recent attacker it resolves as environment (pool
      -- revert); a recent chipper inherits the crown via D3 recency.
      mut.dead = 1
      mut:death()
    end
  end
end

-- ---------------------------------------------------------------------------
-- AI director (zero RNG, oblist order)
-- ---------------------------------------------------------------------------

-- The Mutant's prey: the nearest live competitor, except that with two or
-- more competitors inside the cull radius it takes the weakest (lowest
-- hp, ties to the earlier oblist slot) — kills are its only heal.
local function mutant_prey(livings, mask, mut)
  local team = mut:team_num()
  local nearest = match.nearest_enemy(livings, mask, team, mut:xpos(), mut:ypos(), -1)
  local cull = nil
  local in_radius = 0
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
      if ai.dist_to(w, mut:xpos(), mut:ypos()) <= T.cull_radius then
        in_radius = in_radius + 1
        if cull == nil then
          cull = w
        elseif w.hp < cull.hp then
          cull = w
        end
      end
    end
  end
  if in_radius >= 2 then
    return cull
  end
  return nearest
end

local function run_director(livings, mask)
  local phase = og.mode_get(core.SLOT.PHASE)
  local mutant_id = og.mode_get(S.MUTANT_ENTITY)
  local mut = nil
  if phase == PHASE_MUTANT then
    if mutant_id ~= 0 then
      mut = og.find_by_id(mutant_id)
      if mut ~= nil then
        if mut:dead() ~= 0 then
          mut = nil
        end
      end
    end
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      for k = 1, #livings do
        local member = livings[k]
        if ai.is_directable(member, team) then
          if mut ~= nil then
            if og.entity_id(member) == mutant_id then
              -- The bot Mutant hunts its prey; no retreat-to-heal (the
              -- only heal is killing).
              local prey = mutant_prey(livings, mask, member)
              if prey ~= nil then
                member:set_foe(prey)
                ai.issue_front(member, C.COMMAND_SEARCH, 120, 0, 0)
              end
            elseif ai.may_preempt(member:s_front_command()) then
              -- Hunters converge on the Mutant: the explicit foe defeats
              -- invisibility's acquisition roll (the beacon hands human
              -- hunters the same information), the refreshed GOTO walks
              -- the live position.
              member:set_foe(mut)
              ai.issue_front(member, C.COMMAND_GOTO, 45, mut:xpos(), mut:ypos())
            end
          else
            -- FFA phase: the default ladder already deathmatches; only
            -- repair bots whose foe is broken (nil, dead, or a
            -- non-scoring target the engine backstop handed out).
            if not match.foe_scores(member:foe(), mask, team) then
              local target = match.nearest_enemy(livings, mask, team, member:xpos(), member:ypos(), -1)
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

-- ---------------------------------------------------------------------------
-- Win / timeout / HUD
-- ---------------------------------------------------------------------------

local function run_win_check(mask, level_tick)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local winner = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if winner < 0 then
      if core.mask_has(mask, team) then
        if score_of(team) >= limit then
          winner = team
        end
      end
    end
  end
  if winner < 0 then
    if level_tick >= og.mode_get(S.TIME_LIMIT) then
      winner = match.timeout_leader(mask, score_of)
    end
  end
  if winner >= 0 then
    core.declare_team_win(winner)
  end
end

-- One line per competitor: "RED 3/10", starred while that competitor
-- wears the crown ("YELLOW 10/10 *" = 15 chars, inside the 25 budget).
local function update_hud(mask)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local mutant_team1 = og.mode_get(S.MUTANT_TEAM1)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      local line = og.team_color_name(team) .. " " .. score_of(team) .. "/" .. limit
      if team + 1 == mutant_team1 then
        line = line .. " *"
      end
      og.set_hud_line(team, line, team)
    end
  end
end

-- ---------------------------------------------------------------------------
-- Init
-- ---------------------------------------------------------------------------

local function on_mode_init(level)
  local obs = og.oblist()
  local authored_mask = match.census_mask(obs)
  local mask = core.activate_teams(authored_mask, og.match_setting("team_count"))
  if core.mask_count(mask) < 2 then
    error("mutant: fewer than two competitors")
  end
  og.mode_set(S.TEAM_MASK, mask)
  match.consume_markers(obs, mask)
  match.strip_inactive_teams(obs, mask)

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

  -- One bot per empty competitor team (FFA seats, not squads; family by
  -- team byte for arena flavor).
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
        match.spawn_bots(team, { T.bot_roster[team + 1] }, S.ANCHOR_CURSOR)
      end
    end
  end

  og.set_mode_name("MUTANT")
  og.mode_set(core.SLOT.PHASE, PHASE_FFA)
  -- The activation latch: MODE_ID written LAST.
  og.mode_set(core.SLOT.MODE_ID, core.MODE.MUTANT)
  core.announce("MUTANT! FIRST TO " .. og.mode_get(S.SCORE_LIMIT), C.SOUND_CHARGE)
end

-- ---------------------------------------------------------------------------
-- Per-tick phases (post-act)
-- ---------------------------------------------------------------------------

local function on_mode_tick(level, tick)
  local obs = og.oblist()
  local mask = active_mask()
  match.schedule_dead(obs, mask, og.mode_get(S.RESPAWN_TICKS))
  if og.mode_get(core.SLOT.PHASE) == PHASE_MUTANT then
    run_mutant_upkeep()
  end
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
  end
  run_win_check(mask, tick)
  update_hud(mask)
end

-- Engine handoff: the timer fire revived the walker in place — apply the
-- anchor-cursor placement on top.
local function on_respawn(ent)
  if not mutant_active() then
    return
  end
  match.place_at_anchor(ent, ent:team_num(), S.ANCHOR_CURSOR, false)
end

return {
  S = S,
  T = T,
  on_mode_init = on_mode_init,
  on_mode_tick = on_mode_tick,
  on_damage = on_damage,
  on_entity_death = on_entity_death,
  on_respawn = on_respawn,
}
