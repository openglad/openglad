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

short next_scenario = 1;

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
	if (!load_team_list(filename, myscreen))
	{
		Log("Error loading saved game %s.\n", filename);
		release_keyboard();
		exit(0);
	}

	// Determine the scenario name to load
	sprintf(scenfile, "scen%d", next_scenario);
	// And our default par value ..
	myscreen->par_value = next_scenario;
	// And load the scenario ..
	if (!load_scenario(scenfile, myscreen))
	{
		myscreen->par_value = 1;
		load_scenario("scen0", myscreen);
		myscreen->scen_num = 0;
		mysmoother->set_target(myscreen);
	}
	mysmoother->set_target(myscreen);

	here = myscreen->oblist;
	while (here)
	{
		if (here->ob)
			here->ob->set_difficulty((Uint32)here->ob->stats->level);
		here = here->next;
	}

	// Cycle through the team list ..
	temp_guy = myscreen->first_guy;
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
	if (myscreen->is_level_completed(myscreen->scen_num))
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

short load_team_list(const char * filename, screen  *myscreen)
{
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *infile;
	char temp_filename[80];
	guy  *temp_guy;

	char temptext[10] = "GTL";
	char savedgame[40];
	char temp_campaign[41];
	strcpy(temp_campaign, "org.openglad.gladiator");
	temp_campaign[40] = '\0';
	char temp_version = 8;
	Uint32 newcash;
	Uint32 newscore = 0;
	//  short numguys;
	short listsize;
	short i;

	char tempname[12] = "FRED";
	char guyname[12] = "JOE";
	char temp_order, temp_family;
	short temp_str, temp_dex, temp_con;
	short temp_short, temp_arm, temp_lev;
	char numplayers;
	Uint32 temp_exp;
	short temp_kills;
	Sint32 temp_level_kills;
	Sint32 temp_td, temp_th, temp_ts;
	short temp_teamnum; // version 5+
	short temp_allied;            // v.7+
	short temp_registered;        // v.7+

	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number
	// 2-bytes registered mark            // Versions 7+
	// 40-bytes saved game name, version 2 and up
	// 40-bytes current campaign ID       // Version 8+
	// 2-bytes (short) = scenario number
	// 4-bytes (Sint32)= cash (unsigned)
	// 4-bytes (Sint32)= score (unsigned)
	// 4-bytes (Sint32)= cash-B (unsigned)   // All alternate scores
	// 4-bytes (Sint32)= score-B (unsigned)  // version 6+
	// 4-bytes (Sint32)= cash-C (unsigned)
	// 4-bytes (Sint32)= score-C (unsigned)
	// 4-bytes (Sint32)= cash-D (unsigned)
	// 4-bytes (Sint32)= score-D (unsigned)
	// 2-bytes Allied mode                // Versions 7+
	// 2-bytes (short) = # of team members in list
	// 1-byte number of players
	// 31-bytes RESERVED
	// List of n objects, each of 58-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 12-byte name
	// 2-bytes strength
	// 2-bytes dexterity
	// 2-bytes constitution
	// 2-bytes intelligence
	// 2-bytes armor
	// 2-bytes level
	// 4-bytes unsigned experience
	// 2-bytes # kills, v.3
	// 4-bytes # total levels killed, v.3
	// 4-bytes total damage delt, v.4+
	// 4-bytes total hits inflicted, v.4+
	// 4-bytes total shots made, v.4+
	// 2-bytes team number
	// 2*4 = 8 bytes RESERVED
	// List of 200 or 500 (max levels) 1-byte scenario-level status  // Versions 1-7
	// 2-bytes Number of campaigns in list      // Version 8+
	// List of n campaigns                      // Version 8+
	//   40-bytes Campaign ID string
	//   2-bytes Current level in this campaign
	//   2-bytes Number of level indices in list
	//   List of n level indices
	//     2-bytes Level index
    
    
    myscreen->completed_levels.clear();
    myscreen->current_levels.clear();
    
	//strcpy(temp_filename, scen_directory);
	strcpy(temp_filename, filename);
	strcat(temp_filename, ".gtl"); // gladiator team list

	if ( (infile = open_read_file("save/", temp_filename)) == NULL )
	{
		//gotoxy(1, 22);
		//Log("Error in opening team file: %s\n", filename);
		return 0;
	}

	// Read id header
	SDL_RWread(infile, temptext, 3, 1);
	if ( strcmp(temptext,"GTL"))
	{
	    SDL_RWclose(infile);
		Log("Error, selected file is not a GTL file: %s\n",filename);
		return 0; //not a gtl file
	}

	// Read version number
	SDL_RWread(infile, &temp_version, 1, 1);

	// Versions 7+ have a registered mark ..
	if (temp_version >= 7)
	{
		SDL_RWread(infile, &temp_registered, 2, 1);
	}

	// Do other stuff based on version ..
	if (temp_version != 1)
	{
		if (temp_version >= 2)
			SDL_RWread(infile, savedgame, 40, 1); // read and ignore the name
		else
		{
            SDL_RWclose(infile);
			Log("Error, selected files is not version one: %s\n",filename);
			return 0;
		}
	}

    // Read campaign ID
    std::string old_campaign = myscreen->current_campaign;
	if (temp_version >= 8)
	{
		SDL_RWread(infile, temp_campaign, 1, 40);
		temp_campaign[40] = '\0';
		if(strlen(temp_campaign) > 3)
            strcpy(myscreen->current_campaign, temp_campaign);
        else
            strcpy(myscreen->current_campaign, "org.openglad.gladiator");
	}
	
	// Read scenario number
	SDL_RWread(infile, &next_scenario, 2, 1);
	myscreen->scen_num = next_scenario;

	// Read cash
	SDL_RWread(infile, &newcash, 4, 1);
	myscreen->totalcash = newcash;
	// Read score
	SDL_RWread(infile, &newscore, 4, 1);
	myscreen->totalscore = newscore;

	// Versions 6+ have a score for each possible team, 0-3
	if (temp_version >= 6)
	{
		for (i=0; i < 4; i++)
		{
			SDL_RWread(infile, &newcash, 4, 1);
			myscreen->m_totalcash[i] = newcash;
			SDL_RWread(infile, &newscore, 4, 1);
			myscreen->m_totalscore[i] = newscore;
		}
	}

	// Versions 7+ have the allied information ..
	if (temp_version >= 7)
	{
		SDL_RWread(infile, &temp_allied, 2, 1);
		myscreen->allied_mode = temp_allied;
	}

	// Get # of guys to read
	SDL_RWread(infile, &listsize, 2, 1);

	// Read (and ignore) the # of players
	SDL_RWread(infile, &numplayers, 1, 1);

	// Read the reserved area, 31 bytes
	SDL_RWread(infile, filler, 31, 1);

	// Okay, we've read header .. now read the team list data ..
	if (myscreen->first_guy)
	{
		delete myscreen->first_guy;  // delete the old list of guys
		myscreen->first_guy = NULL;
	}

	// Make a new 'head' guy ..
	myscreen->first_guy = new guy();
	temp_guy = myscreen->first_guy;
	while (listsize--)
	{
		// Get temp values to be read
		temp_order = ORDER_LIVING; // may be changed later
		// Read name of current guy...
		strcpy(guyname, tempname);
		// Set any chars under 12 not used to 0 ..
		for (i=(short) strlen(guyname); i < 12; i++)
			guyname[i] = 0;
		// Now write all those values
		SDL_RWread(infile, &temp_order, 1, 1);
		SDL_RWread(infile, &temp_family,1, 1);
		SDL_RWread(infile, guyname, 12, 1);
		SDL_RWread(infile, &temp_str, 2, 1);
		SDL_RWread(infile, &temp_dex, 2, 1);
		SDL_RWread(infile, &temp_con, 2, 1);
		SDL_RWread(infile, &temp_short, 2, 1);
		SDL_RWread(infile, &temp_arm, 2, 1);
		SDL_RWread(infile, &temp_lev, 2, 1);
		SDL_RWread(infile, &temp_exp, 4, 1);
		// Below here is version 3 and up..
		SDL_RWread(infile, &temp_kills, 2, 1); // how many kills we have
		SDL_RWread(infile, &temp_level_kills, 4, 1); // levels of kills
		// Below here is version 4 and up ..
		SDL_RWread(infile, &temp_td, 4, 1); // total damage
		SDL_RWread(infile, &temp_th, 4, 1); // total hits
		SDL_RWread(infile, &temp_ts, 4, 1); // total shots
		SDL_RWread(infile, &temp_teamnum, 2, 1); // team number

		// And the filler
		SDL_RWread(infile, filler, 8, 1);
		// Now set the values ..
		temp_guy->family       = temp_family;
		strcpy(temp_guy->name,guyname);
		temp_guy->strength     = temp_str;
		temp_guy->dexterity    = temp_dex;
		temp_guy->constitution = temp_con;
		temp_guy->intelligence = temp_short;
		temp_guy->armor        = temp_arm;
		temp_guy->level        = temp_lev;
		temp_guy->exp          = temp_exp;
		if (temp_version >=3)
		{
			temp_guy->kills      = temp_kills;
			temp_guy->level_kills= temp_level_kills;
		}
		else // version 2 or earlier
		{
			temp_guy->kills      = 0;
			temp_guy->level_kills= 0;
		}
		if (temp_version >= 4)
		{
			temp_guy->total_damage = temp_td;
			temp_guy->total_hits   = temp_th;
			temp_guy->total_shots  = temp_ts;
		}
		else
		{
			temp_guy->total_damage = 0;
			temp_guy->total_hits   = 0;
			temp_guy->total_shots  = 0;
		}
		if (temp_version >= 5)
		{
			temp_guy->teamnum = temp_teamnum;
		}
		else
		{
			temp_guy->teamnum = 0;
		}

		// Advance to the next guy ..
		if (listsize)
		{
			temp_guy->next = new guy();
			temp_guy = temp_guy->next;
		}
	}

    if(temp_version < 8)
    {
        char levelstatus[MAX_LEVELS];
        
        if (temp_version >= 5)
            SDL_RWread(infile, levelstatus, 500, 1);
        else
        {
            memset(levelstatus, 0, 500);
            SDL_RWread(infile, levelstatus, 200, 1);
        }
        
        // Guaranteed to be the default campaign if version < 8
        for(int i = 0; i < 500; i++)
        {
            if(levelstatus[i])
                myscreen->add_level_completed(myscreen->current_campaign, i);
        }
    }
    else
    {
        short num_campaigns = 0;
        char campaign[41];
        short num_levels = 0;
        // How many campaigns are stored?
        SDL_RWread(infile, &num_campaigns, 2, 1);
        for(int i = 0; i < num_campaigns; i++)
        {
            // Get the campaign ID (40 chars)
            SDL_RWread(infile, campaign, 1, 40);
            campaign[40] = '\0';
            
            short index = 1;
            // Get the current level for this campaign
            SDL_RWread(infile, &index, 2, 1);
            myscreen->current_levels.insert(std::make_pair(campaign, index));
            
            // Get the number of cleared levels
            SDL_RWread(infile, &num_levels, 2, 1);
            for(int j = 0; j < num_levels; j++)
            {
                // Get the level index
                SDL_RWread(infile, &index, 2, 1);
                
                // Add it to our list
                myscreen->add_level_completed(campaign, index);
            }
        }
    }
    
    // Make sure the default campaign is included
	myscreen->completed_levels.insert(std::make_pair("org.openglad.gladiator", std::set<int>()));
	myscreen->current_levels.insert(std::make_pair("org.openglad.gladiator", 1));
	
    int current_level = load_campaign(old_campaign, myscreen->current_campaign, myscreen->current_levels);
    if(current_level >= 0)
    {
        myscreen->scen_num = current_level;
        next_scenario = current_level;
    }

    SDL_RWclose(infile);

	return 1;
}

short save_game(const char * filename, screen  *myscreen)
{
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *outfile;
	char temp_filename[80];
	oblink  *here = myscreen->oblist;
	walker  * temp_walker;
	char savedgame[40];

	char temptext[10] = "GTL";
	char temp_version = 8;
	short next_scenario = (short) ( myscreen->scen_num + 1 );
	Uint32 newcash = myscreen->totalcash;
	Uint32 newscore = myscreen->totalscore;
	//  short numguys;
	short listsize;
	short i;

	char guyname[12] = "JOE";
	char temp_order, temp_family;
	short temp_str, temp_dex, temp_con;
	short temp_short, temp_arm, temp_lev;
	char numplayers = (char) myscreen->numviews;
	Uint32 temp_exp;
	short temp_kills;
	Sint32 temp_level_kills;
	Sint32 temp_td, temp_th, temp_ts;
	short temp_teamnum;
	short temp_allied;
	short temp_registered;


	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number
	// 2-bytes Registered or not          // Version 7+
	// 40-bytes saved-game name, dummy here
	// 40-bytes current campaign ID       // Version 8+
	// 2-bytes (short) = scenario number
	// 4-bytes (Sint32)= cash (unsigned)
	// 4-bytes (Sint32)= score (unsigned)
	// 4-bytes (Sint32)= cash-B (unsigned)   // All alternate scores
	// 4-bytes (Sint32)= score-B (unsigned)  // version 6+
	// 4-bytes (Sint32)= cash-C (unsigned)
	// 4-bytes (Sint32)= score-C (unsigned)
	// 4-bytes (Sint32)= cash-D (unsigned)
	// 4-bytes (Sint32)= score-D (unsigned)
	// 2-bytes allied setting              // Version 7+
	// 2-bytes (short) = # of team members in list
	// 1-byte number of players
	// 31-bytes RESERVED
	// List of n objects, each of 58-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 12-byte name
	// 2-bytes strength
	// 2-bytes dexterity
	// 2-bytes constitution
	// 2-bytes intelligence
	// 2-bytes armor
	// 2-bytes level
	// 4-bytes Uint32 experience
	// 2-bytes # kills, v.3+
	// 4-bytes # total levels killed, v.3+
	// 4-bytes total damage delt, v.4+
	// 4-bytes total hits inflicted, v.4+
	// 4-bytes total shots made, v.4+
	// 2-bytes team number, v.5+
	// 2*4 = 8 bytes RESERVED
	// List of 500 (max scenarios) 1-byte scenario-level status  // Versions 1-7
	// 2-bytes Number of campaigns in list      // Version 8+
	// List of n campaigns                      // Version 8+
	//   40-bytes Campaign ID string
	//   2-bytes Current level in this campaign
	//   2-bytes Number of level indices in list
	//   List of n level indices
	//     2-bytes Level index

	//strcpy(temp_filename, scen_directory);
	strcpy(temp_filename, filename);
	strcat(temp_filename, ".gtl"); // gladiator team list
	
	if ( (outfile = open_write_file("save/", temp_filename)) == NULL ) // open for write
	{
		//gotoxy(1, 22);
		Log("Error in writing team file %s\n", filename);
		return 0;
	}

	// Write id header
	SDL_RWwrite(outfile, temptext, 3, 1);

	// Write version number
	SDL_RWwrite(outfile, &temp_version, 1, 1);

	// Versions 7+ include a mark for registered or not
	temp_registered = 1;
	SDL_RWwrite(outfile, &temp_registered, 2, 1);

	// Write the name
	SDL_RWwrite(outfile, savedgame, 40, 1);
	
	// Write current campaign
	SDL_RWwrite(outfile, myscreen->current_campaign, 40, 1);

	// Write scenario number
	SDL_RWwrite(outfile, &next_scenario, 2, 1);

	// Write cash
	SDL_RWwrite(outfile, &newcash, 4, 1);
	// Write score
	SDL_RWwrite(outfile, &newscore, 4, 1);

	// Versions 6+ have a score for each possible team
	for (i=0; i < 4; i++)
	{
		newcash = myscreen->m_totalcash[i];
		SDL_RWwrite(outfile, &newcash, 4, 1);
		newscore = myscreen->m_totalscore[i];
		SDL_RWwrite(outfile, &newscore, 4, 1);
	}

	// Versions 7+ include the allied mode information
	temp_allied = myscreen->allied_mode;
	SDL_RWwrite(outfile, &temp_allied, 2, 1);

	// Determine size of team list ...
	listsize = 0;
	while (here)
	{
		if (here->ob && !here->ob->dead && here->ob->myguy)
			//if (here->ob && !here->ob->dead && here->ob->myguy &&
			//    (here->ob->real_team_num==0 || (here->ob->real_team_num==255
			//                                    && here->ob->team_num==0)
			//    )
			//   )
			listsize++;
		here = here->next;
	}

	//gotoxy(1, 22);
	//Log("Team size: %d  ", listsize);
	SDL_RWwrite(outfile, &listsize, 2, 1);

	SDL_RWwrite(outfile, &numplayers, 1, 1);

	// Write the reserved area, 31 bytes
	SDL_RWwrite(outfile, filler, 31, 1);

	// Okay, we've written header .. now dump the data ..
	here = myscreen->oblist;  // back to head of list
	while (here)
	{
		if (here->ob && !here->ob->dead && here->ob->myguy)
		{
			temp_walker = here->ob;

			// Get temp values to be saved
			temp_order = temp_walker->query_order(); // may be changed later
			temp_family= temp_walker->query_family();
			// Write name of current guy...
			strcpy(guyname, temp_walker->myguy->name);
			// Set any chars under 12 not used to 0 ..
			for (i=(short) strlen(guyname); i < 12; i++)
				guyname[i] = 0;
			temp_str = temp_walker->myguy->strength;
			temp_dex = temp_walker->myguy->dexterity;
			temp_con = temp_walker->myguy->constitution;
			temp_short = temp_walker->myguy->intelligence;
			temp_arm = temp_walker->myguy->armor;
			temp_lev = temp_walker->myguy->level;
			temp_exp = temp_walker->myguy->exp;
			// Version 3+ below here
			temp_kills = temp_walker->myguy->kills;
			temp_level_kills = temp_walker->myguy->level_kills;
			// Version 4+ below here
			temp_td = temp_walker->myguy->total_damage;
			temp_th = temp_walker->myguy->total_hits;
			temp_ts = temp_walker->myguy->total_shots;

			// Version 5+ below here
			temp_teamnum = temp_walker->myguy->teamnum;

			// Now write all those values
			SDL_RWwrite(outfile, &temp_order, 1, 1);
			SDL_RWwrite(outfile, &temp_family,1, 1);
			SDL_RWwrite(outfile, guyname, 12, 1);
			SDL_RWwrite(outfile, &temp_str, 2, 1);
			SDL_RWwrite(outfile, &temp_dex, 2, 1);
			SDL_RWwrite(outfile, &temp_con, 2, 1);
			SDL_RWwrite(outfile, &temp_short, 2, 1);
			SDL_RWwrite(outfile, &temp_arm, 2, 1);
			SDL_RWwrite(outfile, &temp_lev, 2, 1);
			SDL_RWwrite(outfile, &temp_exp, 4, 1);
			SDL_RWwrite(outfile, &temp_kills, 2, 1);
			SDL_RWwrite(outfile, &temp_level_kills, 4, 1);
			SDL_RWwrite(outfile, &temp_td, 4, 1);
			SDL_RWwrite(outfile, &temp_th, 4, 1);
			SDL_RWwrite(outfile, &temp_ts, 4, 1);
			SDL_RWwrite(outfile, &temp_teamnum, 2, 1);
			// And the filler
			SDL_RWwrite(outfile, filler, 8, 1);
		}
		// Advance to the next guy ..
		here = here->next;
	}

	// Write the completed levels
	// Number of campaigns
	short num_campaigns = myscreen->completed_levels.size();
    SDL_RWwrite(outfile, &num_campaigns, 2, 1);
	for(std::map<std::string, std::set<int> >::const_iterator e = myscreen->completed_levels.begin(); e != myscreen->completed_levels.end(); e++)
    {
        // Campaign ID
        char campaign[41];
        memset(campaign, 0, 41);
        strcpy(campaign, e->first.c_str());
        SDL_RWwrite(outfile, campaign, 1, 40);
        
        short index = 1;
        std::map<std::string, int>::const_iterator g = myscreen->current_levels.find(e->first);
        if(g != myscreen->current_levels.end())
            index = g->second;
        SDL_RWwrite(outfile, &index, 2, 1);
        
        // Number of levels
        short num_levels = e->second.size();
        SDL_RWwrite(outfile, &num_levels, 2, 1);
        for(std::set<int>::const_iterator f = e->second.begin(); f != e->second.end(); f++)
        {
            // Level index
            index = *f;
            SDL_RWwrite(outfile, &index, 2, 1);
        }
    }

    SDL_RWclose(outfile);

	return 1;
}

