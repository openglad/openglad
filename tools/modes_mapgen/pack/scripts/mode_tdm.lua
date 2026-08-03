-- mode_tdm — registers the TDM impl (lib/mode_tdm_impl.lua) for every manifest row with mode == "tdm", via the mode_match row adapter (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local core = og.use("mode_core")
local match = og.use("mode_match")
local levels = og.use("mode_levels")
local tdm = og.use("mode_tdm_impl")

core.register_mode(match.rows_for(levels), "tdm", {
  on_mode_init = tdm.on_mode_init,
  on_mode_tick = tdm.on_mode_tick,
  on_entity_death = tdm.on_entity_death,
  on_respawn = tdm.on_respawn,
})
