-- core:boomerang — the effect declaration, behavior in lib/effect_shield.lua (cookbook: docs/lua-classpacks-design.md §3).

local shield = og.use("effect_shield")

og.family("effect", {
  id = "core:boomerang",
  wire_id = 7,
  name = "BOOMERANG",
  loops_animation = true,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "%",
  glyph_ascii = "%",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_act = shield.boomerang_on_act,
})
