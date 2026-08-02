-- core:ghost_scare — the effect declaration, behavior in lib/effect_ghost_scare.lua (cookbook: docs/lua-classpacks-design.md §3).

local ghost_scare = og.use("effect_ghost_scare")

og.family("effect", {
  id = "core:ghost_scare",
  wire_id = 1,
  name = "GHOST_SCARE",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "!",
  glyph_ascii = "!",
  glyph_color = "magenta",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_act = ghost_scare.on_act,
  on_death = ghost_scare.on_death,
})
