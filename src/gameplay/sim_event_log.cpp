/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/sim_event_log.h>

#include <cassert>
#include <utility>

namespace og::sim {

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
    assert(owner_thread_ == current && "SimEventLog used from multiple threads");
}
#endif

void SimEventLog::push(EventKind kind, std::uint32_t a, std::uint32_t b, const std::string& text)
{
#ifndef NDEBUG
    assert_thread_ownership_();
#endif
    Event ev;
    ev.tick = current_tick_;
    ev.kind = kind;
    ev.a = a;
    ev.b = b;
    ev.text = text;
    events_.push_back(std::move(ev));
}

void SimEventLog::push_notification(const std::string& message, std::uint32_t duration)
{
#ifndef NDEBUG
    assert_thread_ownership_();
#endif
    Event ev;
    ev.tick = current_tick_;
    ev.kind = EventKind::Notification;
    ev.a = duration;
    ev.text = message;
    events_.push_back(std::move(ev));
}

void SimEventLog::push_sound(std::uint32_t sound_id)
{
    push(EventKind::PlaySound, sound_id, 0);
}

std::vector<Event> SimEventLog::drain()
{
#ifndef NDEBUG
    assert_thread_ownership_();
#endif
    std::vector<Event> result;
    result.swap(events_);
    return result;
}

} // namespace og::sim
