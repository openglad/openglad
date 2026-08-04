-- modes:flag — the CTF flag treasure; on_eat carries the REAL pickup/return/capture rules (lib/mode_ctf_impl.lua; cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

local ctf = og.use("mode_ctf_impl")

og.family("treasure", {
  id = "modes:flag",
  wire_id = 13,
  name = "FLAG",
  init_ignore = false,
  init_frame = 0,
  sprite = "packs/org.openglad.modes.core/sprites/flag.png",
  glyph = "F",
  glyph_ascii = "F",
  glyph_color = "team",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "team",
  radar_jitter = 7,
  radar_landmark = true,

  on_eat = ctf.flag_on_eat,
})
