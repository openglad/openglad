-- modes:bshadow — the basketball's ground spot; declares no hooks at all, lib/mode_basketball_impl.lua places it and picks its height frame every tick (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- Deliberately hookless. Spawned with og.add_fx_ob, this lives on the fx
-- list, which never acts, so an on_act here could never run — it would be a
-- permanently uncovered function under the function = 100 coverage gate.
-- The 4-frame strip in sprites/bshadow.png shrinks with altitude and the
-- impl drives set_frame, so nothing here animates either.
--
-- Beacon slot 0 points at this ground-truth proxy. Its descriptor supplies
-- the basketball's one yellow radar pulse while the drawn ball rises with
-- fake z, so an apex shot never produces a second, lifted objective dot.
og.family("effect", {
  id = "modes:bshadow",
  wire_id = "auto",
  name = "BSHADOW",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE" },
  sprite = "packs/modes.core/sprites/bshadow.png",
  glyph = ".",
  glyph_ascii = ".",
  glyph_color = "black",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = 88, -- COLOR_YELLOW, the nearest radar band to the orange art
  radar_jitter = 0,
  radar_landmark = true,
  radar_ping = true, -- #209: the ground-truth objective draws LOUD
})
