-- modes:waypoint — the CTF control point; occupancy-driven (mode_ctf_impl.run_control_points), so its on_eat is the preserved no-op (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local ctf = og.use("mode_ctf_impl")

og.family("treasure", {
  id = "modes:waypoint",
  wire_id = 14,
  name = "WAYPOINT",
  init_ignore = false,
  init_frame = 0,
  sprite = "packs/org.openglad.modes.core/sprites/ctfpoint.png",
  glyph = "O",
  glyph_ascii = "O",
  glyph_color = "team",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "team",
  radar_jitter = 7,
  radar_landmark = true,

  on_eat = ctf.waypoint_on_eat,
})
