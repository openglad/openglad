-- Mutant rules + AI director — FFA-to-mutant phase machine over the damage gate, crown/transfer/revert, idempotent buffs + HP decay with kill-heal, beacon lifecycle, hunt/cull steering (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Rule spec: docs/ffa-design.md §8 (D13) amending modes.md §7 — competitors
-- are individual deployed characters on the fighter band (team bytes 16-31,
-- shared machinery in lib/mode_fighters.lua), cap 16, bot fill to the
-- manifest row's fighters count. Two humans seated on one lobby team become
-- separate mutually hostile competitors (issue #187). The one-way damage
-- matrix is the on_damage gate (non-mutant against non-mutant deals
-- nothing), and the herd-team/charm-suppression/Bottom-Feeder machinery is
-- cut. Scoring: +1 per kill made as the Mutant, +1 for killing the Mutant;
-- FFA-phase kills only decide who mutates. The timeout tiebreak is the
-- leader by score, ties to the lowest color index (m_score is 4-wide and
-- band competitors never touch it, so the shared three-rung ladder's middle
-- rung is unreachable here).
--
-- Mode-private slot map (header 0-7 is mode_core.SLOT; SCORE and IDS are
-- 16-slot ranges keyed by color index c, band byte 16 + c):
--   8  FIGHTER_COUNT      9  SCORE_LIMIT       10 TIME_LIMIT
--   11 RESPAWN_TICKS      12 ANCHOR_CURSOR     13 MUTANT_ENTITY
--   14 MUTANT_TEAM        15 MUTANT_BASE_DAMAGE
--   16..31 SCORE (+c)     32..47 IDS (+c)      48 BAND_BITMAP
--   49 ITEM_CURSOR        50 ITEM_LAST
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
local items = og.use("mode_items")
local fighters = og.use("mode_fighters")

-- Mode-private slot map (table form of the header comment above; header
-- PHASE is the phase machine: 1 FFA, 2 MUTANT).
local S = {
  FIGHTER_COUNT = 8,
  SCORE_LIMIT = 9,
  TIME_LIMIT = 10,
  RESPAWN_TICKS = 11,
  ANCHOR_CURSOR = 12, -- rotated pool + anchor cursor (one counter for both)
  MUTANT_ENTITY = 13, -- entity id; 0 = no mutant (FFA)
  MUTANT_TEAM = 14, -- the mutant's ASSIGNED band byte; 0 = none
  MUTANT_BASE_DAMAGE = 15, -- pre-buff damage, x256 fixed point (D16)
  SCORE = 16, -- 16..31, +color index
  IDS = 32, -- 32..47, +color index: competitor entity id, 0 = free slot
  BAND_BITMAP = 48, -- 16-bit assignment bitmap, bit c = slot c taken
  ITEM_CURSOR = 49, -- mode_items pad rotation cursor
  ITEM_LAST = 50, -- mode_items last-spawn tick (seeded at init)
}

local PHASE_FFA = 1
local PHASE_MUTANT = 2

-- Tuning (modes.md §7.9 minus the D4 cuts; manifest score_limit and
-- time_limit override the defaults, the match_setting request overrides
-- the manifest).
local T = {
  score_limit = 10,
  time_limit_ticks = 7200,
  respawn_ticks = 60,
  invis_ticks = 100,
  speed_bonus = 1.0,
  speed_ticks = 30,
  dmg_mult = 2.0,
  decay_period = 12, -- ticks between decay steps (1 s)
  decay_hp = 1.0,
  kill_heal = 15,
  teleport_range = 160, -- px; consumed by the pack teleport overrides
  -- Respawning pickups (lib/mode_items): fallback interval when the
  -- manifest row carries none — 15 s, because the HP-decay economy
  -- starves without a food trickle (the user-requested mode).
  item_interval = 180,
  ai_cadence = 15,
  cull_radius = 120,
  -- The mutant mark, visible to observers without a mode-var read
  -- (16384 is the first bit past og.C.BIT_LAST's sentinel range).
  mutant_bit = 16384,
  -- Bot fill target when the manifest row carries no fighters count
  -- (rows 840-843 carry fighters = 4 — the pre-conversion feel).
  default_fighters = 4,
  -- One bot per free band slot, family cycled over the color index.
  bot_roster = { "core:soldier", "core:archer", "core:elf", "core:thief" },
}

local function mutant_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.MUTANT
end

local function score_of(c)
  return og.mode_get(S.SCORE + c)
end

local function add_score(c, delta)
  og.mode_set(S.SCORE + c, score_of(c) + delta)
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

-- Leader and runner-up color indices (-1 when absent): highest score,
-- ties to the LOWEST color index (ascending scan, strictly-greater
-- replacement) — the win tiebreak and the timeout ladder in one place.
local function standings(bitmap)
  local first = -1
  local second = -1
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if fighters.band_has(bitmap, c) then
      if first < 0 then
        first = c
      elseif score_of(c) > score_of(first) then
        second = first
        first = c
      elseif second < 0 then
        second = c
      elseif score_of(c) > score_of(second) then
        second = c
      end
    end
  end
  return first, second
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

-- The crown wears the competitor's ASSIGNED band byte (color slot c), not
-- whatever byte the walker happens to wear at the kill — a charmed killer
-- wears the charmer's byte, and the beacon/announce/scoring identity must
-- stay the slot's own.
local function crown(w, c)
  local team = fighters.band_byte(c)
  og.mode_set(S.MUTANT_ENTITY, og.entity_id(w))
  og.mode_set(S.MUTANT_TEAM, team)
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
  og.mode_set(S.MUTANT_TEAM, 0)
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
  local bitmap = og.mode_get(S.BAND_BITMAP)
  local victim_team = ent:team_num()
  local victim_fights = false
  if victim_team >= C.FFA_TEAM_BASE then
    if victim_team < C.FFA_TEAM_BASE + C.FFA_TEAM_COUNT then
      victim_fights = fighters.band_has(bitmap,
                                        victim_team - C.FFA_TEAM_BASE)
    end
  end

  -- Everyone respawns (Lua eligibility — the engine death scan does not
  -- run on scripted levels); summons and other owned walkers do not, so
  -- scrub the stain under them (D6). Scheduled synchronously so the
  -- seat-retention arm sees already_scheduled on the death tick.
  local eligible = victim_fights
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

  -- A competitor death: a registered band living that owns its life.
  local victim_competes = victim_fights
  if victim_competes then
    victim_competes = match.owns_its_life(ent)
  end
  -- Killer resolution rides the 48-tick stamp; the stamped TEAM survives
  -- a mutual kill. A scoring killer is a stamp inside the ASSIGNED band.
  local killer_scores = false
  if killer_team >= C.FFA_TEAM_BASE then
    if killer_team < C.FFA_TEAM_BASE + C.FFA_TEAM_COUNT then
      killer_scores = fighters.band_has(bitmap,
                                        killer_team - C.FFA_TEAM_BASE)
    end
  end

  local phase = og.mode_get(core.SLOT.PHASE)
  if phase == PHASE_FFA then
    -- First blood crowns the killer. Environment deaths (nil killer),
    -- suicides and summon deaths decide nothing, a killer who died
    -- with its victim cannot wear the crown, and only a REGISTERED
    -- competitor (its entity id in an IDS slot) may take it.
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
    local heir_slot = fighters.slot_of(og.entity_id(killer), S.IDS)
    if heir_slot == nil then
      return
    end
    crown(killer, heir_slot)
    return
  end
  if phase ~= PHASE_MUTANT then
    return
  end

  if is_mutant(ent) then
    -- The Mutant fell. A competitor kill scores +1 for the killer's slot
    -- (a mutual kill keeps the slot credit even when the killer handle
    -- is gone — the D3 recency channel resolves the team) and a LIVE
    -- registered killer inherits the mutancy plus the kill-heal;
    -- anything else (environment, decay, suicide) reverts the pool.
    local scored = false
    if killer_scores then
      if killer == nil then
        scored = true
      elseif killer ~= ent then
        scored = true
      end
    end
    if scored then
      add_score(killer_team - C.FFA_TEAM_BASE, 1)
    end
    de_mutate()
    local heir = nil
    local heir_slot = nil
    if killer ~= nil then
      if killer ~= ent then
        if killer_scores then
          if killer:dead() == 0 then
            heir_slot = fighters.slot_of(og.entity_id(killer), S.IDS)
            if heir_slot ~= nil then
              heir = killer
            end
          end
        end
      end
    end
    if heir ~= nil then
      crown(heir, heir_slot)
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
  local mutant_slot = og.mode_get(S.MUTANT_TEAM) - C.FFA_TEAM_BASE
  add_score(mutant_slot, 1)
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
  if mut == nil or mut:dead() ~= 0 then
    -- A kill that bypassed walker::death (a bare dead-flag write) left a
    -- stale crown; revert deterministically. Real deaths already ran the
    -- transfer machine before this phase.
    revert_to_ffa()
    return
  end
  apply_buffs(mut)
  -- The beacon tint is the ASSIGNED byte from MUTANT_TEAM, never the
  -- worn byte — a berserk-charmed mutant briefly wears a low byte the
  -- widened og.set_beacon would refuse.
  og.set_beacon(0, mut, og.mode_get(S.MUTANT_TEAM))
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
local function mutant_prey(livings, bitmap, mut)
  local team = mut:team_num()
  local nearest = fighters.nearest_fighter(livings, team, bitmap,
                                           mut:xpos(), mut:ypos())
  local cull = nil
  local in_radius = 0
  for k = 1, #livings do
    local w = livings[k]
    if fighters.foe_is_fighter(w, team, bitmap) then
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

local function run_director(livings, bitmap)
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
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if fighters.band_has(bitmap, c) then
      local member = og.find_by_id(og.mode_get(S.IDS + c))
      if member ~= nil then
        local team = fighters.band_byte(c)
        if ai.is_directable(member, team) then
          if mut ~= nil then
            if og.entity_id(member) == mutant_id then
              -- The bot Mutant hunts its prey; no retreat-to-heal (the
              -- only heal is killing).
              local prey = mutant_prey(livings, bitmap, member)
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
            -- non-band target the engine backstop handed out).
            if not fighters.foe_is_fighter(member:foe(), team, bitmap) then
              local target = fighters.nearest_fighter(livings, team, bitmap,
                                                      member:xpos(),
                                                      member:ypos())
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

local function run_win_check(bitmap, level_tick)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local winner = -1
  for c = 0, C.FFA_TEAM_COUNT - 1 do
    if winner < 0 then
      if fighters.band_has(bitmap, c) then
        if score_of(c) >= limit then
          winner = c
        end
      end
    end
  end
  if winner < 0 then
    if level_tick >= og.mode_get(S.TIME_LIMIT) then
      winner = standings(bitmap)
    end
  end
  if winner >= 0 then
    core.declare_team_win(fighters.band_byte(winner))
  end
end

-- Leader and runner-up rows: "TEAL 3/10", starred while that competitor
-- wears the crown ("LAVENDER 999/255 *" = 20 chars, inside the 25 budget),
-- tinted with the band byte.
local function hud_line_of(c, limit, mutant_team)
  local line = og.team_color_name(fighters.band_byte(c)) .. " " ..
               score_of(c) .. "/" .. limit
  if fighters.band_byte(c) == mutant_team then
    line = line .. " *"
  end
  return line
end

local function update_hud(bitmap)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local mutant_team = og.mode_get(S.MUTANT_TEAM)
  local first, second = standings(bitmap)
  if first >= 0 then
    og.set_hud_line(0, hud_line_of(first, limit, mutant_team),
                    fighters.band_byte(first))
  end
  if second >= 0 then
    og.set_hud_line(1, hud_line_of(second, limit, mutant_team),
                    fighters.band_byte(second))
  end
end

-- ---------------------------------------------------------------------------
-- Init
-- ---------------------------------------------------------------------------

local function on_mode_init(level)
  local obs = og.oblist()
  -- Competitors = every deployed character (bound first, cap 16), each on
  -- its own shuffled band byte (issue #187: two humans seated on one lobby
  -- team become separate mutually hostile competitors).
  local deployed = fighters.deploy(obs, C.FFA_TEAM_COUNT)
  fighters.assign(deployed, S.IDS, S.BAND_BITMAP)
  -- All four marker clusters are consumed (position pools here, not
  -- identities) and the whole authored score-range cast — livings and
  -- generators without a roster guy — retires: an empty active mask over
  -- the TDM strip semantics. Wildlife (bytes 4-7) is arena identity and
  -- stays; the reseated competitors are already out of the score range.
  match.consume_markers(obs, 15)
  match.strip_inactive_teams(obs, 0)

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

  -- Scatter the deployed competitors over the rotated pools so seat-team
  -- clusters never start stacked (init-time placement may teleport — the
  -- blessed RNG fallback).
  for k = 1, #deployed do
    fighters.place_rotated(deployed[k], S.ANCHOR_CURSOR, true)
  end

  -- Bot fill to the row's fighters count: one bot per free band slot.
  local target = T.default_fighters
  if row ~= nil then
    if row.fighters ~= nil then
      target = row.fighters
    end
  end
  target = og.clamp(target, 0, C.FFA_TEAM_COUNT)
  local count = fighters.fill_bots(#deployed, target, S.IDS, S.BAND_BITMAP,
                                   S.ANCHOR_CURSOR, T.bot_roster)
  if count < 2 then
    error("mutant: fewer than two competitors")
  end
  og.mode_set(S.FIGHTER_COUNT, count)

  og.set_mode_name("MUTANT")
  og.mode_set(S.ITEM_LAST, og.world_tick())
  og.mode_set(core.SLOT.PHASE, PHASE_FFA)
  -- The activation latch: MODE_ID written LAST (it also keeps the init
  -- bot spawns out of the on_entity_spawn adoption arm).
  og.mode_set(core.SLOT.MODE_ID, core.MODE.MUTANT)
  core.announce("MUTANT! FIRST TO " .. og.mode_get(S.SCORE_LIMIT), C.SOUND_CHARGE)
end

-- ---------------------------------------------------------------------------
-- Per-tick phases (post-act)
-- ---------------------------------------------------------------------------

local function on_mode_tick(level, tick)
  local obs = og.oblist()
  -- Durable origin marking (mode_match.owns_its_life's other half): a
  -- summon orphaned by its owner's death stays out of the score ledger
  -- and the respawn queue.
  match.mark_owned_lives(obs)
  fighters.adopt_new(obs, S.IDS, S.SCORE, S.BAND_BITMAP)
  local bitmap = og.mode_get(S.BAND_BITMAP)
  og.mode_set(S.FIGHTER_COUNT, fighters.band_count(bitmap))
  fighters.schedule_dead(S.IDS, S.BAND_BITMAP, og.mode_get(S.RESPAWN_TICKS))
  if og.mode_get(core.SLOT.PHASE) == PHASE_MUTANT then
    run_mutant_upkeep()
  end
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
  run_win_check(bitmap, tick)
  update_hud(bitmap)
end

-- Mid-join adoption rides the spawn dispatch AND the per-tick sweep. The
-- engine attaches a roster guy AFTER add_ob returns (create_team_walker),
-- so the just-spawned walker never reads has_guy here — this arm adopts
-- earlier joiners immediately and the on_mode_tick sweep lands this one
-- next tick.
local function on_entity_spawn(_ent)
  if not mutant_active() then
    return
  end
  fighters.adopt_new(og.oblist(), S.IDS, S.SCORE, S.BAND_BITMAP)
end

-- Engine handoff: the timer fire revived the walker in place — re-assert
-- the slot's band byte (D12: revive_player_walker clears real_team_num to
-- 255 but skips the team restore for band bytes, so a competitor that died
-- charmed would otherwise keep the charmer's byte), then reposition over
-- the rotated pools.
local function on_respawn(ent)
  if not mutant_active() then
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
  on_damage = on_damage,
  on_entity_death = on_entity_death,
  on_entity_spawn = on_entity_spawn,
  on_respawn = on_respawn,
}
