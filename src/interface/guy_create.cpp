/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_context.h>
#include <openglad/resources/gloader.h>

std::unique_ptr<walker> guy_create_walker_owned(guy& g, screen* screen_)
{
    if (screen_ == nullptr || screen_->myloader == nullptr)
        return nullptr;

    auto temp_guy = std::make_unique<guy>(g);
    auto temp_walker = screen_->myloader->create_walker_owned(Order::Living, temp_guy->family);
    if (!temp_walker)
        return nullptr;
    temp_walker->set_owned_myguy(std::move(temp_guy));
    temp_walker->stats()->level = temp_walker->myguy->level;

    g.update_derived_stats(temp_walker.get());

    // Set our team number ..
    temp_walker->team_num = static_cast<unsigned char>(temp_walker->myguy->teamnum);
    temp_walker->real_team_num = 255;

    return temp_walker;
}

walker* guy_create_and_add_walker(guy& g, screen* screen_)
{
    auto temp_guy = std::make_unique<guy>(g);
    walker* temp_walker = screen_->world().add_ob(Order::Living, temp_guy->family);
    temp_walker->set_owned_myguy(std::move(temp_guy));
    temp_walker->stats()->level = temp_walker->myguy->level;

    g.update_derived_stats(temp_walker);

    // Set our team number ..
    temp_walker->team_num = static_cast<unsigned char>(temp_walker->myguy->teamnum);
    temp_walker->real_team_num = 255;

    return temp_walker;
}
