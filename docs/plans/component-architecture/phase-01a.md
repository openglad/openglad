# Phase 1a: Move entity lists to GameWorld

**Part of:** [Component Architecture Plan](README.md)
**Followed by:** [Phase 1b](phase-01b.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component)

---

## Context: Phases 1a–3

**Goal:** Create `GameWorld` with entity lists, spatial data, tick logic (from
`legacy simulation layer`), RNG, and game state flags. `screen` holds both `GameWorld` and
`LevelData` temporarily during the transition.

These four phases (1a, 1b, 2, 3) keep each change independently testable and
bisectable.

**What moves into GameWorld (across Phases 1a–3):**
- Entity lists (`oblist`, `weaplist`, `fxlist`, `dead_list`) from `LevelData`
- `living_count` (renamed from `numobs`) from `LevelData`
- Spatial data (`myobmap`, `grid`, `mysmoother`, `pixmaxx/pixmaxy`) from `LevelData`
- Level metadata (`id`, `title`, `par_value`, `time_bonus_limit`, `type`) from `LevelData`
- Tick logic, tick counters, and RNG from `legacy simulation layer` (absorbing `legacy simulation layer` entirely)
- Game state flags (`level_done`, `enemy_freeze`, `end`, etc.) from `screen`
- `tick()` writes `level_done`, `game_ended`, `next_level`, and `ending` directly on GameWorld instead of returning `TickResult`

**What stays on `LevelData` temporarily:**
- `myloader` (entity factory) — moves to resources in Phase 6
- `renderer_`, `pixdata[]`, `topx/topy` — moves to `LevelVisuals` in Phase 7
- Sim context wiring fields (`sim_ctx_*`) — transitional (eliminated by Phase 4)

**Forwarding accessor strategy:** Phases 1a–3 create forwarding accessors on
`LevelData` and `screen` to minimize churn. These are temporary scaffolding —
each phase should migrate the callers it touches to use the new canonical path
(`world().*`) and remove the corresponding forwarding accessor once no callers
remain. Do not accumulate stale forwarders across phases. By Phase 7 (LevelData
deletion), all LevelData forwarding accessors must be gone. By Phase 10
(screen refactoring), all screen forwarding accessors must be gone.

---

## Phase 1a Goal

Create the GameWorld class. Move entity lists and `living_count` from
`LevelData` into `GameWorld`. Also move `delete_objects()` (list clearing
portion) and `remaining_foes()` (operates on oblist). `screen` gets a
`GameWorld world_;` member. `LevelData` keeps forwarding accessors temporarily.

## Steps

1. Create `include/openglad/gameplay/game_world.h` with GameWorld class (entity
   lists, `living_count`)
2. Create `src/gameplay/game_world.cpp`
3. Add `GameWorld world_;` member to `screen`
4. Move entity lists (`oblist`, `weaplist`, `fxlist`, `dead_list`) and
   `living_count` (renamed from `numobs`) from `LevelData` into `GameWorld`
5. Move `delete_objects()` (list clearing portion) and `remaining_foes()` free
   function to GameWorld
6. Provide forwarding accessors on both `screen` and `LevelData` to minimize
   churn (callers migrate incrementally)
7. `GameWorld::add_ob()` delegates to `LevelData::myloader` temporarily

## `add_ob()` Callsite Scope

`add_ob()` is called from ~34 locations across
entity code, loader code, and level editor code. Entity code goes through
`sim_level->add_ob()` which will forward to `world_->add_ob()` via LevelData
forwarding during the transition. This is a high-touch migration point.

## Entity Wiring During Transition

Entity creation flows
`GameWorld::add_ob()` → `LevelData::myloader->create_walker_owned()` ->
`LevelData::wire_entity()` (sets `sim_*` pointers via `LevelDataHooks`).
This circular delegation (`GameWorld` → `LevelData` → hooks → back to screen
state) is ugly but temporary — Phase 4 eliminates `sim_*` pointer wiring,
and Phase 6 replaces the loader path entirely.

## Files Changed (major)

- New: `include/openglad/gameplay/game_world.h`, `src/gameplay/game_world.cpp`
- Modified: `include/openglad/data/level_data.h` (fields removed, forwarding added)
- Modified: `include/openglad/runtime/screen.h` (new `world_` member)
- Modified: All files that reference `level_data.oblist` etc. (high churn but mechanical)

## Screen Lifecycle Methods

- `screen::ready_for_battle()` — display-only (views, UI flags, timer). Not
  affected by GameWorld extraction.
- `screen::reset()` — calls `save_data.reset()` and `level_data.clear()`.
  After Phase 1a, the `level_data.clear()` call must also clear `world_`
  state (or be replaced by a `world_.clear()` + `level_data.clear()` pair).
- `screen::endgame()` — mixed concerns: shows results_screen (display),
  updates save_data scores (persistence), calls save_data.save() (I/O).
  Stays on screen during Phases 1–3 but needs splitting in Phase 10 when
  screen moves to interface.

## Risk

High — `LevelData` is referenced widely. But keeping `LevelData`
alive as a shell with forwarding accessors makes this incremental.

## Testing

Full ctest. Create a `TestGameWorld` RAII helper early in this phase
(creates a minimal GameWorld + SimEventLog for use in unit tests). In Phase 4,
this helper is extended into `TestGameplayContext` which also installs the
`current_game` thread-local.
