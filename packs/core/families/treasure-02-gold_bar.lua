-- core:gold_bar — the treasure declaration, behavior in lib/treasure_valuables.lua (cookbook: docs/lua-classpacks-design.md §3).

local valuables = og.use("treasure_valuables")

og.family("treasure", {
  id = "core:gold_bar",
  wire_id = 2,
  name = "GOLD_BAR",
  init_ignore = false,
  init_frame = 0,
  sprite = og.NIL,
  glyph = "$",
  glyph_ascii = "$",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 88,
  radar_jitter = 5,

  tuning = {
    -- on_eat: score banked per bar level
    score_per_level = 200,
  },

  on_eat = valuables.gold_bar_on_eat,
})
