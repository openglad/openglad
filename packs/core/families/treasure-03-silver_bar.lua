-- core:silver_bar — the treasure declaration, behavior in lib/treasure_valuables.lua (cookbook: docs/lua-classpacks-design.md §3).

local valuables = og.use("treasure_valuables")

og.family("treasure", {
  id = "core:silver_bar",
  wire_id = 3,
  name = "SILVER_BAR",
  init_ignore = false,
  init_frame = 1,
  sprite = og.NIL,
  glyph = "$",
  glyph_ascii = "$",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = 23,
  radar_jitter = 5,

  tuning = {
    -- on_eat: score banked per bar level
    score_per_level = 50,
  },

  on_eat = valuables.silver_bar_on_eat,
})
