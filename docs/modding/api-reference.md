# OpenGlad Scripting API Reference

The class-pack Lua API. Read `docs/lua-classpacks-design.md` first — the
Determinism Cookbook (R1–R10) governs everything here. This file is the
symbol-level reference for pack authors and for transliterating C++ family
code.

## Sandbox

Available: `string` (no `dump`), `table`, integer-safe `math` subset
(`floor ceil abs min max tointeger type maxinteger mininteger huge pi ult`),
`ipairs`, `select`, `tonumber`, address-free `tostring`, `pcall`/`xpcall`,
`assert`/`error`, `rawget/rawset/rawequal/rawlen`,
`setmetatable`/`getmetatable`, `print` (= `og.log`).

NOT available (deliberately): `pairs`/`next`, `io`, `os`, `require`, `load`,
`coroutine`, `debug`, `utf8`, `math.random`, transcendentals. Budgets: 5M VM
instructions per hook call, 32 MiB per VM.

Scripts in the same pack share one environment (globals are pack-local);
different packs are isolated. Use `local` for everything unless you are
deliberately sharing helpers between a pack's scripts — and NEVER store
mutable sim state in globals/upvalues (cookbook R6).

## Deterministic arithmetic (`og.*`)

| Function | Semantics |
|---|---|
| `og.div(a,b)` / `og.mod(a,b)` | C integer division/remainder (truncate toward zero; sign of dividend). Errors on `b==0` / overflow. Use for EVERY integer `/` `%`. |
| `og.fadd/fsub/fmul/fdiv(a,b)` | One operation performed in C `float`, returned as the exact widened double. Use one call per C++ float operator. Never use Lua `/` on floats. |
| `og.i8/i16/i32/u8(x)` | Modular narrowing of an integer, matching C++ casts. |
| `og.trunc(x)` | `(int64)trunc(double)` — the C cast-float-to-int semantics. Errors on NaN/out-of-range. |
| `og.rand(n)` | Sim RNG: uniform `[0, n)`. THE only randomness source. Preserve call order/count when transliterating. |
| `og.cosmetic_rand(n)` | The C++ `cosmetic_rng_override()` pattern: parity-harness cosmetic stream when installed, else the sim RNG. Use ONLY where the C++ drew through that selector (elf spread, path-check cadence). |

## Hooks

```lua
og.register_hooks(order, family_id_string, { hook_name = function ... })
```

`order` ∈ `"living" | "weapon" | "treasure" | "generator" | "fx"` (alias
`"effect"`). Family ids: `"core:<name>"` for built-ins (e.g. `core:soldier`,
`core:boomerang`) — the registry name lowercased with spaces→underscores.
When registry names collide (golem/giant_skeleton/tower1 all answer to
BEAST), use the numeric escape `"core:#<id>"` (e.g. `core:#19` for
giant_skeleton). Unknown family or hook name = load error (the whole
chunk is rejected). A hook that errors at runtime is treated as absent for
that dispatch (deterministic on every peer) and the error is recorded.

Hook signatures (return values coerced to boolean where meaningful):

- living: `do_special(self)→bool`, `check_special_ai(self)→bool`,
  `hit_response(self, foe)`, `set_difficulty(self, level)`,
  `level_up(guy, level_diff)`, `on_death(self)→bool`,
  `on_act_living(self)`, `on_shoved(self)`,
  `on_fire_weapon(self, weapon)→bool` (false blocks the shot),
  `handle_teleport(self)→bool`, `on_create(self)`,
  `customize_weapon(self, weapon)`, `on_ani_complete(self)→bool`,
  `on_melee_hit(self, target)`
- weapon: `on_death(self)→bool`, `on_animate(self)→bool` (false = die),
  `on_hit_target(weapon, target, owner)`
- fx: `on_act(self)→bool` (true = handled this tick), `on_death(self)→bool`
- treasure: `on_eat(self, eater)→bool`
- generator: `customize_spawn(generator, spawn)`

## Entity handles

Hooks receive entity handles. A handle is valid during the dispatch that
produced it — do not stash handles in globals (R6). `==` compares entity
identity. `nil` means "no entity" (e.g. `self:foe()` with no foe).

`og.is_alive(h)`, `og.entity_id(h)`, `og.family_id(order, "core:name")`.

### Methods: walker fields

Getters return integers for integer-typed C++ fields, numbers for floats;
setters narrow exactly as the C++ field type does.

Position/size: `xpos ypos sizex sizey sizez floor set_floor worldx worldy
worldz setxy(x,y) setworldxy(x,y)`.
Movement/facing: `lastx set_lastx lasty set_lasty stepsize set_stepsize
curdir set_curdir facing(x,y)`.
Team/identity: `team_num set_team_num real_team_num set_real_team_num user
family order has_guy`.
Life state: `dead set_dead death_called set_death_called lifetime
set_lifetime summoned set_summoned save_all_protected
set_save_all_protected`.
Timers: `invulnerable_left invisibility_left flight_left charm_left
speed_bonus_left view_all skip_exit shifter_down bonus_rounds weapons_left
keys` (+ `set_*` for each).
Combat: `damage set_damage busy set_busy fire_frequency set_fire_frequency
current_weapon set_current_weapon default_weapon set_default_weapon
do_bounce set_do_bounce` (weapons only).
Animation: `ani_type set_ani_type cycle set_cycle drawcycle set_frame
animate act_type in_act current_special set_current_special set_ignore
lineofsight set_lineofsight`.
References: `foe set_foe leader set_leader owner set_owner collide_ob`.
Actions: `attack(target) fire() special() death() teleport()
teleport_ranged(range) find_teleport_target() turn_undead(range,power)
do_summon(family,lifetime) do_heal_effects(healer,target,amount)
transform_to(order,family) transfer_stats(target) spaces_clear()
is_friendly(other) collide(other) center_on(other) distance_to_ob(other)
distance_to_ob_center(other) clear_myguy() move_myguy_to(target)
set_difficulty(level)`.

### Methods: statistics (`s_` prefix, on the walker)

`s_hitpoints s_max_hitpoints s_magicpoints s_max_magicpoints s_armor
s_magic_per_round s_heal_per_round` (floats, + `s_set_*`);
`s_level s_weapon_cost s_frozen_delay s_current_distance s_last_distance
s_max_heal_delay s_current_heal_delay s_max_magic_delay
s_current_magic_delay` (ints, + `s_set_*`);
`s_special_cost(i) s_set_special_cost(i,v) s_query_bit_flags(flag)
s_set_bit_flags(flag, 0|1) s_add_command(cmd,iter,i1,i2) s_force_command(...)
s_set_command(...) s_clear_command() s_has_commands() s_forward_blocked()
s_name() s_set_name(str) s_controller()`.

### Methods: guy record (`g_` prefix; walker with a guy, or the guy handle in `level_up`)

`g_strength g_dexterity g_constitution g_intelligence g_armor g_level g_exp
g_total_shots g_scen_shots g_total_hits g_scen_hits g_name` (+ `g_set_*`
for the mutable ones), `g_update_derived_stats(walker)`.

## World API (`og.*`)

Spawning: `og.add_ob(order, family[, atstart])`, `og.add_fx_ob`,
`og.add_weap_ob`, `og.summon(self, order, family)` (spawn at summoner,
owner set).

Queries: `og.find_near_foe(self)`, `og.find_nearest_blood(self)`,
`og.foes_in_range(self, range)` → array;
`og.find_foes_in_range(list, range, self)` → array, count — `list` ∈
`"ob"|"weap"|"fx"` selecting the world entity list the C++ call scanned;
same shape for `find_friends_in_range`, `find_in_range`,
`find_foe_weapons_in_range`. `og.oblist()` → every living-list entity in
list order. All arrays preserve the C++ iteration order — iterate with
`for i = 1, #t`.

Terrain: `og.query_passable(x, y, self[, floor])`,
`og.query_grid_passable(...)`, `og.query_object_passable(...)`.

Events: `og.emit_sound(id)`, `og.emit_positional_sound(self, id)`,
`og.emit_notification(text[, duration])` (integers and plain strings only —
never format floats into sim-visible text), `og.emit_event(kind, a, b)`
with kinds in `og.C.EVENT_*` (PLAY_SOUND, NOTIFICATION, SET_PALETTE,
REQUEST_REDRAW, DAMAGE_TILE).

World state: `og.enemy_freeze()`, `og.set_enemy_freeze(v)`, `og.my_team()`,
`og.set_palette(id)` (pair it with an `EVENT_SET_PALETTE` emit like the C++
did). Stats extras: `s_frozen_delay_raw` (unmasked; negatives are the
thaw-immunity phase), `s_old_family`. Guy extras: `g_set_exp`,
`og.exp_from_action(self, target, action, value)` with action ∈ attack,
kill, heal, turn_undead, raise_skeleton, raise_ghost, resurrect,
resurrect_penalty, protection, eat_corpse.

Shared helpers (identical to the C++ family helpers):
`og.soften(raw, knee, ceiling)`, `og.charm_duration(level_diff)`,
`og.freeze_duration(level, con)`, `og.heal_amount(mp, level)` → amount,
cost; `og.scare_duration(level)`, `og.elemental_lifetime(level)`,
`og.image_lifetime(level)`, `og.entity_display_name(self[, fallback])`,
`og.apply_level_up(guy, diff, str, dex, con, intel, armor)`,
`og.apply_difficulty_scaling(self, level, hp, mp, dmg, armor)`,
`og.check_special_ai_distance(self, threshold)`.

World state: `og.level_id()`, `og.level_tick()`, `og.level_done()`,
`og.game_ended()`, `og.remaining_foes(self)`.

`og.log(...)` / `print(...)`: diagnostics (shows under TESTING traces).

## Level scripts

```lua
og.register_level_hooks(level_id, {   -- level_id -1 = every level
  on_load = function(level) ... end,          -- first tick of the level on
                                              -- THIS peer (fresh or
                                              -- mid-join); derive state
                                              -- from the world, idempotent
  on_tick = function(level, tick) ... end,    -- every tick, pre-acts
  on_entity_death = function(ent) ... end,    -- living deaths
  on_entity_spawn = function(ent) ... end,    -- sim-authored living/
                                              -- generator spawns only
})
og.set_entity_hooks(ent, { on_death = function(ent) ... end })
```

Per-entity hooks are registered at runtime (typically from `on_load` or
`on_entity_spawn` after selecting entities via `og.oblist()` / position /
family) and are consumed when they fire. Exact-level registrations shadow
wildcard ones per hook kind. Win/lose logic lives in `on_tick` via the
world-state getters. A campaign ships level scripts inside its embedded
pack (`packs/<id>/scripts/` in the campaign zip) keyed by its level ids.

## Constants (`og.C.*`)

`ORDER_LIVING/WEAPON/TREASURE/GENERATOR/FX`; `COMMAND_*` (WALK, FIRE,
RANDOM_WALK, DIE, FOLLOW, RUSH, MULTIDO, QUICK_FIRE, SET_WEAPON,
RESET_WEAPON, SEARCH, ATTACK, RIGHT_WALK, UNCHARM); `BIT_*` (FLYING,
SWIMMING, ANIMATE, INVINCIBLE, NO_RANGED, IMMORTAL, NO_COLLIDE, PHANTOM,
NAMED, FORESTWALK, MAGICAL, FIRE, ETHEREAL); `ANI_*` (WALK, ATTACK,
TELE_OUT, TELE_IN, SKEL_GROW, SLIME_SPLIT, EXPLODE, GROW, GLOWGROW,
GLOWPULSE, EXPAND_8, DOOR_OPEN, SCARE, BOMB, SPIN); `GRID_SIZE`; `ACT_*` (RANDOM, FIRE,
CONTROL, GUARD, GENERATE, DIE, SIT); `SOUND_*` (BOW, CLANG, DIE1, BLAST,
SPARKLE, TELEPORT, YO, BOLT, HEAL, CHARGE, FWIP, EXPLODE, DIE2, ROAR,
MONEY, EAT); `SHOT_DRAIN_CAP`, `MP_POOL_DAMAGE_CAP`,
`ENEMY_FREEZE_BANK_CAP`, `NUM_SPECIALS`.

## Worked example

`packs/core/scripts/soldier.lua` is the canonical transliteration example —
compare it side-by-side with the original `family_soldier.cpp` callbacks.

## Transliteration checklist

1. Map every C++ float operator to exactly one `og.f*` call; never chain in
   Lua. Comparisons of floats are fine directly.
2. Every integer `/` and `%` → `og.div`/`og.mod`.
3. `(int32)someFloat` → `og.trunc`; explicit narrowing casts → `og.i8/i16/u8`
   only where the C++ did more than a plain setter store (setters already
   narrow).
4. Preserve `og.rand` call order and count exactly. Watch out for C++
   expressions with two `rng.next()` calls in one expression — C++ operand
   order is unspecified; make the order explicit and let parity adjudicate.
5. `for_each_foe_in_range` → `og.foes_in_range`; `world->find_*_in_range`
   → matching `og.find_*` with the correct list selector.
6. `dynamic_cast<living*>` guards → `self:order() ~= og.C.ORDER_LIVING`.
7. Keep every emitted string byte-identical.
8. Run `og_test_parity` after each family; goldens must not change.
