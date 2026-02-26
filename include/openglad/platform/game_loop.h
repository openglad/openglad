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

#include <functional>

#include <openglad/platform/game_loop_state.h>

class screen;
union SDL_Event;

struct GameLoopDeps {
    // If null, defaults will be used (SDL_PollEvent, handle_events).
    std::function<int(SDL_Event*)> poll_event;
    std::function<void(const SDL_Event&)> handle_event;

    // Optional: allows tests to bypass expensive rendering even in non-TESTING builds.
    bool enable_render = true;
    bool enable_event_poll = true;
    // When false, skip the per-frame time_delay() call.  Used by multi-session
    // demos that run many sessions per display frame and apply their own
    // frame-rate cap externally.
    bool enable_frame_timing = true;
};

enum class GameFrameResult {
    Continue = 0,
    Done,
    AbortedMission
};

GameFrameResult game_frame_with_result(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps = {});

// Runs one frame of the main game loop. Returns true when the mission is done.
bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps = {});
