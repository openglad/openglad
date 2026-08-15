/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Sim-layer input processing: translates InputState into entity commands.
// Extracted from viewscreen::process_input() (G11) so entity-driving logic
// lives in the sim layer, not the render layer.

#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/test_trace.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

struct BreakawayTarget
{
    walker* target = nullptr;
    short dx = 0;
    short dy = 0;
};

[[nodiscard]] bool z_ranges_overlap(const walker& lhs, const walker& rhs)
{
    // A zero height is the legacy full-height sentinel used by ob_pass_check.
    if (lhs.sizez() <= 0 || rhs.sizez() <= 0)
        return true;
    const float lhs_top = lhs.worldz() + static_cast<float>(lhs.sizez());
    const float rhs_top = rhs.worldz() + static_cast<float>(rhs.sizez());
    return lhs_top > rhs.worldz() && rhs_top > lhs.worldz();
}

[[nodiscard]] std::int32_t axis_gap(std::int32_t a_start,
                                    std::int32_t a_size,
                                    std::int32_t b_start,
                                    std::int32_t b_size)
{
    const std::int32_t a_end = a_start + a_size;
    const std::int32_t b_end = b_start + b_size;
    return std::max({b_start - a_end, a_start - b_end, 0});
}

[[nodiscard]] int contact_padding(const walker& control)
{
    const float step = control.stepsize();
    if (!std::isfinite(step))
        return 1;
    const float bounded = std::clamp(step, 1.0f,
                                     static_cast<float>(GRID_SIZE));
    return static_cast<int>(std::ceil(bounded));
}

[[nodiscard]] bool is_breakaway_contact(const walker& control,
                                        const walker& target,
                                        int padding)
{
    if (&target == &control || target.dead() || target.dormant() ||
        target.query_order() != Order::Living ||
        target.stats() == nullptr ||
        target.floor() != control.floor() || control.is_friendly(&target) ||
        !z_ranges_overlap(control, target))
    {
        return false;
    }

    const FamilyDescriptor* const family =
        get_family_descriptor(target.family());
    if ((family != nullptr && family->is_stationary) ||
        target.stats()->query_bit_flags(BIT_NO_COLLIDE))
    {
        return false;
    }

    return axis_gap(control.xpos(), control.sizex(),
                    target.xpos(), target.sizex()) <= padding &&
           axis_gap(control.ypos(), control.sizey(),
                    target.ypos(), target.sizey()) <= padding;
}

[[nodiscard]] short unit_delta(std::int32_t delta)
{
    return static_cast<short>((delta > 0) - (delta < 0));
}

[[nodiscard]] std::pair<short, short> breakaway_direction(
    const walker& control, const walker& target, std::size_t stable_ordinal)
{
    const std::int32_t dx =
        (static_cast<std::int32_t>(target.xpos()) * 2 + target.sizex()) -
        (static_cast<std::int32_t>(control.xpos()) * 2 + control.sizex());
    const std::int32_t dy =
        (static_cast<std::int32_t>(target.ypos()) * 2 + target.sizey()) -
        (static_cast<std::int32_t>(control.ypos()) * 2 + control.sizey());
    if (dx != 0 || dy != 0)
        return {unit_delta(dx), unit_delta(dy)};

    // Interpenetrating equal-centre bodies have no geometric "outward".
    // Entity id is stable across snapshots/replays; list order is the stable
    // construction fallback for headless fixtures and pre-id entities.
    static constexpr std::array<std::pair<short, short>, NUM_FACINGS>
        kFallbackDirections = {{{0, -1}, {1, -1}, {1, 0}, {1, 1},
                                {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    const std::size_t key = target.entity_id() != 0
        ? static_cast<std::size_t>(target.entity_id() - 1)
        : stable_ordinal;
    return kFallbackDirections[key % kFallbackDirections.size()];
}

[[nodiscard]] bool breakaway_actionable(const walker& control,
                                        short player_num)
{
    return control.user() == player_num && control.act_type() == ACT_CONTROL &&
           control.ani_type() == ANI_WALK && control.busy() <= 0.0f &&
           control.stats() != nullptr &&
           control.stats()->frozen_delay() == 0 &&
           control.stats()->commands.empty();
}

bool apply_modern_breakaway(GameWorld& level, walker& control,
                            short player_num)
{
    if (level.dynamics_ruleset != og::sim::DynamicsRuleset::Modern ||
        !breakaway_actionable(control, player_num))
    {
        return false;
    }

    const int padding = contact_padding(control);
    std::vector<BreakawayTarget> contacts;
    contacts.reserve(4);
    std::size_t stable_ordinal = 0;
    for (const auto& owned : level.oblist)
    {
        walker* const target = owned.get();
        if (target != nullptr &&
            is_breakaway_contact(control, *target, padding))
        {
            const auto [dx, dy] =
                breakaway_direction(control, *target, stable_ordinal);
            contacts.push_back({target, dx, dy});
        }
        ++stable_ordinal;
    }

    if (contacts.size() < 2)
        return false;

    for (const BreakawayTarget& contact : contacts)
    {
        contact.target->face_delta(contact.dx, contact.dy);
        // Make the outward snap visible even if the ensuing forced walk is
        // blocked. Preserve attack poses and their animation cycles.
        if (contact.target->ani_type() == ANI_WALK)
            contact.target->set_frame_from_current_walk_animation();
        contact.target->stats()->force_command(
            COMMAND_WALK, og::sim::kBreakawayWalkTicks,
            contact.dx, contact.dy);
    }
    control.set_busy(std::max(control.busy(),
                              og::sim::kBreakawayRecoveryTicks));
    return true;
}

} // namespace

walker* sim_find_next_control(GameWorld& level, short my_team)
{
    TRACE("sim_input", "find_next_control for team %d", my_team);

    // First look for a player character, not already controlled
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() && !w->dormant() &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->myguy &&
            w->team_num() == my_team)
        {
            TRACE("sim_input", "found player character '%s'", w->stats()->name.c_str());
            return w;
        }
    }

    // Second, look for anyone on our team
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() && !w->dormant() &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->team_num() == my_team)
        {
            TRACE("sim_input", "found team member '%s'", w->stats()->name.c_str());
            return w;
        }
    }

    // The 2002 third pass announced itself with the wonderfully blunt
    // "Now try for ANYONE who's left alive." Keep the line as history, not
    // behavior: a seat may never fall through to another team. A foreign
    // company hero is an enemy, not an emergency replacement body.
    TRACE("sim_input", "found no one");
    return nullptr;
}

walker* sim_cycle_next_character(
    const std::list<std::unique_ptr<walker>>& oblist,
    walker* current,
    bool reverse,
    const std::function<bool(const walker*)>& pred)
{
    auto is_current = [current](const std::unique_ptr<walker>& p) { return p.get() == current; };

    if (!reverse)
    {
        // Get where we are in the list
        auto mine = std::find_if(oblist.begin(), oblist.end(), is_current);
        if (mine == oblist.end())
            return nullptr;
        // Search from after current to end
        for (auto e = std::next(mine); e != oblist.end(); ++e)
        {
            walker* w = e->get();
            if (w && pred(w))
                return w;
        }
        // Wrap: search from begin to current
        for (auto e = oblist.begin(); e != mine; ++e)
        {
            walker* w = e->get();
            if (w && pred(w))
                return w;
        }
    }
    else
    {
        auto mine = std::find_if(oblist.rbegin(), oblist.rend(), is_current);
        if (mine == oblist.rend())
            return nullptr;
        for (auto e = std::next(mine); e != oblist.rend(); ++e)
        {
            walker* w = e->get();
            if (w && pred(w))
                return w;
        }
        for (auto e = oblist.rbegin(); e != mine; ++e)
        {
            walker* w = e->get();
            if (w && pred(w))
                return w;
        }
    }
    return nullptr;
}

SimInputResult sim_process_player_input(
    const PlayerInput& pi,
    walker*& control,
    GameWorld& level,
    short player_num,
    short my_team,
    SimInputDebounce& debounce,
    const std::string (*special_names)[NUM_SPECIALS],
    og::sim::SimEventLog* sim_events)
{
    SimInputResult result;
    walker* oldcontrol = control;

    // --- Control setup ---
    if (control && control->user() == -1)
    {
        control->set_act_type(ACT_CONTROL);
        control->set_user(static_cast<signed char>(player_num));
        control->stats()->clear_command_for_control_switch(); // forced fright + charm survive (runaway-specials §4)
    }
    if (!control || control->dead())
    {
        // Respawning players keep their exact entity assignment. The normal
        // death path below auto-claims another teammate, which makes the
        // player miss the countdown and then leaves the revived hero as AI.
        // This predicate also covers the just-died, pre-death-scan tick.
        if (control != nullptr &&
            og::sim::respawn_retains_player_control(level, control))
        {
            result.new_control = control;
            return result;
        }

        // §4.4 site 2 (death auto-switch / entry claim): Follow ⇒ null seat,
        // NO endgame request (server broadcasts ControlChange entity 0);
        // EndGame ⇒ today's result fields; Claimed ⇒ today's claim tail.
        if (og::sim::sim_reacquire_apply(level, my_team, player_num, control, result))
            return result;
        if (control->user() == -1)
            control->set_user(static_cast<signed char>(player_num));
        control->set_act_type(ACT_CONTROL);
        result.control_hp_changed = true;
        result.control_hp = control->stats()->hitpoints();
    }

    // --- Bonus rounds ---
    if (control && control->bonus_rounds())
    {
        control->set_bonus_rounds(control->bonus_rounds() - 1);
        if (control->lastx() != 0.0f || control->lasty() != 0.0f)
            control->walk();
    }

    // --- Switch character ---
    if (!pi.was_pressed(InputAction::SwitchChar))
        debounce.changedchar = 0;
    else if (!debounce.changedchar && !pi.is_held(InputAction::Cheat))
    {
        // KEY_SHIFTER will go backward
        bool reverse = pi.is_held(InputAction::Shift);
        debounce.changedchar = 1;

        // Unset our control
        if (control->user() == player_num)
        {
            control->restore_act_type();
            control->set_user(-1);
        }
        control = nullptr;

        auto filter = [&level, oldcontrol, my_team, player_num](const walker* w) {
            // Never hand control to a dead or dormant (delayed-spawn) ally:
            // dormant walkers are invisible, out of the obmap, skipped by the
            // act phase, and excluded from snapshots, so selecting one strands
            // the player on a ghost and blanks the HUD (bugs A1/A10).
            return !w->dead() && !w->dormant() &&
                   w->query_order() == Order::Living &&
                   w->is_friendly(oldcontrol) && w->team_num() == my_team &&
                   w->real_team_num() == 255 && w->user() == -1 &&
                   og::sim::control_claim_allowed(level, w, player_num); // §4.4 site 1
        };
        control = sim_cycle_next_character(level.oblist, oldcontrol, reverse, filter);

        if (!control)
            control = oldcontrol;

        result.control_hp_changed = true;
        result.control_hp = control->stats()->hitpoints();
    }

    // --- Switch special ---
    if (!pi.was_pressed(InputAction::SwitchSpecial))
        debounce.changedspec = 0;

    if (pi.was_pressed(InputAction::SwitchSpecial) && !debounce.changedspec)
    {
        debounce.changedspec = 1;
        control->set_current_special(control->current_special() + 1);

        const int special_index = static_cast<int>(control->current_special());
        const int family_index = static_cast<int>(static_cast<unsigned char>(control->family()));
        bool special_missing = true;
        if (special_names != nullptr &&
            family_index >= 0 && family_index < NUM_FAMILIES &&
            special_index >= 0 && special_index < NUM_SPECIALS)
        {
            special_missing = (special_names[family_index][special_index] == "NONE");
        }

        if (special_index < 0 || special_index > (NUM_SPECIALS - 1)
            || special_missing
            || (((control->current_special() - 1) * 3 + 1) > control->stats()->level()))
            control->set_current_special(1);
    }

    // --- yo_delay tick ---
    // Make sure we haven't yelled recently (this is here because it is
    // guaranteed to run exactly once each frame)
    if (control->yo_delay() > 0)
        control->set_yo_delay(control->yo_delay() - 1);

    // --- Yell for help ---
    if (pi.was_pressed(InputAction::Yell) && !control->yo_delay()
        && !pi.is_held(InputAction::Shift)
        && !pi.is_held(InputAction::Cheat))
    {
        const bool did_breakaway =
            apply_modern_breakaway(level, *control, player_num);
        if (!did_breakaway)
        {
            for (auto& uptr : level.oblist)
            {
                walker* w = uptr.get();
                if (w && (w->query_order() == Order::Living) &&
                    (w->act_type() != ACT_CONTROL) &&
                    (w->team_num() == control->team_num()) &&
                    (!w->leader()))
                {
                    w->set_leader(control);
                    // Remove any current foe ..
                    w->set_foe(nullptr);
                    w->stats()->force_command(COMMAND_FOLLOW, 100, 0, 0);
                }
            }
        }
        control->set_yo_delay(30);
        result.play_sound = SOUND_YO;
        result.notify_text = "Yo!";
        result.notify_source = control;
        // The authoritative server owns input processing, so the cue has to
        // reach the mirrors as sim events. The render-layer consumer of the
        // result fields below is unreachable during gameplay (view.cpp early
        // return once the local transport shadow is live).
        og::sim::emit_sound(sim_events, SOUND_YO);
        og::sim::emit_event_text(sim_events, og::sim::EventKind::Notification, "Yo!");
    }

    // --- Shift+Yell: summon/release ---
    if (pi.is_held(InputAction::Shift) && pi.was_pressed(InputAction::Yell)
        && !pi.is_held(InputAction::Cheat))
    {
        switch (control->action())
        {
            case 0:
                for (auto& uptr : level.oblist)
                {
                    walker* w = uptr.get();
                    if (w && (w->team_num() == control->team_num()) && w->is_friendly(control))
                    {
                        w->set_leader(control);
                        w->set_foe(nullptr);
                        w->set_action(ACTION_FOLLOW);
                    }
                }
                result.notify_text = "SUMMONING DEFENSE!";
                result.notify_source = control;
                og::sim::emit_event_text(sim_events, og::sim::EventKind::Notification,
                                         "SUMMONING DEFENSE!");
                break;
            case ACTION_FOLLOW:
                for (auto& uptr : level.oblist)
                {
                    walker* w = uptr.get();
                    if (w && (w->query_order() == Order::Living) &&
                        (w->act_type() != ACT_CONTROL) &&
                        (w->team_num() == control->team_num()))
                    {
                        // Set to normal operation
                        w->set_action(0);
                    }
                }
                control->set_action(0);
                result.notify_text = "RELEASING MEN!";
                result.notify_source = control;
                og::sim::emit_event_text(sim_events, og::sim::EventKind::Notification,
                                         "RELEASING MEN!");
                break;
            default:
                control->set_action(0);
                break;
        }
    }

    // --- Ensure correct user assignment ---
    if (control->user() != player_num)
    {
        result.new_control = control;
        return result;
    }

    if (control->ani_type() != ANI_WALK)
        control->animate();

    // if we changed control characters
    if (control != oldcontrol)
        control->stats()->clear_command_for_control_switch(); // forced fright + charm survive (runaway-specials §4)

    if (control->dead() || control->stats()->frozen_delay())
    {
        if (control->stats()->frozen_delay())
            control->stats()->player_thaw_tick(); // 1 -> 0 writes -kFreezeThawImmunityTicks (runaway-specials §3.3)
        result.new_control = control;
        return result;
    }

    // --- Movement and actions ---
    // Make sure we're not performing some queued action ..
    if (control->stats()->commands.empty())
    {
        const int walkx = pi.move_x();
        const int walky = pi.move_y();
        if (level.dynamics_ruleset == og::sim::DynamicsRuleset::Modern &&
            (walkx != 0 || walky != 0))
        {
            // Resolve intent before actions: movement, weapon heading and
            // specials all observe the same direction on this tick.
            control->face_delta(static_cast<short>(walkx),
                                static_cast<short>(walky));
            // Stationary/zero-step actors still need a nonzero aim vector:
            // init_fire() and directional specials read lastx/lasty before
            // walkstep() reaches the stationary-family branch.
            if (control->stepsize() == 0.0f)
            {
                control->set_lastx(static_cast<float>(walkx));
                control->set_lasty(static_cast<float>(walky));
            }
            // A blocked walk normally leaves its frame untouched. Refresh
            // only the walk pose so a successful snap is visible even when
            // the requested translation is obstructed; never disturb an
            // attack animation or its cycle.
            if (control->ani_type() == ANI_WALK)
                control->set_frame_from_current_walk_animation();
        }

        #ifndef USE_TOUCH_INPUT
        control->set_shifter_down(pi.is_held(InputAction::Shift) ? 1 : 0);
        #else
        if (pi.was_pressed(InputAction::Shift))
        {
            control->set_shifter_down(1);
            control->special();
            control->set_shifter_down(0);
        }
        #endif

        if (pi.was_pressed(InputAction::Special))
            control->special();

        if (pi.was_pressed(InputAction::Fire))
            control->init_fire();

        // Holding Special key for rapid use (MP cost naturally rate-limits)
        if (pi.is_held(InputAction::Special))
            control->special();

        if (walkx != 0 || walky != 0)
        {
            control->walkstep(walkx, walky);
        }
        else if (control->stats()->query_bit_flags(BIT_ANIMATE))
        {
            control->set_cycle(control->cycle() + 1);
            control->set_frame_from_current_walk_animation();
        }

        if (pi.is_held(InputAction::Fire))
            control->init_fire();
    }

    result.new_control = control;
    return result;
}
