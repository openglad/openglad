-- core:sprinkle — the weapon declaration, behavior in lib/weapon_animate.lua (cookbook: docs/lua-classpacks-design.md §3).

local animate = og.use("weapon_animate")

og.family("weapon", {
  id = "core:sprinkle",
  wire_id = 6,
  name = "SPRINKLE",
  fire_sound = 4,
  skip_sit_notify = false,
  is_auto_attackable = false,
  flags = { "FLYING" },
  init_lifetime = 0,
  init_ani_type = 0,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  sprite = og.NIL,
  glyph = "·",
  glyph_ascii = "+",
  glyph_color = "magenta",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_hit_target = animate.sprinkle_on_hit_target,
})
