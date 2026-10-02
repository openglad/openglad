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
 * copy of these three definitions, and of the follow-id resolver.
 */
#pragma once

#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace og::curses {

// The local (single-player) curses session binds its one seat as global
// player 0.
inline constexpr std::size_t kLocalSeatPlayerIndex = 0;

// Resolve the entity a terminal seat's view follows: the seat's mapped avatar
// while that walker is in the mirror and alive, else the first living walker
// tagged with the seat's player index, else 0. A dead or missing mapped id is
// never returned, so the camera and HUD never rest on a corpse or on an id
// the mirror does not hold yet. The client's controlled-entity map is GLOBAL
// (indexed by player index, identical on every peer). One rule for the local
// session and the networked host/join sessions.
inline std::uint32_t resolve_followed_entity_id(const og::sim::GameClient& client,
                                                const GameWorld& mirror,
                                                std::size_t local_player_index)
{
    const auto& ids = client.controlled_entity_ids();
    if (local_player_index < ids.size() && ids[local_player_index] != 0) {
        if (const walker* w = mirror.find_by_id(ids[local_player_index]);
            w != nullptr && !w->dead())
            return ids[local_player_index];
    }
    // Fallback: the first living entity this player controls (user == index).
    for (const auto& up : mirror.oblist) {
        const walker* w = up.get();
        if (w && !w->dead() &&
            w->user() == static_cast<int>(local_player_index))
            return w->entity_id();
    }
    return 0;
}

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
