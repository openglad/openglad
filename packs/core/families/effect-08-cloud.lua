-- core:cloud — the effect declaration, behavior in lib/effect_cloud.lua (cookbook: docs/lua-classpacks-design.md §3).

local cloud = og.use("effect_cloud")

og.family("effect", {
  id = "core:cloud",
  wire_id = 8,
  name = "CLOUD",
  loops_animation = true,
  creates_hit_effect = false,
  flags = { "FLYING", "NO_COLLIDE" },
  sprite = og.NIL,
  glyph = "☁",
  glyph_ascii = "%",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_act = cloud.on_act,
})
