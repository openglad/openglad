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
  -- plumbing: live_mines oblist scan; rule in kit_thief.lua:16
  for i = 1, #obs do
    local ob = obs[i]
    -- rule: A thief may keep at most mine_cap (3) live mines of its own; another thief's mines and
    --   dead mines do not count; the fourth is refused for free.
    -- tests: KitThief.fourth_mine_is_refused
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
  -- rule: A flee roll that lands still steps the thief right (+1,0) so it always leaves its mine.
  -- tests: KitThief.bot_steps_right_off_a_mine_when_the_flee_roll_is_still
  -- tests: SemLibsThief.a_bot_thief_walks_off_its_mine_on_the_step_it_rolled
  if flee_dx == 0 and flee_dy == 0 then
    flee_dx = 1
  end
  self:s_force_command(C.COMMAND_WALK, 20, flee_dx, flee_dy)
end

-- MINE: a bomb with no fuse, laid under the thief, seen only by its team.
local function mine(self)
  local count, found = live_mines(self)
  -- rule: Part of the rule above that begins "A thief may keep at most mine_cap (3) live ..."
  -- tests: KitThief.fourth_mine_is_refused
  if found and count >= og.tuning(found).mine_cap then
    return false, "MINE LIMIT REACHED"
  end
  local laid = og.add_ob("fx", FX_MINE)
  -- plumbing: add_ob mine nil (world full): 'COULD NOT CREATE MINE'
  if not laid then
    return false, "COULD NOT CREATE MINE"
  end
  local t = og.tuning(laid)
  -- rule: Laying a mine counts as a shot for a roster hero (total_shots and scen_shots +1); a bot
  --   keeps no tally.
  -- tests: KitThief.mine_costs_double_and_is_a_bomb_when_off
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
  -- rule: A bot thief steps off a mine it just laid (a forced 20-step walk, the bomb's flee); a
  --   seated thief is left where it is.
  -- tests: KitThief.bot_steps_right_off_a_mine_when_the_flee_roll_is_still
  -- tests: KitThief.ai_mine_fires_for_a_hunter_past_the_blast_when_mana_allows
  -- tests: SemLibsThief.a_seated_thief_lays_a_mine_without_stepping_off_it
  if self:user() == -1 then
    step_off(self)
  end
  return true
end

-- The DROP BOMB slot's cast: MINE while the shifter is down and the New
-- Specials setting puts it in play, otherwise the classic bomb.
function M.bomb_or_mine(classic)
  return function(self)
    -- rule: DROP BOMB with the shifter down and New Specials on lays a MINE; otherwise it is the
    --   classic bomb.
    -- tests: KitThief.mine_costs_double_and_is_a_bomb_when_off
    if self:alternate_down() then
      return mine(self)
    end
    return classic(self)
  end
end

return M
