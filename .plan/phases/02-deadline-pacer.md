# Phase 2 — Replace the variable-tick accumulator with a deadline pacer

## Phase Name
`deadline-pacer`

## Implement Phase ID
`phase_2_deadline_pacer`

## Preexisting Inputs
- `.plan/plan.md` — full implementation plan.
- `src/core/frame_pacing.cpp`, `include/openglad/core/frame_pacing.h` — existing pacing helpers (kept; new pacer lives alongside).
- `tests/test_game_loop.cpp:1595-1690` — `GameLoopJitter` suite (must keep passing; `now_ms` injected via `GameLoopDeps`).
- `src/server/server_main.cpp:360-380` — `sleep_until` deadline pattern (inspiration for the pacer).
- `include/openglad/core/runtime_trace.h:64-67` — `emit_runtime_trace` / `make_runtime_trace_record` (call directly, no macro gating).
- `include/openglad/core/frame_rate_config.h` — produced in Phase 1 (already exists when this phase runs).
- `src/core/frame_rate_config.cpp` — produced in Phase 1.
- `og_unit_core` test group in `CMakeLists.txt` — created in Phase 1.

## New Outputs
- In `include/openglad/core/frame_pacing.h`, add:
    ```cpp
    struct FrameDeadlineDecision {
        bool run_tick = false;
        bool run_render = false;
        std::uint32_t sleep_ms = 0;        // how long the caller must sleep
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
        - Else (slipped more than one interval): set `next_deadline_ms_ = now + interval_ms_`, emit a `pacer_resync` runtime trace via `emit_runtime_trace(make_runtime_trace_record("frame_pacing", "pacer_resync"))`, and return `{true, true, 0, next_deadline_ms_}` so the current frame still runs exactly one tick (drop lost time deliberately rather than burst-catch-up).
- New tests `tests/unit/test_frame_deadline_pacer.cpp` added to the `og_unit_core` group.

## File Changes
- Edit `include/openglad/core/frame_pacing.h` and `src/core/frame_pacing.cpp` to add `FrameDeadlinePacer` and `FrameDeadlineDecision`.
- New tests `tests/unit/test_frame_deadline_pacer.cpp` covering: steady-state advance, pause/resume resync (slip > 1 interval emits one `pacer_resync` and runs exactly one tick on the resync frame), configure → tick → tick determinism with injected clock, interval=0 clamps to 1, `sleep_ms` matches `interval_ms - elapsed`. Add to the `og_unit_core` group created in Phase 1.

## Implementation Details
- The class is fully defined in the header; the `pacer_resync` runtime-trace emit lives in the .cpp to keep the header free of `runtime_trace.h`.
- `FrameDeadlinePacer` is the production replacement for the multi-tick accumulator inside `ticks_to_run_this_call()` (`src/platform/sdl/game_loop.cpp:123-184`). Removal of the multi-tick path is **Phase 3**, not this phase. This phase only adds the new class and tests; it does not yet wire it into the desktop loop.

## Verification Phases
- **Phase ID:** `phase_2_check`
    - **Type:** `check`
    - **bounce_target:** `phase_2_deadline_pacer`
    - **Purpose:** confirm a single `og::core::FrameDeadlinePacer` exists, that it is purely deterministic given an injected clock, and that the existing `GameLoopJitter` tests still pass.
    - **Commands inline:**
        - `cmake --build --preset ci-test --target og_unit_core og_test_game_core`
        - `ctest --preset ci-test --output-on-failure -R 'FrameDeadlinePacer|GameLoopJitter'`

## Success Criteria
- Unit tests assert that 1000 simulated frames with a perfect clock advance `next_deadline_ms_` by exactly `1000 * interval_ms_`.
- One stalled iteration produces a one-frame skip and exactly one `pacer_resync` trace.
- `sleep_ms` is correct mid-interval.
- No call ever returns `run_tick=true` twice for one delta below `interval_ms`.
- Existing `GameLoopJitter` tests in `og_test_game_core` still pass unchanged.

## Git Commit Requirement
Before yielding control, the implementer MUST stage and commit all changes with `git add -A && git commit -m "deadline-pacer: <change>"`. Do not yield with a dirty working tree.
