-- core lib: living_common — shared living-special preludes: busy gate, teleport guard/hook, post-cost MP pools, rests (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local C = og.C

local M = {}

-- Busy gate: the shared special prelude `if busy > 0 then bail`. Sites
-- that compare `busy() ~= 0` keep their literal spelling — only the `> 0`
-- gate is this predicate.
function M.is_busy(self)
  return self:busy() > 0
end

-- Teleport guard: a caster mid-teleport cannot cast again. One ani_type
-- read serves both compares (the getter is pure).
function M.mid_teleport(self)
  local ani = self:ani_type()
  return ani == C.ANI_TELE_OUT or ani == C.ANI_TELE_IN
end

-- The shared mage/archmage handle_teleport hook body: re-enter animation,
-- then the engine teleport (which rolls for placement).
function M.handle_teleport(self)
  self.ani_type = C.ANI_TELE_IN
  self:set_cycle(0)
  self:teleport()
  return true
end

-- Post-cost MP pool: the C++ banks (int)(magicpoints - special_cost(slot))
-- — one float subtract, then C float->int truncation.
local function spare_mp(self, slot)
  return og.trunc(og.fsub(self.magicpoints, self:s_special_cost(slot)))
end
M.spare_mp = spare_mp

-- Marker/summon surcharge: half the post-cost pool, deducted from MP. The
-- pool can be negative: og.div is C trunc, not Lua floor; magicpoints is a
-- C++ float: per-op rounding.
function M.halve_mp_surcharge(self, slot)
  local surcharge = og.div(spare_mp(self, slot), 2)
  self.magicpoints = og.fsub(self.magicpoints, surcharge)
end

-- Heartburst/chain damage pool: half the post-cost pool, bound by
-- C.MP_POOL_DAMAGE_CAP (the cap binds only above ~1280 MP). The
-- pool can be negative: og.div is C trunc, not Lua floor.
function M.mp_pool_damage(self, slot)
  return og.min(og.div(spare_mp(self, slot), 2), C.MP_POOL_DAMAGE_CAP)
end

-- Rests. A rest is a wait between two casts of one special that leaves
-- ordinary attacks and the other specials alone: a clock on the caster,
-- special_rest_left ticks for the 1-based slot special_rest_slot, counted
-- down by living::act. One clock per walker, so only the last rested slot
-- waits. `cast` is the special's own cast; `rest_key` names the rest's length
-- in ticks on the family's tuning block; `name` starts the refusal,
-- "<NAME> IN <N>S", the seconds read as the HUD reads them. It holds whatever
-- the New Specials setting says (FREEZE TIME, HOWL, SCARE and MIND CONTROL
-- rest in the classic game too).
function M.with_rest(cast, slot, rest_key, name)
  return function(self)
    local left = self:special_rest_left()
    if left > 0 and self:special_rest_slot() == slot then
      return false, string.format("%s IN %dS", name, og.timer_seconds(left))
    end
    local ok, why = cast(self)
    if ok then
      self:set_special_rest_left(og.tuning(self)[rest_key])
      self:set_special_rest_slot(slot)
    end
    return ok, why
  end
end

-- A bot gate for a rested special: while the rest runs a bot does not pick
-- the slot; otherwise `gate` answers (no gate: yes). For a family-wide gate
-- the rest is read only when the bot is on the rested slot.
function M.gate_with_rest(gate, slot)
  return function(self)
    if self:current_special() == slot
        and self:special_rest_slot() == slot
        and self:special_rest_left() > 0 then
      return false
    end
    if gate then
      return gate(self)
    end
    return true
  end
end

return M
