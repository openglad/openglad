-- core:flag — the treasure declaration, behavior in lib/treasure_ctf.lua (cookbook: docs/lua-classpacks-design.md §3).

local ctf = og.use("treasure_ctf")

og.family("treasure", {
  id = "core:flag",
  wire_id = 13,
  name = "FLAG",
  init_ignore = false,
  init_frame = 0,
  sprite = og.NIL,
  glyph = "F",
  glyph_ascii = "F",
  glyph_color = "team",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "team",
  radar_jitter = 7,

  on_eat = ctf.flag_on_eat,
})
