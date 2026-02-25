/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/game_world.h>
#include <openglad/data/level_data.h>
#include <openglad/entities/walker.h>

namespace og::gameplay {

GameWorld::~GameWorld() = default;

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
    short myfoes = 0;
    for (const auto& uptr : oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            (w->query_order() == Order::Living) &&
            !myguy->is_friendly(w))
            myfoes++;
    }
    return myfoes;
}

walker* GameWorld::add_ob(Order order, std::int32_t family, bool atstart)
{
    if (!level_data_) return nullptr;
    return level_data_->add_ob(order, family, atstart);
}

walker* GameWorld::add_fx_ob(Order order, std::int32_t family)
{
    if (!level_data_) return nullptr;
    return level_data_->add_fx_ob(order, family);
}

walker* GameWorld::add_weap_ob(Order order, std::int32_t family)
{
    if (!level_data_) return nullptr;
    return level_data_->add_weap_ob(order, family);
}

} // namespace og::gameplay
