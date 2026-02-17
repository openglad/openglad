/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/runtime/game_context.h>
#include <openglad/data/gparser.h>
#include <openglad/sim/sim_event_log.h>

class options;

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

screen* GameContext::active_screen() const
{
    if (render_service) {
        if (screen* s = render_service->game_screen())
            return s;
    }
    return game_screen;
}

options* GameContext::active_prefs() const
{
    if (render_service) {
        if (options* p = render_service->prefs())
            return p;
    }
    return prefs;
}

cfg_store* GameContext::active_config() const
{
    if (config_service) {
        if (cfg_store* c = config_service->config())
            return c;
    }
    return config;
}

InputState* GameContext::active_input()
{
    if (input_service) {
        if (InputState* in = input_service->input_state())
            return in;
    }
    return &input;
}

void GameContext::poll_input()
{
    if (input_service) {
        input_service->poll_input();
        return;
    }
    input_state_from_sdl(input);
}

// ---------------------------------------------------------------------------
// Global context singleton
// ---------------------------------------------------------------------------

// Default production RNG instance
static ProductionRandom s_production_rng;

// The default global context, initialized lazily from existing globals
static GameContext s_default_context;
static GameContext* s_active_context = nullptr;

// Forward-declare the globals this wraps
extern screen* myscreen;

namespace
{
class LegacyConfigContextService final : public IConfigContextService
{
public:
    cfg_store* config() override { return &cfg; }
};
} // namespace

static LegacyConfigContextService s_default_config_service;

GameContext& ctx()
{
    if (s_active_context)
        return *s_active_context;

    // Lazily populate from existing globals
    s_default_context.game_screen = myscreen;
    s_default_context.config = &cfg;
    if (!s_default_context.rng)
        s_default_context.rng = &s_production_rng;
    if (!s_default_context.config_service)
        s_default_context.config_service = &s_default_config_service;

    return s_default_context;
}

void set_global_context(GameContext* context)
{
    s_active_context = context;
}
