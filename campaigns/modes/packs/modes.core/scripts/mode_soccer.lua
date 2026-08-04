-- mode_soccer — binds the soccer impl (lib/mode_soccer_impl.lua) to every manifest level with mode == "soccer", one hook table per row (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local levels = og.use("mode_levels")
local soccer = og.use("mode_soccer_impl")

-- The manifest is keyed by level id with holes and the sandbox has no
-- pairs — scan the D9 id space and register a per-row hook table (the
-- row carries the goal rects, kickoff, caps and limits).
for id = 0, 1023 do
  local row = levels.levels[id]
  if row ~= nil then
    if row.mode == "soccer" then
      og.register_level_hooks(id, soccer.make_hooks(row))
    end
  end
end
