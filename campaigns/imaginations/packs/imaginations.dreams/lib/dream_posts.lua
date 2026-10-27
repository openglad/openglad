-- imaginations.dreams lib: dream_posts — the one stand-down rule every dream-log level shares.
--
-- Dream soldiers return to their posts: a once-woken guard hunts
-- (ACT_RANDOM) forever, and with no pathfinding a hunter whose prey is
-- unreachable — across a moat, behind a wall — stands in place turning
-- toward it forever. The cure is the engine's own sight test: act_guard
-- fires at genuinely sighted foes and RE-WAKES on the next tick of any
-- clear sighting (garrisons ship with hold-post OFF for exactly this
-- reason — a held post never wakes), so cycling a hunter back onto a post
-- costs an engaged fighter nothing while a blocked one stands down
-- cleanly. A walker carrying a saved character (has_guy) is never posted:
-- a company member on a foreign roster color is a player's hero without a
-- seat here, and posting it would park someone's save for the whole dream.
--
-- Each level names which hunters it stands down with a predicate; the
-- filter above is shared, so the rule has one body.
local C = og.C

local M = {}

-- Post every living hunter (ACT_RANDOM, alive, no saved character) for
-- which wants(ob) answers true.
function M.stand_down(wants)
  local obs = og.oblist()
  for i = 1, #obs do
    local ob = obs[i]
    if ob:order() == C.ORDER_LIVING
        and ob:dead() == 0
        and ob:act_type() == C.ACT_RANDOM
        and not ob:has_guy() then
      if wants(ob) then
        ob:set_act_type(C.ACT_GUARD)
      end
    end
  end
end

return M
