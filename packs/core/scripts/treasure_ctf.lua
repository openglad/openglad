-- core CTF treasures — on_eat hooks transliterated from
-- src/gameplay/families/treasure_family_ctf.cpp (the carryable flag and the
-- control-point waypoint). Cookbook (docs/lua-classpacks-design.md §3)
-- applies.
--
-- core:waypoint is fully live: control points are occupancy-driven (CTF
-- phase 5 owns them from ctf_run_tick), so a touch is genuinely a no-op
-- returning true (src/gameplay/ctf/ctf.cpp:1170-1176).
--
-- core:flag is one call: og.ctf_on_flag_touch(self, eater) wraps
-- og::sim::ctf_on_flag_touch (src/gameplay/ctf/ctf.cpp) exactly as the C++
-- hook did. The flag's whole body is CtfState machinery — team_active, the
-- per-team flag records, send_flag_home, capture counting and scoring — so
-- the rules stay in the CTF engine rather than being re-invented in Lua.

local function flag_on_eat(self, eater)
  return og.ctf_on_flag_touch(self, eater)
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
