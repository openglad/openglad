/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The terminal clients' game-flow event reader, shared by the local session
 * (curses_game_runtime.cpp) and the networked host/join sessions
 * (curses_network.cpp). One implementation: both used to carry their own
 * copy of these three definitions.
 */
#pragma once

#include <openglad/gameplay/sim_event_log.h>

#include <cstdint>
#include <string>
#include <vector>

namespace og::curses {

// Pull human-readable notification text out of an event batch into `out`.
// A targeted line (target_player >= 0) is addressed to one global player and
// is dropped by every other seat.
inline void collect_notifications(const og::sim::SimEventBatch& batch,
                                  std::vector<std::string>& out, int local_player)
{
    for (const og::sim::Event& ev : batch.events) {
        if (ev.kind == og::sim::EventKind::Notification && !ev.text.empty() &&
            (ev.target_player < 0 || ev.target_player == local_player))
            out.push_back(ev.text);
    }
}

// Latched level-end state. The authoritative end (win/loss/exit/withdraw) is
// delivered as an EndGame/SetEnd game-flow event; we must NOT store it in the
// mirror world's end/ending fields, because the very next delta snapshot
// re-serializes the server world's end=0 (in return-to-lobby mode the server
// never sets its own world.end) and would clobber it. Latching in the session
// instead is durable.
struct PendingEnd {
    bool ended = false;
    short ending = 0;
    short next_level = -1;
};

// Apply terminal game-flow events: latch any level end into `end` and collect
// notification text. (In the SDL client this is screen::dispatch_sim_event_batch.)
inline void apply_game_flow_batch(const og::sim::SimEventBatch& batch,
                                  PendingEnd& end,
                                  std::vector<std::string>& messages,
                                  int local_player)
{
    for (const og::sim::Event& ev : batch.events) {
        switch (ev.kind) {
        case og::sim::EventKind::EndGame:
            end.ended = true;
            end.ending = static_cast<short>(static_cast<std::int32_t>(ev.a));
            end.next_level = static_cast<short>(static_cast<std::int32_t>(ev.b));
            break;
        case og::sim::EventKind::SetEnd:
            end.ended = true;
            break;
        case og::sim::EventKind::Notification:
            if (!ev.text.empty() &&
                (ev.target_player < 0 || ev.target_player == local_player))
                messages.push_back(ev.text);
            break;
        default:
            break;
        }
    }
}

} // namespace og::curses
