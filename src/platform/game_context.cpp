/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>

#include <cassert>

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

GameContext& ctx()
{
    assert(og::runtime::current_session != nullptr);
    return og::runtime::current_session->ctx_;
}

namespace og::runtime {
std::chrono::steady_clock::time_point* active_session_reset_time()
{
    if (!current_session)
        return nullptr;
    return &current_session->reset_time_;
}
} // namespace og::runtime

namespace
{
struct TestContextSnapshot {
    IRandom* rng = nullptr;
    InputState input = {};
};

thread_local bool s_test_context_active = false;
thread_local TestContextSnapshot s_test_context_snapshot{};
} // namespace

void push_test_context(GameContext* context)
{
    GameContext& active = ctx();
    if (!s_test_context_active) {
        s_test_context_snapshot = TestContextSnapshot{
            .rng = active.rng,
            .input = active.input,
        };
        s_test_context_active = true;
    }

    if (context) {
        if (context->rng)
            active.rng = context->rng;
        active.input = context->input;
        set_gameplay_rng_override(&context->rng);
    } else {
        set_gameplay_rng_override(nullptr);
    }
}

void pop_test_context()
{
    if (!s_test_context_active)
        return;

    GameContext& active = ctx();
    active.rng = s_test_context_snapshot.rng;
    active.input = s_test_context_snapshot.input;
    s_test_context_active = false;
    set_gameplay_rng_override(active.rng ? &active.rng : nullptr);
}
