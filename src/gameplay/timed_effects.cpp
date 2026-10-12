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
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>

namespace og::sim {

namespace {

// Tie order: POSSESS 0, the marker roles 1..5 as listed, then the classic
// timers SUMMONED 6 .. HASTE 12 (the loss of a body or of control before
// the buffs).
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

constexpr int kRankSummoned = 6;
constexpr int kRankCharmed = 7;
constexpr int kRankStunned = 8;
constexpr int kRankInvisible = 9;
constexpr int kRankInvincible = 10;
constexpr int kRankFlying = 11;
constexpr int kRankHaste = 12;

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
    // PHASE writes the walker's invulnerability and speed beside its veil:
    // while the veil runs those two fields ARE the phase, not a potion.
    bool phased = false;

    // The kit's own effects exist only with the New Specials setting on;
    // with it off they are skipped, so a staged one cannot leak.
    if (world.new_specials != 0)
    {
        // The host carries the possession's clock; the rider is the hidden
        // one.
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
            if (m == nullptr)
                continue;
            if (m->role == MARKER_PHASE_VEIL)
                phased = true;
            best.offer(m->label, ob->lifetime(), m->rank);
        }
    }

    // The classic timers, shown with the setting off too. A summoned body
    // the seat drives dies when its own lifetime runs out.
    if (control.lifetime() > 0)
        best.offer("SUMMONED", control.lifetime(), kRankSummoned);
    if (control.charm_left() > 0)
        best.offer("CHARMED", control.charm_left(), kRankCharmed);
    if (control.stats() != nullptr && control.stats()->frozen_delay() > 0)
        best.offer("STUNNED", control.stats()->frozen_delay(), kRankStunned);
    if (control.invisibility_left() > 0)
        best.offer("INVISIBLE", control.invisibility_left(), kRankInvisible);
    if (!phased && control.invulnerable_left() > 0)
        best.offer("INVINCIBLE", control.invulnerable_left(), kRankInvincible);
    // A walker stuck over ground it cannot stand on is held at 1 every
    // tick, so 1 is not a flight that is ending.
    if (control.flight_left() > 1)
        best.offer("FLYING", control.flight_left(), kRankFlying);
    // The speed bonus counts down only while above 1 and then sticks at 1
    // for good, so the ticks left are one less than the field.
    if (!phased && control.speed_bonus_left() > 1)
        best.offer("HASTE", control.speed_bonus_left() - 1, kRankHaste);
    return best.timer;
}

int timer_seconds(int ticks) noexcept
{
    return (ticks + 6) / 12;
}

}  // namespace og::sim
