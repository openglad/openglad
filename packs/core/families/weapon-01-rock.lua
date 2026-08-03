-- core:rock — the weapon declaration, behavior in lib/weapon_rock.lua (cookbook: docs/lua-classpacks-design.md §3).

local rock = og.use("weapon_rock")

og.family("weapon", {
  id = "core:rock",
  wire_id = 1,
  name = "ROCK",
  fire_sound = 10,
  skip_sit_notify = false,
  is_auto_attackable = false,
  flags = { "FORESTWALK" },
  init_lifetime = 0,
  init_ani_type = 0,
  vz = 0.7,
  gravity = 0.09,
  sizez = 0,
  can_drop_floors = true,
  sprite = og.NIL,
  glyph = "*",
  glyph_ascii = "*",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_death = rock.on_death,
})
