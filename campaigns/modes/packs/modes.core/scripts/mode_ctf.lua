-- mode_ctf — registers the CTF impl (lib/mode_ctf_impl.lua) for every manifest row with mode == "ctf" (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local core = og.use("mode_core")
local levels = og.use("mode_levels")
local ctf = og.use("mode_ctf_impl")

core.register_mode(levels, "ctf", {
  on_mode_plan = ctf.on_mode_plan,
  on_mode_init = ctf.on_mode_init,
  on_mode_tick = ctf.on_mode_tick,
  on_respawn = ctf.on_respawn,
})
