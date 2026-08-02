-- core:key — the treasure declaration, behavior in lib/treasure_valuables.lua (cookbook: docs/lua-classpacks-design.md §3).

local valuables = og.use("treasure_valuables")

og.family("treasure", {
  id = "core:key",
  wire_id = 11,
  name = "KEY",
  init_ignore = false,
  init_frame = -1,
  sprite = og.NIL,
  glyph = "[",
  glyph_ascii = "[",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_eat = valuables.key_on_eat,
})
