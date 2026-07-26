-- core CTF treasures — on_eat hooks transliterated from
-- src/gameplay/families/treasure_family_ctf.cpp (the carryable flag and the
-- control-point waypoint). Cookbook (docs/lua-classpacks-design.md §3)
-- applies.
--
-- core:waypoint is fully live: control points are occupancy-driven (CTF
-- phase 5 owns them from ctf_run_tick), so a touch is genuinely a no-op
-- returning true (src/gameplay/ctf/ctf.cpp:1170-1176).
--
-- MISSING BINDING (core:flag only). The whole flag hook is one call —
--   og::sim::ctf_on_flag_touch(self, eater)
--     src/gameplay/families/treasure_family_ctf.cpp:15
--   defined at src/gameplay/ctf/ctf.cpp:1066-1168
-- and its body is pure CtfState machinery that no og.* binding reaches:
--   * world.type & GameWorld::TYPE_CTF, world.ctf.active   ctf.cpp:1071
--   * eater->last_self_teleport_tick() vs world.tick_count_ ctf.cpp:1086
--   * world.ctf.team_active[team]                          ctf.cpp:1089
--   * world.ctf.flags[t].present / .state / .flag_entity_id /
--     .carrier_entity_id / .return_ticks / .x / .y         ctf.cpp:1092-1160
--   * send_flag_home(world, team)                          ctf.cpp:1105, 1128
--   * world.ctf.captures[team], world.ctf.capture_limit    ctf.cpp:1133-1145
--   * award_team_score(world, team, captures*kCtfCaptureScore)
--                                                          ctf.cpp:1137
-- Inventing any of that in Lua would be inventing CTF semantics, so the
-- hook raises at BRANCH ENTRY — before any mutation — and R9 hands the
-- dispatch back to the still-registered C++ flag_on_eat byte-identically.
-- One binding, og.ctf_on_flag_touch(self, eater) -> bool, would make this
-- family live as a single-line body.

local function flag_on_eat(self, eater)
  return og.MISSING_ctf_on_flag_touch(self, eater)
end

local function ctf_point_on_eat(self, eater)
  -- Control points are occupancy-driven (phase 5); touching is a no-op.
  return true
end

og.register_hooks("treasure", "core:flag", {
  on_eat = flag_on_eat,
})

og.register_hooks("treasure", "core:waypoint", {
  on_eat = ctf_point_on_eat,
})
