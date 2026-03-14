# Phase 8: Dirty Tracking via Setter Refactor

> **See also:** [Phase 2 (dirty tracking infrastructure)](phase-02-cross-reference-ids.md) | [Phase 5 (WorldSnapshot)](phase-05-world-snapshot.md) | [Phase 9 (serialization)](phase-09-serialization-delta.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Convert the 86 serializable fields to use setter methods that automatically call `mark_dirty()`. This replaces manual `mark_dirty()` instrumentation with compile-time enforcement — making fields private means the compiler finds every mutation site, eliminating the risk of missed `mark_dirty()` calls.

## Approach

For each of the 86 serializable fields across SimEntity, walker, and statistics:

1. Make the field private (or protected if accessed by subclasses)
2. Add an inline getter: `type field() const { return field_; }`
3. Add an inline setter: `void set_field(type v) { field_ = v; mark_dirty(DIRTY_BIT_FIELD); }`
4. Fix all compilation errors — each error is a mutation site that now automatically calls `mark_dirty()`

The compiler finds every mutation site. No runtime safety net needed to catch missed instrumentation — though a belt-and-suspenders CI test is included (see below).

## Field Groups

The 86 fields break down into natural batches for incremental commits:

- **SimEntity fields** (19 fields, ~50-100 mutation sites): `entity_id_`, `xpos`, `ypos`, `sizex`, `sizey`, `team_num`, `real_team_num`, `user`, `dead`, `death_called`, `invulnerable_left`, `invisibility_left`, `flight_left`, `bonus_rounds`, `order`, `family`, `frame`, `worldx_`, `worldy_`
- **Walker movement/state fields** (20 fields, ~100-150 sites): `lastx`, `lasty`, `stepsize`, `normal_stepsize`, `curdir`, `enddir`, `action`, `act_type`, `old_act_type`, `ani_type`, `cycle`, `drawcycle`, `current_special`, `ignore`, `in_act`, `shifter_down`, `yo_delay`, `skip_exit`, `outline`, `hurt_flash`
- **Walker combat fields** (10 fields, ~50-80 sites): `damage`, `fire_frequency`, `busy`, `current_weapon`, `default_weapon`, `attack_lunge`, `attack_lunge_angle`, `hit_recoil`, `hit_recoil_angle`, `last_hitpoints`
- **Walker timer/identity fields** (13 fields, ~30-50 sites): `lifetime`, `speed_bonus`, `speed_bonus_left`, `charm_left`, `weapons_left`, `keys`, `view_all`, `lineofsight`, `path_check_counter`, `foe_id`, `leader_id`, `owner_id`, `collide_ob_id`, `regen_delay_`
- **Statistics fields** (22 fields, ~50-100 sites): `hitpoints`, `max_hitpoints`, `magicpoints`, `max_magicpoints`, `max_heal_delay`, `current_heal_delay`, `max_magic_delay`, `current_magic_delay`, `magic_per_round`, `heal_per_round`, `armor`, `level`, `bit_flags`, `delete_me`, `frozen_delay`, `weapon_cost`, `special_cost[6]`, `old_order`, `old_family`, `last_distance`, `current_distance`, `controller_id`
- **Weap field** (1 field, ~5 sites): `do_bounce`

## Existing Setters

Several fields already go through setter-like methods — these only need a `mark_dirty()` call added:

- **Position:** `setxy()` / `set_world_pos()` — add `mark_dirty()` for `xpos`, `ypos`, `worldx_`, `worldy_`
- **Cross-references:** `set_foe()`, `set_leader()`, `set_owner()`, `set_collide_ob()`, `set_controller()` — already call `mark_dirty()` (Phase 2)
- **Frame:** `set_frame()` — add `mark_dirty(DIRTY_BIT_FRAME)`
- **Order/family:** `set_order_family()` — add `mark_dirty()` for both fields

Fields accessed exclusively through existing setters don't need new getter/setter pairs — just add the `mark_dirty()` call to the existing method.

## Compound Assignment Patterns

For high-churn fields like `hitpoints` where `stats->hitpoints -= damage` is common:

```cpp
// Before:
stats->hitpoints -= damage;

// After:
stats->set_hitpoints(stats->hitpoints() - damage);
```

For the highest-churn fields, convenience methods can reduce verbosity:

```cpp
void adjust_hitpoints(float delta) { set_hitpoints(hitpoints_ + delta); }
```

Use judgment — only add convenience methods where a compound pattern appears 10+ times.

## Performance

Setters are inline and compile to the same code as manual `mark_dirty()` — a single OR instruction alongside the field write. Zero overhead vs. the manual instrumentation approach.

## Snapshot Capture Compatibility

`capture_snapshot()` (Phase 6) reads fields to build the EntitySnapshot. It accesses private fields via:
- Using the public getters (preferred — maintains encapsulation)
- Or friend declarations where getter overhead is undesirable (shouldn't be necessary since getters are inline)

The `EntitySnapshot` struct itself keeps plain public fields — it's a data transfer object, not a game entity.

## Commit Strategy

Do the refactor in batches by field group, each independently compilable:

1. SimEntity fields (19 fields)
2. Walker movement/state fields (20 fields)
3. Walker combat fields (10 fields)
4. Walker timer/identity fields (13 fields)
5. Statistics fields (22 fields)
6. Weap field (1 field)

Each commit privatizes one group, adds getters/setters, and fixes all resulting compiler errors. The build must pass after each commit.

## CI Safety-Net Test

Even with compile-time enforcement via setters, retain a comparison-based validation test as belt-and-suspenders. This catches bugs like a setter that writes the field but forgets to call `mark_dirty()`, or a field that's mutated through a stale reference obtained before privatization.

```cpp
// tests/test_dirty_tracking_safety.cpp
// (add to ALL_INTEGRATION_TEST_SOURCES and assign to an og_add_test_group() in CMakeLists.txt)
// For each tick in a combat scenario:
// 1. Capture snapshot using dirty bits (the real path)
// 2. Capture a second "reference" snapshot by brute-force comparing all fields
//    against the previous tick's full snapshot
// 3. Assert the dirty-bit snapshot's mask is a SUPERSET of the reference mask
//    (extra dirty bits are OK — conservative. Missing bits = bug.)
```

This test runs a worst-case 4-player combat scenario (many entity spawns, deaths, projectiles, explosions, AI actions, speed changes) for 200+ ticks and validates every single entity's dirty mask against the brute-force reference. If any setter forgot its `mark_dirty()` call, or if a field was mutated through a path that bypassed the setter, this test catches it.

**This test is a hard gate for Phase 8 completion.** It must pass before any networking code uses dirty-based deltas.

## Changes

- Make 86 serializable fields private across SimEntity, walker, statistics, weap
- Add ~86 inline getter/setter pairs (in headers)
- Add `mark_dirty()` calls to existing setter-like methods (`setxy`, `set_frame`, `set_order_family`)
- Fix ~200-400 compilation errors across `src/gameplay/`, `src/interface/`, `include/openglad/gameplay/`, and `tests/` (mechanical, guided by compiler)
- Add CI safety-net test (`tests/test_dirty_tracking_safety.cpp`)

**Verify:** Build passes (compilation itself validates all mutation sites go through setters). All existing tests pass. CI safety-net test passes on worst-case combat scenario.
