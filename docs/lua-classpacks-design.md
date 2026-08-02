# Lua Class Packs: Hotpluggable Families, Scripted Objects, and Level Scripting

OpenGlad's built-in family data and family-specific behavior ship through
`packs/core/`, using the same YAML and Lua path as third-party packs. The
engine provides registries, generic entity behavior, and script dispatch; it
does not carry a second native implementation of the core families.

This document is the **architecture and determinism** reference: how packs
are built, loaded, identified and dispatched, and the rules any sim-facing
Lua must obey. `og_test_parity` is the enforcement gate.

Companion documents, each in its own lane:

| Document | Lane |
|---|---|
| [docs/modding/api-reference.md](modding/api-reference.md) | Symbol-level reference: every `og.*` function, walker/stats/guy method, constant and hook signature, plus a guided tour of the runnable example pack in `docs/modding/examples/emberwisp/`. |
| [docs/lua-style.md](lua-style.md) | Naming, headers, comments, helper modules, and compatibility style for in-tree pack Lua. |
| [.claude/skills/openglad-modding/SKILL.md](../.claude/skills/openglad-modding/SKILL.md) | The practical playbook: build, stage, test, and *prove* a mod dispatches. |
| [AGENTS.md](../AGENTS.md) | Router for non-Claude agents. |

## 1. Goals

- Character families (living/weapon/effect/treasure/generator) defined by
  **packs**: virtual directories carrying YAML descriptors, Lua behavior
  scripts, sprite PNGs, and animation tables. A pack may be a host directory
  or a subtree of a mounted campaign archive. No native code in packs, ever.
- The built-in living, weapon, effect, treasure, and generator families ship
  as the **core pack**, built from `packs/core/` at build time and mounted
  like built-in campaigns. The engine carries no compiled-in family-specific
  behavior.
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
  resources IO); `check_vendor_leaks.sh` permits Lua headers only under
  `src/gameplay/script/`.
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
sim-facing expression preserve the engine's numeric semantics.

**R1 — Integer division and modulo.** Use `og.div(a,b)` / `og.mod(a,b)` unless
the operand ranges prove plain `//` / `%` equivalent. The helpers use C
semantics (truncate toward zero; div-by-zero raises a script error), while
Lua `//` / `%` use floor semantics and differ for negative operands. Never
use `/` for integer division.

The shipped core pack uses plain `//` or `%` only where audited
engine-produced ranges make the Lua and C results identical. Those ranges
are not load-time validation: save, wire, and snapshot readers can still
admit a crafted negative level, for example, and Lua floor division then
differs from the historic C truncation. Every peer still runs the same Lua,
so this does not create a desync. New code keeps `og.div`/`og.mod` unless its
intended input domain has an equally explicit proof.

**R2 — Float arithmetic is per-op through bindings.** Every C++ float
operation maps to exactly one call: `og.fadd(a,b)`, `og.fsub`, `og.fmul`,
`og.fdiv` — each casts operands to `float`, performs the op in `float`, and
returns the widened result. Chains keep per-op float rounding this way.
  - Exception (allowed, for readability): a SINGLE `+`, `-`, or `*` is safe
    only when both operands already have the values C++ `float` would hold
    and the exact result is representable as a C++ `float`. A common
    sufficient case is integer-valued operands and result within float's
    exact integer range (up to 2^24 in magnitude). Otherwise use `og.f*`.
    Division is NEVER done in Lua (double rounding).
  - Float comparisons in Lua are safe (float→double widening is exact).

**R3 — Narrowing writes go through typed helpers.** C++ stores into
`char`/`short`/`unsigned char` wrap. Use `og.i8(x)`, `og.i16(x)`, `og.u8(x)`,
`og.i32(x)` to reproduce the wrap at exactly the sites the C++ narrowed, and
`og.trunc(x)` for `static_cast<int32>(float)` (truncation toward zero).
Field setters additionally clamp/wrap to the underlying field type, matching
the C++ member types.

The walker property layer inherits this rule for free: `self.hp = v`,
`self.team = v`, `self.busy = v`, … route through the SAME registered
setters as the method spellings (`s_set_hitpoints`, `set_team_num`,
`set_busy`), so a property write narrows exactly like the method call and
the two spellings cannot drift. Reads resolve METHOD-FIRST: a name that is
also a method (`busy`, `dead`, `lifetime`, `damage`, `ani_type`, `foe`, …)
answers the function, so those reads stay `self:busy()`; only the
non-colliding names (`hp`, `max_hp`, `magicpoints`, `max_magicpoints`,
`level`, `team`) read as values.

**R4 — RNG only via `og.rand(n)` / `og.rand0(n)`** (routes to
`current_game->world->rng_`). Preserve the ORDER and COUNT of rand calls
exactly when translating behavior. `math.random` does not exist in the
sandbox.

`og.rand0(n)` is shorthand for the guarded form, with `IRandom::next`'s real
contract: `n <= 0` answers 0 **without advancing the stream** (C++ `next(0)`
returns before the LCG step), and for `n > 0` it is `og.rand` verbatim — so
replacing `if n > 0 then r = og.rand(n) end` with `r = og.rand0(n)` can never
move the stream in either case. Keep plain `og.rand` where the bound is
provably positive: its error on `n <= 0` is a loud tripwire
([lua-style.md](lua-style.md) S5 governs the choice).

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

R6 gets mechanical support at two boundaries: `og.use` exports are served as
frozen read-only views, and a registered `specials` table is stored as a
PRIVATE copy. Those boundaries prevent caller-side table mutation from
changing dispatch. They cannot detect mutable closure upvalues, so module
authors must still keep chunk-level state immutable.

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

Chunks are compiled **text-only** (`luaL_loadbufferx(..., "t")` at every
pack-script and module compile site): a precompiled binary chunk is a load
error, exactly like a syntax error. That is the canonical Lua hardening —
`lundump` does no consistency checking, so crafted bytecode is an
arbitrary-code vector straight through the sandbox floor above — and it also
protects the coverage gate: a stripped binary chunk has function spans but no
line info, which would erase a pack file's line denominator while its
function bar stayed satisfiable (see `scripts/coverage/README.md`). Ship
`.lua` text, never `luac` output.

**R8 — Budgets.** Per host entry: instruction budget (default 5M, via a
`LUA_MASKCOUNT` hook armed on the OUTERMOST call only, so a nested dispatch
such as `g_upgrade_to_level` re-entering `level_up` does not re-arm it) and
a per-VM memory cap (default 32 MiB, counting allocator). Blowing a budget
raises a deterministic script error — a runaway loop is an error on every
peer, not a hang.

**R9 — Hook errors stop that hook deterministically.** Every peer makes the
same decision, but mutations completed before the error are not rolled back.
No native family implementation retries the operation. The caller may still
take its generic default path when the failed hook produces no result.

Validate error-prone preconditions before the first mutation whenever
possible. Every hook failure traces under `script_error`, is stored on the
host (bounded and de-duplicated; see §6), is logged at error level, and
increments the process-wide hook-failure latch used by tests.

**R10 — String hash seed is fixed** (`luai_makeseed` override) so any
incidental hash-order exposure is at least identical across builds; R5 still
applies.

## 4. Pack format

```
packs/<pack_id>/                  # virtual directory; may live in a campaign archive
├── classpack.yaml                # descriptor data + animation sets
├── families/<name>.yaml          # optional split descriptor files
├── lib/<name>.lua                # optional og.use modules
├── sprites/<name>.png            # optional pack-shipped art (indexed PNG)
└── scripts/<file>.lua            # behavior hooks
```

**Split layout.** A pack may carry its descriptor data in one
`classpack.yaml`, in per-family `families/*.yaml` files, or both. The loader
parses `classpack.yaml` first (when present), then every `families/*.yaml` in
sorted filename order, into one pack. Each family file is an ordinary
classpack document (usually `families:` with a single entry; `anims:` and
header scalars are legal anywhere, and later header values win). A later
entry pinning an already-declared wire slot overwrites only the data fields
it declares and replaces that slot's `tuning:` map whole. One unusable file
rejects the whole pack. The core pack uses a header-only `classpack.yaml`
plus `families/<order>-<NN>-<slug>.yaml`, where NN is the pinned wire id.

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
    - id: core:soldier               # required declared id; qualified ids are scoped (§5)
      wire_id: 0                     # integer 0..255, or `auto` (>= 21)
      name: "SOLDIER"                # display name and bare-id fallback (§5)
      short_name: ~                  # nullable: picker label override
      stats:                         # attribute scores; all six required
        strength: 12
        dexterity: 6
        constitution: 12
        intelligence: 8
        armor: 9
        level: 1                     # starting level
      combat:                        # field numbers; all five required
        hp: 120
        melee_damage: 20
        stepsize: 4                  # px per step
        fire_delay: 6                # busy ticks AFTER each attack; lower is
                                     # faster (Lua: self:fire_frequency())
        fire_mp_cost: 2              # MAGIC POINTS per ranged shot
      costs:                         # gold, and only gold
        hire: 250
        train:                       # per-point price on each stats axis;
          strength: 6                #   an omitted axis is 0
          dexterity: 10
          constitution: 6
          intelligence: 25
          armor: 50
          level: 200                 # vestigial (see below)
      specials:                      # up to five, in slot order
        - id: charge                 # the key a script's specials table uses
          name: "CHARGE"             # the HUD string
          mp_cost: 25
        - id: boomerang
          name: "BOOMERANG"
          mp_cost: 100
        - id: whirlwind
          name: "WHIRLWIND"
          mp_cost: 120
        - id: disarm
          name: "DISARM"
          mp_cost: 150
      default_weapon: core:knife     # resolved through the weapon registry
      init_bit_flags: []             # names minus the BIT_ prefix: FLYING, ...
      init_ani_type: 0
      init_max_magicpoints: 0        # 0 = the usual 10 + INT*3
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
      tuning:                        # optional scalar map, any order's entry;
        charge_bonus: 12             #   read back via og.tuning(self) as a
        whirlwind_range_base: 60     #   frozen table. Unquoted integers stay
        #                            #   Lua integers, decimals become floats,
        #                            #   QUOTED scalars stay strings.
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
      wire_id: 0
      name: "EXPAND"
      loops_animation: false
      creates_hit_effect: false
      init_bit_flags: []
  treasure:
    - id: core:stain
      wire_id: 0
      name: "STAIN"
      init_ignore: true
      init_frame: -1
  generator:
    - id: core:tent
      wire_id: 0
      name: "TENT"
      default_weapon: core:skeleton  # what it spawns
      has_lifetime: true
      spawn_ani_type: 0
      clear_owner: false
      editor_label: "TENT"           # level-editor palette caption
```

### The living blocks: `stats` / `combat` / `costs` / `specials`

Those four blocks are the only place the "undeclared changes nothing" rule
is tightened, and the reason is the same in each case: the honest default
would be a gameplay trap.

- **Every member of `stats:` and `combat:` is required** when the block
  appears. A missing `armor:` would install a 0-armor class, and nothing
  in the game would say so.
- **`costs.hire` is required; every `costs.train` axis is optional** (an
  omitted axis prices at 0, which is what an unpriced axis has always
  shipped). `costs.train.level` is vestigial — levels are priced by the
  exp curve and nothing reads the entry — but the core files ship 200 and
  it is honoured, so a pack restating a core family must restate it too or
  price the axis at 0. New packs should omit it.
- **`costs:` is gold and `combat:` is magic points.** `fire_mp_cost` is
  what a ranged shot spends from the caster's MP; it lives beside
  `fire_delay` and not among the prices, because the two currencies must
  never share the word "cost".
- **An unknown key INSIDE one of the blocks warns and is dropped**, while
  an unknown key at entry level stays silent. Forward compatibility is
  about a future engine adding a section; inside `combat:` the realistic
  unknown key is `step_size`, and swallowing that installs a default
  nobody wrote.
- **`combat.mp`, `combat.ranged_damage`, `combat.range` and
  `combat.defense` are refused by name.** They were columns of the old
  positional array that never had a reader. Max MP is `10 + INT*3` (or
  `init_max_magicpoints`), a ranged attack's damage belongs to the weapon
  family, reach is `ai_line_of_sight`, and armor is `stats.armor`.
- **The keys the blocks replaced are refused by name too**, in any shape,
  with a line saying where the value moved: `base_stats` → `stats:`,
  `derived_bonuses` → `combat:`, `stat_costs` → `costs.train`,
  `hiring_cost` → `costs.hire`, `weapon_cost` → `combat.fire_mp_cost`,
  and `special_costs` / `special_names` / `alternate_names` → the
  `specials:` list. Silence there would be the worst outcome the schema
  can produce — an unknown key is skipped, so an unmigrated file would
  install a family with no attributes and no diagnosis. Run
  `scripts/migrate_classpack_v2.py` on a pack that still spells them.

`specials:` is a list of up to five entries and its ORDER is the slot:
entry *i* is slot *i+1*, which the player can select from level
`(N-1)*3+1` — so slot 5 unlocks at 13. An entry may name its own `slot: N`
to leave a hole; slots must strictly increase. Absent slots are disabled
("NONE", cost 5000), and a family with no specials writes `specials: []`.
Each entry takes:

| key | |
|---|---|
| `id` | required, `[a-z0-9_]+`, unique in the family — the script-side key |
| `name` | required, the HUD string |
| `mp_cost` | required, magic points per cast |
| `alternate: {name: ...}` | optional; shown while Shift is held |
| `slot: N` | optional, 2..5, to skip a hole |

The `id` is what makes the YAML and the Lua reference each other by name:
it is the only key a `specials` table may use for a slot, and
`og.register_hooks` resolves it once, at registration (§6). An alternate is
DISPLAY only and shares the special's `mp_cost` — the behavior fork is the
handler reading `self:shifter_down()`, so an `mp_cost` under `alternate:`
is recognized and warned about rather than silently believed.

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

Automatically assigned class-pack families claim registry ids at or above
21. Families in those slots cannot use the historic `PIX(order, family)`
addressing, so the loader keeps their graphics in a second block appended
after the core one. That block is rebuilt from descriptors on every
`reload_graphics()`, which is what lets an unmounted pack's family disappear
cleanly. A pack may instead pin an existing slot explicitly to patch or
replace that family.

The consequence: **for the four non-living orders, a pack family gets only
what its descriptor holds — the sprite and, when it ships one, its animation
table.** Hitpoints, act type, stepsize, damage and line of sight for
weapons/effects/treasures/generators come from the loader's `EntityDef`
table, which pins core ids only, so a mod weapon starts at zero for all of
them and has to set what it needs on the entity after spawning it. Living
families have no such gap: their loader stats come from the `combat:`
block.

Scripts register behavior:

```lua
og.register_hooks("living", "mypack:warlock", {
  do_special = function(self) ... end,
  on_death   = function(self) ... end,
})
```

Supported hook sets are: living (14), weapon (`on_death`, `on_animate`,
`on_hit_target`), effect (`on_act`, `on_death`), treasure (`on_eat`), and
generator (`customize_spawn`). Exact signatures are in
[the API reference](modding/api-reference.md#hook-signatures).

## 5. Identity: string ids, wire bytes, palette

- Runtime keeps int8 family bytes everywhere (walker, snapshots, saves,
  level entities) — zero hot-path cost, wire format shape unchanged.
- Registries resolve string ids to bytes per order. **Core pack ids are
  pinned** to the legacy bytes (soldier=0 … tower1=20; likewise for the legacy
  weapon/effect/treasure/generator numeric ids), so existing levels, saves,
  goldens, and the wire stay byte-compatible.
- Mod families get bytes assigned at mount: campaign-embedded packs declare
  `wire_id: auto` and receive deterministic ids (assignment order = pack id
  lexicographic, then YAML order, first automatic byte from 21 up). Levels,
  saves, and snapshots continue to store family bytes; the mounted pack set
  determines what those bytes mean.
- **A positional id is exact.** `#<byte>` or
  `<any-namespace>:#<byte>` addresses that populated slot directly; the
  namespace is ignored.
- **A declared `id:` is authoritative.** For any non-positional string,
  resolution first compares the complete normalized input against every
  populated descriptor's declared id. A namespace, when present, is
  therefore a real scope:
  `alpha:warlock` and `beta:warlock` can name distinct families even when
  both descriptors use the display `name: "WARLOCK"`.
- **Display `name:` is a compatibility fallback.** If no declared id matches,
  the resolver takes the part after the first `:` (or the whole bare string)
  and returns the lowest populated slot whose display name matches. This is
  why `soldier` resolves, and why `mypack:soldier` still reaches the core
  soldier when no descriptor declared `mypack:soldier`. Do not use that
  fallback as a scope check; register hooks and references with declared ids.
- Matching is case-insensitive and treats spaces like underscores. A new
  family should still declare a meaningful `name:` for display and bare-name
  compatibility. Reusing a display name is legal, but its bare form is
  ambiguous and resolves to the lowest byte.
- The core pack explicitly declares positional ids for historic display-name
  collisions such as the `BEAST` and `SLIME` groups.
- There is no string-id provenance or remapping in the save, level, or
  snapshot formats. A byte that belongs to a mod must be loaded with the
  compatible pack set. Core ids remain safe across legacy data because the
  core pack pins their historic bytes.

## 6. Dispatch

Sim call sites do not invoke descriptor function pointers directly. They go
through the `og::script::hooks::*` helpers
(`include/openglad/gameplay/script/family_hooks.h`), each of which:

1. resolves the active `WorldScripts` (the current world's, or a shared UI
   instance for picker-side hooks like `level_up` that run outside any world,
   lazily built and rebuilt whenever the pack set changes);
2. checks a per-`(order, family)` bitmask of registered hooks;
3. looks the function up in the VM registry, pushes typed arguments, and
   pcalls it with the budget armed on the outermost entry;
4. converts the result and applies R9 on error. No native family behavior is
   retried; a helper that reports no result leaves only the call site's
   generic default path.

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
replay in pack-id then filename order, that ordering is deterministic. A mod
pack that sorts after `core` can intentionally override a core hook by
registering the same declared id, such as `core:soldier`. The warning names
the order, family, hook and source location, and is recorded as a script
error, so an accidental in-pack collision is diagnosable rather than silent.

**Specials dispatch (the table form of `do_special`).** A living family may
register `specials = { charge = fn, …, default = fn }` instead of a
`do_special` function (one or the other per call — both is a load error).
`self:current_special()` selects the entry; a missing slot falls to
`default`; a table with neither consumes the dispatch as a **successful
no-op** — result `true`, no Lua call, no budget armed, no stream touched.
Every value is a function, an empty table is a load error, and the table
occupies the `do_special` hook slot, so collision reporting and last-wins
behave like any named hook.

A key is the string `default` or one of the `id`s the family's `specials:`
list declares (§4). The id is resolved to its slot ONCE, at registration,
and stored in an int-keyed private copy, so the cast path never sees it —
what it buys is that the data and the behavior reference each other by
name. Reordering the YAML list cannot silently re-bind a handler, and a key
naming no declared id is a load error listing the ids that exist.

A slot number for a key is refused outright — the same load error, naming
the key and the ids that would have worked. Slot numbers were the original
spelling and they bound a handler to a position in the list; nothing in the
tree keys that way any more.

Around either form the engine contract is unchanged
(`walker_specials.cpp`): the caller gates on `magicpoints() >=
special_cost(current_special)` BEFORE dispatch and deducts that cost only
after a `true` answer — so `false` means "did not fire, charge nothing",
and the no-op fall-through, answering `true`, is charged like the ladders'
unmatched case always was. That last case is the one real trap in the
mechanism, so every VM build now sweeps the living families and warns for
each castable slot the final table handles neither directly nor through
`default`, naming the special and the MP a cast would waste.

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

- The host announces a manifest for every mounted non-core pack needed by the
  session. Each manifest carries the pack id and version plus every relative
  file path, size, and FNV-1a content hash.
- A client compares the manifest with an already mounted pack or a cache
  directory at
  `<user_path>/packs_cache/<pack_id>@<manifest-hash>/`. It requests a
  missing or mismatched pack, receives sequential 32 KiB file chunks over
  the reliable transport, verifies every file, persists the cache entry, and
  mounts it for the session.
- Limits are 16 packs and 512 files per manifest, 16 MiB per pack, and
  64 MiB for the session. Unsafe ids or paths, malformed manifests,
  out-of-order chunks, hash mismatches, and cap violations fail the transfer
  with a reason.
- Protocol v10 defines the manifest, request, file-chunk, and completion
  messages. The built-in `core` pack is never transferred.

## 9. Verification and coverage

The core pack is runtime data, not an optional behavior overlay. `io_init`
installs it and refuses to start if any required core family slot is
unpopulated. All core descriptor data comes from the split YAML documents,
and all family-specific behavior comes from pack Lua. Hook failures are
therefore loud, and tests that dispatch core behavior assert a clean
hook-failure latch rather than expecting another implementation to retry.

The verification layers cover different promises:

- **Unit tests** exercise the sandbox, budgets, arithmetic helpers,
  descriptor parsing and installation, id resolution, hook dispatch, level
  hooks, and pack transfer.
- **Parity** requires byte-identical recorder output for the core scenarios,
  both with the recorder off and armed. It proves only paths the scenario
  corpus reaches, so uncovered behavior needs a focused unit or integration
  test.
- **Mutation pins** for family behavior and data target the shipped `.lua`
  and YAML sources (engine-side pins stay in the C++ sim files). The pin-map
  check must report no toothless mutation; changing a pinned behavior or
  datum must flip its expected scenarios.
- **Integration tests** cover mounted campaign packs, picker visibility,
  save/load behavior, multiplayer transfer, and descriptor-driven
  presentation.

**Pack Lua is inside the coverage gate.** `scripts/coverage/` measures
every pack script the engine can load, line-by-line and function-by-function,
and merges the result with gcovr's `src/` numbers;
`.github/workflows/coverage.yml` enforces one bar — 95 % line, 100 % function
— on the C++ half alone, on the Lua half alone, and on their union. Separate
floors prevent one language's surplus from hiding the other's shortfall. See
`scripts/coverage/README.md` for how each number is produced and why arming
the recorder is a runtime switch rather than a compile flag. Focused gaps are
covered by `tests/unit/test_pack_lua_paths.cpp` (the cloud's overlap test, the
cleric's whole kit, the archmage's response chain, the slime split), which is
the place to add the next one.

Three rules define the Lua metric:

* **Every prototype is a function.** The denominator is the compiled prototype
  tree, not the set of registered hooks, so an uncalled local helper or
  anonymous callback costs exactly what a hook costs — and a script no test
  loads is a file of misses rather than an absence.
* **A function is covered when a line of its body ran**, never at the point
  the engine decided to dispatch it. A hook that is registered — even
  dispatched — but never *entered* reads as a miss. What the metric does NOT
  distinguish, and no line-derived metric can: once a hook's body is entered,
  an empty body counts (its `end` carries `OP_RETURN` and fires a line event)
  and so does one that raises on its first statement — a dispatched no-op
  stub is indistinguishable from an implementation. See "What the numbers do
  NOT claim" in `scripts/coverage/README.md`.
* **One statement per line** (`scripts/check_lua_statement_lines.py`, a build
  dependency of `og_gameplay`). Line coverage counts lines, so `if low then
  flee() end` on one line is a branch the metric cannot see. Writing it out
  costs nothing and makes the branch measurable.

Coverage identity is the content hash. Byte-identical copies collapse into
one entry, but a **byte-variant** copy of a shipped script (a CRLF re-encode,
a whitespace edit, an abandoned fork of a pack file) is a second denominator
entry at 0 %. The report names it in its never-loaded list, and its prototypes
remain misses until a test loads those exact bytes or the variant is deleted.
