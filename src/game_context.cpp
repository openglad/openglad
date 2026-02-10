#include "game_context.h"
#include "base.h"
#include "gparser.h"

// The existing global random() function (defined in screen.cpp)
Uint32 random(Uint32 x);

Uint32 ProductionRandom::next(Uint32 max_exclusive)
{
    return random(max_exclusive);
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

GameContext& ctx()
{
    if (s_active_context)
        return *s_active_context;

    // Lazily populate from existing globals
    s_default_context.game_screen = myscreen;
    s_default_context.prefs = theprefs;
    s_default_context.config = &cfg;
    if (!s_default_context.rng)
        s_default_context.rng = &s_production_rng;

    return s_default_context;
}

void set_global_context(GameContext* context)
{
    s_active_context = context;
}
