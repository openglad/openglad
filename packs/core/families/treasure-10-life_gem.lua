-- core:life_gem — the treasure declaration, behavior in lib/treasure_valuables.lua (cookbook: docs/lua-classpacks-design.md §3).

local valuables = og.use("treasure_valuables")

og.family("treasure", {
  id = "core:life_gem",
  wire_id = 10,
  name = "LIFE_GEM",
  init_ignore = false,
  init_frame = -1,
  sprite = og.NIL,
  glyph = "+",
  glyph_ascii = "+",
  glyph_color = "red",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_eat = valuables.life_gem_on_eat,
})
