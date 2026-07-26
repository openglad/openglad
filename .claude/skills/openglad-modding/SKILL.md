---
name: openglad-modding
description: Writing, testing, and shipping OpenGlad class packs and level scripts (Lua mods — new classes, scripted objects, custom specials, per-level hooks). Use whenever the task creates or modifies anything under packs/, touches og.* Lua APIs, adds families/weapons/effects/treasures, or debugs pack loading/dispatch/determinism. Also use for transliterating C++ family behavior to Lua.
---

# OpenGlad Modding (Class Packs + Level Scripts)

Everything mod-related is Lua + YAML in a pack — never native code. The three
canonical references (read in this order for any non-trivial task):

1. `docs/lua-classpacks-design.md` — architecture; **§3 Determinism Cookbook
   (R1–R10) is non-negotiable law** for anything that runs inside the sim.
2. `docs/modding/api-reference.md` — every og.* function, walker/stats/guy
   method, constant, and the transliteration checklist.
3. Worked examples: `packs/core/scripts/soldier.lua` (canonical style,
   side-by-side with `src/gameplay/families/family_soldier.cpp`) and
   `packs/core/scripts/orc.lua` (rand-guarding, raw frozen-delay, guy exp).

## Pack anatomy

```
packs/<pack_id>/
├── classpack.yaml     # descriptor data: stats, sprites, animation, costs
└── scripts/*.lua      # behavior hooks; loaded filename-lexicographically;
                       # one shared environment per pack
```

Packs under the repo `packs/` dir ship with the game (staged to the build
tree; preloaded on wasm). User packs live in `~/.openglad/packs/`. Campaign
zips may embed `packs/` (mounted for that campaign only).

## The rules that bite

- **Determinism**: every integer `/`/`%` → `og.div`/`og.mod`; every float op
  → one `og.f*` call; randomness ONLY via `og.rand` (or `og.cosmetic_rand`
  at C++ cosmetic-selector sites); `og.rand(n)` errors on n≤0 while C++
  `next(0)` silently returns 0 — guard. No `pairs` (doesn't exist). Arrays
  only. Never format floats into sim-visible strings.
- **Hook errors fall back to any still-present C++ callback** — fail at
  branch entry or not at all; a partial script run + C++ fallback
  double-executes side effects.
- **Family ids**: `"core:<registry-name-lowercased>"`; collisions (BEAST ×3)
  use `"core:#<id>"`. `og.family_id(order, id_str)` resolves or nil.
- **State**: no mutable sim state in globals/upvalues. Per-entity persistent
  state = declared `state_slots` (snapshot-serialized).

## Testing a mod / conversion

```bash
cmake --build --preset ci-test --target stage_runtime_assets  # after .lua edits
./build/ci-test/og_test_parity            # 188/188 required for core changes
./build/ci-test/og_unit_script --gtest_brief=1
./build/ci-test/og_unit_families --gtest_brief=1
```

- `.lua`/`.yaml` edits need only re-staging, never a C++ rebuild.
- **Dispatch proof** (mandatory when converting core behavior): perturb one
  constant in the STAGED copy (`build/ci-test/packs/...`), confirm the
  targeted parity scenarios flip, restore, confirm green. A green run with
  no flip on perturbation means your hook never dispatched (load error →
  check `ScriptHost` errors, or no scenario coverage → document it).
- Load errors are recorded per-VM and reported under TESTING traces
  (`script_error` category); `og.log(...)` output lands in the `script`
  trace category.

## New-class quickstart

1. Copy the closest core family's YAML block in `packs/core/classpack.yaml`
   into your pack's `classpack.yaml`; set `id: <pack>:<name>`,
   `wire_id: auto`, pick `sprite` + `animation` (reuse a core set first).
2. `scripts/<name>.lua`: `og.register_hooks("living", "<pack>:<name>",
   {...})` — start from soldier.lua's shape.
3. Stage + run the game (or `openglad_text`) — the class appears in the
   hire menu when `playable: true`.

## Gotchas index

- Two RNG draws in one C++ expression (transliteration): left-first
  temporaries matched GCC everywhere adjudicated so far — but always flag.
- `stats` accessors are flattened with the `s_` prefix; guy record `g_`.
- `hit_response`'s Lua `self` is the stats OWNER (the C++ got statistics*).
- Handles are dispatch-scoped for dying entities; `==` compares entity id.
- Registering an empty hook table is a load error by design; a family with
  all-null C++ callbacks gets a comment-only chunk (see tower1.lua).
- Instruction budget 5M/hook, memory 32MiB/VM — a runaway loop is a
  deterministic script error on every peer, not a hang.
