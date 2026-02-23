/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/runtime/game_context.h>
#include <openglad/sim/sim_event_log.h>

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
// Global context singleton
// ---------------------------------------------------------------------------

static ProductionRandom s_production_rng;
static GameContext s_default_context;
static GameContext* s_active_context = nullptr;

GameContext& ctx()
{
    if (s_active_context)
        return *s_active_context;

    if (!s_default_context.rng)
        s_default_context.rng = &s_production_rng;

    return s_default_context;
}

void set_global_context(GameContext* context)
{
    s_active_context = context;
}
