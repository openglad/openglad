-- core:waypoint — the treasure declaration, behavior in lib/treasure_ctf.lua (cookbook: docs/lua-classpacks-design.md §3).

local ctf = og.use("treasure_ctf")

og.family("treasure", {
  id = "core:waypoint",
  wire_id = 14,
  name = "WAYPOINT",
  init_ignore = false,
  init_frame = 0,
  sprite = og.NIL,
  glyph = "O",
  glyph_ascii = "O",
  glyph_color = "team",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "team",
  radar_jitter = 7,

  on_eat = ctf.ctf_point_on_eat,
})
