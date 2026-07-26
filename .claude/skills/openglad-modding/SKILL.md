---
name: openglad-modding
description: Writing, testing, and shipping OpenGlad class packs and level scripts (Lua mods — new classes, scripted objects, custom specials, per-level hooks). Use whenever the task creates or modifies anything under packs/, touches og.* Lua APIs, adds families/weapons/effects/treasures, or debugs pack loading/dispatch/determinism. Also use for transliterating C++ family behavior to Lua.
---

# OpenGlad Modding (Class Packs + Level Scripts)

Everything mod-related is Lua + YAML in a pack — never native code. This file
is the **playbook**: how to build, stage, test, and prove a mod. The two
reference documents, in reading order for any non-trivial task:

1. `docs/lua-classpacks-design.md` — architecture and pack format. **§3
   Determinism Cookbook (R1–R10) is non-negotiable law** for anything that
   runs inside the sim; §4 is the `classpack.yaml` schema.
2. `docs/modding/api-reference.md` — every `og.*` function, walker/stats/guy
   method, constant, and hook signature, plus a guided tour of the runnable
   example pack in `docs/modding/examples/emberwisp/` (yaml + lua + art).

Real code to read side by side: `packs/core/scripts/soldier.lua` against its
`core:soldier` entry in `packs/core/classpack.yaml` (canonical style — the
engine has no C++ family sources left to compare with),
`packs/core/scripts/orc.lua` (rand-guarding, raw frozen-delay, guy exp), and
`tools/concept_mapgen/showcase_pack.cpp` (the Ninefold Court's `court.lua` —
level hooks, per-entity hooks, generator `customize_spawn`).

## Pack anatomy

```
packs/<pack_id>/
├── classpack.yaml     # descriptor data: stats, sprites, animation sets,
│                      # costs, glyph/radar presentation
├── sprites/*.png      # optional pack-shipped art (indexed PNG)
└── scripts/*.lua      # behavior hooks; every .lua is loaded, filename-
                       # lexicographically; one shared environment per pack
```

Packs under the repo `packs/` dir ship with the game (staged to the build
tree, preloaded on wasm). User packs live in `<user_path>/packs/`. Campaign
zips may embed `packs/`, mounted for that campaign only. There is no
per-family `script:` key — the whole `scripts/` directory is loaded.

## The rules that bite

- **Determinism**: every integer `/` and `%` → `og.div`/`og.mod`; every float
  op → one `og.f*` call; randomness ONLY via `og.rand` (or `og.cosmetic_rand`
  at C++ cosmetic-selector sites). `og.rand(n)` errors on `n <= 0` while C++
  `next(0)` silently returns 0 — guard it. No `pairs` (it does not exist).
  Arrays only. Never format a float into a sim-visible string.
- **Hook errors fall back to any still-present C++ callback** — so fail at
  branch entry or not at all; a partial script run plus the C++ callback
  double-executes side effects.
- **Family ids resolve fully-qualified first, bare name second.** Three forms
  in order: `"pack:#<byte>"` (exact byte), `"pack:name"` (exact match on the
  `id:` a mounted pack declared — the namespace IS a scope, so two packs can
  both ship a `WARLOCK`), then the bare-name fallback that ignores the
  namespace and takes the lowest matching `name:`. Case- and space/underscore-
  insensitive. Always address families by their qualified id; the fallback
  means `mypack:soldier` still finds core's SOLDIER when your pack has no
  `soldier`, which is convenience, not a scope check. Give a new family a
  unique `name:` anyway — and never omit it, since a free living slot defaults
  to `BEAST`, which three core families already answer to. Genuine core name
  collisions use `"core:#<id>"`. `og.family_id(order, id_str)` resolves or
  returns nil; call it once at chunk load into a `local`.
- **No mutable sim state in globals or upvalues.** There is no per-entity
  script storage (`state_slots:` is a forward-compat key nothing reads).
  Re-derive from the world every dispatch: census via `og.oblist()`, cadence
  via `og.mod(tick, N)`, stable subsets via `og.mod(og.entity_id(e), N)`,
  marks via a stats bit flag or `s_set_name`.

## Testing a mod / conversion

```bash
cmake --build --preset ci-test --target stage_runtime_assets  # after .lua/.yaml edits
./build/ci-test/og_test_parity            # 188/188 required for core changes
./build/ci-test/og_unit_script --gtest_brief=1
./build/ci-test/og_unit_families --gtest_brief=1
OPENGLAD_CONFIG_DIR=$(mktemp -d) ./build/ci-test/og_unit_data --gtest_brief=1
```

- `.lua` and `.yaml` edits need only re-staging, never a C++ rebuild.
- `og_unit_data` carries the `classpack.yaml` parser and install tests, and
  needs an isolated config dir (a headless run with nothing mounted otherwise
  rewrites the repo's `cfg/openglad.yaml` through the cwd fallback).
- **Dispatch proof** (mandatory when converting core behavior): perturb one
  constant in the STAGED copy (`build/ci-test/packs/...`), confirm the
  targeted parity scenarios flip, restore, confirm green. A green run with
  no flip on perturbation means your hook never dispatched — either a load
  error (check the diagnostics below) or no scenario coverage; say which.

### Reading the diagnostics

- Load and runtime errors trace under `script_error` with source location and
  message, on **every** occurrence.
- The stored error records are capped at 64 *distinct* `(where, message)`
  pairs and repeats collapse into a `count`, so "this fired once" in the
  record list can mean "this fired 40,000 times". Read the count, and read
  `dropped_error_count()` before concluding a pack is clean.
- `og.log(...)` / `print(...)` traces under `script`; the stored transcript
  is the most recent 512 lines, oldest evicted.
- A duplicate hook registration logs a warning naming the order, family, hook
  and source location, and is recorded as an error too.

### Choosing a perturbation that actually proves dispatch

The parity dump records sim state, not presentation. Perturbing these proves
nothing — they are **parity-invisible**:

| Invisible | Visible |
|---|---|
| sounds, notifications | hitpoints, dead flag |
| lifetimes, MP grants¹ | positions, weapon tracks |
| damage values that never land² | level, team, charm/freeze state |
| glyphs, radar colours, editor labels | |

¹ potion stat grants are invisible; only the *consumption* (set_dead) shows.
² an explosion nobody is standing in changes no recorded state.

If every perturbation you try is inert, the honest conclusion is usually
"this path has no parity coverage" — say so rather than inventing a proof.

### Order-of-evaluation traps

C++ leaves evaluation order unspecified where Lua does not. Adjudicated per
site by parity, and the answers genuinely differ:

- `rng(a) >= rng(b)` (comparison operands) → **left-first** (thief, orc)
- `f(..., rng(3), rng(3))` (call arguments) → **right-first** (slime grow)

Write explicit temporaries, pick an order, and flip it if parity fails. Never
assume the previous site's answer generalises.

Also: calls that *look* pure consume the RNG stream. `attack()`,
`query_object_passable()` (via the obmap miss roll), `og.charm_duration`,
`og.freeze_duration` and `og.heal_amount` all draw. Reordering or
short-circuiting them changes the stream even if nothing else differs.

## New-class quickstart

1. Copy `docs/modding/examples/emberwisp/` (a runnable pack with its own art
   and animation table), or lift the closest core family's block out of
   `packs/core/classpack.yaml`.
2. Set `id: <pack>:<name>`, a **unique** `name:`, `wire_id: auto`, and a
   `sprite:` (a living with no sprite has no graphics and cannot be drawn).
   Pick an `animation:` — reuse a built-in set (`standard`, `mage`,
   `skeleton`, `giant_skeleton`, `slime`, `small_slime`, `static`) before
   authoring your own `anims:` table.
3. Give it presentation so it reads correctly outside the SDL client:
   `glyph` / `glyph_ascii` / `glyph_color` for the curses client,
   `radar_color` for the minimap, `editor_label` for a generator. Undeclared
   presentation falls back to the legacy "unknown family" look, which is a
   valid but anonymous result.
4. `scripts/<name>.lua`: `og.register_hooks("living", "<pack>:<name>", {...})`
   — start from soldier.lua's shape.
5. Stage, then run the game or `openglad_text`. A `playable: true` family
   appears in the hire menu.

## Shipping a family that replaces a core one

Registration is last-wins, and pack scripts load filename-lexicographically
within a pack, packs in pack-id order. So a mod pack registering
`core:soldier` overrides the core pack's soldier — that is the supported way
to reskin a stock class. The engine warns on every re-registration, naming
the family and hook, so an *accidental* collision is diagnosable.

Consequence for multi-file packs: if two of your own scripts register the
same family, the lexicographically later filename wins. Split by family, not
by concern.

Replacing its **data** (stats, sprite, description) is a `classpack.yaml`
entry that pins the stock family's `wire_id` and **keeps its `id:`** —
`id: core:soldier`, `wire_id: 0`. The wire slot is the identity: an entry
claiming slot 0 under some other `id:` retires `core:soldier`, which then only
resolves through the bare-name fallback (and not at all if you renamed it
too). The installer warns when an install changes a slot's declared id.

## Level scripts

Level hooks are ordinary pack scripts registered against level ids
(`og.register_level_hooks(level_id, {...})`, `-1` = every level); a campaign
ships them in an embedded pack that mounts and unmounts with it.

- `on_load` runs on the **first tick of the level on this peer** — fresh
  start and mid-join alike. Derive everything from the world; be idempotent.
  There is no "start of fight" moment you can assume.
- `on_entity_death` covers living **and generator** deaths, so a tower or
  pillar falling is an event, not something to poll for.
- `on_entity_spawn` covers only sim-authored `add_ob` spawns; snapshot and
  replay paths stay silent.
- `og.set_entity_hooks(ent, { on_death = fn })` attaches a one-shot
  per-entity hook (consumed when it fires); register from `on_load` or
  `on_entity_spawn` after selecting the entity.

## Gotchas index

- Two RNG draws in one C++ expression: adjudicate per site (above).
- `stats` accessors are flattened onto the walker with the `s_` prefix; the
  guy record uses `g_`. `s_do_command()` is the only binding that *runs* the
  command queue; the rest only edit it. `s_force_fright` MERGES into a
  forced walk at the queue front — it is not `s_force_command`.
- `hit_response`'s Lua `self` is the stats OWNER (the C++ got a `statistics*`).
- `level_up`'s first argument is a **guy handle** (`g_*` methods only), and it
  runs picker-side with no world.
- Handles are dispatch-scoped; `==` compares entity id. Stashing one is a
  clean script error on next use, not UB.
- `find_teleport_target()` is treasures-only and `do_bounce`/`set_do_bounce`
  are weapons-only; both error on any other order.
- `set_floor` before `setxy` when placing on a non-default floor (obmap
  re-buckets on every position change). `og.summon` already did both.
- Registering an empty hook table is a load error by design; a family with
  all-null C++ callbacks gets a comment-only chunk (see tower1.lua).
- Instruction budget 5M per host entry, memory 32 MiB per VM — a runaway loop
  is a deterministic script error on every peer, not a hang. The budget arms
  on the outermost entry only, so nested dispatch (`g_upgrade_to_level`
  re-entering `level_up`) does not re-arm it.
