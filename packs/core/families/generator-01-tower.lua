-- core:tower — the generator declaration (cookbook: docs/lua-classpacks-design.md §3).

og.family("generator", {
  id = "core:tower",
  wire_id = 1,
  name = "TOWER",
  default_weapon = "core:mage",
  has_lifetime = false,
  spawn_ani_type = 3,
  clear_owner = true,
  sprite = og.NIL,
  editor_label = "MAGE TOWER",
  glyph = "T",
  glyph_ascii = "T",
  glyph_color = "default",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
})
