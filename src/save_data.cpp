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

#include "save_data.h"

#include "walker.h"
#include "guy.h"
#include "campaign_picker.h"




SaveData::SaveData()
    : current_campaign("org.openglad.gladiator"), scen_num(1), score(0), totalcash(0), totalscore(0), my_team(0), numplayers(1), allied_mode(1)
{
    completed_levels.insert(std::make_pair("org.openglad.gladiator", std::set<int>()));
    current_levels.insert(std::make_pair("org.openglad.gladiator", 1));
    
	for(int i = 0; i < 4; i++)
	{
		m_score[i] = 0;             // For Player-v-Player
		m_totalcash[i] = 0;
		m_totalscore[i] = 0;
	}
	
	team_size = 0;
	for(int i = 0; i < MAX_TEAM_SIZE; i++)
    {
        team_list[i] = NULL;
    }
}

SaveData::~SaveData()
{
	for(int i = 0; i < MAX_TEAM_SIZE; i++)
    {
        delete team_list[i];
    }
}

void SaveData::reset()
{
	current_campaign = "org.openglad.gladiator";
	completed_levels.clear();
    current_levels.clear();
    completed_levels.insert(std::make_pair("org.openglad.gladiator", std::set<int>()));
    current_levels.insert(std::make_pair("org.openglad.gladiator", 1));
	

	score = totalcash = totalscore = 0;
	for (int i = 0; i < 4; i++)
	{
		m_score[i] = 0;
		m_totalcash[i] = 5000;
		m_totalscore[i] = 0;
	}
	
	for(int i = 0; i < team_size; i++)
    {
        delete team_list[i];
        team_list[i] = NULL;
    }
	team_size = 0;
	
	scen_num = 1;
	my_team = 0;
    //numplayers = 1;
	//allied_mode = 1;
}

bool SaveData::load(const std::string& filename)
{
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *infile;
	char temp_filename[80];

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
	unsigned char temp_numplayers;
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
    
    
	strcpy(temp_filename, filename.c_str());
	strcat(temp_filename, ".gtl"); // gladiator team list

	if ( (infile = open_read_file("save/", temp_filename)) == NULL )
	{
		Log("Failed to open save file: %s\n", filename.c_str());
		return 0;
	}
    
    completed_levels.clear();
    current_levels.clear();
    
	for(int i = 0; i < team_size; i++)
    {
        delete team_list[i];
        team_list[i] = NULL;
    }
    team_size = 0;

	// Read id header
	SDL_RWread(infile, temptext, 3, 1);
	if ( strcmp(temptext,"GTL"))
	{
	    SDL_RWclose(infile);
		Log("Error, selected file is not a GTL file: %s\n", filename.c_str());
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
			Log("Error, selected files is not version one: %s\n", filename.c_str());
			return 0;
		}
	}
	save_name = savedgame;

    // Read campaign ID
	if (temp_version >= 8)
	{
		SDL_RWread(infile, temp_campaign, 1, 40);
		temp_campaign[40] = '\0';
		if(strlen(temp_campaign) > 3)
            current_campaign = temp_campaign;
        else
            current_campaign = "org.openglad.gladiator";
	}
	
	// Read scenario number
	short temp_scenario;
	SDL_RWread(infile, &temp_scenario, 2, 1);
	scen_num = temp_scenario;

	// Read cash
	SDL_RWread(infile, &newcash, 4, 1);
	totalcash = newcash;
	// Read score
	SDL_RWread(infile, &newscore, 4, 1);
	totalscore = newscore;

	// Versions 6+ have a score for each possible team, 0-3
	if (temp_version >= 6)
	{
		for (i=0; i < 4; i++)
		{
			SDL_RWread(infile, &newcash, 4, 1);
			m_totalcash[i] = newcash;
			SDL_RWread(infile, &newscore, 4, 1);
			m_totalscore[i] = newscore;
		}
	}

	// Versions 7+ have the allied information ..
	if (temp_version >= 7)
	{
		SDL_RWread(infile, &temp_allied, 2, 1);
		allied_mode = temp_allied;
	}

	// Get # of guys to read
	SDL_RWread(infile, &listsize, 2, 1);

	// Read (and ignore) the # of players
	SDL_RWread(infile, &temp_numplayers, 1, 1);
	numplayers = temp_numplayers;

	// Read the reserved area, 31 bytes
	SDL_RWread(infile, filler, 31, 1);

	// Okay, we've read header .. now read the team list data ..
    for(int i = 0; i < listsize; i++)
    {
        guy* temp_guy = new guy;
        team_list[i] = temp_guy;
        team_size++;
        
		// Get temp values to be read
		temp_order = ORDER_LIVING; // may be changed later
		// Read name of current guy...
		memset(guyname, 0, 12);
		strcpy(guyname, tempname);
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
	}
	
    // Make sure the default campaign is included
	completed_levels.insert(std::make_pair("org.openglad.gladiator", std::set<int>()));
	current_levels.insert(std::make_pair("org.openglad.gladiator", 1));

    if(temp_version < 8)
    {
        char levelstatus[MAX_LEVELS];
        memset(levelstatus, 0, 500);
        
        if (temp_version >= 5)
            SDL_RWread(infile, levelstatus, 500, 1);
        else
            SDL_RWread(infile, levelstatus, 200, 1);
        
        // Guaranteed to be the default campaign if version < 8
        for(int i = 0; i < 500; i++)
        {
            if(levelstatus[i])
                add_level_completed(current_campaign, i);
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
            current_levels[campaign] = index;
            
            // Get the number of cleared levels
            SDL_RWread(infile, &num_levels, 2, 1);
            for(int j = 0; j < num_levels; j++)
            {
                // Get the level index
                SDL_RWread(infile, &index, 2, 1);
                
                // Add it to our list
                add_level_completed(campaign, index);
            }
        }
    }
    
	
    int current_level = load_campaign(current_campaign, current_levels);
    if(current_level >= 0)
    {
        if(scen_num != current_level)
            Log("Error: Loaded scen_num %d, but found current_level %d\n", scen_num, current_level);
        //scen_num = current_level;
    }

    SDL_RWclose(infile);

	return 1;
}


void SaveData::update_guys(oblink* oblist)
{
    // Delete our old guys
	for(int i = 0; i < team_size; i++)
    {
        delete team_list[i];
        team_list[i] = NULL;
    }
    team_size = 0;
    
    
    // Remove new (or existing) "guys" from the list and store them in this SaveData to be saved and trained.
    oblink* here = oblist;  // back to head of list
	while (here)
	{
		if (here->ob && !here->ob->dead && here->ob->myguy)
		{
		    // Take this one
			team_list[team_size] = new guy(*here->ob->myguy);
			team_size++;
		}
		here = here->next;
	}
}


bool SaveData::save(const std::string& filename)
{
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *outfile;
	char temp_filename[80];
	char savedgame[41];
	memset(savedgame, 0, 41);
	char temp_campaign[41];
	memset(temp_campaign, 0, 41);

	char temptext[10] = "GTL";
	char temp_version = 8;
	
	Uint32 newcash = totalcash;
	Uint32 newscore = totalscore;
	//  short numguys;
	short listsize;
	short i;

	char guyname[12] = "JOE";
	char temp_order, temp_family;
	short temp_str, temp_dex, temp_con;
	short temp_short, temp_arm, temp_lev;
	unsigned char numplayers = this->numplayers;
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
	strcpy(temp_filename, filename.c_str());
	strcat(temp_filename, ".gtl"); // gladiator team list
	
	if ( (outfile = open_write_file("save/", temp_filename)) == NULL ) // open for write
	{
		//gotoxy(1, 22);
		Log("Error in writing team file %s\n", filename.c_str());
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
	strncpy(savedgame, save_name.c_str(), 40);
	SDL_RWwrite(outfile, savedgame, 40, 1);
	
	// Write current campaign
	strncpy(temp_campaign, current_campaign.c_str(), 40);
	SDL_RWwrite(outfile, temp_campaign, 40, 1);

	// Write scenario number
	short temp_scenario = scen_num;
	SDL_RWwrite(outfile, &temp_scenario, 2, 1);

	// Write cash
	SDL_RWwrite(outfile, &newcash, 4, 1);
	// Write score
	SDL_RWwrite(outfile, &newscore, 4, 1);

	// Versions 6+ have a score for each possible team
	for (i=0; i < 4; i++)
	{
		newcash = m_totalcash[i];
		SDL_RWwrite(outfile, &newcash, 4, 1);
		newscore = m_totalscore[i];
		SDL_RWwrite(outfile, &newscore, 4, 1);
	}

	// Versions 7+ include the allied mode information
	temp_allied = allied_mode;
	SDL_RWwrite(outfile, &temp_allied, 2, 1);

	// Determine size of team list ...
	listsize = team_size;

	//gotoxy(1, 22);
	//Log("Team size: %d  ", listsize);
	SDL_RWwrite(outfile, &listsize, 2, 1);

	SDL_RWwrite(outfile, &numplayers, 1, 1);

	// Write the reserved area, 31 bytes
	SDL_RWwrite(outfile, filler, 31, 1);

	// Okay, we've written header .. now dump the data ..
	for(int i = 0; i < team_size; i++)
	{
	    guy* temp_guy = team_list[i];
	    
        // Get temp values to be saved
        temp_order = ORDER_LIVING;
        temp_family= temp_guy->family;
        // Write name of current guy...
        strcpy(guyname, temp_guy->name);
        // Set any chars under 12 not used to 0 ..
        for (int j = strlen(guyname); j < 12; j++)
            guyname[j] = 0;
        temp_str = temp_guy->strength;
        temp_dex = temp_guy->dexterity;
        temp_con = temp_guy->constitution;
        temp_short = temp_guy->intelligence;
        temp_arm = temp_guy->armor;
        temp_lev = temp_guy->level;
        temp_exp = temp_guy->exp;
        // Version 3+ below here
        temp_kills = temp_guy->kills;
        temp_level_kills = temp_guy->level_kills;
        // Version 4+ below here
        temp_td = temp_guy->total_damage;
        temp_th = temp_guy->total_hits;
        temp_ts = temp_guy->total_shots;

        // Version 5+ below here
        temp_teamnum = temp_guy->teamnum;

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

	// Write the completed levels
	
	// Make sure our current level is saved
	std::map<std::string, int>::iterator cur = current_levels.find(current_campaign);
	if(cur != current_levels.end())
    {
        cur->second = scen_num;
    }
    else
        current_levels.insert(std::make_pair(current_campaign, scen_num));
    
	// Number of campaigns
	short num_campaigns = completed_levels.size();
    SDL_RWwrite(outfile, &num_campaigns, 2, 1);
	for(std::map<std::string, std::set<int> >::const_iterator e = completed_levels.begin(); e != completed_levels.end(); e++)
    {
        // Campaign ID
        char campaign[41];
        memset(campaign, 0, 41);
        strcpy(campaign, e->first.c_str());
        SDL_RWwrite(outfile, campaign, 1, 40);
        
        short index = 1;
        std::map<std::string, int>::const_iterator g = current_levels.find(e->first);
        if(g != current_levels.end())
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



bool SaveData::is_level_completed(int level_index) const
{
    std::map<std::string, std::set<int> >::const_iterator e = completed_levels.find(current_campaign);
    // Campaign not found?  Then this level is not done.
    if(e == completed_levels.end())
        return false;
    
    // If the level is listed, then it is completed.
    std::set<int>::const_iterator f = e->second.find(level_index);
    return (f != e->second.end());
}

int SaveData::get_num_levels_completed(const std::string& campaign) const
{
    std::map<std::string, std::set<int> >::const_iterator e = completed_levels.find(campaign);
    // Campaign not found?
    if(e == completed_levels.end())
        return 0;
    
    return e->second.size();
}

void SaveData::add_level_completed(const std::string& campaign, int level_index)
{
    std::map<std::string, std::set<int> >::iterator e = completed_levels.find(campaign);
    
    // Campaign not found?  Add it in.
    if(e == completed_levels.end())
        e = completed_levels.insert(std::make_pair(campaign, std::set<int>())).first;
    
    // Add the completed level
    e->second.insert(level_index);
}

void SaveData::reset_campaign(const std::string& campaign)
{
    std::map<std::string, std::set<int> >::iterator e = completed_levels.find(campaign);
    
    if(e != completed_levels.end())
        e->second.clear();
}

