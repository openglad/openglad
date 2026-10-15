-- core:war_banner — the orc captain's standard, planted on a corpse; behavior in lib/weapon_banner.lua (cookbook: docs/lua-classpacks-design.md §3).

local banner = og.use("weapon_banner")

og.family("weapon", {
  id = "core:war_banner",
  wire_id = 21,
  name = "WAR BANNER",
  fire_sound = 10,
  -- planted with ACT_SIT: no "Weapon sitting" notice
  skip_sit_notify = true,
  -- foes chop it down
  is_auto_attackable = true,
  flags = {},
  init_lifetime = 0,
  -- a non-walk ani_type, so on_animate runs every tick
  init_ani_type = 1,
  vz = 0,
  gravity = 0,
  sizez = 0,
  can_drop_floors = false,
  hp = 120,
  -- allies within this many pixels never flee (og::sim::fearless)
  rally_radius = 96,
  -- solid scenery: nothing teleports, respawns or lands on it
  blocks_placement = true,
  sprite = "banner.png",
  glyph = "⚑",
  glyph_ascii = "P",
  glyph_color = "team",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "team",
  radar_jitter = 0,

  tuning = {
    -- the aura: pulse cadence (ticks), radius (px, = rally_radius), the
    -- heal per pulse and the fright laid on a weaker foe
    banner_pulse = 8,
    banner_radius = 96,
    banner_regen = 1,
    banner_fright = 20,
  },

  on_animate = banner.on_animate,
  on_death = banner.on_death,
})
