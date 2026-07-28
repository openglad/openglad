-- core lib: living_common — shared living-special preludes: busy gate, teleport guard/hook, post-cost MP pools (cookbook: docs/lua-classpacks-design.md §3).

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
-- C.MP_POOL_DAMAGE_CAP (§2.12: the cap binds only above ~1280 MP). The
-- pool can be negative: og.div is C trunc, not Lua floor.
function M.mp_pool_damage(self, slot)
  return og.min(og.div(spare_mp(self, slot), 2), C.MP_POOL_DAMAGE_CAP)
end

return M
