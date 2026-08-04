-- Soccer rules + AI director — N-team ball game over manifest goal rects (D7): fixed-point ball physics (kick/shot impulses, wall bounces, contact damage, rolling spin), last-toucher scoring where a self-goal still puts a point on the board, submenu-honoring respawns, chaser/goalie roles (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local anchors = og.use("mode_anchors")
local caps = og.use("mode_caps")
local strip = og.use("mode_strip")

-- Mode-private slot map (8+; header 0-7 is mode_core.SLOT). Ball position
-- and velocity are x256 fixed point (DECISIONS D16: int32 mode vars).
local S = {
  SCORE_LIMIT = 8,
  RESPAWN_TICKS = 9,
  TEAM_COUNT = 10,
  TEAM_MASK = 11,
  TIME_LIMIT = 12,
  ANCHOR_CURSOR = 13,
  BALL_ENTITY = 14,
  BALL_PX = 15, -- ball CENTER x, x256
  BALL_PY = 16, -- ball CENTER y, x256
  BALL_VX = 17, -- px/tick, x256
  BALL_VY = 18, -- px/tick, x256
  LAST_TOUCH1 = 19, -- last-touching score team + 1, 0 = untouched
  LAST_KICKER = 20, -- entity id, 0 = none
  KICKOFF_UNTIL = 21, -- absolute world tick; impulses ignored while ahead
  KICKOFF_POS = 22, -- packed pixel center of the kickoff spot
  GOALS = 23, -- 23..26, +team
  GOAL_POS = 27, -- 27..30, +team, packed rect top-left (x, y)
  GOAL_SIZE = 31, -- 31..34, +team, packed (w, h)
  SPAWN_CAP = 35, -- 35..42, +team byte 0-7, -1 = uncapped
  STALL_SINCE = 43, -- world tick the dead-ball clock started, 0 = running play
  BALL_SPIN = 44, -- rolling-spin phase, 0..spin_cycle-1 (see run_spin)
  LAST_TOUCH2 = 45, -- last DISTINCT other score team + 1, 0 = none
}

local T = {
  -- Friction: a full kick is 8 px/tick = 2048 in x256 fixed point. A
  -- linear decay of F per tick stops it after 2048/F ticks; the 2.5-3 s
  -- target at 12 ticks/s is 30-36 ticks, so F = 64 stops a full kick in
  -- exactly 2048/64 = 32 ticks (~2.67 s).
  friction = 64,
  kick_radius = 12, -- Manhattan center distance that counts as touching
  hit_radius = 12, -- flight contact distance (walker half 8 + ball half 6, less a graze)
  kick_min_px = 2, -- melee kick floor, px/tick
  kick_max_px = 8, -- melee kick cap = the reference full kick
  shot_scale = 2, -- weapon impulse: 2 px/tick per point of weapon damage
  shot_min_px = 4,
  shot_max_px = 12,
  fast_fp = 512, -- 2 px/tick x256: at or below, contact deals no damage
  dmg_cap = 12, -- 1 damage per px/tick of L1 speed, capped (D7)
  restitution = 128, -- x256: the ball keeps half its speed off a walker
  substep_px = 8, -- max px per movement sub-step in the dominant axis
  kickoff_freeze = 36, -- ticks the ball ignores impulses after a reset
  dead_ball_ticks = 600, -- motionless + unattended this long resets to kickoff
  dead_ball_radius = 160, -- a live Living this close (L1) keeps the ball in play
  score_limit = 3,
  goal_score = 400,
  time_limit_ticks = 10800,
  respawn_ticks = 60,
  -- Rolling spin. sprites/ball.png is an 8-frame turn of the ball's patch
  -- lattice, so one full roll is 8 * spin_step = spin_cycle phase units
  -- (re-frame the sprite and spin_cycle has to move with it). A full kick
  -- (8 px/tick = 2048 in x256) advances speed/spin_divisor = 256 units a
  -- tick, i.e. exactly one frame per tick; a half-speed ball takes two
  -- ticks a frame, and a stopped ball holds the frame it landed on.
  spin_step = 256,
  spin_cycle = 2048,
  spin_divisor = 8,
  ai_cadence = 15,
  goalie_leash = 48,
  goalie_engage = 80, -- ball this close to the goal pulls the goalie out
  approach_offset = 20, -- chasers stand this far past the ball
}

-- Facing byte (FACE_UP = 0 .. FACE_UP_LEFT = 7, clockwise) to a unit step.
local FACING_X = { 0, 1, 1, 1, 0, -1, -1, -1 }
local FACING_Y = { -1, -1, 0, 1, 1, 1, 0, -1 }

-- Chaser lateral stagger (px) by oblist-order index mod 3.
local STAGGER = { 0, 16, -16 }

local function fget(base, i)
  return og.mode_get(base + i)
end

local function fset(base, i, v)
  og.mode_set(base + i, v)
end

local function soccer_active()
  return og.mode_get(core.SLOT.MODE_ID) == core.MODE.SOCCER
end

local function iabs(v)
  if v < 0 then
    return -v
  end
  return v
end

local function walker_center(w)
  local cx = w:xpos() + og.div(w:sizex(), 2)
  local cy = w:ypos() + og.div(w:sizey(), 2)
  return cx, cy
end

local function ball_center()
  local bx = og.div(og.mode_get(S.BALL_PX), 256)
  local by = og.div(og.mode_get(S.BALL_PY), 256)
  return bx, by
end

-- Move the ball entity so its center sits at pixel (cx, cy) and bank the
-- fixed-point center.
local function place_ball(ball, cx, cy)
  og.mode_set(S.BALL_PX, cx * 256)
  og.mode_set(S.BALL_PY, cy * 256)
  ball:setxy(cx - og.div(ball:sizex(), 2), cy - og.div(ball:sizey(), 2))
end

-- The design §6.3 kickoff backstop: an active team with zero live
-- Livings comes back at every kickoff, whatever the difficulty submenu
-- says — respawn Off cannot strand a team. Roster (guy) corpses persist
-- in the oblist and are scheduled for revival (on_respawn places them at
-- the team anchors); summoned corpses (owner, no guy) stay down. The
-- engine sweeps unscheduled AI corpses at the end of their death tick,
-- so a wiped bot side whose bodies are already gone is reprovisioned
-- with a fresh squad — the same spawn path init uses for empty active
-- teams — unless revives are already in flight.
local function revive_wiped_teams()
  local mask = og.mode_get(S.TEAM_MASK)
  local ticks = og.mode_get(S.RESPAWN_TICKS)
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
          anchors.spawn_bot_squad(team, S.ANCHOR_CURSOR)
        end
      end
    end
  end
end

-- Dead ball at the kickoff spot; impulses stay frozen for kickoff_freeze
-- ticks and the WHOLE touch history clears (an untouched goal scores
-- nothing, and no pre-reset toucher can be credited for a self-goal after
-- the re-spot). Every kickoff also runs the wiped-team revive backstop.
local function kickoff_reset(ball)
  local pos = og.mode_get(S.KICKOFF_POS)
  og.mode_set(S.BALL_VX, 0)
  og.mode_set(S.BALL_VY, 0)
  og.mode_set(S.LAST_TOUCH1, 0)
  og.mode_set(S.LAST_TOUCH2, 0)
  og.mode_set(S.LAST_KICKER, 0)
  og.mode_set(S.KICKOFF_UNTIL, og.world_tick() + T.kickoff_freeze)
  og.mode_set(S.STALL_SINCE, 0)
  -- A re-spotted ball is a dead ball: it rolls from a standstill on frame 0.
  og.mode_set(S.BALL_SPIN, 0)
  ball:set_frame(0)
  ball:set_team_num(C.SCORE_TEAM_COUNT)
  place_ball(ball, core.pos_x(pos), core.pos_y(pos))
  revive_wiped_teams()
end

-- L1-normalized impulse: |vx| + |vy| always sums to the commanded speed,
-- so the damage rule's px-per-tick reading works on the same norm.
local function set_ball_velocity(dx, dy, speed_px)
  local mag = iabs(dx) + iabs(dy)
  if mag == 0 then
    return
  end
  local fp = speed_px * 256
  og.mode_set(S.BALL_VX, og.div(fp * dx, mag))
  og.mode_set(S.BALL_VY, og.div(fp * dy, mag))
end

-- Touch bookkeeping: the ball wears the toucher's team color; only score
-- teams can be credited with a goal. A touch by a DIFFERENT score team
-- demotes the outgoing toucher into LAST_TOUCH2 — the side a self-goal
-- credits in a 3-4 team match. An uncredited impulse (a weapon with no
-- living owner) clears LAST_TOUCH1 only: an unowned shot does not erase
-- whoever forced the play.
local function stamp_toucher(ball, kicker_id, team)
  og.mode_set(S.LAST_KICKER, kicker_id)
  if team >= C.SCORE_TEAM_COUNT then
    og.mode_set(S.LAST_TOUCH1, 0)
    return
  end
  local previous = og.mode_get(S.LAST_TOUCH1)
  if previous > 0 then
    if previous - 1 ~= team then
      og.mode_set(S.LAST_TOUCH2, previous)
    end
  end
  og.mode_set(S.LAST_TOUCH1, team + 1)
  ball:set_team_num(team)
end

-- Walk-in kick: the first living (oblist order) touching the ball kicks it
-- along (ball - kicker), at a speed scaled by its melee damage. A kicker
-- standing dead center kicks along its own facing.
local function run_kicks(ball, livings)
  if og.world_tick() < og.mode_get(S.KICKOFF_UNTIL) then
    return false
  end
  local bx, by = ball_center()
  for k = 1, #livings do
    local kicker = livings[k]
    local kx, ky = walker_center(kicker)
    if iabs(bx - kx) + iabs(by - ky) <= T.kick_radius then
      local dx = bx - kx
      local dy = by - ky
      if dx == 0 then
        if dy == 0 then
          dx = FACING_X[kicker:curdir() + 1]
          dy = FACING_Y[kicker:curdir() + 1]
        end
      end
      local speed_px = og.clamp(og.trunc(kicker:damage()), T.kick_min_px, T.kick_max_px)
      set_ball_velocity(dx, dy, speed_px)
      stamp_toucher(ball, og.entity_id(kicker), kicker:team_num())
      og.emit_positional_sound(ball, C.SOUND_BOLT)
      return true
    end
  end
  return false
end

-- Weapon hit: the impulse follows the shot's own motion (lastx/lasty are
-- its per-tick step), at the stronger shot scale; the shooter becomes the
-- last toucher when a living owns the weapon.
local function run_shots(ball)
  if og.world_tick() < og.mode_get(S.KICKOFF_UNTIL) then
    return false
  end
  local bx, by = ball_center()
  local weapons = og.weaplist()
  for k = 1, #weapons do
    local shot = weapons[k]
    if shot:dead() == 0 then
      local sx, sy = walker_center(shot)
      if iabs(bx - sx) + iabs(by - sy) <= T.hit_radius then
        local mx = og.trunc(shot:lastx())
        local my = og.trunc(shot:lasty())
        if mx ~= 0 or my ~= 0 then
          local speed_px = og.clamp(og.trunc(shot:damage()) * T.shot_scale, T.shot_min_px, T.shot_max_px)
          set_ball_velocity(mx, my, speed_px)
          local shooter = shot:owner()
          local credited = false
          if shooter ~= nil then
            if shooter:order() == C.ORDER_LIVING then
              stamp_toucher(ball, og.entity_id(shooter), shooter:team_num())
              credited = true
            end
          end
          if not credited then
            stamp_toucher(ball, 0, C.SCORE_TEAM_COUNT)
          end
          og.emit_positional_sound(ball, C.SOUND_BOLT)
          return true
        end
      end
    end
  end
  return false
end

-- Flight: sub-stepped movement with axis-separated wall reflection, then
-- fast-contact damage (1 per px/tick, capped, ball:attack), then linear
-- friction. attack() draws RNG — the contact itself is the deterministic
-- input, so the draw order is stable.
local function run_flight(ball, livings)
  local vx = og.mode_get(S.BALL_VX)
  local vy = og.mode_get(S.BALL_VY)
  local speed = iabs(vx) + iabs(vy)
  if speed == 0 then
    return
  end
  local px = og.mode_get(S.BALL_PX)
  local py = og.mode_get(S.BALL_PY)
  local half_x = og.div(ball:sizex(), 2)
  local half_y = og.div(ball:sizey(), 2)
  local last_kicker = og.mode_get(S.LAST_KICKER)
  local steps = og.div(og.max(iabs(vx), iabs(vy)), T.substep_px * 256) + 1
  local bounced = false
  for _ = 1, steps do
    local nx = px + og.div(vx, steps)
    if not og.query_grid_passable(og.div(nx, 256) - half_x, og.div(py, 256) - half_y, ball) then
      vx = -vx
      nx = px + og.div(vx, steps)
      bounced = true
    end
    px = nx
    local ny = py + og.div(vy, steps)
    if not og.query_grid_passable(og.div(px, 256) - half_x, og.div(ny, 256) - half_y, ball) then
      vy = -vy
      ny = py + og.div(vy, steps)
      bounced = true
    end
    py = ny
    if speed > T.fast_fp then
      local bx = og.div(px, 256)
      local by = og.div(py, 256)
      local victim = nil
      for k = 1, #livings do
        local w = livings[k]
        if victim == nil then
          if og.entity_id(w) ~= last_kicker then
            local wx, wy = walker_center(w)
            if iabs(bx - wx) + iabs(by - wy) <= T.hit_radius then
              victim = w
            end
          end
        end
      end
      if victim ~= nil then
        ball:set_damage(og.min(og.div(speed, 256), T.dmg_cap))
        ball:attack(victim)
        vx = -og.div(vx * T.restitution, 256)
        vy = -og.div(vy * T.restitution, 256)
        og.emit_positional_sound(ball, C.SOUND_CLANG)
        break
      end
    end
  end
  if bounced then
    og.emit_positional_sound(ball, C.SOUND_CLANG)
  end
  speed = iabs(vx) + iabs(vy)
  local slowed = speed - T.friction
  if slowed <= 0 then
    vx = 0
    vy = 0
  else
    vx = og.div(vx * slowed, speed)
    vy = og.div(vy * slowed, speed)
  end
  og.mode_set(S.BALL_VX, vx)
  og.mode_set(S.BALL_VY, vy)
  og.mode_set(S.BALL_PX, px)
  og.mode_set(S.BALL_PY, py)
  ball:setxy(og.div(px, 256) - half_x, og.div(py, 256) - half_y)
end

-- Rolling spin: the drawn frame is a phase-derived index into the 8-frame
-- rotation strip. The phase advances each tick by the ball's own speed and
-- is signed by its direction of travel, so a ball flying right rolls the
-- frames forward (clockwise on screen) and one flying left rolls them
-- backward; a vertical-dominant flight takes its sign from vy instead.
-- Phase lives in a mode var and the frame lands on the walker's replicated
-- frame field, so a client mirror draws the server's frame without
-- re-deriving anything (mirrors never run animate()). A motionless ball
-- does not advance and holds the frame it stopped on.
local function run_spin(ball)
  local vx = og.mode_get(S.BALL_VX)
  local vy = og.mode_get(S.BALL_VY)
  local speed = iabs(vx) + iabs(vy)
  if speed == 0 then
    return
  end
  local dir = og.sign(vx)
  if iabs(vy) > iabs(vx) then
    dir = og.sign(vy)
  end
  local phase = og.mode_get(S.BALL_SPIN) + dir * og.div(speed, T.spin_divisor)
  -- og.mod is the C remainder (sign of the dividend), so a leftward roll
  -- lands negative and has to be folded back into 0..spin_cycle-1.
  phase = og.mod(phase, T.spin_cycle)
  if phase < 0 then
    phase = phase + T.spin_cycle
  end
  og.mode_set(S.BALL_SPIN, phase)
  ball:set_frame(og.div(phase, T.spin_step))
end

-- The one other active team in a two-team match; -1 when the mask holds
-- nobody else.
local function other_active_team(mask, team)
  for other = 0, C.SCORE_TEAM_COUNT - 1 do
    if other ~= team then
      if core.mask_has(mask, other) then
        return other
      end
    end
  end
  return -1
end

-- Who a self-goal by `team` credits. Two active teams: the single
-- opponent, unconditionally — with one other side on the pitch there is
-- no attribution question to ask. Three or four: the last DISTINCT other
-- toucher, the side that forced the play (a shot deflected in off a
-- defender is its goal in any reading of soccer). -1 = nobody has a claim,
-- which is the forfeit arm below.
local function own_goal_beneficiary(mask, team)
  if og.mode_get(S.TEAM_COUNT) == 2 then
    return other_active_team(mask, team)
  end
  local other = og.mode_get(S.LAST_TOUCH2) - 1
  if other < 0 then
    return -1
  end
  if other == team then
    return -1
  end
  if not core.mask_has(mask, other) then
    return -1
  end
  return other
end

-- Goal: ball center inside team T's defended rect scores for the last
-- toucher. A self-goal (the last toucher IS team T) still puts a point on
-- the board — this supersedes D7's own-goal-no-score arm: the beneficiary
-- above takes it, or, when no other team ever touched the ball, team T
-- forfeits one of its own. A ball nobody ever touched still scores
-- nothing: with no attribution there is no beneficiary. Either way the
-- ball returns to the kickoff spot.
local function run_goal_check(ball)
  local mask = og.mode_get(S.TEAM_MASK)
  local cx, cy = ball_center()
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      local pos = fget(S.GOAL_POS, team)
      local size = fget(S.GOAL_SIZE, team)
      local gx = core.pos_x(pos)
      local gy = core.pos_y(pos)
      if cx >= gx and cx < gx + core.pos_x(size) then
        if cy >= gy and cy < gy + core.pos_y(size) then
          local scorer1 = og.mode_get(S.LAST_TOUCH1)
          local scorer = scorer1 - 1
          if scorer1 <= 0 then
            core.announce("NO GOAL!", C.SOUND_YO)
          elseif scorer ~= team then
            fset(S.GOALS, scorer, fget(S.GOALS, scorer) + 1)
            og.award_score(scorer, T.goal_score)
            core.announce(og.team_color_name(scorer) .. " TEAM GOAL!", C.SOUND_MONEY)
          else
            local other = own_goal_beneficiary(mask, team)
            if other >= 0 then
              fset(S.GOALS, other, fget(S.GOALS, other) + 1)
              og.award_score(other, T.goal_score)
              core.announce("OWN GOAL! " .. og.team_color_name(other) .. " +1", C.SOUND_MONEY)
            else
              -- Forfeit. The goals var is the mode metric and takes the
              -- -1; the match score does NOT, because og.award_score
              -- takes an unsigned delta (bindings_entity.cpp casts to
              -- uint32 onto GameWorld::m_score), so a negative credit
              -- would wrap to ~4.29e9 and hand this team the timeout
              -- tiebreak it just lost a goal for.
              fset(S.GOALS, team, fget(S.GOALS, team) - 1)
              core.announce("OWN GOAL! " .. og.team_color_name(team) .. " -1", C.SOUND_YO)
            end
          end
          kickoff_reset(ball)
          return
        end
      end
    end
  end
end

-- The design §6.2.4 dead ball: a motionless ball with no live Living
-- within dead_ball_radius for dead_ball_ticks straight goes back to the
-- kickoff spot (which also runs the wiped-team revive backstop). Motion
-- or a nearby Living clears the stall clock.
local function run_dead_ball(ball, livings)
  local speed = iabs(og.mode_get(S.BALL_VX)) + iabs(og.mode_get(S.BALL_VY))
  if speed > 0 then
    og.mode_set(S.STALL_SINCE, 0)
    return
  end
  local bx, by = ball_center()
  for k = 1, #livings do
    local wx, wy = walker_center(livings[k])
    if iabs(bx - wx) + iabs(by - wy) <= T.dead_ball_radius then
      og.mode_set(S.STALL_SINCE, 0)
      return
    end
  end
  local now = og.world_tick()
  local since = og.mode_get(S.STALL_SINCE)
  if since == 0 then
    og.mode_set(S.STALL_SINCE, now)
    return
  end
  if now - since >= T.dead_ball_ticks then
    core.announce("BALL RESET", C.SOUND_YO)
    kickoff_reset(ball)
  end
end

-- Corpse eligibility mirrors the classic difficulty-submenu semantics the
-- engine applies on non-scripted maps (0 off, 1 heroes, 2 everyone,
-- 3 Team-1 heroes; ctf.cpp classic_respawn_corpse_eligible), minus the
-- spawn-point gate no binding exposes: an "everyone" AI corpse respawns
-- while it has no live owner.
local function run_death_scan(obs)
  local mode = og.match_setting("respawn_mode")
  if mode == 0 then
    return
  end
  local mask = og.mode_get(S.TEAM_MASK)
  local ticks = og.mode_get(S.RESPAWN_TICKS)
  for k = 1, #obs do
    local w = obs[k]
    if w:dead() ~= 0 then
      if w:order() == C.ORDER_LIVING then
        local team = anchors.score_team(w)
        if team < C.SCORE_TEAM_COUNT then
          if core.mask_has(mask, team) then
            local eligible = false
            if w:has_guy() then
              eligible = true
              if mode == 3 then
                eligible = team == 0
              end
            elseif mode == 2 then
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

local function on_respawn(ent)
  if not soccer_active() then
    return
  end
  anchors.place_at_anchor(ent, ent:team_num(), S.ANCHOR_CURSOR, false)
end

-- Score limit first; the time limit picks the leader by goals, then match
-- score; ties keep the lower team byte (the shared tiebreak rule).
local function run_win_check(level_tick)
  local mask = og.mode_get(S.TEAM_MASK)
  local limit = og.mode_get(S.SCORE_LIMIT)
  local winner = -1
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if winner < 0 then
      if core.mask_has(mask, team) then
        if fget(S.GOALS, team) >= limit then
          winner = team
        end
      end
    end
  end
  if winner < 0 then
    if level_tick >= og.mode_get(S.TIME_LIMIT) then
      for team = 0, C.SCORE_TEAM_COUNT - 1 do
        if core.mask_has(mask, team) then
          if winner < 0 then
            winner = team
          elseif fget(S.GOALS, team) > fget(S.GOALS, winner) then
            winner = team
          elseif fget(S.GOALS, team) == fget(S.GOALS, winner) then
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

-- 8-dir snap of a vector (components engage inside the 2:1 octant rule);
-- a zero vector snaps to +x so callers always get a direction.
local function dir8(dx, dy)
  local sx = 0
  local sy = 0
  if 2 * iabs(dx) >= iabs(dy) then
    sx = og.sign(dx)
  end
  if 2 * iabs(dy) >= iabs(dx) then
    sy = og.sign(dy)
  end
  if sx == 0 and sy == 0 then
    sx = 1
  end
  return sx, sy
end

local function goal_center(team)
  local pos = fget(S.GOAL_POS, team)
  local size = fget(S.GOAL_SIZE, team)
  local cx = core.pos_x(pos) + og.div(core.pos_x(size), 2)
  local cy = core.pos_y(pos) + og.div(core.pos_y(size), 2)
  return cx, cy
end

-- Role partition per team, in oblist order (zero RNG):
--   GOALIE   the member nearest the defended rect, leashed there (the CTF
--            defender idiom); a ball threatening the goal pulls it onto
--            the chaser approach point to clear.
--   CHASERS  everyone else walks the approach point: approach_offset px
--            past the ball on the line from the nearest enemy goal, so
--            walking into the ball kicks it goalward (D7 geometry), with
--            a lateral stagger so they do not stack.
-- Everyone leads on the ball entity, which suppresses the tick auto-foe
-- backstop; foes are re-cleared every cadence so brawls stay brief.
local function run_team_director(livings, ball, team)
  local members = {}
  for k = 1, #livings do
    if ai.is_directable(livings[k], team) then
      members[#members + 1] = livings[k]
    end
  end
  if #members == 0 then
    return
  end
  local bx, by = ball_center()
  local gcx, gcy = goal_center(team)

  local mask = og.mode_get(S.TEAM_MASK)
  local target_team = -1
  local target_distance = 0
  for enemy = 0, C.SCORE_TEAM_COUNT - 1 do
    if enemy ~= team then
      if core.mask_has(mask, enemy) then
        local ex, ey = goal_center(enemy)
        local distance = iabs(bx - ex) + iabs(by - ey)
        if target_team < 0 then
          target_team = enemy
          target_distance = distance
        elseif distance < target_distance then
          target_team = enemy
          target_distance = distance
        end
      end
    end
  end
  local ax = bx
  local ay = by
  local pdx = 0
  local pdy = 0
  if target_team >= 0 then
    local ex, ey = goal_center(target_team)
    local sx, sy = dir8(bx - ex, by - ey)
    ax = bx + sx * T.approach_offset
    ay = by + sy * T.approach_offset
    pdx = -sy
    pdy = sx
  end

  local assigned = {}
  for i = 1, #members do
    assigned[i] = false
  end
  local goalie_index = ai.nearest_unassigned(members, assigned, gcx, gcy)
  if goalie_index ~= nil then
    assigned[goalie_index] = true
    local goalie = members[goalie_index]
    goalie:set_foe(nil)
    goalie:set_leader(ball)
    if ai.dist_to(goalie, gcx, gcy) > T.goalie_leash then
      ai.issue_front(goalie, C.COMMAND_GOTO, 45, gcx, gcy)
    elseif iabs(bx - gcx) + iabs(by - gcy) <= T.goalie_engage then
      ai.issue_front(goalie, C.COMMAND_GOTO, 30, ax, ay)
    end
  end

  local chaser_index = 0
  for i = 1, #members do
    if not assigned[i] then
      local chaser = members[i]
      chaser:set_foe(nil)
      chaser:set_leader(ball)
      if ai.may_preempt(chaser:s_front_command()) then
        local stagger = STAGGER[og.mod(chaser_index, 3) + 1]
        ai.issue_front(chaser, C.COMMAND_GOTO, 30, ax + pdx * stagger, ay + pdy * stagger)
      end
      chaser_index = chaser_index + 1
    end
  end
end

local function run_director(livings, ball)
  local mask = og.mode_get(S.TEAM_MASK)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      run_team_director(livings, ball, team)
    end
  end
end

-- One score row per team; beacon slot 0 tracks the ball for the HUD and
-- radar channels.
local function update_hud(ball)
  local mask = og.mode_get(S.TEAM_MASK)
  local limit = og.mode_get(S.SCORE_LIMIT)
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      core.hud_score_line(team, team, fget(S.GOALS, team), limit)
    end
  end
  og.set_beacon(0, ball)
end

-- Lazy init, first scripted tick. Raising reports the failed-init shape:
-- the engine latches inactive and classic rules own the level from the
-- next tick (the CTF discipline).
local function on_mode_init(level, row)
  if row == nil then
    error("soccer: no manifest row for level " .. level)
  end
  -- Active teams: anchor-marker teams (the versus authoring contract)
  -- clamped by the lobby request; the manifest team count is the default
  -- request.
  local authored_mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if og.respawn_anchor_count(team) > 0 then
      authored_mask = core.mask_add(authored_mask, team)
    end
  end
  local requested = og.match_setting("team_count")
  if requested <= 0 then
    requested = row.teams or 0
  end
  local mask = core.activate_teams(authored_mask, requested)
  local active_count = core.mask_count(mask)
  if active_count < 2 then
    error("soccer: fewer than two anchor teams")
  end
  if row.goal_rects == nil then
    error("soccer: manifest row has no goal_rects")
  end
  if row.kickoff == nil then
    error("soccer: manifest row has no kickoff")
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if core.mask_has(mask, team) then
      local rect = row.goal_rects[team]
      if rect == nil then
        error("soccer: no goal rect for team " .. team)
      end
      fset(S.GOAL_POS, team, core.pos_pack(rect.x, rect.y))
      fset(S.GOAL_SIZE, team, core.pos_pack(rect.w, rect.h))
    end
  end
  og.mode_set(S.KICKOFF_POS, core.pos_pack(row.kickoff.x, row.kickoff.y))
  og.mode_set(S.TEAM_MASK, mask)
  og.mode_set(S.TEAM_COUNT, active_count)

  local limit = T.score_limit
  local requested_limit = og.match_setting("score_limit")
  if requested_limit > 0 then
    limit = requested_limit
  elseif row.score_limit ~= nil then
    if row.score_limit > 0 then
      limit = row.score_limit
    end
  end
  og.mode_set(S.SCORE_LIMIT, og.clamp(limit, 1, 255))
  local respawn_ticks = og.match_setting("respawn_ticks")
  if respawn_ticks <= 0 then
    respawn_ticks = T.respawn_ticks
  end
  og.mode_set(S.RESPAWN_TICKS, respawn_ticks)
  og.mode_set(S.TIME_LIMIT, row.time_limit or T.time_limit_ticks)
  caps.bank_caps(row, S.SPAWN_CAP)

  -- Consume the active teams' start markers (anchors are already
  -- recorded engine-side) and strip inactive score teams' livings,
  -- generators and markers; roster walkers are never stripped.
  local obs = og.oblist()
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
        if keep then
          if order == C.ORDER_SPECIAL then
            w:set_dead(1)
          end
        elseif not w:has_guy() then
          w:set_dead(1)
        end
      end
    end
  end
  -- Roster-only armies on request (shared rule set), before the census below
  -- so bot backfill sees the post-strip world.
  strip.strip_authored_troops(mask, nil)

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
        anchors.spawn_bot_squad(team, S.ANCHOR_CURSOR)
      end
    end
  end

  -- og.add_ob, not og.add_fx_ob: the fx list never acts, while an acting
  -- ball dispatches the family on_act whose true return keeps the engine
  -- from moving, animating or expiring it (the cloud precedent).
  local ball_family = og.family_id("effect", "modes:ball")
  local ball = og.add_ob("effect", ball_family)
  if ball == nil then
    error("soccer: cannot spawn the ball")
  end
  og.mode_set(S.BALL_ENTITY, og.entity_id(ball))
  kickoff_reset(ball)

  og.set_mode_name("SOCCER")
  og.mode_set(core.SLOT.PHASE, 1)
  -- The activation latch: written LAST, so hooks observe a fully
  -- initialized match or nothing.
  og.mode_set(core.SLOT.MODE_ID, core.MODE.SOCCER)
  core.announce("SOCCER! FIRST TO " .. og.mode_get(S.SCORE_LIMIT), C.SOUND_CHARGE)
end

-- Per-tick phases (post-act; the engine already ran the respawn timers
-- and owns the win-latch short-circuit).
local function on_mode_tick(level, tick)
  local obs = og.oblist()
  run_death_scan(obs)
  local livings = {}
  local gens = {}
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
      end
    end
  end
  local ball = og.find_by_id(og.mode_get(S.BALL_ENTITY))
  if ball ~= nil then
    local kicked = run_kicks(ball, livings)
    if not kicked then
      run_shots(ball)
    end
    run_flight(ball, livings)
    run_spin(ball)
    run_goal_check(ball)
    run_dead_ball(ball, livings)
  end
  if og.mod(og.world_tick(), T.ai_cadence) == 0 then
    caps.apply_caps(gens, spawn_count, S.SPAWN_CAP)
    if ball ~= nil then
      run_director(livings, ball)
    end
  end
  run_win_check(tick)
  update_hud(ball)
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
  }
end

return {
  S = S,
  T = T,
  make_hooks = make_hooks,
}
