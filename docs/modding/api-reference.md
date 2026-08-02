# OpenGlad Scripting API Reference

The class-pack Lua API, symbol by symbol. Read
[`docs/lua-classpacks-design.md`](../lua-classpacks-design.md) first — the
Determinism Cookbook (R1–R10) governs everything here, and §4 is the
`classpack.yaml` schema. For *how to build, run and prove* a mod, see
[`.claude/skills/openglad-modding/SKILL.md`](../../.claude/skills/openglad-modding/SKILL.md).

Everything below is generated from the registration tables in
`src/gameplay/script/bindings_entity.cpp` (`kWalkerMethods`, `kGuyMethods`,
`kOgWorldFuncs`, `kConstants`) and the sandbox/hook entry points in
`src/gameplay/script/script_host.cpp` and
`src/gameplay/script/world_scripts.cpp`. Those files are the authority; if
this file disagrees with them, they win and this file is a bug.

## Sandbox

Available: `string` (minus `dump`), all of `table`, an integer-safe `math`
subset (`floor ceil abs min max tointeger type maxinteger mininteger huge pi
ult`), `assert`, `error`, `ipairs`, `select`, `tonumber`, `type`,
`pcall`/`xpcall`, `rawget`/`rawset`/`rawequal`/`rawlen`, `setmetatable`/
`getmetatable`, `_VERSION`, `_G` (the sandbox root), an address-free
`tostring`, and `print` (an alias for `og.log`).

Absent on purpose: `pairs`/`next` (hash order is seeded per run), `io`, `os`,
`require`/`package`, `load`/`loadstring`/`loadfile`/`dofile`,
`collectgarbage`, `coroutine`, `debug`, `utf8`, `string.dump`, `math.random`,
and every transcendental. Budgets: 5M VM instructions per host entry, 32 MiB
per VM (`ScriptLimits` in `include/openglad/gameplay/script/script_host.h`).

Scripts in the same pack share one environment, so a pack's globals are
visible to its own other scripts; different packs are isolated and all
environments read through to a shared read-only root. Use `local` for
everything unless you are deliberately sharing a helper inside one pack — and
never store mutable sim state in a global or upvalue (cookbook R6).

## Deterministic arithmetic (`og.*`)

| Function | Semantics |
|---|---|
| `og.div(a,b)` / `og.mod(a,b)` | C integer division/remainder (truncate toward zero, sign of dividend). Errors on `b == 0` and on overflow. Use for EVERY integer `/` and `%`. |
| `og.fadd/fsub/fmul/fdiv(a,b)` | One operation performed in C `float`, returned as the exact widened double. One call per C++ float operator; never chain in Lua. |
| `og.i8/i16/i32/u8(x)` | Modular narrowing of an integer, matching the C++ cast. |
| `og.trunc(x)` | `(int64)trunc(double)` — C cast-float-to-int semantics. Errors on NaN / out of range. |
| `og.max(a,b)` / `og.min(a,b)` | `std::max` / `std::min` exactly: `og.max` answers `b` only when `a < b`, `og.min` answers `b` only when `b < a`, every tie answers `a`. Exact mixed integer/float ordering (never a lossy int64→double round-trip); the winning argument comes back unchanged, so integer subtype survives. Arguments must be numbers (no string coercion); NaN is an error. |
| `og.clamp(v,lo,hi)` | `std::clamp`: `lo` when `v < lo`, else `hi` when `hi < v`, else `v` itself. `hi < lo` (std's UB precondition) is a script error. Same ordering/subtype/NaN rules as `og.max`. |
| `og.sign(x)` | `-1`, `0` or `1` as an integer, for any number (`og.sign(-0.0)` is 0; NaN is an error) — the guarded `v /= abs(v)` idiom as a total function. |
| `og.rand(n)` | Sim RNG, uniform over `[0, n)`. The only randomness source. Errors when `n <= 0` (C++ `next(0)` silently returns 0 — guard the call, or use `og.rand0`). |
| `og.rand0(n)` | `og.rand` with `IRandom`'s real `n <= 0` contract: answers 0 **without advancing the stream** (C++ `next(0)` returns before the LCG step), which is what the hand-written `if n > 0 then r = og.rand(n) end` guard trios encode. Negative `n` behaves as 0 too. For `n > 0` it is `og.rand` verbatim, so swapping a guarded `og.rand` for `og.rand0` cannot move the stream. Requires an active world on every path. |
| `og.cosmetic_rand(n)` | Draws from the parity harness's cosmetic stream when one is installed, else the sim RNG — the C++ `cosmetic_rng_override()` pattern. Use ONLY where the C++ drew through that selector (path-check cadence, elf spread). Also errors when `n <= 0`. |
| `og.log(...)` / `print(...)` | Diagnostics. Traces under category `script`, and appends to the host's bounded transcript. |

## Hooks

```lua
og.register_hooks(order, family_id_string, { hook_name = function ... })
```

`order` is `"living" | "weapon" | "treasure" | "generator" | "fx"` (`"effect"`
is an accepted alias for `"fx"`).

**The namespace is a real scope.** `og::families::resolve_family_string_id`
tries three forms, in this order:

1. **Positional escape** `"<pack>:#<byte>"` (or bare `"#<byte>"`) — the family
   at that exact wire byte, valid only if something occupies it. The
   namespace is ignored here.
2. **Fully-qualified id** `"<pack>:<name>"` — an exact match against the `id:`
   a mounted pack declared for the family. This is what makes packs
   pluggable: two packs may each ship a family named `WARLOCK` and stay
   addressable as `alpha:warlock` and `beta:warlock`.
3. **Bare-name fallback** — the part after the first `:` (or the whole string)
   matched against the descriptor's `name:`, namespace ignored. The lowest
   matching byte wins.

Matching is case-insensitive and treats a space and an underscore as the same
character, on both sides. Consequences:

- **Address families by their fully-qualified id.** `og.family_id("living",
  "mypack:warlock")` means *your* warlock even if three other mounted packs
  ship one.
- Form 3 is back-compat, not a scope check: a bare `"soldier"` resolves, and
  so does a namespace nobody declared — `"mypack:soldier"` still finds core's
  SOLDIER when `mypack` ships no `soldier`. Don't lean on that; a later pack
  version that *does* add a `soldier` silently changes what the string means.
- Give every mod family a `name:` no other mounted family uses if you want the
  bare form to stay unambiguous. A living entry that omits `name:` inherits
  the registry default `BEAST` and its bare form resolves to whichever `BEAST`
  sits at the lowest byte.
- When registry names genuinely collide (golem / giant_skeleton / tower1 are
  all `BEAST`; the slime trio is all `SLIME`), form 1 addresses the exact
  byte — e.g. `core:#19` for giant_skeleton, which is why the core pack's
  `families/*.yaml` files carry ids like `core:#19`.

An unknown order, an unresolvable family, a non-function hook value, or a
table with no recognised hook name is a **load error**: the whole chunk is
rejected. A hook that errors at *runtime* is treated as absent for that
dispatch — identically on every peer — and the error is recorded (see
[Script errors](#script-errors-and-logging)).

`og.family_id(order, "core:name")` performs the same resolution and returns
the wire byte, or `nil`. Resolve family bytes once at chunk load and keep them
in a `local` — they are constants, which is the one kind of upvalue R6 allows.
When the named family is one your own pack declares, wrap the lookup:
`local FX_X = assert(og.family_id("fx", "mypack:x"))`. `assert` narrows the
`integer?` away for the type checker, and a typo'd id fails loudly at pack
load instead of surfacing as a nil three hooks later. Keep the bare, nilable
form only when probing for a family another pack may or may not ship.

### Hook signatures

Returns are coerced to boolean where the caller uses them. A hook listed
without a return value may still return anything; it is ignored.

**living** (registered against `FamilyDescriptor`):

| Hook | Signature | Notes |
|---|---|---|
| `do_special` | `(self) → bool` | `false` = the special did not fire. Usually registered as a [`specials` table](#the-specials-table) instead. |
| `check_special_ai` | `(self) → bool` | AI's "should I special now?" |
| `hit_response` | `(self, foe)` | `self` is the stats OWNER (`statistics::controller()`), not a stats handle. |
| `set_difficulty` | `(self, level)` | |
| `level_up` | `(guy, level_diff)` | First argument is a **guy handle** (`g_*` methods only), not a walker. Runs picker-side with no world. |
| `on_death` | `(self) → bool` | Returning a value at all suppresses the default bloodspot path. |
| `on_act_living` | `(self)` | |
| `on_shoved` | `(self)` | |
| `on_fire_weapon` | `(self, weapon) → bool` | `false` blocks the shot. |
| `handle_teleport` | `(self) → bool` | |
| `on_create` | `(self)` | |
| `customize_weapon` | `(self, weapon)` | |
| `on_ani_complete` | `(self) → bool` | |
| `on_melee_hit` | `(self, target)` | |

**weapon**: `on_death(self) → bool`, `on_animate(self) → bool` (`false` = die),
`on_hit_target(weapon, target, owner)`.

**fx** / **effect**: `on_act(self) → bool` (`true` = handled this tick),
`on_death(self) → bool`.

**treasure**: `on_eat(self, eater) → bool`.

**generator**: `customize_spawn(generator, spawn)` — runs after the
descriptor-driven spawn setup (level roll, lifetime, ani). Generators carry no
C++ callbacks at all, so this hook is the only generator behavior there is.

### The `specials` table

A living family with more than a trivial special registers the table form of
`do_special` (register one or the other per call — both is a load error):

```lua
og.register_hooks("living", "core:orc", {
  specials = {
    howl = yell,            -- the id living-14-orc.yaml declares for slot 1
    default = eat_corpse,   -- every other slot
  },
  ...
})
```

Dispatch implements the slot semantics directly:
`self:current_special()` selects the entry, a missing slot falls to
`default`, and a table with neither consumes the dispatch as a **successful
no-op** — result `true`, with no Lua call at all (no budget armed, no RNG
touched). Each entry has the plain `do_special` signature: `(self) → bool`,
`false` = the special did not fire.

**Keying by id.** A key may be the special's declared `id` from the family's
`specials:` list instead of its slot integer. `og.register_hooks` resolves
the id to the slot once, at registration, and stores the same int-keyed
table it always did — the cast path is unchanged. What changes is that the
two files now name each other: reordering the YAML list cannot silently
re-bind a handler, and a misspelling on either side is a load error naming
the ids the family does declare. The shipped core scripts key by id.

Which slot an id is, and when a player can reach it, is the list order:
entry *i* of `specials:` is slot *i+1*, selectable from level `(N-1)*3+1`.

Rules:

- Living orders only; keys are integers `1..255` (`current_special` is a
  char-wide slot index), a declared special id, or the string `"default"`;
  every value must be a function; an empty table is a load error.
- An id and an integer that name the SAME slot in one table is a load error
  — the table says two things about one special and nothing can choose.
- An id key against a family whose descriptor declares none (a pack still
  on the pre-v2 schema) is a load error that says so.
- The engine stores a **private copy** at registration — mutating the
  caller's table afterwards cannot change dispatch (R6 at the registration
  boundary).
- The table occupies the `do_special` hook slot, so duplicate-registration
  reporting and last-wins override behave like any named hook, including
  across packs.

**The cost contract around either form** (`walker_specials.cpp`): the engine
gates the dispatch on `magicpoints >= special_cost(current_special)` (the
slot's `mp_cost` in the family's `specials:` list) *before* calling the
hook, and deducts that cost only after the hook answers `true`. So `false`
means "did not fire, charge nothing", and specials that price themselves (a
pool vent, a scaling drain) do their own extra deduction inside the entry.

A castable slot with neither a handler nor a `default` still charges — that
is the defined unmatched-slot behavior and it has not changed. It is no
longer silent: every VM build sweeps the families and warns, naming the
special and what the wasted cast would cost.

**Alternates.** A special may declare `alternate: {name: ...}`, which is a
DISPLAY name shown while Shift is held. There is no separate cost and no
engine dispatch for it: the handler forks on `self:shifter_down()` and
spends the same `mp_cost`.

### Duplicate registration: last one wins

Registering a hook slot a previous chunk already filled is legal and the later
registration wins. Because pack scripts load pack-id-lexicographically and then
filename-lexicographically inside a pack, this is the supported way to
**override a core family**: a mod pack that registers `core:soldier` replaces
the core pack's soldier behavior. Overriding by id keeps working exactly as
before — `core:soldier` resolves to core's soldier through form 2, since that
is the id the core pack declared for it.

To override a stock family's *data* (stats, sprite, description) rather than
its behavior, give your `classpack.yaml` entry that family's `wire_id` **and
keep its `id:`** — `id: core:soldier`, `wire_id: 0`. The wire slot is the
family's identity, so an entry that claims slot 0 under a different `id:`
retires `core:soldier`: the id stops resolving (except through the bare-name
fallback, if you left `name:` alone) and the install warns.

Every collision logs a warning naming the order, family, hook and the source
location of the later registration, and is recorded as a script error record,
so an accidental clash inside your own pack is diagnosable instead of silent.
If two of your own scripts claim the same family, the lexicographically later
filename wins — split multi-file packs by family, not by concern.

## Entity handles

Hooks receive entity handles (a walker handle, or a guy handle for
`level_up`). A handle is valid for the dispatch that produced it; do not stash
it in a global or upvalue (R6). `==` compares entity identity. `nil` means "no
entity" — e.g. `self:foe()` when there is no foe.

Walker handles resolve through the world's entity-id index, with a fallback to
the raw pointer for entities already dying (so `on_death` can still read
`self`) guarded by the dispatch generation. A handle used after its dispatch
raises `stale or dead entity handle`. Guy handles are strictly
dispatch-scoped.

| Function | Result |
|---|---|
| `og.is_alive(h)` | Handle still resolves AND `dead() == 0`. |
| `og.entity_id(h)` | Stable sim entity id, `0` for an untracked entity. |
| `og.family_id(order, id_str)` | Wire byte, or `nil`. |

`og.entity_id` is the standard trick for "every Nth spawn" style logic without
mutable state: entity ids are assigned identically on every peer, so
`og.mod(og.entity_id(spawn), 3) == 0` selects a stable subset.

## Walker methods

Getters return integers for integer-typed C++ fields and numbers for
float-typed ones. Setters narrow through the exact C++ parameter type, so an
out-of-range value wraps the same way the C++ store did — you do not need an
extra `og.i16()` around a plain setter call.

### Properties

A walker handle also exposes a small set of dotted properties — **the
preferred spelling wherever one exists** (style contract S6). Each one
routes through the SAME registered accessor as its method spelling — same
value, same narrowing, same `stale or dead entity handle` on a bad handle —
so the two spellings cannot drift apart:

| Property | Backing accessors | Notes |
|---|---|---|
| `self.hp` | `s_hitpoints` / `s_set_hitpoints` | float |
| `self.max_hp` | `s_max_hitpoints` / `s_set_max_hitpoints` | float |
| `self.magicpoints` | `s_magicpoints` / `s_set_magicpoints` | float |
| `self.max_magicpoints` | `s_max_magicpoints` / `s_set_max_magicpoints` | float |
| `self.level` | `s_level` / `s_set_level` | int32 |
| `self.team` | `team_num` / `set_team_num` | narrows unsigned char |
| `self.busy` | write-only property | reads stay `self:busy()` |
| `self.dead` | write-only property | reads stay `self:dead()` |
| `self.weapons_left` | write-only property | reads stay the method |
| `self.lifetime`, `self.damage`, `self.ani_type`, `self.current_special`, `self.foe` | write-only properties | reads stay methods |
| `self.xpos`, `self.ypos` | read-only slots | writing errors (they read as methods) |

**Read resolution is method-first.** `self:busy()` is sugar for
`self.busy(self)`, so a name that is both a method and a property can only
serve one spelling — and every existing script calls the method. Reading a
name in the table above that is also a method (`busy`, `dead`,
`weapons_left`, `lifetime`, `damage`, `ani_type`, `current_special`, `foe`,
`xpos`, `ypos`) therefore answers the method function, exactly as before;
reading `hp`, `max_hp`, `magicpoints`, `max_magicpoints`, `level` or `team`
answers the value. **Writes have no such conflict** — assignment never
resolved methods — so every writable property above works as a write,
including the shadowed-read names: `self.busy = 5` runs `set_busy`'s float
path today.

Bad writes are script errors with distinct messages: a read-only property
(`walker property 'xpos' is read-only`), a method name (`'death' is a
walker method, not a writable property`), anything else (`cannot assign
unknown walker field '...'`). Unknown reads still answer `nil`.

**Float-valued** (getter returns a double holding an exact `float`):
`worldx worldy worldz lastx lasty stepsize damage fire_frequency busy
speed_bonus`.

### Position, size, floor

`xpos()` `ypos()` `sizex()` `sizey()` `sizez()` `worldx()` `worldy()`
`worldz()` `floor()` / `set_floor(v)` `setxy(x,y) → bool`
`setworldxy(x,y)` `center_on(other)` `distance_to_ob(other) → int`
`distance_to_ob_center(other) → int` `spaces_clear() → int`
`facing(x,y) → int`.

Positions are pixels, not tiles; `og.C.GRID_SIZE` converts. Set the floor
*before* `setxy` when placing an entity on a non-default floor (the obmap
keying rule).

### Movement, facing, animation

`lastx()/set_lastx(v)` `lasty()/set_lasty(v)` `stepsize()/set_stepsize(v)`
`curdir()/set_curdir(v)` `ani_type()/set_ani_type(v)` `cycle()/set_cycle(v)`
`drawcycle()` `set_frame(v) → int` `animate() → bool` `act_type()`
`in_act() → bool` `set_ignore(v)` `lineofsight()/set_lineofsight(v)`.

### Identity, team, life state

`family()` `order()` `user()` `has_guy() → bool`
`team_num()/set_team_num(v)` `real_team_num()/set_real_team_num(v)`
`dead()/set_dead(v)` `death_called()/set_death_called(v)`
`summoned()/set_summoned(bool)`
`save_all_protected()/set_save_all_protected(bool)`
`is_friendly(other) → bool`.

### Timers and charges

`lifetime()` `invulnerable_left()` `invisibility_left()` `flight_left()`
`charm_left()` `speed_bonus()` `speed_bonus_left()` `bonus_rounds()`
`weapons_left()` `keys()` `view_all()` `skip_exit()` `shifter_down()` — each
with a matching `set_*`.

### Combat and weapons

`damage()/set_damage(v)` `busy()/set_busy(v)`
`fire_frequency()/set_fire_frequency(v)`
`current_weapon()/set_current_weapon(v)`
`current_special()/set_current_special(v)`
`default_weapon()/set_default_weapon(v)`
`do_bounce()/set_do_bounce(v)` — **weapons only**; errors on any other order.

### References

`foe()/set_foe(h)` `leader()/set_leader(h)` `owner()/set_owner(h)`
`collide_ob()`. Setters accept `nil` to clear.

### Actions

| Method | Result |
|---|---|
| `attack(target)` | `bool`. **Draws from the RNG** — reordering or short-circuiting it changes the stream. |
| `fire()` | New weapon handle, or `nil`. |
| `special()` | `bool`. |
| `death()` | `bool`. |
| `teleport()` | `bool`. |
| `teleport_ranged(range)` | `bool`. |
| `find_teleport_target()` | Handle or `nil`. **Treasures only** (errors otherwise). |
| `turn_undead(range, power)` | `int`. |
| `do_summon(family, lifetime)` | Handle or `nil`. |
| `do_heal_effects(healer_or_nil, target, amount)` | — |
| `transform_to(order, family)` | — |
| `transfer_stats(target)` | — |
| `collide(other)` | `bool` — invokes the collision handler (this is `walker::collide`; the getter is `collide_ob()`). |
| `clear_myguy()` | — |
| `move_myguy_to(target)` | — |
| `set_difficulty(level)` | — |

## Statistics (`s_` prefix, called on the walker)

The `statistics` record is flattened onto the walker handle.

> **Deprecation (style contract S6).** The `s_*` getter/setter spellings
> leak the 1995 struct layout and are **legacy aliases**: they keep working
> indefinitely (removing them would break out-of-tree packs), but new and
> refactored code uses the [property spellings](#properties) where one
> exists (`self.hp`, not `self:s_hitpoints()`) and the fused verbs
> (`add_frozen_stun`, `heal_clamped`) instead of get→combine→set chains.
> The methods below that have no property or fused-verb replacement
> (command queue, bit flags, special costs, the raw/read-only extras) are
> not deprecated — they are the API for what they do.

**Float-valued**: `s_hitpoints s_max_hitpoints s_magicpoints
s_max_magicpoints s_armor s_magic_per_round s_heal_per_round` — each with
`s_set_*`.

**Integer-valued**: `s_level s_weapon_cost s_frozen_delay s_current_distance
s_last_distance s_max_heal_delay s_current_heal_delay s_max_magic_delay
s_current_magic_delay` — each with `s_set_*`.

**Read-only extras**

| Method | Meaning |
|---|---|
| `s_frozen_delay_raw()` | The unmasked value. Negatives are the thaw-immunity phase that the plain `s_frozen_delay()` getter hides (the orc howl needs the raw number). |
| `s_old_family()` | Pre-transform family — how cleric resurrect restores a corpse's original family. |
| `s_controller()` | The walker that owns this stats record, or `nil`. |
| `s_name()` / `s_set_name(str)` | Entity name string. |

**Fused verbs**

| Method | Meaning |
|---|---|
| `add_frozen_stun(n)` | `s_set_frozen_delay(og.combat.stun_total(s_frozen_delay_raw(), n))` fused into one call, in exactly that order: RAW read (thaw-immunity negatives are seen by the policy, not masked to 0 first), `stun_total`'s discard/cap, then the short-narrowing setter. The universal application pattern for orc-yell-style stun adds. |
| `heal_clamped(amount [, source])` | The self-heal cluster (orc eat-corpse shape), fused in exactly this order: (1) `s_set_hitpoints(og.fadd(s_hitpoints(), amount))`, (2) `do_heal_effects(source, self, og.i16(amount))` — the marker carries the FULL amount, int16-narrowed, even when the clamp follows, (3) clamp to `s_max_hitpoints()`. `amount` is an integer; `source` may be omitted or `nil` (only the target's marker lands then). Requires an active world BEFORE any mutation. |

**Specials and flags**

`s_special_cost(i) → int` and `s_set_special_cost(i, v)` — `i` must be in
`[0, og.C.NUM_SPECIALS)`, out-of-range is an error, not a clamp.
`s_query_bit_flags(flag) → bool` and `s_set_bit_flags(flag, 0|1)`, with flag
values from `og.C.BIT_*`.

**Command queue**

| Method | Meaning |
|---|---|
| `s_add_command(cmd, iterations, info1, info2)` | Append. |
| `s_force_command(cmd, iterations, info1, info2)` | Prepend as a forced command. |
| `s_set_command(cmd, iterations, info1, info2)` | Replace. |
| `s_clear_command()` | Empty the queue. |
| `s_has_commands() → bool` | |
| `s_do_command() → int` | Execute one queued step. The only binding that *runs* the queue; the rest only edit it. |
| `s_force_fright(iterations, info1, info2)` | The ghost-scare fright injection (`statistics::force_fright`). NOT interchangeable with `s_force_command`: it MERGES into an existing forced `COMMAND_WALK` at the queue front so overlapping scares cannot stack end to end. |
| `s_forward_blocked() → bool` | |

## Guy record (`g_` prefix)

Called on a **walker that has a guy** (`has_guy()`; otherwise the call errors
with `walker has no guy record`):

`g_strength g_dexterity g_constitution g_intelligence g_armor g_exp
g_total_shots g_scen_shots g_total_hits g_scen_hits` — each with `g_set_*`.
Read-only: `g_level()`, `g_name()`.

| Method | Meaning |
|---|---|
| `g_update_derived_stats(walker)` | Recompute the walker's derived stats from the guy record. |
| `g_upgrade_to_level(level [, set_xp])` | `guy::upgrade_to_level`. `set_xp` defaults to true. Re-dispatches the family's `level_up` hook internally; the nested VM entry is fine (budgets arm on the outermost call only). |

**The guy handle passed to `level_up` carries a smaller set**: `g_strength`,
`g_dexterity`, `g_constitution`, `g_intelligence`, `g_armor` (each with
`g_set_*`), plus read-only `g_level`, `g_exp`, `g_name`. The shot/hit
counters and the two methods above are not on it. `level_up` typically
delegates to `og.apply_level_up(guy, diff, str, dex, con, intel, armor)`,
which accepts either a guy handle or a walker.

## World API (`og.*`)

### Spawning

| Function | Result |
|---|---|
| `og.add_ob(order, family [, atstart])` | Handle or `nil`. Adds to the ob list (weapons go to the weapon list). Dispatches `on_entity_spawn` for livings and generators. |
| `og.add_fx_ob(order, family)` | Handle or `nil` — fx list. |
| `og.add_weap_ob(order, family)` | Handle or `nil` — weapon list. |
| `og.summon(self, order, family)` | Handle or `nil` — spawns at the summoner with `owner` set. |
| `og.summon_configured(self, order, family, opts)` | `og.summon` plus the standard configuration cluster (soldier-boomerang shape), applied in this FIXED order: `ani_type` (char narrowing), `lifetime` (int32), `hp_add` (float add onto hitpoints), `max_hp_from_hp` (truthy: copy hp into max hp), `damage_add` (float add onto damage). Absent keys apply nothing; every present key is type-checked and an unknown key is an error BEFORE the summon happens (a typo'd option must not half-configure a live entity). A failed summon answers `nil` exactly like `og.summon`. |

`order` is the same string vocabulary as `og.register_hooks`; `family` is a
**byte**, so resolve it with `og.family_id` first.

### Queries

| Function | Result |
|---|---|
| `og.find_near_foe(self)` | Handle or `nil`. |
| `og.find_nearest_blood(self)` | Handle or `nil`. |
| `og.foes_in_range(self, range)` | Array — the `for_each_foe_in_range` traversal. |
| `og.find_foes_in_range(list, range, self)` | Array, count. |
| `og.find_friends_in_range(list, range, self)` | Array, count. |
| `og.find_in_range(list, range, self)` | Array, count. |
| `og.find_foe_weapons_in_range(list, range, self)` | Array, count. |
| `og.oblist()` | Array of every entity in the ob list, in list order. |
| `og.living_count()` | The world's living head-count field. NOT derivable from an `og.oblist()` scan — the counter can legitimately drift (an editor map resize erases livings without decrementing), and scripts must read the same field the C++ read. |
| `og.remaining_foes(self)` | `int`. |

`list` selects which world entity list the C++ call scanned: `"ob"`,
`"weap"`, or `"fx"`. Any other string is an error. All arrays preserve the
C++ iteration order — walk them with `for i = 1, #t` (there is no `pairs`).

### Terrain

`og.query_passable(x, y, self [, floor])`,
`og.query_grid_passable(...)`, `og.query_object_passable(...)` — all
`→ bool`, all take pixel coordinates. `og.query_object_passable` **draws from
the RNG** through the obmap miss roll.

`og.query_genre(tile_x, tile_y [, floor]) → int` asks what the terrain *is*
rather than whether it can be walked on — compare the result against
`og.C.TYPE_*` (`TYPE_WALL`, `TYPE_WATER`, `TYPE_TREES`, …). It is the one
terrain call that takes **tile** coordinates, because the smoother it reads
does; divide pixels by `og.C.GRID_SIZE` first. Out-of-range tiles report
`TYPE_GRASS`, so there is no nil case. Omitting `floor` reads the
default-floor smoother.

### Family data

`og.family_flag(order, family_byte, flag_name) → bool | nil` — a read-only
view of living-descriptor flags, for scripts branching on another entity's
family traits. Only `order == "living"` is supported (anything else errors).
Recognised flags: `has_returning_weapon`, `is_undead`, `leaves_bloodspot`,
`is_stationary`. An unknown flag name is an error; an unpopulated family byte
returns `nil`.

**`og.tuning(self) → frozen table`** — the `tuning:` map self's family
declared in `classpack.yaml`, for lifting balance constants out of behavior
code:

```yaml
families:
  living:
    - id: mymod:brute
      tuning:
        yell_stun: 10      # plain integer → Lua integer
        heal_scale: 2.5    # decimal → Lua float
        eats_corpses: true # boolean
        label: "5"         # QUOTED stays a string
```

```lua
local stun = og.tuning(self).yell_stun or 10
```

Key access only: reads are plain indexing, absent keys answer `nil` (so
scripts can carry defaults), writes raise `attempt to modify a read-only
table`, and no iteration is provided — the no-`pairs` rule never comes up.
A family that declared nothing gets an empty frozen table. Tuning is
LOAD-TIME pack content exactly like the rest of the descriptor: it rides
multiplayer pack transfer inside `classpack.yaml` and never appears in a
snapshot, and identical values mean byte-identical sim.

### Animation tables

A consumer reads the row through these two calls rather than indexing a raw
table, because the bound accessor applies the `walker::ani_count` invariant
that protects the four `animate()` sites from snapshot-driven out-of-range
rows.

| Function | Result |
|---|---|
| `og.ani_frame(entity, row, index)` | The single frame value, or `nil`. `nil` covers every case where the C++ `if (self->ani)` guard (or a bad row/index) would have bailed, so the script simply skips its `set_frame`. The sentinel slot itself is addressable (`index == length`), so a legitimately empty row reads back the same `-1` the C++ would have handed `set_frame`. |
| `og.ani_row(entity, row)` | Array of the frames up to (excluding) the `-1` sentinel, or `nil`. An empty table means "present but zero-length" (the C++ `seq_len <= 0` stop); `nil` means no table, row past `ani_count`, a null row, or a missing sentinel. |

Row layout matches the built-in tables: `row = ani_type * 8 + curdir`.

### Sim events

| Function | Effect |
|---|---|
| `og.emit_sound(id)` | `og.C.SOUND_*`. |
| `og.emit_positional_sound(self, id)` | |
| `og.emit_notification(text [, duration])` | Integers and plain strings only — never format a float into sim-visible text. |
| `og.emit_event(kind, a, b)` | Raw event; `kind` from `og.C.EVENT_*`. `a`/`b` default to 0. |

### World state

| Function | Result |
|---|---|
| `og.level_id()` | The world's level id. |
| `og.level_tick()` | Tick counter for this level. |
| `og.level_done()` | `int`. |
| `og.game_ended()` | `bool`. |
| `og.my_team()` | The local team number. |
| `og.enemy_freeze()` / `og.set_enemy_freeze(v)` | The freeze bank. |
| `og.set_palette(id)` | Sets `current_palette_id`. Pair it with an `EVENT_SET_PALETTE` emit, exactly like the C++ did — the field alone changes no pixels. |
| `og.current_scenario()` | Current scenario number. |
| `og.level_completed(level) → bool` | Whether that level is in the completed set. |
| `og.world_can_exit_whenever() → bool` | The `TYPE_CAN_EXIT_WHENEVER` world flag. |
| `og.scenario_title(name) → string` | Reads a scenario's display title through the world's provider seam; `"none"` when there is no provider or the read fails, so a caller's existing fallback still works. |

### Score, exit flow, CTF

| Function | Effect |
|---|---|
| `og.award_score(team, points)` | Bumps the team's score and emits `ScoreChange`. Teams outside the score table are silently ignored, matching the C++ `is_valid_score_team()` guard. |
| `og.set_withdraw_request(level)` | Latches the exit pad's withdraw request (`withdraw_requested` + `withdraw_level` together). |
| `og.emit_exit_confirmation(prompt, dest_level [, is_withdraw])` | `RequestExitConfirmation` with the exit pad's payload. |
| `og.emit_withdraw_to_level(level)` | `WithdrawToLevel`. |
| `og.ctf_on_flag_touch(flag, eater) → bool` | The whole CTF flag-touch operation, wrapped as one call exactly as the C++ treasure hook delegates to it. CTF rules stay in the CTF engine. `og.ctf_flag_touch` is the same function under the `og.*` verb spelling. |

### Shared helpers

These engine helpers are exposed so pack code does not have to re-derive
them or risk changing their RNG draws.

| Function | Result |
|---|---|
| `og.soften(raw, knee, ceiling)` | `int` — the soft-knee cap. |
| `og.charm_duration(level_diff)` | `int`. Draws RNG. |
| `og.freeze_duration(level, con)` | `int`. Draws RNG. |
| `og.heal_amount(mp, level)` | `amount, cost`. Draws RNG. |
| `og.scare_duration(level)` | `int`. |
| `og.scare_radius(level)` | `int`. |
| `og.elemental_lifetime(level)` | `int`. |
| `og.image_lifetime(level)` | `int`. |
| `og.entity_display_name(self [, fallback])` | `string`. |
| `og.exp_from_action(self, target_or_nil, action, value)` | `int`. `action` ∈ `attack`, `kill`, `heal`, `turn_undead`, `raise_skeleton`, `raise_ghost`, `resurrect`, `resurrect_penalty`, `protection`, `eat_corpse`; anything else is an error. |
| `og.apply_level_up(guy, diff, str, dex, con, intel, armor)` | — |
| `og.apply_difficulty_scaling(self, level, hp, mp, dmg, armor)` | — Livings only. |
| `og.check_special_ai_distance(self, threshold) → bool` | Livings only. |

### `og.combat.*` — combat_math.h, bound directly

Draw-free bindings over the `og::combat` constexpr helpers in
`include/openglad/core/combat_math.h` — the formulas pack scripts used to
re-implement by hand or compose from `og.soften` plus copied constants.
Prefer these over spelling the formula out: the knee/cap policy then has
exactly one definition. (The four flat spellings that predate the namespace
— `og.scare_duration`, `og.scare_radius`, `og.elemental_lifetime`,
`og.image_lifetime` — stay as they are; new combat_math surface lands here.)

| Function | C++ helper (legacy formula) |
|---|---|
| `og.combat.yell_radius(level)` | `yell_radius` — orc yell radius, `160 + 20*L` px, flat cap 420. |
| `og.combat.stun_total(cur_raw, add)` | `stun_total` — orc yell stun accumulator over RAW `frozen_delay`: `cur_raw < 0` (thaw immunity) discards the add; negative adds count as 0; monotonic cap at 150 (an over-cap value is answered unchanged). |
| `og.combat.bomb_damage(level)` | `bomb_damage` — thief bomb, `soften(15*(L+1), 210, 300)`. |
| `og.combat.cloak_total(cur, gain)` | `cloak_total` — thief cloak accumulator, `max(cur, min(cur + gain, 350))`; never reduces a potion-granted over-cap value. |
| `og.combat.glow_bonus(level)` | `glow_bonus` — cleric glow lifetime bonus, `110*L`, FLAT cap 2200 (deliberately not a soften). |
| `og.combat.druid_faerie_lifetime(level)` | `druid_faerie_lifetime` — `soften(50 + 40*L, 570, 800)`. |
| `og.combat.skeleton_lifetime(level)` | `skeleton_lifetime` — `soften(125 + 40*L, 645, 900)`. |
| `og.combat.ghost_raise_lifetime(level)` | `ghost_raise_lifetime` — `soften(150 + 40*L, 670, 925)`. |

## Pack lib modules (`og.use`)

The sandbox strips `require` on purpose, so shared helpers used to be
impossible — every chunk was an island and duplication was forced. The
engine-provided replacement: a pack may ship modules as
`packs/<id>/lib/<name>.lua`, and a chunk of that pack binds them with

```lua
local common = og.use("living_common")
```

Rules, each of which is deterministic by construction:

* **Load once, eagerly, in order.** Every new VM loads each registered
  module exactly once — pack-id-lexicographic, then filename-lexicographic,
  before any pack script runs — and memoizes the export `og.use` answers.
  A module may `og.use` a later module of its own pack; it loads on demand
  inside that load, once.
* **`return` your exports.** A module must end with
  `return <table of functions/constants>`; falling off the end is a load
  error. A non-table export (a bare constant) is allowed and passes through
  unwrapped.
* **Exports are frozen.** The returned table is served as a read-only view:
  writes raise (`attempt to modify a read-only table`), `#` works, the
  metatable is fenced. The freeze is shallow — which is not an invitation:
  lib modules must be PURE (tables of functions and constants, no
  chunk-level mutable state). That is cookbook R6 at the module boundary;
  the shallow freeze cannot detect mutable closure upvalues.
* **Pack-relative.** A chunk of pack P resolves P's modules only; the error
  for a missing module names the expected path
  (`packs/<id>/lib/<name>.lua`).
* **Load-time only.** `og.use` works while a pack chunk (script or module)
  is loading and errors at dispatch time — bind modules to locals at the
  top of the chunk. A dispatch-time `og.use` would be hidden coupling the
  reader cannot see.
* **Same fences as every chunk.** Text-only compile, instruction/memory
  budgets, fresh isolated environment per module (modules communicate
  through exports, never globals). Cycles are detected
  (`circular dependency on module '...'`) and a failed module latches: every
  later `og.use` of it errors the same way instead of re-running it.
* **They ride everything automatically.** Lib files are ordinary pack
  content: the multiplayer transfer manifest ships them, the coverage
  denominator counts them, and the statement-per-line lint scans them —
  the same as `scripts/`.

## Level scripts

```lua
og.register_level_hooks(level_id, {   -- level_id -1 = every level
  on_load         = function(level) ... end,
  on_tick         = function(level, tick) ... end,
  on_entity_death = function(ent) ... end,
  on_entity_spawn = function(ent) ... end,
})

og.set_entity_hooks(ent, { on_death = function(ent) ... end })
```

- `on_load` fires on the first tick of the level **on this peer** — fresh
  start and mid-join alike. Derive everything from the world and make it
  idempotent.
- `on_tick` fires every tick, before entity acts.
- `on_entity_death` fires for **living and generator** deaths. Generators
  dispatch it after their death FX, so the hook observes the same world a
  living-death hook would; a pillar or tower falling is an event, not
  something to poll for.
- `on_entity_spawn` fires only for sim-authored living/generator spawns
  through `add_ob`. Snapshot and replay insertion paths bypass it and stay
  silent.
- An exact-level registration shadows a wildcard (`-1`) one **per hook kind**.
- `og.set_entity_hooks` requires a tracked entity (`og.entity_id(h) ~= 0`).
  The registration is consumed when it fires, so a dead entity id never fires
  twice. Register them from `on_load` or `on_entity_spawn` after selecting
  entities via `og.oblist()`, position, or family.
- Win/lose logic belongs in `on_tick` via the world-state getters.
- Registering a table with no recognised hook name is a load error.

A campaign ships level scripts inside its embedded pack
(`packs/<pack_id>/scripts/` in the campaign zip), keyed by its level ids. The
pack mounts and unmounts with the campaign.

## Constants (`og.C.*`)

| Group | Members |
|---|---|
| Orders | `ORDER_LIVING ORDER_WEAPON ORDER_TREASURE ORDER_GENERATOR ORDER_FX` |
| Commands | `COMMAND_` + `WALK FIRE RANDOM_WALK DIE FOLLOW RUSH MULTIDO QUICK_FIRE SET_WEAPON RESET_WEAPON SEARCH ATTACK RIGHT_WALK UNCHARM` |
| Stat bit flags | `BIT_` + `FLYING SWIMMING ANIMATE INVINCIBLE NO_RANGED IMMORTAL NO_COLLIDE PHANTOM NAMED FORESTWALK MAGICAL FIRE ETHEREAL` |
| Animation types | `ANI_` + `WALK ATTACK TELE_OUT TELE_IN SKEL_GROW SLIME_SPLIT EXPLODE GROW GLOWGROW GLOWPULSE EXPAND_8 DOOR_OPEN SCARE BOMB SPIN` |
| Act types | `ACT_` + `RANDOM FIRE CONTROL GUARD GENERATE DIE SIT` |
| Sounds | `SOUND_` + `BOW CLANG DIE1 BLAST SPARKLE TELEPORT YO BOLT HEAL CHARGE FWIP EXPLODE DIE2 ROAR MONEY EAT` |
| Sim event kinds | `EVENT_` + `PLAY_SOUND NOTIFICATION SET_PALETTE REQUEST_REDRAW DAMAGE_TILE` |
| Combat caps | `SHOT_DRAIN_CAP MP_POOL_DAMAGE_CAP ENEMY_FREEZE_BANK_CAP STARBURST_ADD_CAP MACE_LIFE_CAP SPRINKLE_REFRESH_OWNER_LEVEL SPRINKLE_REFRESH_FLOOR` |
| Facings | `FACE_` + `UP UP_RIGHT RIGHT DOWN_RIGHT DOWN DOWN_LEFT LEFT UP_LEFT`, plus `NUM_FACINGS` |
| Terrain genres | `TYPE_` + `GRASS WATER TREES DIRT COBBLE GRASS_DARK DIRT_DARK WALL CARPET GRASS_LIGHT AIR GLASS DROP_BLOCK ZSTAIRS SNOW LAVA MARSH ASH UNKNOWN` |
| Misc | `GRID_SIZE NUM_SPECIALS MAXOBS` |

## Script errors and logging

Two engine behaviors you will meet the first time a pack misbehaves:

- **Errors are traced, bounded and de-duplicated.** Every occurrence traces
  under the `script_error` category with its source location and message —
  that is the live signal and it never collapses. The stored record vector is
  capped at `kMaxStoredScriptErrors` (64) *distinct* `(where, message)` pairs;
  a repeat bumps that record's `count` instead of appending, and occurrences
  past the cap only increment `dropped_error_count()`. So a hook that errors
  every tick on every instance costs one record, not one per tick, and the
  first errors — the ones that explain the break — are the ones kept.
- **The log transcript is a bounded tail.** `og.log`/`print` always traces
  under the `script` category, but the stored transcript keeps only the most
  recent `kMaxStoredScriptLogLines` (512) lines, evicting oldest-first, with
  the evicted count in `dropped_log_line_count()`.

Both stores live on the `ScriptHost` and are per-world (server world and
local mirror have separate VMs, the same isolation rule as the obmap).

## Worked example: a complete class pack

`docs/modding/examples/emberwisp/` is a runnable pack you can read end to
end in a few minutes. Nothing in it is inherited from a core family: it
ships its own sprite sheet, its own animation table, its own descriptor data
and its own behavior script.

```
docs/modding/examples/emberwisp/
├── classpack.yaml            one living family, wire_id: auto, tuning block
├── scripts/emberwisp.lua     hooks + a specials-table special
└── sprites/
    ├── emberwisp.png         16x16, 8 frames, indexed to the engine palette
    └── emberwisp.json        Aseprite "Hash" sidecar describing the frames
```

The script is written in the current idiom
([lua-style.md](../lua-style.md)): walker properties, `og.tuning`, a
`specials` table, `og.rand` where the bound is a positive literal and
`og.rand0` where tuning can drive it to zero, and the `add_frozen_stun`
fused verb. It deliberately does *not* use `og.use` — that is for helpers
shared by two or more files (S4), and a one-script pack keeps helpers
local. `packs/core/lib/` has the real modules.

It deliberately sits outside `packs/`, because everything under `packs/` is
mounted at startup and a new living family would change the hire menu, the
registries and the auto-assigned wire ids of the shipped game. Mount it
explicitly instead — the mount point is what fixes the pack id and makes the
`sprite:` path resolve:

```cpp
og::resources::mount("docs/modding/examples/emberwisp", "packs/emberwisp/", 1);
og::resources::refresh_pack_scripts();   // rescans scripts + reinstalls YAML
loader.reload_graphics();                // picks up the pack's sprites
```

Its README covers the art pipeline. The parts worth reading here are the
descriptor block, the animation table, and the script.

### The family, in `classpack.yaml`

```yaml
families:
  living:
    - id: example:emberwisp
      wire_id: auto                       # next free id >= 21, deterministic
      name: "EMBERWISP"                   # THE identity hooks resolve against
      stats:                              # all six required
        strength: 8
        dexterity: 14
        constitution: 7
        intelligence: 16
        armor: 4
        level: 1                          # starting level
      combat:                             # all five required
        hp: 70
        melee_damage: 11
        stepsize: 5                       # px per step
        fire_delay: 9                     # busy ticks AFTER an attack;
                                          # lower is faster
        fire_mp_cost: 2                   # MP per ranged shot
      costs:                              # gold only
        hire: 400
        train: {strength: 30, dexterity: 8, constitution: 30,
                intelligence: 8, armor: 60}
      specials:                           # order = slot; slot N at level
        - id: flare_burst                 # (N-1)*3+1
          name: "FLARE BURST"             # HUD string
          mp_cost: 5                      # engine-gated, engine-deducted
      init_bit_flags: [FLYING, FORESTWALK]
      default_weapon: core:fireball       # via the weapon registry
      init_max_magicpoints: 40            # the live max-MP knob (the usual
                                          # maximum is 10 + INT*3)
      leaves_bloodspot: false
      magic_damage_modifier: 0.5
      death_message: "EMBERWISP GUTTERS OUT"
      sprite: packs/emberwisp/sprites/emberwisp.png   # virtual-FS path
      animation: emberwisp_motion         # this pack's own set, below
      playable: true
      playable_order: 90
      glyph: "✦"                          # one UTF-8 codepoint
      glyph_ascii: "*"                    # one byte, for ASCII terminals
      glyph_color: yellow
      glyph_bold: true
      radar_color: 228
      radar_jitter: 3                     # adds rand(3) — a real RNG call
      tuning:                             # balance data, read by the script
        flare_cost: 6.0                   # decimal → Lua float
        burn_floor: 10                    # integer → Lua integer
        burst_range: 80
        stun_base: 4
        stun_per_level: 8
```

Every key except `id` is optional, and an undeclared key changes nothing:
the installer copies the registry slot's current descriptor and patches only
what the YAML names. The `tuning:` block is the family's own dial panel —
`og.tuning(self)` serves it back to the script as a frozen table, so
rebalancing the wisp is a YAML edit, not a code edit.

### Its animation table

```yaml
anims:
  emberwisp_motion:
    rows: 16                       # two ani_types over eight facings
    frames:
      - [0, 1, 2, 3, 2, 1]         # ani_type 0 (walk), facing 0
      # ... facings 1..7, same list (the wisp has no directional art)
      - [4, 5, 6, 7]               # ani_type 1 (attack), facing 0
      # ... facings 1..7
```

Row index is `ani_type * 8 + curdir`; frames are indices into the sprite's
frame stack; there is no `-1` in the YAML because the row's end *is* the end.
Declaring fewer rows than `rows:` asks for repeats them **cyclically**, so
two declared rows over `rows: 16` would alternate walk/attack per facing
rather than filling eight of each — write every row out unless the cycle is
exactly what you want.

### Its behavior, in `docs/modding/examples/emberwisp/scripts/emberwisp.lua`

```lua
local C = og.C

-- Each wisp starts with a random slice of its ember already spent.
-- Exactly one draw, unconditional, so the RNG stream advances identically
-- on every peer (R4). The bound is a positive literal, so plain og.rand
-- is right — its error on n <= 0 is a tripwire worth keeping.
local function on_create(self)
  local spent = og.rand(16)
  -- magicpoints is a C++ float: per-op rounding.
  self.magicpoints = og.fsub(self.max_magicpoints, spent)
  self.ani_type = C.ANI_WALK
end

-- Returning false blocks the shot: a wisp below the flare cost cannot
-- fire. One that can flares into ani_type 1 — rows 8..15 of this pack's
-- own animation table.
local function on_fire_weapon(self)
  if self.magicpoints < og.tuning(self).flare_cost then
    return false
  end
  self.ani_type = C.ANI_ATTACK
  self:set_cycle(0)
  return true
end

-- Special 1, FLARE BURST: vent every point of ember above the burn floor
-- as a stunning flash. Answering false means "did not fire" — the engine
-- then skips the special's descriptor MP cost.
local function flare_burst(self)
  local t = og.tuning(self)
  local ember = og.trunc(self.magicpoints) - t.burn_floor
  if ember <= 0 then
    return false
  end
  -- Tuning is modder data: clamp it into a sane sim window before use.
  local range = og.clamp(t.burst_range, C.GRID_SIZE, 320)
  local foes = og.foes_in_range(self, range)
  for i = 1, #foes do
    -- One stun roll per foe, in list order (R4). The bound is tuning-
    -- driven and may be zero: og.rand0 answers 0 then WITHOUT advancing
    -- the stream (IRandom::next(0) semantics).
    local roll = og.rand0(self.level * t.stun_per_level)
    -- add_frozen_stun is combat_math.stun_total fused with the setter;
    -- the thaw-immunity discard and the 150 cap are its policy, not ours.
    foes[i]:add_frozen_stun(t.stun_base + roll)
  end
  -- magicpoints and busy are C++ floats: per-op rounding.
  self.magicpoints = og.fsub(self.magicpoints, ember)
  self.busy = og.fadd(self:busy(), 4.0)
  og.emit_positional_sound(self, C.SOUND_EXPLODE)
  return true
end

og.register_hooks("living", "example:emberwisp", {
  on_create = on_create,
  on_fire_weapon = on_fire_weapon,
  -- The specials table replaces a hand-written current_special() ladder.
  -- The key is the `id` the YAML declares for the slot; `default` (absent
  -- here) catches the rest, and a table with neither entry makes that cast
  -- a successful no-op.
  specials = {
    flare_burst = flare_burst,
  },
})
```

Details worth noticing: `self.magicpoints` reads the property, but the
`busy` *read* stays `self:busy()` (reads resolve method-first — `busy` is
also a method name — while the `self.busy =` write works for every writable
property); the plain `-` and `*` on `ember` and the roll bound are exact
integer arithmetic, so no `og.f*` shim; and both `og.rand` and `og.rand0`
appear, each where its contract is the right one.

### Where to go for more

| Pattern | Read |
|---|---|
| A whole family, canonical style | `packs/core/scripts/soldier.lua` beside `packs/core/families/living-00-soldier.yaml` (the core pack uses the split per-family layout; behavior dispatches through Lua) |
| Specials table, `og.rand0`, tuning reads, `add_frozen_stun` | `packs/core/scripts/orc.lua` beside `packs/core/families/living-14-orc.yaml` |
| `og.use` lib modules (shared preludes, parameterized AI gates, shared effect geometry) | `packs/core/lib/living_common.lua`, `packs/core/lib/ai.lua`, `packs/core/lib/effect_common.lua` |
| Level hooks, per-entity hooks, generator `customize_spawn` | `court.lua`, embedded in `tools/concept_mapgen/showcase_pack.cpp` |
| Every descriptor key, per order | [design doc §4](../lua-classpacks-design.md) |
| Naming, headers, comments, shim policy | [lua-style.md](../lua-style.md) (S1–S6) |

### One statement per line

Pack Lua is measured by the same coverage gate as the C++, and line coverage
counts lines. `if low then flee() end` on one line makes the branch body share
a coverage point with the test that guards it, so a branch nothing ever takes
reads as covered. Write it out:

```lua
-- rejected by scripts/check_lua_statement_lines.py
if self:busy() > 0 then return false end

-- what to write instead
if self:busy() > 0 then
  return false
end
```

The rule is mechanical: no statement after `then` / `do` / `else` / `repeat`,
after a `;`, or on a function's header line, and no two statements run
together. An empty block (`function() end`, `if x then end`) is fine — it
hides nothing. The check runs on every build, over `packs/`, over the example
packs under `docs/modding/`, and over pack Lua that lives in a C++ `R"LUA(`
literal.

### Two limits worth knowing before you design

- **A living family with no `sprite:` has no graphics and cannot be drawn.**
  Sprite lookup tries `pix/<value>` first, then `<value>` as a virtual-FS
  path, and the frame sidecar is resolved with whichever prefix opened the
  PNG — so a pack's `.json` always sits beside its own `.png` and can
  neither shadow nor be shadowed by anything under `pix/`.
- **Pack families of the four non-living orders get only art and motion from
  their descriptor.** Hitpoints, act type, stepsize, damage and line of
  sight for weapons/effects/treasures/generators come from the loader's
  `EntityDef` table, which pins core ids only; a mod weapon therefore starts
  at zero for all of them and must set what it needs on the entity after
  spawning it. Living families do not have this gap — their loader stats
  come from the descriptor's `combat:` block.

## Transliteration checklist

For porting C++-shaped behavior into pack Lua (new mods should just write
the idiom the worked example shows; this list is for byte-exact ports).

1. Map every C++ float operator to exactly one `og.f*` call; never chain in
   Lua. Comparing floats directly is fine. Plain `+`/`-`/`*` is safe only
   when the exact result is representable in C `float`; division never is.
2. Map integer `/` and `%` to `og.div` / `og.mod` unless documented operand
   ranges prove Lua floor division/remainder identical to C truncation.
3. `(int32)someFloat` → `og.trunc`. Explicit narrowing casts → `og.i8`/
   `og.i16`/`og.u8` **only** where the C++ did more than a plain setter store
   (setters — and the properties routed through them — already narrow).
4. Preserve `og.rand` call order and count exactly. A guarded C++ draw
   (`if (n > 0) r = rng(n)`) is one `og.rand0(n)` — identical stream both
   ways. Watch for C++ expressions with two `rng.next()` calls: C++ operand
   order is unspecified, so make the order explicit and let parity
   adjudicate PER SITE. Adjudicated so far: comparison operands
   (`rng(a) >= rng(b)`) ran LEFT-first (thief, orc); function-call arguments
   (`f(..., rng(3), rng(3))`) ran RIGHT-first (slime grow). Do not assume
   either — flip on parity failure.
5. Remember the calls that draw from the RNG without looking like it:
   `attack()`, `query_object_passable()`, `og.charm_duration`,
   `og.freeze_duration`, `og.heal_amount`.
6. `for_each_foe_in_range` → `og.foes_in_range`; `world->find_*_in_range` →
   the matching `og.find_*` with the correct list selector.
7. `dynamic_cast<living*>` guards → `self:order() ~= og.C.ORDER_LIVING`.
8. A `switch (current_special())` ladder → a `specials` table; the
   dispatcher's select/default/fall-through semantics are the ladder's.
9. Keep every emitted string byte-identical.
10. Run `og_test_parity` after each family; goldens must not change.
