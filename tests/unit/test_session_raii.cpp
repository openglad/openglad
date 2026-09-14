#include <openglad/platform/game_session.h>

#include <openglad/core/irandom.h> // SeededRandom (the oracle for cfg.rng_seed)
#include <openglad/resources/gparser.h> // cfg
#include <openglad/legacy/base.h> // myscreen
#include <openglad/interface/render/view.h> // theprefs

#include <set>
#include <vector>

#include <gtest/gtest.h>

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

        // Merged from game_session_headless_restores_legacy_globals: a session
        // built without allocate_seeded_rng still owns a usable generator (the
        // ProductionRandom fallback, game_session.cpp:162-164). The myscreen_/
        // theprefs_ pins that case also carried were nullptr == nullptr in this
        // screenless binary and pinned nothing.
        ASSERT_NE(nullptr, session.ctx_.rng)
            << "GameSession must fall back to its ProductionRandom when no seeded RNG is requested";

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

    // Two sessions agreeing with each other proves nothing (a constructor that
    // dropped cfg.rng_seed keeps them equal). Pin the sequence against the
    // generator the product is required to build: SeededRandom(cfg.rng_seed).
    SeededRandom probe(123u);
    EXPECT_EQ(probe.next(1000), a0) << "draw 0 must be SeededRandom(123)'s draw 0";
    EXPECT_EQ(probe.next(1000), a1) << "draw 1 must be SeededRandom(123)'s draw 1";
    EXPECT_EQ(probe.next(1000), a2) << "draw 2 must be SeededRandom(123)'s draw 2";

    ASSERT_NE(session.ctx_.rng, session2.ctx_.rng)
        << "each GameSession must own its own SeededRandom, not share one";

    EXPECT_EQ(a0, b0) << "same seed, same draw 0";
    EXPECT_EQ(a1, b1) << "same seed, same draw 1";
    EXPECT_EQ(a2, b2) << "same seed, same draw 2";
}

TEST(SessionRaii, game_session_repeated_create_destroy)
{
    // Sessions can be created and destroyed repeatedly without leaking global
    // state: each construction with install_legacy_globals installs itself as
    // current/primary session and each destructor restores the previous one.
    // (Headless: no screen/prefs, which need SDL/PhysFS. myscreen_/theprefs_
    // are nullptr on both sides here, so the session pointers are the oracle.)
    og::runtime::SessionState* const baseline_session = og::runtime::current_session;
    og::runtime::SessionState* const baseline_primary =
        og::runtime::primary_session.load(std::memory_order_acquire);
    GameplayContext* const baseline_game = current_game;

    for (int i = 0; i < 5; ++i) {
        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;

        session_cfg.allocate_seeded_rng = true;
        session_cfg.rng_seed = static_cast<Uint32>(i);
        og::runtime::GameSession session(session_cfg);

        ASSERT_EQ(&session, og::runtime::current_session)
            << "round " << i << ": construction must install this session as current_session";
        ASSERT_EQ(&session, og::runtime::primary_session.load(std::memory_order_acquire))
            << "round " << i << ": construction must install this session as primary_session";
        ASSERT_EQ(&session.game_, current_game)
            << "round " << i << ": construction must install this session's GameplayContext";
        ASSERT_EQ(session.ctx_.rng, ctx().rng)
            << "round " << i << ": ctx() must resolve to this session's own generator";

        SeededRandom probe(static_cast<Uint32>(i));
        EXPECT_EQ(probe.next(100), session.ctx_.rng->next(100))
            << "round " << i << ": the session RNG must be seeded from cfg.rng_seed";
    }

    ASSERT_EQ(baseline_session, og::runtime::current_session)
        << "every destructor must restore the previous current_session";
    ASSERT_EQ(baseline_primary, og::runtime::primary_session.load(std::memory_order_acquire))
        << "every destructor must restore the previous primary_session";
    ASSERT_EQ(baseline_game, current_game)
        << "every destructor must restore the previous current_game";
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
    // Merged from game_session_cfg_accessible (which was a bare `(void)cfg;`):
    // config() hands back the process-wide gparser store, no per-session copy.
    EXPECT_EQ(&cfg, &session.config());
    EXPECT_FALSE(session.has_local_transport_runtime());
}

// ---------------------------------------------------------------------------
// SessionScope isolation tests
// ---------------------------------------------------------------------------

TEST(SessionRaii, session_scope_activates_and_restores_globals)
{
    // NOTE: myscreen_/theprefs_ are nullptr for both the enclosing session and
    // this one in og_unit_sim (screenless/prefless), so comparing them pinned
    // nothing. The session/game identity pointers below are the real oracle.
    og::runtime::SessionState* const baseline_session = og::runtime::current_session;
    og::runtime::GameSession* const baseline_game_session = og::runtime::current_game_session;
    GameplayContext* const baseline_game = current_game;

    // Headless: skip screen and prefs allocation (both need SDL/PhysFS).
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;

    og::runtime::GameSession session(session_cfg);

    ASSERT_EQ(baseline_session, og::runtime::current_session)
        << "install_legacy_globals=false must leave current_session alone";
    ASSERT_EQ(baseline_game, current_game)
        << "install_legacy_globals=false must leave current_game alone";

    {
        auto scope = session.activate();
        ASSERT_EQ(&session, og::runtime::current_session)
            << "activate() must install this session as current_session";
        ASSERT_EQ(&session, og::runtime::current_game_session)
            << "activate() must install this session as current_game_session";
        ASSERT_EQ(&session.ctx_, &ctx())
            << "ctx() must follow current_session into this session's context";
        ASSERT_EQ(&session.game_, current_game)
            << "activate() must install this session's GameplayContext as current_game";
    }

    ASSERT_EQ(baseline_session, og::runtime::current_session)
        << "~SessionScope must restore the previous current_session";
    ASSERT_EQ(baseline_game_session, og::runtime::current_game_session)
        << "~SessionScope must restore the previous current_game_session";
    ASSERT_EQ(baseline_game, current_game)
        << "~SessionScope must restore the previous current_game";
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
        ASSERT_EQ(session1.ctx_.rng, ctx().rng)
            << "activate() must install session1's own generator as ctx().rng";
        s1_val = ctx().rng->next(1000);
    }

    // Generate values from session2 - should be independent
    Uint32 s2_val;
    {
        auto scope = session2.activate();
        ASSERT_EQ(session2.ctx_.rng, ctx().rng)
            << "activate() must install session2's own generator as ctx().rng";
        s2_val = ctx().rng->next(1000);
    }

    // `s1_val != s2_val` alone is satisfied by two successive draws from one
    // shared stream. Pin each draw to its own seed's generator instead.
    SeededRandom p1(42u);
    EXPECT_EQ(p1.next(1000), s1_val)
        << "session seeded 42 must yield SeededRandom(42)'s first draw";
    SeededRandom p2(99u);
    EXPECT_EQ(p2.next(1000), s2_val)
        << "session seeded 99 must yield SeededRandom(99)'s first draw";
    EXPECT_NE(s1_val, s2_val) << "distinct seeds must give distinct first draws";
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

    // myscreen_ is nullptr on every session in this screenless binary, so the
    // old `current_session->myscreen_ == baseline` tail was nullptr == nullptr
    // and stayed green under a ~SessionScope that restored nothing. The
    // session identity pointer is the oracle that actually moves.
    og::runtime::SessionState* const baseline_session = og::runtime::current_session;
    og::runtime::GameSession* const baseline_game_session =
        og::runtime::current_game_session;

    {
        auto scope1 = session1.activate();
        IRandom* rng1 = ctx().rng;
        ASSERT_EQ(&session1, og::runtime::current_session)
            << "the outer activate() must install session1 as current_session";
        ASSERT_EQ(session1.ctx_.rng, rng1)
            << "ctx() must resolve to session1's own generator";

        {
            auto scope2 = session2.activate();
            IRandom* rng2 = ctx().rng;
            ASSERT_EQ(&session2, og::runtime::current_session)
                << "the inner activate() must install session2 as current_session";
            ASSERT_EQ(session2.ctx_.rng, rng2)
                << "ctx() must resolve to session2's own generator";
            ASSERT_NE(rng1, rng2)
                << "the two sessions must not share one generator";
        }

        ASSERT_EQ(&session1, og::runtime::current_session)
            << "~SessionScope must restore the OUTER session, not the baseline";
        ASSERT_EQ(rng1, ctx().rng)
            << "ctx() must follow current_session back to session1";
    }

    ASSERT_EQ(baseline_session, og::runtime::current_session)
        << "the outer ~SessionScope must restore the enclosing session";
    ASSERT_EQ(baseline_game_session, og::runtime::current_game_session)
        << "the outer ~SessionScope must restore the enclosing game session";
}

TEST(SessionRaii, twelve_sessions_coexist)
{
    // Verify that 12 GameSession instances can be created concurrently
    // with independent state (matches the openglad_demo configuration).
    // The N=5 case multiple_sessions_coexist_headless was the same scenario
    // with fewer pins and was merged here.
    // myscreen_/theprefs_ are nullptr throughout in this screenless binary, so
    // the session identity pointer is what proves the globals were restored.
    og::runtime::SessionState* const baseline_session = og::runtime::current_session;

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

    // (`sessions.size() == N` and `make_unique != nullptr` pinned this test's
    // own push_back loop, not the product; dropped.)
    for (int i = 0; i < N; i++) {
        ASSERT_NE(nullptr, sessions[static_cast<size_t>(i)]->ctx_.rng)
            << "session " << i << ": allocate_seeded_rng must leave ctx_.rng set";
    }

    // Each session has independent RNG state
    std::vector<Uint32> values;
    for (int i = 0; i < N; i++) {
        auto scope = sessions[static_cast<size_t>(i)]->activate();
        ASSERT_EQ(sessions[static_cast<size_t>(i)]->ctx_.rng, ctx().rng)
            << "session " << i << ": activate() must install its own generator as ctx().rng";
        values.push_back(ctx().rng->next(100000));
    }

    // Each draw is a pure function of that session's seed and of nothing else:
    // twelve successive draws from one shared stream would also be distinct, so
    // pin the value, not just the distinctness.
    for (int i = 0; i < N; i++) {
        SeededRandom probe(static_cast<Uint32>(i * 1000 + 42));
        EXPECT_EQ(probe.next(100000), values[static_cast<size_t>(i)])
            << "session " << i << " must draw from SeededRandom(" << (i * 1000 + 42) << ")";
    }

    std::set<Uint32> unique_values(values.begin(), values.end());
    ASSERT_EQ(static_cast<size_t>(N), unique_values.size())
        << "twelve distinct seeds must give twelve distinct first draws";

    // (The frame_state_.currentcycle write/read-back loop that used to sit
    // here was a plain-struct self-oracle: no product logic ran between the
    // store and the load. The per-session RNG values above already prove the
    // twelve sessions hold independent state.)

    sessions.clear();
    ASSERT_EQ(baseline_session, og::runtime::current_session)
        << "destroying every session must leave the enclosing session current";
}

TEST(SessionRaii, draws_on_one_session_do_not_advance_another_sessions_stream)
{
    // Rule: each GameSession owns its generator, so 100 draws taken while
    // session A is active leave session B's stream sitting at seed 2's draw 0.
    //
    // The old body compared session_b against a same-seed session_b_fresh --
    // a constructor that dropped cfg.rng_seed entirely keeps those two equal,
    // so the comparison stayed green. Pin B against the generator the product
    // is required to build, SeededRandom(2), and pin A's own seed as the
    // negative control.
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;

    session_cfg.allocate_seeded_rng = true;
    session_cfg.rng_seed = 1;

    og::runtime::GameSession session_a(session_cfg);
    session_cfg.rng_seed = 2;
    og::runtime::GameSession session_b(session_cfg);

    // Burn 100 draws on A.
    {
        auto scope = session_a.activate();
        for (int i = 0; i < 100; i++) {
            ctx().rng->next(1000);
        }
    }

    Uint32 b_val = 0;
    {
        auto scope = session_b.activate();
        ASSERT_EQ(session_b.ctx_.rng, ctx().rng)
            << "activate() must install session B's own generator";
        b_val = ctx().rng->next(1000);
    }

    SeededRandom expect_b(2u);
    const Uint32 want_b = expect_b.next(1000);
    EXPECT_EQ(want_b, b_val)
        << "session B must still be at SeededRandom(2)'s draw 0 after 100 draws on A";

    SeededRandom expect_a(1u);
    EXPECT_NE(expect_a.next(1000), b_val)
        << "session B must not be drawing from session A's seed-1 stream";

    // A's stream really did move: its 101st draw is seed 1's 101st, not its 1st.
    SeededRandom expect_a_stream(1u);
    for (int i = 0; i < 100; i++)
        expect_a_stream.next(1000);
    Uint32 a_val = 0;
    {
        auto scope = session_a.activate();
        a_val = ctx().rng->next(1000);
    }
    EXPECT_EQ(expect_a_stream.next(1000), a_val)
        << "session A's generator advanced by exactly the 100 draws taken on it";
}
