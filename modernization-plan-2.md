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

## Phase 1: Introduce “Context” Objects (No Behavioral Change)

1. Add `GameContext` (or `AppContext`) containing:
   - `screen*` (or `screen&`)
   - `options*`
   - `cfg` access
   - `InputState` snapshot (see Phase 2)
   - `IRandom` (see Phase 3)
2. Convert internal subsystems to accept a `GameContext&` parameter instead of reaching for globals.
3. Keep globals temporarily as thin wrappers so churn is localized:
   - `GameContext& ctx()` returns the “current global context”
   - existing code paths call into new overloads using `ctx()`

Deliverable: no tests changed, compilation still works for all targets.

## Phase 2: Split Input Into Snapshot + Mapping

1. Create `InputState` as a plain struct that represents “what the player is doing this frame”:
   - per-player movement direction
   - fire/special/yell/etc pressed/held
   - mouse state (if needed)
2. Move SDL event processing into `InputBackendSDL` which updates an `InputState`.
3. Make simulation consume only `InputState` (not SDL events).

Deliverable: unit tests can feed synthetic input without SDL threads.

## Phase 3: Deterministic RNG And Time

1. Introduce `IRandom` (or `RandomStream`) interface:
   - `Uint32 next_u32(Uint32 max_exclusive)`
2. Provide:
   - production RNG wrapper over current `random(...)`
   - test RNG with a fixed sequence/seed
3. Introduce `ITimeSource` for `ticks()` / `dt`.

Deliverable: combat/AI behaviors become reproducible in tests.

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

## Phase 5: Clarify Ownership + Lifetimes

1. Replace remaining “raw owning pointers” in core state with:
   - `std::unique_ptr` for ownership
   - `std::span` for views
2. Introduce stable IDs (`EntityId`) to refer to entities rather than raw pointers where possible.
3. Restrict mutation to simulation code; UI reads state through accessors.

Deliverable: fewer use-after-free risks and simpler tests.

## Phase 6: Testing Strategy For The New Architecture

- Keep current integration tests:
  - picker menu flows
  - full game loop “fairy death / overpowered team”
- Add fast unit tests for:
  - combat math and damage application
  - AI command selection and movement decisions
  - save/load serialization of `GameState` (pure encode/decode)
  - input mapping (SDL events -> `InputState`)

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

