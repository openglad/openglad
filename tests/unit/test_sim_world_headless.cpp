#include <openglad/sim/event.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_emit.h>
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

    og::sim::emit_event(og::sim::EventKind::SetPalette, 1);

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

    og::sim::emit_event(og::sim::EventKind::RequestRedraw);

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
