-- modes director toolkit — eligibility, front-command + preemption discipline, Manhattan nearest with oblist-order ties, stale-leader hygiene (transcribed from the C++ CTF director; cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

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
  is_directable = is_directable,
  issue_front = issue_front,
  may_preempt = may_preempt,
  dist_to = dist_to,
  nearest_unassigned = nearest_unassigned,
  clear_stale_leader = clear_stale_leader,
}
