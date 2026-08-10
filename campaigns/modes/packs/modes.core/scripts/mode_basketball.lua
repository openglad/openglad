-- mode_basketball — binds the basketball impl (lib/mode_basketball_impl.lua) to every manifest level with mode == "basketball", one hook table per row (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local levels = og.use("mode_levels")
local bball = og.use("mode_basketball_impl")

-- The manifest is keyed by level id with holes and the sandbox has no
-- pairs — scan the D9 id space and register a per-row hook table (the
-- row carries the hoops, arc radius, jump-ball spot, caps and limits).
for id = 0, 1023 do
  local row = levels.levels[id]
  if row ~= nil then
    if row.mode == "basketball" then
      og.register_level_hooks(id, bball.make_hooks(row))
    end
  end
end
