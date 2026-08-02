-- core:flash — the effect declaration (cookbook: docs/lua-classpacks-design.md §3).

og.family("effect", {
  id = "core:flash",
  wire_id = 4,
  name = "FLASH",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "*",
  glyph_ascii = "*",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
})
