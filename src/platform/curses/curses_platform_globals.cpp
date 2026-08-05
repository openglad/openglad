/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Process-level globals required by the SDL-free engine when there is no
 * GameSession owner (the headless client owns its session state directly).
 * Mirrors the definitions in src/server/server_main.cpp and the text client.
 */
#include <openglad/interface/session_state.h>
#include <openglad/interface/game_context.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace og::runtime {

static SessionState s_headless_session{};
thread_local SessionState* current_session = &s_headless_session;
std::atomic<SessionState*> primary_session{&s_headless_session};
std::atomic<GameplayContext*> primary_game{&s_headless_session.game_};

} // namespace og::runtime

// SDL builds show a modal dialog; headless just logs to stderr.
void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

// Legacy entropy source used by ProductionRandom. The deterministic sim uses
// its own per-world RNG; this only backs non-authoritative scratch randomness.
std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 12345;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}
