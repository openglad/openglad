# Phase 5 — Browser pacing and headless server pacing alignment

## Phase Name
`browser-and-headless-pacer`

## Implement Phase ID
`phase_5_browser_and_headless_pacer`

## Preexisting Inputs
- `.plan/plan.md` — full implementation plan.
- `src/platform/sdl/glad.cpp:243-340` (`emscripten_frame_wrapper`).
- `src/platform/sdl/game_loop.cpp:437-469` (`run_browser_wrapper_frame`).
- `src/core/frame_pacing.cpp:13-60` (`browser_frame_target_interval_ms` and `step_browser_frame_pacing` — kept; cadence input changes).
- `src/server/server_main.cpp:360-380` — existing `sleep_until` headless pacer.
- Phase 1 outputs: `target_fps_from_cfg()` getter, `target_frame_interval_ms()`.
- Phase 2 outputs: `FrameDeadlinePacer`.
- `tests/unit/test_game_loop_wrapper.cpp` (in `og_unit_sim`) — covers `step_browser_frame_pacing`.
- `tests/test_game_loop.cpp:1595-1690` — `GameLoopJitter.browser_wrapper_*` tests.
- `scripts/test_headless_server_cli.sh`, `scripts/test_emscripten_build.sh` — preexisting scripts.

## New Outputs
- Browser pacer sources its target interval from `og::core::target_frame_interval_ms(session.target_fps_)` instead of `browser_frame_target_interval_ms(timer_wait, speed_factor)`. The 16 ms browser floor at `src/core/frame_pacing.cpp:22` remains (browser cannot tick faster than rAF).
- Headless server pacer (`src/server/server_main.cpp`) uses `og::core::target_frame_interval_ms(target_fps_from_cfg(cfg))` instead of `wait_ticks * TIMER_WAIT_TO_MS`.
- Headless server CLI gains `--fps <n>`: when present, it calls `cfg.apply_setting("graphics", "target_fps", arg)` before reading the value back via `target_fps_from_cfg(cfg)`.
- New `runtime_trace` `browser_frame_step` emitted at most once per frame in the rAF wrapper.

## File Changes
- `src/core/frame_pacing.cpp` and `include/openglad/core/frame_pacing.h`: change `browser_frame_target_interval_ms()` to take `int fps` instead of `(short timer_wait, float speed_factor)`. Update all callers in the same change; do not add a compat overload. Update `tests/unit/test_game_loop_wrapper.cpp` (in `og_unit_sim`) and any other test in the same change.
- `src/platform/sdl/glad.cpp:243-340` (`emscripten_frame_wrapper`): replace `timer_wait`/`speed_factor` reads with `og::runtime::current_session->target_fps_`. Strict single-step contract: each call to `run_browser_wrapper_frame` may execute at most one sim tick (it already does); add a `runtime_trace` `browser_frame_step` that emits at most once per frame and update `tests/unit/test_game_loop_wrapper.cpp` to assert the count.
- `src/server/server_main.cpp:360-380`: replace `wait_ticks * TIMER_WAIT_TO_MS` with `og::core::target_frame_interval_ms(...)`. Parse `--fps <n>` in the existing CLI argument loop and call `cfg.apply_setting("graphics", "target_fps", value)` before computing the interval. The integer is then read via `target_fps_from_cfg(cfg)` (clamps).
- `tests/test_game_loop.cpp:1595-1690`: update `GameLoopJitter.browser_wrapper_*` tests to drive the new fps-based input shape; assertions on rounded interval should use `target_frame_interval_ms()`.
- `tests/unit/test_game_loop_wrapper.cpp`: same.

## Implementation Details
- The browser tick is still scheduled by the browser's rAF loop, so the deadline pacer is **not** used in the browser to *sleep*; the browser uses `step_browser_frame_pacing()` to decide whether the rAF callback should run a tick. What changes is the input — the rAF helper now reads `target_fps_` from the session instead of `timer_wait` and `speed_factor`.
- `world.timer_wait` continues to be transmitted in snapshots (sim determinism). Visual cadence is no longer slaved to it.
- On the headless server, the deadline-sleep loop keeps its existing structure but pulls the fps value from cfg at startup; in-game changes to the target fps require a server restart (acceptable for headless).

## Verification Phases
- **Phase ID:** `phase_5_check`
    - **Type:** `check`
    - **bounce_target:** `phase_5_browser_and_headless_pacer`
    - **Purpose:** prove the browser wrapper and the headless server both source their frame interval from the same `target_fps` knob.
    - **Commands inline:**
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure -R 'GameLoopWrapper|GameLoopJitter|HeadlessServer'`
        - `bash scripts/test_headless_server_cli.sh`
        - `bash scripts/test_emscripten_build.sh` (must self-detect a missing emsdk and exit 0 with a printed skip message; the checker treats any non-zero exit as failure).

## Success Criteria
- All `GameLoopJitter.browser_*` tests pass with the new signatures.
- `scripts/test_headless_server_cli.sh` runs and exits cleanly; the server respects `--fps <n>`.
- `scripts/test_emscripten_build.sh` either compiles cleanly or self-skips with exit 0.
- No code path in the browser wrapper or headless server reads `timer_wait * speed_factor` for pacing — the value is sourced from `target_fps`.

## Git Commit Requirement
Before yielding control, the implementer MUST stage and commit all changes with `git add -A && git commit -m "browser-and-headless-pacer: <change>"`. Do not yield with a dirty working tree.
