# Phase 11: Decouple Sim Tick from Render Frame

> **See also:** [Context (Sim Tick Rate)](docs/plans/networking/common/context.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

**Medium refactor.** The Emscripten build has a reference accumulator implementation, but it lives in a different function (`emscripten_frame_wrapper()` in `glad.cpp:133-223`) from the native game loop (`game_frame_with_result()` in `game_loop.cpp:39-168`). This phase restructures the native game loop — not just removing `#ifdef` guards.

Currently `game_frame_with_result()` does exactly 1 tick (`s.act()` at line 67) + 1 render (`s.redraw()` at line 81) per call on native, paced by `time_delay()` (`src/core/util.cpp:132-152`, converts delay ticks at 13.6ms each to microseconds, sleeps then spin-waits for precision). The Emscripten accumulator is a separate code path that can't be copy-pasted — it drives a state machine rather than a blocking loop.

**Complexity notes:**
- `game_frame_with_result()` currently returns `GameFrameResult` for a single frame. With an accumulator, it may execute 0-N ticks per call (0 if not enough time has accumulated, >1 if the frame took long). The return value semantics need to handle "done" detected during any of those ticks.
- Input polling (`ctx().poll_input()` at line 135, `s.process_input()` at line 138) currently happens once per call, AFTER the tick. With an accumulator, input should be polled once per call (before the tick loop), and each tick consumes the same input snapshot. This matches the networking model where input is sampled once and sent to the server.
- The `time_delay()` FPS cap (lines 152-164) becomes unnecessary when the accumulator manages timing. But `timer_wait` still determines the tick interval: `tick_interval_ms = timer_wait * 13.6f`.

**Changes:**
- Remove `#ifdef __EMSCRIPTEN__` guards from `last_frame_time`/`accumulated_time` fields in `GameLoopFrameState` (`include/openglad/interface/game_loop_state.h`) — make them available on all platforms
- Add `uint32_t fixed_tick_ms` to `GameLoopDeps` (`include/openglad/platform/game_loop.h:22-34`). Default to `DEFAULT_SIM_TICK_MS` from `net_constants.h`. When 0, derive from `timer_wait * TIMER_WAIT_TO_MS` (backward compat with current per-frame behavior).
- Restructure `game_frame_with_result()` in `src/platform/sdl/game_loop.cpp`:
  1. Measure elapsed time since last call (using `SDL_GetTicks()` or `steady_clock`)
  2. Add to accumulator
  3. Compute tick interval: `fixed_tick_ms > 0 ? fixed_tick_ms : timer_wait * TIMER_WAIT_TO_MS`
  4. While accumulator >= tick_interval: poll input (once before loop), call `s.act()`, subtract interval. Cap at 4 ticks per call to prevent spiral-of-death (if a frame takes 500ms, don't try to catch up with 6 ticks — clamp to 4 and drop the rest).
  5. Render once
  6. Remove old `time_delay()` FPS cap when accumulator is active
- Default `fixed_tick_ms=0` means "derive tick interval from timer_wait" (backward compat — existing behavior unchanged, just accumulator-driven instead of sleep-driven)

**Input timing reorder (intentional behavioral change):**
The current code polls input AFTER the tick: `s.act()` (line 67) → `ctx().poll_input()` (line 135) → `s.process_input()` (line 138). This means input from frame N is consumed in frame N+1's tick. The restructured accumulator loop polls input ONCE before the tick loop, so input from frame N is consumed in frame N's tick(s). This reduces input latency by one frame (~83ms at default speed) — **better** for networked play, and matches the networking model where input is sampled once and sent to the server before the tick. This is an intentional improvement, not a bug, but it's a subtle behavioral change that could affect speedrun timing or tight input sequences in tests.

**Verify:** Default behavior unchanged (game feels the same). With `fixed_tick_ms=83` (matching `DEFAULT_SIM_TICK_MS`), game runs at same speed. Adjust `timer_wait` up/down, verify game speed changes proportionally. All existing tests pass. If any test is sensitive to the 1-frame input latency reduction, update the test — the new timing is correct.
