-- Soccer rules + AI director — N-team ball game over manifest goal rects (D7): fixed-point ball physics (kick/shot impulses, wall bounces, contact damage, rolling spin), last-toucher scoring where a self-goal still puts a point on the board, always-on respawns (a ball game never strands a side), chaser/goalie roles (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")
local ai = og.use("mode_ai")
local anchors = og.use("mode_anchors")
local caps = og.use("mode_caps")
local items = og.use("mode_items")
local match = og.use("mode_match")
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
  ITEM_CURSOR = 46, -- lib/mode_items pad rotation cursor (#225)
  ITEM_LAST = 47, -- world tick of the last item spawn, seeded at init
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
  -- Respawning pickups (lib/mode_items): fallback interval when the
  -- manifest row carries none. mode_items refills ONE pad per interval
  -- whatever the pad count, so the interval tracks mouths fed, not pads:
  -- a pitch fields 24-48 livings (12 markers per team over 2-4 teams,
  -- plus 823's skeletons), the campaign's heaviest headcount, so soccer
  -- sits at the FFA/Mutant floor of 180 rather than TDM's 300 (#225).
  item_interval = 180,
  -- Keeper geometry. The intercept line sits goalie_standoff px in front
  -- of the goal mouth's open face; the defensive box is the defended rect
  -- grown by goalie_box on every side — a ball inside it is charged and
  -- cleared by contact. goalie_leash bounds every commanded destination
  -- (Manhattan from the rect center): it must cover the worst intercept,
  -- rect_halfthickness + standoff + halfspan = 16 + 16 + 64 = 96 on the
  -- shipped 32x128 / 128x32 mouths, so 112 leaves a step of slack while
  -- still pinning the keeper to its goal.
  goalie_leash = 112,
  goalie_standoff = 16,
  goalie_box = 64,
  approach_offset = 20, -- chasers stand this far past the ball
  -- Chaser drive-through. The approach point above is a STAGING spot: it
  -- parks the chaser approach_offset px from the ball, outside the 12 px
  -- contact radius, so a chaser that reaches it stands next to the ball
  -- and never kicks (the milling bug). A chaser already goal-side of the
  -- ball is therefore commanded THROUGH it instead — at the ball's own
  -- center, drive_offset px further on toward the attacked goal. Two
  -- numbers make that land: the GOTO destination is the walker's
  -- TOP-LEFT corner, so the drive target carries a half-body correction
  -- (the contact kick reads centers), and GOTO counts itself arrived
  -- within 12 px, which for a target on the ball is inside the contact
  -- radius. drive_reach is the miss distance ai.chaser_drives' corridor
  -- test budgets for; drive_cross bounds the lateral miss a chaser may
  -- start a drive from, widened to drive_cross_hold once it is committed.
  -- This whole table is the geometry argument ai.chaser_drives reads.
  drive_offset = 8,
  drive_cross = 24,
  drive_cross_hold = 32,
  drive_reach = 10,
}

-- The shared scalar helpers, bound to locals at load time: soccer's L1
-- geometry calls them on every walker, every tick.
local iabs = core.iabs
local walker_center = core.walker_center

-- Chaser lateral stagger (px) by support index mod 3 — the striker takes
-- no stagger, so the fan-out starts at the second chaser.
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
  match.revive_wiped_teams(anchors, og.mode_get(S.TEAM_MASK), og.mode_get(S.RESPAWN_TICKS), S.ANCHOR_CURSOR)
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
          -- Dead-center: kick along the kicker's facing. Core-family
          -- specials (soldier whirlwind, archer volley) park curdir at
          -- the -1 sentinel while their command burst drains; a
          -- facing-less kicker punts FACE_DOWN deterministically.
          local fdir = kicker:curdir() + 1
          dx = ai.FACING_X[fdir]
          dy = ai.FACING_Y[fdir]
          if dx == nil then
            dx = 0
            dy = 1
          end
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

-- Issue #219: a ball entering an AUTHORED goal mouth whose team is not in
-- the match must not sit there silently — mapgen paints every authored
-- mouth and activation is decided at match time, so a dead mouth looks
-- live. Announce the closed goal and re-spot at the kickoff. The
-- KICKOFF_UNTIL guard is the rate limit: at most one announcement per
-- freeze window, even if authored geometry parks the kickoff inside a
-- closed mouth. Init validated row and row.goal_rects (this hook never
-- runs after a failed init), and inactive rects are deliberately NOT
-- banked in mode vars (the GOAL_POS == 0 contract stands).
local function run_closed_goal_check(ball, row, mask, cx, cy)
  if og.world_tick() < og.mode_get(S.KICKOFF_UNTIL) then
    return
  end
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if not core.mask_has(mask, team) then
      local rect = row.goal_rects[team]
      if rect ~= nil then
        if cx >= rect.x and cx < rect.x + rect.w then
          if cy >= rect.y and cy < rect.y + rect.h then
            core.announce(og.team_color_name(team) .. " GOAL CLOSED!", C.SOUND_YO)
            kickoff_reset(ball)
            return
          end
        end
      end
    end
  end
end

-- Goal: ball center inside team T's defended rect scores for the last
-- toucher. A self-goal (the last toucher IS team T) still puts a point on
-- the board — this supersedes D7's own-goal-no-score arm: the beneficiary
-- above takes it, or, when no other team ever touched the ball, team T
-- forfeits one of its own. A ball nobody ever touched still scores
-- nothing: with no attribution there is no beneficiary. Either way the
-- ball returns to the kickoff spot. A ball in an authored-but-CLOSED
-- mouth takes the announce-and-reset arm above instead.
local function run_goal_check(ball, row)
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
  run_closed_goal_check(ball, row, mask, cx, cy)
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

local function goal_center(team)
  local pos = fget(S.GOAL_POS, team)
  local size = fget(S.GOAL_SIZE, team)
  local cx = core.pos_x(pos) + og.div(core.pos_x(size), 2)
  local cy = core.pos_y(pos) + og.div(core.pos_y(size), 2)
  return cx, cy
end

-- The ball-intercept point on team T's goal mouth: the ball's cross-axis
-- coordinate clamped to the defended rect's span, on a line goalie_standoff
-- px in front of the open face. Wide rects (w >= h) are N/S walls whose
-- mouth opens vertically toward the kickoff spot; tall rects are E/W walls
-- opening horizontally — which covers the shipped pitches' E/W mouths and
-- FOURSQUARE's four walls alike.
local function goal_intercept(team, bx, by)
  local pos = fget(S.GOAL_POS, team)
  local size = fget(S.GOAL_SIZE, team)
  local gx = core.pos_x(pos)
  local gy = core.pos_y(pos)
  local gw = core.pos_x(size)
  local gh = core.pos_y(size)
  local kpos = og.mode_get(S.KICKOFF_POS)
  if gw >= gh then
    local line_y = gy - T.goalie_standoff
    if core.pos_y(kpos) >= gy + og.div(gh, 2) then
      line_y = gy + gh + T.goalie_standoff
    end
    return og.clamp(bx, gx, gx + gw), line_y
  end
  local line_x = gx - T.goalie_standoff
  if core.pos_x(kpos) >= gx + og.div(gw, 2) then
    line_x = gx + gw + T.goalie_standoff
  end
  return line_x, og.clamp(by, gy, gy + gh)
end

-- Ball inside team T's defensive box: the defended rect grown by
-- goalie_box on every side.
local function ball_in_goal_box(team, bx, by)
  local pos = fget(S.GOAL_POS, team)
  local size = fget(S.GOAL_SIZE, team)
  local gx = core.pos_x(pos)
  local gy = core.pos_y(pos)
  if bx < gx - T.goalie_box then
    return false
  end
  if bx > gx + core.pos_x(size) + T.goalie_box then
    return false
  end
  if by < gy - T.goalie_box then
    return false
  end
  return by <= gy + core.pos_y(size) + T.goalie_box
end

-- Role partition per team, in oblist order (zero RNG):
--   GOALIE   the member nearest the defended rect — assigned only when
--            the team fields two or more (a singleton squad is all
--            striker, matched-teams D38). Its objective every
--            cadence is the ball-intercept point on its own goal mouth
--            (goal_intercept above), so it shadows the ball's cross-axis
--            from in front of the mouth and a shot on goal runs into it;
--            a ball inside the defensive box is charged directly —
--            contact kicks fire along (ball - kicker), so a goal-side
--            keeper clears automatically. The leash bounds the charge: a
--            box ball beyond it is played from the intercept line, so
--            every commanded destination keeps the keeper at the mouth.
--   STRIKER  the remaining member nearest the ball. It is re-commanded
--            onto the ball EVERY cadence whatever it was doing, so an
--            engagement can never hold on to the squad's ball player, and
--            it plays two-phase (ai.chaser_drives): the approach point
--            while it is wrong-side or badly aligned, the drive through
--            the ball once it is goal-side and lined up.
--   CHASERS  everyone else walks the approach point: approach_offset px
--            past the ball on the line from the nearest enemy goal, so
--            walking into the ball kicks it goalward (D7 geometry), with
--            a lateral stagger so they do not stack — and they take the
--            same two-phase treatment when a drive is on.
-- Engagement: at most ONE non-striker may hold an active combat command
-- (the earliest in oblist order — the front-command preemption discipline
-- decides what counts). It keeps its foe and its orders, so a squad can
-- still chase an attacker off its half; every further chaser is pulled
-- back onto the ball. Ball play beats brawling, one defender at a time.
-- Everyone else leads on the ball entity, which suppresses the tick
-- auto-foe backstop; foes are re-cleared every cadence so brawls stay
-- brief.
local function run_team_director(livings, ball, team)
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
  -- The drive axis is the approach direction reversed: ball -> goal.
  local dsx = 0
  local dsy = 0
  if target_team >= 0 then
    local ex, ey = goal_center(target_team)
    local sx, sy = ai.dir8(bx - ex, by - ey)
    ax = bx + sx * T.approach_offset
    ay = by + sy * T.approach_offset
    pdx = -sy
    pdy = sx
    dsx = -sx
    dsy = -sy
  end

  local assigned = {}
  for i = 1, #members do
    assigned[i] = false
  end
  -- The small-team rule (matched-teams D38, general to every whittled
  -- squad): a sole member plays STRIKER, never goalkeeper — a lone keeper
  -- camps its mouth and can never score, so a 1v1 would stall for the
  -- whole clock. Defense degrades gracefully: the striker plays the ball
  -- wherever it lies, and a touch near its own goal clears goalward (the
  -- approach-point geometry). Two members keep goalie + striker.
  -- The gate is the TEAM's live count, not the directable count: a lone
  -- BOT beside a live human teammate keeps the mouth — the human can
  -- score, so the desperation reading does not apply (review finding on
  -- mixed human+bot teams; is_directable excludes player walkers).
  if #members > 1 or team_live > #members then
    local goalie_index = ai.nearest_unassigned(members, assigned, gcx, gcy)
    if goalie_index ~= nil then
      assigned[goalie_index] = true
      local goalie = members[goalie_index]
      goalie:set_foe(nil)
      goalie:set_leader(ball)
      local tx, ty = goal_intercept(team, bx, by)
      if ball_in_goal_box(team, bx, by) then
        if iabs(bx - gcx) + iabs(by - gcy) <= T.goalie_leash then
          tx = bx
          ty = by
        end
      end
      ai.issue_front(goalie, C.COMMAND_GOTO, 30, tx, ty)
    end
  end

  local striker = ai.nearest_unassigned(members, assigned, bx, by)
  local peeled = false
  local support_index = 0
  for i = 1, #members do
    if not assigned[i] then
      local chaser = members[i]
      -- The one permitted peel: a non-striker already in combat keeps its
      -- foe and its orders. Everyone else is re-commanded onto the ball,
      -- preemption discipline or not.
      local peel = false
      if i ~= striker then
        if not ai.may_preempt(chaser:s_front_command()) then
          peel = not peeled
          peeled = true
        end
      end
      if not peel then
        local stagger = 0
        if i ~= striker then
          support_index = support_index + 1
          stagger = STAGGER[og.mod(support_index, 3) + 1]
        end
        local tx = ax + pdx * stagger
        local ty = ay + pdy * stagger
        local drives = false
        if target_team >= 0 then
          drives = ai.chaser_drives(chaser, bx, by, dsx, dsy, T)
        end
        if drives then
          -- Through the ball. GOTO drives the walker's top-left corner,
          -- so the half-body correction is what puts its CENTER — the
          -- point the contact kick measures from — on the ball.
          tx = bx + dsx * T.drive_offset - og.div(chaser:sizex(), 2)
          ty = by + dsy * T.drive_offset - og.div(chaser:sizey(), 2)
        end
        chaser:set_foe(nil)
        chaser:set_leader(ball)
        ai.issue_front(chaser, C.COMMAND_GOTO, 30, tx, ty)
      end
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

-- The pure PLAN phase (on_mode_plan): authored domain (anchor-marker
-- teams — the versus authoring contract, read from the engine anchor
-- census), activation, per-team fills and the resolved score limit.
-- Runs under the plan-dispatch fence (no world access) at launch AND at
-- picker preview time; on_mode_init below EXECUTES the returned plan.
local function on_mode_plan(level, inputs, row)
  if row == nil then
    error("soccer: no manifest row for level " .. level)
  end
  local authored_mask = 0
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    if inputs.teams[team + 1].anchors > 0 then
      authored_mask = core.mask_add(authored_mask, team)
    end
  end
  -- TROOPS:OWN/FAIR (rosters or all-bot alike): the lobby request wins
  -- the COUNT — roster teams plus authored backfill (issue #218), with
  -- TEAMS: Auto resolving to the authored count (2026-08-18 directive).
  -- Only TROOPS:ALL falls through to the lobby request (manifest default
  -- at Auto) over the authored anchor teams.
  local mask, starts, matched, matched_size =
      match.plan_activation(inputs, authored_mask, row.teams or 0)
  local limit = T.score_limit
  if inputs.score_limit > 0 then
    limit = inputs.score_limit
  elseif row.score_limit ~= nil then
    if row.score_limit > 0 then
      limit = row.score_limit
    end
  end
  local teams, seeded = match.plan_fills(inputs, mask, {
    matched = matched,
    matched_size = matched_size,
  })
  local reason = nil
  if not starts then
    reason = "soccer: fewer than two anchor teams"
  end
  return {
    mode = "SOCCER",
    starts = starts,
    reason = reason,
    authored_mask = authored_mask,
    active_mask = mask,
    matched = matched,
    matched_size = matched_size,
    score_limit = og.clamp(limit, 1, 255),
    seeded_squads = seeded,
    teams = teams,
  }
end

-- Lazy init, first scripted tick — EXECUTES the chained plan (activation,
-- fills and the score limit are the plan's; the world writes, strips and
-- spawns are this apply's). Raising reports the failed-init shape: the
-- engine latches inactive and classic rules own the level from the next
-- tick (the CTF discipline).
local function on_mode_init(level, plan, row)
  if row == nil then
    error("soccer: no manifest row for level " .. level)
  end
  if plan == nil then
    error("soccer: no match plan for level " .. level)
  end
  if not plan.starts then
    error(plan.reason)
  end
  local mask = plan.active_mask
  local active_count = core.mask_count(mask)
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

  -- Score limit is the PLAN's (request > manifest row > default, clamped);
  -- the respawn/time knobs resolve here.
  og.mode_set(S.SCORE_LIMIT, plan.score_limit)
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
  -- FAIR banking (the plan decided matched-ness and the headcount; the
  -- power TARGET is measured here, D24) before any squad spawn reads it.
  match.bank_match_target(plan, obs)
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
  -- Roster-only armies on request, before the plan's squad fills below,
  -- so the spawns land in the final world.
  strip.strip_authored_troops(nil)

  -- Bot squads where the plan said so (the empty active teams).
  for team = 0, C.SCORE_TEAM_COUNT - 1 do
    local fill = plan.teams[team + 1].fill
    if fill == "bots" or fill == "matched" then
      anchors.spawn_bot_squad(team, S.ANCHOR_CURSOR)
    end
  end

  -- og.add_ob, not og.add_fx_ob: the fx list never acts, while an acting
  -- ball dispatches the family on_act whose true return keeps the engine
  -- from moving, animating or expiring it (the cloud precedent).
  local ball_family = og.family_id("effect", "modes:ball") --[[@as integer]] -- the pack declares modes:ball; a nil would be a load bug and add_ob erroring loudly is the right failure
  local ball = og.add_ob("effect", ball_family)
  if ball == nil then
    error("soccer: cannot spawn the ball")
  end
  og.mode_set(S.BALL_ENTITY, og.entity_id(ball))
  kickoff_reset(ball)

  og.set_mode_name("SOCCER")
  og.mode_set(S.ITEM_LAST, og.world_tick())
  og.mode_set(core.SLOT.PHASE, 1)
  -- The activation latch: written LAST, so hooks observe a fully
  -- initialized match or nothing.
  og.mode_set(core.SLOT.MODE_ID, core.MODE.SOCCER)
  core.announce("SOCCER! FIRST TO " .. og.mode_get(S.SCORE_LIMIT), C.SOUND_CHARGE)
end

-- Per-tick phases (post-act; the engine already ran the respawn timers
-- and owns the win-latch short-circuit). row is the static manifest row
-- make_hooks closed over — the goal check reads its authored goal_rects
-- for the closed-mouth arm (issue #219).
local function on_mode_tick(level, tick, row)
  local obs = og.oblist()
  match.schedule_dead(obs, og.mode_get(S.TEAM_MASK), og.mode_get(S.RESPAWN_TICKS))
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
    run_goal_check(ball, row)
    run_dead_ball(ball, livings)
  end
  if og.mod(og.world_tick(), T.ai_cadence) == 0 then
    caps.apply_caps(gens, spawn_count, S.SPAWN_CAP)
    if ball ~= nil then
      run_director(livings, ball)
    end
  end
  -- Post-death-scan, pre-HUD (the mode_items contract). The row arrives
  -- through the make_hooks closure, not mode_levels: unit fixtures
  -- register synthetic rows that never enter the manifest module.
  items.run(row, S.ITEM_CURSOR, S.ITEM_LAST, T.item_interval)
  run_win_check(tick)
  update_hud(ball)
end

-- One hook table per level, closing over its manifest row (static
-- authored data, read-only — not the mutable upvalue state R6 bans).
local function make_hooks(row)
  return {
    on_mode_plan = function(level, inputs)
      return on_mode_plan(level, inputs, row)
    end,
    on_mode_init = function(level, plan)
      on_mode_init(level, plan, row)
    end,
    on_mode_tick = function(level, tick)
      on_mode_tick(level, tick, row)
    end,
    on_respawn = on_respawn,
  }
end

return {
  S = S,
  T = T,
  make_hooks = make_hooks,
}
