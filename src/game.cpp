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
	oblink        * here;
	short         myord, myfam;
	int           multi_team = 0;
	int           i;

	// First load the team list ..
	if (!myscreen->save_data.load(filename))
	{
		Log("Error loading saved game %s.\n", filename);
		release_keyboard();
		exit(1);
	}

	// Determine the scenario name to load
	sprintf(scenfile, "scen%d", myscreen->save_data.scen_num);
	
	// And our default par value ..
	myscreen->par_value = myscreen->save_data.scen_num;
	// And load the scenario ..
	if (!load_scenario(scenfile, myscreen))
	{
	    Log("Failed to load \"%s\".  Falling back to loading scenario 1.\n", scenfile);
	    // Failed?  Try level 1.
		myscreen->par_value = 1;
		myscreen->save_data.scen_num = 1;
		if(!load_scenario("scen1", myscreen))
        {
            Log("Fallback loading failed to load scenario 1.\n");
            exit(2);
        }
	}
	myscreen->mysmoother.set_target(myscreen->grid);

	here = myscreen->oblist;
	while (here)
	{
		if (here->ob)
			here->ob->set_difficulty((Uint32)here->ob->stats->level);
		here = here->next;
	}

	// Cycle through the team list ..
	temp_guy = myscreen->save_data.first_guy;
	while (temp_guy)
	{
		temp_walker = myscreen->add_ob(ORDER_LIVING, temp_guy->family);
		temp_walker->myguy = temp_guy;
		temp_walker->stats->level = temp_guy->level;

		// Set hitpoints based on stats:
		temp_walker->stats->max_hitpoints = (short)
		                                    (10 + (temp_guy->constitution*3 +
		                                           ((temp_guy->strength)/2) + (short) (25*temp_guy->level))  );
		temp_walker->stats->hitpoints = temp_walker->stats->max_hitpoints;

		// Set damage based on strength and level
		temp_walker->damage += (temp_guy->strength/4) + temp_guy->level
		                       + (temp_guy->dexterity/11);
		// Set magicpoints based on stats:
		temp_walker->stats->max_magicpoints = (short)
		                                      (10 + (temp_guy->intelligence*3) + ( 25 * temp_guy->level) +(short) (temp_guy->dexterity) );
		temp_walker->stats->magicpoints = temp_walker->stats->max_magicpoints;

		// Set our armor level ..
		temp_walker->stats->armor =(short)  ( temp_guy->armor + (temp_guy->dexterity / 14) + temp_guy->level );

		// Set the heal delay ..

		temp_walker->stats->max_heal_delay = REGEN;
		temp_walker->stats->current_heal_delay =
		    (temp_guy->constitution) + (temp_guy->strength/6) +
		    (temp_guy->level * 2) + 20; //for purposes of calculation only

		while (temp_walker->stats->current_heal_delay > REGEN)
		{
			temp_walker->stats->current_heal_delay -= REGEN;
			temp_walker->stats->heal_per_round++;
		} // this takes care of the integer part, now calculate the fraction

		if (temp_walker->stats->current_heal_delay > 1)
		{
			temp_walker->stats->max_heal_delay /=
			    (Sint32) (temp_walker->stats->current_heal_delay + 1);
		}
		temp_walker->stats->current_heal_delay = 0; //start off without healing

		//make sure we have at least a 2 wait, otherwise we should have
		//calculated our heal_per_round as one higher, and the math must
		//have been screwed up some how
		if (temp_walker->stats->max_heal_delay < 2)
			temp_walker->stats->max_heal_delay = 2;

		// Set the magic delay ..
		temp_walker->stats->max_magic_delay = REGEN;
		temp_walker->stats->current_magic_delay = (Sint32)
		        (temp_guy->intelligence * 45) + (temp_guy->level*60) +
		        (temp_guy->dexterity * 15) + 200;

		while (temp_walker->stats->current_magic_delay > REGEN)
		{
			temp_walker->stats->current_magic_delay -= REGEN;
			temp_walker->stats->magic_per_round++;
		} // this takes care of the integer part, now calculate the fraction

		if (temp_walker->stats->current_magic_delay > 1)
		{
			temp_walker->stats->max_magic_delay /=
			    (Sint32) (temp_walker->stats->current_magic_delay + 1);
		}
		temp_walker->stats->current_magic_delay = 0; //start off without magic regen

		//make sure we have at least a 2 wait, otherwise we should have
		//calculated our magic_per_round as one higher, and the math must
		//have been screwed up some how
		if (temp_walker->stats->max_magic_delay < 2)
			temp_walker->stats->max_magic_delay = 2;

		//stepsize makes us run faster, max for a non-weapon is 12
		temp_walker->stepsize += (temp_guy->dexterity/54);
		if (temp_walker->stepsize > 12)
			temp_walker->stepsize = 12;
		temp_walker->normal_stepsize = temp_walker->stepsize;

		//fire_frequency makes us fire faster, min is 1
		temp_walker->fire_frequency -= (temp_guy->dexterity / 47);
		if (temp_walker->fire_frequency < 1)
			temp_walker->fire_frequency = 1;

		// Fighters: limited weapons
		if (temp_walker->query_family() == FAMILY_SOLDIER)
			temp_walker->weapons_left = (short) ((temp_walker->stats->level+1) / 2);

		// Set our team number ..
		temp_walker->team_num = temp_guy->teamnum;
		temp_walker->real_team_num = 255;

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
			temp_guy = temp_guy->next;
		}
		else
		{
			// Scatter the overflowing characters..
			temp_walker->teleport();
			temp_guy = temp_guy->next;
		}

	}

	// Now remove any extra guys .. (set to dead)
	replace_walker = myscreen->first_of(ORDER_SPECIAL, FAMILY_RESERVED_TEAM);
	while (replace_walker)
	{
		replace_walker->dead = 1;
		replace_walker = myscreen->first_of(ORDER_SPECIAL, FAMILY_RESERVED_TEAM);
	}
	// Remove the links between 'guys'
	here = myscreen->oblist;
	while (here)
	{
		if (here->ob && here->ob->myguy)
			here->ob->myguy->next = NULL;
		here = here->next;
	}

	// Have we already done this scenario?
	if (myscreen->save_data.is_level_completed(myscreen->save_data.scen_num))
	{
		//                Log("already done level\n");
		here = myscreen->oblist;
		while (here)
		{
			if (here->ob)
			{
				myfam = here->ob->query_family();
				myord = here->ob->query_order();
				if ( ( (here->ob->team_num==0 || here->ob->myguy) && myord==ORDER_LIVING) || //living team member
				        (myord==ORDER_TREASURE && myfam==FAMILY_EXIT) || // exit
				        (myord==ORDER_TREASURE && myfam==FAMILY_TELEPORTER)  // teleporters
				   )
				{
					// do nothing; legal guy
				}
				else
					here->ob->dead = 1;
			}
			here = here->next;
		}
		here = myscreen->weaplist;
		while (here)
		{
			if (here->ob)
			{
				myfam = here->ob->query_family();
				myord = here->ob->query_order();
				if ( (here->ob->team_num==0 && myord==ORDER_LIVING) || //living team member
				        (myord==ORDER_TREASURE && myfam==FAMILY_EXIT) || // exit
				        (myord==ORDER_TREASURE && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing; legal guy
				}
				else
					here->ob->dead = 1;
			}
			here = here->next;
		}

		here = myscreen->fxlist;
		while (here)
		{
			if (here->ob)
			{
				myfam = here->ob->query_family();
				myord = here->ob->query_order();
				if ( (here->ob->team_num==0 && myord==ORDER_LIVING) || //living team member
				        (myord==ORDER_TREASURE && myfam==FAMILY_EXIT) || // exit
				        (myord==ORDER_TREASURE && myfam==FAMILY_TELEPORTER)  // teleporters

				   )
				{
					// do nothing; legal guy
				}
				else
					here->ob->dead = 1;
			}
			here = here->next;
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
