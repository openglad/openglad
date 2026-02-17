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

#include <openglad/core/stats.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include "SDL_stdinc.h"
#include <openglad/legacy/test_trace.h>
#include <openglad/sim/sim_emit.h>

#include <list>

bool walker::special()
{
	TRACE("walker", "special: family=%d current_special=%d", family, current_special);

	if (dead)
	{
		Log("Dead guy doing special!\n");
		return 0;
	}

	if (!stats_)
	{
		Log("Special with no stats\n");
		return 0;
	}

	if (stats_->magicpoints < stats_->special_cost[static_cast<int>(current_special)])
		return 0;

	if (query_order() != Order::Living)
		return 0;

	// Dispatch via family descriptor callback
	auto* fd = get_family_descriptor(query_family());
	if (fd && fd->do_special)
	{
		if (fd->do_special(this))
			stats_->magicpoints -= stats_->special_cost[static_cast<int>(current_special)];
	}
	return 0;
}

bool walker::teleport()
{
	Sint32 newx = 0, newy = 0;
	Sint32 distance = 0;

	// First check to see if we have a marker to go to
	// NOTE: it must be a bit away from us ..
	for(auto& uptr : sim_level->oblist)
	{
	    walker* ob = uptr.get();
		if (ob &&
		        ob->query_order() == Order::FX &&
		        ob->query_family() == FAMILY_MARKER &&
		        ob->owner == this &&
		        !ob->dead
		   )
		{
			// Found our marker!
				distance = distance_to_ob(ob);
				if (sim_level->query_passable(ob->xpos, ob->ypos, this) && (distance > 64))
				{
					center_on(ob);
					ob->lifetime--;
				if (ob->lifetime < 1)
				{
					ob->dead = 1;
					ob->death();
				}
				return 1;
			} // end of successful transport
			else  // blocked somehow?
			{
				if (user != -1 && (distance > 64) ) // only tell players
					og::sim::emit_notification(sim_events, "Marker is Blocked!");
			}
			}
			} // end of checking for marker (we failed)

	// No marker: pick a random passable grid cell. Historically this was an
	// unbounded loop, which can hang tests (and gameplay) if level/grid state
	// isn't initialized or nothing is passable.
	if (!sim_level || !sim_level->grid.valid() ||
	    sim_level->grid.w <= 0 || sim_level->grid.h <= 0 ||
	    sim_level->pixmaxx <= 0 || sim_level->pixmaxy <= 0)
		return 0;

	Sint32 keep_going = 200; // maxtries
	do
	{
		newx = static_cast<Sint32>(sim_rng->next(static_cast<Uint32>(sim_level->grid.w))) * GRID_SIZE;
		newy = static_cast<Sint32>(sim_rng->next(static_cast<Uint32>(sim_level->grid.h))) * GRID_SIZE;
		keep_going--;
	} while (keep_going > 0 &&
	         !sim_level->query_passable(static_cast<float>(newx), static_cast<float>(newy), this));

	if (keep_going > 0)
	{
		setxy(newx, newy);
		return 1;
	}
	return 0;
}

bool walker::teleport_ranged(Sint32 range)
{
	Sint32 newx = 0, newy = 0;
	Sint32 keep_going = 200; // maxtries

	newx = static_cast<Sint32>(sim_rng->next(static_cast<Uint32>(2 * range))) - range + xpos;
	newy = static_cast<Sint32>(sim_rng->next(static_cast<Uint32>(2 * range))) - range + ypos;

	while(!sim_level->query_passable(static_cast<float>(newx), static_cast<float>(newy), this) && keep_going)
	{
		newx = static_cast<Sint32>(sim_rng->next(static_cast<Uint32>(2 * range))) - range + xpos;
		newy = static_cast<Sint32>(sim_rng->next(static_cast<Uint32>(2 * range))) - range + ypos;
		keep_going--;
	}
	if (keep_going)
	{
		setxy(newx, newy);
		return 1;
	}
	return 0; // failed to find safe spot
}

// Turns undead; ie, skeleton or ghost, within range
// Returns the number of dead destroyed
Sint32 walker::turn_undead(Sint32 range, [[maybe_unused]] Sint32 power)
{
	Sint32 killed = 0;
	Sint32 targets = 0;

	std::list<walker*> deadlist = sim_level->find_foes_in_range(sim_level->oblist, range,
	                                       &targets, this);
	if (!targets)
		return -1;

    for(auto* w : deadlist)
	{
		const auto* target_fd = w ? get_family_descriptor(w->query_family()) : nullptr;
		if (w && target_fd && target_fd->is_undead)
		{
			if (sim_rng->next(range*40) > sim_rng->next(w->stats()->level*10) )
			{
				w->dead = 1;
				w->stats()->hitpoints = 0;
				//w->death();
				attack(w); // to generate bloodspot, etc.
				killed++;
			}
		}
	}
	
	return killed;
}

// *******************************************
//
//    MONSTER intellIGENCE ROUTINES
//
// *******************************************

// Basically, we check a direction for foes.
// If we find one, we init_fire.  If not,
// we do nothing. init_fire will take care of
// turning us if we need it.
