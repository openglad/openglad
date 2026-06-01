# Phase 1 — fps60 baseline audit

## Render-rate baseline

- Current default: `og::core::kDefaultTargetFps = 72`
  (`include/openglad/core/frame_rate_config.h:14`).
- `og::core::target_frame_interval_ms(72) = 14 ms`;
  `og::core::target_frame_interval_ms(60) = 17 ms`
  (`include/openglad/core/frame_rate_config.h:40-47`).
- Per-session render interval is read by `render_interval_ms_for_session()`
  at `src/platform/sdl/game_loop.cpp:153-161`: it pulls
  `og::runtime::current_session->target_fps_` and runs it through
  `og::core::target_frame_interval_ms(...)`.
- Measured render rate (jitter test path; no interactive display available):
  `og_test_game_core --gtest_filter='*deterministic_72_fps_render_with_master_sim_cadence*'`
  from `tests/test_frame_pacing_jitter.cpp:279-282` →
  `run_jitter_drive_at_fps(72)`. Over the synthetic `kFrames = 600` window
  with `render_interval = 14 ms`, observed `render_count = 600` and simulated
  wall-clock elapsed = `600 * 14 ms = 8400 ms` → realised render rate
  `600 / 8.4 s ≈ 71.43 FPS` (the integer-ms rounding ceiling at 14 ms/frame).
  Test result: `[  PASSED  ] 1 test.`

## Sim-cadence baseline

- Runtime defaults assign `world.timer_wait = 6`:
  - `src/gameplay/game_world.cpp:1261` (`world::reset()`)
  - `src/interface/screen.cpp:825` (`screen::ctor`)
- Struct defaults also `= 6`:
  - `include/openglad/gameplay/game_world.h:219`
    (`signed char timer_wait = 6;`)
  - `include/openglad/gameplay/world_snapshot.h:193`
    (`std::int8_t timer_wait = 6;`)
- Derived sim tick interval: `6 * 13.6 ms = 81.6 ms`, rounds half-up to
  `82 ms` (`std::lround(DEFAULT_TIMER_WAIT * TIMER_WAIT_TO_MS) = 82u`,
  asserted at `tests/test_frame_pacing_jitter.cpp:134` and
  `tests/test_game_loop.cpp:1930`). Sim rate ≈ `1000 / 82 ≈ 12.2 Hz`.
- Mirror constants confirmed equal at `13.6f`:
  - `og::core::kTimerWaitToMs` —
    `include/openglad/core/util.h:120` (`constexpr float kTimerWaitToMs = 13.6f;`).
  - `og::sim::TIMER_WAIT_TO_MS` —
    `include/openglad/gameplay/net_constants.h:10`
    (`inline constexpr float TIMER_WAIT_TO_MS = 13.6f;`).

## Interpolation wiring

- `src/platform/sdl/local_transport_shadow.cpp:491` —
  `configure_display_game_client(...)` calls
  `gameplay_screen.set_render_interpolation_client(&display_client);` so the
  display `og::sim::GameClient` is installed on the active screen for the
  normal local-play path.
- `src/interface/render/walker_draw.cpp:236-248` —
  `query_render_interpolation_alpha()` fetches the active client via
  `display_game_client()` and returns
  `client->render_interpolation_alpha(display_interpolation_speed_factor())`
  when the client is non-null (otherwise `1.0f`). Both
  `viewscreen::redraw()` overloads
  (`src/interface/render/view.cpp:328,335,419,426`) call this and then
  resolve the control walker through it.
- `src/interface/render/walker_draw.cpp:251-309` —
  `resolve_walker_render_position(walker, alpha)` returns interpolated
  worldx/worldy/xpos/ypos when the client is present and the walker has a
  non-zero `entity_id()`. With the display client installed, lines 273-297
  apply the interpolated path; lines 254-271 / 274-290 are the
  fallback-to-raw paths for missing client or missing entity_id.

## Wall-clock gameplay timings at 72 fps

Methodology: scratch GoogleTest (`tests/test_fps60_scratch.cpp`, NOT committed
— deleted before this commit) that pinned
`og::runtime::current_session->target_fps_ = 72` via a `TargetFpsPin` guard
and exercised the per-sim-tick math used in `walker::act()`
(`src/gameplay/walker.cpp:831-845`) over a fixed tick count. Tick interval
held at the master cadence `DEFAULT_TIMER_WAIT * TIMER_WAIT_TO_MS = 6 *
13.6 = 81.6 ms`. Scratch test ran inside `og_test_game_core` and was
verified to PASS before deletion.

Reference values Phase 3 must reproduce within ±50 ms or ±1 tick at the
new 60-fps default:

- **Walker traverses 320 px at `stepsize = 2.0f`**
  (representative early-game living stepsize):
  - `ticks = 160` (160 walkstep calls × 2.0 px = 320 px).
  - `ms = 160 * 81.6 ≈ 13056.0 ms` (≈ 13.06 s wall-clock).
  - Scratch test printout: `WALL_CLOCK_TRAVERSE stepsize=2.0 distance=320
    ticks=160 ms=13056.0 tick_ms=81.6`.

- **One full attack swing (lunge + recoil) on a target**
  starting from `attack_lunge = 1.0f`, `hit_recoil = 1.0f` and applying the
  walker::act() decrements (`attack_lunge -= 0.4f`,
  `hit_recoil -= 0.6f`, each clamped to 0.0f) per tick:
  - `lunge_ticks = 3` (1.0 → 0.6 → 0.2 → 0.0).
  - `recoil_ticks = 2` (1.0 → 0.4 → 0.0).
  - Swing completes when both lunge and recoil reach zero → `ticks = 3`
    (max of the two) → `ms = 3 * 81.6 = 244.8 ms`.
  - Scratch test printout: `WALL_CLOCK_SWING ticks=3 ms=244.8
    lunge_ticks=3 lunge_ms=244.8 recoil_ticks=2 recoil_ms=163.2
    tick_ms=81.6`.

Both quantities are per-sim-tick, governed by `world.timer_wait` master
cadence, so they are independent of `target_fps_` and Phase 3 must show
identical ticks at the new 60-fps default. The wall-clock ms figures
likewise depend only on `TIMER_WAIT_TO_MS = 13.6f` and `timer_wait = 6` —
neither is touched by Phase 2.

## Hard-coded 72 inventory

Complete verbatim output of
`grep -RnE '\b72\b' include/openglad/core/frame_rate_config.h include/openglad/interface/session_state.h src/core/frame_rate_config.cpp`:

```
include/openglad/core/frame_rate_config.h:14:inline constexpr int kDefaultTargetFps = 72;
include/openglad/core/frame_rate_config.h:39:// Rounds half-up: 72 fps -> 14 ms, 60 fps -> 17 ms. Always >= 1 ms.
include/openglad/interface/session_state.h:81:    int target_fps_ = 72;
src/core/frame_rate_config.cpp:8:// field directly here; we anchor on the literal 72 instead. 72 is the
src/core/frame_rate_config.cpp:11:static_assert(kDefaultTargetFps == 72,
```

Production sites that must move when the default moves (Phase 2):

- `include/openglad/core/frame_rate_config.h:14` —
  `inline constexpr int kDefaultTargetFps = 72;`
- `include/openglad/core/frame_rate_config.h:39` — doc comment for
  `target_frame_interval_ms`:
  `// Rounds half-up: 72 fps -> 14 ms, 60 fps -> 17 ms. Always >= 1 ms.`
- `include/openglad/interface/session_state.h:81` —
  `int target_fps_ = 72;`
- `src/core/frame_rate_config.cpp:5-12` — comment block above the
  `static_assert`. Line 8 contains the literal `72` twice
  (`anchor on the literal 72 instead. 72 is the`).
- `src/core/frame_rate_config.cpp:11` —
  `static_assert(kDefaultTargetFps == 72, …)`.

Test-tree references (each annotated; today every one is **explicit
opt-in** — none assert on the default literal):

- `tests/test_frame_pacing_jitter.cpp:123`
  (`// - render_pacer fires at the configured target fps (~14 ms at 72 fps,`)
  — **explicit opt-in** (dual-rate docstring naming both 72 and 60;
  describes the two parameterised cases, not the default).
- `tests/test_frame_pacing_jitter.cpp:182`
  (`// (≈ 102 at 72 fps, ≈ 124 at 60 fps). Allow ±2 to absorb phase between`)
  — **explicit opt-in** (dual-rate docstring).
- `tests/test_frame_pacing_jitter.cpp:281`
  (`run_jitter_drive_at_fps(72);` inside
  `TEST(FramePacingJitter, deterministic_72_fps_render_with_master_sim_cadence)`)
  — **explicit opt-in** (the test is explicitly parameterised at 72; a
  sibling test at line 286 parameterises 60).
- `tests/test_game_loop.cpp:1911`
  (`og::runtime::current_session->target_fps_ = 72;` inside
  `MasterSpeedRegression`) — **explicit opt-in** (direct assignment;
  restored on exit at lines 1912-1917).
- `tests/test_game_loop.cpp:1925`
  (`og::core::target_frame_interval_ms(72);`) — **explicit opt-in**
  (interval math probed at the literal 72).
- `tests/test_game_loop.cpp:1979`
  (`"render must run at target_fps (~14 ms / frame at 72 fps)";`) —
  **explicit opt-in** (failure message for the 72-pinned test).
- `tests/unit/test_frame_rate_config.cpp:97`
  (`EXPECT_EQ(og::core::target_frame_interval_ms(72), 14u);`) —
  **explicit opt-in** (interval-math regression at literal 72; sibling
  expectations cover 60, 30, 120, 144).
