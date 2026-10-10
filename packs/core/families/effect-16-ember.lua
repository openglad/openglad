-- core:ember — the burning footprint IMMOLATE leaves; behavior in lib/effect_ember.lua (cookbook: docs/lua-classpacks-design.md §3).

local ember = og.use("effect_ember")

og.family("effect", {
  id = "core:ember",
  wire_id = 16,
  name = "EMBER",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE" },
  sprite = "ember.png",
  glyph = "∴",
  glyph_ascii = ":",
  glyph_color = "red",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
  -- An ember reads its own numbers: its elemental may be gone.
  tuning = {
    -- Burn the foes on it every ember_pulse ticks.
    ember_pulse = 6,
    -- Show the dying frame for the last ember_dying ticks.
    ember_dying = 10,
  },

  on_act = ember.on_act,
})
