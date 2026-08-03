-- modes:ball — the soccer ball fx; inert under its own act (on_act true), lib/mode_soccer_impl.lua owns all motion, damage and resets (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- The engine effect act must never move, animate or expire the ball —
-- flight, bounces and goals all run in the soccer on_mode_tick.
local function on_act(self)
  return true
end

og.family("effect", {
  id = "modes:ball",
  wire_id = "auto",
  name = "BALL",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE" },
  sprite = "packs/org.openglad.modes.core/sprites/ball.png",
  glyph = "o",
  glyph_ascii = "o",
  glyph_color = "white",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 31, -- COLOR_WHITE
  radar_jitter = 0,
  radar_landmark = true,

  on_act = on_act,
})
