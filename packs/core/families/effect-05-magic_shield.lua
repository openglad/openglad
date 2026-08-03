-- core:magic_shield — the effect declaration, behavior in lib/effect_shield.lua (cookbook: docs/lua-classpacks-design.md §3).

local shield = og.use("effect_shield")

og.family("effect", {
  id = "core:magic_shield",
  wire_id = 5,
  name = "MAGIC_SHIELD",
  loops_animation = true,
  creates_hit_effect = false,
  flags = { "PHANTOM" },
  sprite = og.NIL,
  glyph = "○",
  glyph_ascii = "O",
  glyph_color = "cyan",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_act = shield.magic_shield_on_act,
})
