#include <openglad/runtime/game_session.h>

#include <openglad/data/gparser.h> // cfg
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

OG_UNIT_TEST(test_game_session_repeated_create_destroy)
{
    // Verify sessions can be created and destroyed repeatedly without leaking
    // or corrupting global state. (Headless: skip screen/prefs which need SDL/PhysFS.)
    screen* baseline_screen = myscreen;
    options* baseline_prefs = theprefs;

    for (int i = 0; i < 5; ++i) {
        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;
        session_cfg.install_global_context = true;
        session_cfg.allocate_seeded_rng = true;
        session_cfg.rng_seed = static_cast<Uint32>(i);
        og::runtime::GameSession session(session_cfg);

        OG_ASSERT(myscreen == nullptr);
        OG_ASSERT(theprefs == nullptr);
        OG_ASSERT(session.context().rng != nullptr);
        // Verify RNG works within session
        session.context().rng->next(100);
    }

    // After all sessions destroyed, globals should be restored
    OG_ASSERT(myscreen == baseline_screen);
    OG_ASSERT(theprefs == baseline_prefs);
}

OG_UNIT_TEST(test_game_session_cfg_accessible)
{
    // Verify the global cfg_store is always accessible (no session indirection needed).
    // cfg is a global object declared in gparser.h.
    (void)cfg; // Just verify it's accessible
}
