-- core — the class-pack header: id, version, title, authors (cookbook: docs/lua-classpacks-design.md §3).
--
-- The 73 families are declared under families/, one file each
-- (<order>-<NN>-<slug>.lua, NN = the pinned wire id) — except the slime
-- trio, which shares the chunk its three og.family calls need. The loader
-- evaluates every families/*.lua in sorted filename order into one pack
-- (src/resources/packs.cpp install_classpacks). Descriptor fields and
-- `tuning` blocks are authoritative in those files; edit them directly.
-- Behavior reads a family's tuning values back through og.tuning.
--
-- og.pack is legal in any of a pack's family chunks and the last call wins,
-- so this file's name is only a reading order: `00-` sorts it ahead of the
-- declarations, which is where a header belongs.
--
-- The core pack ships no og.anims: every core family uses a built-in
-- animation table (animation = "standard"|"mage"|"skeleton"|
-- "giant_skeleton"|"slime"|"small_slime"|"static" for livings, the gloader
-- EntityDef row for the other orders). A mod pack declares its own frame
-- sets with og.anims and names them from a family's `animation` field —
-- see docs/lua-classpacks-design.md §4.

og.pack{
  id = "core",
  version = "1",
  title = "OpenGlad Core Families",
  authors = { "FSGames / the OpenGlad project" },
}
