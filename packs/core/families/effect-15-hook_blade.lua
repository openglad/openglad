-- core:hook_blade — the orc captain's chained blade, behavior in lib/effect_hook_blade.lua (cookbook: docs/lua-classpacks-design.md §3).

local hook = og.use("effect_hook_blade")

og.family("effect", {
  id = "core:hook_blade",
  wire_id = 15,
  name = "HOOK BLADE",
  creates_hit_effect = false,
  flags = {},
  -- The thrown knife's eight frames, stepped by on_act as it spins.
  sprite = "knife.png",
  glyph = "j",
  glyph_ascii = "j",
  glyph_color = "white",
  glyph_bold = true,
  glyph_transparent = false,
  radar_color = "none",
  radar_jitter = 0,

  on_act = hook.on_act,
})
