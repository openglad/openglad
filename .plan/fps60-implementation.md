# Phase 2 — FPS 60 default implementation

## Edits

- `include/openglad/core/frame_rate_config.h:11-13` -- expanded comment block to
  explain 60 FPS choice (~16.7 ms per frame) and reaffirm sim cadence is owned by
  `world.timer_wait`.
- `include/openglad/core/frame_rate_config.h:14` -- `kDefaultTargetFps = 72;` ->
  `kDefaultTargetFps = 60;`.
- `include/openglad/core/frame_rate_config.h:39` -- doc comment on
  `target_frame_interval_ms` rewritten to: `Rounds half-up: 60 fps -> 17 ms, 120 fps -> 8 ms, 30 fps -> 33 ms. Always >= 1 ms.` (no literal `72`).
- `include/openglad/interface/session_state.h:81` -- `int target_fps_ = 72;` ->
  `int target_fps_ = 60;`.
- `src/core/frame_rate_config.cpp:5-10` -- comment block rewritten to remove the
  two `72` literals; now anchors generically on "the literal default".
- `src/core/frame_rate_config.cpp:11-12` -- `static_assert(kDefaultTargetFps == 72, ...)`
  -> `static_assert(kDefaultTargetFps == 60, "session_state.h target_fps_ literal default must match kDefaultTargetFps (60)")`.

## Tests

Test count before: 2240 gtest cases (36 ctest groups). After: 2240 (36 groups).
`ctest --preset ci-test --output-on-failure` -> 100% pass, 0 failures. No test
sources were edited; every `72` site in the Phase 1 inventory is an explicit
opt-in (param/assignment/docstring) and asserts on its own chosen rate, not the
default literal.

## Out-of-scope (NOT touched)

Sim cadence (`timer_wait=6`, `kTimerWaitToMs`, `TIMER_WAIT_TO_MS`) and the
interpolation client wiring (`set_render_interpolation_client`) were NOT touched.
