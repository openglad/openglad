-- core:kit_marker — the invisible New Specials helper; behavior in lib/kit_marker.lua (cookbook: docs/lua-classpacks-design.md §3).

local km = og.use("kit_marker")

og.family("effect", {
  id = "core:kit_marker",
  wire_id = 17,
  name = "KIT MARKER",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE" },
  sprite = "empty.png",
  glyph = " ",
  glyph_ascii = " ",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = true,
  radar_color = "none",
  radar_jitter = 0,

  on_act = km.on_act,
})
