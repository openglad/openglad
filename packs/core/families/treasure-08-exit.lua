-- core:exit — the treasure declaration, behavior in lib/treasure_navigation.lua (cookbook: docs/lua-classpacks-design.md §3).

local navigation = og.use("treasure_navigation")

og.family("treasure", {
  id = "core:exit",
  wire_id = 8,
  name = "EXIT",
  init_ignore = false,
  init_frame = -1,
  sprite = og.NIL,
  glyph = ">",
  glyph_ascii = ">",
  glyph_color = "cyan",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 120,
  radar_jitter = 7,

  on_eat = navigation.exit_on_eat,
})
