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

#include <openglad/interface/game_loop_state.h>

class screen;

struct GameLoopDeps {
    // If null, defaults will be used (SDL_PollEvent, handle_events).
    std::function<int(SDL_Event*)> poll_event;
    std::function<void(const SDL_Event&)> handle_event;
    std::function<void(screen&)> after_act;
    std::function<std::uint32_t()> now_ms;

    // When non-zero, pins the sim tick interval in milliseconds
    // (for example og::sim::DEFAULT_SIM_TICK_MS). When zero, derive the
    // interval from world.timer_wait for backward compatibility.
    std::uint32_t fixed_tick_ms = 0;

    // Optional: allows tests to bypass expensive rendering even in non-TESTING builds.
    bool enable_render = true;
    bool enable_event_poll = true;
    // When false, skip internal accumulator pacing and run at most one sim tick
    // per call. Used by multi-session demos that apply their own pacing.
    bool enable_frame_timing = true;
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
