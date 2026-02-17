/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/sim/sim_world.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/legacy/base.h>     // FAMILY_EXIT, etc.
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <format>

namespace og::sim {

// File-local helper: find the nearest hostile entity for AI targeting.
// Mirrors screen::find_far_foe() but operates on LevelData directly.
// Uses the sim-layer's own deterministic RNG for invisibility checks.
static walker* find_far_foe(LevelData& level, walker* ob, SimRandom& rng)
{
    if (!ob)
        return nullptr;

    walker* endfoe = nullptr;
    std::int32_t distance = 10000;
    ob->stats()->last_distance = 10000;

    for (auto& uptr : level.oblist)
    {
        walker* foe = uptr.get();
        if (foe == nullptr || foe->dead)
            continue;

        if (ob->is_friendly(foe) == 0)
        {
            if ((foe->query_order() == Order::Living ||
                 foe->query_order() == Order::Generator) &&
                (!(rng.next(foe->invisibility_left / 20))))
            {
                std::int32_t tempdistance = ob->distance_to_ob(foe);
                if (tempdistance < distance)
                {
                    distance = tempdistance;
                    endfoe = foe;
                }
            }
        }
    }
    return endfoe;
}

TickResult SimWorld::tick(LevelData& level, SaveData& save,
                          std::int32_t& enemy_freeze, char end,
                          SimEventLog& events)
{
    TickResult result;
    tick_count_++;
    events.set_tick(tick_count_);

    result.level_done = 2; // unless we find valid foes while looping

    if (enemy_freeze)
        enemy_freeze--;
    if (enemy_freeze == 1)
        events.push(EventKind::SetPalette, 0, 0);

    // --- Entity act phase ---
    bool printed_time = false;
    for (auto& uptr : level.oblist)
    {
        walker* ob = uptr.get();
        if (!enemy_freeze) // normal functionality
        {
            if (ob && !ob->dead)
            {
                ob->in_act = true;
                ob->act();
                ob->in_act = false;
                if (ob && !ob->dead)
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(save.my_team)) &&
                        ob->query_order() == Order::Living)
                        result.level_done = 0;
                    if (ob->foe == nullptr && ob->leader == nullptr)
                        ob->foe = find_far_foe(level, ob, rng_);
                }
            }
        }
        else // enemy livings are frozen
        {
            if (!(enemy_freeze % 10) && !printed_time)
            {
                std::string obmessage = std::format("TIME LEFT: {}", enemy_freeze);
                events.push_notification(obmessage, 10);
                printed_time = true;
            }
            if (ob && !ob->dead &&
                (((ob->query_order() != Order::Living) &&
                  (ob->query_order() != Order::Generator)) ||
                 (ob->team_num == 0)))
            {
                ob->act();
                if (ob && !ob->dead)
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(save.my_team)) &&
                        ob->query_order() == Order::Living)
                        result.level_done = 0;
                }
            }
        }
    }

    // --- Weapon act phase ---
    for (auto& uptr : level.weaplist)
    {
        walker* ob = uptr.get();
        if (ob && !ob->dead)
        {
            ob->act();
            if (ob && !ob->dead)
            {
                if (!ob->is_friendly_to_team(static_cast<unsigned char>(save.my_team)) &&
                    ob->query_order() == Order::Living)
                    result.level_done = 0;
            }
        }
    }

    // --- Check background for exits ---
    for (auto& uptr : level.fxlist)
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
        result.next_level = static_cast<short>(level.id + 1);
        return result;
    }

    if (end)
    {
        result.game_ended = true;
        return result;
    }

    // --- Cleanup stale pointers ---
    for (auto& uptr : level.oblist)
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

    for (auto& uptr : level.weaplist)
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
    // Note: viewscreen control pointer cleanup is handled by the caller
    // (screen::act) since viewscreens are a rendering concern.
    for (auto e = level.oblist.begin(); e != level.oblist.end();)
    {
        walker* ob = e->get();
        if (ob && ob->dead && ob->myguy == nullptr)
        {
            level.dead_list.push_back(std::move(*e));

            if (ob->query_order() == Order::Living)
                level.numobs--;

            e = level.oblist.erase(e);
            continue;
        }
        e++;
    }

    std::erase_if(level.fxlist, [](const auto& uptr) {
        walker* ob = uptr.get();
        return ob && ob->dead;
    });

    std::erase_if(level.weaplist, [](const auto& uptr) {
        walker* ob = uptr.get();
        return ob && ob->dead;
    });

    return result;
}

} // namespace og::sim
