/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/gameplay/session_access.h>
#include <openglad/gameplay/sim_event_log.h>

// The existing global random() function (defined in screen.cpp or text_client main)
std::uint32_t random(std::uint32_t x);

GameContext::GameContext()
    : sim_events(std::make_unique<og::sim::SimEventLog>())
{}

GameContext::~GameContext() = default;

std::uint32_t ProductionRandom::next(std::uint32_t max_exclusive)
{
    return random(max_exclusive);
}

void GameContext::poll_input()
{
    input_state_from_sdl(input);
}

// ---------------------------------------------------------------------------
// Global context accessor
// ---------------------------------------------------------------------------

static thread_local GameContext* s_test_context_override = nullptr;

namespace {
GameContext& fallback_context()
{
    static thread_local GameContext fallback;
    static thread_local ProductionRandom fallback_rng;
    if (!fallback.rng)
        fallback.rng = &fallback_rng;
    if (!fallback.sim_events)
        fallback.sim_events = std::make_unique<og::sim::SimEventLog>();
    return fallback;
}
} // namespace

GameContext& ctx()
{
    if (s_test_context_override)
        return *s_test_context_override;
    og::runtime::ensure_thread_session();
    if (og::runtime::current_session)
        return og::runtime::current_session->ctx_;
    return fallback_context();
}

void set_global_context(GameContext* context)
{
    s_test_context_override = context;
}

namespace og::gameplay {

int allocate_guy_id()
{
    og::runtime::ensure_thread_session();
    if (og::runtime::current_session)
        return og::runtime::current_session->guy_id_counter_++;

    static thread_local int s_fallback_guy_id = 0;
    return s_fallback_guy_id++;
}

std::int32_t current_difficulty_index()
{
    og::runtime::ensure_thread_session();
    if (og::runtime::current_session)
        return og::runtime::current_session->current_difficulty_;
    return 1;
}

} // namespace og::gameplay
