# Phase 5 — End-to-End Speed Verification Notes

Run date: 2026-05-06
Branch: wip/networking @ aba7e746

## Result: PASS

All success criteria met. The full ci-test suite is green (36/36),
the master sim-cadence regression is locked in, browser and headless
paths use the new cadence helper, and the sim-cadence formula
`world.timer_wait * 13.6 / g_game_speed_factor_` is preserved
through `og::core::rounded_render_tick_interval_ms`.

## Commands Executed

| Command | Result |
| --- | --- |
| `cmake --build --preset ci-test` | OK |
| `ctest --preset ci-test --output-on-failure` | 36/36 passed in ~204s |
| `cmake --build --preset dev-debug` | OK (882 targets) |
| `cmake --build --preset ci-test --target openglad_server` | OK |
| `bash scripts/test_headless_server_cli.sh build/ci-test/openglad_server` | exit 0 |
| `ctest --preset ci-test -R 'MasterSpeedRegression' --output-on-failure` | n/a (filter matches inside `og_test_game_core`); ran binary directly — PASS |
| `og_test_game_core --gtest_filter='GameLoopJitter.browser_wrapper_*'` | 6/6 PASS |
| `og_unit_server --gtest_filter='HeadlessTickInterval.*'` | 4/4 PASS |

## Static Greps

| Grep | Expectation | Result |
| --- | --- | --- |
| `schedule_target_fps_interval` in `src/`, `include/`, `tests/` | 0 matches | 0 matches (only stale `.plan/`/`workflow.yaml` doc references) |
| `schedule_timer_wait_interval` in source tree | ≥1 match | Found in `include/openglad/core/sim_cadence.h:32,37` and `tests/unit/test_sim_cadence.cpp:33,48,63` (see deviation §1) |
| `target_frame_interval_ms` in `src/platform/sdl/game_loop.cpp` | only inside `render_interval_ms_for_session` | Sole match at line 160 inside that helper |
| `target_fps_from_cfg` in `src/server/server_main.cpp` | result not assigned to `frame_interval_ms` | 0 matches in that file; `frame_interval_ms` is sourced from `og::server::compute_headless_tick_interval_ms(og::sim::DEFAULT_TIMER_WAIT)` (server_main.cpp:255-257) |
| `compute_sim_interval_ms` in `src/platform/sdl/game_loop.cpp` | ≥1 match | Match at line 82, called from `game_frame_with_result()` |

## Side-by-side Sim Cadence Formula

- `master:src/platform/sdl/game_loop.cpp` line 159:
  `s.world().timer_wait / g_game_speed_factor_` (units: query_timer ticks).
- HEAD: `compute_sim_interval_ms()` →
  `rounded_render_tick_interval_ms(timer_wait, speed_factor)` in
  `include/openglad/core/util.h:118-127`, which evaluates
  `timer_wait * 13.6 / speed_factor` (ms).

The 13.6× factor (`kTimerWaitToMs`) converts master's tick-domain
result into milliseconds, preserving the per-tick cadence.

## Deviations

1. **`schedule_timer_wait_interval` lives under `include/`, not `src/`.**
   The phase-5 plan's grep instruction reads "in `src/`". The helper
   `compute_sim_interval_ms` is defined inline in
   `include/openglad/core/sim_cadence.h`, so the trace token is
   emitted from a header rather than a `.cpp` under `src/`. This is
   the intended Phase 1 design (header-only inline helper for testability)
   and is callable from `src/platform/sdl/game_loop.cpp:82`. Functionally
   equivalent; only the literal grep path differs from the plan text.
   No bounce-back needed.
