-- core:tree — the weapon declaration, behavior in lib/weapon_animate.lua (cookbook: docs/lua-classpacks-design.md §3).

local animate = og.use("weapon_animate")

og.family("weapon", {
  id = "core:tree",
  wire_id = 4,
  name = "TREE",
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
  glyph = "♣",
  glyph_ascii = "&",
  glyph_color = "green",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_animate = animate.tree_blood_on_animate,
})
