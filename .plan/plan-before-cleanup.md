# Plan: Jitter-Free Networking with Configurable FPS

## 1. Context

### Goal (from `.plan/goal.md`)
> redesign and reimplement the networking code so that there is no possibility for jitter: each frame should run in the exact same amount of time. make the fps configurable

### Problem Statement
Today OpenGlad's per-frame wall-clock time varies for two distinct reasons, both of which add up to perceptible jitter on screen and to noisy ticks over the wire:

1. **Frame pacing is variable-work, fixed-period.** The native main loop at `src/platform/sdl/glad_gameplay.cpp:255-257` is a tight `while (!g_frame_state().done) { game_frame(...); }` with no sleep — the only thing that throttles is `ticks_to_run_this_call()` returning 0 when the accumulator hasn't reached the interval. Within `game_frame_with_result()` (`src/platform/sdl/game_loop.cpp:307`) a single call may execute **0..4 sim ticks** plus a render. Since `kMaxTicksPerCall = 4` (`src/platform/sdl/game_loop.cpp:55`), when one frame is slow the next frame catches up by running multiple ticks, which is the textbook source of frame-time jitter.

2. **Networking work is interleaved synchronously inside the tick.** `local_transport_shadow_finish_tick()` (`src/platform/sdl/local_transport_shadow.cpp:1053`) runs the authoritative server step and then drains snapshot/event messages into every client mirror inside the same call as the simulation. `poll_local_transport_client()` (`src/platform/sdl/local_transport_shadow.cpp:182`) drains an unbounded number of messages per tick. There is no per-frame budget cap on networking I/O. The amount of work per tick varies with snapshot size, keyframe interval (`KEYFRAME_INTERVAL_TICKS = 60` at `include/openglad/gameplay/net_constants.h:14`), and message backlog.

3. **FPS is implicit, derived from `world.timer_wait`.** The legacy `timer_wait` field (DOS-era constant `TIMER_WAIT_TO_MS = 13.6f` at `include/openglad/gameplay/net_constants.h:10`) and `g_game_speed_factor_` (`include/openglad/interface/session_state.h:76`) feed `og::core::rounded_render_tick_interval_ms()` (`include/openglad/core/util.h:118`) to compute the per-tick interval. There is no first-class `fps` setting — the render rate is a side effect of these three coupled values, and the value is round-tripped through `world_snapshot.cpp` so that all peers stay in sync. Browser pacing is clamped to `>= 16ms` (60 FPS cap) in `src/core/frame_pacing.cpp:22`.

### Codebase Background

**Modules.** Per `CLAUDE.md`, dependencies flow `ui → runtime → sim → core`. Pacing primitives live in `og::core` (`src/core/frame_pacing.cpp`, `include/openglad/core/frame_pacing.h`). The desktop game loop lives in `og::runtime` / `src/platform/sdl/game_loop.cpp` (target `og_runtime` per `CMakeLists.txt`). Networking transports live in `og::sim` (`src/gameplay/`).

**Configuration.** `cfg_store` is declared in `include/openglad/resources/gparser.h` (class at line 22, `load_settings`/`save_settings` at lines 26/28, `apply_setting` at line 30, `get_setting` at line 31). The on-disk format is **YAML** at `cfg/openglad.yaml` (the only canonical config file in `cfg/`). The "graphics" map already exists with keys `fullscreen`, `overscan_percentage`, `render`. `cfg.load_settings()` is invoked from `initialize_runtime_config()` (`src/platform/sdl/glad.cpp:200`); `cfg.save_settings()` is invoked from `bootstrap_runtime()` (`src/platform/sdl/glad.cpp:216`) after derived settings (overscan) are mirrored back at lines 220-224.

**Frame loop entry points.**
- Native: `glad_main()` in `src/platform/sdl/glad_gameplay.cpp:227` calls `game_frame(*current_screen, g_frame_state())` in a tight `while` at lines 255-257, no sleep.
- Browser: `emscripten_frame_wrapper()` in `src/platform/sdl/glad.cpp:246` runs at rAF cadence (`emscripten_set_main_loop(..., 0, 0)` at line 415) and uses `step_browser_frame_pacing()` (`src/core/frame_pacing.cpp:26`) to decide when to step the sim.
- Headless server: `src/server/server_main.cpp:360-380` already does deadline-based pacing via `std::this_thread::sleep_until(...)` using `wait_ticks * TIMER_WAIT_TO_MS` — this is the pattern to generalize.

**Networking shadow.** `LocalTransportRuntime` (`src/platform/sdl/local_transport_shadow.cpp:49-80`) carries the server session, server transport, and a vector of clients. The display client mirrors authoritative state. `local_transport_shadow_send_input()` enqueues a per-player `InputState` keyed to a sim tick. `local_transport_shadow_finish_tick()` runs `runtime->server->step()` then `poll_local_transport_client()` for each client.

**Runtime tracing.** `og::runtime::emit_runtime_trace(make_runtime_trace_record("category", "event"))` is the production-and-test trace API (`include/openglad/core/runtime_trace.h:64-67`). There is **no** `OPENGLAD_HAS_RUNTIME_TRACE` macro — call the API unconditionally; it is a no-op when disabled by `set_runtime_trace_enabled(false)`.

**Existing pacing tests, fixtures, and metrics.**
- `tests/test_game_loop.cpp` has a `GameLoopJitter` suite (lines 1595-1690) and uses `GameLoopDeps::now_ms` injection.
- `tests/unit/test_game_loop_wrapper.cpp` (in the `og_unit_sim` group) covers `step_browser_frame_pacing()`.
- `scripts/run_jitter_gtests.sh` runs `*Jitter*` tests across `og_test_game_core`, `og_test_view`, and `og_unit_sim`.
- `scripts/analyze_jitter_metrics.mjs` parses test output for jitter analysis.
- `tests/test_network_fixture.h` and `tests/integration_main.cpp` (integration) / `tests/unit/unit_main.cpp` (unit) provide test entry points.
- `scripts/check_vendor_leaks.sh`, `scripts/test_headless_server_cli.sh`, `scripts/test_emscripten_build.sh` exist and can be invoked by verifiers.

**Test groups in `CMakeLists.txt`.** Existing unit groups are `og_unit_sim`, `og_unit_emscripten_transport`, `og_unit_families`, `og_unit_entity`, `og_unit_data`. There is **no** `og_unit_core` or `og_unit_runtime` group — Phase 1 creates `og_unit_core`. Existing test groups include `og_test_game_core` (lines 1593-1606), `og_test_picker_network` (line 1667), and many others. New unit tests go into `og_unit_core`; new integration tests go into the most appropriate existing test group.

These existing tests, scripts, fixtures, and headers are **preexisting inputs** the redesign must consume and extend rather than recreate.

### High-Level Approach
Make every wall-clock frame consume exactly the same amount of time (within the OS scheduler's resolution) by:

1. Introducing a single explicit `target_fps` configuration knob (default 72, matching the historical 13.6 ms tick) sourced from `cfg`, replacing the implicit `timer_wait × g_game_speed_factor` derivation as the source of truth for the loop's wall-clock cadence.
2. Replacing the catch-up-burst accumulator (`kMaxTicksPerCall`) with a strict one-tick-per-frame deadline-based pacer that **always sleeps** to the next deadline, never runs multiple sim ticks in one frame, and never busy-waits.
3. Bounding network work per tick with an explicit message budget so that snapshot/keyframe storms cannot stretch a frame.
4. Decoupling network I/O from the sim step by giving the shadow runtime a fixed message-count cap per tick.

The legacy `timer_wait` field is retained as a sim-determinism input (it still scales `world.tick_count_` semantics for serialized snapshots) but is **decoupled from wall-clock pacing**. The `target_fps` setting governs render and tick cadence; sim physics keeps consuming ticks at the configured rate.

---

## 2. Generated Workflow Contract

The downstream workflow generator MUST emit a single inline YAML file that obeys these rules. These rules are non-negotiable.

- **Linear execution only.** No `parallel_groups`. Phases run strictly in numerical order (Phase 1 → Phase 2 → … → Phase N).
- **Self-contained inline YAML.** No top-level `include`. No phase-level `prompt_file`, `workflow_file`, `workflow_dir`, `checks`, or any other YAML-source indirection. Every phase prompt is written inline.
- **No agent-guided `bounce_targets` lists.** Each verifier uses a single fixed `bounce_target` pointing back to the implement phase it just verified. No multi-target bounce arrays, no agent-chosen targets.
- **Every verifier is an explicit top-level `check` phase.** Verification is never folded into an implement phase or hidden behind `checks:`. Each verifier appears in the phase list with type `check`.
- **Verifier locality.** A verifier MUST stay in the implement block it verifies (i.e., immediately follows its implement phase) and its `bounce_target` MUST be that implement phase's ID.
- **Commands inline in the verifier prompt.** If a verifier needs to run tests, lint, build, or any other shell command, those commands are written into the checker's prompt as literal `cmake --build --preset ci-test` / `ctest --preset ci-test --output-on-failure -R '<filter>'` invocations. These commands are NOT modeled as separate non-agentic phases.
- **Consume-existing-artifacts contract.** This plan and `.plan/goal.md` already exist. Do not refetch them, do not re-derive the goal, do not re-collect a fresh codebase inventory. Existing inputs the workflow must read in place:
    - `.plan/goal.md` — the original one-line goal.
    - `.plan/plan.md` — this document.
    - The existing source tree under `src/`, `include/openglad/`, `tests/`, `scripts/`, `CMakeLists.txt`, `cfg/`.
    - Existing `GameLoopJitter` tests in `tests/test_game_loop.cpp:1595-1690`.
    - Existing browser pacing helper `step_browser_frame_pacing()` in `src/core/frame_pacing.cpp`.
    - Existing pacing primitive `og::core::rounded_render_tick_interval_ms()` in `include/openglad/core/util.h:118`.
    - Existing `scripts/run_jitter_gtests.sh` and `scripts/analyze_jitter_metrics.mjs`.
    - Existing headless server pacer in `src/server/server_main.cpp:360-380` (the `sleep_until` pattern to generalize).
    - Existing test harnesses: `tests/test_network_fixture.h`, `tests/integration_main.cpp`, `tests/unit/unit_main.cpp`.
    - Existing canonical config `cfg/openglad.yaml`.
  These must be **consumed or updated in place**, never refetched, recollected, rediscovered, or regenerated from scratch.
- **Commit before yielding.** Every implement-phase prompt in the generated workflow MUST instruct the agent to `git add -A && git commit -m "<phase short name>: <change>"` before yielding control. Verifier phases do not commit; they only run checks and bounce.

---

## 3. Implementation Phases

The work is sequential. Each implement phase is followed by exactly one `check` phase with a fixed `bounce_target` pointing back to its implement phase.

### Phase 1 — Add FPS configuration plumbing

- **Phase Name:** `add-fps-config`
- **Implement Phase ID:** `phase_1_add_fps_config`
- **Verification Phases:**
    - ID: `phase_1_check`
    - Type: `check`
    - `bounce_target: phase_1_add_fps_config`
    - Purpose: confirm the new `target_fps` knob is read from `cfg`, defaulted, clamped, and exposed as a typed accessor; confirm the new `og_unit_core` group exists and links.
    - Commands the checker must run (write inline in checker prompt):
        - `cmake --preset ci-test`
        - `cmake --build --preset ci-test --target og_unit_core`
        - `ctest --preset ci-test --output-on-failure -R 'FrameRateConfig'`
- **Preexisting Inputs:**
    - `include/openglad/resources/gparser.h` (`cfg_store::get_setting` line 31, `apply_setting` line 30, `load_settings` line 26, `save_settings` line 28).
    - `cfg/openglad.yaml` (canonical config, YAML format, contains the `graphics:` map).
    - `src/platform/sdl/glad.cpp:200` (`initialize_runtime_config` calling `cfg.load_settings()`) and `src/platform/sdl/glad.cpp:204-230` (`bootstrap_runtime` reading from already-loaded cfg, persisting back via `cfg.save_settings()` at line 216, and mirroring `overscan_percentage` at lines 220-224).
    - `include/openglad/gameplay/net_constants.h` (existing constants `DEFAULT_SIM_TICK_MS` line 9, `TIMER_WAIT_TO_MS` line 10).
    - `include/openglad/interface/session_state.h:76` (`g_game_speed_factor_` field — new field is added adjacent to it).
- **New Outputs:**
    - New header `include/openglad/core/frame_rate_config.h` exporting:
        - `inline constexpr int kDefaultTargetFps = 72;`
        - `inline constexpr int kMinTargetFps = 10;`
        - `inline constexpr int kMaxTargetFps = 240;`
        - `int target_fps_from_cfg(cfg_store& cfg);` (clamps & defaults)
        - `void apply_target_fps_to_cfg(cfg_store& cfg, int fps);`
        - `std::uint32_t target_frame_interval_ms(int fps);` (defined inline; rounds half-up; safe-clamps to `>= 1`)
    - New impl `src/core/frame_rate_config.cpp` (added to `OG_CORE_SOURCES` in `CMakeLists.txt`).
    - New unit tests `tests/unit/test_frame_rate_config.cpp`.
    - **New** `og_add_unit_group(og_unit_core …)` block in `CMakeLists.txt`, sibling to the existing `og_add_unit_group(og_unit_sim …)` block (around line 1762). The new group contains only `tests/unit/test_frame_rate_config.cpp` initially; later phases extend it.
    - One-line YAML key added to `cfg/openglad.yaml`: `  target_fps: 72` under `graphics:` (preserving alphabetical ordering with `fullscreen`, `overscan_percentage`, `render`).
- **File Changes:**
    - Create `include/openglad/core/frame_rate_config.h`, `src/core/frame_rate_config.cpp`, `tests/unit/test_frame_rate_config.cpp`.
    - `CMakeLists.txt`:
        - Add `${SRC_DIR}/core/frame_rate_config.cpp` to `OG_CORE_SOURCES` (alongside `${SRC_DIR}/core/frame_pacing.cpp`).
        - Add a new `og_add_unit_group(og_unit_core FILES ${CMAKE_SOURCE_DIR}/tests/unit/test_frame_rate_config.cpp)` block adjacent to `og_unit_sim` (around line 1762).
    - `src/platform/sdl/glad.cpp` `bootstrap_runtime()` (after the overscan block at lines 220-224): read & persist the new setting:
        ```cpp
        const int fps = og::core::target_fps_from_cfg(cfg);
        og::core::apply_target_fps_to_cfg(cfg, fps);
        og::runtime::current_session->target_fps_ = fps;
        ```
      This runs **after** `cfg.save_settings()` is called, just like the overscan mirror: the apply_setting writes the canonical clamped value back so the next save persists it. Reorder the `cfg.save_settings()` call to occur after both overscan and target_fps have been mirrored, mirroring the existing overscan pattern; specifically move `cfg.save_settings()` to immediately after the new `apply_setting` call so both keys are flushed in one save.
    - `include/openglad/interface/session_state.h`: add `int target_fps_ = 72;` adjacent to `g_game_speed_factor_` at line 76. Default literal matches `og::core::kDefaultTargetFps`; do not include the new header here to avoid a circular include — keep the literal default in sync with the constant via a `static_assert` in `frame_rate_config.cpp`.
    - `cfg/openglad.yaml`: add `target_fps: 72` under `graphics:`.
- **Implementation Details:**
    - YAML setting key path: `("graphics", "target_fps")`. Reader returns the parsed int clamped to `[kMinTargetFps, kMaxTargetFps]`; missing or unparseable falls back to `kDefaultTargetFps`. Writer always serializes a base-10 integer with no leading zeros via `cfg.apply_setting("graphics", "target_fps", std::to_string(fps))`.
    - `target_frame_interval_ms(int fps)` returns `std::max<std::uint32_t>(1u, static_cast<std::uint32_t>((1000 + fps / 2) / fps))`. Document that this rounds half-up so 72 fps → 14 ms and 60 fps → 17 ms.
    - The session field is read-only after `bootstrap_runtime()` for this phase. Future runtime adjustments go through a setter that re-applies to `cfg` (out of scope here).
- **Verification:** After this phase, `target_fps_from_cfg()` is unit-tested for default, clamp-low, clamp-high, parse-failure, and persistence round-trip; `cmake --build --preset ci-test --target og_unit_core` succeeds; the broader test suite still compiles.

---

### Phase 2 — Replace the variable-tick accumulator with a deadline pacer

- **Phase Name:** `deadline-pacer`
- **Implement Phase ID:** `phase_2_deadline_pacer`
- **Verification Phases:**
    - ID: `phase_2_check`
    - Type: `check`
    - `bounce_target: phase_2_deadline_pacer`
    - Purpose: confirm a single `og::core::FrameDeadlinePacer` exists, that it is purely deterministic given an injected clock, and that the existing `GameLoopJitter` tests still pass.
    - Commands inline:
        - `cmake --build --preset ci-test --target og_unit_core og_test_game_core`
        - `ctest --preset ci-test --output-on-failure -R 'FrameDeadlinePacer|GameLoopJitter'`
- **Preexisting Inputs:**
    - `src/core/frame_pacing.cpp`, `include/openglad/core/frame_pacing.h` (existing pacing helpers — kept; the new pacer lives alongside them).
    - `tests/test_game_loop.cpp:1595-1690` (`GameLoopJitter` suite — must keep passing; `now_ms` is injected via `GameLoopDeps`).
    - `src/server/server_main.cpp:360-380` (`sleep_until` deadline pattern — the inspiration for the pacer).
    - `include/openglad/core/runtime_trace.h:64-67` (`emit_runtime_trace` / `make_runtime_trace_record` — call directly, no macro gating).
    - `include/openglad/core/frame_rate_config.h` from Phase 1.
- **New Outputs:**
    - In `include/openglad/core/frame_pacing.h`, add:
        ```cpp
        struct FrameDeadlineDecision {
            bool run_tick = false;
            bool run_render = false;
            std::uint32_t sleep_ms = 0;     // how long the caller must sleep
            std::uint32_t next_deadline_ms = 0; // absolute deadline of next frame
        };

        class FrameDeadlinePacer {
        public:
            void configure(std::uint32_t interval_ms, std::uint32_t now_ms);
            FrameDeadlineDecision tick(std::uint32_t now_ms);
            void reset(std::uint32_t now_ms);
            std::uint32_t interval_ms() const;
        private:
            std::uint32_t interval_ms_ = 0;
            std::uint32_t next_deadline_ms_ = 0;
            bool initialized_ = false;
        };
        ```
    - Implementation in `src/core/frame_pacing.cpp` enforcing **exactly one tick per call**:
        - `configure(interval, now)` clamps `interval_ms_ = std::max<std::uint32_t>(1u, interval)`, sets `next_deadline_ms_ = now + interval_ms_`, and `initialized_ = true`.
        - `tick(now)`: if not initialized, `configure(1, now)` and return `{false, false, 1, now + 1}` plus a `pacer_resync` trace. Otherwise:
            - If `now < next_deadline_ms_`, return `{run_tick=false, run_render=false, sleep_ms = next_deadline_ms_ - now, next_deadline_ms = next_deadline_ms_}`.
            - Else if `now - next_deadline_ms_ <= interval_ms_` (within one interval of slip): advance `next_deadline_ms_ += interval_ms_` and return `{true, true, 0, next_deadline_ms_}`.
            - Else (slipped more than one interval): set `next_deadline_ms_ = now + interval_ms_`, emit a `pacer_resync` runtime trace via `emit_runtime_trace(make_runtime_trace_record("frame_pacing", "pacer_resync"))`, and return `{true, true, 0, next_deadline_ms_}` so the current frame still runs exactly one tick (we drop the lost time deliberately rather than burst-catch-up).
- **File Changes:**
    - Edit `include/openglad/core/frame_pacing.h` and `src/core/frame_pacing.cpp` to add `FrameDeadlinePacer` and `FrameDeadlineDecision`.
    - New tests `tests/unit/test_frame_deadline_pacer.cpp` covering: steady-state advance, pause/resume resync (slip > 1 interval emits one `pacer_resync` and runs exactly one tick on the resync frame), configure → tick → tick determinism with injected clock, interval=0 clamps to 1, and that `sleep_ms` matches `interval_ms - elapsed`. Add this file to the `og_unit_core` group created in Phase 1.
- **Implementation Details:**
    - The class is fully defined in the header; the `pacer_resync` runtime-trace emit lives in the .cpp to keep the header free of `runtime_trace.h`.
    - `FrameDeadlinePacer` is the production replacement for the multi-tick accumulator inside `ticks_to_run_this_call()` (`src/platform/sdl/game_loop.cpp:123-184`). Removal of the multi-tick path is **Phase 3**, not this phase. This phase only adds the new class and tests; it does not yet wire it into the desktop loop.
- **Verification:** Unit tests assert that running 1000 simulated frames with a perfect clock advances `next_deadline_ms_` by exactly `1000 * interval_ms_`, that one stalled iteration produces a one-frame skip and exactly one `pacer_resync` trace, that `sleep_ms` is correct mid-interval, and that no call ever returns `run_tick=true` twice for one delta below `interval_ms`.

---

### Phase 3 — Switch the desktop loop to one-tick-per-frame deadline pacing

- **Phase Name:** `desktop-loop-rewrite`
- **Implement Phase ID:** `phase_3_desktop_loop_rewrite`
- **Verification Phases:**
    - ID: `phase_3_check`
    - Type: `check`
    - `bounce_target: phase_3_desktop_loop_rewrite`
    - Purpose: prove the desktop loop now runs at most one sim tick per call, sleeps to the deadline, and that all existing game-loop tests pass with the new shape.
    - Commands inline:
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure -R 'GameLoop|FrameDeadlinePacer|FrameRateConfig|Picker'`
        - `bash scripts/run_jitter_gtests.sh build/ci-test/og_test_game_core build/ci-test/og_test_view build/ci-test/og_unit_sim`
- **Preexisting Inputs:**
    - `src/platform/sdl/game_loop.cpp:55` (`kMaxTicksPerCall = 4`), `:72` (`compute_tick_schedule`), `:123-184` (`ticks_to_run_this_call`).
    - `src/platform/sdl/game_loop.cpp:307` (`game_frame_with_result`).
    - `src/platform/sdl/glad_gameplay.cpp:227-257` (`glad_main` and the bare `while` loop).
    - `include/openglad/platform/game_loop.h` (`GameLoopDeps` shape lines 24-43; existing fields stay).
    - `include/openglad/interface/game_loop_state.h` (`GameLoopFrameState` lines 10-19; new field added here).
    - Phase 1's `target_fps_` session field; Phase 2's `FrameDeadlinePacer`.
- **New Outputs:**
    - `GameLoopDeps::sleep_ms` callback (`std::function<void(std::uint32_t)>`) — defaults to `SDL_Delay`. Tests inject a no-op or capturing lambda.
    - A native main-loop helper `og::runtime::run_native_game_loop(screen&, GameLoopFrameState&, const GameLoopDeps&)` declared in `include/openglad/platform/game_loop.h` that owns the sleep+tick cycle, replacing the bare `while (!done) game_frame(...)` in `glad_main()`.
- **File Changes:**
    - `include/openglad/platform/game_loop.h`: add `std::function<void(std::uint32_t)> sleep_ms;` to `GameLoopDeps`. Declare `void run_native_game_loop(screen&, GameLoopFrameState&, const GameLoopDeps&);`.
    - `include/openglad/interface/game_loop_state.h`: add `#include <openglad/core/frame_pacing.h>` and `og::core::FrameDeadlinePacer pacer;` field. Keep `last_frame_time` and `accumulated_time` for now (legacy callers may still touch them in tests); production code stops reading them.
    - `src/platform/sdl/game_loop.cpp`:
        - Delete `kMaxTicksPerCall` and the multi-tick branch in `ticks_to_run_this_call()`. The function still exists for browser callers but always returns 0 or 1.
        - In `compute_tick_schedule()`, the interval is now `og::core::target_frame_interval_ms(og::runtime::current_session->target_fps_) / std::max(0.01f, og::runtime::current_session->g_game_speed_factor_)` clamped to `>= 1` ms. `deps.fixed_tick_ms` still wins when non-zero (kept for tests).
        - `game_frame_with_result()`: at the start of the timing block, configure the pacer on first call (`st.pacer.configure(interval_ms, now_ms())` if `interval_ms() == 0`), then call `auto decision = st.pacer.tick(now_ms())`. If `decision.run_tick == false`, invoke `(deps.sleep_ms ? deps.sleep_ms : SDL_Delay)(decision.sleep_ms)` and return `Continue` without rendering and without polling input again. If `run_tick == true`, run **exactly one** sim tick (no for-loop over `ticks_to_run`) and one render.
        - Render is gated on `decision.run_render` — render only on tick frames, never on idle wakeups.
        - Emit `runtime_trace` `desktop_loop_sleep_ms` with the captured sleep value so jitter scripts can observe the actual sleep distribution.
    - `src/platform/sdl/glad_gameplay.cpp:255-257`: replace the `while (!g_frame_state().done) { game_frame(...); }` block with `run_native_game_loop(*current_screen, g_frame_state(), GameLoopDeps{});`. The helper is the one and only place that calls `SDL_Delay` for pacing in the native build.
    - `tests/test_game_loop.cpp`: update any test that asserted multi-tick catch-up behavior to assert the new single-tick contract; existing `now_ms` injection tests should pass with at most a fixture tweak.
    - Add new tests inside the `og_test_game_core` group:
        - `GameLoop.single_tick_per_call`: N=100 calls with `now_ms` advanced by exactly the interval each call asserts exactly N ticks executed, never more.
        - `GameLoop.idle_call_sleeps_remaining_interval`: inject `now_ms` returning `interval_ms / 2` past the last tick; capture `deps.sleep_ms` arg and assert it equals the remaining half-interval.
- **Implementation Details:**
    - The browser path (`run_browser_wrapper_frame()` in `src/platform/sdl/game_loop.cpp:437`) is updated separately in **Phase 5**; this phase leaves it alone except for the `pacer` field already being present on `GameLoopFrameState` (the browser path simply does not call `pacer.tick`).
    - `last_frame_time` and `accumulated_time` are kept on `GameLoopFrameState` to avoid disturbing test fixtures, but production code in `game_frame_with_result()` no longer reads them. Final removal is a future cleanup, out of scope here.
- **Verification:** Existing `GameLoopJitter` tests pass; the two new tests above pass; `scripts/run_jitter_gtests.sh` succeeds.

---

### Phase 4 — Bound networking work per tick

- **Phase Name:** `bounded-network-drain`
- **Implement Phase ID:** `phase_4_bounded_network_drain`
- **Verification Phases:**
    - ID: `phase_4_check`
    - Type: `check`
    - `bounce_target: phase_4_bounded_network_drain`
    - Purpose: prove that snapshot/keyframe storms cannot stretch a frame because both inbound drain and outbound publish are capped per tick.
    - Commands inline:
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure -R 'NetTransport|GameClient|GameServer|LocalTransportShadow'`
- **Preexisting Inputs:**
    - `src/platform/sdl/local_transport_shadow.cpp:1053-1119` (`local_transport_shadow_finish_tick`) and `:182-204` (`poll_local_transport_client`).
    - `include/openglad/gameplay/game_client.h:39-40` (`poll_messages` / `poll_messages(float)`).
    - `include/openglad/gameplay/game_server.h:131` (`poll_incoming_messages`).
    - `include/openglad/gameplay/net_constants.h` (existing `KEYFRAME_INTERVAL_TICKS` line 14, `MAX_GRID_DIRTY_TILES` line 21).
    - `tests/unit/test_net_transport*.cpp` and `tests/test_network_fixture.h` (existing transport tests — extend, don't replace).
- **New Outputs:**
    - In `include/openglad/gameplay/net_constants.h`:
        ```cpp
        inline constexpr int MAX_INBOUND_MESSAGES_PER_TICK = 64;
        inline constexpr int MAX_OUTBOUND_MESSAGES_PER_TICK = 32;
        inline constexpr std::uint32_t MAX_NETWORK_BUDGET_US_PER_TICK = 2000; // 2 ms (advisory, traced not enforced)
        ```
    - New overloads:
        - `void GameClient::poll_messages(float current_render_alpha, int max_messages);`
        - `void GameServer::poll_incoming_messages(int max_messages);`
        - `int GameServer::messages_drained_last_call() const;` (so the shadow can know whether the cap fired).
- **File Changes:**
    - `include/openglad/gameplay/game_client.h`, `src/gameplay/game_client.cpp`: add the bounded overload; the existing unbounded versions delegate to the new one with `INT_MAX`.
    - `include/openglad/gameplay/game_server.h`, `src/gameplay/game_server.cpp`: same shape, plus `messages_drained_last_call()`.
    - `src/platform/sdl/local_transport_shadow.cpp`:
        - `poll_local_transport_client()` gains an `int budget` parameter and forwards.
        - `local_transport_shadow_finish_tick()` calls `poll_local_transport_client(*session.myscreen_, client, MAX_INBOUND_MESSAGES_PER_TICK)`. If the server reports it drained the full budget, emit `runtime_trace` `shadow_inbound_overflow` so tests can assert the cap fired.
    - New tests in `tests/test_local_transport_shadow_budget.cpp` (placed in the existing `og_test_picker_network` group at `CMakeLists.txt:1667` — that group already links the shadow runtime). Tests:
        - Push 1000 fake snapshot messages; assert exactly `MAX_INBOUND_MESSAGES_PER_TICK` are drained per call and the un-drained queue persists.
        - Run repeated ticks until the queue drains; assert no tick processes more than the cap.
        - Assert exactly one `shadow_inbound_overflow` trace on the first tick of the storm and at least one across the storm.
- **Implementation Details:**
    - The exact budget values (64 / 32 / 2 ms) are tunable but live in the header so they are visible in code review and in jitter runs.
    - When the cap is hit, the un-drained queue stays in the transport and is consumed next tick. Snapshots are idempotent under sequence numbers (already true), so deferred application is safe.
    - The 2-ms time budget is advisory only in this phase (recorded in trace but not enforced by an additional clock check inside the loop). Hard time-budget enforcement is deferred — the message-count cap is the primary jitter safety net.
- **Verification:** New unit tests pass; existing `og_test_picker_network` and the inprocess transport tests pass unchanged; `runtime_trace` records `shadow_inbound_overflow` at least once in the new storm test.

---

### Phase 5 — Browser pacing and headless server pacing alignment

- **Phase Name:** `browser-and-headless-pacer`
- **Implement Phase ID:** `phase_5_browser_and_headless_pacer`
- **Verification Phases:**
    - ID: `phase_5_check`
    - Type: `check`
    - `bounce_target: phase_5_browser_and_headless_pacer`
    - Purpose: prove the browser wrapper and the headless server both source their frame interval from the same `target_fps` knob.
    - Commands inline:
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure -R 'GameLoopWrapper|GameLoopJitter|HeadlessServer'`
        - `bash scripts/test_headless_server_cli.sh`
        - `bash scripts/test_emscripten_build.sh` (the script must self-detect a missing emsdk and exit 0 with a printed skip message; the checker treats any non-zero exit as failure).
- **Preexisting Inputs:**
    - `src/platform/sdl/glad.cpp:243-340` (`emscripten_frame_wrapper`) and `src/platform/sdl/game_loop.cpp:437-469` (`run_browser_wrapper_frame`).
    - `src/core/frame_pacing.cpp:13-60` (`browser_frame_target_interval_ms` and `step_browser_frame_pacing` — kept; cadence input changes).
    - `src/server/server_main.cpp:360-380` (existing `sleep_until` headless pacer).
    - The Phase 1 `target_fps_from_cfg()` getter and Phase 2 `FrameDeadlinePacer`.
- **New Outputs:**
    - Browser pacer now sources its target interval from `og::core::target_frame_interval_ms(session.target_fps_)` instead of `browser_frame_target_interval_ms(timer_wait, speed_factor)`. The 16 ms browser floor at `src/core/frame_pacing.cpp:22` remains (the browser cannot tick faster than rAF anyway).
    - Headless server pacer (`src/server/server_main.cpp`) uses `og::core::target_frame_interval_ms(target_fps_from_cfg(cfg))` instead of `wait_ticks * TIMER_WAIT_TO_MS`.
    - Headless server CLI gains `--fps <n>`: when present, it calls `cfg.apply_setting("graphics", "target_fps", arg)` before reading the value back via `target_fps_from_cfg(cfg)`.
- **File Changes:**
    - `src/core/frame_pacing.cpp` and `include/openglad/core/frame_pacing.h`: change `browser_frame_target_interval_ms()` to take `int fps` instead of `(short timer_wait, float speed_factor)`. Update all callers in the same change; do not add a compat overload. Update `tests/unit/test_game_loop_wrapper.cpp` (in `og_unit_sim`) and any other test in the same change.
    - `src/platform/sdl/glad.cpp:243-340` (`emscripten_frame_wrapper`): replace the `timer_wait`/`speed_factor` reads with `og::runtime::current_session->target_fps_`. Strict single-step contract: each call to `run_browser_wrapper_frame` may execute at most one sim tick (it already does); add a `runtime_trace` `browser_frame_step` that emits at most once per frame and update `tests/unit/test_game_loop_wrapper.cpp` to assert the count.
    - `src/server/server_main.cpp:360-380`: replace `wait_ticks * TIMER_WAIT_TO_MS` with `og::core::target_frame_interval_ms(...)`. Parse `--fps <n>` in the existing CLI argument loop (alongside the existing args) and call `cfg.apply_setting("graphics", "target_fps", value)` before computing the interval. The integer is then read via `target_fps_from_cfg(cfg)` (which clamps).
    - `tests/test_game_loop.cpp:1595-1690`: update `GameLoopJitter.browser_wrapper_*` tests to drive the new fps-based input shape; assertions on rounded interval should use `target_frame_interval_ms()`.
    - `tests/unit/test_game_loop_wrapper.cpp`: same.
- **Implementation Details:**
    - The browser tick is still scheduled by the browser's rAF loop, so the deadline pacer is **not** used in the browser to *sleep*; the browser uses `step_browser_frame_pacing()` to decide whether the rAF callback should run a tick. What changes is the input — the rAF helper now reads `target_fps_` from the session instead of `timer_wait` and `speed_factor`.
    - `world.timer_wait` continues to be transmitted in snapshots (sim determinism). Visual cadence is no longer slaved to it.
    - On the headless server, the deadline-sleep loop keeps its existing structure but pulls the fps value from cfg at startup; in-game changes to the target fps require a server restart (acceptable for headless).
- **Verification:** All `GameLoopJitter.browser_*` tests pass with the new signatures; `scripts/test_headless_server_cli.sh` runs and exits cleanly; `scripts/test_emscripten_build.sh` either compiles cleanly or self-skips with exit 0.

---

### Phase 6 — End-to-end jitter measurement and final docs

- **Phase Name:** `jitter-measurement-final`
- **Implement Phase ID:** `phase_6_jitter_measurement_final`
- **Verification Phases:**
    - ID: `phase_6_check`
    - Type: `check`
    - `bounce_target: phase_6_jitter_measurement_final`
    - Purpose: confirm a measurable jitter regression test is in place and that the full test suite is green end-to-end.
    - Commands inline:
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure`
        - `bash scripts/run_jitter_gtests.sh build/ci-test/og_test_game_core build/ci-test/og_test_view build/ci-test/og_unit_sim`
        - `bash scripts/check_vendor_leaks.sh`
- **Preexisting Inputs:**
    - All artifacts from Phases 1-5 (consumed in place).
    - `scripts/analyze_jitter_metrics.mjs` and `scripts/run_jitter_gtests.sh`.
    - `cfg/openglad.yaml` (already updated in Phase 1).
- **New Outputs:**
    - New deterministic jitter regression test `tests/test_frame_pacing_jitter.cpp` (placed in the `og_test_game_core` group at `CMakeLists.txt:1593-1606`):
        - Drives `game_frame_with_result()` for 600 simulated frames at 72 fps with an injected `now_ms` that advances by exactly `target_frame_interval_ms(72)` each call.
        - Captures every value passed to `deps.sleep_ms` and every `desktop_loop_sleep_ms` runtime trace.
        - Asserts `max(observed_interval) - min(observed_interval) <= 1` ms (i.e., zero structural jitter; only integer rounding remains).
        - Asserts exactly 600 sim ticks ran (no catch-up bursts, no skipped ticks).
        - Repeats the same flow at 60 fps in a second test case to prove the knob is honored.
    - Jitter assertion under storm: extend the budgeted-drain test from Phase 4 (or add a new case in `tests/test_frame_pacing_jitter.cpp`) asserting that with 1000 inbound messages queued, `local_transport_shadow_finish_tick` returns within a bound proportional to `MAX_INBOUND_MESSAGES_PER_TICK` rather than to the queue length, measured against an injected clock.
- **File Changes:**
    - New `tests/test_frame_pacing_jitter.cpp`; add to the `og_test_game_core` group at `CMakeLists.txt:1593`.
    - **Do not** add markdown summary docs; per repo rules, documentation files are not created unless explicitly requested.
- **Implementation Details:**
    - The jitter regression test must be wall-clock-independent — it uses an injected clock so it is reproducible on any CI runner including ASan and UBSan.
    - The two fps cases (60 and 72) live in the same compilation unit; both use the injected-clock harness from `tests/test_game_loop.cpp` patterns.
- **Verification:** Full `ctest --preset ci-test` is green; `scripts/run_jitter_gtests.sh` exits zero; `scripts/check_vendor_leaks.sh` exits zero (no third-party header leakage from the new code, which only touches `og::core`, `og::runtime`, and `og::sim` public headers).

---

## 4. Critical Files

Implementation will touch the following files. New files are marked with **NEW**.

- **NEW** `include/openglad/core/frame_rate_config.h` — `target_fps_from_cfg`, `apply_target_fps_to_cfg`, `target_frame_interval_ms`, fps constants. (Phase 1)
- **NEW** `src/core/frame_rate_config.cpp` — implementation of the above. (Phase 1)
- `include/openglad/core/frame_pacing.h` — adds `FrameDeadlinePacer` class & `FrameDeadlineDecision`. Browser helper signature changes to fps-based in Phase 5. (Phases 2, 5)
- `src/core/frame_pacing.cpp` — implementation of `FrameDeadlinePacer`; browser helper retargeted to fps. (Phases 2, 5)
- `include/openglad/interface/session_state.h` — adds `int target_fps_ = 72;` next to `g_game_speed_factor_` (line 76). (Phase 1)
- `include/openglad/interface/game_loop_state.h` — adds `og::core::FrameDeadlinePacer pacer;` field and a `#include <openglad/core/frame_pacing.h>`. (Phase 3)
- `include/openglad/platform/game_loop.h` — adds `GameLoopDeps::sleep_ms` callback and declares `run_native_game_loop`. (Phase 3)
- `src/platform/sdl/game_loop.cpp` — deletes `kMaxTicksPerCall` and the multi-tick accumulator; runs exactly one tick per call; sleeps to deadline; renders only on tick frames; defines `run_native_game_loop`. (Phase 3)
- `src/platform/sdl/glad_gameplay.cpp:227-257` — native `glad_main()` switches from tight `while` to `run_native_game_loop`. (Phase 3)
- `src/platform/sdl/glad.cpp:200-230, 243-340` — reads `target_fps` from cfg in `bootstrap_runtime()` (after `cfg.load_settings()` already ran in `initialize_runtime_config`); browser wrapper uses session `target_fps_`. (Phases 1, 5)
- `include/openglad/gameplay/net_constants.h` — adds `MAX_INBOUND_MESSAGES_PER_TICK`, `MAX_OUTBOUND_MESSAGES_PER_TICK`, `MAX_NETWORK_BUDGET_US_PER_TICK`. (Phase 4)
- `include/openglad/gameplay/game_client.h`, `src/gameplay/game_client.cpp` — bounded `poll_messages` overload. (Phase 4)
- `include/openglad/gameplay/game_server.h`, `src/gameplay/game_server.cpp` — bounded `poll_incoming_messages`; `messages_drained_last_call()`. (Phase 4)
- `src/platform/sdl/local_transport_shadow.cpp:182-204, 1053-1119` — bounded drain with overflow trace. (Phase 4)
- `src/server/server_main.cpp:360-380` — uses `target_frame_interval_ms`; accepts `--fps`. (Phase 5)
- **NEW** `tests/unit/test_frame_rate_config.cpp` — unit tests for the cfg knob. (Phase 1)
- **NEW** `tests/unit/test_frame_deadline_pacer.cpp` — unit tests for the pacer. (Phase 2)
- **NEW** `tests/test_local_transport_shadow_budget.cpp` — bounded drain tests, in `og_test_picker_network`. (Phase 4)
- **NEW** `tests/test_frame_pacing_jitter.cpp` — end-to-end deterministic jitter regression, in `og_test_game_core`. (Phase 6)
- `tests/test_game_loop.cpp:1595-1690` — update `GameLoopJitter` tests for the new fps-based input shape; add new single-tick / idle-sleep tests. (Phases 3, 5)
- `tests/unit/test_game_loop_wrapper.cpp` — update for browser helper signature change. (Phase 5)
- `CMakeLists.txt` — adds `${SRC_DIR}/core/frame_rate_config.cpp` to `OG_CORE_SOURCES`; adds new `og_add_unit_group(og_unit_core …)` block; appends new test files to `og_test_game_core` and `og_test_picker_network`. (Phases 1-6)
- `cfg/openglad.yaml` — adds `target_fps: 72` under `graphics:`. (Phase 1)

---

## 5. Final Verification

After Phase 6, the implementation is complete when **all** of the following hold:

1. **Build is green for the standard preset.**
   ```bash
   cmake --preset ci-test
   cmake --build --preset ci-test
   ctest --preset ci-test --output-on-failure
   ```
   Every test group (`og_unit_*`, `og_test_*`) passes. ASan preset (`ci-asan`) also passes:
   ```bash
   cmake --preset ci-asan && cmake --build --preset ci-asan && ctest --preset ci-asan --output-on-failure
   ```

2. **Jitter scripts pass.**
   ```bash
   bash scripts/run_jitter_gtests.sh \
     build/ci-test/og_test_game_core \
     build/ci-test/og_test_view \
     build/ci-test/og_unit_sim
   ```
   The new `FramePacingJitter.*` tests in `og_test_game_core` are exercised and pass.

3. **Vendor leak check passes.** `bash scripts/check_vendor_leaks.sh` exits zero.

4. **Headless server smoke.** `bash scripts/test_headless_server_cli.sh` succeeds; the server respects `--fps <n>`.

5. **Web build compiles.** `bash scripts/test_emscripten_build.sh` succeeds (or cleanly self-skips with exit 0 when emsdk is absent).

6. **Manual native run.** Building `dev-debug` and running `./build/dev-debug/openglad` shows steady frame-time (visually: no stutter; instrumentally: `desktop_loop_sleep_ms` traces stay within ±1 ms of the configured interval). Setting `graphics.target_fps: 60` in `cfg/openglad.yaml` and restarting yields a 60-fps cadence; `graphics.target_fps: 144` yields a 144-fps cadence (clamped to `[10, 240]`).

7. **Determinism preserved.** `og_test_snapshot_safety`, `og_test_snapshot_benchmark`, and `og_test_picker_network` continue to pass — proving that decoupling pacing from `world.timer_wait` did not break sim/network determinism, since `world.timer_wait` is still serialized end-to-end.

8. **Behavioral contract.** A single call to `game_frame_with_result()` runs **at most one** sim tick. There is no code path that runs 2-4 ticks in a single frame. The deadline pacer never advances by more than one interval per call. Network drain inside the shadow is capped per tick.

When all of the above hold, the goal — "no possibility for jitter: each frame should run in the exact same amount of time, fps configurable" — is satisfied within the limits of the host OS scheduler.
