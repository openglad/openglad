-- core:explosion — the effect declaration, behavior in lib/effect_bomb.lua (cookbook: docs/lua-classpacks-design.md §3).

local bomb = og.use("effect_bomb")

og.family("effect", {
  id = "core:explosion",
  wire_id = 3,
  name = "EXPLOSION",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "✺",
  glyph_ascii = "*",
  glyph_color = "red",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_death = bomb.explosion_on_death,
})
