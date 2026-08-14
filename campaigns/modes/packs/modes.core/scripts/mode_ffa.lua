-- mode_ffa — registers the FFA impl (lib/mode_ffa_impl.lua) for every manifest row with mode == "ffa", via the mode_match row adapter (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local core = og.use("mode_core")
local match = og.use("mode_match")
local levels = og.use("mode_levels")
local ffa = og.use("mode_ffa_impl")

core.register_mode(match.rows_for(levels), "ffa", {
  on_mode_init = ffa.on_mode_init,
  on_mode_tick = ffa.on_mode_tick,
  on_entity_death = ffa.on_entity_death,
  on_entity_spawn = ffa.on_entity_spawn,
  on_respawn = ffa.on_respawn,
})
