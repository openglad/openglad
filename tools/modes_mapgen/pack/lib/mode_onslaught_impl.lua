-- Onslaught rules + AI director — generator warfare (D5): the damage-gate flip (generators never die), zero-generator grace elimination, waypoint holds (spawn level + respawn bonuses), fire_frequency spawn caps, attacker/defender/holder roles (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local anchors = og.use("mode_anchors")
local caps = og.use("mode_caps")

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
}

local T = {
  no_gen_grace_ticks = 600, -- 50 s without a generator eliminates
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

-- The D5 flip: a generator never dies. A lethal hit from a living on an
-- active score team re-teams the intact generator (team + real team, full
-- HP) and cancels the damage; any other lethal source — environment,
-- weapons without a living owner, non-score attackers, the generator's
-- own team (no free self-heal) — is floored at 1 hp instead. The flip is
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
  if not flip then
    return og.max(og.trunc(hp) - 1, 0)
  end
  target:set_team_num(team)
  target:set_real_team_num(team)
  target.hp = target.max_hp
  og.emit_positional_sound(target, C.SOUND_MONEY)
  og.emit_notification(og.team_color_name(team) .. " TAKES A " .. generator_label(target:family()) .. "!")
  return false
end

-- The shared customize_spawn for the core generator families (registered
-- once, in scripts/mode_onslaught.lua): every capped scripted mode marks
-- its generator spawns for the cap census (soccer fields ally generators
-- too), and onslaught adds the held-waypoint spawn-level bonus.
local function customize_spawn(gen, spawn)
  local mode = og.mode_get(core.SLOT.MODE_ID)
  local marked = mode == core.MODE.ONSLAUGHT
  if not marked then
    marked = mode == core.MODE.SOCCER
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
-- cannot be score-farmed or cleric-raised (D6).
local function on_entity_death(ent, killer, killer_team)
  if not ons_active() then
    return
  end
  if ent:order() ~= C.ORDER_LIVING then
    return
  end
  if not ent:has_guy() then
    core.scrub_corpse(ent)
  end
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
              w:set_dead(1)
            end
          end
        end
      else
        local count = gen_count[team + 1]
        fset(S.GEN_COUNT, team, count)
        if count > 0 then
          fset(S.ZERO_SINCE, team, 0)
        elseif fget(S.ZERO_SINCE, team) == 0 then
          fset(S.ZERO_SINCE, team, now)
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

-- One row per team: generator count plus the held-waypoint tag, or OUT.
local function update_hud(gen_count)
  local mask = og.mode_get(S.TEAM_MASK)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      if fget(S.ELIMINATED, team) ~= 0 then
        og.set_hud_line(team, og.team_color_name(team) .. " OUT", team)
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
  local requested = og.match_setting("team_count")
  if requested <= 0 then
    requested = row.teams or 0
  end
  local mask = core.activate_teams(authored_mask, requested)
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
  run_waypoints(livings)
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
