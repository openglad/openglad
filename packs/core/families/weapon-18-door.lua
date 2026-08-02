-- core:door — the weapon declaration, behavior in lib/weapon_door.lua (cookbook: docs/lua-classpacks-design.md §3).

local door = og.use("weapon_door")

og.family("weapon", {
  id = "core:door",
  wire_id = 18,
  name = "DOOR",
  fire_sound = 10,
  skip_sit_notify = true,
  is_auto_attackable = true,
  flags = {},
  init_lifetime = 0,
  init_ani_type = 0,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  sprite = og.NIL,
  glyph = "+",
  glyph_ascii = "+",
  glyph_color = "yellow",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_death = door.on_death,
})
