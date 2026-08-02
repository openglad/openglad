-- core:speed_potion — the treasure declaration, behavior in lib/treasure_consumables.lua (cookbook: docs/lua-classpacks-design.md §3).

local consumables = og.use("treasure_consumables")

og.family("treasure", {
  id = "core:speed_potion",
  wire_id = 12,
  name = "SPEED_POTION",
  init_ignore = false,
  init_frame = 3,
  sprite = og.NIL,
  glyph = "!",
  glyph_ascii = "!",
  glyph_color = "magenta",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- on_eat: speed-bonus ticks granted per level
    duration_per_level = 50,
  },

  on_eat = consumables.speed_potion_on_eat,
})
