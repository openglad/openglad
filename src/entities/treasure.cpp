/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//
// treasure; a derived class of walker
//

#include <cmath>
#include <string>
#include <openglad/entities/obmap.h>
#include <openglad/data/gloader.h>
#include <openglad/data/level_data.h>
#include <openglad/entities/treasure.h>
#include <openglad/entities/walker_render.h>
#include <openglad/entities/treasure_family_descriptor.h>
#include <openglad/entities/treasure_family_registry.h>
#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
// pixieN include not needed; render access is through IWalkerRender
#include <algorithm>
#include <format>
#include <cstring>

treasure::treasure(const PixieData& data)
    : walker(data)
{
	ignore =static_cast<char>(0);
	dead = static_cast<char>(0);
}

treasure::treasure()
    : walker()
{
	ignore = static_cast<char>(0);
	dead = static_cast<char>(0);
}

treasure::~treasure()
{
	//bufffers: PORT: cannot call destructor w/o obj: walker::~walker();
}

bool treasure::act()
{
	// Abort all later code for now ..
	return 1;
}

bool treasure::eat_me(walker  * eater)
{
	const auto* tfd = get_treasure_family_descriptor(family);
	if (tfd && tfd->on_eat)
		return tfd->on_eat(this, eater);

	return 1;
}

void treasure::set_direct_frame(short whatframe)
{
	frame = whatframe;

	// Update render component's bmp pointer if available
	if (render_)
		render_->set_frame(whatframe);
}

// Finds the next connected teleporter in the fxlist for you to warp to.
walker  * treasure::find_teleport_target()
{
	auto& ls = sim_level->fxlist;
	//Log("Teleporting from #%d ..", number);

	// First find where we are in the list ...
    auto pred = [this](const std::unique_ptr<walker>& p) { return p.get() == this; };
	auto mine = std::find_if(ls.begin(), ls.end(), pred);
    if(mine == ls.end())
    {
        return nullptr;
    }

	// Now search the rest of the list ..
	auto e = mine;
	e++;
	for(; e != ls.end(); e++)
	{
	    walker* w = e->get();
		if (w && !w->dead)
		{
			if (w->query_order() == Order::Treasure &&
			        w->query_family() == FAMILY_TELEPORTER &&
			        w->stats()->level == stats_->level)
			{
				//Log(" to target %d\n", number);
				return w;
			}
		}
	}

	// Hit the end of the list, look from top down now ..
	for(e = ls.begin(); e != mine; e++)
	{
	    walker* w = e->get();
		if (w && !w->dead)
		{
			if (w->query_order() == Order::Treasure &&
			        w->query_family() == FAMILY_TELEPORTER &&
			        w->stats()->level == stats_->level)
			{
				//Log(" to looped target %d\n", number);
				return w;
			}
		}
	}

	return nullptr;
}
