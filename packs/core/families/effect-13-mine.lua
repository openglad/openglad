-- core:mine — the effect declaration, behavior in lib/effect_mine.lua (cookbook: docs/lua-classpacks-design.md §3).

local mine = og.use("effect_mine")

og.family("effect", {
  id = "core:mine",
  wire_id = 13,
  name = "MINE",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE" },
  sprite = "mine.png",
  glyph = "⊗",
  glyph_ascii = "x",
  glyph_color = "red",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  -- The thief's MINE (Shift + DROP BOMB) reads these from the mine it lays.
  tuning = {
    -- Live mines one thief may have down at once
    mine_cap = 3,
    -- Lifetime in ticks: base + per thief level
    mine_lifetime_base = 600,
    mine_lifetime_per_level = 30,
    -- Starting invisibility: the faint shimmer only its own team sees
    mine_shimmer = 20,
    -- A blast lights every other mine of its team this close (distance_to_ob)
    mine_chain_px = 24,
    -- From this level a mine also leaves a poison puff this many ticks long
    mine_puff_level = 7,
    mine_puff_ticks = 20,
  },

  on_act = mine.on_act,
  on_death = mine.on_death,
})
