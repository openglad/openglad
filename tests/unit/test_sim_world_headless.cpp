#include <openglad/sim/event.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/gameplay/game_world.h>

#include "unit.h"

// --- EventKind values ---

OG_UNIT_TEST(test_event_kind_set_palette_value)
{
    OG_ASSERT(static_cast<std::uint32_t>(og::sim::EventKind::SetPalette) == 11);
}

OG_UNIT_TEST(test_event_kind_request_redraw_value)
{
    OG_ASSERT(static_cast<std::uint32_t>(og::sim::EventKind::RequestRedraw) == 12);
}

// --- SimEventLog handles new event types ---

OG_UNIT_TEST(test_sim_event_log_set_palette_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;
    log.push(og::sim::EventKind::SetPalette, 0, 0);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(ev.a == 0);
    OG_ASSERT(ev.tick == 1);
}

OG_UNIT_TEST(test_sim_event_log_set_palette_blue)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 2;
    log.push(og::sim::EventKind::SetPalette, 1, 0);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(ev.a == 1);
}

OG_UNIT_TEST(test_sim_event_log_request_redraw_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 3;
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::RequestRedraw);
    OG_ASSERT(ev.tick == 3);
}

// --- emit_event convenience helper ---

OG_UNIT_TEST(test_emit_event_set_palette)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 5;

    og::sim::emit_event(&log, og::sim::EventKind::SetPalette, 1);

    OG_ASSERT(log.size() == 1);
    OG_ASSERT(log.events()[0].kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(log.events()[0].a == 1);
}

OG_UNIT_TEST(test_emit_event_request_redraw)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 7;

    og::sim::emit_event(&log, og::sim::EventKind::RequestRedraw);

    OG_ASSERT(log.size() == 1);
    OG_ASSERT(log.events()[0].kind == og::sim::EventKind::RequestRedraw);
}

// --- Mixed event stream with new types ---

OG_UNIT_TEST(test_mixed_event_stream_with_new_types)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push_notification("freeze!");
    log.push(og::sim::EventKind::SetPalette, 1, 0);
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    OG_ASSERT(log.size() == 4);
    OG_ASSERT(log.events()[0].kind == og::sim::EventKind::PlaySound);
    OG_ASSERT(log.events()[1].kind == og::sim::EventKind::Notification);
    OG_ASSERT(log.events()[2].kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(log.events()[3].kind == og::sim::EventKind::RequestRedraw);
}

// --- SimRandom deterministic RNG ---

OG_UNIT_TEST(test_sim_random_same_seed_same_sequence)
{
    og::sim::SimRandom rng1(42);
    og::sim::SimRandom rng2(42);

    // Same seed must produce identical sequences
    for (int i = 0; i < 100; i++)
    {
        OG_ASSERT(rng1.next(1000) == rng2.next(1000));
    }
}

OG_UNIT_TEST(test_sim_random_different_seeds_differ)
{
    og::sim::SimRandom rng1(42);
    og::sim::SimRandom rng2(99);

    // Different seeds should produce different first values
    OG_ASSERT(rng1.next(1000) != rng2.next(1000));
}

OG_UNIT_TEST(test_sim_random_reset_reproduces)
{
    og::sim::SimRandom rng(123);
    auto v1 = rng.next(1000);
    auto v2 = rng.next(1000);
    auto v3 = rng.next(1000);

    rng.state_ = 123;
    OG_ASSERT(rng.next(1000) == v1);
    OG_ASSERT(rng.next(1000) == v2);
    OG_ASSERT(rng.next(1000) == v3);
}

OG_UNIT_TEST(test_sim_random_zero_max_returns_zero)
{
    og::sim::SimRandom rng(42);
    OG_ASSERT(rng.next(0) == 0);
}

OG_UNIT_TEST(test_sim_world_owns_rng)
{
    og::gameplay::GameWorld world1;
    og::gameplay::GameWorld world2;
    world1.rng_.state_ = 42;
    world2.rng_.state_ = 42;

    // Both worlds should have the same RNG state
    OG_ASSERT(world1.rng_.state_ == world2.rng_.state_);

    // After advancing one, they should differ
    world1.rng_.next(100);
    OG_ASSERT(world1.rng_.state_ != world2.rng_.state_);
}

// --- EndGame, DamageTile, SetEnd event kinds (Phase 2: G5, G6) ---

OG_UNIT_TEST(test_event_kind_endgame_value)
{
    OG_ASSERT(static_cast<std::uint32_t>(og::sim::EventKind::EndGame) == 13);
}

OG_UNIT_TEST(test_event_kind_damage_tile_value)
{
    OG_ASSERT(static_cast<std::uint32_t>(og::sim::EventKind::DamageTile) == 14);
}

OG_UNIT_TEST(test_event_kind_set_end_value)
{
    OG_ASSERT(static_cast<std::uint32_t>(og::sim::EventKind::SetEnd) == 15);
}

OG_UNIT_TEST(test_emit_endgame_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 10;
    og::sim::emit_event(&log, og::sim::EventKind::EndGame, 0, 5);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::EndGame);
    OG_ASSERT(ev.a == 0);  // ending type: normal
    OG_ASSERT(ev.b == 5);  // next_level
    OG_ASSERT(ev.tick == 10);
}

OG_UNIT_TEST(test_emit_endgame_save_all_failure)
{
    // Simulates walker::death() emitting EndGame with SCEN_TYPE_SAVE_ALL
    og::sim::SimEventLog log;
    log.current_tick_ = 20;
    og::sim::emit_event(&log, og::sim::EventKind::EndGame,
                        4, static_cast<std::uint32_t>(-1));

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::EndGame);
    OG_ASSERT(ev.a == 4);  // SCEN_TYPE_SAVE_ALL
    OG_ASSERT(ev.b == static_cast<std::uint32_t>(-1));  // no next level
}

OG_UNIT_TEST(test_emit_damage_tile_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 15;
    og::sim::emit_event(&log, og::sim::EventKind::DamageTile, 120, 240);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::DamageTile);
    OG_ASSERT(ev.a == 120);  // x_pixel
    OG_ASSERT(ev.b == 240);  // y_pixel
}

OG_UNIT_TEST(test_emit_set_end_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 25;
    og::sim::emit_event(&log, og::sim::EventKind::SetEnd);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::SetEnd);
    OG_ASSERT(ev.a == 0);
    OG_ASSERT(ev.b == 0);
}

OG_UNIT_TEST(test_mixed_stream_with_phase2_events)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push(og::sim::EventKind::DamageTile, 50, 60);
    log.push_notification("level complete!");
    log.push(og::sim::EventKind::SetEnd, 0, 0);
    log.push(og::sim::EventKind::EndGame, 0, 3);

    OG_ASSERT(log.size() == 5);
    OG_ASSERT(log.events()[0].kind == og::sim::EventKind::PlaySound);
    OG_ASSERT(log.events()[1].kind == og::sim::EventKind::DamageTile);
    OG_ASSERT(log.events()[1].a == 50);
    OG_ASSERT(log.events()[1].b == 60);
    OG_ASSERT(log.events()[2].kind == og::sim::EventKind::Notification);
    OG_ASSERT(log.events()[3].kind == og::sim::EventKind::SetEnd);
    OG_ASSERT(log.events()[4].kind == og::sim::EventKind::EndGame);
    OG_ASSERT(log.events()[4].b == 3);
}

// --- emit_* with null log is a no-op ---

OG_UNIT_TEST(test_emit_sound_null_log_is_noop)
{
    // Should not crash
    og::sim::emit_sound(nullptr, 42);
}

OG_UNIT_TEST(test_emit_notification_null_log_is_noop)
{
    og::sim::emit_notification(nullptr, "test");
}

OG_UNIT_TEST(test_emit_event_null_log_is_noop)
{
    og::sim::emit_event(nullptr, og::sim::EventKind::PlaySound, 1);
}
