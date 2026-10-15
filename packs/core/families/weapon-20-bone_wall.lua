-- core:bone_wall — the skeleton's BONE WALL segment: solid, chop-able, timed; behavior in lib/weapon_bone_wall.lua (cookbook: docs/lua-classpacks-design.md §3).

local bone_wall = og.use("weapon_bone_wall")

og.family("weapon", {
  id = "core:bone_wall",
  wire_id = 20,
  name = "BONE WALL",
  fire_sound = 10,
  -- The cast sits the segment down (ACT_SIT); its ani_type keeps weap::act
  -- on the animate path, so the sitting notice would never be reached, and
  -- the flag says so for any path that does reach it.
  skip_sit_notify = true,
  -- Foes chop it down; nothing lands, spawns or teleports onto it.
  is_auto_attackable = true,
  blocks_placement = true,
  flags = {},
  init_lifetime = 0,
  init_ani_type = 1,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  hp = 40,
  sprite = "bonewall.png",
  glyph = "▒",
  glyph_ascii = "#",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_animate = bone_wall.on_animate,
})
