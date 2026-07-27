# Lua Class Packs: Hotpluggable Families, Scripted Objects, and Level Scripting

Status: implementation in progress on `feature/lua-classpacks`. All 21 living
families plus the weapon/effect/treasure behaviors run as Lua in the core
pack; the C++ callbacks are still present as a fallback (see §9a).

This document is the **architecture and determinism** reference: how packs
are built, loaded, identified and dispatched, and the rules any sim-facing
Lua must obey. `og_test_parity` is the enforcement gate.

Companion documents, each in its own lane:

| Document | Lane |
|---|---|
| [docs/modding/api-reference.md](modding/api-reference.md) | Symbol-level reference: every `og.*` function, walker/stats/guy method, constant and hook signature, plus a guided tour of the runnable example pack in `docs/modding/examples/emberwisp/`. |
| [.claude/skills/openglad-modding/SKILL.md](../.claude/skills/openglad-modding/SKILL.md) | The practical playbook: build, stage, test, and *prove* a mod dispatches. |
| [AGENTS.md](../AGENTS.md) | Router for non-Claude agents. |

## 1. Goals

- Character families (living/weapon/effect/treasure/generator) defined by
  **packs**: directories or zips carrying YAML descriptors, Lua behavior
  scripts, sprite PNGs, and animation tables. No native code in packs, ever.
- The 21 built-in living families plus weapon/effect/treasure behaviors ship
  as the **core pack**, built from `packs/core/` at build time and mounted
  like builtin campaigns. The engine carries no compiled-in family behavior.
- Campaigns may embed packs (`packs/` inside the campaign zip) that mount for
  that campaign only.
- Levels may carry scripts: level-wide hooks and per-entity hooks.
- Multiplayer transfers missing/mismatched packs host→client automatically.
- Deterministic across gcc/x86_64 and Emscripten/wasm: byte-identical sim,
  parity goldens unchanged for core families.

Non-goals: an editor UI for authoring scripts (hand-written YAML + Lua, with
the docs and agent skills covering authoring); mod security beyond
sandbox+budgets (packs are data, but Lua runs untrusted — the sandbox
handles it).

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
- **VM ownership**: each `GameWorld` lazily owns a `WorldScripts` (a
  `ScriptHost` plus the registered-hook bitmasks). Server world and local
  mirror get separate VMs — the same isolation rule as the obmap. Dispatch
  resolves the VM via `current_game->world`, the existing context mechanism.
  A separate ambient **UI instance** serves picker-side hooks (`level_up`)
  that run outside any world; it rebuilds itself when the pack set changes.
- VM lifecycle: created on first use; replays every mounted pack's scripts in
  a deterministic order (pack id lexicographic, then file name lexicographic);
  destroyed with the world. A chunk that fails to compile or errors at load
  is recorded and skipped — deterministically, on every peer.
- Presentation types the descriptors carry (`og::FamilyGlyph`,
  `og::RadarBlip`, `og::GlyphColor`) are declared in **core**, not in
  `interface`: descriptors live in `gameplay`, and gameplay may only name
  types from core or gameplay. `check_vendor_leaks.sh` enforces the whole
  matrix, including keeping `lua.h` and friends under `src/gameplay/script/`.

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
walker/stats/guy fields. Upvalues and globals holding mutable sim state are
forbidden — they would escape snapshots and desync a peer that joins
mid-level. Constants (resolved family bytes, geometry, lookup tables) are the
one legitimate upvalue.

There is no per-entity script storage today: derive state from the world on
every dispatch instead. The world is the only durable place, and everything
in it is already snapshotted. Practical substitutes, all used in shipped
scripts:

- **Re-derive a census** each dispatch (`og.oblist()` filtered by order /
  family / team / `dead()`), rather than remembering a count.
- **Anchor cadence to the absolute tick** (`og.mod(tick, 300) == 0`), not to
  a remembered "ticks since X" origin.
- **Derive a stable subset from the entity id** — ids are assigned
  identically on every peer, so `og.mod(og.entity_id(spawn), 3) == 0` is a
  deterministic "every third spawn" with no counter.
- **Stamp the world itself** when you truly need a mark: a stats bit flag, a
  name, a lifetime. `s_name() == "Adjutant"` is snapshot-safe; a Lua table
  keyed by entity id is not.

`state_slots:` survives in some drafts as a *forward-compatibility* key — the
YAML reader skips it like any other unknown key, and no binding reads it. Do
not design around it yet.

**R7 — Sandbox floor.** Not available: `io`, `os`, `package`/`require`,
`load`/`loadstring`/`dofile`/`loadfile`, `collectgarbage`, coroutines,
`debug`, `utf8`, `pairs`/`next` (R5), `string.dump`, `math.random`, float
transcendentals (`sin`, `exp`, `log`, `sqrt`; `^` produces floats — avoid).
Available: `string` (incl. `format`; never format floats or tables into
sim-visible strings), all of `table`, an integer `math` subset (`floor`,
`ceil`, `abs`, `min`, `max`, `tointeger`, `type`, `maxinteger`,
`mininteger`, `huge`, `pi`, `ult`), `assert`, `error`, `ipairs`, `select`,
`type`, `tonumber`, address-free `tostring`, `pcall`/`xpcall`, the four
`raw*` functions, `setmetatable`/`getmetatable`, `_VERSION`, `_G` (the
sandbox root), and `print` (= `og.log`). The exact list is `kAllowedBase` /
`kAllowedMath` in `src/gameplay/script/script_host.cpp`.

**R8 — Budgets.** Per host entry: instruction budget (default 5M, via a
`LUA_MASKCOUNT` hook armed on the OUTERMOST call only, so a nested dispatch
such as `g_upgrade_to_level` re-entering `level_up` does not re-arm it) and
a per-VM memory cap (default 32 MiB, counting allocator). Blowing a budget
raises a deterministic script error — a runaway loop is an error on every
peer, not a hang.

**R9 — Hook errors are deterministic non-events.** A hook that errors is
treated as *absent for that dispatch*: same decision on every peer, so no
divergence. The error is always traced (`script_error`) and recorded on the
host (bounded and de-duplicated — see §6).

The consequence that bites: while the C++ callbacks are still present (§9a),
"absent" means the C++ callback runs next. **A hook must therefore fail
before it mutates sim state, or not at all** — a partial script run followed
by the full C++ callback double-executes side effects. Put the failure at
branch entry. Once the callbacks are deleted, "absent" degrades to *nothing
happening* instead, which is why script errors must stay loud.

**R10 — String hash seed is fixed** (`luai_makeseed` override) so any
incidental hash-order exposure is at least identical across builds; R5 still
applies.

## 4. Pack format

```
packs/<pack_id>/                  # a directory, or a zip mounted at this path
├── classpack.yaml                # descriptor data + animation sets
├── sprites/<name>.png            # optional pack-shipped art (indexed PNG)
└── scripts/<file>.lua            # behavior hooks
```

Three places a pack can live, all mounted into the same virtual `packs/`
tree and all read the same way:

| Location | Purpose |
|---|---|
| repo `packs/` → staged to the build tree, preloaded on wasm | ships with the game (this is where `packs/core` lives) |
| `<user_path>/packs/` | user-installed packs |
| `packs/` inside a campaign zip | mounts and unmounts **with that campaign** |

Loading is a whole-tree rescan (`refresh_pack_scripts`) triggered by
`io_init`, campaign mount/unmount, and multiplayer pack transfer. It clears
the registered script set, re-enumerates, and reinstalls descriptor data, so
an unmounted pack never leaves a family behind. Enumeration is sorted at both
levels — pack id, then file name — which is what makes script order, auto
wire-id assignment, and last-registration-wins reproducible on every peer.

Every `*.lua` under a pack's `scripts/` is loaded; there is no per-family
`script:` key. All of a pack's scripts share one environment.

### `classpack.yaml`

Top level: `pack`, `version`, `title`, `authors`, `families`, `anims`. Under
`families`, one sequence per order: `living`, `weapon`, `effect` (alias
`fx`), `treasure`, `generator`.

**Every key except `id` is optional, and an undeclared key changes nothing.**
The installer copies the registry slot's *current* descriptor, patches only
the fields the YAML declares, and stores it back — so a pack can restate one
number about a core family without touching anything else, and a fresh mod
slot starts from the order's defaults. Unknown keys and unknown nested
mappings are skipped for forward compatibility; a *malformed value* for a
known key (a bad integer, a bad boolean) fails the whole pack instead.

Fields, by order (names match the descriptor struct members):

```yaml
families:
  living:
    - id: core:soldier               # required; the namespace is decorative (§5)
      wire_id: 0                     # integer 0..255, or `auto` (>= 21)
      name: "SOLDIER"                # THE identity hooks resolve against (§5)
      short_name: ~                  # nullable: picker label override
      base_stats: [12, 6, 12, 8, 9, 1]              # STR DEX CON INT ARMOR LVL
      derived_bonuses: [120, 0, 20, 0, 0, 0, 4, 6]  # HP MP ATK RATK RNG DEF SPD ATKSPD
      stat_costs: [6, 10, 6, 25, 50, 200]           # STR DEX CON INT ARMOR LVL
      special_costs: [5000, 25, 100, 120, 150, 5000]  # index 0 unused
      hiring_cost: 250
      weapon_cost: 2
      default_weapon: core:knife     # resolved through the weapon registry
      init_bit_flags: []             # names minus the BIT_ prefix: FLYING, ...
      init_ani_type: 0
      init_max_magicpoints: 0
      special_names: ["NONE", "CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM", "NONE"]
      alternate_names: ["NONE", "NONE", "NONE", "NONE", "NONE", "NONE"]
      leaves_bloodspot: true
      magic_damage_modifier: 1
      is_stationary: false
      has_returning_weapon: true
      is_undead: false
      promotes_to: ~                 # nullable family id string
      promotion_level_req: 0
      death_message: "SOLDIER SLAIN"
      ai_line_of_sight: 7
      description: "Your basic grunt..."
      names: ["Lothar", "Arthur"]    # random-name pool
      playable: true
      playable_order: 0
      # + sprite, animation, and the presentation block (below)
  weapon:
    - id: core:knife
      wire_id: 0
      name: "KNIFE"
      fire_sound: 10
      skip_sit_notify: false
      is_auto_attackable: false
      init_bit_flags: []
      init_lifetime: 0
      init_ani_type: 0
      vz: 0.35
      gravity: 0.05
      sizez: 0
      can_drop_floors: true
  effect:                            # `fx:` is accepted as an alias
    - id: core:expand
      name: "EXPAND"
      loops_animation: false
      creates_hit_effect: false
      init_bit_flags: []
  treasure:
    - id: core:stain
      name: "STAIN"
      init_ignore: true
      init_frame: -1
  generator:
    - id: core:tent
      name: "TENT"
      default_weapon: core:skeleton  # what it spawns
      has_lifetime: true
      spawn_ani_type: 0
      clear_owner: false
      editor_label: "TENT"           # level-editor palette caption
```

### Art, animation, and presentation (all five orders)

```yaml
      sprite: "packs/org.example.mypack/sprites/warlock.png"
      animation: warlock_walk
      glyph: "w"                # exactly one UTF-8 codepoint, quoted
      glyph_ascii: "w"          # exactly one byte
      glyph_color: magenta      # default black red green yellow blue
                                # magenta cyan white team
      glyph_bold: true
      glyph_transparent: false  # the curses "draw nothing here" flag
      radar_color: 40           # palette index, or `none` / `team`
      radar_jitter: 2           # adds rand(jitter); 0 = NO rng call
```

`sprite:` is nullable (`~` clears it) and its value is passed to the sprite
loader untouched: the loader tries `pix/<value>` first, then `<value>` as a
path in the mounted virtual tree. Core families name a bare file in `pix/`;
a pack shipping its own art gives the full virtual path starting at its mount
point (`packs/<pack_id>/sprites/<name>.png`). A living family with no sprite
has no graphics at all and cannot be drawn.

Frame metadata comes from a per-PNG Aseprite "Hash" JSON sidecar named after
the PNG; a PNG with no sidecar is a single-frame sprite. The sidecar is
resolved with **whichever prefix opened the PNG** and no other, so a pack's
`.json` always sits beside its own `.png`: a file under `pix/` cannot shadow
a pack's sidecar (`pix/` is user-mountable through the sprite-sheet setting)
and a pack's sidecar can never be applied to core art. `meta.size` must equal
`frame w` × `frame h × frame count`, which is the cross-check that catches a
sidecar drifting away from its art.

`animation:` names either one of the seven built-in living tables —
`standard`, `mage`, `skeleton`, `giant_skeleton`, `slime`, `small_slime`,
`static` — or one of this pack's `anims:` sets. **Built-in names are reserved
and win**, so the legacy tables keep working unchanged; naming a built-in on
a living also clears any pack table back to `nil`. The other four orders can
only name a pack set (the built-in tables are living-shaped).

Presentation replaces the family switches the UI layers used to carry (the
curses glyph table, the radar blip colour, the editor's generator captions).
It lives on the descriptors, so — per the component rules — its types are
declared in `core`: `og::FamilyGlyph`, `og::RadarBlip`, `og::GlyphColor` in
`include/openglad/core/family_presentation.h`. `og::GlyphColor` mirrors
`og::curses::Color` entry-for-entry and adds `Team`, meaning "resolve to the
entity's team colour". `radar_color` has two sentinels: `none`
(`og::kRadarColorNone`, draw no blip) and `team` (`og::kRadarColorTeam`).
`radar_jitter: 0` means *make no RNG call*, matching the legacy draw path
where only flickering families rolled — the call count is observable.

Presentation is cosmetic, so a malformed value (a multi-character `glyph`, an
unknown `glyph_color`, a negative `radar_jitter`) warns and keeps the current
setting rather than sinking the pack.

### `anims:` — pack-shipped frame tables

```yaml
anims:
  warlock_walk:
    rows: 16          # optional: pad to this many rows by cycling the
                      # declared ones (so one row + rows: 16 reproduces the
                      # legacy uniform tables). Must be >= rows given.
    frames:
      - [0, 1, 2, 3]  # row index = ani_type * 8 + facing
      - [4, 5, 6, 7]
      - ~             # an explicit null row (as anislime rows 24..31 are)
```

Frames are sprite indices; there is no `-1` in the YAML because the row's end
*is* the end — the loader appends the sentinel. Caps, each a warn-and-drop of
the whole **set** (families naming a dropped set keep whatever animation they
had): at most 256 rows, 1..255 frames per row, frame indices `0..127`. A
duplicate set name keeps the first.

Materialized rows and row-pointer arrays live in the process-lifetime
classpack store (`src/resources/packs.cpp`), so a descriptor's
`anim_table` pointer is valid for the life of the process. The descriptor
carries `anim_table` (nullptr = use the built-in table selected by
`animation_type`) and `anim_row_count`, which is **authoritative**:
consumers read both off the descriptor and never re-derive the count.
`anim_row_count` bounds `walker::ani_count`, which is what keeps the four
`animate()` sites safe against a snapshot- or save-driven `ani_type`/`curdir`
walking off the end of a table. `anim_table_count()` only recognises the
built-in tables and answers 0 for a pack table, and `ani_count == 0` means
"legacy direct indexing, bounds checks OFF" — so the loader must never
recompute a count a descriptor already supplied.

### What a pack family does NOT get

Class-pack families claim registry ids at or above 21, which the historic
`PIX(order, family)` addressing cannot represent, so the loader keeps them in
a second block appended after the core one. That block is rebuilt from
descriptors on every `reload_graphics()`, which is what lets an unmounted
pack's family disappear cleanly.

The consequence: **for the four non-living orders, a pack family gets only
what its descriptor holds — the sprite and, when it ships one, its animation
table.** Hitpoints, act type, stepsize, damage and line of sight for
weapons/effects/treasures/generators come from the loader's `EntityDef`
table, which pins core ids only, so a mod weapon starts at zero for all of
them and has to set what it needs on the entity after spawning it. Living
families have no such gap: their loader stats come from `derived_bonuses`.

Scripts register behavior:

```lua
og.register_hooks("living", "mypack:warlock", {
  do_special = function(self) ... end,
  on_death   = function(self) ... end,
})
```

Hook sets per order mirror the existing descriptor callbacks: living (14),
weapon (`on_death`, `on_animate`, `on_hit_target`), effect (`on_act`,
`on_death`), treasure (`on_eat`), generator (`customize_spawn`, which has no
C++ counterpart — generators are pure script). Exact signatures are in
[the API reference](modding/api-reference.md#hook-signatures).

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
- **String ids resolve by descriptor `name:`, not by namespace.**
  `og::families::resolve_family_string_id` splits at the first `:`, discards
  the prefix, lowercases the rest and maps spaces to underscores, then scans
  the order's registry for a descriptor whose `name` normalizes to the same
  thing. `mypack:soldier` and `core:soldier` are therefore the same family,
  and the namespace is documentation rather than a scope. A mod family must
  pick a `name:` no other mounted family uses — and must not omit it, since
  a free living slot defaults to `BEAST`, which three core families already
  answer to.
- Where core registry names genuinely collide (golem / giant_skeleton /
  tower1 are all `BEAST`, the slime trio all `SLIME`), the positional escape
  `core:#<id>` addresses the exact byte. `family_string_id()` emits that form
  automatically for every member of a collision group, which is why
  `packs/core/classpack.yaml` carries ids like `core:#19`.
- Saves: v10 adds an optional pack-provenance chunk (list of
  `(pack_id, version, [family string ids in byte order])`) so a save with
  modded characters can remap or reject cleanly if packs changed. Legacy v9
  saves load unchanged (core ids are pinned).
- Missing pack at load: entity families with no registered byte fall back to
  a visible "unknown" descriptor (soldier body, warning glyph, name from the
  provenance chunk) rather than crashing — same spirit as the ani_count
  invariant.

## 6. Dispatch

Sim call sites do not invoke descriptor function pointers directly. They go
through the `og::script::hooks::*` helpers
(`include/openglad/gameplay/script/family_hooks.h`), each of which:

1. checks a per-`(order, family)` bitmask of registered hooks — a miss costs
   one bit test and creates no VM, so a script-less sim is byte-identical;
2. resolves the active `WorldScripts` (the current world's, or a shared UI
   instance for picker-side hooks like `level_up` that run outside any world,
   rebuilt whenever the pack set changes);
3. looks the function up in the VM registry, pushes typed arguments, and
   pcalls it with the budget armed on the outermost entry;
4. converts the result and applies R9 on error — and, during the transition,
   falls through to the descriptor's C++ callback when the script hook is
   absent or errored (§9a).

Walker handles carry an entity id plus a raw pointer and a dispatch
generation. Resolution prefers the world's id index; the raw pointer is only
honoured when the handle's generation matches the current dispatch, which is
what lets `on_death` still read a `self` that has already left the index
while making a stashed handle a clean script error (`stale or dead entity
handle`) instead of UB. Guy handles are generation-only, so they are strictly
dispatch-scoped.

**Diagnostics are bounded.** Both stores live on the `ScriptHost`:

- Errors always trace (`script_error`, every occurrence). The stored vector
  holds at most `kMaxStoredScriptErrors` (64) *distinct* `(where, message)`
  pairs; a repeat bumps that record's `count`, and overflow only increments
  `dropped_error_count()`. Without this, a hook erroring once per tick per
  instance — an effect's `on_act`, a flag's touch handler — would append a
  fresh traceback for the life of the world.
- `og.log`/`print` always traces (`script`). The transcript keeps the most
  recent `kMaxStoredScriptLogLines` (512) lines, evicting oldest-first so
  `log().back()` still means "most recent", with the loss counted in
  `dropped_log_line_count()`.

**Duplicate hook registration warns; last registration wins.** Since scripts
replay in pack-id then filename order, that ordering is deterministic — and
it is the supported way to override a core family (a mod pack registering
`core:soldier` replaces the core soldier's behavior). The warning names the
order, family, hook and source location, and is recorded as a script error,
so an accidental in-pack collision is diagnosable rather than silent.

## 7. Level and entity scripting

Level scripts are ordinary pack scripts — a campaign embeds a pack
(`packs/<pack_id>/scripts/*.lua` inside its zip) that mounts and unmounts
with the campaign — and they register against level ids rather than families:

```lua
og.register_level_hooks(level_id, {   -- level_id -1 = every level
  on_load         = function(level) ... end,
  on_tick         = function(level, tick) ... end,
  on_entity_death = function(ent) ... end,
  on_entity_spawn = function(ent) ... end,
})
og.set_entity_hooks(ent, { on_death = function(ent) ... end })
```

- `on_load` fires on the **first tick of the level on this peer** — fresh
  start and mid-join alike. It must derive everything from the world and be
  idempotent; there is no "start of fight" moment it can assume.
- `on_tick` fires every tick, before entity acts. Win/lose logic lives here.
- `on_entity_death` fires for living **and generator** deaths. Generators
  dispatch it after their death FX, so a level script sees a tent, tower or
  pillar falling as an event rather than something to poll for.
- `on_entity_spawn` fires only for sim-authored living/generator spawns
  through `add_ob`; snapshot and replay insertion paths stay silent.
- An exact-level registration shadows a wildcard one **per hook kind**.
- `og.set_entity_hooks` attaches a per-entity `on_death` at runtime (usually
  from `on_load` or `on_entity_spawn`, after selecting the entity via
  `og.oblist()`, position or family). The registration is consumed when it
  fires, so an entity id never fires twice, and it requires a tracked entity.

Every level dispatcher early-outs when no pack registered that hook kind, so
script-less sims pay nothing and stay byte-identical.

The worked showcase is `tools/concept_mapgen/showcase_pack.cpp` — the
Ninefold Court (scen 605), whose `court.lua` uses `on_load` to stamp ward
invulnerability and hang a per-entity death hook, `on_entity_death` to run
the ward phase machine off generator deaths, `on_tick` for the judgment
cadence, and a generator `customize_spawn` to promote every third spawn.

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
  scenarios cover a Lua-only mod family and level scripting hooks.
- **Integration**: picker lists pack families; save/load with mod
  characters; MP transfer e2e (host with pack, vanilla joiner); curses/text
  clients render mod families via descriptor glyphs.
- **Canary**: mutation pins re-anchored where sim files shifted; teeth run
  locally (`genuine toothless` must stay 0) — Lua family behavior gets its
  own mutation pins (mutate a .lua constant in the core pack, expect flips).

## 9a. Retiring the C++ family implementations

The conversion deliberately kept both implementations alive: `hooks::*`
tries the Lua hook first and falls back to the descriptor's C++ callback.
That fallback is what made a 34-file conversion safe to land
incrementally, but it is not the end state — "no assumptions about
families left in the engine" means the C++ implementations go away.

Retirement happens in two stages, each independently verifiable:

**Stage A — behavior.** Null every behavior callback slot; keep the
`describe_family_*` data functions. Parity passing after this is the
decisive proof that Lua carries 100% of behavior, because there is no
longer anything to fall back to.

Two consequences to handle in the same change:

- **A hook error stops being harmless.** With a C++ callback present, an
  erroring hook silently degrades to the old behavior; without one it
  degrades to *nothing happening*. Script errors must therefore become
  loud: always traced, and under `TESTING` latched so a test cannot pass
  while a hook is quietly failing.
- **Mutation-canary pins anchored in `family_*.cpp` text** must move to
  the `.lua` equivalents, or the canary silently loses its teeth
  (`genuine toothless` must stay 0).

**Stage B — data.** Delete the family `.cpp` files entirely; the five
registries populate from `packs/core/classpack.yaml` alone. After this the
engine contains no notion of "soldier" or "mage" at all — every family,
core and mod, arrives through the same door.

Stage B makes the core pack a hard runtime requirement. That is consistent
with how the engine already treats its other required assets: `io_init`
throws when the user path or the default campaign is missing. A missing or
malformed core pack must fail the same way — one clear diagnostic, not a
crash and not a silent half-populated registry.

**Coverage caveat.** Parity only proves the paths its corpus exercises.
Conversion work surfaced real gaps (CTF has no scenario at all; several
`level_up`, on-death and heal paths are never reached). Those paths need
differential tests — build a world from a fixed seed, run with pack
scripts registered, then rebuild the identical world with
`clear_pack_scripts()` and run again, asserting identical state — or new
parity scenarios, *before* the corresponding C++ callback is deleted.

**Pack Lua is now inside the coverage gate.** `scripts/coverage/` measures
every pack script the engine can load, line-by-line and function-by-function,
and merges the result with gcovr's `src/` numbers;
`.github/workflows/coverage.yml` enforces one bar — 95 % line, 100 % function
— across the union. See `scripts/coverage/README.md` for how each number is
produced and why arming the recorder is a runtime switch rather than a compile
flag. Gaps the gate surfaced are covered by
`tests/unit/test_pack_lua_paths.cpp` (the cloud's overlap test, the cleric's
whole kit, the archmage's response chain, the slime split), which is the place
to add the next one.

Three rules the gate depends on, because each one was a way to score coverage
without having any:

* **Every prototype is a function.** The denominator is the compiled prototype
  tree, not the set of registered hooks, so an uncalled local helper or
  anonymous callback costs exactly what a hook costs — and a script no test
  loads is a file of misses rather than an absence.
* **A function is covered when a line of its body ran**, never at the point
  the engine decided to dispatch it. An empty hook and one that dies on its
  first statement both read as misses, which is what they are.
* **One statement per line** (`scripts/check_lua_statement_lines.py`, a build
  dependency of `og_gameplay`). Line coverage counts lines, so `if low then
  flee() end` on one line is a branch the metric cannot see. Writing it out
  costs nothing and makes the branch measurable.

## 10. Rollout

| # | Stage | State |
|---|---|---|
| 1 | `og_lua` + ScriptHost + sandbox/budget unit tests | done |
| 2 | Pack reader + registries with string ids + core pack YAML (stats only) | done |
| 3 | `og.*` binding layer + soldier converted (cookbook proven) | done |
| 4 | Remaining families in waves, parity per wave | done — all 21 livings plus weapon/effect/treasure behaviors are Lua |
| 5 | Generators scripted; UI sweep (glyph / radar / editor label from descriptors) | descriptor fields + `classpack.yaml` schema landed; consumer sweep in progress |
| 6 | Level/entity scripting; campaign-embedded packs | done (showcase: the Ninefold Court, scen 605) |
| 7 | MP transfer, protocol v10 | done |
| 8 | Skills/docs; concept-playground showcase; media; PR | in progress |
| 9 | Retire the C++ implementations (§9a, stages A and B) | not started |
