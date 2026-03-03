#pragma once

#include <cstdint>

// Lightweight frame-state struct used by GameSession.
// Split out of game_loop.h to avoid pulling SDL.h into the include chain
// (base.h → game_session.h → game_loop_state.h).
struct GameLoopFrameState {
    bool done = false;
    bool initialized = false;
    short currentcycle = 0;
    short cycletime = 3;

#ifdef __EMSCRIPTEN__
    std::uint32_t last_frame_time = 0;
    std::uint32_t accumulated_time = 0;
#endif
};
