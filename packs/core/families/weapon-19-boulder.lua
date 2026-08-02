-- core:boulder — the weapon declaration, behavior in lib/weapon_projectiles.lua (cookbook: docs/lua-classpacks-design.md §3).

local projectiles = og.use("weapon_projectiles")

og.family("weapon", {
  id = "core:boulder",
  wire_id = 19,
  name = "BOULDER",
  fire_sound = 10,
  skip_sit_notify = false,
  is_auto_attackable = false,
  flags = {},
  init_lifetime = 0,
  init_ani_type = 0,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  sprite = og.NIL,
  glyph = "*",
  glyph_ascii = "*",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_death = projectiles.explode_on_death,
})
