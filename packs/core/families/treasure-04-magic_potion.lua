-- core:magic_potion — the treasure declaration, behavior in lib/treasure_consumables.lua (cookbook: docs/lua-classpacks-design.md §3).

local consumables = og.use("treasure_consumables")

og.family("treasure", {
  id = "core:magic_potion",
  wire_id = 4,
  name = "MAGIC_POTION",
  init_ignore = false,
  init_frame = 0,
  sprite = og.NIL,
  glyph = "!",
  glyph_ascii = "!",
  glyph_color = "magenta",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 72,
  radar_jitter = 5,

  tuning = {
    -- on_eat: fill the pool, then overfill by this much per level
    mana_overfill_per_level = 50,
  },

  on_eat = consumables.magic_potion_on_eat,
})
