-- core lib: ai — parameterized check_special_ai shapes, closures over constants only (R6-safe) (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

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
--
-- Both builders here take PLAIN CONSTANTS, deliberately: these gates run
-- every AI act tick, and a per-call og.tuning read here blew the
-- instruction budget on the generator scenarios (+23.8% on bones).
-- Gate-only ranges are therefore code
-- constants; only ranges a special body shares read og.tuning (at the
-- family's own check_special_ai, where the special keeps the single
-- source of truth).
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
