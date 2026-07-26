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
