-- modes director toolkit — eligibility, front-command + preemption discipline, Manhattan nearest with oblist-order ties, facing steps, 8-dir snap, the drive-through corridor, stale-leader hygiene (transcribed from the C++ CTF director; cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C
local core = og.use("mode_core")

-- Facing byte (FACE_UP = 0 .. FACE_UP_LEFT = 7, clockwise) to a unit step.
-- The walker's own replicated facing is the only direction memory the
-- director is allowed (R6 bans script-side mutable state), so both the
-- dead-center contact rule and the drive hysteresis below read it here.
local FACING_X = { 0, 1, 1, 1, 0, -1, -1, -1 }
local FACING_Y = { -1, -1, 0, 1, 1, 1, 0, -1 }

-- Directable: a live, unfrozen AI living on the team. Player-controlled
-- walkers (ACT_CONTROL or a bound user) are never touched.
local function is_directable(w, team)
  if w:dead() ~= 0 then
    return false
  end
  if w:order() ~= C.ORDER_LIVING then
    return false
  end
  if w:team_num() ~= team then
    return false
  end
  if w:act_type() == C.ACT_CONTROL then
    return false
  end
  if w:user() ~= -1 then
    return false
  end
  return w:s_frozen_delay() <= 0
end

-- Refresh the front of the queue in place when the wanted command type is
-- already active; otherwise force-insert it. Re-issuing every cadence this
-- way keeps the role current without growing the command queue.
local function issue_front(w, commandtype, count, a, b)
  if w:s_front_command() == commandtype then
    w:s_refresh_front(count, a, b)
  else
    w:s_force_command(commandtype, count, a, b)
  end
end

-- Preemption discipline: only an idle queue, a self-issued foe search, a
-- stale goto, or a follow may be preempted; active combat commands are
-- left alone.
local function may_preempt(front_type)
  if front_type == 0 then
    return true
  end
  if front_type == C.COMMAND_SEARCH then
    return true
  end
  if front_type == C.COMMAND_GOTO then
    return true
  end
  return front_type == C.COMMAND_FOLLOW
end

-- Manhattan distance to a point, matching walker::distance_to_ob's metric
-- so role decisions agree with the chase logic they hand off to.
local function dist_to(w, x, y)
  local dx = w:xpos() - x
  if dx < 0 then
    dx = -dx
  end
  local dy = w:ypos() - y
  if dy < 0 then
    dy = -dy
  end
  return dx + dy
end

-- 8-dir snap of a vector (components engage inside the 2:1 octant rule);
-- a zero vector snaps to +x so callers always get a direction.
local function dir8(dx, dy)
  local sx = 0
  local sy = 0
  if 2 * core.iabs(dx) >= core.iabs(dy) then
    sx = og.sign(dx)
  end
  if 2 * core.iabs(dy) >= core.iabs(dx) then
    sy = og.sign(dy)
  end
  if sx == 0 and sy == 0 then
    sx = 1
  end
  return sx, sy
end

-- Nearest unassigned member to (x, y): Manhattan distance, ties resolve to
-- the earlier oblist slot. Returns the members index, or nil when everyone
-- is assigned.
local function nearest_unassigned(members, assigned, x, y)
  local best = nil
  local best_distance = 0
  for i = 1, #members do
    if not assigned[i] then
      local distance = dist_to(members[i], x, y)
      if best == nil then
        best = i
        best_distance = distance
      elseif distance < best_distance then
        best = i
        best_distance = distance
      end
    end
  end
  return best
end

-- Drive geometry, in the CENTER frame the contact rule reads. (bx, by) is
-- the objective the mover has to touch — a ball, a loose flag — and
-- (sx, sy) is the 8-dir step from it toward whatever the mover is driving
-- it at. `along` is how far the objective sits down that axis from the
-- mover — positive means the mover is behind it, so contact from here
-- carries the objective onward — and `cross` is the mover's lateral miss
-- off that line.
local function drive_geometry(mover, bx, by, sx, sy)
  local cx, cy = core.walker_center(mover)
  local dx = bx - cx
  local dy = by - cy
  return dx * sx + dy * sy, core.iabs(dy * sx - dx * sy)
end

-- Drive THROUGH the objective (true) or walk a staging point (false)?
-- A staging point parks the mover beside the objective, outside the 12 px
-- contact radius, so a mover that reaches it stands there and never
-- touches — the milling bug. Three conditions license the drive instead:
--   goal-side  along > 0, so the touch leaves in the wanted direction;
--   corridor   a lateral miss within geom.drive_cross, widened to
--              geom.drive_cross_hold for a mover already facing that way.
--              That facing is the hysteresis memory, and it is replicated
--              walker state written by the mover's own last step, never
--              script-side storage (R6): an objective drifting across the
--              line cannot flip a committed mover back to the staging
--              point every cadence, while one turned away has to
--              re-qualify on the tight corridor;
--   reach      the straight line from here to the drive target has to
--              pass close enough to touch. That target sits
--              geom.drive_offset px beyond the objective, so the line
--              crosses the objective's own plane cross * drive_offset /
--              (along + drive_offset) px wide — the integer form below —
--              and a drive that would sail past is refused rather than
--              milling.
-- All three regions shrink along the commanded path (both along and cross
-- fall as the mover closes, and the reach test is a ray from the drive
-- target), so a mover that starts a drive holds it until it has gone
-- through. `geom` is the calling mode's tuning table: it must carry
-- drive_offset, drive_cross, drive_cross_hold and drive_reach.
local function chaser_drives(mover, bx, by, sx, sy, geom)
  local along, cross = drive_geometry(mover, bx, by, sx, sy)
  if along <= 0 then
    return false
  end
  local dir = mover:curdir() + 1
  local tol = geom.drive_cross
  -- Core-family specials (soldier whirlwind, archer volley) park curdir
  -- at the -1 sentinel while their command burst drains; that mover has
  -- no facing, so it earns no hold bonus and qualifies on the tight
  -- corridor.
  local fx = FACING_X[dir]
  if fx ~= nil then
    if fx * sx + FACING_Y[dir] * sy > 0 then
      tol = geom.drive_cross_hold
    end
  end
  if cross > tol then
    return false
  end
  return cross * geom.drive_offset <= geom.drive_reach * (along + geom.drive_offset)
end

-- Director-set leaders outlive their roles: a carrier's mode-entity leader
-- and an escort's carrier-walker leader both suppress the tick auto-foe
-- backstop. Clear them when the walker takes a non-carrier, non-escort
-- role. Player leaders from a yell are never the director's and stay
-- untouched. leader_is_mode_entity is the mode's own predicate (CTF: "is
-- this a flag entity").
local function clear_stale_leader(w, leader_is_mode_entity)
  local leader = w:leader()
  if leader == nil then
    return
  end
  if leader_is_mode_entity(leader) then
    w:set_leader(nil)
    return
  end
  if leader:dead() ~= 0 then
    return
  end
  if leader:order() ~= C.ORDER_LIVING then
    return
  end
  if leader:user() ~= -1 then
    return
  end
  if leader:team_num() == w:team_num() then
    w:set_leader(nil)
  end
end

return {
  FACING_X = FACING_X,
  FACING_Y = FACING_Y,
  is_directable = is_directable,
  issue_front = issue_front,
  may_preempt = may_preempt,
  dist_to = dist_to,
  dir8 = dir8,
  nearest_unassigned = nearest_unassigned,
  drive_geometry = drive_geometry,
  chaser_drives = chaser_drives,
  clear_stale_leader = clear_stale_leader,
}
