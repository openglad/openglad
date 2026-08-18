-- modes:bball — the basketball fx; inert under its own act (on_act true), lib/mode_basketball_impl.lua owns the arc, the fake height axis, the spin and every reset (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- Same contract as modes:ball, one axis richer. The engine effect act must
-- never move, animate or expire the basketball: ground x/y, height z, the
-- shot arc, rim clangs, dunks and every center reset run in the basketball
-- on_mode_tick, which writes the DRAWN position each tick (setxy of the
-- ground spot lifted by z) so mirrors need no sim state of their own.
--
-- There is deliberately no `animation` table: nothing ever calls animate()
-- on the ball, so the impl picks the frame off the 8-frame rotation strip in
-- sprites/bball.png itself, and that resolved frame is what replicates.
--
-- Spawned with og.add_ob (the oblist): the fx list never acts, and this
-- family is the one the impl pins every tick.
local function on_act(self)
  return true
end

og.family("effect", {
  id = "modes:bball",
  wire_id = "auto",
  name = "BBALL",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE" },
  sprite = "packs/modes.core/sprites/bball.png",
  glyph = "b",
  glyph_ascii = "b",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 88, -- COLOR_YELLOW, the nearest radar band to the orange art
  radar_jitter = 0,
  radar_landmark = true,
  radar_ping = true, -- #209: the ball is the objective — draw it LOUD

  on_act = on_act,
})
