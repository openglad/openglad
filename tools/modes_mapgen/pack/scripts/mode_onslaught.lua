-- mode_onslaught — binds the onslaught impl (lib/mode_onslaught_impl.lua) to every manifest level with mode == "onslaught", plus the ONE customize_spawn registration for the core generator families (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local levels = og.use("mode_levels")
local onslaught = og.use("mode_onslaught_impl")

-- The manifest is keyed by level id with holes and the sandbox has no
-- pairs — scan the D9 id space and register a per-row hook table (the
-- row carries the spawn caps and limits).
for id = 0, 1023 do
  local row = levels.levels[id]
  if row ~= nil then
    if row.mode == "onslaught" then
      og.register_level_hooks(id, onslaught.make_hooks(row))
    end
  end
end

-- The generator families are core's, so their customize_spawn is
-- registered here exactly once (a second registration anywhere else is a
-- recorded duplicate-hook error). The handler no-ops unless a mode that
-- needs it (onslaught, capped soccer) is active on this level.
og.register_hooks("generator", "core:tent", { customize_spawn = onslaught.customize_spawn })
og.register_hooks("generator", "core:tower", { customize_spawn = onslaught.customize_spawn })
og.register_hooks("generator", "core:bones", { customize_spawn = onslaught.customize_spawn })
og.register_hooks("generator", "core:treehouse", { customize_spawn = onslaught.customize_spawn })
