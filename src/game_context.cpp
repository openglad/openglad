#include "game_context.h"
#include "base.h"
#include "gparser.h"
#include "input.h"
#include "view.h" // options definition for GameContext::~GameContext

// The existing global random() function (defined in screen.cpp)
Uint32 random(Uint32 x);

GameContext::~GameContext() = default;

Uint32 ProductionRandom::next(Uint32 max_exclusive)
{
    return random(max_exclusive);
}

// ---------------------------------------------------------------------------
// PlayerInput helpers
// ---------------------------------------------------------------------------

int PlayerInput::move_x() const
{
    int dx = 0;
    if (held[static_cast<int>(InputKey::Left)] ||
        held[static_cast<int>(InputKey::UpLeft)] ||
        held[static_cast<int>(InputKey::DownLeft)])
        dx -= 1;
    if (held[static_cast<int>(InputKey::Right)] ||
        held[static_cast<int>(InputKey::UpRight)] ||
        held[static_cast<int>(InputKey::DownRight)])
        dx += 1;
    return dx;
}

int PlayerInput::move_y() const
{
    int dy = 0;
    if (held[static_cast<int>(InputKey::Up)] ||
        held[static_cast<int>(InputKey::UpLeft)] ||
        held[static_cast<int>(InputKey::UpRight)])
        dy -= 1;
    if (held[static_cast<int>(InputKey::Down)] ||
        held[static_cast<int>(InputKey::DownLeft)] ||
        held[static_cast<int>(InputKey::DownRight)])
        dy += 1;
    return dy;
}

// ---------------------------------------------------------------------------
// InputState
// ---------------------------------------------------------------------------

void InputState::clear()
{
    for (auto& p : players) {
        for (int i = 0; i < NUM_INPUT_KEYS; i++) {
            p.held[i] = false;
            p.pressed[i] = false;
        }
    }
    quit_requested = false;
}

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
extern options* theprefs;

namespace
{
class LegacyConfigContextService final : public IConfigContextService
{
public:
    cfg_store* config() override { return &cfg; }
};

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
} // namespace

static LegacyConfigContextService s_default_config_service;
static LegacyRenderContextService s_default_render_service;
static LegacyInputContextService s_default_input_service(s_default_context);

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
    if (!s_default_context.render_service)
        s_default_context.render_service = &s_default_render_service;
    if (!s_default_context.input_service)
        s_default_context.input_service = &s_default_input_service;

    return s_default_context;
}

void set_global_context(GameContext* context)
{
    s_active_context = context;
}
