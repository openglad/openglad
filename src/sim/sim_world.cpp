/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/sim/sim_world.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/runtime/screen.h>
#include <openglad/entities/walker.h>
#include <openglad/render/view.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/legacy/base.h>
#include <format>

namespace og::sim {

TickResult SimWorld::tick(screen& scr, SimEventLog& events)
{
    TickResult result;
    tick_count_++;
    events.set_tick(tick_count_);

    result.level_done = 2; // unless we find valid foes while looping

    if (scr.enemy_freeze)
        scr.enemy_freeze--;
    if (scr.enemy_freeze == 1)
        set_palette(scr.ourpalette);

    // --- Entity act phase ---
    for (auto& uptr : scr.level_data.oblist)
    {
        walker* ob = uptr.get();
        if (!scr.enemy_freeze) // normal functionality
        {
            if (ob && !ob->dead)
            {
                ob->in_act = true;
                ob->act();
                ob->in_act = false;
                if (ob && !ob->dead)
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(scr.save_data.my_team)) &&
                        ob->query_order() == Order::Living)
                        result.level_done = 0;
                    if (ob->foe == nullptr && ob->leader == nullptr)
                        ob->foe = scr.find_far_foe(ob);
                }
            }
        }
        else // enemy livings are frozen
        {
            if (!(scr.enemy_freeze % 10) && result.level_done == 2)
            {
                std::string obmessage = std::format("TIME LEFT: {}", scr.enemy_freeze);
                events.push_notification(obmessage);
            }
            if (ob && !ob->dead &&
                (((ob->query_order() != Order::Living) &&
                  (ob->query_order() != Order::Generator)) ||
                 (ob->team_num == 0)))
            {
                ob->act();
                if (ob && !ob->dead)
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(scr.save_data.my_team)) &&
                        ob->query_order() == Order::Living)
                        result.level_done = 0;
                }
            }
        }
    }

    // --- Weapon act phase ---
    for (auto& uptr : scr.level_data.weaplist)
    {
        walker* ob = uptr.get();
        if (ob && !ob->dead)
        {
            ob->act();
            if (ob && !ob->dead)
            {
                if (!ob->is_friendly_to_team(static_cast<unsigned char>(scr.save_data.my_team)) &&
                    ob->query_order() == Order::Living)
                    result.level_done = 0;
            }
        }
    }

    // --- Check background for exits ---
    for (auto& uptr : scr.level_data.fxlist)
    {
        walker* ob = uptr.get();
        if (ob && !ob->dead)
        {
            if (ob->query_order() == Order::Treasure &&
                ob->query_family() == FAMILY_EXIT &&
                result.level_done != 0)
            {
                result.level_done = 1;
            }
        }
    }

    // --- Level completion check ---
    if (result.level_done == 2)
    {
        result.game_ended = true;
        result.ending = 0;
        result.next_level = static_cast<short>(scr.level_data.id + 1);
        return result;
    }

    if (scr.end)
    {
        result.game_ended = true;
        return result;
    }

    // --- Cleanup stale pointers ---
    for (auto& uptr : scr.level_data.oblist)
    {
        walker* ob = uptr.get();
        if (ob->foe && ob->foe->dead)
            ob->foe = nullptr;
        if (ob->leader && ob->leader->dead)
            ob->leader = nullptr;
        if (ob->owner && ob->owner->dead)
            ob->owner = nullptr;
        if (ob->collide_ob && ob->collide_ob->dead)
            ob->collide_ob = nullptr;
    }

    for (auto& uptr : scr.level_data.weaplist)
    {
        walker* ob = uptr.get();
        if (ob->foe && ob->foe->dead)
            ob->foe = nullptr;
        if (ob->leader && ob->leader->dead)
            ob->leader = nullptr;
        if (ob->owner && ob->owner->dead)
            ob->owner = nullptr;
        if (ob->collide_ob && ob->collide_ob->dead)
            ob->collide_ob = nullptr;
    }

    // --- Remove dead entities ---
    for (auto e = scr.level_data.oblist.begin(); e != scr.level_data.oblist.end();)
    {
        walker* ob = e->get();
        if (ob && ob->dead && ob->myguy == nullptr)
        {
            // Is it a player?
            if (ob->user != -1)
            {
                for (int i = 0; i < scr.numviews; i++)
                {
                    if (ob == scr.viewob[i]->control)
                        scr.viewob[i]->control = nullptr;
                }
            }

            scr.level_data.dead_list.push_back(std::move(*e));

            if (ob->query_order() == Order::Living)
                scr.level_data.numobs--;

            e = scr.level_data.oblist.erase(e);
            continue;
        }
        e++;
    }

    std::erase_if(scr.level_data.fxlist, [](const auto& uptr) {
        walker* ob = uptr.get();
        return ob && ob->dead;
    });

    std::erase_if(scr.level_data.weaplist, [](const auto& uptr) {
        walker* ob = uptr.get();
        return ob && ob->dead;
    });

    return result;
}

} // namespace og::sim
