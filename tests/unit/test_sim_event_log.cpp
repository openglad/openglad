#include <openglad/gameplay/sim_event_log.h>

#include "unit.h"

OG_UNIT_TEST(test_sim_event_log_push_and_clear)
{
    og::sim::SimEventLog log;
    OG_ASSERT(log.empty());
    OG_ASSERT(log.size() == 0);

    log.current_tick_ = 1;
    log.push(og::sim::EventKind::SetPalette, 1, 0);
    OG_ASSERT(!log.empty());
    OG_ASSERT(log.size() == 1);

    const auto& ev = log.events()[0];
    OG_ASSERT(ev.tick == 1);
    OG_ASSERT(ev.kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(ev.a == 1);
    OG_ASSERT(ev.b == 0);

    log.clear();
    OG_ASSERT(log.empty());
}

OG_UNIT_TEST(test_sim_event_log_push_sound)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 5;
    log.push_sound(42);

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::PlaySound);
    OG_ASSERT(ev.a == 42);
    OG_ASSERT(ev.tick == 5);
}

OG_UNIT_TEST(test_sim_event_log_push_notification)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 10;
    log.push_notification("Hello, world!");

    OG_ASSERT(log.size() == 1);
    const auto& ev = log.events()[0];
    OG_ASSERT(ev.kind == og::sim::EventKind::Notification);
    OG_ASSERT(ev.text == "Hello, world!");
    OG_ASSERT(ev.tick == 10);
}

OG_UNIT_TEST(test_sim_event_log_drain)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;
    log.push_sound(1);
    log.push_sound(2);
    log.push_sound(3);

    OG_ASSERT(log.size() == 3);

    auto drained = log.drain();
    OG_ASSERT(drained.size() == 3);
    OG_ASSERT(log.empty());
    OG_ASSERT(drained[0].a == 1);
    OG_ASSERT(drained[1].a == 2);
    OG_ASSERT(drained[2].a == 3);
}

OG_UNIT_TEST(test_sim_event_log_tick_tracking)
{
    og::sim::SimEventLog log;
    OG_ASSERT(log.current_tick_ == 0);

    log.current_tick_ = 42;
    OG_ASSERT(log.current_tick_ == 42);

    log.push(og::sim::EventKind::SetPalette, 1, 0);
    OG_ASSERT(log.events()[0].tick == 42);

    log.current_tick_ = 43;
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);
    OG_ASSERT(log.events()[1].tick == 43);
}

OG_UNIT_TEST(test_sim_event_log_multiple_event_types)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push_notification("test msg");
    log.push(og::sim::EventKind::SetPalette, 1, 0);
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    OG_ASSERT(log.size() == 4);
    OG_ASSERT(log.events()[0].kind == og::sim::EventKind::PlaySound);
    OG_ASSERT(log.events()[1].kind == og::sim::EventKind::Notification);
    OG_ASSERT(log.events()[2].kind == og::sim::EventKind::SetPalette);
    OG_ASSERT(log.events()[3].kind == og::sim::EventKind::RequestRedraw);
}
