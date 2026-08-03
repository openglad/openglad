-- core:glow — the weapon declaration, behavior in lib/weapon_animate.lua (cookbook: docs/lua-classpacks-design.md §3).

local animate = og.use("weapon_animate")

og.family("weapon", {
  -- cleric's shield glad
  id = "core:glow",
  wire_id = 12,
  name = "GLOW",
  fire_sound = 10,
  skip_sit_notify = false,
  is_auto_attackable = true,
  flags = {},
  init_lifetime = 350,
  init_ani_type = 0,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  sprite = og.NIL,
  glyph = "∘",
  glyph_ascii = "o",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_animate = animate.glow_on_animate,
})
