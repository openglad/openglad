-- modes:ball — the soccer ball fx; inert under its own act (on_act true), lib/mode_soccer_impl.lua owns all motion, damage and resets (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- The engine effect act must never move, animate or expire the ball —
-- flight, bounces, goals and the rolling spin all run in the soccer
-- on_mode_tick. There is deliberately no `animation` table: nothing ever
-- calls animate() on the ball, so the impl picks the frame off the 8-frame
-- rotation strip in sprites/ball.png itself (run_spin), and that resolved
-- frame is what replicates to client mirrors.
local function on_act(self)
  return true
end

og.family("effect", {
  id = "modes:ball",
  wire_id = "auto",
  name = "BALL",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE", "SWIMMING" },
  sprite = "packs/modes.core/sprites/ball.png",
  glyph = "o",
  glyph_ascii = "o",
  glyph_color = "white",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 31, -- COLOR_WHITE
  radar_jitter = 0,
  radar_landmark = true,
  radar_ping = true, -- #209: the ball is the objective — draw it LOUD

  on_act = on_act,
})
