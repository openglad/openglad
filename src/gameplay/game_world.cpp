/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/game_world.h>

#include <openglad/data/gloader.h>
#include <openglad/data/level_data.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>

#include <algorithm>

GameWorld::~GameWorld()
{
    if (level_data_ != nullptr)
        level_data_->attach_world(nullptr);
}

void GameWorld::set_level_data(LevelData* level_data)
{
    level_data_ = level_data;
}

walker* GameWorld::add_to_list(Order order, std::int32_t family,
                               std::list<std::unique_ptr<walker>>& target_list,
                               bool count_living, bool atstart)
{
    if (!level_data_ || !level_data_->myloader)
        return nullptr;

    auto w = level_data_->myloader->create_walker_owned(order, family);
    if (!w)
        return nullptr;

    level_data_->wire_spawned_entity(w.get());
    if (count_living)
        living_count++;

    walker* raw = w.get();
    if (atstart)
        target_list.push_front(std::move(w));
    else
        target_list.push_back(std::move(w));
    return raw;
}

walker* GameWorld::add_ob(Order order, std::int32_t family, bool atstart)
{
    if (order == Order::Weapon)
        return add_to_list(order, family, weaplist, false, atstart);

    return add_to_list(order, family, oblist, order == Order::Living, atstart);
}

walker* GameWorld::add_fx_ob(Order order, std::int32_t family)
{
    return add_to_list(order, family, fxlist, false, false);
}

walker* GameWorld::add_weap_ob(Order order, std::int32_t family)
{
    return add_to_list(order, family, weaplist, false, false);
}

short GameWorld::remove_ob(walker* ob)
{
    auto pred = [ob](const std::unique_ptr<walker>& p) { return p.get() == ob; };

    auto e = std::find_if(weaplist.begin(), weaplist.end(), pred);
    if (e != weaplist.end())
    {
        weaplist.erase(e);
        return 1;
    }

    auto f = std::find_if(fxlist.begin(), fxlist.end(), pred);
    if (f != fxlist.end())
    {
        fxlist.erase(f);
        return 1;
    }

    auto g = std::find_if(oblist.begin(), oblist.end(), pred);
    if (g != oblist.end())
    {
        if (ob && ob->query_order() == Order::Living && living_count > 0)
            living_count--;
        oblist.erase(g);
        return 1;
    }

    return 0;
}

void GameWorld::delete_objects()
{
    oblist.clear();
    fxlist.clear();
    weaplist.clear();
    dead_list.clear();
    living_count = 0;
}

short GameWorld::remaining_foes(walker* myguy) const
{
    if (!myguy)
        return 0;

    short myfoes = 0;
    for (auto& uptr : oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            (w->query_order() == Order::Living) &&
            !myguy->is_friendly(w))
        {
            myfoes++;
        }
    }
    return myfoes;
}
