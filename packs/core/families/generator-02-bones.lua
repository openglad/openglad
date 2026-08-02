-- core:bones — the generator declaration (cookbook: docs/lua-classpacks-design.md §3).

og.family("generator", {
  id = "core:bones",
  wire_id = 2,
  name = "BONES",
  default_weapon = "core:ghost",
  has_lifetime = true,
  spawn_ani_type = 0,
  clear_owner = false,
  sprite = og.NIL,
  editor_label = "BONEPILE",
  glyph = "n",
  glyph_ascii = "n",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
})
