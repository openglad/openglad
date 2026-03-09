# Phase 7: Snapshot Application (Snapshot -> World)

> **See also:** [Phase 5 (WorldSnapshot)](phase-05-world-snapshot.md) | [Phase 6 (Capture)](phase-06-snapshot-capture.md) | [Context](../common/context.md) | [Verification Strategy](../common/verification-strategy.md)

Apply a WorldSnapshot to a GameWorld, replacing its state. This is what clients do when they receive a server snapshot.

## `GameplayContextGuard` RAII

The thread-local `GameplayContext* current_game` (`gameplay_context.h:49`) is dereferenced by ~332 occurrences across 39 gameplay files. `GameWorld::tick()` silently returns if `current_game == nullptr` (no error, no crash — just skips the tick). The `obmap` collision code dereferences `current_game->world->rng_` directly during spatial queries (`obmap.cpp:276`). Entity death callbacks (`walker::death()`) and factory callbacks (`entity_configurator`) also read `current_game`.

**Add a `GameplayContextGuard` RAII type** (~20 lines) that installs and uninstalls `current_game`:

```cpp
// include/openglad/gameplay/gameplay_context.h

class GameplayContextGuard {
public:
    explicit GameplayContextGuard(GameplayContext* ctx) : prev_(current_game) {
        assert(current_game == nullptr || current_game == ctx &&
               "Re-entrant GameplayContext installation — context switch bug");
        current_game = ctx;
    }
    ~GameplayContextGuard() { current_game = prev_; }
    GameplayContextGuard(const GameplayContextGuard&) = delete;
    GameplayContextGuard& operator=(const GameplayContextGuard&) = delete;
private:
    GameplayContext* prev_;
};
```

The debug assert catches context-switch bugs immediately (e.g., server context active when client code runs) rather than as mysterious desync hours later. When server and client run on the same thread (InProcessTransport), `current_game` points to whichever world most recently installed itself — if any code path forgets to swap contexts, this assert fires.

**Mandatory requirement:** `current_game` MUST point to the client's mirror world (with valid `world`, `sim_events`, and `config` pointers) before `apply_snapshot()` runs. Use `GameplayContextGuard` to install the context. If server and client run on separate threads, each thread must install its own `current_game` (it's `thread_local`).

## Bypass Death Path Entirely During Entity Removal

Entity removal during `apply_snapshot()` must NOT trigger death callbacks. The death callback chain is deep and creates serious side effects:

1. `walker::death()` (`walker.cpp:1294-1379`): creates treasure entities (life gems at line 1318), spawns 4 explosion FX entities for generators (line 1356-1358), creates bloodstain entities (line 1390), emits sounds and `EndGame` events, consumes RNG (3 calls per generator explosion iteration: `rng_.next(sizex-8)`, `rng_.next(sizey-8)`, `rng_.next(3)`)
2. `effect::death()` (`effect.cpp:132-146`): delegates to family descriptor `on_death` callbacks — e.g., `bomb_on_death` (`effect_family_bomb.cpp:17-30`) creates explosion entities and emits sounds; `ghost_scare_on_death` has its own side effects
3. `weap::death()` (`weap.cpp:142-159`): delegates to weapon family descriptor `on_death` callbacks — e.g., `projectile_explode_on_death`, `rock_on_death`, `knife_on_death`, `door_on_death` — various entity creation
4. `living::death()` (inherits `walker::death()`): family-specific `on_death` callbacks — e.g., `slime_on_death` (`family_slime.cpp:30-48`) creates smaller slime entities AND transfers `myguy` ownership; `fire_elemental_on_death` creates fire effects

All of these would corrupt the state being applied by creating new entities, consuming RNG, and emitting events.

**Solution:** When removing entities not present in the snapshot, bypass the death path entirely. These entities aren't "dying" in the gameplay sense — they're being replaced by authoritative server state. Implementation:
- Remove from `obmap` via `obmap::remove()` (spatial index cleanup is correct and necessary)
- Remove from `id_index_`
- Erase from the entity list directly (destroying the `unique_ptr` calls `walker::~walker()`)
- `walker::~walker()` (at `src/interface/walker_render_bridge.cpp:97-116`) clears cross-reference pointers, removes from obmap (idempotent if already removed), and resets render/stats — all safe. It does NOT call `death()`.
- The key insight: `~walker()` and `death()` are separate paths. `death()` is called during gameplay when an entity's HP reaches zero. `~walker()` is called when the owning `unique_ptr` is destroyed. During snapshot application, we only trigger `~walker()` (via list erasure), never `death()`.

Additionally, suppress `SimEventLog::push()` during `apply_snapshot()` to prevent stale events from any incidental code paths. Add a `bool suppressed_ = false` flag **on `SimEventLog` itself** (not on `GameWorld`) — `SimEventLog` is where `push()` lives, so that's where the check belongs. Use an RAII guard to manage the flag:

```cpp
// include/openglad/gameplay/sim_event_log.h

class SimEventLogSuppressGuard {
public:
    explicit SimEventLogSuppressGuard(SimEventLog& log) : log_(log) {
        log_.set_suppressed(true);
    }
    ~SimEventLogSuppressGuard() { log_.set_suppressed(false); }
    SimEventLogSuppressGuard(const SimEventLogSuppressGuard&) = delete;
    SimEventLogSuppressGuard& operator=(const SimEventLogSuppressGuard&) = delete;
private:
    SimEventLog& log_;
};
```

`apply_snapshot()` creates a `SimEventLogSuppressGuard` at the top of the function. If `apply_snapshot()` returns early or throws, the destructor clears the flag — no risk of `suppressed_` getting stuck true and silently eating all future events.

## Changes

- Implement `void apply_snapshot(GameWorld&, const WorldSnapshot&)` in `src/gameplay/world_snapshot.cpp`

**Operation sequence (order matters):**
1. Set `applying_snapshot_ = true`
2. Install `GameplayContextGuard` for the target world's context
3. Restore GameWorld-level state fields (game flow flags, timers, scores, `control_hp`, `withdraw_requested`, `withdraw_level`, `guy_id_counter`, `current_palette_id`, `pending_exit_prompt`)
4. Update local `guy` lookup table from snapshot's `guy` data (exp, kills, etc.)
5. **Remove missing entities:** Walk existing entity lists, remove any entity whose `entity_id_` is not in the snapshot. Call `obmap::remove()`, erase from `id_index_`, then erase from list (which destroys the `unique_ptr` -> calls `~walker()`, NOT `death()`).
6. **Update existing entities:** For entities present in both world and snapshot (matched by `entity_id_`), overwrite all snapshot fields. Clear `stats_->commands` (AI command queue) — the client never runs `tick()` so commands are never consumed, but clearing prevents unbounded growth from stale initial-load commands. **Ensure both integer positions (`xpos`/`ypos`) and float positions (`worldx_`/`worldy_`) are written before updating the obmap** — the obmap indexes by `xpos`/`ypos`, so stale integer positions cause incorrect spatial queries. **If order/family changed** (e.g., slime transform), call `entity_configurator()` to update sprite/animation data before overwriting fields — same as `transform_to()` does internally. No subclass change is needed because `transform_to()` only changes family within the same Order in practice. **Note:** `entity_configurator()` loads sprite pixel data from campaign files — this is an I/O operation in the hot path. Since `transform_to()` events are rare (slime splits, wave evolution), the I/O cost is negligible in practice. If profiling shows otherwise, cache the configurator results per (order, family) pair.
7. **Create new entities:** For entity IDs in snapshot but not in world, use factory callbacks: `entity_factory()` -> `entity_configurator()` -> overwrite all snapshot fields. Add to `id_index_`. Insert into obmap via `obmap::add()`.
8. **Resolve cross-references:** Walk all entities, resolve `foe_id`, `leader_id`, `owner_id`, `collide_ob_id`, `controller_id` to pointers via `id_index_` (O(1) per lookup). **IMPORTANT: This step is deliberately deferred until ALL entities (existing + new) are in `id_index_`.** New entities created in step 7 may reference other new entities (e.g., a freshly spawned weapon's `owner_id` pointing to a freshly spawned living entity). If cross-references were resolved during creation (step 7), the target entity might not exist yet. Resolving all cross-references in a single pass after all entities are present guarantees correctness regardless of creation order. Future maintainers must not "optimize" by resolving cross-refs during entity creation.
9. **Update obmap for moved entities:** For entities that existed before and changed position, call `obmap::add()` (the defensive remove-then-add in `obmap::add()` handles this).
10. Reconstruct `ani` pointer from `(order, family, ani_type)` for new/updated entities
11. Reconstruct `myguy` pointer from `guy_id` via local guy lookup table
12. Apply grid dirty tiles from snapshot (overwrite `grid.data[]` entries)
13. **Reorder entity lists to match snapshot order.** Snapshot vectors preserve server list iteration order. After steps 5-7, the client's lists may have entities in a different order (existing entities stay in their original list positions, new entities are appended). Walk each list and reorder to match the snapshot vector's entity_id sequence. This preserves tick-order determinism for future lockstep/prediction work and makes divergence debugging easier. Implementation: build an `entity_id -> list::iterator` map, then `splice()` nodes into a fresh list in snapshot order. O(N) with no reallocation.
14. **Clear `dead_list`.** The client never runs `GameWorld::tick()`, so nothing ever moves entities to `dead_list` on the client side. Entities removed in step 5 are destroyed directly. Clear `dead_list` after each apply to prevent unbounded growth from any stale entries.
15. `SimEventLogSuppressGuard` destructor restores `suppressed_ = false` (RAII — no manual cleanup needed)

## Entity Creation via Factory Callbacks

GameWorld creates entities through three callbacks set up by the platform layer (`game_world.h:148-150`):
- `entity_factory(Order, int32_t family)` — allocates the correct subtype (`living`/`weap`/`treasure`/`effect`)
- `entity_configurator(walker&, Order, int32_t)` — loads sprite data, sets `frames`
- `entity_derived_stats(walker*, Order, int32_t)` — computes derived stats from base stats

`apply_snapshot()` MUST use these callbacks to create new entities (not raw `new walker()`), because:
1. The correct subclass must be instantiated (`weap` for weapons, `living` for NPCs, etc.)
2. `entity_configurator` sets up sprite/animation data that the skip-list fields (`ani`, `frames`) depend on
3. `entity_derived_stats` initializes baseline stats that the snapshot then overwrites

Flow for a new entity in a snapshot: `entity_factory()` -> `entity_configurator()` -> overwrite all snapshot fields -> resolve cross-references. The factory callbacks must be available on the client's GameWorld (they are, since the client has a full GameSession with rendering support).

**Error handling for factory callbacks:** If `entity_factory()` returns nullptr (e.g., unknown order/family combination due to version mismatch), log an error and skip that entity rather than crashing. The client will be missing that entity until the next keyframe. This is a graceful degradation — the game continues with a visual glitch rather than a crash.

## Entity Recycling — The Common Path

Most entities persist across consecutive snapshots. Steps 5-7 above handle three cases: remove (entity gone), update (entity persists), create (entity new). The **update** path (step 6) is the common case and is cheap — just field overwrites on an existing allocation. Only genuinely new entity IDs (never seen in any previous snapshot) trigger factory creation. This naturally avoids the allocation churn concern for projectiles that live across multiple ticks.

## obmap Spatial Index Update Strategy

The obmap uses `std::map<pair<short,short>, list<walker*>>` + `std::unordered_map<walker*, list<pair<short,short>>>` — two-way mapping between grid cells and entities. `obmap::add()` is defensive: if the entity already exists in `walker_to_pos`, it calls `remove()` first (idempotent). This means it's safe to call `add()` for both new and moved entities without tracking position deltas.

Simplified strategy (preferred over manual diffing for robustness):
- **Removed entities:** `obmap::remove()` before erasing from list (step 5)
- **New entities:** `obmap::add()` after field overwrite (step 7)
- **Moved entities:** `obmap::add()` after field overwrite (step 6 — the defensive remove-then-add handles this)

This is O(N) in total entities. For ~200 entities at 12 ticks/sec, this is negligible.

## Entity Allocation Churn

Projectiles spawn and die constantly. At 12 ticks/sec with 4 players + NPCs firing, expect 20-50 entity creates/destroys per second. Modern allocators handle this volume fine. **Deferred optimization:** if profiling (Phase 9 benchmark or later playtesting) shows allocation is a bottleneck, add a simple free list of recycled walker allocations (per-subclass: `weap` pool for projectiles, `effect` pool for FX). The pool would be a `std::vector<std::unique_ptr<walker>>` on GameWorld — pop to reuse, push on "death" instead of deleting. Do NOT build this upfront.

## `dead_list` and obmap Invariant

Skipping `dead_list` in snapshots is safe because dead entities are removed from the obmap during their `death()` callback chain (before being moved to `dead_list` in `GameWorld::tick()`). Dead entities in `dead_list` have no spatial presence and don't affect collision queries. The snapshot captures only live entities from `oblist`/`fxlist`/`weaplist`. Dead player entities (`myguy != nullptr`) are the exception — they stay in `oblist` (not moved to `dead_list`), so they ARE captured.

**Verify:** Round-trip test — capture snapshot, clear world, apply snapshot, capture again, assert identical. Test across entity death/spawn events. Verify obmap is consistent after apply (spatial queries return correct results). Verify `do_bounce` round-trips correctly for weapon entities. Verify `regen_delay_` and `path_check_counter` round-trip correctly. Verify no death callbacks fire during entity removal (no new entities created, no events emitted, no RNG consumed). Verify grid dirty tiles round-trip correctly. Verify `transform_to` scenario: capture a snapshot where a slime has transformed to FAMILY_SMALL_SLIME, apply to a world that still has FAMILY_SLIME, verify family updated and sprite reconfigured.
