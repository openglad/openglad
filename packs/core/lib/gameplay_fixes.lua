-- core lib: gameplay_fixes — the Gameplay Fixes setting and the few helpers the fixed branches of the classic specials share (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C

local M = {}

-- The setting as this session plays it. A pure read: it draws nothing and
-- has no side effect, so a gate may ask it anywhere. 0 = the classic game,
-- bugs included (docs/gameplay-fixes.md).
function M.on()
  return og.match_setting("gameplay_fixes") ~= 0
end

-- TURN UNDEAD's reach: 4 px per level, and with the fixes on never under
-- turn_undead_range_min. The roll keeps its classic strength either way
-- (walker::turn_undead).
function M.turn_range(self, t)
  local reach = t.turn_undead_range_per_level * self.level
  if not M.on() then
    return reach
  end
  return og.max(reach, t.turn_undead_range_min)
end

-- A cast that pre-refunded weapon costs and then could not fire: the rocks
-- (or bolts) that never flew are not refunded after all. fire() already
-- charged the one swing it made. magicpoints is a C++ float: per-op
-- rounding.
function M.take_back(self, rocks)
  self.magicpoints = og.fsub(self.magicpoints, rocks * self:s_weapon_cost())
end

-- A foe a charm can take: never charmed and a living-order body (the
-- finder hands over only the living); with the setting on, also not a hero
-- a player is driving (Possess's "HERO RESISTS" rule, walker:seat_driven).
-- `fixes` is the caller's one read of the setting, so a loop asks it once.
function M.charmable(foe, fixes)
  if foe:real_team_num() ~= 255 then
    return false
  end
  if foe:order() ~= C.ORDER_LIVING then
    return false
  end
  if not fixes then
    return true
  end
  return not foe:seat_driven()
end

-- The nearest charmable foe in `foes` by distance_to_ob (a tie goes to the
-- first in the list), or nil. max_level nil = any level; otherwise only a
-- foe at or below it (a higher level always resists). Draws nothing.
-- Only the fixed branches call it, so `charmable` runs with fixes = true.
function M.nearest_charmable(self, foes, max_level)
  local best = nil
  local best_dist = 0
  for i = 1, #foes do
    local foe = foes[i]
    local eligible = M.charmable(foe, true)
    if eligible and max_level ~= nil then
      eligible = foe.level <= max_level
    end
    if eligible then
      local dist = self:distance_to_ob(foe)
      if best == nil or dist < best_dist then
        best = foe
        best_dist = dist
      end
    end
  end
  return best
end

-- The soldier's WHIRLWIND and DISARM and the archer's BARRAGE and EXPLODING
-- BOLT refuse as SPECIAL BUSY while busy is not zero. A hero fires on a
-- fractional delay (the class's delay less dexterity / 47) and busy only
-- counts down while it is above zero, so after one shot it rests a fraction
-- below zero for good, and the classic gate refuses forever. With the
-- setting on, busy at or below zero is not busy, as for every other
-- special. Draws nothing.
function M.special_busy(self)
  if M.on() then
    return self:busy() > 0
  end
  return self:busy() ~= 0
end

return M
