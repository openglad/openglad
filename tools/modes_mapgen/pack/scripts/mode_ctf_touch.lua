-- mode_ctf_touch — rebinds the AUTHORED core:flag / core:waypoint touches (the shipped maps carry wire bytes 13/14) to the Lua CTF rules while this pack is mounted; overriding an og.family declaration is the silent, designed og.register_hooks path (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local ctf = og.use("mode_ctf_impl")

og.register_hooks("treasure", "core:flag", {
  on_eat = ctf.core_flag_on_eat,
})

og.register_hooks("treasure", "core:waypoint", {
  on_eat = ctf.waypoint_on_eat,
})
