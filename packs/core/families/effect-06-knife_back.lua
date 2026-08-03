-- core:knife_back — the effect declaration, behavior in lib/effect_knife_back.lua (cookbook: docs/lua-classpacks-design.md §3).

local knife_back = og.use("effect_knife_back")

og.family("effect", {
  id = "core:knife_back",
  wire_id = 6,
  name = "KNIFE_BACK",
  loops_animation = true,
  creates_hit_effect = true,
  flags = {},
  sprite = og.NIL,
  glyph = "'",
  glyph_ascii = "'",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_act = knife_back.on_act,
})
