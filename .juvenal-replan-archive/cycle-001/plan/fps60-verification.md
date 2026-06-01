# Phase 3 — FPS 60 verification

## Render rate measured

No interactive display is available, so render rate was measured via
`ctest --preset ci-test -R deterministic_60_fps_render_with_master_sim_cadence -V`
(test source: `tests/test_frame_pacing_jitter.cpp:284-287` →
`run_jitter_drive_at_fps(60)`). The drive loop advances a fake clock by
`render_interval = og::core::target_frame_interval_ms(60) = 17 ms` per
iteration over `kFrames = 600` iterations, and the test asserts
`render_count` is within ±1 of `kFrames`.

- `render_interval = 17 ms` (rounds half-up: `(1000 + 30) / 60 = 17`,
  matches `tests/unit/test_frame_rate_config.cpp:97`-style expectations).
- `render_count = 600` over a `600 × 17 ms = 10200 ms` synthetic window.
- Realised render rate = `600 / 10.2 s ≈ 58.82 FPS`, within the
  `60 ± 2` FPS gate. The remaining ~1 FPS gap is the integer-ms rounding
  ceiling at `17 ms / frame`, identical in character to the Phase 1
  72-fps reading (~71.43 FPS at 14 ms/frame, baselined in
  `fps60-baseline.md`).
- Test result: `[  PASSED  ]` for
  `FramePacingJitter.deterministic_60_fps_render_with_master_sim_cadence`
  (2 ms).

## Sim cadence measured

Same drive loop. `sim_pacer` is configured with the master cadence
`sim_interval = std::lround(DEFAULT_TIMER_WAIT * TIMER_WAIT_TO_MS) = 82 ms`
(asserted at `tests/test_frame_pacing_jitter.cpp:134`), independent of
`target_fps_`. Sim ticks are counted via `deps.after_act` increments of
`tick_count`; the test verifies `tick_count ≈ kFrames * render_interval /
sim_interval` within ±2.

- `expected_sim_ticks = (600 × 17) / 82 = 10200 / 82 = 124`.
- Observed sim ticks over `10200 ms` simulated wall clock = `124`.
- Realised sim cadence = `124 / 10.2 s ≈ 12.16 Hz`, within the `12 ± 1`
  Hz gate. The mean inter-tick interval (asserted by EXPECT_NEAR at
  `test_frame_pacing_jitter.cpp:213-216`) tracks `sim_interval = 82 ms`
  within one render-frame quantum (`17 ms`) — confirming sim is governed
  by `world.timer_wait`, not by the new 60-fps render target.

## Wall-clock gameplay timings comparison

Methodology (mirrors Phase 1, recorded in
`fps60-baseline.md` → `## Wall-clock gameplay timings at 72 fps`):
scratch GoogleTest (`tests/test_fps60_scratch.cpp`, **deleted before
commit** as required) pinned
`og::runtime::current_session->target_fps_ = 60` via a `TargetFpsPin`
guard and exercised the per-sim-tick math used in `walker::act()`
(`src/gameplay/walker.cpp:831-845`) over a fixed tick count. Tick
interval held at the master cadence `DEFAULT_TIMER_WAIT * TIMER_WAIT_TO_MS
= 6 × 13.6 = 81.6 ms`. Scratch test ran inside `og_test_game_core` and
both cases passed before deletion.

72-fps reference numbers cited verbatim from `fps60-baseline.md`
(see `## Wall-clock gameplay timings at 72 fps` in that file); they were
**not** re-measured and `kDefaultTargetFps` was **not** temporarily
reverted to 72.

**Walker traverses 320 px at `stepsize = 2.0f`:**

| Quantity | 72-fps reference (`fps60-baseline.md`) | 60-fps measurement (scratch) | Δ |
|---|---|---|---|
| `ticks`   | `160`        | `160`        | `0 ticks` |
| `ms`      | `13056.0`    | `13056.0`    | `0 ms` |
| `tick_ms` | `81.6`       | `81.6`       | `0 ms` |

Scratch printout at 60 fps:
`WALL_CLOCK_TRAVERSE stepsize=2.0 distance=320 ticks=160 ms=13056.0 tick_ms=81.6`.
Δ = 0 ms / 0 ticks, well within the ±50 ms / ±1 tick gate.

**One full attack swing (lunge + recoil):**

| Quantity        | 72-fps reference (`fps60-baseline.md`) | 60-fps measurement (scratch) | Δ |
|---|---|---|---|
| `ticks`         | `3`        | `3`        | `0 ticks` |
| `ms`            | `244.8`    | `244.8`    | `0 ms` |
| `lunge_ticks`   | `3`        | `3`        | `0 ticks` |
| `lunge_ms`      | `244.8`    | `244.8`    | `0 ms` |
| `recoil_ticks`  | `2`        | `2`        | `0 ticks` |
| `recoil_ms`     | `163.2`    | `163.2`    | `0 ms` |
| `tick_ms`       | `81.6`     | `81.6`     | `0 ms` |

Scratch printout at 60 fps:
`WALL_CLOCK_SWING ticks=3 ms=244.8 lunge_ticks=3 lunge_ms=244.8 recoil_ticks=2 recoil_ms=163.2 tick_ms=81.6`.
Δ = 0 ms / 0 ticks, well within the ±50 ms / ±1 tick gate.

Both quantities are governed solely by `world.timer_wait = 6` and
`TIMER_WAIT_TO_MS = 13.6f` (master sim cadence), neither of which Phase 2
touched, so exact-match agreement with the Phase 1 numbers is the
expected result and confirms that the render-rate change is sim-neutral.

## Conclusion

The goal is met. With `kDefaultTargetFps = 60`
(`include/openglad/core/frame_rate_config.h:15`) and
`session_state::target_fps_ = 60`
(`include/openglad/interface/session_state.h:81`), the render path runs
at ~58.82 FPS — within the `60 ± 2` FPS acceptance gate — while sim
cadence holds at ~12.16 Hz (`60 ± 2` FPS gate becomes a sim-side `12 ± 1`
Hz gate) because gameplay timing is owned by `world.timer_wait`, not by
`target_fps_`. The two paired gameplay events (320-px traverse and full
attack swing) reproduce the Phase 1 wall-clock numbers exactly (0 ms / 0
tick delta), confirming perceived game speed is unchanged. Final test
gate: `cmake --preset ci-test && cmake --build --preset ci-test && ctest
--preset ci-test --output-on-failure` →
**100% tests passed, 0 tests failed out of 36 ctest groups (2240 gtest
cases total)**.
