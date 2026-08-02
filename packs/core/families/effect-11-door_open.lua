-- core:door_open — the effect declaration, behavior in lib/effect_door_open.lua (cookbook: docs/lua-classpacks-design.md §3).

local door_open = og.use("effect_door_open")

og.family("effect", {
  id = "core:door_open",
  wire_id = 11,
  name = "DOOR_OPEN",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "/",
  glyph_ascii = "/",
  glyph_color = "yellow",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_act = door_open.on_act,
})
