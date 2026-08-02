-- core:flag + core:waypoint — CTF touch hooks (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.
--
-- core:waypoint is fully live: ctf_run_tick owns control points from their
-- occupancy, so a touch is genuinely a no-op returning true.
--
-- core:flag delegates to og.ctf_on_flag_touch, the binding around
-- og::sim::ctf_on_flag_touch. The flag's whole body is CtfState machinery —
-- team_active, per-team flag records, send_flag_home, capture counting and
-- scoring — so those rules remain centralized in the CTF engine.

local function flag_on_eat(self, eater)
  return og.ctf_on_flag_touch(self, eater)
end

local function ctf_point_on_eat(self, eater)
  -- Control points are occupancy-driven; touching is a no-op.
  return true
end

-- The declarations that reference this module (packs/core/families/):
--   core:flag      on_eat = ctf.flag_on_eat
--   core:waypoint  on_eat = ctf.ctf_point_on_eat
return {
  flag_on_eat = flag_on_eat,
  ctf_point_on_eat = ctf_point_on_eat,
}
