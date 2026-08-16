-- westlands.fire lib: fronts — the split-party derivations the camp composes from (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- Every camp state after the Falls is DERIVED here, never stored: the front
-- of each road is the first level of it the company has not cleared, the
-- muster is a census of the oath tags the roster carries, and the split is
-- frozen exactly while a road level is behind a company that swore both
-- roads. Nothing in this module writes anything (og.use exports are frozen),
-- so a save that never met the split reads as the unsworn bypass by
-- derivation alone — there is no split flag to migrate.
local M = {}

-- The oath tags the roster's assign column writes (labels WAR / BURDEN).
M.WAR_TAG = 1
M.BURDEN_TAG = 2

-- The Falls part the road; the Mountain of Fire joins it again.
M.FALLS = 12
M.SUMMIT = 24

-- The two act-3 roads, in marching order.
M.WAR = { 13, 14, 15, 16, 17 }
M.BURDEN = { 19, 20, 21, 22, 23 }

-- The front of a road: the first level of it still to fight, or nil once the
-- whole road is walked.
function M.front(road)
  for i = 1, #road do
    local level = road[i]
    if not og.campaign_level_completed(level) then
      return level
    end
  end
  return nil
end

-- True once any level of a road is behind the company — the event that turns
-- the swearing window into a marching split.
function M.started(road)
  for i = 1, #road do
    if og.campaign_level_completed(road[i]) then
      return true
    end
  end
  return false
end

-- Which road a level belongs to: M.WAR_TAG, M.BURDEN_TAG, or 0 for the
-- levels both columns share (the approach, the summit, the endings).
function M.road_of(level)
  for i = 1, #M.WAR do
    if M.WAR[i] == level then
      return M.WAR_TAG
    end
  end
  for i = 1, #M.BURDEN do
    if M.BURDEN[i] == level then
      return M.BURDEN_TAG
    end
  end
  return 0
end

-- The oath census over an og.campaign_team() roster: the sworn of each road,
-- and the blades that wait. The column travels whole, so an undeployed sworn
-- hero still musters. `standing` is the OTHER count — tonight's sortie, the
-- party that actually walks out of the camp — because swearing a hero stands
-- him down, and a company that reads "party 3" while nobody is deployed has
-- been lied to by its own march row.
function M.muster(team)
  local count = { war = 0, burden = 0, unsworn = 0, standing = 0, size = 0 }
  for i = 1, #team do
    count.size = count.size + 1
    if team[i].deployed then
      count.standing = count.standing + 1
    end
    local tag = team[i].tag
    if tag == M.WAR_TAG then
      count.war = count.war + 1
    elseif tag == M.BURDEN_TAG then
      count.burden = count.burden + 1
    else
      count.unsworn = count.unsworn + 1
    end
  end
  return count
end

-- The freeze: a road level is behind the company AND both columns hold at
-- least one sword. A front that is wiped (or was never sworn) unfreezes the
-- split by the same rule — the locks lift and all ride.
function M.frozen(count, started)
  if not started then
    return false
  end
  if count.war < 1 then
    return false
  end
  if count.burden < 1 then
    return false
  end
  return true
end

return M
