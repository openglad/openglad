#include <openglad/gameplay/event.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include "../test_game_world_fixture.h"

#include <gtest/gtest.h>

#include <vector>

// --- EventKind values ---

TEST(SimWorldHeadless, event_kind_set_palette_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::SetPalette) == 11);
}

TEST(SimWorldHeadless, event_kind_request_redraw_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::RequestRedraw) == 12);
}

// --- SimEventLog handles new event types ---

TEST(SimWorldHeadless, sim_event_log_set_palette_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;
    log.push(og::sim::EventKind::SetPalette, 0, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(ev.a == 0);
    ASSERT_TRUE(ev.tick == 1);
}

TEST(SimWorldHeadless, sim_event_log_set_palette_blue)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 2;
    log.push(og::sim::EventKind::SetPalette, 1, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(ev.a == 1);
}

TEST(SimWorldHeadless, sim_event_log_request_redraw_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 3;
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::RequestRedraw);
    ASSERT_TRUE(ev.tick == 3);
}

// --- emit_event convenience helper ---

TEST(SimWorldHeadless, emit_event_set_palette)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 5;

    og::sim::emit_event(&log, og::sim::EventKind::SetPalette, 1);

    ASSERT_TRUE(log.size() == 1);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(log.events()[0].a == 1);
}

TEST(SimWorldHeadless, emit_event_request_redraw)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 7;

    og::sim::emit_event(&log, og::sim::EventKind::RequestRedraw);

    ASSERT_TRUE(log.size() == 1);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::RequestRedraw);
}

// --- Mixed event stream with new types ---

TEST(SimWorldHeadless, mixed_event_stream_with_new_types)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push_notification("freeze!");
    log.push(og::sim::EventKind::SetPalette, 1, 0);
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    ASSERT_TRUE(log.size() == 4);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::PlaySound);
    ASSERT_TRUE(log.events()[1].kind == og::sim::EventKind::Notification);
    ASSERT_TRUE(log.events()[2].kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(log.events()[3].kind == og::sim::EventKind::RequestRedraw);
}

// --- SimRandom deterministic RNG ---

TEST(SimWorldHeadless, sim_random_same_seed_same_sequence)
{
    og::sim::SimRandom rng1(42);
    og::sim::SimRandom rng2(42);

    // Same seed must produce identical sequences
    for (int i = 0; i < 100; i++)
    {
        ASSERT_TRUE(rng1.next(1000) == rng2.next(1000));
    }
}

TEST(SimWorldHeadless, sim_random_different_seeds_differ)
{
    og::sim::SimRandom rng1(42);
    og::sim::SimRandom rng2(99);

    // Different seeds should produce different first values
    ASSERT_TRUE(rng1.next(1000) != rng2.next(1000));
}

TEST(SimWorldHeadless, sim_random_reset_reproduces)
{
    og::sim::SimRandom rng(123);
    auto v1 = rng.next(1000);
    auto v2 = rng.next(1000);
    auto v3 = rng.next(1000);

    rng.state_ = 123;
    ASSERT_TRUE(rng.next(1000) == v1);
    ASSERT_TRUE(rng.next(1000) == v2);
    ASSERT_TRUE(rng.next(1000) == v3);
}

TEST(SimWorldHeadless, sim_random_zero_max_returns_zero)
{
    og::sim::SimRandom rng(42);
    ASSERT_TRUE(rng.next(0) == 0);
}

TEST(SimWorldHeadless, sim_world_owns_rng)
{
    GameWorld world1(42);
    GameWorld world2(42);

    // Both worlds should have the same RNG state
    ASSERT_TRUE(world1.rng_.state_ == world2.rng_.state_);

    // After advancing one, they should differ
    world1.rng_.next(100);
    ASSERT_TRUE(world1.rng_.state_ != world2.rng_.state_);
}

// --- EndGame and SetEnd-adjacent event kinds (Phase 2: G5, G6) ---

TEST(SimWorldHeadless, event_kind_endgame_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::EndGame) == 13);
}

TEST(SimWorldHeadless, event_kind_set_end_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::SetEnd) == 15);
}

TEST(SimWorldHeadless, event_kind_request_exit_confirmation_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::RequestExitConfirmation) == 16);
}

TEST(SimWorldHeadless, event_kind_withdraw_to_level_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::WithdrawToLevel) == 17);
}

TEST(SimWorldHeadless, event_kind_score_change_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::ScoreChange) == 18);
}

TEST(SimWorldHeadless, emit_endgame_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 10;
    og::sim::emit_event(&log, og::sim::EventKind::EndGame, 0, 5);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::EndGame);
    ASSERT_TRUE(ev.a == 0);  // ending type: normal
    ASSERT_TRUE(ev.b == 5);  // next_level
    ASSERT_TRUE(ev.tick == 10);
}

TEST(SimWorldHeadless, emit_endgame_save_all_failure)
{
    // Simulates walker::death() emitting EndGame with SCEN_TYPE_SAVE_ALL
    og::sim::SimEventLog log;
    log.current_tick_ = 20;
    og::sim::emit_event(&log, og::sim::EventKind::EndGame,
                        4, static_cast<std::uint32_t>(-1));

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::EndGame);
    ASSERT_TRUE(ev.a == 4);  // SCEN_TYPE_SAVE_ALL
    ASSERT_TRUE(ev.b == static_cast<std::uint32_t>(-1));  // no next level
}

TEST(SimWorldHeadless, emit_set_end_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 25;
    og::sim::emit_event(&log, og::sim::EventKind::SetEnd);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::SetEnd);
    ASSERT_TRUE(ev.a == 0);
    ASSERT_TRUE(ev.b == 0);
}

TEST(SimWorldHeadless, emit_request_exit_confirmation_with_text)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 27;
    og::sim::emit_event_text(&log, og::sim::EventKind::RequestExitConfirmation,
                             "Exit to Level 3?", 3, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::RequestExitConfirmation);
    ASSERT_TRUE(ev.a == 3);
    ASSERT_TRUE(ev.b == 0);
    ASSERT_TRUE(ev.text == "Exit to Level 3?");
}

TEST(SimWorldHeadless, emit_withdraw_to_level_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 28;
    og::sim::emit_event(&log, og::sim::EventKind::WithdrawToLevel, 5, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::WithdrawToLevel);
    ASSERT_TRUE(ev.a == 5);
    ASSERT_TRUE(ev.b == 0);
}

TEST(SimWorldHeadless, emit_score_change_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 29;
    og::sim::emit_event(&log, og::sim::EventKind::ScoreChange, 2, 125);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::ScoreChange);
    ASSERT_TRUE(ev.a == 2);
    ASSERT_TRUE(ev.b == 125);
}

TEST(SimWorldHeadless, mixed_stream_with_phase2_events)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push_notification("level complete!");
    log.push(og::sim::EventKind::ScoreChange, 1, 250);
    log.push(og::sim::EventKind::SetEnd, 0, 0);
    log.push(og::sim::EventKind::EndGame, 0, 3);

    ASSERT_TRUE(log.size() == 5);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::PlaySound);
    ASSERT_TRUE(log.events()[1].kind == og::sim::EventKind::Notification);
    ASSERT_TRUE(log.events()[2].kind == og::sim::EventKind::ScoreChange);
    ASSERT_TRUE(log.events()[2].a == 1);
    ASSERT_TRUE(log.events()[2].b == 250);
    ASSERT_TRUE(log.events()[3].kind == og::sim::EventKind::SetEnd);
    ASSERT_TRUE(log.events()[4].kind == og::sim::EventKind::EndGame);
    ASSERT_TRUE(log.events()[4].b == 3);
}

// --- emit_* with null log is a no-op ---

TEST(SimWorldHeadless, emit_sound_null_log_is_noop)
{
    // Should not crash
    og::sim::emit_sound(nullptr, 42);
}

TEST(SimWorldHeadless, emit_notification_null_log_is_noop)
{
    og::sim::emit_notification(nullptr, "test");
}

TEST(SimWorldHeadless, emit_event_null_log_is_noop)
{
    og::sim::emit_event(nullptr, og::sim::EventKind::PlaySound, 1);
}

// --- Grid passability ---

// Authored Specials (reserved-team markers, ACT_RANDOM) pathfind with no
// owner; the arrow-wall pass-through gamble dereferenced the null owner and
// crashed the sim (real data: Tryxian Chronicles scen108/scen115). Unowned
// non-living entities must treat arrow walls as solid instead.
TEST(SimWorldHeadless, arrow_wall_is_solid_for_unowned_special)
{
    TestGameWorld t;
    PixieData& grid = t.world().grid;
    const int gx = 2;
    const int gy = 2;
    grid.data[gx + grid.w * gy] = PIX_WALL4;

    walker* special = t.world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
    ASSERT_NE(nullptr, special);
    ASSERT_EQ(nullptr, special->owner());

    EXPECT_FALSE(t.world().query_grid_passable(
        static_cast<float>(gx * GRID_SIZE),
        static_cast<float>(gy * GRID_SIZE), special));
}

// --- Generator spawn-rate multiplier (difficulty submenu) ---

// The generator cadence gate scales the Bernoulli COMPARISON by
// world.generator_rate percent (level_draw * rate > threshold_draw * 100);
// the draw bounds themselves never change. At the defaults (0 = unset and
// the explicit 100) both sides carry the same factor, so the world RNG
// stream must be byte-identical draw for draw. A non-default rate must
// diverge (different success odds -> extra direction draws on success).
TEST(SimWorldHeadless, generator_rate_default_keeps_rng_stream_identical)
{
    constexpr int kTicks = 400;
    const auto run = [](short rate) {
        TestGameWorld t;
        GameWorld& world = t.world();
        world.generator_rate = rate;

        walker* generator = world.add_ob(Order::Generator, FAMILY_TOWER);
        EXPECT_NE(nullptr, generator);
        generator->setxy(80, 80);
        generator->stats()->set_level(5);
        generator->set_act_type(ACT_GENERATE);

        world.rng_.state_ = 0xC0FFEE42u;
        std::vector<std::uint32_t> states;
        states.reserve(kTicks);
        for (int i = 0; i < kTicks; ++i)
        {
            generator->act();
            // Test-built walkers carry no animation table, so a successful
            // roll would otherwise wedge in ANI_ATTACK; reset symmetrically
            // in every run so the cadence gate rolls each tick.
            generator->set_ani_type(ANI_WALK);
            states.push_back(world.rng_.state_);
        }
        return states;
    };

    const std::vector<std::uint32_t> baseline = run(0);
    EXPECT_EQ(baseline, run(100))
        << "rate 100 must be an exact integer identity with the legacy bound";
    EXPECT_NE(baseline, run(200))
        << "a non-default rate must change the generator RNG stream";
}

// Calm (rate 50) must REDUCE a generator's cadence, never silence it: the
// rate scales the comparison, not the draw bound, so even a level-1
// generator (level_draw in {0,1,2}) can still win the roll. A scaled BOUND
// would collapse to next(1) == 0 at level 1 and never fire — and the
// sanitize floor (25) would reach the forbidden SimRandom::next(0). Success
// is observed via the +1 self-heal on every successful roll.
TEST(SimWorldHeadless, generator_rate_calm_keeps_level1_generators_live)
{
    constexpr int kTicks = 8000;
    const auto successes = [](short rate) {
        TestGameWorld t;
        GameWorld& world = t.world();
        world.generator_rate = rate;

        walker* generator = world.add_ob(Order::Generator, FAMILY_TOWER);
        EXPECT_NE(nullptr, generator);
        generator->setxy(80, 80);
        generator->stats()->set_level(1);
        generator->set_act_type(ACT_GENERATE);
        generator->stats()->set_max_hitpoints(30000);
        generator->stats()->set_hitpoints(100);

        world.rng_.state_ = 0xC0FFEE42u;
        for (int i = 0; i < kTicks; ++i)
        {
            generator->act();
            generator->set_ani_type(ANI_WALK); // keep the cadence gate rolling
        }
        return generator->stats()->hitpoints() - 100;
    };

    const auto baseline = successes(0);
    const auto calm = successes(50);
    const auto sanitize_floor = successes(25);
    ASSERT_GT(baseline, 0) << "a level-1 generator fires on the default rate";
    EXPECT_GT(calm, 0) << "Calm halves the odds; it must not go silent";
    EXPECT_LT(calm, baseline) << "Calm fires less often than the default";
    EXPECT_GT(sanitize_floor, 0)
        << "the lobby-sanitize floor (25) must keep level-1 generators live";
    EXPECT_LE(sanitize_floor, calm)
        << "a lower rate can never out-fire a higher one on the same stream";
}

// The exit pad's third branch: foes alive AND destination unearned used to be
// completely SILENT — the player walked over the exit with no acknowledgment
// (the tower playtest's "exit seems blocked" report). It now says why, on the
// existing skip_exit(10) cadence, without touching the two event-emitting
// branches (both parity-pinned: exit prompt + withdraw pair).
TEST(SimWorldHeadless, exit_with_foes_alive_says_foes_remain)
{
    TestGameWorld t;
    const auto notifications = [&](const char* needle) {
        int count = 0;
        for (const auto& ev : t.events.events())
            if (ev.kind == og::sim::EventKind::Notification &&
                ev.text.find(needle) != std::string::npos)
                count++;
        return count;
    };
    const auto exit_prompts = [&] {
        int count = 0;
        for (const auto& ev : t.events.events())
            if (ev.kind == og::sim::EventKind::RequestExitConfirmation)
                count++;
        return count;
    };

    walker* exit_pad = t.world().add_fx_ob(Order::Treasure, FAMILY_EXIT);
    ASSERT_NE(nullptr, exit_pad);
    exit_pad->setxy(120, 120);
    ASSERT_NE(nullptr, exit_pad->stats());
    exit_pad->stats()->set_level(2); // destination level id

    walker* hero = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, hero);
    hero->setxy(120, 120);
    hero->set_act_type(ACT_CONTROL);

    // Foes alive, destination unearned: the toast fires, no prompt.
    t.world().level_done = 0;
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"));
    EXPECT_EQ(0, exit_prompts());

    // The skip_exit throttle bounds the cadence while standing on the pad.
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"))
        << "an immediate re-eat must be swallowed by set_skip_exit(10)";

    // Withdraw shape (destination already completed): the withdraw pair
    // fires, never the toast.
    hero->set_skip_exit(0);
    t.world().completed_levels.insert(2);
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"));
    EXPECT_EQ(1, exit_prompts()) << "withdraw emits RequestExitConfirmation";
    t.world().completed_levels.erase(2);

    // Cleared level: the normal exit prompt, no new toast.
    hero->set_skip_exit(0);
    t.world().level_done = 2;
    t.world().withdraw_requested = false;
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"));
    EXPECT_EQ(2, exit_prompts());
}

// Life-gem value vs permadeath: with keep_fallen_heroes set (permadeath off)
// the fallen hero returns with their growth intact, so the gem drops at HALF
// the legacy value — full salvage would double-dip. Default (0) is the
// byte-identical legacy math: heart_value * 0.75 / 2.
TEST(SimWorldHeadless, life_gem_halves_when_permadeath_is_off)
{
    const auto dropped_gem_value = [](short keep_fallen) -> float {
        TestGameWorld t;
        t.world().keep_fallen_heroes = keep_fallen;

        walker* hero = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
        if (hero == nullptr)
            return -1.0f;
        hero->setxy(120, 120);
        hero->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        hero->myguy->exp = 4000; // non-trivial heart value

        hero->set_dead(1);
        hero->death();

        for (const auto& uptr : t.world().oblist)
        {
            walker* w = uptr.get();
            if (w != nullptr && w->query_order() == Order::Treasure &&
                w->family() == FAMILY_LIFE_GEM)
                return w->stats()->hitpoints();
        }
        return -1.0f;
    };

    const float full = dropped_gem_value(0);
    const float halved = dropped_gem_value(1);
    ASSERT_GT(full, 0.0f) << "permadeath-on death must drop a valued gem";
    ASSERT_GT(halved, 0.0f);
    EXPECT_FLOAT_EQ(full * 0.5f, halved)
        << "permadeath-off gems carry exactly half the legacy value";
}
