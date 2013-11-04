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

short load_saved_game(const char *filename, screen  *myscreen)
{
	char          scenfile[20];
	guy           *temp_guy;
	walker        *temp_walker,  *replace_walker;
	short         myord, myfam;
	int           multi_team = 0;
	int           i;
	
	myscreen->numviews = myscreen->save_data.numplayers;
	
	myscreen->cleanup(myscreen->numviews);
	myscreen->initialize_views();

	// Determine the scenario name to load
	sprintf(scenfile, "scen%d", myscreen->save_data.scen_num);
	
	// And load the scenario ..
	myscreen->level_data.id = myscreen->save_data.scen_num;
	if(!myscreen->level_data.load())
	{
	    Log("Failed to load \"%s\".  Falling back to loading scenario 1.\n", scenfile);
	    // Failed?  Try level 1.
		myscreen->save_data.scen_num = 1;
        myscreen->level_data.id = 1;
        if(!myscreen->level_data.load())
        {
            Log("Fallback loading failed to load scenario 1.\n");
            exit(2);
        }
	}

	std::list<walker*> foelist = myscreen->level_data.oblist;
	for(auto e = foelist.begin(); e != foelist.end(); e++)
	{
	    walker* w = *e;
		if (w)
			w->set_difficulty((Uint32)w->stats->level);
	}

	// Cycle through the team list ..
	for(int i = 0; i < myscreen->save_data.team_size; i++)
    {
	    temp_guy = myscreen->save_data.team_list[i];
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
		replace_walker = myscreen->first_of(ORDER_SPECIAL,
		                                    FAMILY_RESERVED_TEAM,
		                                    (int)temp_guy->teamnum);
		// If that doesn't work, though, grab any marker we can ..
		if (!replace_walker)
			replace_walker = myscreen->first_of(ORDER_SPECIAL, FAMILY_RESERVED_TEAM);
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
	replace_walker = myscreen->first_of(ORDER_SPECIAL, FAMILY_RESERVED_TEAM);
	while (replace_walker)
	{
		replace_walker->dead = 1;
		replace_walker = myscreen->first_of(ORDER_SPECIAL, FAMILY_RESERVED_TEAM);
	}

	// Have we already done this scenario?
	if (myscreen->save_data.is_level_completed(myscreen->save_data.scen_num))
	{
		//                Log("already done level\n");
		foelist = myscreen->level_data.oblist;
		for(auto e = foelist.begin(); e != foelist.end(); e++)
		{
		    walker* w = *e;
			if (w)
			{
			    // Kill everything except for our team, exits, and teleporters
				myfam = w->query_family();
				myord = w->query_order();
				if ( ( (w->team_num==0 || w->myguy) && myord==ORDER_LIVING) || //living team member
				        (myord==ORDER_TREASURE && myfam==FAMILY_EXIT) || // exit
				        (myord==ORDER_TREASURE && myfam==FAMILY_TELEPORTER)  // teleporters
				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}
		
		foelist = myscreen->level_data.weaplist;
		for(auto e = foelist.begin(); e != foelist.end(); e++)
		{
		    walker* w = *e;
			if (w)
			{
				myfam = w->query_family();
				myord = w->query_order();
				if ( (w->team_num==0 && myord==ORDER_LIVING) || //living team member
				        (myord==ORDER_TREASURE && myfam==FAMILY_EXIT) || // exit
				        (myord==ORDER_TREASURE && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing; legal guy
				}
				else
					w->dead = 1;
			}
		}

		foelist = myscreen->level_data.fxlist;
		for(auto e = foelist.begin(); e != foelist.end(); e++)
		{
		    walker* w = *e;
			if (w)
			{
				myfam = w->query_family();
				myord = w->query_order();
				if ( (w->team_num==0 && myord==ORDER_LIVING) || //living team member
				        (myord==ORDER_TREASURE && myfam==FAMILY_EXIT) || // exit
				        (myord==ORDER_TREASURE && myfam==FAMILY_TELEPORTER)  // teleporters

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
