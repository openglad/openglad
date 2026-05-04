# Phase 3 — Switch the desktop loop to one-tick-per-frame deadline pacing

## Phase Name
`desktop-loop-rewrite`

## Implement Phase ID
`phase_3_desktop_loop_rewrite`

## Preexisting Inputs
- `.plan/plan.md` — full implementation plan.
- `src/platform/sdl/game_loop.cpp:55` (`kMaxTicksPerCall = 4`), `:72` (`compute_tick_schedule`), `:123-184` (`ticks_to_run_this_call`).
- `src/platform/sdl/game_loop.cpp:307` (`game_frame_with_result`).
- `src/platform/sdl/glad_gameplay.cpp:227-257` (`glad_main` and the bare `while` loop).
- `include/openglad/platform/game_loop.h` (`GameLoopDeps` shape lines 24-43; existing fields stay).
- `include/openglad/interface/game_loop_state.h` (`GameLoopFrameState` lines 10-19; new field added here).
- Phase 1 outputs: `target_fps_` session field; `og::core::target_frame_interval_ms`.
- Phase 2 outputs: `og::core::FrameDeadlinePacer`, `FrameDeadlineDecision`.
- `tests/test_game_loop.cpp` — existing test fixtures using `now_ms` injection via `GameLoopDeps`.
- `scripts/run_jitter_gtests.sh` — existing jitter runner.

## New Outputs
- `GameLoopDeps::sleep_ms` callback (`std::function<void(std::uint32_t)>`) — defaults to `SDL_Delay`. Tests inject a no-op or capturing lambda.
- Native main-loop helper `og::runtime::run_native_game_loop(screen&, GameLoopFrameState&, const GameLoopDeps&)` declared in `include/openglad/platform/game_loop.h` that owns the sleep+tick cycle, replacing the bare `while (!done) game_frame(...)` in `glad_main()`.
- New tests:
    - `GameLoop.single_tick_per_call`: N=100 calls with `now_ms` advanced by exactly the interval each call asserts exactly N ticks executed, never more.
    - `GameLoop.idle_call_sleeps_remaining_interval`: inject `now_ms` returning `interval_ms / 2` past the last tick; capture `deps.sleep_ms` arg and assert it equals the remaining half-interval.

## File Changes
- `include/openglad/platform/game_loop.h`: add `std::function<void(std::uint32_t)> sleep_ms;` to `GameLoopDeps`. Declare `void run_native_game_loop(screen&, GameLoopFrameState&, const GameLoopDeps&);`.
- `include/openglad/interface/game_loop_state.h`: add `#include <openglad/core/frame_pacing.h>` and `og::core::FrameDeadlinePacer pacer;` field. Keep `last_frame_time` and `accumulated_time` for now (legacy callers may still touch them in tests); production code stops reading them.
- `src/platform/sdl/game_loop.cpp`:
    - Delete `kMaxTicksPerCall` and the multi-tick branch in `ticks_to_run_this_call()`. Function still exists for browser callers but always returns 0 or 1.
    - In `compute_tick_schedule()`, the interval is now `og::core::target_frame_interval_ms(og::runtime::current_session->target_fps_) / std::max(0.01f, og::runtime::current_session->g_game_speed_factor_)` clamped to `>= 1` ms. `deps.fixed_tick_ms` still wins when non-zero (kept for tests).
    - `game_frame_with_result()`: at the start of the timing block, configure the pacer on first call (`st.pacer.configure(interval_ms, now_ms())` if `interval_ms() == 0`), then call `auto decision = st.pacer.tick(now_ms())`. If `decision.run_tick == false`, invoke `(deps.sleep_ms ? deps.sleep_ms : SDL_Delay)(decision.sleep_ms)` and return `Continue` without rendering and without polling input again. If `run_tick == true`, run **exactly one** sim tick (no for-loop over `ticks_to_run`) and one render.
    - Render is gated on `decision.run_render` — render only on tick frames, never on idle wakeups.
    - Emit `runtime_trace` `desktop_loop_sleep_ms` with the captured sleep value so jitter scripts can observe the actual sleep distribution.
- `src/platform/sdl/glad_gameplay.cpp:255-257`: replace the `while (!g_frame_state().done) { game_frame(...); }` block with `run_native_game_loop(*current_screen, g_frame_state(), GameLoopDeps{});`. The helper is the one and only place that calls `SDL_Delay` for pacing in the native build.
- `tests/test_game_loop.cpp`: update any test that asserted multi-tick catch-up behavior to assert the new single-tick contract; existing `now_ms` injection tests should pass with at most a fixture tweak.
- Add new tests inside the `og_test_game_core` group:
    - `GameLoop.single_tick_per_call`
    - `GameLoop.idle_call_sleeps_remaining_interval`

## Implementation Details
- The browser path (`run_browser_wrapper_frame()` in `src/platform/sdl/game_loop.cpp:437`) is updated separately in **Phase 5**; this phase leaves it alone except for the `pacer` field already being present on `GameLoopFrameState` (the browser path simply does not call `pacer.tick`).
- `last_frame_time` and `accumulated_time` are kept on `GameLoopFrameState` to avoid disturbing test fixtures, but production code in `game_frame_with_result()` no longer reads them. Final removal is out of scope here.

## Verification Phases
- **Phase ID:** `phase_3_check`
    - **Type:** `check`
    - **bounce_target:** `phase_3_desktop_loop_rewrite`
    - **Purpose:** prove the desktop loop now runs at most one sim tick per call, sleeps to the deadline, and that all existing game-loop tests pass with the new shape.
    - **Commands inline:**
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure -R 'GameLoop|FrameDeadlinePacer|FrameRateConfig|Picker'`
        - `bash scripts/run_jitter_gtests.sh build/ci-test/og_test_game_core build/ci-test/og_test_view build/ci-test/og_unit_sim`

## Success Criteria
- Existing `GameLoopJitter` tests pass.
- `GameLoop.single_tick_per_call` and `GameLoop.idle_call_sleeps_remaining_interval` pass.
- `scripts/run_jitter_gtests.sh` exits zero.
- No code path inside `game_frame_with_result()` runs more than one sim tick or renders on an idle wakeup.

## Git Commit Requirement
Before yielding control, the implementer MUST stage and commit all changes with `git add -A && git commit -m "desktop-loop-rewrite: <change>"`. Do not yield with a dirty working tree.
