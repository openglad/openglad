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

// ---------------------------------------------------------------------------
// SessionScope isolation tests
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_session_scope_activates_and_restores_globals)
{
    screen* baseline_screen = myscreen;
    options* baseline_prefs = theprefs;

    // Headless: skip prefs allocation (options constructor needs PhysFS).
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;
    session_cfg.install_global_context = false;
    og::runtime::GameSession session(session_cfg);

    // Before activation: globals should still be baseline
    OG_ASSERT(myscreen == baseline_screen);
    OG_ASSERT(theprefs == baseline_prefs);

    {
        auto scope = session.activate();
        // During activation: myscreen should be session's screen (nullptr since no alloc)
        OG_ASSERT(myscreen == session.screen_ptr());
        // theprefs should be nullptr since we didn't allocate prefs
        OG_ASSERT(theprefs == baseline_prefs);
    }

    // After scope destruction: globals should be restored
    OG_ASSERT(myscreen == baseline_screen);
    OG_ASSERT(theprefs == baseline_prefs);
}

OG_UNIT_TEST(test_session_scope_context_isolation)
{
    og::runtime::GameSession::Config cfg1;
    cfg1.allocate_screen = false;
    cfg1.allocate_prefs = false;
    cfg1.install_legacy_globals = false;
    cfg1.install_global_context = false;
    cfg1.allocate_seeded_rng = true;
    cfg1.rng_seed = 42;
    og::runtime::GameSession session1(cfg1);

    og::runtime::GameSession::Config cfg2;
    cfg2.allocate_screen = false;
    cfg2.allocate_prefs = false;
    cfg2.install_legacy_globals = false;
    cfg2.install_global_context = false;
    cfg2.allocate_seeded_rng = true;
    cfg2.rng_seed = 99;
    og::runtime::GameSession session2(cfg2);

    // Generate values from session1
    Uint32 s1_val;
    {
        auto scope = session1.activate();
        s1_val = ctx().rng->next(1000);
    }

    // Generate values from session2 - should be independent
    Uint32 s2_val;
    {
        auto scope = session2.activate();
        s2_val = ctx().rng->next(1000);
    }

    // Different seeds should (almost certainly) produce different values
    // This is probabilistic but seed 42 vs 99 on first call should differ.
    OG_ASSERT(s1_val != s2_val);
}

OG_UNIT_TEST(test_multiple_sessions_coexist_headless)
{
    // Create multiple sessions simultaneously without conflicts
    screen* baseline_screen = myscreen;
    options* baseline_prefs = theprefs;

    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;
    session_cfg.install_global_context = false;
    session_cfg.allocate_seeded_rng = true;

    constexpr int N = 5;
    std::vector<std::unique_ptr<og::runtime::GameSession>> sessions;
    for (int i = 0; i < N; i++) {
        session_cfg.rng_seed = static_cast<Uint32>(i * 100);
        sessions.push_back(std::make_unique<og::runtime::GameSession>(session_cfg));
    }

    // Verify each session has independent RNG state
    std::vector<Uint32> first_values;
    for (int i = 0; i < N; i++) {
        auto scope = sessions[static_cast<size_t>(i)]->activate();
        first_values.push_back(ctx().rng->next(10000));
    }

    // Verify the values are not all the same (different seeds)
    bool all_same = true;
    for (int i = 1; i < N; i++) {
        if (first_values[static_cast<size_t>(i)] != first_values[0]) {
            all_same = false;
            break;
        }
    }
    OG_ASSERT(!all_same);

    // Destroy all sessions
    sessions.clear();

    // Globals should be unchanged
    OG_ASSERT(myscreen == baseline_screen);
    OG_ASSERT(theprefs == baseline_prefs);
}

OG_UNIT_TEST(test_session_scope_nested_activation)
{
    // Test nested SessionScope: inner scope should restore to outer session.
    og::runtime::GameSession::Config cfg1;
    cfg1.allocate_screen = false;
    cfg1.allocate_prefs = false;
    cfg1.install_legacy_globals = false;
    cfg1.install_global_context = false;
    cfg1.allocate_seeded_rng = true;
    cfg1.rng_seed = 111;
    og::runtime::GameSession session1(cfg1);

    og::runtime::GameSession::Config cfg2;
    cfg2.allocate_screen = false;
    cfg2.allocate_prefs = false;
    cfg2.install_legacy_globals = false;
    cfg2.install_global_context = false;
    cfg2.allocate_seeded_rng = true;
    cfg2.rng_seed = 222;
    og::runtime::GameSession session2(cfg2);

    screen* baseline = myscreen;

    {
        auto scope1 = session1.activate();
        // session1's context should be active
        IRandom* rng1 = ctx().rng;
        OG_ASSERT(rng1 == session1.context().rng);

        {
            auto scope2 = session2.activate();
            // session2's context should now be active
            IRandom* rng2 = ctx().rng;
            OG_ASSERT(rng2 == session2.context().rng);
            OG_ASSERT(rng2 != rng1);
        }
        // After inner scope: session1 should be active again
        OG_ASSERT(ctx().rng == rng1);
    }
    // After outer scope: baseline restored
    OG_ASSERT(myscreen == baseline);
}

OG_UNIT_TEST(test_session_frame_state_independence)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;
    session_cfg.install_global_context = false;
    og::runtime::GameSession session1(session_cfg);
    og::runtime::GameSession session2(session_cfg);

    // Modify frame states independently
    session1.frame_state().done = true;
    session1.frame_state().currentcycle = 42;

    session2.frame_state().done = false;
    session2.frame_state().currentcycle = 7;

    // Verify they're independent
    OG_ASSERT(session1.frame_state().done == true);
    OG_ASSERT(session1.frame_state().currentcycle == 42);
    OG_ASSERT(session2.frame_state().done == false);
    OG_ASSERT(session2.frame_state().currentcycle == 7);
}
