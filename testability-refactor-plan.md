# Testability Refactor Plan (Minimal Churn)

Goal: make the codebase significantly easier to test (especially game logic) with the smallest practical refactors, while keeping existing end-to-end game-loop tests intact and fast.

This repo already has a good foundation for “real game” tests:

- `tests/test_main.cpp` boots SDL headless (`offscreen` video, `dummy` audio), builds a real `screen`, and runs UI-driven tests that exercise `picker_main()` and the full `glad_main()` loop.
- Under `TESTING`, `src/glad.cpp` skips `redraw()/refresh()` inside the frame loop, which keeps CI runtime reasonable.
- `TRACE(...)` (`src/test_trace.h`) provides a lightweight, assertable signal for key behaviors without deep mocking.

The main remaining blocker to targeted testing is that lots of “core logic” is still embedded inside SDL/event/render loops or mixed with side effects (randomness, sound/effects spawning, file IO). The plan below focuses on extracting a few high-leverage seams while preserving the current integration-test style.

## Current Structure (What Matters For Tests)

- Entry / game loop:
  - Native `main()` in `src/glad.cpp` constructs global `myscreen` and then runs picker/game.
  - The per-frame loop lives in `src/glad.cpp` as `static void game_frame()` with a file-static `FrameState g_frame_state`.
  - Event polling is hard-wired to `SDL_PollEvent` inside `game_frame()`.
- Global state:
  - `screen* myscreen` is global (declared in `src/glad.cpp`, assigned in `screen::screen()`).
  - Many subsystems assume `myscreen` exists (effects spawning, score, scenario load, etc.).
- Combat/math logic:
  - A lot of deterministic logic exists but is difficult to unit-test because it’s buried in large methods (ex: `walker::attack()` and helpers in `src/walker.cpp`).
  - Some helpers are already “almost pure” (ex: `get_damage_reduction()`), but they are not exposed as a test target and still depend on global/random in places.

## Refactor 1: Make The Game Loop Hookable (But Keep Existing Flow)

### Motivation
We want tests that can:

- Step the game loop deterministically (N frames) without having to spawn an SDL thread just to push input.
- Run “logic-only” frames with controlled events/time, while preserving at least one set of tests that runs the real SDL polling path.

### Smallest Change That Pays Off
Extract the frame loop into a small reusable unit that accepts function hooks for:

- event polling (`poll_event`)
- event handling (`handle_event`)
- time delay/cap (optional, but helps avoid calling `SDL_Delay` in tests)

### Proposed API (new files)
Add `src/game_loop.h` / `src/game_loop.cpp`:

- `struct GameLoopFrameState` (equivalent to current `FrameState`, but not file-static)
- `struct GameLoopDeps`:
  - `int (*poll_event)(SDL_Event*)` (defaults to `SDL_PollEvent`)
  - `void (*handle_event)(const SDL_Event&)` (defaults to `handle_events`)
  - `Uint32 (*get_ticks)()` / `void (*delay_ms)(Uint32)` (defaults to SDL variants) as needed
- `bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps);`
  - returns `true` when done (end of mission / quit)

`src/glad.cpp` keeps:

- the global `myscreen`
- `glad_init()` and `glad_main()` entrypoints

but they delegate per-frame work to `game_frame(...)`.

### Expected Churn
Low. The actual frame logic stays the same; only moved behind a callable seam and parameterized for tests.

### Tests Enabled
- New unit tests can run “N frames” with:
  - a fake poller that replays a deterministic event script
  - a fake delay/time source to make tests fast and stable
- Existing UI/game-loop tests remain, still using real SDL events and `picker_main()`.

## Refactor 2: Extract Combat Calculations Into Pure Functions With Injectable RNG

### Motivation
Combat/XP/regeneration logic is where we want lots of coverage, but it’s currently hard to test because:

- the math is mixed into `walker` methods that spawn FX, touch globals, and depend on `random()`
- tests that reach this logic via the full UI/game loop are slow and coarse

### Smallest Change That Pays Off
Extract the “mathy” parts into a pure module with explicit inputs, and adapt `walker.cpp` to call it.

Concrete first targets (currently in `src/walker.cpp`):

- `get_base_damage(walker* w)` (depends on `random(...)`)
- `get_damage_reduction(walker* attacker, float damage, walker* target)` (essentially pure)

### Proposed API (new files)
Add `src/combat_math.h` / `src/combat_math.cpp`:

- `using RandomU32 = Uint32(*)(Uint32);`
- `float compute_base_damage(float base_damage, RandomU32 rng);`
- `float compute_damage_reduction(float incoming_damage, float target_armor);`
- (optionally) `float compute_post_reduction_damage(float incoming_damage, float target_armor);`

`walker.cpp` becomes:

- a thin adapter: pull `w->damage`, `target->stats()->armor`, and pass them into `combat_math` functions
- use `random` as the default RNG (so gameplay stays identical)

### Expected Churn
Very low and localized to `src/walker.cpp` + new module.

### Tests Enabled
Fast unit tests in `tests/` that cover:

- armor reduction clamping (“always do at least 1 damage” behavior)
- base-damage distribution behavior with a deterministic RNG

This is high coverage-per-line because the combat logic is executed constantly in real gameplay.

## Refactor 3: Add A Tiny “Headless Mode” For Screen Construction (Optional)

### Motivation
Many tests need `screen` only to reach logic; the constructor currently draws a loading UI and pushes to the SDL surface.

This is not incorrect, but it is unnecessary work for unit tests and can make headless behavior more fragile across SDL drivers.

### Smallest Change That Pays Off
Add an optional constructor parameter (or a config struct) that disables the loading-screen drawing and `buffer_to_screen()` calls.

Example:

- `enum class ScreenInitMode { Full, Headless };`
- `screen(short howmany, ScreenInitMode mode = ScreenInitMode::Full);`

Under `Headless`:

- skip the “Loading Gladiator…” drawing
- still load palette/assets needed for logic (keep behavior minimal)

### Expected Churn
Moderate but contained (`src/screen.h`, `src/screen.cpp`, and `tests/test_main.cpp` to opt into headless mode).

### Tests Enabled
Faster and less SDL-driver-sensitive tests; makes it more realistic to add many logic-level unit tests.

This item is explicitly optional; we should only do it if the first two refactors aren’t enough to comfortably expand coverage.

## Sequencing (Small Steps, Preserve Existing Coverage)

1. ✅ Implement Refactor 1 (hookable frame loop) — DONE (commit 6b6713c)
2. ✅ Implement Refactor 2 (combat math extraction) — DONE (commit 6b6713c)
3. ⬜ Refactor 3 (headless screen init) — optional, not yet needed.

## Additional Completed Work (Beyond Original Plan)

- Extracted pure functions: orbit_offset, compute_explosion_range,
  find_next_control, compute_hp_color, compute_mp_color, compute_outline
- Added TRACE instrumentation throughout effect, walker, view modules
- Added #ifdef TESTING guards for blocking input/animation functions
- Added test filter support (pass substring arg to openglad_test)
- Introduced GameContext with IRandom, InputState (modernization-plan-2)
- Converted all core simulation random() calls to injectable RNG
- Extracted freeze/heal/charm duration calculations into combat_math
- Extracted XP polynomial and all ExpAction types into combat_math
- Extracted HP/MP regen tick logic into pure functions
- Added comprehensive tests for calculate_exp/calculate_level and stat bonuses

## Guardrails

- No removal of existing integration tests. They are valuable “black box” coverage and must remain.
- Keep refactors as adapters around existing code; avoid migrating subsystems away from `myscreen` yet.
- Prefer additive seams (new modules + optional dependency injection) over invasive rewrites.

