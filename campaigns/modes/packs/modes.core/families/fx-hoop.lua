-- modes:hoop — the basketball rim and net; declares no hooks at all, lib/mode_basketball_impl.lua spawns one per active team's hoop, tints it and drives its frames (cookbook: docs/lua-classpacks-design.md §3).
-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.

-- Deliberately hookless, the bshadow ruling. Spawned with og.add_fx_ob, this
-- lives on the fx list, which never acts, so an on_act here could never run —
-- it would be a permanently uncovered function under the function = 100
-- coverage gate.
--
-- The 6-frame strip in sprites/hoop.png is 0 idle, 1-3 the net-ripple swish a
-- made basket plays, 4-5 the rim-clang flash; run_hoop_anims in the impl
-- counts those frames down itself, so nothing here animates either. The four
-- tick marks on the rim sit in palette band 248-255, which the blitter remaps
-- to the entity's team colour, so the tint is whatever set_team_num stamped on
-- the defending team's hoop at spawn.
--
-- No radar blip: the hoops never move, the mode already beacons the ball and
-- the carrier, and up to four identical anonymous dots would only crowd them.
--
-- The filename is load-bearing. families/ is evaluated in sorted filename
-- order and "auto" wire ids are handed out in that order, so this file sorting
-- after fx-bshadow.lua is what keeps ball 21, bball 22 and shadow 23 where the
-- mirror-replication pin expects them; the hoop takes effect id 24.
og.family("effect", {
  id = "modes:hoop",
  wire_id = "auto",
  name = "HOOP",
  loops_animation = false,
  creates_hit_effect = false,
  flags = { "NO_COLLIDE" },
  sprite = "packs/modes.core/sprites/hoop.png",
  glyph = "O",
  glyph_ascii = "O",
  glyph_color = "yellow",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,
  radar_landmark = false,
})
