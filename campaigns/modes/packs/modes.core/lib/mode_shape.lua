-- modes lib: mode_shape — which games buy BODIES with the FILL wheel above FAIR (#305), and their hard shape; read by soccer/basketball's T and by campaign_picker's match_knobs (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
local SHAPE = {
  soccer = { bodies = true },
  basketball = { cap = 5, bodies = true },
}

local function of(mode)
  return SHAPE[mode]
end

return { of = of }
