-- core:wave — the weapon declaration, behavior in lib/weapon_wave.lua (cookbook: docs/lua-classpacks-design.md §3).

local wave = og.use("weapon_wave")

og.family("weapon", {
  id = "core:wave",
  wire_id = 13,
  name = "WAVE",
  fire_sound = 10,
  skip_sit_notify = false,
  is_auto_attackable = false,
  flags = { "FLYING", "IMMORTAL", "NO_COLLIDE", "PHANTOM", "MAGICAL" },
  init_lifetime = 0,
  init_ani_type = 0,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  sprite = og.NIL,
  glyph = "≈",
  glyph_ascii = "~",
  glyph_color = "cyan",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_death = wave.wave_on_death,
})
