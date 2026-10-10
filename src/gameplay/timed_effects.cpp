/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// The seat's countdown (see timed_effects.h): one read of the walker and
// of the kit markers it owns, from fields every snapshot carries.
#include <openglad/gameplay/timed_effects.h>

#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/kit_marker_role.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>

namespace og::sim {

namespace {

// Tie order: POSSESS 0, the marker roles 1..5 as listed, HASTE 6.
struct MarkerLabel {
    std::uint8_t role;
    const char* label;
    int rank;
};

constexpr MarkerLabel kMarkerLabels[] = {
    {MARKER_PHASE_VEIL, "PHASE", 1},
    {MARKER_BURROW, "DIG IN", 2},
    {MARKER_WARD, "REASSEMBLE", 3},
    {MARKER_IMMOLATION, "IMMOLATE", 4},
    {MARKER_LEGION, "LEGION", 5},
};

constexpr int kRankHaste = 6;

struct Best {
    SeatTimer timer{nullptr, 0};
    int rank = 0;

    void offer(const char* label, int ticks, int candidate_rank) noexcept
    {
        if (timer.label == nullptr || ticks < timer.ticks ||
            (ticks == timer.ticks && candidate_rank < rank)) {
            timer = SeatTimer{label, ticks};
            rank = candidate_rank;
        }
    }
};

const MarkerLabel* label_for_role(char role) noexcept
{
    for (const MarkerLabel& m : kMarkerLabels) {
        if (static_cast<char>(m.role) == role)
            return &m;
    }
    return nullptr;
}

}  // namespace

SeatTimer seat_timer(const GameWorld& world, const walker& control) noexcept
{
    Best best;
    if (world.new_specials == 0)
        return best.timer;

    // The host carries the possession's clock; the rider is the hidden one.
    if (control.possess_link() != 0 && !control.hidden() &&
        control.possess_ticks() > 0)
        best.offer("POSSESS", control.possess_ticks(), 0);

    // The marker family is resolved by name, and only once a candidate
    // turns up: most walkers own no running effect at all, and the text
    // client asks this for every living each tick.
    int marker_family = -2;  // -2: not resolved yet
    for (const auto& entry : world.oblist) {
        const walker* ob = entry.get();
        if (ob == nullptr || ob->dead() || ob->order() != Order::FX)
            continue;
        if (ob->owner() != &control || ob->lifetime() <= 0)
            continue;
        if (marker_family == -2)
            marker_family = og::families::resolve_family_string_id(
                Order::FX, "core:kit_marker");
        if (static_cast<int>(static_cast<unsigned char>(ob->family())) !=
            marker_family)
            continue;
        const MarkerLabel* m = label_for_role(ob->ani_type());
        if (m != nullptr)
            best.offer(m->label, ob->lifetime(), m->rank);
    }

    if (control.speed_bonus_left() > 0)
        best.offer("HASTE", control.speed_bonus_left(), kRankHaste);
    return best.timer;
}

int timer_seconds(int ticks) noexcept
{
    return (ticks + 11) / 12;
}

}  // namespace og::sim
