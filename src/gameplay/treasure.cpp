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
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <algorithm>
#include <format>
#include <cstring>

treasure::treasure(const PixieData& data)
    : walker(data)
{
	set_ignore(static_cast<char>(0));
	set_dead(static_cast<char>(0));
}

treasure::treasure()
    : walker()
{
	set_ignore(static_cast<char>(0));
	set_dead(static_cast<char>(0));
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
	const auto* tfd = get_treasure_family_descriptor(family());
	// rule: Whether a touched treasure is consumed is decided by its family's Lua on_eat (food
	//   heals, gold scores, keys, potions...); a family with no hook is simply eaten.
	// tests: TreasureEat.drumstick, TreasureEat.drumstick_full_hp
	// tests: TreasureEat.default_fallback_and_teleporter_wraparound
	if (auto hook_result = og::script::hooks::treasure_on_eat(tfd, this, eater))
		return *hook_result;

	return 1;
}

void treasure::set_direct_frame(short whatframe)
{
	walker::set_direct_frame(whatframe);
}

// Finds the next connected teleporter in the fxlist for you to warp to.
walker  * treasure::find_teleport_target()
{
	auto& ls = current_game->world->fxlist;
	//Log("Teleporting from #%d ..", number);

	// First find where we are in the list ...
    auto pred = [this](const std::unique_ptr<walker>& p) { return p.get() == this; };
	auto mine = std::find_if(ls.begin(), ls.end(), pred);
    // rule: A teleporter sends you to the next live teleporter with the same level number (its
    //   channel) after it in list order, wrapping to the start; if none matches you go nowhere.
    // tests: TreasureEat.treasure_find_teleport_target_filters_invalid_candidates_and_wraps
    // tests: TreasureEat.treasure_find_teleport_target_loop_and_missing_self
    // tests: TreasureEat.treasure_batch7_explicit_fxlist_teleporter_branches
    // tests: TreasureEat.treasure_round10_find_teleport_target_forward_wrap_and_empty_paths
    if(mine == ls.end())
    {
        return nullptr;
    }

	// Now search the rest of the list ..
	auto e = mine;
	e++;
	// rule: Part of the rule above that begins "A teleporter sends you to the next live teleporter
	//   ..."
	// tests: TreasureEat.treasure_find_teleport_target_filters_invalid_candidates_and_wraps
	// tests: TreasureEat.treasure_find_teleport_target_loop_and_missing_self
	// tests: TreasureEat.treasure_batch7_explicit_fxlist_teleporter_branches
	// tests: TreasureEat.treasure_round10_find_teleport_target_forward_wrap_and_empty_paths
	for(; e != ls.end(); e++)
	{
	    walker* w = e->get();
		// rule: Part of the rule above that begins "A teleporter sends you to the next live
		//   teleporter ..."
		// tests: TreasureEat.treasure_find_teleport_target_filters_invalid_candidates_and_wraps
		// tests: TreasureEat.treasure_find_teleport_target_loop_and_missing_self
		// tests: TreasureEat.treasure_batch7_explicit_fxlist_teleporter_branches
		// tests: TreasureEat.treasure_round10_find_teleport_target_forward_wrap_and_empty_paths
		if (w && !w->dead())
		{
			// rule: Part of the rule above that begins "A teleporter sends you to the next live
			//   teleporter ..."
			// tests: TreasureEat.treasure_find_teleport_target_filters_invalid_candidates_and_wraps
			// tests: TreasureEat.treasure_find_teleport_target_loop_and_missing_self
			// tests: TreasureEat.treasure_batch7_explicit_fxlist_teleporter_branches
			// tests: TreasureEat.treasure_round10_find_teleport_target_forward_wrap_and_empty_paths
			if (w->query_order() == Order::Treasure &&
			        w->family() == FAMILY_TELEPORTER &&
			        w->stats()->level() == stats_->level())
			{
				//Log(" to target %d\n", number);
				return w;
			}
		}
	}

	// Hit the end of the list, look from top down now ..
	// rule: Part of the rule above that begins "A teleporter sends you to the next live teleporter
	//   ..."
	// tests: TreasureEat.treasure_find_teleport_target_filters_invalid_candidates_and_wraps
	// tests: TreasureEat.treasure_find_teleport_target_loop_and_missing_self
	// tests: TreasureEat.treasure_batch7_explicit_fxlist_teleporter_branches
	// tests: TreasureEat.treasure_round10_find_teleport_target_forward_wrap_and_empty_paths
	for(e = ls.begin(); e != mine; e++)
	{
	    walker* w = e->get();
		// rule: Part of the rule above that begins "A teleporter sends you to the next live
		//   teleporter ..."
		// tests: TreasureEat.treasure_find_teleport_target_filters_invalid_candidates_and_wraps
		// tests: TreasureEat.treasure_find_teleport_target_loop_and_missing_self
		// tests: TreasureEat.treasure_batch7_explicit_fxlist_teleporter_branches
		// tests: TreasureEat.treasure_round10_find_teleport_target_forward_wrap_and_empty_paths
		if (w && !w->dead())
		{
			// rule: Part of the rule above that begins "A teleporter sends you to the next live
			//   teleporter ..."
			// tests: TreasureEat.treasure_find_teleport_target_filters_invalid_candidates_and_wraps
			// tests: TreasureEat.treasure_find_teleport_target_loop_and_missing_self
			// tests: TreasureEat.treasure_batch7_explicit_fxlist_teleporter_branches
			// tests: TreasureEat.treasure_round10_find_teleport_target_forward_wrap_and_empty_paths
			if (w->query_order() == Order::Treasure &&
			        w->family() == FAMILY_TELEPORTER &&
			        w->stats()->level() == stats_->level())
			{
				//Log(" to looped target %d\n", number);
				return w;
			}
		}
	}

	return nullptr;
}
