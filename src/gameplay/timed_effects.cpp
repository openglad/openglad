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
        // rule: The seat's HUD timer shows the running effect that ends soonest; a tie goes to the
        //   fixed order POSSESS, PHASE, DIG IN, REASSEMBLE, IMMOLATE, LEGION, SUMMONED, CHARMED,
        //   STUNNED, INVISIBLE, INVINCIBLE, FLYING, HASTE.
        // tests: TimedEffects.the_soonest_ending_effect_is_shown
        // tests: TimedEffects.a_tie_goes_to_the_table_order
        if (timer.label == nullptr || ticks < timer.ticks ||
            (ticks == timer.ticks && candidate_rank < rank)) {
            timer = SeatTimer{label, ticks};
            rank = candidate_rank;
        }
    }
};

const MarkerLabel* label_for_role(char role) noexcept
{
    // rule: A kit marker counts toward a walker's timer only if it is a live core:kit_marker FX
    //   owned by that walker with time left and a known role (PHASE, DIG IN, REASSEMBLE, IMMOLATE,
    //   LEGION); a running phase veil means the walker's invulnerability and speed fields are the
    //   phase, not potions.
    // tests: TimedEffects.only_the_walkers_own_running_markers_count
    // tests: TimedEffects.phase_owns_its_fields
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
    for (const MarkerLabel& m : kMarkerLabels) {
        // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
        //   only ..."
        // tests: TimedEffects.only_the_walkers_own_running_markers_count
        // tests: TimedEffects.phase_owns_its_fields
        // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
        // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
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
    // rule: Kit timers (possession, kit markers) are shown only with New Specials on; with it off a
    //   staged kit marker cannot leak into the HUD, while the classic timers always show.
    // tests: TimedEffects.classic_timers_show_with_the_setting_off_and_kit_timers_do_not
    if (world.new_specials != 0)
    {
        // The host carries the possession's clock; the rider is the hidden
        // one.
        // rule: During a possession, the visible host body shows the POSSESS countdown; the hidden
        //   rider does not.
        // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
        // tests: TimedEffects.only_the_walkers_own_running_markers_count
        if (control.possess_link() != 0 && !control.hidden() &&
            control.possess_ticks() > 0)
            best.offer("POSSESS", control.possess_ticks(), 0);

        // The marker family is resolved by name, and only once a candidate
        // turns up: most walkers own no running effect at all, and the text
        // client asks this for every living each tick.
        int marker_family = -2;  // -2: not resolved yet
        // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
        //   only ..."
        // tests: TimedEffects.only_the_walkers_own_running_markers_count
        // tests: TimedEffects.phase_owns_its_fields
        // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
        // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
        for (const auto& entry : world.oblist) {
            const walker* ob = entry.get();
            // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
            //   only ..."
            // tests: TimedEffects.only_the_walkers_own_running_markers_count
            // tests: TimedEffects.phase_owns_its_fields
            // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
            // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
            if (ob == nullptr || ob->dead() || ob->order() != Order::FX)
                continue;
            // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
            //   only ..."
            // tests: TimedEffects.only_the_walkers_own_running_markers_count
            // tests: TimedEffects.phase_owns_its_fields
            // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
            // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
            if (ob->owner() != &control || ob->lifetime() <= 0)
                continue;
            // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
            //   only ..."
            // tests: TimedEffects.only_the_walkers_own_running_markers_count
            // tests: TimedEffects.phase_owns_its_fields
            // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
            // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
            if (marker_family == -2)
                marker_family = og::families::resolve_family_string_id(
                    Order::FX, "core:kit_marker");
            // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
            //   only ..."
            // tests: TimedEffects.only_the_walkers_own_running_markers_count
            // tests: TimedEffects.phase_owns_its_fields
            // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
            // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
            if (static_cast<int>(static_cast<unsigned char>(ob->family())) !=
                marker_family)
                continue;
            const MarkerLabel* m = label_for_role(ob->ani_type());
            // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
            //   only ..."
            // tests: TimedEffects.only_the_walkers_own_running_markers_count
            // tests: TimedEffects.phase_owns_its_fields
            // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
            // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
            if (m == nullptr)
                continue;
            // rule: Part of the rule above that begins "A kit marker counts toward a walker's timer
            //   only ..."
            // tests: TimedEffects.only_the_walkers_own_running_markers_count
            // tests: TimedEffects.phase_owns_its_fields
            // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
            // tests: TimedEffectsLua.the_marker_roles_reach_lua_with_the_engines_numbers
            if (m->role == MARKER_PHASE_VEIL)
                phased = true;
            best.offer(m->label, ob->lifetime(), m->rank);
        }
    }

    // The classic timers, shown with the setting off too. A summoned body
    // the seat drives dies when its own lifetime runs out.
    // rule: Classic timers shown on the HUD: SUMMONED (own lifetime), CHARMED, STUNNED, INVISIBLE,
    //   INVINCIBLE (unless phased), FLYING (only above 1, since 1 is the hover hold over
    //   unstandable ground), HASTE (down to its last tick, unless phased).
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffects.flight_hover_tick_is_not_shown
    // tests: TimedEffects.haste_last_tick_is_shown_and_zero_is_not
    // tests: TimedEffects.phase_owns_its_fields
    if (control.lifetime() > 0)
        best.offer("SUMMONED", control.lifetime(), kRankSummoned);
    // rule: Part of the rule above that begins "Classic timers shown on the HUD: SUMMONED (own
    //   lifetime), ..."
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffects.flight_hover_tick_is_not_shown
    // tests: TimedEffects.haste_last_tick_is_shown_and_zero_is_not
    // tests: TimedEffects.phase_owns_its_fields
    if (control.charm_left() > 0)
        best.offer("CHARMED", control.charm_left(), kRankCharmed);
    // rule: Part of the rule above that begins "Classic timers shown on the HUD: SUMMONED (own
    //   lifetime), ..."
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffects.flight_hover_tick_is_not_shown
    // tests: TimedEffects.haste_last_tick_is_shown_and_zero_is_not
    // tests: TimedEffects.phase_owns_its_fields
    if (control.stats() != nullptr && control.stats()->frozen_delay() > 0)
        best.offer("STUNNED", control.stats()->frozen_delay(), kRankStunned);
    // rule: Part of the rule above that begins "Classic timers shown on the HUD: SUMMONED (own
    //   lifetime), ..."
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffects.flight_hover_tick_is_not_shown
    // tests: TimedEffects.haste_last_tick_is_shown_and_zero_is_not
    // tests: TimedEffects.phase_owns_its_fields
    if (control.invisibility_left() > 0)
        best.offer("INVISIBLE", control.invisibility_left(), kRankInvisible);
    // rule: Part of the rule above that begins "Classic timers shown on the HUD: SUMMONED (own
    //   lifetime), ..."
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffects.flight_hover_tick_is_not_shown
    // tests: TimedEffects.haste_last_tick_is_shown_and_zero_is_not
    // tests: TimedEffects.phase_owns_its_fields
    if (!phased && control.invulnerable_left() > 0)
        best.offer("INVINCIBLE", control.invulnerable_left(), kRankInvincible);
    // A walker stuck over ground it cannot stand on is held at 1 every
    // tick, so 1 is not a flight that is ending.
    // rule: Part of the rule above that begins "Classic timers shown on the HUD: SUMMONED (own
    //   lifetime), ..."
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffects.flight_hover_tick_is_not_shown
    // tests: TimedEffects.haste_last_tick_is_shown_and_zero_is_not
    // tests: TimedEffects.phase_owns_its_fields
    if (control.flight_left() > 1)
        best.offer("FLYING", control.flight_left(), kRankFlying);
    // The speed bonus counts down to 0, one tick of speed for each.
    // rule: Part of the rule above that begins "Classic timers shown on the HUD: SUMMONED (own
    //   lifetime), ..."
    // tests: TimedEffects.each_running_effect_answers_its_label_and_clock
    // tests: TimedEffects.flight_hover_tick_is_not_shown
    // tests: TimedEffects.haste_last_tick_is_shown_and_zero_is_not
    // tests: TimedEffects.phase_owns_its_fields
    if (!phased && control.speed_bonus_left() > 0)
        best.offer("HASTE", control.speed_bonus_left(), kRankHaste);
    return best.timer;
}

int timer_seconds(int ticks) noexcept
{
    return (ticks + 6) / 12;
}

}  // namespace og::sim
