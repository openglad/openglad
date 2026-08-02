-- core:expand — the effect declaration (cookbook: docs/lua-classpacks-design.md §3).

og.family("effect", {
  id = "core:expand",
  wire_id = 0,
  name = "EXPAND",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "*",
  glyph_ascii = "*",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
})
