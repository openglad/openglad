-- core:chain — the effect declaration, behavior in lib/effect_chain.lua (cookbook: docs/lua-classpacks-design.md §3).

local chain = og.use("effect_chain")

og.family("effect", {
  id = "core:chain",
  wire_id = 10,
  name = "CHAIN",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = og.NIL,
  glyph = "╪",
  glyph_ascii = "#",
  glyph_color = "white",
  glyph_bold = false,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- Death-fork: acquisition radius base (px; + per-level for AI
    -- casters, + Int/2 for myguy casters), the damage fraction each
    -- fork inherits, and the minimum damage worth forking
    fork_range_base = 240,
    fork_range_per_level = 5,
    fork_damage_mult = 0.5,
    fork_min_damage = 20,
  },

  on_act = chain.on_act,
})
