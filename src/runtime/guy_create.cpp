/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_context.h>
#include <openglad/data/gloader.h>

std::unique_ptr<walker> guy::create_walker_owned(screen* screen_)
{
	    auto temp_guy = std::make_unique<guy>(*this);
	    auto temp_walker = screen_->level_data.myloader->create_walker_owned(Order::Living, temp_guy->family, screen_);
	    if (!temp_walker)
	        return nullptr;
	    temp_walker->set_owned_myguy(std::move(temp_guy));
	    temp_walker->stats()->level = temp_walker->myguy->level;
	    
	    update_derived_stats(temp_walker.get());

    // Set our team number ..
    temp_walker->team_num = static_cast<unsigned char>(temp_walker->myguy->teamnum);
    temp_walker->real_team_num = 255;
    
	    return temp_walker;
}

walker* guy::create_and_add_walker(screen* screen_)
{
    auto temp_guy = std::make_unique<guy>(*this);
    walker* temp_walker = screen_->level_data.add_ob(Order::Living, temp_guy->family);
    temp_walker->set_owned_myguy(std::move(temp_guy));
    temp_walker->stats()->level = temp_walker->myguy->level;
    
    update_derived_stats(temp_walker);

    // Set our team number ..
    temp_walker->team_num = static_cast<unsigned char>(temp_walker->myguy->teamnum);
    temp_walker->real_team_num = 255;
    
    return temp_walker;
}
