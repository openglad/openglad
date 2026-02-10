# Modernization Plan 2 (Major Refactor): Make Core Game Logic Deterministic And Testable

This plan describes a larger, design-level refactor to modernize OpenGlad’s structure after the current “minimal churn” testability work (hookable game loop, extracted combat math, lcov/gcov wiring, and incremental tests).

The goal here is not to chase “modern C++ for its own sake”, but to:

- make logic testable without SDL/event/render loops
- make simulation deterministic (so failures reproduce)
- isolate global state and side effects
- reduce coupling between UI, renderer, IO, and simulation

## Success Criteria

- A new `core` library target that contains the simulation and can run in a headless test runner with no SDL window/audio.
- A deterministic “tick” interface: given `(previous_state, input, dt, rng_seed)` produce `(next_state, emitted_events)`.
- Existing end-to-end UI/game-loop tests remain and still exercise the real picker/game flow.
- The majority of unit tests run without spinning SDL threads or needing assets on disk.
- Coverage is tracked for the `core` library independently of renderer/editor code.

## Non-Goals (For This Plan)

- Rewriting the entire asset pipeline or replacing SDL.
- Making the scenario editor perfectly testable; editor/UI can remain integration-tested.
- Changing gameplay balance.

## Current Pain Points

- Global state (`myscreen`, `theprefs`, global input state) makes most logic hard to isolate.
- Large methods (especially in `walker`, `screen`, `picker`) mix pure math with side effects (FX, sounds, IO).
- `#ifdef TESTING` blocks remove rendering paths from the test binary, which makes “whole-program” coverage hard to interpret.

## Phase 1: Introduce "Context" Objects (No Behavioral Change) — DONE

1. ✅ Added `GameContext` struct in `src/game_context.h` containing:
   - `screen*` (game_screen)
   - `options*` (prefs)
   - `cfg_store*` (config)
   - `InputState` snapshot
   - `IRandom*` (rng)
2. ✅ `ctx()` global accessor lazily populates from existing globals.
3. ✅ `set_global_context()` allows tests to inject custom contexts.
4. ✅ Combat math wired through `ctx().rng` (walker.cpp `get_base_damage()`).

Deliverable: compilation works for all targets, 96 tests pass.

## Phase 2: Split Input Into Snapshot + Mapping — DONE

1. ✅ Created `InputState`/`PlayerInput` structs with:
   - `held[16]` and `pressed[16]` arrays per player (edge-detected)
   - `InputKey` enum class for type-safe key indexing
   - `move_x()`/`move_y()` helpers for direction computation
2. ✅ `input_state_from_sdl()` populates InputState from SDL keyboard/joystick state.
3. ✅ Called each frame in `game_loop.cpp` before `continuous_input()`.
4. Simulation still reads SDL state directly — gradual migration ongoing.

Deliverable: InputState snapshot populated each frame; tests can construct synthetic InputState.

## Phase 3: Deterministic RNG And Time — DONE (RNG part)

1. ✅ `IRandom` interface with `next(Uint32 max_exclusive)`:
   - `ProductionRandom`: wraps existing `random()` function
   - `FixedRandom`: returns a fixed value (for predictable tests)
   - `SeededRandom`: LCG-based reproducible sequences
2. ✅ All core simulation files converted to use `ctx().rng->next()`:
   - walker.cpp (~60 calls), living.cpp (13), effect.cpp (5), screen.cpp (3),
     stats.cpp (9), smooth.cpp (23), radar.cpp (8), video.cpp (5),
     treasure.cpp (1), obmap.cpp (1), glad.cpp (1)
   - Only level_editor.cpp retains direct `random()` (not core simulation)
3. ⬜ `ITimeSource` for `ticks()` / `dt` — deferred (lower priority).

Deliverable: combat/AI behaviors reproducible in tests with SeededRandom.

## Phase 4: Extract A `World` / `Simulation` Layer

1. Define a “core simulation” object (name suggestions):
   - `World`
   - `Simulation`
   - `GameState`
2. Responsibilities:
   - owns walkers/entities and their stats
   - owns level runtime state (obmap, triggers, etc.)
   - updates via `tick(InputState, dt)`
3. Render/UI becomes a consumer of:
   - read-only views of `GameState`
   - emitted “events” (damage numbers, sounds, popups), not direct side effects

Deliverable: `game_frame` becomes a thin adapter:

- poll events -> update `InputState`
- call `Simulation::tick`
- render from `GameState`

## Phase 5: Clarify Ownership + Lifetimes — PARTIALLY DONE

1. ✅ Smart pointer adoption completed in MODERNIZATION_PLAN.md Phase 4:
   - `unique_ptr` for walker lists, viewscreen, loader, obmap, pixie arrays
   - `std::span` for buffer parameters
2. ⬜ Introduce stable IDs (`EntityId`) to refer to entities rather than raw pointers.
3. ⬜ Restrict mutation to simulation code; UI reads state through accessors.

Deliverable: fewer use-after-free risks and simpler tests.

## Phase 6: Testing Strategy For The New Architecture — IN PROGRESS

- ✅ Keep current integration tests (96 tests total):
  - picker menu flows (hire, save/load, view team, options, etc.)
  - full game loop "fairy death / overpowered team"
- ✅ Fast unit tests added for:
  - combat math and damage application (with injectable RNG)
  - orbit_offset, compute_explosion_range, compute_hp/mp_color
  - GameContext, IRandom (ProductionRandom, FixedRandom, SeededRandom)
  - InputState snapshot and PlayerInput direction helpers
  - Deterministic RNG end-to-end via GameContext injection
- ⬜ Still needed:
  - AI command selection with deterministic RNG
  - save/load serialization of `GameState` (pure encode/decode)
  - input mapping (SDL events -> `InputState`) with synthetic events

Coverage:

- Track coverage for `core` library separately.
- Maintain an optional “whole-program” report, but accept that renderer/editor code may remain lower coverage.

## Migration Plan (How To Land This Safely)

1. Start by introducing interfaces (`GameContext`, `InputState`, `IRandom`) and wiring them without changing behavior.
2. Move one subsystem at a time into `core` with adapters:
   - combat application
   - regen/armor/XP rules
   - minimal AI decisions
3. Gate large moves behind “dual path” compilation:
   - old code remains callable until new path is validated by tests.
4. Only after the new `Simulation::tick` path is stable, delete old glue and remove `#ifdef TESTING` compile-time render skips in favor of runtime toggles.

## Risks

- The codebase is tightly coupled to `screen`; unthreading this requires discipline and incremental steps.
- Determinism changes can expose “hidden dependence” on current RNG/time; treat as bugs to fix, not as regressions.
- Some UI flows might assume side effects from simulation; expect adapter work.

