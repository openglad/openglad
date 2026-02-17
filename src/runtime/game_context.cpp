/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/runtime/game_context.h>
#include <openglad/data/gparser.h>
#ifndef OPENGLAD_HEADLESS
#include <openglad/input/input.h>
#include <openglad/render/view.h> // options definition for GameContext::~GameContext
#else
// Headless: unique_ptr<options> destructor needs a complete type.
// The text client never creates an options object (prefs stays nullptr),
// so an empty definition is safe.
class options {};
#endif
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

#ifndef OPENGLAD_HEADLESS
void input_state_from_sdl(InputState& out)
{
    for (int p = 0; p < MAX_PLAYERS; p++) {
        // Save previous held state to detect press edges
        bool was_held[NUM_INPUT_KEYS];
        for (int k = 0; k < NUM_INPUT_KEYS; k++)
            was_held[k] = out.players[p].held[k];

        // Sample current held state from SDL
        for (int k = 0; k < NUM_INPUT_KEYS; k++) {
            out.players[p].held[k] = isPlayerHoldingKey(p, k);
            // Pressed = held now but wasn't held last frame
            out.players[p].pressed[k] = out.players[p].held[k] && !was_held[k];
        }
    }
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
    return prefs.get();
}
#endif

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
#ifndef OPENGLAD_HEADLESS
    input_state_from_sdl(input);
#endif
}

// ---------------------------------------------------------------------------
// Global context singleton
// ---------------------------------------------------------------------------

// Default production RNG instance
static ProductionRandom s_production_rng;

// The default global context, initialized lazily from existing globals
static GameContext s_default_context;
static GameContext* s_active_context = nullptr;

#ifndef OPENGLAD_HEADLESS
// Forward-declare the globals this wraps (SDL build only)
extern screen* myscreen;
extern options* theprefs;
#endif

namespace
{
class LegacyConfigContextService final : public IConfigContextService
{
public:
    cfg_store* config() override { return &cfg; }
};

#ifndef OPENGLAD_HEADLESS
class LegacyRenderContextService final : public IRenderContextService
{
public:
    screen* game_screen() override { return myscreen; }
    options* prefs() override { return theprefs; }
};

class LegacyInputContextService final : public IInputContextService
{
public:
    explicit LegacyInputContextService(GameContext& context) : context_(context) {}

    InputState* input_state() override { return &context_.input; }

    void poll_input() override { input_state_from_sdl(context_.input); }

private:
    GameContext& context_;
};
#endif
} // namespace

static LegacyConfigContextService s_default_config_service;
#ifndef OPENGLAD_HEADLESS
static LegacyRenderContextService s_default_render_service;
static LegacyInputContextService s_default_input_service(s_default_context);
#endif

GameContext& ctx()
{
    if (s_active_context)
        return *s_active_context;

    // Lazily populate from existing globals
#ifndef OPENGLAD_HEADLESS
    s_default_context.game_screen = myscreen;
#endif
    s_default_context.config = &cfg;
    if (!s_default_context.rng)
        s_default_context.rng = &s_production_rng;
    if (!s_default_context.config_service)
        s_default_context.config_service = &s_default_config_service;
#ifndef OPENGLAD_HEADLESS
    if (!s_default_context.render_service)
        s_default_context.render_service = &s_default_render_service;
    if (!s_default_context.input_service)
        s_default_context.input_service = &s_default_input_service;
#endif

    return s_default_context;
}

void set_global_context(GameContext* context)
{
    s_active_context = context;
}
