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
#include "graph.h"
#include "smooth.h"
#include "util.h"
#include "campaign_picker.h"
#include <format>

void popup_dialog(const char* title, const char* message);

short load_saved_game(const char *filename, screen  *myscreen)
{
	TRACE("game", "load_saved_game file=%s scen=%d", filename, myscreen->save_data.scen_num);
	guy           *temp_guy;
	walker        *temp_walker,  *replace_walker;
	Order         myord{};
	short         myfam;
	int           multi_team = 0;
	int           i;

	myscreen->numviews = myscreen->save_data.numplayers;

	myscreen->cleanup(myscreen->numviews);
	myscreen->initialize_views();

	// Determine the scenario name to load
	std::string scenfile = std::format("scen{}", myscreen->save_data.scen_num);
	
	// And load the scenario ..
	myscreen->level_data.id = myscreen->save_data.scen_num;
	if(!myscreen->level_data.load())
	{
	    short old_scen = myscreen->save_data.scen_num;
	    Log("Failed to load \"{}\".  Falling back to loading scenario 1.\n", scenfile);
	    // Failed?  Try level 1.
		myscreen->save_data.scen_num = 1;
        myscreen->level_data.id = 1;
        if(!myscreen->level_data.load())
        {
            std::string buf = std::format("Fallback loading failed ({}).\nCould not load \"{}\"\nPlease report this problem to the developer!\n", old_scen, scenfile);
            popup_dialog("ERROR", buf.c_str());
            exit(2);
        }
	}

	TRACE("game", "level loaded: scen%d", myscreen->level_data.id);
	for(auto& uptr : myscreen->level_data.oblist)
	{
	    walker* w = uptr.get();
		if (w)
			w->set_difficulty(static_cast<Uint32>(w->stats()->level));
	}

	// Cycle through the team list ..
	for(int i = 0; i < myscreen->save_data.team_size; i++)
    {
	    temp_guy = myscreen->save_data.team_list[i].get();
	    temp_walker = temp_guy->create_and_add_walker(myscreen);
	    // Clear the new guy's battle data
	    temp_walker->myguy->scen_damage = 0;
	    temp_walker->myguy->scen_kills = 0;
	    temp_walker->myguy->scen_damage_taken = 0;
	    temp_walker->myguy->scen_min_hp = 5000000;
	    temp_walker->myguy->scen_shots = 0;
	    temp_walker->myguy->scen_hits = 0;

		// Do we have guys on multiple teams? If so, we need
		// to record it so that we can set the controls of
		// the viewscreens correctly
		if (temp_guy->teamnum != 0)
			multi_team = 1;

		// First, try to find a marker that's the correct team number ..
		replace_walker = myscreen->first_of(Order::Special,
		                                    FAMILY_RESERVED_TEAM,
		                                    static_cast<int>(temp_guy->teamnum));
		// If that doesn't work, though, grab any marker we can ..
		if (!replace_walker)
			replace_walker = myscreen->first_of(Order::Special, FAMILY_RESERVED_TEAM);
		if (replace_walker)
		{
			temp_walker->setxy(replace_walker->xpos, replace_walker->ypos);
			replace_walker->dead = 1;
		}
		else
		{
			// Scatter the overflowing characters..
			temp_walker->teleport();
		}
	}
    
    // Destroy all player markers (by setting them to dead)
	replace_walker = myscreen->first_of(Order::Special, FAMILY_RESERVED_TEAM);
	while (replace_walker)
	{
		replace_walker->dead = 1;
		replace_walker = myscreen->first_of(Order::Special, FAMILY_RESERVED_TEAM);
	}

	// Have we already done this scenario?
	if (myscreen->save_data.is_level_completed(myscreen->save_data.scen_num))
	{
		//                Log("already done level\n");
		for(auto& uptr : myscreen->level_data.oblist)
		{
		    walker* w = uptr.get();
			if (w)
			{
			    // Kill everything except for our team, exits, and teleporters
				myfam = w->query_family();
				myord = w->query_order();
				if ( ( (w->team_num==0 || w->myguy) && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters
				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}

		for(auto& uptr : myscreen->level_data.weaplist)
		{
		    walker* w = uptr.get();
			if (w)
			{
				myfam = w->query_family();
				myord = w->query_order();
				if ( (w->team_num==0 && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}

		for(auto& uptr : myscreen->level_data.fxlist)
		{
		    walker* w = uptr.get();
			if (w)
			{
				myfam = w->query_family();
				myord = w->query_order();
				if ( (w->team_num==0 && myord==Order::Living) || //living team member
				        (myord==Order::Treasure && myfam==FAMILY_EXIT) || // exit
				        (myord==Order::Treasure && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}
	}

	// Here we decide if all players are controlling
	// team 0, or if they're playing competing teams ..
	if (multi_team)
	{
		for (i=0; i < myscreen->numviews; i++)
			myscreen->viewob[i]->my_team = i;
	}
	else
	{
		for (i=0; i < myscreen->numviews; i++)
			myscreen->viewob[i]->my_team = 0;
	}

	return 1;
}
