-- core:circle_protection — the weapon declaration, behavior in lib/weapon_animate.lua (cookbook: docs/lua-classpacks-design.md §3).

local animate = og.use("weapon_animate")

og.family("weapon", {
  id = "core:circle_protection",
  wire_id = 16,
  name = "CIRCLE_PROTECTION",
  fire_sound = 10,
  skip_sit_notify = false,
  is_auto_attackable = false,
  flags = { "FLYING", "IMMORTAL", "NO_COLLIDE", "PHANTOM" },
  init_lifetime = 0,
  -- anything non-zero
  init_ani_type = 5,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  sprite = og.NIL,
  glyph = "○",
  glyph_ascii = "O",
  glyph_color = "cyan",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_animate = animate.circle_protection_on_animate,
})
