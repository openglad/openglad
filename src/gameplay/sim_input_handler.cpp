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
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/test_trace.h>

#include <algorithm>

walker* sim_find_next_control(GameWorld& level, short my_team)
{
    TRACE("sim_input", "find_next_control for team %d", my_team);

    // First look for a player character, not already controlled
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            w->query_order() == Order::Living &&
            w->user == -1 &&
            w->myguy &&
            w->team_num == my_team)
        {
            TRACE("sim_input", "found player character '%s'", w->stats()->name.c_str());
            return w;
        }
    }

    // Second, look for anyone on our team
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            w->query_order() == Order::Living &&
            w->user == -1 &&
            w->team_num == my_team)
        {
            TRACE("sim_input", "found team member '%s'", w->stats()->name.c_str());
            return w;
        }
    }

    // Now try for ANYONE who's left alive
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            w->query_order() == Order::Living &&
            w->myguy)
        {
            TRACE("sim_input", "found fallback character '%s'", w->stats()->name.c_str());
            return w;
        }
    }

    TRACE("sim_input", "found no one");
    return nullptr;
}

walker* sim_cycle_next_character(
    std::list<std::unique_ptr<walker>>& oblist,
    walker* current,
    bool reverse,
    const std::function<bool(const walker*)>& pred)
{
    auto is_current = [current](const std::unique_ptr<walker>& p) { return p.get() == current; };

    if (!reverse)
    {
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
    const std::string (*special_names)[6],
    [[maybe_unused]] og::sim::SimEventLog* sim_events)
{
    SimInputResult result;
    walker* oldcontrol = control;

    // --- Control setup ---
    if (control && control->user == -1)
    {
        control->set_act_type(ACT_CONTROL);
        control->user = static_cast<signed char>(player_num);
        control->stats()->clear_command();
    }
    if (!control || control->dead)
    {
        control = sim_find_next_control(level, my_team);
        if (!control)
        {
            result.endgame_requested = true;
            result.endgame_type = 1;
            return result;
        }
        if (control->user == -1)
            control->user = static_cast<signed char>(player_num);
        control->set_act_type(ACT_CONTROL);
        result.control_hp_changed = true;
        result.control_hp = control->stats()->hitpoints;
    }

    // --- Bonus rounds ---
    if (control && control->bonus_rounds)
    {
        control->bonus_rounds = control->bonus_rounds - 1;
        if (control->lastx != 0.0f || control->lasty != 0.0f)
            control->walk();
    }

    // --- Switch character ---
    if (!pi.was_pressed(InputAction::SwitchChar))
        debounce.changedchar = 0;
    else if (!debounce.changedchar && !pi.is_held(InputAction::Cheat))
    {
        bool reverse = pi.is_held(InputAction::Shift);
        debounce.changedchar = 1;

        if (control->user == player_num)
        {
            control->restore_act_type();
            control->user = -1;
        }
        control = nullptr;

        auto filter = [oldcontrol, my_team](const walker* w) {
            return w->query_order() == Order::Living &&
                   w->is_friendly(oldcontrol) && w->team_num == my_team &&
                   w->real_team_num == 255 && w->user == -1;
        };
        control = sim_cycle_next_character(level.oblist, oldcontrol, reverse, filter);

        if (!control)
            control = oldcontrol;

        result.control_hp_changed = true;
        result.control_hp = control->stats()->hitpoints;
    }

    // --- Switch special ---
    if (!pi.was_pressed(InputAction::SwitchSpecial))
        debounce.changedspec = 0;

    if (pi.was_pressed(InputAction::SwitchSpecial) && !debounce.changedspec)
    {
        debounce.changedspec = 1;
        control->current_special = control->current_special + 1;

        const int special_index = static_cast<int>(control->current_special);
        const int family_index = static_cast<int>(static_cast<unsigned char>(control->family));
        bool special_missing = true;
        if (special_names != nullptr &&
            family_index >= 0 && family_index < NUM_FAMILIES &&
            special_index >= 0 && special_index < NUM_SPECIALS)
        {
            special_missing = (special_names[family_index][special_index] == "NONE");
        }

        if (special_index < 0 || special_index > (NUM_SPECIALS - 1)
            || special_missing
            || (((control->current_special - 1) * 3 + 1) > control->stats()->level))
            control->current_special = 1;
    }

    // --- yo_delay tick ---
    if (control->yo_delay > 0)
        control->yo_delay = control->yo_delay - 1;

    // --- Yell for help ---
    if (pi.was_pressed(InputAction::Yell) && !control->yo_delay
        && !pi.is_held(InputAction::Shift)
        && !pi.is_held(InputAction::Cheat))
    {
        for (auto& uptr : level.oblist)
        {
            walker* w = uptr.get();
            if (w && (w->query_order() == Order::Living) &&
                (w->act_type != ACT_CONTROL) &&
                (w->team_num == control->team_num) &&
                (!w->leader))
            {
                w->leader = control;
                w->foe = nullptr;
                w->stats()->force_command(COMMAND_FOLLOW, 100, 0, 0);
            }
        }
        control->yo_delay = 30;
        result.play_sound = SOUND_YO;
        result.notify_text = "Yo!";
        result.notify_source = control;
    }

    // --- Shift+Yell: summon/release ---
    if (pi.is_held(InputAction::Shift) && pi.was_pressed(InputAction::Yell)
        && !pi.is_held(InputAction::Cheat))
    {
        switch (control->action)
        {
            case 0:
                for (auto& uptr : level.oblist)
                {
                    walker* w = uptr.get();
                    if (w && (w->team_num == control->team_num) && w->is_friendly(control))
                    {
                        w->leader = control;
                        w->foe = nullptr;
                        w->action = ACTION_FOLLOW;
                    }
                }
                result.notify_text = "SUMMONING DEFENSE!";
                result.notify_source = control;
                break;
            case ACTION_FOLLOW:
                for (auto& uptr : level.oblist)
                {
                    walker* w = uptr.get();
                    if (w && (w->query_order() == Order::Living) &&
                        (w->act_type != ACT_CONTROL) &&
                        (w->team_num == control->team_num))
                    {
                        w->action = 0;
                    }
                }
                control->action = 0;
                result.notify_text = "RELEASING MEN!";
                result.notify_source = control;
                break;
            default:
                control->action = 0;
                break;
        }
    }

    // --- Ensure correct user assignment ---
    if (control->user != player_num)
    {
        result.new_control = control;
        return result;
    }

    if (control->ani_type != ANI_WALK)
        control->animate();

    if (control != oldcontrol)
        control->stats()->clear_command();

    if (control->dead || control->stats()->frozen_delay)
    {
        if (control->stats()->frozen_delay)
            control->stats()->frozen_delay--;
        result.new_control = control;
        return result;
    }

    // --- Movement and actions ---
    if (control->stats()->commands.empty())
    {
        #ifndef USE_TOUCH_INPUT
        control->shifter_down = pi.is_held(InputAction::Shift) ? 1 : 0;
        #else
        if (pi.was_pressed(InputAction::Shift))
        {
            control->shifter_down = 1;
            control->special();
            control->shifter_down = 0;
        }
        #endif

        if (pi.was_pressed(InputAction::Special))
            control->special();

        if (pi.was_pressed(InputAction::Fire))
            control->init_fire();

        if (pi.is_held(InputAction::Special))
            control->special();

        int walkx = pi.move_x();
        int walky = pi.move_y();

        if (walkx != 0 || walky != 0)
        {
            control->walkstep(walkx, walky);
        }
        else if (control->stats()->query_bit_flags(BIT_ANIMATE))
        {
            control->cycle = control->cycle + 1;
            if (control->ani[control->curdir][control->cycle] == -1)
                control->cycle = 0;
            control->set_frame(control->ani[control->curdir][control->cycle]);
        }

        if (pi.is_held(InputAction::Fire))
            control->init_fire();
    }

    result.new_control = control;
    return result;
}
