#include <openglad/sim/event.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/sim/sim_world.h>
#include <openglad/runtime/game_context.h>

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
    log.set_tick(1);
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
    log.set_tick(2);
    log.push(og::sim::EventKind::SetPalette, 1, 0);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(ev.a == 1);
}

OG_UNIT_TEST(test_sim_event_log_request_redraw_event)
{
    og::sim::SimEventLog log;
    log.set_tick(3);
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::RequestRedraw);
    OG_ASSERT(ev.tick == 3);
}

// --- emit_event convenience helper ---

OG_UNIT_TEST(test_emit_event_set_palette)
{
    // Set up a minimal context with a SimEventLog
    GameContext test_ctx;
    test_ctx.sim_events = std::make_unique<og::sim::SimEventLog>();
    test_ctx.sim_events->set_tick(5);
    set_global_context(&test_ctx);

    og::sim::emit_event(test_ctx.sim_events.get(), og::sim::EventKind::SetPalette, 1);

    OG_ASSERT(test_ctx.sim_events->size() == 1);
    OG_ASSERT(test_ctx.sim_events->events()[0].kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(test_ctx.sim_events->events()[0].a == 1);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_emit_event_request_redraw)
{
    GameContext test_ctx;
    test_ctx.sim_events = std::make_unique<og::sim::SimEventLog>();
    test_ctx.sim_events->set_tick(7);
    set_global_context(&test_ctx);

    og::sim::emit_event(test_ctx.sim_events.get(), og::sim::EventKind::RequestRedraw);

    OG_ASSERT(test_ctx.sim_events->size() == 1);
    OG_ASSERT(test_ctx.sim_events->events()[0].kind == og::sim::EventKind::RequestRedraw);

    set_global_context(nullptr);
}

// --- Mixed event stream with new types ---

OG_UNIT_TEST(test_mixed_event_stream_with_new_types)
{
    og::sim::SimEventLog log;
    log.set_tick(1);

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

    rng.reset(123);
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
    og::sim::SimWorld world1(42);
    og::sim::SimWorld world2(42);

    // Both worlds should have the same RNG state
    OG_ASSERT(world1.rng().state() == world2.rng().state());

    // After advancing one, they should differ
    world1.rng().next(100);
    OG_ASSERT(world1.rng().state() != world2.rng().state());
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
