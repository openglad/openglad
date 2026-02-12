/*
 * Minimal, test-friendly wrapper around the main per-frame game loop.
 *
 * Goals:
 * - Keep existing gameplay behavior identical.
 * - Provide a seam for tests to step frames with injected event sources.
 *
 * Non-goals:
 * - Remove globals like `myscreen`.
 * - Rewrite subsystems (walker/screen/etc).
 */

#pragma once

#include "SDL.h"
#include <functional>

class screen;

struct GameLoopFrameState {
    bool done = false;
    bool initialized = false;
    short currentcycle = 0;
    short cycletime = 3;

#ifdef __EMSCRIPTEN__
    Uint32 last_frame_time = 0;
    Uint32 accumulated_time = 0;
#endif
};

struct GameLoopDeps {
    // If null, defaults will be used (SDL_PollEvent, handle_events).
    std::function<int(SDL_Event*)> poll_event;
    std::function<void(const SDL_Event&)> handle_event;

    // Optional: allows tests to bypass expensive rendering even in non-TESTING builds.
    bool enable_render = true;
    bool enable_event_poll = true;
};

enum class GameFrameResult {
    Continue = 0,
    Done,
    AbortedMission
};

GameFrameResult game_frame_with_result(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps = {});

// Runs one frame of the main game loop. Returns true when the mission is done.
bool game_frame(screen& s, GameLoopFrameState& st, const GameLoopDeps& deps = {});
