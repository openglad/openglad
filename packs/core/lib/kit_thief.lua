-- core lib: kit_thief — the thief's New Specials kit: Shift + DROP BOMB lays a MINE (cookbook: docs/lua-classpacks-design.md §3).

local C = og.C
local FX_MINE = assert(og.family_id("fx", "core:mine"))

local M = {}

-- The thief's live mines: how many, and one of them (whose tuning holds the
-- cap). Mines are added with og.add_ob, which puts effects in the oblist.
local function live_mines(self)
  local obs = og.oblist()
  local count = 0
  local found = nil
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_FX
        and ob:family() == FX_MINE
        and ob:dead() == 0
        and ob:owner() == self then
      count = count + 1
      found = ob
    end
  end
  return count, found
end

-- A bot thief steps off its own mine the way it runs from its bomb.
local function step_off(self)
  local flee_dx = og.rand(3) - 1
  local flee_dy = og.rand(3) - 1
  if flee_dx == 0 and flee_dy == 0 then
    flee_dx = 1
  end
  self:s_force_command(C.COMMAND_WALK, 20, flee_dx, flee_dy)
end

-- MINE: a bomb with no fuse, laid under the thief, seen only by its team.
local function mine(self)
  local count, found = live_mines(self)
  if found and count >= og.tuning(found).mine_cap then
    return false, "MINE LIMIT REACHED"
  end
  local laid = og.add_ob("fx", FX_MINE)
  if not laid then
    return false, "COULD NOT CREATE MINE"
  end
  local t = og.tuning(laid)
  if self:has_guy() then
    self:g_set_total_shots(self:g_total_shots() + 1)
    self:g_set_scen_shots(self:g_scen_shots() + 1)
  end
  laid:set_owner(self)
  laid.team = self.team
  laid.level = self.level
  laid.damage = og.combat.bomb_damage(self.level)
  laid:set_floor(self:floor())
  laid:center_on(self)
  laid.lifetime = t.mine_lifetime_base + t.mine_lifetime_per_level * self.level
  laid:set_invisibility_left(t.mine_shimmer)
  laid:set_ignore(1)
  laid.ani_type = C.ANI_WALK
  if self:user() == -1 then
    step_off(self)
  end
  return true
end

-- The DROP BOMB slot's cast: MINE while the shifter is down and the New
-- Specials setting puts it in play, otherwise the classic bomb.
function M.bomb_or_mine(classic)
  return function(self)
    if self:alternate_down() then
      return mine(self)
    end
    return classic(self)
  end
end

return M
