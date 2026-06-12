#include <openglad/gameplay/event.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include "../test_game_world_fixture.h"

#include <gtest/gtest.h>

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
