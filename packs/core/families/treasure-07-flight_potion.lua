-- core:flight_potion — the treasure declaration, behavior in lib/treasure_consumables.lua (cookbook: docs/lua-classpacks-design.md §3).

local consumables = og.use("treasure_consumables")

og.family("treasure", {
  id = "core:flight_potion",
  wire_id = 7,
  name = "FLIGHT_POTION",
  init_ignore = false,
  init_frame = 11,
  sprite = og.NIL,
  glyph = "!",
  glyph_ascii = "!",
  glyph_color = "magenta",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 72,
  radar_jitter = 5,

  tuning = {
    -- on_eat: flight ticks granted per level
    duration_per_level = 150,
  },

  on_eat = consumables.flight_potion_on_eat,
})
