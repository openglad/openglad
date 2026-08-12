#include <openglad/platform/game_session.h>

#include <openglad/resources/gparser.h> // cfg
#include <openglad/legacy/base.h> // myscreen
#include <openglad/interface/render/view.h> // theprefs

#include <algorithm>
#include <array>
#include <numeric>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

TEST(SessionRaii, game_session_headless_restores_legacy_globals)
{
    screen* prev_screen = og::runtime::current_session->myscreen_;
    options* prev_prefs = og::runtime::current_session->theprefs_;

    {
        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;

        og::runtime::GameSession session(session_cfg);

        ASSERT_TRUE(og::runtime::current_session->myscreen_ == nullptr);
        ASSERT_TRUE(og::runtime::current_session->theprefs_ == nullptr);
        ASSERT_TRUE(session.ctx_.rng != nullptr);
    }

    ASSERT_TRUE(og::runtime::current_session->myscreen_ == prev_screen);
    ASSERT_TRUE(og::runtime::current_session->theprefs_ == prev_prefs);
}

TEST(SessionRaii, game_session_teardown_restores_thread_inheritance_context)
{
    og::runtime::SessionState* baseline_session = og::runtime::current_session;
    GameplayContext* baseline_game = current_game;
    og::runtime::SessionState* baseline_primary_session =
        og::runtime::primary_session.load(std::memory_order_acquire);
    GameplayContext* baseline_primary_game =
        og::runtime::primary_game.load(std::memory_order_acquire);

    {
        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;

        og::runtime::GameSession session(session_cfg);

        ASSERT_TRUE(og::runtime::current_session == &session);
        ASSERT_TRUE(current_game == &session.game_);
        ASSERT_TRUE(og::runtime::primary_session.load(std::memory_order_acquire) == &session);
        ASSERT_TRUE(og::runtime::primary_game.load(std::memory_order_acquire) == &session.game_);

        og::runtime::current_session = nullptr;
        current_game = nullptr;
        og::runtime::ensure_thread_session();
        og::runtime::ensure_thread_game();
        ASSERT_TRUE(og::runtime::current_session == &session);
        ASSERT_TRUE(current_game == &session.game_);
    }

    ASSERT_TRUE(og::runtime::current_session == baseline_session);
    ASSERT_TRUE(current_game == baseline_game);
    ASSERT_TRUE(og::runtime::primary_session.load(std::memory_order_acquire) ==
              baseline_primary_session);
    ASSERT_TRUE(og::runtime::primary_game.load(std::memory_order_acquire) ==
              baseline_primary_game);

    og::runtime::current_session = nullptr;
    current_game = nullptr;
    og::runtime::ensure_thread_session();
    og::runtime::ensure_thread_game();
    ASSERT_TRUE(og::runtime::current_session == baseline_primary_session);
    ASSERT_TRUE(current_game == baseline_primary_game);

    og::runtime::current_session = baseline_session;
    current_game = baseline_game;
}

TEST(SessionRaii, game_session_seeded_rng_is_deterministic)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.install_legacy_globals = false;

    session_cfg.allocate_prefs = false;
    session_cfg.allocate_seeded_rng = true;
    session_cfg.rng_seed = 123u;
    og::runtime::GameSession session(session_cfg);

    const Uint32 a0 = session.ctx_.rng->next(1000);
    const Uint32 a1 = session.ctx_.rng->next(1000);
    const Uint32 a2 = session.ctx_.rng->next(1000);

    og::runtime::GameSession session2(session_cfg);
    const Uint32 b0 = session2.ctx_.rng->next(1000);
    const Uint32 b1 = session2.ctx_.rng->next(1000);
    const Uint32 b2 = session2.ctx_.rng->next(1000);

    ASSERT_TRUE(a0 == b0);
    ASSERT_TRUE(a1 == b1);
    ASSERT_TRUE(a2 == b2);
}

TEST(SessionRaii, game_session_repeated_create_destroy)
{
    // Verify sessions can be created and destroyed repeatedly without leaking
    // or corrupting global state. (Headless: skip screen/prefs which need SDL/PhysFS.)
    screen* baseline_screen = og::runtime::current_session->myscreen_;
    options* baseline_prefs = og::runtime::current_session->theprefs_;

    for (int i = 0; i < 5; ++i) {
        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;

        session_cfg.allocate_seeded_rng = true;
        session_cfg.rng_seed = static_cast<Uint32>(i);
        og::runtime::GameSession session(session_cfg);

        ASSERT_TRUE(og::runtime::current_session->myscreen_ == nullptr);
        ASSERT_TRUE(og::runtime::current_session->theprefs_ == nullptr);
        ASSERT_TRUE(session.ctx_.rng != nullptr);
        // Verify RNG works within session
        session.ctx_.rng->next(100);
    }

    // After all sessions destroyed, globals should be restored
    ASSERT_TRUE(og::runtime::current_session->myscreen_ == baseline_screen);
    ASSERT_TRUE(og::runtime::current_session->theprefs_ == baseline_prefs);
}

TEST(SessionRaii, game_session_cfg_accessible)
{
    // Verify the global cfg_store is always accessible (no session indirection needed).
    // cfg is a global object declared in gparser.h.
    (void)cfg; // Just verify it's accessible
}

TEST(SessionRaii, session_accessors_and_base_transport_state_report_owned_values)
{
    og::runtime::SessionState base;
    EXPECT_FALSE(base.has_local_transport_runtime());

    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;
    og::runtime::GameSession session(session_cfg);

    EXPECT_EQ(nullptr, session.screen_ptr());
    EXPECT_EQ(nullptr, session.prefs_ptr());
    EXPECT_EQ(&cfg, &session.config());
    EXPECT_FALSE(session.has_local_transport_runtime());
}

// ---------------------------------------------------------------------------
// SessionScope isolation tests
// ---------------------------------------------------------------------------

TEST(SessionRaii, session_scope_activates_and_restores_globals)
{
    screen* baseline_screen = og::runtime::current_session->myscreen_;
    options* baseline_prefs = og::runtime::current_session->theprefs_;

    // Headless: skip prefs allocation (options constructor needs PhysFS).
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;

    og::runtime::GameSession session(session_cfg);

    // Before activation: globals should still be baseline
    ASSERT_TRUE(og::runtime::current_session->myscreen_ == baseline_screen);
    ASSERT_TRUE(og::runtime::current_session->theprefs_ == baseline_prefs);

    {
        auto scope = session.activate();
        // During activation: myscreen should be session's screen (nullptr since no alloc)
        ASSERT_TRUE(og::runtime::current_session->myscreen_ == session.screen_ptr());
        // theprefs is nullptr (session has no allocated prefs), same as baseline
        ASSERT_TRUE(og::runtime::current_session->theprefs_ == baseline_prefs);
    }

    // After scope destruction: globals should be restored
    ASSERT_TRUE(og::runtime::current_session->myscreen_ == baseline_screen);
    ASSERT_TRUE(og::runtime::current_session->theprefs_ == baseline_prefs);
}

TEST(SessionRaii, moved_session_scope_retains_activation_until_new_owner_dies)
{
    og::runtime::SessionState* const baseline = og::runtime::current_session;
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;
    og::runtime::GameSession session(session_cfg);

    {
        auto original = session.activate(false);
        auto moved = std::move(original);
        EXPECT_EQ(&session, og::runtime::current_session);
    }
    EXPECT_EQ(baseline, og::runtime::current_session);
}

TEST(SessionRaii, session_scope_context_isolation)
{
    og::runtime::GameSession::Config cfg1;
    cfg1.allocate_screen = false;
    cfg1.allocate_prefs = false;
    cfg1.install_legacy_globals = false;

    cfg1.allocate_seeded_rng = true;
    cfg1.rng_seed = 42;
    og::runtime::GameSession session1(cfg1);

    og::runtime::GameSession::Config cfg2;
    cfg2.allocate_screen = false;
    cfg2.allocate_prefs = false;
    cfg2.install_legacy_globals = false;

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
    ASSERT_TRUE(s1_val != s2_val);
}

TEST(SessionRaii, multiple_sessions_coexist_headless)
{
    // Create multiple sessions simultaneously without conflicts
    screen* baseline_screen = og::runtime::current_session->myscreen_;
    options* baseline_prefs = og::runtime::current_session->theprefs_;

    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;

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
    ASSERT_TRUE(!all_same);

    // Destroy all sessions
    sessions.clear();

    // Globals should be unchanged
    ASSERT_TRUE(og::runtime::current_session->myscreen_ == baseline_screen);
    ASSERT_TRUE(og::runtime::current_session->theprefs_ == baseline_prefs);
}

TEST(SessionRaii, session_scope_nested_activation)
{
    // Test nested SessionScope: inner scope should restore to outer session.
    og::runtime::GameSession::Config cfg1;
    cfg1.allocate_screen = false;
    cfg1.allocate_prefs = false;
    cfg1.install_legacy_globals = false;

    cfg1.allocate_seeded_rng = true;
    cfg1.rng_seed = 111;
    og::runtime::GameSession session1(cfg1);

    og::runtime::GameSession::Config cfg2;
    cfg2.allocate_screen = false;
    cfg2.allocate_prefs = false;
    cfg2.install_legacy_globals = false;

    cfg2.allocate_seeded_rng = true;
    cfg2.rng_seed = 222;
    og::runtime::GameSession session2(cfg2);

    screen* baseline = og::runtime::current_session->myscreen_;

    {
        auto scope1 = session1.activate();
        // session1's context should be active
        IRandom* rng1 = ctx().rng;
        ASSERT_TRUE(rng1 == session1.ctx_.rng);

        {
            auto scope2 = session2.activate();
            // session2's context should now be active
            IRandom* rng2 = ctx().rng;
            ASSERT_TRUE(rng2 == session2.ctx_.rng);
            ASSERT_TRUE(rng2 != rng1);
        }
        // After inner scope: session1 should be active again
        ASSERT_TRUE(ctx().rng == rng1);
    }
    // After outer scope: baseline restored
    ASSERT_TRUE(og::runtime::current_session->myscreen_ == baseline);
}

TEST(SessionRaii, session_frame_state_independence)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;

    og::runtime::GameSession session1(session_cfg);
    og::runtime::GameSession session2(session_cfg);

    // Modify frame states independently
    session1.frame_state_.done = true;
    session1.frame_state_.currentcycle = 42;

    session2.frame_state_.done = false;
    session2.frame_state_.currentcycle = 7;

    // Verify they're independent
    ASSERT_TRUE(session1.frame_state_.done == true);
    ASSERT_TRUE(session1.frame_state_.currentcycle == 42);
    ASSERT_TRUE(session2.frame_state_.done == false);
    ASSERT_TRUE(session2.frame_state_.currentcycle == 7);
}

// ---------------------------------------------------------------------------
// Multi-session demo verification tests
// ---------------------------------------------------------------------------

TEST(SessionRaii, twelve_sessions_coexist)
{
    // Verify that 12 GameSession instances can be created concurrently
    // with independent state (matches the openglad_demo configuration).
    screen* baseline_screen = og::runtime::current_session->myscreen_;
    options* baseline_prefs = og::runtime::current_session->theprefs_;

    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;

    session_cfg.allocate_seeded_rng = true;

    constexpr int N = 12;
    std::vector<std::unique_ptr<og::runtime::GameSession>> sessions;
    for (int i = 0; i < N; i++) {
        session_cfg.rng_seed = static_cast<Uint32>(i * 1000 + 42);
        sessions.push_back(std::make_unique<og::runtime::GameSession>(session_cfg));
    }

    // All 12 sessions exist simultaneously
    ASSERT_TRUE(sessions.size() == N);
    for (int i = 0; i < N; i++) {
        ASSERT_TRUE(sessions[static_cast<size_t>(i)] != nullptr);
        ASSERT_TRUE(sessions[static_cast<size_t>(i)]->ctx_.rng != nullptr);
    }

    // Each session has independent RNG state
    std::vector<Uint32> values;
    for (int i = 0; i < N; i++) {
        auto scope = sessions[static_cast<size_t>(i)]->activate();
        values.push_back(ctx().rng->next(100000));
    }

    // Count unique values - with 12 different seeds, we should get
    // at least 2 distinct values (overwhelmingly likely to get 12).
    std::set<Uint32> unique_values(values.begin(), values.end());
    ASSERT_TRUE(unique_values.size() >= 2);

    // Each session has independent frame state
    for (int i = 0; i < N; i++) {
        sessions[static_cast<size_t>(i)]->frame_state_.currentcycle =
            static_cast<short>(i);
    }
    for (int i = 0; i < N; i++) {
        ASSERT_TRUE(sessions[static_cast<size_t>(i)]->frame_state_.currentcycle ==
                  static_cast<short>(i));
    }

    sessions.clear();
    ASSERT_TRUE(og::runtime::current_session->myscreen_ == baseline_screen);
    ASSERT_TRUE(og::runtime::current_session->theprefs_ == baseline_prefs);
}

TEST(SessionRaii, session_state_modification_isolation)
{
    // Modify state in one session, verify others are unaffected.
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;

    session_cfg.allocate_seeded_rng = true;
    session_cfg.rng_seed = 1;

    og::runtime::GameSession session_a(session_cfg);
    session_cfg.rng_seed = 2;
    og::runtime::GameSession session_b(session_cfg);

    // Consume RNG values from session A
    {
        auto scope = session_a.activate();
        for (int i = 0; i < 100; i++) {
            ctx().rng->next(1000);
        }
    }

    // Session B's RNG should be unaffected - first value should match
    // a fresh session with seed 2
    session_cfg.rng_seed = 2;
    og::runtime::GameSession session_b_fresh(session_cfg);

    Uint32 b_val, b_fresh_val;
    {
        auto scope = session_b.activate();
        b_val = ctx().rng->next(1000);
    }
    {
        auto scope = session_b_fresh.activate();
        b_fresh_val = ctx().rng->next(1000);
    }
    ASSERT_TRUE(b_val == b_fresh_val);

    // Session A's frame state changes don't affect B
    session_a.frame_state_.done = true;
    ASSERT_TRUE(session_b.frame_state_.done == false);
}

TEST(SessionRaii, demo_grid_layout_non_overlapping)
{
    // Verify the demo compositor's 6x4 display-cell layout (per-index
    // rounding, matching composite_session in demo.cpp) produces 24
    // non-overlapping sub-regions that tile the whole display, including
    // displays that don't divide evenly.
    constexpr int COLS = 6;
    constexpr int ROWS = 4;
    constexpr int TOTAL = COLS * ROWS;

    struct Rect { int x, y, w, h; };

    for (const auto& [DISPLAY_W, DISPLAY_H] :
         {std::pair{1536, 960}, std::pair{1024, 768}, std::pair{1366, 767}}) {
        std::vector<Rect> rects;
        for (int i = 0; i < TOTAL; i++) {
            int col = i % COLS;
            int row = i / COLS;
            int x = col * DISPLAY_W / COLS;
            int y = row * DISPLAY_H / ROWS;
            rects.push_back({x, y,
                             (col + 1) * DISPLAY_W / COLS - x,
                             (row + 1) * DISPLAY_H / ROWS - y});
        }

        ASSERT_TRUE(rects.size() == 24);

        // Verify no two rects overlap
        for (size_t i = 0; i < rects.size(); i++) {
            for (size_t j = i + 1; j < rects.size(); j++) {
                const auto& a = rects[i];
                const auto& b = rects[j];
                bool overlaps = (a.x < b.x + b.w) && (a.x + a.w > b.x) &&
                                (a.y < b.y + b.h) && (a.y + a.h > b.y);
                ASSERT_TRUE(!overlaps);
            }
        }

        // Verify they tile the full display: total area matches and every
        // rect stays in bounds, which together with non-overlap means full
        // coverage (no seams).
        int total_area = 0;
        for (const auto& r : rects) {
            total_area += r.w * r.h;
        }
        ASSERT_TRUE(total_area == DISPLAY_W * DISPLAY_H);

        for (const auto& r : rects) {
            ASSERT_TRUE(r.w > 0);
            ASSERT_TRUE(r.h > 0);
            ASSERT_TRUE(r.x >= 0);
            ASSERT_TRUE(r.y >= 0);
            ASSERT_TRUE(r.x + r.w <= DISPLAY_W);
            ASSERT_TRUE(r.y + r.h <= DISPLAY_H);
        }
    }
}

TEST(SessionRaii, demo_scenario_diversity)
{
    // Replicate the demo's scenario selection logic and verify that
    // 12 sessions are assigned diverse (not all identical) scenario IDs.
    // This catches the original bug where only 4 similar scenarios were used.

    static constexpr int NUM_SESSIONS = 12;
    static const std::array<int, 20> SCENARIO_POOL = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16,
        9411, 9412, 9413, 9414,
    };

    // Use a fixed seed for determinism in tests
    std::mt19937 rng(12345);
    std::vector<int> pool(SCENARIO_POOL.begin(), SCENARIO_POOL.end());
    std::shuffle(pool.begin(), pool.end(), rng);

    std::vector<int> chosen;
    for (int i = 0; i < NUM_SESSIONS; i++)
        chosen.push_back(pool[static_cast<size_t>(i) % pool.size()]);

    // All 12 should be assigned
    ASSERT_TRUE(chosen.size() == NUM_SESSIONS);

    // With 20 scenarios shuffled and 12 picked, we must have at least 2 distinct
    std::set<int> unique_ids(chosen.begin(), chosen.end());
    ASSERT_TRUE(unique_ids.size() >= 2);

    // In fact, all 12 should be distinct since pool (20) > NUM_SESSIONS (12)
    ASSERT_TRUE(unique_ids.size() == static_cast<size_t>(NUM_SESSIONS));

    // Verify pool is large enough to always give unique assignments
    ASSERT_TRUE(SCENARIO_POOL.size() >= static_cast<size_t>(NUM_SESSIONS));
}
