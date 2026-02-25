# Phase 4: Introduce GameplayContext and current_game

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 3](phase-03.md)
**Followed by:** [Phase 5](phase-05.md)
**Key types:** [GameplayContext](key-types.md#gameplaycontext-new--gameplay-component), [GameWorld](key-types.md#gameworld-new--gameplay-component)

---

## Goal

Entity and sim code accesses shared state through a single
`thread_local GameplayContext* current_game` instead of per-entity injected
pointers. This phase must run before Phase 5 (SaveData decoupling) because
Phase 5 needs `current_game->world->` to replace `sim_save->` references —
without `current_game`, entities have no path to reach GameWorld fields.

This phase only needs Phases 1–3 (GameWorld with entity lists, tick logic, and
game state flags). It does NOT need gloader moved or LevelData killed.

## `tick()` Signature Evolution

Phase 2 absorbed `legacy simulation layer` into `GameWorld`.
`GameWorld::tick()` currently takes `SimEventLog&` as an argument. This phase
completes the evolution: `tick()` takes no args and reads `sim_events` from
`current_game`.

## Steps

1. Create `include/openglad/gameplay/gameplay_context.h` with the struct and
   thread_local declaration
2. Define `current_game` in `src/gameplay/gameplay_context.cpp`
3. Platform code (GameSession) creates GameplayContext and sets `current_game`
4. `GameWorld::tick()` drops all args — reads from `current_game` instead
5. Migrate entity code from `sim_*` pointers to `current_game->` access:
   - `sim_level->query_passable(...)` → `current_game->world->query_passable(...)`
   - `sim_rng->next(n)` → `current_game->world->rng_.next(n)`
   - `sim_events` → `current_game->sim_events`
   - `sim_enemy_freeze` → `current_game->world->enemy_freeze` (direct field)
   - `stats.cpp:38` — `ctx().rng->next()` → `current_game->world->rng_.next()`
     (stat calculations during gameplay)
6. Remove `sim_level`, `sim_rng`, `sim_events`,
   `sim_enemy_freeze` fields from `SimEntity`
7. Remove entity wiring from **both** sites that set `sim_*` pointers:
   - `LevelData::wire_entity()` (`level_data.cpp:517-526`) — sets `myobmap`,
     `sim_level`, and copies of `sim_ctx_*` stored on LevelData
   - `sdl_level_data_wire_entity_from_screen()` (`sdl_context_services.cpp:79-89`)
     — **overwrites** `sim_save`, `sim_enemy_freeze`, `sim_events`, `sim_rng`,
     `sim_config` with values from `current_session` and `ctx()`
   Both sites must be eliminated — removing only one leaves stale pointers
8. Migrate `walker::myobmap` (walker.h line 196) — a raw `obmap*` set by
   `wire_entity()` (`level_data.cpp:519`). Replace with
   `current_game->world->myobmap.get()` access and remove from walker

## CMake Dependency Shift

Currently `og_entities` has
`target_include_directories(PRIVATE third_party/micropather)` for the
pathfinding scratch state (`walker_pathing.cpp`). When pathfinding state moves
to `GameplayContext` in this phase, the micropather include dependency shifts
from `og_entities` to the gameplay component target. Update `CMakeLists.txt`
accordingly.

## Remaining `sim_*` Pointers

This phase migrates four of the six `sim_*` pointers. The remaining
two (`sim_save`, `sim_config`) are left on `SimEntity` until Phase 5, which
removes them as part of the SaveData decoupling.

## Threading Note

There is a `std::atomic<GameSession*> primary_session`
(`game_session.cpp:37`) with an `ensure_thread_session()` helper
(`game_session.h:167-171`). Child threads (test injectors, potentially demo
rendering) use this to inherit session context when their `thread_local
current_session` is null. If `current_game` becomes the gameplay entry point,
child threads that need gameplay access will need an analogous
`ensure_thread_game()` pattern, or the threading model needs to be documented as
single-threaded-only for gameplay.

## Subsumes Desingletonize Phase 1

The existing `ctx()` global accessor and
`set_global_context()` are retired. Code that used `ctx().rng` uses
`current_game->world->rng_`. Code that used `ctx().sim_events` uses
`current_game->sim_events`.

## RNG Behavioral Change

Currently, `legacy simulation layer` owns a deterministic
`SimRandom rng_` (LCG), but entity code uses `sim_rng` which is overwritten by
site 2 (`sdl_context_services.cpp:79-89`) to point at `ctx().rng` — a
`ProductionRandom` that wraps a non-deterministic global `random()` function.
After migration, entities use `current_game->world->rng_` (SimRandom). This
switches entity code from non-deterministic to deterministic RNG. This is
intentional (enables replay-safe gameplay) but is a behavioral change that
could affect game feel and should be regression-tested.

## `ctx().rng` Call Sites NOT Migrated to `current_game`

The following
`ctx().rng` usages are for visual effects or editor tooling, not gameplay
simulation. They are not migrated to `current_game->world->rng_`:
- `score_panel.cpp:31`, `video.cpp:36`, `radar.cpp:30`, `glad.cpp:64`
  (render-layer visual randomness)
- `smooth.cpp:25` (editor-only terrain smoothing — `smooth()` is never called
  during gameplay ticks, only in the level editor; `query_genre_x_y()` which
  entities call does not use RNG)

After `ctx()` retirement (Phase 12), these switch to
`current_session->ctx_.rng` or a dedicated render/editor-layer RNG.

**Note:** `view.cpp` uses `ctx().input` (line 410) and `ctx().sim_events`
(line 509), not `ctx().rng`. Those usages are addressed separately when `ctx()`
is retired in Phase 12.

## Risk

High churn — touches most entity/sim files. But fully mechanical
(search-replace per pointer).

## Testing

Full ctest. Headless unit tests verify thread_local works.
