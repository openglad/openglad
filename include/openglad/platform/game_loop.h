/*
 * Minimal, test-friendly wrapper around the main per-frame game loop.
 *
 * Goals:
 * - Keep existing gameplay behavior identical.
 * - Provide a seam for tests to step frames with injected event sources.
 *
 * Non-goals:
 * - Remove globals like `og::runtime::current_session->myscreen_`.
 * - Rewrite subsystems (walker/screen/etc).
 */

#pragma once

#include "SDL.h"
#include <cstdint>
#include <functional>

#include <openglad/core/frame_pacing.h>
#include <openglad/interface/game_loop_state.h>

class screen;

struct GameLoopDeps {
    // If null, defaults will be used (SDL_PollEvent, handle_events).
    std::function<int(SDL_Event*)> poll_event;
    std::function<void(const SDL_Event&)> handle_event;
    std::function<void(screen&)> after_act;
    std::function<std::uint32_t()> now_ms;
    // Called when the deadline pacer decides the caller should sleep until
    // the next frame deadline. Defaults to SDL_Delay. Tests inject a no-op
    // or capturing lambda to observe the sleep distribution without blocking.
    std::function<void(std::uint32_t)> sleep_ms;

    // When non-zero, pins the sim tick interval in milliseconds
    // (for example og::sim::DEFAULT_SIM_TICK_MS). When zero, derive the
    // interval from world.timer_wait * TIMER_WAIT_TO_MS / g_game_speed_factor_
    // (master-cadence default; restores the phase-12 contract).
    std::uint32_t fixed_tick_ms = 0;

    // Optional: allows tests to bypass expensive rendering even in non-TESTING builds.
    bool enable_render = true;
    bool enable_event_poll = true;
    bool enable_tick = true;
    // When false, skip internal accumulator pacing and run at most one sim tick
    // per call. Used by multi-session demos that apply their own pacing.
    bool enable_frame_timing = true;

    // Invoked once per call only when the render pacer's run_render is true.
    // Default empty — production builds rely on the in-line redraw/refresh
    // path; tests use this hook to observe render-pacer firing.
    std::function<void(screen&)> on_render;
};

enum class GameFrameResult {
    Continue = 0,
    Done,
    AbortedMission
};

namespace og::runtime::detail {

// Shared with tests so the production redraw/present sequence stays covered
// even though the TESTING build skips full rendering in game_frame_with_result().
void render_pending_redraw(screen& s, bool enable_render);

} // namespace og::runtime::detail

GameFrameResult game_frame_with_result(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps = {});

// Runs one frame of the main game loop. Returns true when the mission is done.
bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps = {});

namespace og::runtime {

// Drives the native main loop: repeatedly invokes game_frame() until the
// frame state is marked done. The deadline pacer inside game_frame_with_result
// is responsible for sleeping to the next frame deadline; this helper does
// not call SDL_Delay itself.
void run_native_game_loop(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps = {});

} // namespace og::runtime

void run_browser_wrapper_frame(screen& s,
                               GameLoopFrameState& st,
                               std::uint32_t current_time_ms,
                               const og::core::BrowserFramePacingResult& pacing,
                               const GameLoopDeps& render_deps = {},
                               const GameLoopDeps& tick_deps = {});
