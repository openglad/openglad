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
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/test_trace.h>

#include <algorithm>

namespace {

// Ticks a cue silences the next one (#222). Matches the yell cooldown, which
// is the established "you just heard from this seat" interval.
constexpr short kCueThrottleTicks = 30;

// What a failed cast says, or nullptr when it says nothing: a corpse, a
// statless walker or a non-Living body are engine states the player cannot
// answer, so they stay silent rather than blaming the key.
const char* special_failure_message(walker::SpecialFailure why)
{
    switch (why)
    {
        case walker::SpecialFailure::NoMP:           return "NOT ENOUGH MP";
        case walker::SpecialFailure::Disabled:       return "SPECIALS DISABLED";
        case walker::SpecialFailure::ScriptDeclined: return "SPECIAL FAILED";
        case walker::SpecialFailure::None:
        case walker::SpecialFailure::Dead:
        case walker::SpecialFailure::NoStats:
        case walker::SpecialFailure::NotLiving:
            break;
    }
    return nullptr;
}

// A seat-addressed cue with its clang, dropped while the throttle is warm.
void emit_throttled_cue(SimInputDebounce& debounce, short player_num,
                        og::sim::SimEventLog* sim_events, const char* message)
{
    if (message == nullptr || debounce.cue_delay > 0)
        return;
    debounce.cue_delay = kCueThrottleTicks;
    og::sim::emit_sound(sim_events, SOUND_CLANG);
    og::sim::emit_notification(sim_events, message, 0,
                               static_cast<std::int32_t>(player_num));
}

// A seat-addressed cue for a key edge the input layer already debounces
// (character switch, the frozen-input drop): one press, one cue.
void emit_press_cue(short player_num, og::sim::SimEventLog* sim_events,
                    const char* message)
{
    og::sim::emit_sound(sim_events, SOUND_CLANG);
    og::sim::emit_notification(sim_events, message, 0,
                               static_cast<std::int32_t>(player_num));
}

// Cast a player special and voice the reason when nothing happened.
void player_cast_special(walker* control, SimInputDebounce& debounce,
                         short player_num, og::sim::SimEventLog* sim_events)
{
    walker::SpecialFailure why = walker::SpecialFailure::None;
    if (control->special(&why))
        return;
    emit_throttled_cue(debounce, player_num, sim_events,
                       special_failure_message(why));
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
        {
            // #223: the key used to do nothing at all when the company is
            // down to one body. Say so instead of reading as a dead key.
            control = oldcontrol;
            emit_press_cue(player_num, sim_events, "NO ONE TO SWITCH TO");
        }

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

    // Same guarantee, same place: the failure-cue throttle (#222).
    if (debounce.cue_delay > 0)
        debounce.cue_delay--;

    // --- Yell for help ---
    if (pi.was_pressed(InputAction::Yell) && !control->yo_delay()
        && !pi.is_held(InputAction::Shift)
        && !pi.is_held(InputAction::Cheat))
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
        control->set_yo_delay(30);
        result.play_sound = SOUND_YO;
        result.notify_text = "Yo!";
        result.notify_source = control;
        // The authoritative server owns input processing, so the cue has to
        // reach the mirrors as sim events. The render-layer consumer of the
        // result fields below is unreachable during gameplay (view.cpp early
        // return once the local transport shadow is live).
        og::sim::emit_sound(sim_events, SOUND_YO);
        og::sim::emit_notification(sim_events, "Yo!", 0,
                                   static_cast<std::int32_t>(player_num));
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
                og::sim::emit_notification(sim_events, "SUMMONING DEFENSE!", 0,
                                           static_cast<std::int32_t>(player_num));
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
                og::sim::emit_notification(sim_events, "RELEASING MEN!", 0,
                                           static_cast<std::int32_t>(player_num));
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
        {
            // #222: every key from here down is dropped on the floor. Tell
            // the seat its hero is iced rather than letting the pad feel
            // broken.
            if (pi.was_pressed(InputAction::Special) ||
                pi.was_pressed(InputAction::Fire))
                emit_press_cue(player_num, sim_events, "FROZEN!");
            control->stats()->player_thaw_tick(); // 1 -> 0 writes -kFreezeThawImmunityTicks (runaway-specials §3.3)
        }
        result.new_control = control;
        return result;
    }

    // --- Movement and actions ---
    // Make sure we're not performing some queued action ..
    if (control->stats()->commands.empty())
    {
        #ifndef USE_TOUCH_INPUT
        control->set_shifter_down(pi.is_held(InputAction::Shift) ? 1 : 0);
        #else
        if (pi.was_pressed(InputAction::Shift))
        {
            control->set_shifter_down(1);
            player_cast_special(control, debounce, player_num, sim_events);
            control->set_shifter_down(0);
        }
        #endif

        if (pi.was_pressed(InputAction::Special))
        {
            // #222 alliance act-freeze: a hostile-to-my_team caster is
            // skipped by the act phase (game_world.cpp), so the cast fires
            // but its effects sit still. Voice that, and cast exactly as
            // before — the cue may not change a single MP.
            if (level.enemy_freeze > 0 &&
                !control->is_friendly_to_team(
                    static_cast<unsigned char>(level.my_team)))
            {
                emit_throttled_cue(debounce, player_num, sim_events,
                                   "SPECIALS FROZEN");
            }
            player_cast_special(control, debounce, player_num, sim_events);
        }

        if (pi.was_pressed(InputAction::Fire))
            control->init_fire();

        // Holding Special key for rapid use (MP cost naturally rate-limits)
        if (pi.is_held(InputAction::Special))
            player_cast_special(control, debounce, player_num, sim_events);

        int walkx = pi.move_x();
        int walky = pi.move_y();

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
