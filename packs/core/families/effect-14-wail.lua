-- core:wail — the ghost's wail bolt declaration, behavior in lib/effect_wail.lua (cookbook: docs/lua-classpacks-design.md §3).

local wail = og.use("effect_wail")

og.family("effect", {
  id = "core:wail",
  wire_id = 14,
  name = "WAIL",
  loops_animation = false,
  creates_hit_effect = false,
  flags = {},
  sprite = "lightnin.png",
  glyph = "~",
  glyph_ascii = "~",
  glyph_color = "cyan",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  tuning = {
    -- A bolt flies for wail_bolt_life ticks at wail_bolt_step px a tick
    -- (the chain bolt's speed) before it gives up on its foe.
    wail_bolt_life = 40,
    wail_bolt_step = 12,
    -- On a hit the wail forks to foes within wail_fork_px of the struck
    -- one. The first bolt may hop wail_hops more times; a bolt with no
    -- hops left frightens its foe and forks no further, so the chain ends.
    wail_fork_px = 120,
    wail_hops = 3,
  },

  on_act = wail.on_act,
})
