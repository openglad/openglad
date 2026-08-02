-- core:bomb — the effect declaration, behavior in lib/effect_bomb.lua (cookbook: docs/lua-classpacks-design.md §3).

local bomb = og.use("effect_bomb")

og.family("effect", {
  id = "core:bomb",
  wire_id = 2,
  name = "BOMB",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "◍",
  glyph_ascii = "o",
  glyph_color = "red",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_death = bomb.bomb_on_death,
})
