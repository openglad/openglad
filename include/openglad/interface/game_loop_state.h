#pragma once

#include <openglad/core/frame_pacing.h>
#include <openglad/gameplay/input_state.h>

#include <cstdint>

// Lightweight frame-state struct used by GameSession.
// Split out of game_loop.h to avoid pulling SDL.h into the include chain
// (base.h → game_session.h → game_loop_state.h).
struct GameLoopFrameState {
    bool done = false;
    bool initialized = false;
    short currentcycle = 0;
    short cycletime = 3;
    // Legacy accumulator fields. Production code in game_frame_with_result()
    // no longer reads these; the deadline pacer below replaces them. Kept
    // here so existing test fixtures and the browser wrapper can continue
    // touching them without churn.
    std::uint32_t last_frame_time = 0;
    std::uint32_t accumulated_time = 0;
    bool has_pending_input = false;
    InputState pending_input = {};
    og::core::FrameDeadlinePacer pacer;
};
