---
name: openglad-modding
description: Writing, testing, and shipping OpenGlad class packs and level scripts (Lua mods — new classes, scripted objects, custom specials, per-level hooks). Use whenever the task creates or modifies anything under packs/, touches og.* Lua APIs, adds families/weapons/effects/treasures, or debugs pack loading/dispatch/determinism. Also use for transliterating C++ family behavior to Lua.
---

# OpenGlad Modding (Class Packs + Level Scripts)

Everything mod-related is Lua + YAML in a pack — never native code. This file
is the **playbook**: how to build, stage, test, and prove a mod. The three
reference documents, in reading order for any non-trivial task:

1. `docs/lua-classpacks-design.md` — architecture and pack format. **§3
   Determinism Cookbook (R1–R10) is non-negotiable law** for anything that
   runs inside the sim; §4 is the descriptor YAML schema.
2. `docs/modding/api-reference.md` — every `og.*` function, walker/stats/guy
   method, property, constant, and hook signature, plus a guided tour of the
   runnable example pack in `docs/modding/examples/emberwisp/` (yaml + lua +
   art).
3. `docs/lua-style.md` — the style contract (S1–S6): naming, one-line
   headers, comment policy, when a helper becomes a lib module, shim policy,
   and the `s_*` deprecation rules. Binding for all in-tree pack Lua.

Real code to read side by side: `packs/core/scripts/soldier.lua` against
`packs/core/families/living-00-soldier.yaml` (canonical style — the engine
has no C++ family sources left to compare with), `packs/core/scripts/orc.lua`
(specials table, `og.rand0`, tuning reads, `add_frozen_stun`, guy exp),
`packs/core/lib/living_common.lua` + `ai.lua` + `effect_common.lua` (what
`og.use` modules look like), and `tools/concept_mapgen/showcase_pack.cpp` (the Ninefold Court's
`court.lua` — level hooks, per-entity hooks, generator `customize_spawn`).

## Pack anatomy

```
packs/<pack_id>/
├── classpack.yaml       # descriptor data: stats, sprites, animation sets,
│                        # costs, glyph/radar presentation, tuning: blocks
├── families/*.yaml      # optional split layout: parsed after classpack.yaml
│                        # in sorted filename order, same schema, ONE pack
├── lib/*.lua            # og.use modules: pure exports, loaded once per VM
├── sprites/*.png        # optional pack-shipped art (indexed PNG)
└── scripts/*.lua        # behavior hooks; every .lua is loaded, filename-
                         # lexicographically; one shared environment per pack
```

Packs under the repo `packs/` dir ship with the game (staged to the build
tree, preloaded on wasm). User packs live in `<user_path>/packs/`. Campaign
zips may embed `packs/`, mounted for that campaign only. There is no
per-family `script:` key — the whole `scripts/` directory is loaded. The
shipped core pack uses the split layout: a header-only `classpack.yaml` plus
one `families/<order>-<NN>-<slug>.yaml` per family (NN = pinned wire id).

## The rules that bite

- **Determinism**: every integer `/` and `%` → `og.div`/`og.mod`; every float
  op → one `og.f*` call (plain `+`/`-`/`*` is fine when both operands are
  provably integer-valued and < 2^24); randomness ONLY via `og.rand` /
  `og.rand0` (or `og.cosmetic_rand` at C++ cosmetic-selector sites).
  `og.rand(n)` errors on `n <= 0`; `og.rand0(n)` answers 0 there WITHOUT
  advancing the stream (exactly C++ `next(0)`) — use `og.rand0` when the
  bound can legitimately reach zero (level/tuning-scaled), plain `og.rand`
  when it is provably positive (the error is a tripwire). No `pairs` (it
  does not exist). Arrays only. Never format a float into a sim-visible
  string.
- **The new-idiom surface** (prefer these over raw method chains):
  - *Properties*: `self.hp`, `self.max_hp`, `self.magicpoints`,
    `self.max_magicpoints`, `self.level`, `self.team` read AND write;
    `self.busy = v`, `self.dead = 1`, `self.ani_type = v`, `self.foe = h`,
    `self.lifetime = v`, `self.damage = v` write-only. **Reads resolve
    method-first**: `self.busy` (no parens) answers the method *function*,
    not the value — reads of `busy`/`dead`/`ani_type`/`foe`/… stay method
    calls (`self:busy()`). Property writes narrow through the same setter
    the method used.
  - *Fused verbs*: `ob:add_frozen_stun(n)` (stun_total over RAW
    frozen_delay + setter), `self:heal_clamped(amount[, source])`,
    `og.summon_configured(self, order, family, opts)` — each documents and
    preserves an exact C++ op order; never re-spell those sequences.
  - *`og.combat.*`*: draw-free combat_math.h formulas (`yell_radius`,
    `stun_total`, `bomb_damage`, `cloak_total`, `glow_bonus`, lifetimes).
    Never hand-inline a formula that exists there.
  - *`og.max/min/clamp/sign`*: std:: tie semantics, C++-covered branches —
    use instead of if/else clamp ladders.
  - *`og.tuning(self)`*: the family's `tuning:` YAML map as a frozen table.
    Balance constants belong there, not inline in behavior code. Key access
    only; absent keys answer nil; no iteration.
  - *`specials` table*: `specials = { [1]=fn, ..., default=fn }` in
    `og.register_hooks` instead of a `current_special()` elseif ladder.
    Missing index → `default`; neither → successful no-op (`true`, no Lua
    call). The engine gates on `special_costs[current_special]` MP before
    dispatch and deducts it only on a `true` answer.
  - *`og.use("name")`*: binds `packs/<id>/lib/<name>.lua` (own pack only),
    at chunk load time only, frozen pure exports. A helper used by 2+ files
    goes in lib/; a single-file helper stays a `local function`.
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
- **The line lints.** One *statement* per line (`if low then flee() end`
  makes the branch body share a coverage point with its guard, so an
  untested branch reads as covered), one *short-circuit* (`and`/`or`) per
  line, one `function` keyword per line.
  `scripts/check_lua_statement_lines.py` rejects violations on every build
  (statements also after `;`, after `do`/`else`/`repeat`, and on a function
  header line; empty blocks are fine). Applies to `packs/` (scripts AND
  lib), the example packs under `docs/modding/`, and pack Lua inside a C++
  `R"LUA(` literal.
- **No mutable sim state in globals or upvalues.** There is no per-entity
  script storage (`state_slots:` is a forward-compat key nothing reads).
  Re-derive from the world every dispatch: census via `og.oblist()`, cadence
  via `og.mod(tick, N)`, stable subsets via `og.mod(og.entity_id(e), N)`,
  marks via a stats bit flag or `s_set_name`. The same rule is enforced at
  the module boundary: `og.use` exports are frozen, and a lib module must
  be a pure table of functions/constants — no chunk-level mutable state.

## Testing a mod / conversion

```bash
cmake --build --preset ci-test --target stage_runtime_assets  # after .lua/.yaml edits
./build/ci-test/og_test_parity            # 188/188 required for core changes
./build/ci-test/og_unit_script --gtest_brief=1
./build/ci-test/og_unit_families --gtest_brief=1
OPENGLAD_CONFIG_DIR=$(mktemp -d) ./build/ci-test/og_unit_data --gtest_brief=1
```

- `.lua` and `.yaml` edits (scripts, lib modules, `classpack.yaml`,
  `families/*.yaml`) need only re-staging, never a C++ rebuild.
- `og_unit_data` carries the `classpack.yaml` parser and install tests, and
  needs an isolated config dir (a headless run with nothing mounted otherwise
  rewrites the repo's `cfg/openglad.yaml` through the cwd fallback).
- **Dispatch proof** (mandatory when converting core behavior): perturb one
  constant in the STAGED copy (`build/ci-test/packs/...` — a `.lua` or a
  `tuning:` value in the staged YAML, both are staged data), confirm the
  targeted parity scenarios flip, restore, confirm green. A green run with
  no flip on perturbation means your hook never dispatched — either a load
  error (check the diagnostics below) or no scenario coverage; say which.
- **Refactoring the shipped core pack** (not new mods): drive batches
  through `scripts/refactor/probe.sh` — build, parity OFF + ARMED, coverage,
  pin-map check, instruction-budget diff, auto-revert on red.

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

1. Copy `docs/modding/examples/emberwisp/` (a runnable pack with its own
   art, animation table, tuning block and specials-table special), or lift
   the closest core family's `packs/core/families/<order>-<NN>-<slug>.yaml`.
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
   — start from the emberwisp script's shape (properties, `og.tuning`,
   a `specials` table). Balance constants go in the family's `tuning:`
   block, not inline; specials also need `special_names`/`special_costs`
   in the descriptor (the engine gates and charges the MP cost).
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

Replacing its **data** (stats, sprite, description) is a descriptor entry
that pins the stock family's `wire_id` and **keeps its `id:`** —
`id: core:soldier`, `wire_id: 0` (in your pack's `classpack.yaml` or a
`families/*.yaml` file; the split layout parses identically). The wire slot
is the identity: an entry claiming slot 0 under some other `id:` retires
`core:soldier`, which then only resolves through the bare-name fallback (and
not at all if you renamed it too). The installer warns when an install
changes a slot's declared id. A sparse entry patches only the fields it
declares — but any entry that touches a slot replaces that slot's `tuning:`
map whole.

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
- **Property reads are method-first.** `local b = self.busy` hands you the
  method *function* (truthy!), not the value — a `if self.busy > 0` typo is
  a comparison error at best. Value reads for the shadowed names stay
  method calls; only `hp`/`max_hp`/`magicpoints`/`max_magicpoints`/
  `level`/`team` read as values. All listed properties *write* correctly.
- `og.use` works at chunk load only (bind to a `local` at the top; a
  dispatch-time call errors) and resolves within the calling pack — you
  cannot import another pack's lib.
- `og.tuning(self)` is frozen (writes raise) and non-iterable; absent keys
  answer `nil`, so `t.key or DEFAULT` is the out-of-tree pattern (in-tree
  core scripts read keys bare — the YAML is the single source of truth).
  Any descriptor entry that touches a slot replaces its tuning map WHOLE.
- A `specials` table with no entry for the cast slot and no `default` is a
  *successful* no-op: the engine still deducts the slot's MP cost (that is
  the retired ladders' unmatched case, preserved). Return `false` from an
  entry to fire-and-charge-nothing.
- `stats` accessors are flattened onto the walker with the `s_` prefix; the
  guy record uses `g_`. Where a property or fused verb exists, the `s_*`
  spelling is a legacy alias (style S6) — new code uses the property.
  `s_do_command()` is the only binding that *runs* the
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
