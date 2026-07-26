# Lua Class Packs: Hotpluggable Families, Scripted Objects, and Level Scripting

Status: implementation in progress on `feature/lua-classpacks`.

This document is the reference for the class-pack system: architecture,
determinism rules, pack format, and dispatch design. Anyone converting a
family to Lua or writing a mod MUST follow the Determinism Cookbook below;
`og_test_parity` is the enforcement gate.

## 1. Goals

- Character families (living/weapon/effect/treasure/generator) defined by
  **packs**: zip archives carrying YAML descriptors, Lua behavior scripts,
  sprite PNGs, and animation tables. No native code in packs, ever.
- The 21 built-in living families plus weapon/effect/treasure behaviors ship
  as the **core pack**, built from `packs/core/` at build time and mounted
  like builtin campaigns. The engine carries no compiled-in family behavior.
- Campaigns may embed packs (`packs/` inside the campaign zip) that mount for
  that campaign only.
- Levels may carry scripts: level-wide hooks and per-entity hooks.
- Multiplayer transfers missing/mismatched packs host→client automatically.
- Deterministic across gcc/x86_64 and Emscripten/wasm: byte-identical sim,
  parity goldens unchanged for core families.

Non-goals: editor UI for authoring scripts (sidecar YAML + docs + agent
skills cover authoring); mod security beyond sandbox+budgets (packs are
data, but Lua runs untrusted — sandbox handles it).

## 2. Architecture

```
og_lua (static lib)            vendored Lua 5.4.8, compiled as C++ (errors
                               become exceptions; RAII-safe with -fexceptions,
                               which the web build already sets globally)
gameplay/script/               ScriptHost (pimpl; lua.h never escapes),
                               og.* binding layer, hook trampolines
resources/packs/               classpack.yaml reader, pack mount/enumerate,
                               pack hashing; pushes descriptors + script
                               sources into gameplay registries
```

- Component rules hold: gameplay depends only on core (+og_lua, an external
  lib like GTest); resources parses YAML/zip (libyaml/libzip stay behind
  resources IO); `check_vendor_leaks.sh` gains `lua.h|lauxlib.h|lualib.h|lua.hpp`
  patterns, allowed only under `src/gameplay/script/`.
- **VM ownership**: each `GameWorld` owns a `ScriptHost` (server world and
  local mirror get separate VMs — same isolation rule as obmap). Hook
  trampolines resolve the VM via `current_game->world`, the existing context
  mechanism. A separate ambient **UI host** serves picker-side hooks
  (`level_up`, `promotion_new_level`) that run outside any world.
- VM lifecycle: created at world init; executes all mounted packs' scripts
  (deterministic order: pack id lexicographic, then script path
  lexicographic); destroyed with the world. Script errors during load =
  pack rejected (deterministic).

## 3. Determinism Cookbook (MANDATORY for all pack Lua)

Lua 5.4 integers are int64 and exact. Lua floats are C doubles. The sim uses
C++ `float` in places (hitpoints, busy, damage). The rules below make every
transliterated expression bit-identical to the C++ original.

**R1 — Integer division and modulo.** Never use `//`, `/`, or `%` on
integers. Use `og.div(a,b)` / `og.mod(a,b)` (C semantics: truncate toward
zero; div-by-zero raises a script error). Lua `//`/`%` floor instead of
truncate and differ for negative operands.

**R2 — Float arithmetic is per-op through bindings.** Every C++ float
operation maps to exactly one call: `og.fadd(a,b)`, `og.fsub`, `og.fmul`,
`og.fdiv` — each casts operands to `float`, performs the op in `float`, and
returns the widened result. Chains keep per-op float rounding this way.
  - Exception (allowed, for readability): a SINGLE `+`, `-`, or `*` whose
    operands are floats and whose exact result fits a double is identical
    either way; but when in doubt, use `og.f*`. Division is NEVER done in
    Lua (double rounding).
  - Float comparisons in Lua are safe (float→double widening is exact).

**R3 — Narrowing writes go through typed helpers.** C++ stores into
`char`/`short`/`unsigned char` wrap. Use `og.i8(x)`, `og.i16(x)`, `og.u8(x)`,
`og.i32(x)` to reproduce the wrap at exactly the sites the C++ narrowed, and
`og.trunc(x)` for `static_cast<int32>(float)` (truncation toward zero).
Field setters additionally clamp/wrap to the underlying field type, matching
the C++ member types.

**R4 — RNG only via `og.rand(n)`** (routes to `current_game->world->rng_`).
Preserve the ORDER and COUNT of rand calls exactly when transliterating.
`math.random` does not exist in the sandbox.

**R5 — Arrays only; `pairs`/`next` do not exist.** Hash-part iteration order
depends on a per-run seed (Lua 5.4 mixes heap addresses into it), so the
sandbox does not provide `pairs`/`next` at all. Use arrays +
`ipairs`/numeric `for`; keyed lookup tables are fine (indexing is
deterministic), you just cannot enumerate them. Binding functions that
return entity sets return ARRAYS in the same order the C++ iteration
produced (oblist order). `tostring` is an address-free variant (no
`table: 0x...` text) so pointer bits can never influence script behavior.

**R6 — No hidden state.** Hook functions may use locals and read/write
walker fields and declared per-entity state slots (`og.get_slot(self, i)` /
`og.set_slot(self, i, int32)`; slots are declared in classpack.yaml,
serialized in snapshots/saves). Upvalues/globals holding mutable sim state
are forbidden — they would escape snapshots. Constants are fine.

**R7 — Sandbox floor.** Not available: `io`, `os`, `package`/`require`,
`load`/`loadstring`/`dofile`/`loadfile`, `collectgarbage`, coroutines,
`debug`, `utf8`, `pairs`/`next` (R5), `string.dump`, `math.random`, float
transcendentals (`sin`, `exp`, `log`, `sqrt`, `^` produces floats — avoid).
Available: `string` (incl. `format`; never format floats or tables into
sim-visible strings), `table`, integer `math` subset (`floor`, `ceil`,
`abs`, `min`, `max`, `tointeger`, `type`, `maxinteger`, `mininteger`,
`huge`, `pi`, `ult`), `ipairs`, `select`, address-free `tostring`,
`tonumber`, `pcall`/`xpcall`, `print` (= `og.log`).

**R8 — Budgets.** Per hook invocation: instruction budget (default 5M,
`LUA_MASKCOUNT` hook) and per-VM memory cap (default 32 MiB, counting
allocator). Blowing a budget raises a deterministic script error.

**R9 — Hook errors are deterministic fallbacks.** A hook that errors traces
(`TRACE("script", ...)`) and behaves as if the hook were absent (descriptor
default path). Under TESTING builds the error is additionally latched for
test assertion. Same behavior on every peer, so no divergence.

**R10 — String hash seed is fixed** (`luai_makeseed` override) so any
incidental hash-order exposure is at least identical across builds; R5 still
applies.

## 4. Pack format

```
mypack.gladpack (zip; PhysFS-mounted at packs/<pack_id>/)
├── classpack.yaml
├── sprites/<name>.png            # indexed-color, LodePNG path
├── anims/<set>.yaml              # optional custom animation tables
└── scripts/<file>.lua            # behavior hooks
```

`classpack.yaml` (labels follow campaign.yaml conventions):

```yaml
pack: org.example.mypack
version: 3
title: My Pack
authors: ...
families:
  living:
    - id: mypack:warlock          # string id, namespaced by convention
      wire_id: auto               # core pack pins 0..20; mods use auto
      name: WARLOCK
      base_stats: [12, 6, 12, 8, 9, 1]
      # ... every static FamilyDescriptor field, same names as the struct
      sprite: sprites/warlock.png
      animation: mage             # named set: standard|mage|skeleton|... or anims/<set>
      state_slots: 2              # per-entity int32 slots, snapshot-serialized
      script: scripts/warlock.lua # registers hooks for this id
  weapon:  [...]                  # WeaponFamilyDescriptor fields + hooks
  effect:  [...]
  treasure: [...]
  generator: [...]
```

Concrete field inventory (v1), mirroring the descriptor structs and the
gloader EntityDef table exactly:

```yaml
families:
  living:
    - id: core:soldier
      wire_id: 0                     # core pins legacy bytes; mods: auto
      name: SOLDIER
      short_name: ~                  # optional picker label
      base_stats: [12, 6, 12, 8, 9, 1]        # STR DEX CON INT ARMOR LVL
      hiring_cost: 250
      derived_bonuses: [120, 0, 20, 0, 0, 0, 4, 6]  # HP MP ATK RATK RNG DEF SPD ATKSPD
      stat_costs: [6, 10, 6, 25, 50, 200]
      special_costs: [5000, 25, 100, 120, 150, 5000]
      weapon_cost: 2
      default_weapon: core:knife     # resolved through the weapon registry
      init_bit_flags: []             # names: FLYING, SWIMMING, ...
      init_ani_type: 0
      init_max_magicpoints: 0
      special_names: [NONE, CHARGE, BOOMERANG, WHIRLWIND, DISARM, NONE]
      alternate_names: [NONE, NONE, NONE, NONE, NONE, NONE]
      leaves_bloodspot: true
      magic_damage_modifier: 1.0
      is_stationary: false
      has_returning_weapon: true
      is_undead: false
      promotes_to: ~                 # family id string or null
      promotion_level_req: 0
      death_message: SOLDIER SLAIN
      sprite: sprites/footman.png
      animation: standard            # named set (see anims section)
      ai_line_of_sight: 7
      description: |
        Your basic grunt...
      names: [Lothar, Arthur, ...]
      playable: true
      playable_order: 0
      state_slots: 0
  weapon:
    - id: core:knife
      wire_id: 0
      name: KNIFE
      sprite: sprites/knife.png
      animation: knife
      hitpoints: 6
      act_type: fire                 # fire|sit|random|control|generate|die
      stepsize: 5
      ai_line_of_sight: 7
      damage: 6
      fire_frequency: 0
      fire_sound: fwip
      skip_sit_notify: false
      is_auto_attackable: false
      init_bit_flags: []
      init_lifetime: 0
      init_ani_type: 0
      vz: 0.0
      gravity: 0.0
      sizez: 0
      can_drop_floors: false
  # treasure/generator/fx sections carry their descriptor fields the same way
anims:
  # Named frame tables replacing the gloader constexpr arrays. A set is a
  # list of rows (one per ani_type*8+facing slot; a set may give 8, 16, or
  # 32 rows, or use `repeat:` shorthand); frames are sprite indices, row
  # playback ends at the sentinel (no -1 in YAML; row end = end).
  knife:
    rows: 16
    frames:
      - [0, 1, 2, 3]                 # ...
```

Scripts register behavior:

```lua
og.register_hooks("living", "mypack:warlock", {
  do_special = function(self) ... end,
  on_death   = function(self) ... end,
})
```

Hook sets per order mirror the existing descriptor callbacks: living (14),
weapon (`on_death`, `on_animate`, `on_hit_target`), effect (`on_act`,
`on_death`), treasure (`on_eat`), generator (new: `customize_spawn`).

## 5. Identity: string ids, wire bytes, palette

- Runtime keeps int8 family bytes everywhere (walker, snapshots, saves,
  level entities) — zero hot-path cost, wire format shape unchanged.
- Registries map string id ↔ byte per order. **Core pack ids are pinned** to
  the legacy bytes (soldier=0 … tower1=20; likewise for the legacy
  weapon/effect/treasure/generator numeric ids), so existing levels, saves,
  goldens, and the wire stay byte-compatible.
- Mod families get bytes assigned at mount: campaign-embedded packs declare
  `wire_id: auto` and receive deterministic ids (assignment order = pack id
  lexicographic, then YAML order, first free byte from 21 up). Level files
  (FSS v11) therefore encode mod families without format changes; the
  campaign's own pack list IS the palette.
- Saves: v10 adds an optional pack-provenance chunk (list of
  `(pack_id, version, [family string ids in byte order])`) so a save with
  modded characters can remap or reject cleanly if packs changed. Legacy v9
  saves load unchanged (core ids are pinned).
- Missing pack at load: entity families with no registered byte fall back to
  a visible "unknown" descriptor (soldier body, warning glyph, name from the
  provenance chunk) rather than crashing — same spirit as the ani_count
  invariant.

## 6. Dispatch

`FamilyDescriptor` (and the weapon/effect/treasure/generator descriptor
structs) keep their function-pointer fields. Pack loading installs generic
**trampolines** for hooks the pack's script registered; the trampoline:

1. resolves `current_game->world->script_host()` (or the UI host for
   picker-side hooks),
2. looks up `hooks[order][family_byte][hook_name]` in the VM registry,
3. pushes typed arguments (walker handles), pcalls with budgets armed,
4. converts the result (bool hooks) and applies R9 on error.

Walker handles are validated: a per-world live-set (pointer membership,
maintained on create/destroy) makes a stale handle a script error instead of
UB. Handles never outlive the hook invocation (documented; enforced by a
generation counter bumped per dispatch in TESTING builds).

## 7. Level and entity scripting

Campaign zips gain `scripts/level<N>.lua` plus optional
`scripts/level<N>.yaml` (entity selectors → per-entity hooks):

- Level hooks: `on_level_load(world)`, `on_level_tick()`, `on_level_win()`,
  `on_entity_death(ent)`, `on_entity_spawn(ent)`.
- Entity selectors (YAML): `{order, family, index}` or `{order, family,
  near: [x, y]}` → attach named hook tables and initial state-slot values to
  matching level entities at load.
- Runs in the world's ScriptHost under the same cookbook. `on_level_tick`
  fires once per sim tick after walker acts (fixed point in the frame).

## 8. Multiplayer pack transfer (protocol v10)

- Lobby handshake gains a pack manifest exchange: `(pack_id, version,
  sha256, size)` for every non-core pack the session needs (host's mounted
  set for the selected campaign).
- Client compares against local cache (`user_path/packs/cache/<sha256>.gladpack`
  and installed packs); requests missing blobs; host streams chunks (32 KiB)
  over the reliable transport with progress events surfaced in the lobby
  (SDL lobby + text client lines).
- Caps: 16 MiB per pack, 64 MiB per session; violations reject the join
  with a reason string. Received packs mount read-only from cache and are
  never auto-installed outside it.
- `kNetworkProtocolVersion` 9 → 10 (the 5 literal wire-byte tests get
  updated alongside).

## 9. Testing strategy

- **Unit**: ScriptVM sandbox (banned symbols absent, budgets trip
  deterministically, og.div/og.mod/og.f*/og.i* cross-checked against C++
  semantics over sign/edge cases), palette assignment, classpack.yaml
  parsing, provenance chunk round-trip, transfer chunking over loopback.
- **Parity**: og_test_parity stays green through every family conversion —
  each family lands only when goldens still pass byte-identical. New parity
  scenarios cover: a Lua-only mod family, level scripting hooks, state
  slots across snapshot round-trips.
- **Integration**: picker lists pack families; save/load with mod
  characters; MP transfer e2e (host with pack, vanilla joiner); curses/text
  clients render mod families via descriptor glyphs.
- **Canary**: mutation pins re-anchored where sim files shifted; teeth run
  locally (`genuine toothless` must stay 0) — Lua family behavior gets its
  own mutation pins (mutate a .lua constant in the core pack, expect flips).

## 10. Rollout

1. `og_lua` + ScriptHost + sandbox/budget unit tests.
2. Pack reader + registries with string ids + core pack YAML (stats only;
   behavior still C++) — parity green checkpoint.
3. og.* binding layer + soldier converted (cookbook proven) — parity green.
4. Remaining families in waves (≤3 agents), parity per wave; delete C++
   behavior files as their Lua twins land.
5. Generators scripted; UI sweep (glyph/radar/editor/picker from
   descriptors).
6. Level/entity scripting; campaign-embedded packs.
7. MP transfer, protocol v10.
8. Skills/docs; concept-playground showcase; media; PR.
