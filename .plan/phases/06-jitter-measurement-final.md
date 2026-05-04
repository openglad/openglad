# Phase 6 — End-to-end jitter measurement and final docs

## Phase Name
`jitter-measurement-final`

## Implement Phase ID
`phase_6_jitter_measurement_final`

## Preexisting Inputs
- `.plan/plan.md` — full implementation plan.
- All artifacts from Phases 1-5 (consumed in place):
    - `include/openglad/core/frame_rate_config.h`, `src/core/frame_rate_config.cpp` (Phase 1).
    - `include/openglad/core/frame_pacing.h`, `src/core/frame_pacing.cpp` containing `FrameDeadlinePacer` (Phase 2).
    - Updated `src/platform/sdl/game_loop.cpp` and `src/platform/sdl/glad_gameplay.cpp` (Phase 3).
    - Updated `src/platform/sdl/local_transport_shadow.cpp` and bounded message overloads (Phase 4).
    - Updated browser/headless pacers (Phase 5).
- `scripts/analyze_jitter_metrics.mjs` and `scripts/run_jitter_gtests.sh`.
- `cfg/openglad.yaml` (already updated in Phase 1).
- `scripts/check_vendor_leaks.sh`.
- Existing `og_test_game_core` group at `CMakeLists.txt:1593-1606`.

## New Outputs
- New deterministic jitter regression test `tests/test_frame_pacing_jitter.cpp` (in `og_test_game_core` group at `CMakeLists.txt:1593-1606`):
    - Drives `game_frame_with_result()` for 600 simulated frames at 72 fps with an injected `now_ms` advancing by exactly `target_frame_interval_ms(72)` each call.
    - Captures every value passed to `deps.sleep_ms` and every `desktop_loop_sleep_ms` runtime trace.
    - Asserts `max(observed_interval) - min(observed_interval) <= 1` ms (zero structural jitter; only integer rounding remains).
    - Asserts exactly 600 sim ticks ran (no catch-up bursts, no skipped ticks).
    - Repeats the same flow at 60 fps in a second test case to prove the knob is honored.
- Jitter assertion under storm: extend the budgeted-drain test from Phase 4 (or add a new case in `tests/test_frame_pacing_jitter.cpp`) asserting that with 1000 inbound messages queued, `local_transport_shadow_finish_tick` returns within a bound proportional to `MAX_INBOUND_MESSAGES_PER_TICK` rather than to the queue length, measured against an injected clock.

## File Changes
- New `tests/test_frame_pacing_jitter.cpp`; add to the `og_test_game_core` group at `CMakeLists.txt:1593`.
- **Do not** add markdown summary docs; per repo rules, documentation files are not created unless explicitly requested.

## Implementation Details
- The jitter regression test must be wall-clock-independent — uses an injected clock so it is reproducible on any CI runner including ASan and UBSan.
- The two fps cases (60 and 72) live in the same compilation unit; both use the injected-clock harness from `tests/test_game_loop.cpp` patterns.

## Verification Phases
- **Phase ID:** `phase_6_check`
    - **Type:** `check`
    - **bounce_target:** `phase_6_jitter_measurement_final`
    - **Purpose:** confirm a measurable jitter regression test is in place and that the full test suite is green end-to-end.
    - **Commands inline:**
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure`
        - `bash scripts/run_jitter_gtests.sh build/ci-test/og_test_game_core build/ci-test/og_test_view build/ci-test/og_unit_sim`
        - `bash scripts/check_vendor_leaks.sh`

## Success Criteria
- Full `ctest --preset ci-test` is green.
- `scripts/run_jitter_gtests.sh` exits zero.
- `scripts/check_vendor_leaks.sh` exits zero (no third-party header leakage from the new code, which only touches `og::core`, `og::runtime`, and `og::sim` public headers).
- New `FramePacingJitter.*` tests in `og_test_game_core` are exercised and pass for both 60 and 72 fps.
- Storm-jitter assertion holds: `local_transport_shadow_finish_tick` is bounded by `MAX_INBOUND_MESSAGES_PER_TICK`, not by queue length.

## Git Commit Requirement
Before yielding control, the implementer MUST stage and commit all changes with `git add -A && git commit -m "jitter-measurement-final: <change>"`. Do not yield with a dirty working tree.
