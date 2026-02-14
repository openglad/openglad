#include <openglad/runtime/game_session.h>

#include <openglad/legacy/base.h> // myscreen
#include <openglad/render/view.h> // theprefs

#include "unit.h"

OG_UNIT_TEST(test_game_session_headless_restores_legacy_globals)
{
    screen* prev_screen = myscreen;
    options* prev_prefs = theprefs;

    {
        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;
        session_cfg.install_global_context = true;
        og::runtime::GameSession session(session_cfg);

        OG_ASSERT(myscreen == nullptr);
        OG_ASSERT(theprefs == nullptr);
        OG_ASSERT(session.context().rng != nullptr);
    }

    OG_ASSERT(myscreen == prev_screen);
    OG_ASSERT(theprefs == prev_prefs);
}

OG_UNIT_TEST(test_game_session_seeded_rng_is_deterministic)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.install_legacy_globals = false;
    session_cfg.install_global_context = false;
    session_cfg.allocate_prefs = false;
    session_cfg.allocate_seeded_rng = true;
    session_cfg.rng_seed = 123u;
    og::runtime::GameSession session(session_cfg);

    const Uint32 a0 = session.context().rng->next(1000);
    const Uint32 a1 = session.context().rng->next(1000);
    const Uint32 a2 = session.context().rng->next(1000);

    og::runtime::GameSession session2(session_cfg);
    const Uint32 b0 = session2.context().rng->next(1000);
    const Uint32 b1 = session2.context().rng->next(1000);
    const Uint32 b2 = session2.context().rng->next(1000);

    OG_ASSERT(a0 == b0);
    OG_ASSERT(a1 == b1);
    OG_ASSERT(a2 == b2);
}
