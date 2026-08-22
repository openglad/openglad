/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/sim_event_log.h>

#include <iterator>
#include <utility>

namespace og::sim {

void SimEventLog::push(EventKind kind, std::uint32_t a, std::uint32_t b)
{
    if (suppressed_)
        return;

    Event ev;
    ev.tick = current_tick_;
    ev.kind = kind;
    ev.a = a;
    ev.b = b;
    events_.push_back(std::move(ev));
}

void SimEventLog::push_with_text(EventKind kind, const std::string& text,
                                 std::uint32_t a, std::uint32_t b)
{
    if (suppressed_)
        return;

    Event ev;
    ev.tick = current_tick_;
    ev.kind = kind;
    ev.a = a;
    ev.b = b;
    ev.text = text;
    events_.push_back(std::move(ev));
}

void SimEventLog::push_notification(const std::string& message, std::uint32_t duration,
                                    std::int32_t target_player)
{
    if (suppressed_)
        return;

    Event ev;
    ev.tick = current_tick_;
    ev.kind = EventKind::Notification;
    ev.a = duration;
    ev.target_player = target_player;
    ev.text = message;
    events_.push_back(std::move(ev));
}

void SimEventLog::push_sound(std::uint32_t sound_id)
{
    push(EventKind::PlaySound, sound_id, 0);
}

std::vector<Event> SimEventLog::drain()
{
    std::vector<Event> result;
    result.swap(events_);
    return result;
}

void SimEventLog::append(std::vector<Event> events)
{
    if (events.empty())
        return;
    events_.insert(events_.end(),
                   std::make_move_iterator(events.begin()),
                   std::make_move_iterator(events.end()));
}

} // namespace og::sim
