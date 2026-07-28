# Ember Wisp — an example class pack with its own art and motion

A minimal but complete mod pack. Nothing in it is inherited from a core
family: it ships its own sprite sheet, its own animation table, its own
descriptor data and its own behavior script.

```
emberwisp/
├── classpack.yaml            one living family, wire_id: auto
├── scripts/emberwisp.lua     behavior hooks
└── sprites/
    ├── emberwisp.png         16x16, 8 frames, indexed to the engine palette
    └── emberwisp.json        Aseprite "Hash" sidecar describing the frames
```

## Trying it

The pack lives here rather than under `packs/` on purpose: everything under
`packs/` is mounted at startup, and a new living family would change the
hire menu, the registries and the auto-assigned wire ids of the shipped
game. Mount it explicitly instead:

```cpp
og::resources::mount("docs/modding/examples/emberwisp", "packs/emberwisp/", 1);
og::resources::refresh_pack_scripts();   // rescans scripts + reinstalls YAML
loader.reload_graphics();                // picks up the pack's sprites
```

The mount point decides the pack id — `emberwisp`, the directory name under
`packs/` — and it is what makes the `sprite:` path resolve.

## What the script demonstrates

`scripts/emberwisp.lua` is written in the current pack idiom
([docs/lua-style.md](../../../lua-style.md)) and exercises most of the
modern surface in ~60 lines:

- **Walker properties** — `self.magicpoints`, `self.max_magicpoints`,
  `self.level` read as values; `self.ani_type = C.ANI_WALK` and
  `self.busy = ...` assign through the same narrowing setters as the
  method spellings. (`busy` is also a method name, so its *read* stays
  `self:busy()` — reads resolve method-first.)
- **A `tuning:` block** — every balance constant (`flare_cost`,
  `burn_floor`, `burst_range`, `stun_base`, `stun_per_level`) lives in
  `classpack.yaml` and is read back with `og.tuning(self)`, a frozen
  read-only table. Rebalancing the wisp is a YAML edit.
- **A `specials` table** — `specials = { [1] = flare_burst }` replaces a
  hand-written `current_special()` ladder. A slot with no entry and no
  `default` is a successful no-op; answering `false` from the entry means
  "did not fire" and skips the descriptor's special MP cost.
- **`og.rand` vs `og.rand0`** — the create-time roll has a positive
  literal bound, so it uses plain `og.rand` (its `n <= 0` error is a
  tripwire); the per-foe stun roll's bound is tuning-driven and may be
  zero, so it uses `og.rand0`, which answers 0 *without advancing the
  stream*.
- **Fused verbs and bound helpers** — `foe:add_frozen_stun(n)` applies
  `combat_math::stun_total` (thaw-immunity discard, cap 150) in one call;
  `og.clamp` sanity-bounds the modder-supplied `burst_range`.

It deliberately does NOT use `og.use`: that mechanism is for helpers
shared by two or more files, and a one-script pack keeps helpers as
`local function`s (style rule S4). See `packs/core/lib/` for real
modules.

## The three things worth copying

**Sprite paths are virtual-filesystem paths.** `sprite: packs/emberwisp/
sprites/emberwisp.png` is looked up as-is. Core art passes bare names
(`footman.png`), which resolve under `pix/`; a pack passes the full path
starting at its mount point. The frame sidecar is resolved next to the PNG
that actually opened, so a pack's `.json` sits beside its `.png`.

**Frame metadata comes from the sidecar.** A PNG with no sidecar is a
single-frame sprite. `meta.size` must equal `frame w` x `frame h * frame
count` or the sprite is rejected — that cross-check is what catches a
sidecar drifting away from its art.

**Animation rows are `ani_type * 8 + curdir`.** `rows: 16` gives a family
two ani_types over eight facings: 0 = walk, 1 = attack. Declaring fewer
rows than `rows:` asks for repeats them *cyclically*, so two declared rows
over `rows: 16` alternate walk/attack per facing rather than filling eight
of each. Write every row out unless the cycle is what you want.

The row count is also load-bearing beyond looks: it becomes
`walker::ani_count`, which bounds the animation index arithmetic against a
snapshot- or save-supplied `ani_type`/`curdir`. A pack table always carries
its explicit count.
