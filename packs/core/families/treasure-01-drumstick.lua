-- core:drumstick — the treasure declaration, behavior in lib/treasure_consumables.lua (cookbook: docs/lua-classpacks-design.md §3).

local consumables = og.use("treasure_consumables")

og.family("treasure", {
  id = "core:drumstick",
  wire_id = 1,
  name = "DRUMSTICK",
  init_ignore = false,
  init_frame = -1,
  sprite = og.NIL,
  glyph = "%",
  glyph_ascii = "%",
  glyph_color = "yellow",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = 136,
  radar_jitter = 2,

  tuning = {
    -- on_eat heal: base = heal_per_level * level, plus a rand0(base) roll
    heal_per_level = 10,
  },

  on_eat = consumables.drumstick_on_eat,
})
