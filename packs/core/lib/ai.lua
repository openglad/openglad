-- core lib: ai — parameterized check_special_ai shapes, closures over constants only (R6-safe) (cookbook: docs/lua-classpacks-design.md §3).

local M = {}

-- Cast when the acquired-or-found foe is within `dist`: the engine's
-- shared acquire-foe + distance gate.
function M.foe_within(dist)
  return function(self)
    return og.check_special_ai_distance(self, dist)
  end
end

-- Cast when the current-or-found foe sits inside the (near, far) distance
-- window. Acquisition assigns self.foe exactly like the ladder prelude
-- did; with no foe to be found the gate is closed.
function M.foe_in_window(near, far)
  return function(self)
    local foe = self:foe()
    if not foe then
      self:set_foe(og.find_near_foe(self))
      foe = self:foe()
    end
    if not foe then
      return false
    end
    local distance = self:distance_to_ob(foe)
    return distance < far and distance > near
  end
end

return M
