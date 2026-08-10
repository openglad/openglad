-- Onslaught rules + AI director — generator warfare (D5): the damage-gate flip (generators never die), zero-generator grace elimination, waypoint holds (spawn level + respawn bonuses), fire_frequency spawn caps, attacker/defender/holder roles (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local anchors = og.use("mode_anchors")
local caps = og.use("mode_caps")
local match = og.use("mode_match")
local strip = og.use("mode_strip")

-- Mode-private slot map (8+; header 0-7 is mode_core.SLOT).
local S = {
  RESPAWN_TICKS = 8,
  TEAM_MASK = 9,
  TEAM_COUNT = 10,
  TIME_LIMIT = 11,
  ANCHOR_CURSOR = 12,
  GEN_COUNT = 13, -- 13..16, +team: live-generator census (the HUD mirror)
  ZERO_SINCE = 17, -- 17..20, +team: world tick the census hit zero, 0 = has some
  ELIMINATED = 21, -- 21..24, +team
  WP_COUNT = 25,
  WP_ENTITY = 26, -- 26..29, +wp
  WP_POS = 30, -- 30..33, +wp, packed (x, y)
  WP_OWNER1 = 34, -- 34..37, +wp, holder + 1, 0 = neutral
  WP_PROG_TEAM1 = 38, -- 38..41, +wp, contending team + 1, 0 = none
  WP_PROGRESS = 42, -- 42..45, +wp
  SPAWN_CAP = 46, -- 46..53, +team byte 0-7, -1 = uncapped
  FLIP_GUARD = 54, -- 54..63, +og.div(rank, 2): two packed 15-bit last-flip
  -- records per slot, keyed by the generator's rank among live generators
  -- in entity-id order (the set is fixed after init — flips never kill).
}

local T = {
  -- Scoring (modes.md §5.5/§5.8): m_score through og.award_score. Taking a
  -- generator is the match; a waypoint is a tenth of one; bodies are small
  -- change, with a hero worth five gen-spawns.
  gen_capture_score = 300,
  wp_capture_score = 50,
  spawn_kill_score = 10,
  hero_kill_score = 50,
  no_gen_grace_ticks = 600, -- 50 s without a generator eliminates
  flip_guard_ticks = 96, -- a fresh flip cannot flip again for 8 s (B6)
  wp_radius_px = 48, -- the hold disc (Euclidean, the CP geometry)
  wp_hold_ticks = 36, -- majority ticks to take a waypoint
  respawn_ticks = 120,
  time_limit_ticks = 14400,
  ai_cadence = 15,
  defend_radius = 96, -- defender leash around its generator
  attack_adjacent = 48, -- set_foe on the target generator inside this
  defender_ratio_div = 4, -- ceil(remaining / 4) defend
}

-- The capturable generator families (D16 ground truth) and their flip
-- labels; an unknown family reads as POST.
local GEN_NAMES = { "core:tent", "core:tower", "core:bones", "core:treehouse" }
local GEN_LABELS = { "TENT", "TOWER", "BONES", "TREEHOUSE" }

local function fget(base, i)
  return og.mode_get(base + i)
end

local function fset(base, i, v)
  og.mode_set(base + i, v)
end

local function ons_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.ONSLAUGHT
end

local function team_holds_waypoint(team)
  local count = og.mode_get(S.WP_COUNT)
  for i = 0, count - 1 do
    if fget(S.WP_OWNER1, i) - 1 == team then
      return true
    end
  end
  return false
end

local function generator_label(family)
  for k = 1, #GEN_NAMES do
    if og.family_id("generator", GEN_NAMES[k]) == family then
      return GEN_LABELS[k]
    end
  end
  return "POST"
end

-- Post-flip guard records (B6): last-flip world ticks per generator, two
-- 15-bit entries per FLIP_GUARD slot. Record = og.mod(tick, GUARD_WINDOW)
-- + 1, 0 = never flipped; the window (16384) exceeds every shipped time
-- limit (14400), so elapsed comparisons never wrap within a match.
-- Generators past 2 * GUARD_SLOTS ranks (no shipped map has more than 12)
-- degrade gracefully to unguarded.
local GUARD_SLOTS = 10
local GUARD_WINDOW = 16384

-- The target's rank among live generators in entity-id order — stable for
-- the whole match because the D5 flip keeps every generator alive.
local function generator_rank(target)
  local id = og.entity_id(target)
  local rank = 0
  local obs = og.oblist()
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      if w:order() == C.ORDER_GENERATOR then
        if og.entity_id(w) < id then
          rank = rank + 1
        end
      end
    end
  end
  return rank
end

local function guard_read(rank)
  local slot = og.div(rank, 2)
  if slot >= GUARD_SLOTS then
    return 0
  end
  local packed = fget(S.FLIP_GUARD, slot)
  if og.mod(rank, 2) == 0 then
    return og.mod(packed, 32768)
  end
  return og.div(packed, 32768)
end

local function guard_write(rank, record)
  local slot = og.div(rank, 2)
  if slot >= GUARD_SLOTS then
    return
  end
  local packed = fget(S.FLIP_GUARD, slot)
  local lo = og.mod(packed, 32768)
  local hi = og.div(packed, 32768)
  if og.mod(rank, 2) == 0 then
    lo = record
  else
    hi = record
  end
  fset(S.FLIP_GUARD, slot, hi * 32768 + lo)
end

local function flip_guarded(rank)
  local record = guard_read(rank)
  if record == 0 then
    return false
  end
  local elapsed = og.mod(og.world_tick() - (record - 1), GUARD_WINDOW)
  return elapsed < T.flip_guard_ticks
end

-- The D5 flip: a generator never dies. A lethal hit from a living on an
-- active score team re-teams the intact generator (team + real team, full
-- HP) and cancels the damage; any other lethal source — environment,
-- weapons without a living owner, non-score attackers, the generator's
-- own team (no free self-heal) — is floored at 1 hp instead. A generator
-- that flipped within the last flip_guard_ticks shares that floor arm
-- (no re-flip, no notification: the B6 ping-pong killer). At 1 hp or
-- below the floor arm cancels outright: a floor verdict of 0 is a
-- REPLACEMENT amount to the engine, not a cancel, and walker::attack's
-- own hp <= 0 death check would turn it into a kill on a 0-hp shell
-- (the INVESTIGATION-3 explode-instead-of-flip chain). The flip is
-- the only ownership transition, so the unproven death-path resurrect
-- never runs.
local function on_damage(target, attacker, amount)
  if not ons_active() then
    return nil
  end
  if target:order() ~= C.ORDER_GENERATOR then
    return nil
  end
  local hp = target:s_hitpoints()
  if amount < hp then
    return nil
  end
  local team = -1
  if attacker ~= nil then
    if attacker:order() == C.ORDER_LIVING then
      team = attacker:team_num()
    end
  end
  local flip = false
  if team >= 0 and team < C.SCORE_TEAM_COUNT then
    if core.mask_has(og.mode_get(S.TEAM_MASK), team) then
      flip = team ~= target:team_num()
    end
  end
  local rank = -1
  if flip then
    rank = generator_rank(target)
    if flip_guarded(rank) then
      flip = false
    end
  end
  if not flip then
    if og.trunc(hp) <= 1 then
      return false
    end
    return og.trunc(hp) - 1
  end
  guard_write(rank, og.mod(og.world_tick(), GUARD_WINDOW) + 1)
  target:set_team_num(team)
  target:set_real_team_num(team)
  target.hp = target.max_hp
  og.award_score(team, T.gen_capture_score)
  og.emit_positional_sound(target, C.SOUND_MONEY)
  og.emit_notification(og.team_color_name(team) .. " TAKES A " .. generator_label(target:family()) .. "!")
  return false
end

-- The shared customize_spawn for the core generator families (registered
-- once, in scripts/mode_onslaught.lua): every capped scripted mode marks
-- its generator spawns for the cap census (soccer fields ally generators
-- too), and onslaught adds the held-waypoint spawn-level bonus.
--
-- TDM marks for a different reason: the mark is its durable proof that a
-- walker's life came from a generator, which outlives the owner link the
-- engine severs when the generator dies (mode_match.owns_its_life).
local function customize_spawn(gen, spawn)
  local mode = og.mode_get(core.SLOT.MODE_ID)
  local marked = mode == core.MODE.ONSLAUGHT
  if not marked then
    marked = mode == core.MODE.SOCCER
  end
  if not marked then
    marked = mode == core.MODE.TDM
  end
  if not marked then
    return
  end
  caps.mark_spawn(spawn)
  if mode ~= core.MODE.ONSLAUGHT then
    return
  end
  local team = gen:team_num()
  if team < C.SCORE_TEAM_COUNT then
    if team_holds_waypoint(team) then
      local bonus_level = spawn:s_level() + 1
      spawn:s_set_level(bonus_level)
      spawn:set_difficulty(bonus_level)
    end
  end
end

-- Hero corpses respawn (delay halved at SCHEDULE time while their team
-- holds a waypoint); AI reinforcements come only from generators, so AI
-- corpses never enter the queue.
local function run_death_scan(obs)
  local mask = og.mode_get(S.TEAM_MASK)
  local ticks = og.mode_get(S.RESPAWN_TICKS)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() ~= 0 then
      if w:order() == C.ORDER_LIVING then
        if w:has_guy() then
          local team = anchors.score_team(w)
          if team < C.SCORE_TEAM_COUNT then
            if core.mask_has(mask, team) then
              if fget(S.ELIMINATED, team) == 0 then
                if not og.respawn_pending(w) then
                  local delay = ticks
                  if team_holds_waypoint(team) then
                    delay = og.max(1, og.div(delay, 2))
                  end
                  og.respawn_schedule(w, delay)
                end
              end
            end
          end
        end
      end
    end
  end
end

-- Non-hero corpses never respawn here: scrub the fresh stain so they
-- cannot be score-farmed or cleric-raised (D6). Kills also pay out (§5.5):
-- a gen-spawn is small change next to a hero, and both are small change
-- next to a generator.
local function on_entity_death(ent, killer, killer_team)
  if not ons_active() then
    return
  end
  if ent:order() ~= C.ORDER_LIVING then
    return
  end
  local hero = ent:has_guy()
  if not hero then
    core.scrub_corpse(ent)
  end
  -- No-pay arms: environment and suicide, killers off the active mask, and
  -- teamkills (the shared no-reward-for-friendly-fire rule).
  if killer == nil then
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
  if not core.mask_has(og.mode_get(S.TEAM_MASK), killer_team) then
    return
  end
  if killer_team == anchors.score_team(ent) then
    return
  end
  local award = T.spawn_kill_score
  if hero then
    award = T.hero_kill_score
  end
  og.award_score(killer_team, award)
end

local function on_respawn(ent)
  if not ons_active() then
    return
  end
  anchors.place_at_anchor(ent, ent:team_num(), S.ANCHOR_CURSOR, false)
end

local function eliminate_team(team, livings)
  fset(S.ELIMINATED, team, 1)
  fset(S.GEN_COUNT, team, 0)
  core.announce(og.team_color_name(team) .. " FALLS!", C.SOUND_ROAR)
end

-- Zero live generators starts the grace clock; holding out the full grace
-- without recapturing one eliminates the team. The sweep runs descending
-- so a same-tick double KO spares the lowest team byte (the shared
-- tiebreak direction). Elimination is terminal: the team's livings die
-- with it, and late respawn-queue revives are re-killed here every tick.
local function run_elimination(livings, gen_count)
  local mask = og.mode_get(S.TEAM_MASK)
  local now = og.world_tick()
  for team = C.SCORE_TEAM_COUNT - 1, 0, -1 do
    if core.mask_has(mask, team) then
      if fget(S.ELIMINATED, team) ~= 0 then
        for k = 1, #livings do
          local w = livings[k]
          if w:dead() == 0 then
            if w:team_num() == team then
              -- A bare dead-flag write, so walker::death never runs and
              -- on_entity_death never scrubs: do it here, or an eliminated
              -- team leaves a field of stains to raise and farm.
              w:set_dead(1)
              core.scrub_corpse(w)
            end
          end
        end
      else
        local count = gen_count[team + 1]
        fset(S.GEN_COUNT, team, count)
        if count > 0 then
          fset(S.ZERO_SINCE, team, 0)
        elseif fget(S.ZERO_SINCE, team) == 0 then
          -- The clock starts here, and this branch IS the once: it is the
          -- 0 -> now transition of a replicated var, so the warning fires
          -- exactly one tick per grace window — and fires again after a
          -- recapture clears the var — with no Lua-side flag (R6).
          fset(S.ZERO_SINCE, team, now)
          core.announce(og.team_color_name(team) .. " HAS NO ENGINES!", C.SOUND_YO)
        elseif now - fget(S.ZERO_SINCE, team) >= T.no_gen_grace_ticks then
          local survivors = 0
          for other = 0, C.SCORE_TEAM_COUNT - 1 do
            if core.mask_has(mask, other) then
              if fget(S.ELIMINATED, other) == 0 then
                survivors = survivors + 1
              end
            end
          end
          if survivors > 1 then
            eliminate_team(team, livings)
          end
        end
      end
    end
  end
end

-- Simplified CP hold (the 48px majority rule): the strict-majority team
-- accrues one progress per tick toward its meter; losing the majority
-- clears the meter (no CTF-style owner decay); a full meter flips the
-- holder and re-teams the waypoint entity.
local function run_waypoints(livings)
  local mask = og.mode_get(S.TEAM_MASK)
  local count = og.mode_get(S.WP_COUNT)
  for i = 0, count - 1 do
    local pos = fget(S.WP_POS, i)
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
          if dx * dx + dy * dy <= T.wp_radius_px * T.wp_radius_px then
            present[team + 1] = present[team + 1] + 1
            total = total + 1
          end
        end
      end
    end
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
    local owner = fget(S.WP_OWNER1, i) - 1
    if contender >= 0 and contender ~= owner then
      if fget(S.WP_PROG_TEAM1, i) ~= contender + 1 then
        fset(S.WP_PROG_TEAM1, i, contender + 1)
        fset(S.WP_PROGRESS, i, 0)
      end
      local progress = fget(S.WP_PROGRESS, i) + 1
      fset(S.WP_PROGRESS, i, progress)
      if progress >= T.wp_hold_ticks then
        fset(S.WP_OWNER1, i, contender + 1)
        fset(S.WP_PROGRESS, i, 0)
        fset(S.WP_PROG_TEAM1, i, 0)
        local wp = og.find_by_id(fget(S.WP_ENTITY, i))
        if wp ~= nil then
          wp:set_team_num(contender)
        end
        og.award_score(contender, T.wp_capture_score)
        core.announce(og.team_color_name(contender) .. " HOLDS WAYPOINT!", C.SOUND_MONEY)
      end
    elseif fget(S.WP_PROGRESS, i) ~= 0 then
      fset(S.WP_PROGRESS, i, 0)
      fset(S.WP_PROG_TEAM1, i, 0)
    end
  end
end

local function leader_is_generator(leader)
  return leader:order() == C.ORDER_GENERATOR
end

-- Role partition per team, in oblist order (zero RNG):
--   HOLDERS    one member per waypoint the team does not hold, walking it.
--   DEFENDERS  ceil(remaining / 4), round-robin over own generators
--              (generator leader = the anti-backstop), leashed back past
--              the defend radius.
--   ATTACKERS  the rest: walk the nearest enemy (or neutral) generator;
--              adjacency sets it as the foe so the melee starts.
local function run_team_director(livings, gens, team)
  local members = {}
  for k = 1, #livings do
    if ai.is_directable(livings[k], team) then
      members[#members + 1] = livings[k]
    end
  end
  if #members == 0 then
    return
  end
  local own = {}
  local enemy = {}
  for k = 1, #gens do
    local gen = gens[k]
    if gen:team_num() == team then
      own[#own + 1] = gen
    else
      enemy[#enemy + 1] = gen
    end
  end

  local assigned = {}
  for i = 1, #members do
    assigned[i] = false
  end
  local wp_count = og.mode_get(S.WP_COUNT)
  for i = 0, wp_count - 1 do
    if fget(S.WP_OWNER1, i) - 1 ~= team then
      local pos = fget(S.WP_POS, i)
      local best = ai.nearest_unassigned(members, assigned, core.pos_x(pos), core.pos_y(pos))
      if best ~= nil then
        assigned[best] = true
        local holder = members[best]
        ai.clear_stale_leader(holder, leader_is_generator)
        holder:set_foe(nil)
        ai.issue_front(holder, C.COMMAND_GOTO, 45, core.pos_x(pos), core.pos_y(pos))
      end
    end
  end

  local remaining = {}
  for i = 1, #members do
    if not assigned[i] then
      remaining[#remaining + 1] = members[i]
    end
  end
  local defender_count = 0
  if #own > 0 then
    defender_count = og.div(#remaining + T.defender_ratio_div - 1, T.defender_ratio_div)
  end
  for i = 1, defender_count do
    local defender = remaining[i]
    local post = own[og.mod(i - 1, #own) + 1]
    defender:set_leader(post)
    if ai.dist_to(defender, post:xpos(), post:ypos()) > T.defend_radius then
      ai.issue_front(defender, C.COMMAND_GOTO, 45, post:xpos(), post:ypos())
    end
  end
  for i = defender_count + 1, #remaining do
    local attacker = remaining[i]
    ai.clear_stale_leader(attacker, leader_is_generator)
    local target = nil
    local target_distance = 0
    for k = 1, #enemy do
      local distance = ai.dist_to(attacker, enemy[k]:xpos(), enemy[k]:ypos())
      if target == nil then
        target = enemy[k]
        target_distance = distance
      elseif distance < target_distance then
        target = enemy[k]
        target_distance = distance
      end
    end
    if target ~= nil then
      if target_distance <= T.attack_adjacent then
        attacker:set_foe(target)
        ai.issue_front(attacker, C.COMMAND_SEARCH, 120, 0, 0)
      elseif ai.may_preempt(attacker:s_front_command()) then
        ai.issue_front(attacker, C.COMMAND_GOTO, 60, target:xpos(), target:ypos())
      end
    end
  end
end

local function run_director(livings, gens)
  local mask = og.mode_get(S.TEAM_MASK)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if fget(S.ELIMINATED, team) == 0 then
        run_team_director(livings, gens, team)
      end
    end
  end
end

-- Last team standing wins; the time limit picks the leader by live
-- generators, then match score; ties keep the lower team byte (the
-- shared tiebreak rule).
local function run_win_check(level_tick, gen_count)
  local mask = og.mode_get(S.TEAM_MASK)
  local survivors = 0
  local last_team = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if fget(S.ELIMINATED, team) == 0 then
        survivors = survivors + 1
        if last_team < 0 then
          last_team = team
        end
      end
    end
  end
  if survivors == 1 then
    core.declare_team_win(last_team)
    return
  end
  if level_tick >= og.mode_get(S.TIME_LIMIT) then
    local winner = -1
    for team = 0, C.SCORE_TEAM_COUNT - 1 do
      if core.mask_has(mask, team) then
        if fget(S.ELIMINATED, team) == 0 then
          if winner < 0 then
            winner = team
          elseif gen_count[team + 1] > gen_count[winner + 1] then
            winner = team
          elseif gen_count[team + 1] == gen_count[winner + 1] then
            if og.team_score(team) > og.team_score(winner) then
              winner = team
            end
          end
        end
      end
    end
    if winner >= 0 then
      core.declare_team_win(winner)
    end
  end
end

-- Whole seconds left on a grace clock that started at `since`, rounded up:
-- the row reads 1s through the whole final second and reaches 0s only in
-- the one shape run_elimination declines to eliminate (the last team
-- standing, whose clock runs past the deadline for the tick it takes the
-- win), which is what the clamp is for.
local TICKS_PER_SECOND = 12

local function grace_seconds(since)
  local remaining = og.max(0, T.no_gen_grace_ticks - (og.world_tick() - since))
  return og.div(remaining + TICKS_PER_SECOND - 1, TICKS_PER_SECOND)
end

-- One row per team: generator count plus the held-waypoint tag, the
-- recapture countdown while the team is on the grace clock, or OUT. Every
-- row renders from replicated state (run_elimination refreshed GEN_COUNT
-- and ZERO_SINCE earlier this tick), so peers draw identical rows.
local function update_hud(gen_count)
  local mask = og.mode_get(S.TEAM_MASK)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if fget(S.ELIMINATED, team) ~= 0 then
        og.set_hud_line(team, og.team_color_name(team) .. " OUT", team)
      elseif fget(S.ZERO_SINCE, team) ~= 0 then
        -- "YELLOW OUT IN 50s" — 17 chars, inside the 25-byte row.
        local left = grace_seconds(fget(S.ZERO_SINCE, team))
        og.set_hud_line(team, og.team_color_name(team) .. " OUT IN " .. left .. "s", team)
      else
        local text = og.team_color_name(team) .. " " .. gen_count[team + 1] .. " GEN"
        if team_holds_waypoint(team) then
          text = text .. " +WP"
        end
        og.set_hud_line(team, text, team)
      end
    end
  end
end

-- Lazy init, first scripted tick. Raising reports the failed-init shape:
-- the engine latches inactive and classic rules own the level from the
-- next tick (the CTF discipline).
local function on_mode_init(level, row)
  if row == nil then
    error("onslaught: no manifest row for level " .. level)
  end
  -- Authored teams: a score team is in the match when it fields a live
  -- generator or a living at init.
  local obs = og.oblist()
  local authored_mask = 0
  local gen_count = { 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      local order = w:order()
      if order == C.ORDER_LIVING then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          authored_mask = core.mask_add(authored_mask, team)
        end
      elseif order == C.ORDER_GENERATOR then
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          authored_mask = core.mask_add(authored_mask, team)
          gen_count[team + 1] = gen_count[team + 1] + 1
        end
      end
    end
  end
  -- TROOPS:OWN with deployed rosters: the rosters are the match (the
  -- shared rule); otherwise the lobby request (manifest default) over the
  -- authored generator/living teams.
  local mask = match.own_roster_activation(authored_mask, obs)
  if mask == nil then
    -- Normalized request: Auto (raw <= 0) means no numeric clamp — matched
    -- POWER is out of scope for Onslaught entirely (D17).
    local requested = core.team_count_request()
    if requested <= 0 then
      requested = row.teams or 0
    end
    mask = core.activate_teams(authored_mask, requested)
  end
  local active_count = core.mask_count(mask)
  if active_count < 2 then
    error("onslaught: fewer than two teams")
  end
  og.mode_set(S.TEAM_MASK, mask)
  og.mode_set(S.TEAM_COUNT, active_count)

  -- Waypoints from the authored markers (fxlist order, up to 4).
  local point_family = og.family_id("treasure", "modes:waypoint")
  local wp_count = 0
  local fxlist = og.fxlist()
  for k = 1, #fxlist do
    local e = fxlist[k]
    if e:dead() == 0 then
      if e:order() == C.ORDER_TREASURE then
        if e:family() == point_family then
          if wp_count >= 4 then
            e:set_dead(1)
          else
            fset(S.WP_ENTITY, wp_count, og.entity_id(e))
            fset(S.WP_POS, wp_count, core.pos_pack(e:xpos(), e:ypos()))
            wp_count = wp_count + 1
          end
        end
      end
    end
  end
  og.mode_set(S.WP_COUNT, wp_count)

  -- Consume the active teams' start markers (anchors are already
  -- recorded engine-side) and strip inactive score teams' livings,
  -- generators and markers; roster walkers and the neutral bytes (4-7,
  -- the capturable neutral generators) are never stripped.
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
          if core.mask_has(mask, team) then
            if order == C.ORDER_SPECIAL then
              w:set_dead(1)
            end
          elseif not w:has_guy() then
            w:set_dead(1)
          end
        end
      end
    end
  end
  -- Roster-only armies on request; keep_generators because the foundries ARE
  -- the board here -- neutral bytes included, they are all capturable.
  strip.strip_authored_troops({ keep_generators = true })

  caps.bank_caps(row, S.SPAWN_CAP)
  local respawn_ticks = og.match_setting("respawn_ticks")
  if respawn_ticks <= 0 then
    respawn_ticks = T.respawn_ticks
  end
  og.mode_set(S.RESPAWN_TICKS, respawn_ticks)
  og.mode_set(S.TIME_LIMIT, row.time_limit or T.time_limit_ticks)

  -- Grace clocks for active teams starting without a generator.
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      fset(S.GEN_COUNT, team, gen_count[team + 1])
      if gen_count[team + 1] == 0 then
        fset(S.ZERO_SINCE, team, og.world_tick())
      end
    end
  end

  og.set_mode_name("ONSLAUGHT")
  og.mode_set(core.SLOT.PHASE, 1)
  -- The activation latch: written LAST, so hooks observe a fully
  -- initialized match or nothing.
  og.mode_set(core.SLOT.MODE_ID, core.MODE.ONSLAUGHT)
  core.announce("ONSLAUGHT! HOLD THE LINE", C.SOUND_CHARGE)
end

-- Per-tick phases (post-act; the engine already ran the respawn timers
-- and owns the win-latch short-circuit).
local function on_mode_tick(level, tick)
  local obs = og.oblist()
  run_death_scan(obs)
  local livings = {}
  local gens = {}
  local gen_count = { 0, 0, 0, 0 }
  local spawn_count = { 0, 0, 0, 0, 0, 0, 0, 0 }
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() == 0 then
      local order = w:order()
      if order == C.ORDER_LIVING then
        livings[#livings + 1] = w
        if caps.is_marked_spawn(w) then
          local team = w:team_num()
          if team < 8 then
            spawn_count[team + 1] = spawn_count[team + 1] + 1
          end
        end
      elseif order == C.ORDER_GENERATOR then
        gens[#gens + 1] = w
        local team = w:team_num()
        if team < C.SCORE_TEAM_COUNT then
          gen_count[team + 1] = gen_count[team + 1] + 1
        end
      end
    end
  end
  run_elimination(livings, gen_count)
  -- run_elimination kills the eliminated team's members in place, so the
  -- census above is stale by one tick: re-derive before the waypoint hold
  -- or the just-killed still contest their team's disc.
  local contenders = {}
  for k = 1, #livings do
    local w = livings[k]
    if w:dead() == 0 then
      contenders[#contenders + 1] = w
    end
  end
  run_waypoints(contenders)
  if og.mod(og.world_tick(), T.ai_cadence) == 0 then
    caps.apply_caps(gens, spawn_count, S.SPAWN_CAP)
    run_director(livings, gens)
  end
  run_win_check(tick, gen_count)
  update_hud(gen_count)
end

-- One hook table per level, closing over its manifest row (static
-- authored data, read-only — not the mutable upvalue state R6 bans).
local function make_hooks(row)
  return {
    on_mode_init = function(level)
      on_mode_init(level, row)
    end,
    on_mode_tick = on_mode_tick,
    on_respawn = on_respawn,
    on_damage = on_damage,
    on_entity_death = on_entity_death,
  }
end

return {
  S = S,
  T = T,
  make_hooks = make_hooks,
  customize_spawn = customize_spawn,
}
