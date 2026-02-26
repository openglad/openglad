/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/sim_event_log.h>

#include <cassert>
#include <cstdio>
#include <utility>

namespace og::sim {

// SimEventLog is intentionally single-threaded and must be accessed by the
// simulation/runtime thread that owns a tick. Ownership checks are compiled
// only in debug builds to avoid per-event overhead in release builds.
void SimEventLog::debug_assert_single_thread() const
{
#ifndef NDEBUG
    assert_thread_ownership_();
#endif
}

#ifndef NDEBUG
void SimEventLog::assert_thread_ownership_() const
{
    const std::thread::id current = std::this_thread::get_id();
    if (!owner_thread_initialized_)
    {
        owner_thread_ = current;
        owner_thread_initialized_ = true;
        return;
    }
    assert(owner_thread_ == current &&
           "SimEventLog is single-threaded: access from multiple threads is not supported");
}
#endif

void SimEventLog::push(EventKind kind, std::uint32_t a, std::uint32_t b, const std::string& text)
{
    debug_assert_single_thread();
    Event ev;
    ev.tick = current_tick_;
    ev.kind = kind;
    ev.a = a;
    ev.b = b;
    ev.text = text;
    emit(std::move(ev));
}

void SimEventLog::push_notification(const std::string& message, std::uint32_t duration)
{
    debug_assert_single_thread();
    Event ev;
    ev.tick = current_tick_;
    ev.kind = EventKind::Notification;
    ev.a = duration;
    ev.text = message;
    emit(std::move(ev));
}

void SimEventLog::push_sound(std::uint32_t sound_id)
{
    push(EventKind::PlaySound, sound_id, 0);
}

std::vector<Event> SimEventLog::drain()
{
    debug_assert_single_thread();
    std::vector<Event> result;
    result.swap(events_);
    return result;
}

void SimEventLog::emit(Event&& ev)
{
    if (events_.size() >= kMaxEventsPerDrain)
    {
#ifndef NDEBUG
        std::fprintf(stderr,
                     "[WARN] SimEventLog overflow at tick %u; dropping oldest event\n",
                     current_tick_);
#endif
        events_.erase(events_.begin());
    }
    events_.push_back(std::move(ev));
}

} // namespace og::sim
