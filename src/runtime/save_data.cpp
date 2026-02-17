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

#include <openglad/data/save_data.h>
#include <openglad/core/util.h>
#include <openglad/legacy/test_trace.h>
#include <format>

#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#ifndef OPENGLAD_HEADLESS
#include <openglad/ui/campaign_picker.h>
#include <openglad/platform/io.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

#ifndef OPENGLAD_HEADLESS
namespace {
bool rw_read_exact(SDL_RWops* rwops, void* dst, size_t size, size_t count)
{
    return rwops && SDL_RWread(rwops, dst, size, count) == count;
}

bool rw_write_exact(SDL_RWops* rwops, const void* src, size_t size, size_t count)
{
    return rwops && SDL_RWwrite(rwops, src, size, count) == count;
}
} // namespace
#endif // !OPENGLAD_HEADLESS


#ifdef USE_TOUCH_INPUT
#define DISABLE_MULTIPLAYER
#endif


SaveData::SaveData()
    : current_campaign("org.openglad.gladiator"), scen_num(1), score(0), totalcash(0), totalscore(0), my_team(0), numplayers(1), allied_mode(1)
{
    completed_levels.insert(std::make_pair("org.openglad.gladiator", std::set<int>()));
    current_levels.insert(std::make_pair("org.openglad.gladiator", 1));
    
    for (size_t i = 0; i < std::size(m_score); i++)
	{
		m_score[i] = 0;
		m_totalcash[i] = 5000;
		m_totalscore[i] = 0;
	}
	
	team_size = 0;
}

SaveData::~SaveData()
{
}

void SaveData::reset()
{
	current_campaign = "org.openglad.gladiator";
	completed_levels.clear();
    current_levels.clear();
    completed_levels.insert(std::make_pair("org.openglad.gladiator", std::set<int>()));
    current_levels.insert(std::make_pair("org.openglad.gladiator", 1));
	

	score = totalcash = totalscore = 0;
    for (size_t i = 0; i < std::size(m_score); i++)
	{
		m_score[i] = 0;
		m_totalcash[i] = 5000;
		m_totalscore[i] = 0;
	}
	
	for(int i = 0; i < team_size; i++)
    {
        team_list[i].reset();
    }
	team_size = 0;
	
	scen_num = 1;
	my_team = 0;
    //numplayers = 1;
	//allied_mode = 1;
}

#ifndef OPENGLAD_HEADLESS
bool SaveData::load(const std::string& filename)
{
    last_io_error_ = SaveDataIoError::None;
	TRACE("load", "SaveData::load file=%s", filename.c_str());
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *infile;

	char temptext[10] = "GTL";
	char savedgame[40];
	char temp_campaign[41];
	snprintf(temp_campaign, sizeof(temp_campaign), "org.openglad.gladiator");
	temp_campaign[40] = '\0';
	std::uint8_t temp_version = 9;
	Uint32 newcash;
	Uint32 newscore = 0;
	//  short numguys;
	std::int16_t listsize = 0;

	char tempname[12] = "FRED";
	char guyname[12] = "JOE";
	std::uint8_t temp_order = 0;
	char temp_family;
	std::int16_t temp_str = 0;
	std::int16_t temp_dex = 0;
	std::int16_t temp_con = 0;
	std::int16_t temp_short = 0;
	std::int16_t temp_arm = 0;
	std::int16_t temp_lev = 0;
	std::uint8_t temp_numplayers = 0;
	Uint32 temp_exp;
	std::int16_t temp_kills = 0;
	Sint32 temp_level_kills;
	Sint32 temp_td, temp_th, temp_ts;
	std::int16_t temp_teamnum = 0; // version 5+
	std::int16_t temp_allied = 0;            // v.7+
	std::int16_t temp_registered = 0;        // v.7+

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
	// 2-bytes level  // Does not include upgraded stats in version 8 or lower
	// 4-bytes unsigned experience
	// 2-bytes # kills, v.3
	// 4-bytes # total levels killed, v.3
	// 4-bytes total damage dealt, v.4+
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
    
    Log("Loading save: {}\n", filename);
	std::string temp_filename = std::format("{}.gtl", filename); // gladiator team list

	if ( (infile = open_read_file("save/", temp_filename.c_str())) == nullptr )
	{
		LogError("Failed to open save file: {}\n", filename);
        last_io_error_ = SaveDataIoError::OpenReadFailed;
		return 0;
	}

#define READ_OR_FAIL(dst, size, count) \
    do { \
        if(!rw_read_exact(infile, (dst), (size), (count))) { \
            LogError("Failed to read save file: {} ({} bytes x {})\n", filename, (int)(size), (int)(count)); \
            SDL_RWclose(infile); \
            last_io_error_ = SaveDataIoError::ReadFailed; \
            return 0; \
        } \
    } while(0)
    
    completed_levels.clear();
    current_levels.clear();
    
	for(int i = 0; i < team_size; i++)
    {
        team_list[i].reset();
    }
    team_size = 0;

	// Read id header
	READ_OR_FAIL(temptext, 3, 1);
	if ( std::string(temptext) != "GTL")
	{
	    SDL_RWclose(infile);
		LogError("Selected file is not a GTL file: {}\n", filename);
        last_io_error_ = SaveDataIoError::InvalidHeader;
		return 0; //not a gtl file
	}

	// Read version number
	READ_OR_FAIL(&temp_version, 1, 1);

	// Versions 7+ have a registered mark ..
	if (temp_version >= 7)
	{
		READ_OR_FAIL(&temp_registered, 2, 1);
	}

	// Do other stuff based on version ..
	if (temp_version != 1)
	{
		if (temp_version >= 2)
			READ_OR_FAIL(savedgame, 40, 1); // read and ignore the name
		else
		{
            SDL_RWclose(infile);
			LogError("Save file version not supported: {}\n", filename);
            last_io_error_ = SaveDataIoError::UnsupportedVersion;
			return 0;
		}
	}
	save_name = savedgame;

    // Read campaign ID
	if (temp_version >= 8)
	{
		READ_OR_FAIL(temp_campaign, 1, 40);
		temp_campaign[40] = '\0';
		if(std::string(temp_campaign).size() > 3)
            current_campaign = temp_campaign;
        else
            current_campaign = "org.openglad.gladiator";
	}
	
	// Read scenario number
	std::int16_t temp_scenario = 0;
	READ_OR_FAIL(&temp_scenario, 2, 1);
	scen_num = temp_scenario;

	// Read cash
	READ_OR_FAIL(&newcash, 4, 1);
	totalcash = newcash;
	// Read score
	READ_OR_FAIL(&newscore, 4, 1);
	totalscore = newscore;

		// Versions 6+ have a score for each possible team, 0-3
		if (temp_version >= 6)
		{
            for (size_t team_idx = 0; team_idx < std::size(m_totalcash); team_idx++)
			{
				READ_OR_FAIL(&newcash, 4, 1);
				m_totalcash[team_idx] = newcash;
				READ_OR_FAIL(&newscore, 4, 1);
				m_totalscore[team_idx] = newscore;
			}
		}

	// Versions 7+ have the allied information ..
	if (temp_version >= 7)
	{
		READ_OR_FAIL(&temp_allied, 2, 1);
		allied_mode = temp_allied;
	}

	// Get # of guys to read
	READ_OR_FAIL(&listsize, 2, 1);
    const bool invalid_team_size = listsize > MAX_TEAM_SIZE;
    if (invalid_team_size)
    {
        LogError("save_load_team_size_invalid file={} listsize={} max={}\n",
            filename, listsize, MAX_TEAM_SIZE);
    }

	// Read the # of players
	READ_OR_FAIL(&temp_numplayers, 1, 1);
	#ifdef DISABLE_MULTIPLAYER
	temp_numplayers = 1;
	#endif
	numplayers = temp_numplayers;

	// Read the reserved area, 31 bytes
	READ_OR_FAIL(filler, 31, 1);

	// Okay, we've read header .. now read the team list data ..
    for(int i = 0; i < listsize; i++)
    {
        guy* temp_guy_ptr = nullptr;
        if (i < MAX_TEAM_SIZE)
        {
            auto temp_guy = std::make_unique<guy>();
            temp_guy_ptr = temp_guy.get();
            team_list[i] = std::move(temp_guy);
            team_size++;
        }
        
		// Get temp values to be read
		temp_order = static_cast<unsigned char>(Order::Living); // may be changed later
		// Read name of current guy...
		std::fill_n(guyname, 12, '\0');
		snprintf(guyname, sizeof(guyname), "%s", tempname);
		// Now write all those values
		READ_OR_FAIL(&temp_order, 1, 1);
		READ_OR_FAIL(&temp_family,1, 1);
		READ_OR_FAIL(guyname, 12, 1);
		READ_OR_FAIL(&temp_str, 2, 1);
		READ_OR_FAIL(&temp_dex, 2, 1);
		READ_OR_FAIL(&temp_con, 2, 1);
		READ_OR_FAIL(&temp_short, 2, 1);
		READ_OR_FAIL(&temp_arm, 2, 1);
		READ_OR_FAIL(&temp_lev, 2, 1);
		READ_OR_FAIL(&temp_exp, 4, 1);
		// Below here is version 3 and up..
		READ_OR_FAIL(&temp_kills, 2, 1); // how many kills we have
		READ_OR_FAIL(&temp_level_kills, 4, 1); // levels of kills
		// Below here is version 4 and up ..
		READ_OR_FAIL(&temp_td, 4, 1); // total damage
		READ_OR_FAIL(&temp_th, 4, 1); // total hits
		READ_OR_FAIL(&temp_ts, 4, 1); // total shots
		READ_OR_FAIL(&temp_teamnum, 2, 1); // team number

		// And the filler
		READ_OR_FAIL(filler, 8, 1);
			// Now set the values ..
            if (temp_guy_ptr != nullptr)
            {
			    temp_guy_ptr->family       = temp_family;
			    temp_guy_ptr->name = guyname;
			    temp_guy_ptr->strength     = temp_str;
			    temp_guy_ptr->dexterity    = temp_dex;
			    temp_guy_ptr->constitution = temp_con;
			    temp_guy_ptr->intelligence = temp_short;
			    temp_guy_ptr->armor        = temp_arm;
			    if(temp_version >= 9)
	                temp_guy_ptr->level = temp_lev;
	            else
	                temp_guy_ptr->upgrade_to_level(temp_lev);
			    temp_guy_ptr->exp          = temp_exp;
			    if (temp_version >=3)
			    {
				    temp_guy_ptr->kills      = temp_kills;
				    temp_guy_ptr->level_kills= temp_level_kills;
			    }
			    else // version 2 or earlier
			    {
				    temp_guy_ptr->kills      = 0;
				    temp_guy_ptr->level_kills= 0;
			    }
			    if (temp_version >= 4)
			    {
				    temp_guy_ptr->total_damage = temp_td;
				    temp_guy_ptr->total_hits   = temp_th;
				    temp_guy_ptr->total_shots  = temp_ts;
			    }
			    else
			    {
				    temp_guy_ptr->total_damage = 0;
				    temp_guy_ptr->total_hits   = 0;
				    temp_guy_ptr->total_shots  = 0;
			    }
			    if (temp_version >= 5)
			    {
				    temp_guy_ptr->teamnum = temp_teamnum;
			    }
			    else
			    {
				    temp_guy_ptr->teamnum = 0;
			    }
            }
		}

    if (invalid_team_size)
    {
        last_io_error_ = SaveDataIoError::ReadFailed;
        SDL_RWclose(infile);
        return false;
    }
	
    // Make sure the default campaign is included
	completed_levels.insert(std::make_pair("org.openglad.gladiator", std::set<int>()));
	current_levels.insert(std::make_pair("org.openglad.gladiator", 1));

    if(temp_version < 8)
    {
        char levelstatus[MAX_LEVELS];
        std::fill_n(levelstatus, 500, '\0');
        
        if (temp_version >= 5)
            READ_OR_FAIL(levelstatus, 500, 1);
        else
            READ_OR_FAIL(levelstatus, 200, 1);
        
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
        READ_OR_FAIL(&num_campaigns, 2, 1);
        for(int i = 0; i < num_campaigns; i++)
        {
            // Get the campaign ID (40 chars)
            READ_OR_FAIL(campaign, 1, 40);
            campaign[40] = '\0';
            
            short index = 1;
            // Get the current level for this campaign
            READ_OR_FAIL(&index, 2, 1);
            current_levels[campaign] = index;
            
            // Get the number of cleared levels
            READ_OR_FAIL(&num_levels, 2, 1);
            for(int j = 0; j < num_levels; j++)
            {
                // Get the level index
                READ_OR_FAIL(&index, 2, 1);
                
                // Add it to our list
                add_level_completed(campaign, index);
            }
        }
    }
    
	Log("Loading campaign: {}\n", current_campaign);
    int current_level = load_campaign(current_campaign, current_levels);
    if(current_level < 0)
    {
        LogError("Failed to load current campaign {} from save {} (error code {})\n",
            current_campaign, filename, current_level);
        SDL_RWclose(infile);
        last_io_error_ = SaveDataIoError::CampaignLoadFailed;
        return 0;
    }
    if(current_level >= 0)
    {
        if(scen_num != current_level)
            LogError("save_load_level_mismatch save={} scen_num={} current_level={}\n",
                filename, scen_num, current_level);
        //scen_num = current_level;
    }

    SDL_RWclose(infile);

	TRACE("load", "SaveData::load complete: scen=%d team_size=%d", scen_num, team_size);
    last_io_error_ = SaveDataIoError::None;
#undef READ_OR_FAIL
	return 1;
}

Sint32 calculate_level(Uint32 temp_exp);

void SaveData::update_guys(std::list<std::unique_ptr<walker>>& oblist)
{
    // Delete our old guys
	for(int i = 0; i < team_size; i++)
    {
        team_list[i].reset();
    }
    team_size = 0;


    // Remove new (or existing) "guys" from the list and store them in this SaveData to be saved and trained.
    for(auto& uptr : oblist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead && ob->myguy)
		{
		    // Take this one
			team_list[team_size] = std::make_unique<guy>(*ob->myguy);
			// Update his level from the experience
			Uint32 exp = team_list[team_size]->exp;
			team_list[team_size]->upgrade_to_level(static_cast<short>(calculate_level(team_list[team_size]->exp)));
			team_list[team_size]->exp = exp;
			team_size++;
		}
	}
}


bool SaveData::save(const std::string& filename)
{
    last_io_error_ = SaveDataIoError::None;
	TRACE("save", "SaveData::save file=%s", filename.c_str());
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *outfile;
	char savedgame[41];
	std::fill_n(savedgame, 41, '\0');
	char temp_campaign[41];
	std::fill_n(temp_campaign, 41, '\0');

	char temptext[10] = "GTL";
	std::uint8_t temp_version = 9;
	
	Uint32 newcash = totalcash;
	Uint32 newscore = totalscore;
	//  short numguys;
	std::int16_t listsize = 0;

	char guyname[12] = "JOE";
	std::uint8_t temp_order = 0;
	char temp_family;
	std::int16_t temp_str = 0;
	std::int16_t temp_dex = 0;
	std::int16_t temp_con = 0;
	std::int16_t temp_short = 0;
	std::int16_t temp_arm = 0;
	std::int16_t temp_lev = 0;
	std::uint8_t numplayers_to_save = this->numplayers;
	Uint32 temp_exp;
	std::int16_t temp_kills = 0;
	Sint32 temp_level_kills;
	Sint32 temp_td, temp_th, temp_ts;
	std::int16_t temp_teamnum = 0;
	std::int16_t temp_allied = 0;
	std::int16_t temp_registered = 0;


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
	// 2-bytes level  // Does not include upgraded stats in versions 8 or lower
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
	Log("Saving save: {}\n", filename);
	std::string temp_filename = std::format("{}.gtl", filename); // gladiator team list

	if ( (outfile = open_write_file("save/", temp_filename.c_str())) == nullptr ) // open for write
	{
		LogError("Failed to write team file: {}\n", filename);
        last_io_error_ = SaveDataIoError::OpenWriteFailed;
		return 0;
	}

#define WRITE_OR_FAIL(src, size, count) \
    do { \
        if(!rw_write_exact(outfile, (src), (size), (count))) { \
            LogError("Failed to write save file: {} ({} bytes x {})\n", filename, (int)(size), (int)(count)); \
            SDL_RWclose(outfile); \
            last_io_error_ = SaveDataIoError::WriteFailed; \
            return 0; \
        } \
    } while(0)

	// Write id header
	WRITE_OR_FAIL(temptext, 3, 1);

	// Write version number
	WRITE_OR_FAIL(&temp_version, 1, 1);

	// Versions 7+ include a mark for registered or not
	temp_registered = 1;
	WRITE_OR_FAIL(&temp_registered, 2, 1);

	// Write the name
	snprintf(savedgame, sizeof(savedgame), "%s", save_name.c_str());
	WRITE_OR_FAIL(savedgame, 40, 1);
	
	// Write current campaign
	Log("Saving campaign status: {}\n", current_campaign);
	snprintf(temp_campaign, sizeof(temp_campaign), "%s", current_campaign.c_str());
	WRITE_OR_FAIL(temp_campaign, 40, 1);

	// Write scenario number
	short temp_scenario = scen_num;
	WRITE_OR_FAIL(&temp_scenario, 2, 1);

	// Write cash
	WRITE_OR_FAIL(&newcash, 4, 1);
	// Write score
	WRITE_OR_FAIL(&newscore, 4, 1);

	// Versions 6+ have a score for each possible team
    for (size_t team_idx = 0; team_idx < std::size(m_totalcash); team_idx++)
	{
		newcash = m_totalcash[team_idx];
		WRITE_OR_FAIL(&newcash, 4, 1);
		newscore = m_totalscore[team_idx];
		WRITE_OR_FAIL(&newscore, 4, 1);
	}

	// Versions 7+ include the allied mode information
	temp_allied = allied_mode;
	WRITE_OR_FAIL(&temp_allied, 2, 1);

	// Determine size of team list ...
	listsize = static_cast<std::int16_t>(team_size);

	//gotoxy(1, 22);
	//Log("Team size: %d  ", listsize);
	WRITE_OR_FAIL(&listsize, 2, 1);

	WRITE_OR_FAIL(&numplayers_to_save, 1, 1);

	// Write the reserved area, 31 bytes
	WRITE_OR_FAIL(filler, 31, 1);

	// Okay, we've written header .. now dump the data ..
	for(int team_idx = 0; team_idx < team_size; team_idx++)
	{
	    guy* temp_guy = team_list[team_idx].get();
	    
        // Get temp values to be saved
        temp_order = static_cast<unsigned char>(Order::Living);
        temp_family= temp_guy->family;
        // Write name of current guy...
        std::fill_n(guyname, 12, '\0');
        snprintf(guyname, sizeof(guyname), "%s", temp_guy->name.c_str());
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
        WRITE_OR_FAIL(&temp_order, 1, 1);
        WRITE_OR_FAIL(&temp_family,1, 1);
        WRITE_OR_FAIL(guyname, 12, 1);
        WRITE_OR_FAIL(&temp_str, 2, 1);
        WRITE_OR_FAIL(&temp_dex, 2, 1);
        WRITE_OR_FAIL(&temp_con, 2, 1);
        WRITE_OR_FAIL(&temp_short, 2, 1);
        WRITE_OR_FAIL(&temp_arm, 2, 1);
        WRITE_OR_FAIL(&temp_lev, 2, 1);
        WRITE_OR_FAIL(&temp_exp, 4, 1);
        WRITE_OR_FAIL(&temp_kills, 2, 1);
        WRITE_OR_FAIL(&temp_level_kills, 4, 1);
        WRITE_OR_FAIL(&temp_td, 4, 1);
        WRITE_OR_FAIL(&temp_th, 4, 1);
        WRITE_OR_FAIL(&temp_ts, 4, 1);
        WRITE_OR_FAIL(&temp_teamnum, 2, 1);
        // And the filler
        WRITE_OR_FAIL(filler, 8, 1);
	}

	// Write the completed levels
	
	// Make sure our current level is saved
	std::map<std::string, int>::iterator cur = current_levels.find(current_campaign);
	if(cur != current_levels.end())
    {
        cur->second = scen_num;
    }
    else
    {
        current_levels.insert(std::make_pair(current_campaign, scen_num));
    }
    
	// Number of campaigns
	short num_campaigns = static_cast<short>(completed_levels.size());
    WRITE_OR_FAIL(&num_campaigns, 2, 1);
	for(std::map<std::string, std::set<int> >::const_iterator e = completed_levels.begin(); e != completed_levels.end(); e++)
    {
        // Campaign ID
        char campaign[41];
        std::fill_n(campaign, 41, '\0');
        snprintf(campaign, sizeof(campaign), "%s", e->first.c_str());
        WRITE_OR_FAIL(campaign, 1, 40);
        
	        short index = 1;
	        std::map<std::string, int>::const_iterator g = current_levels.find(e->first);
	        if(g != current_levels.end())
	            index = static_cast<short>(g->second);
	        WRITE_OR_FAIL(&index, 2, 1);
        
	        // Number of levels
	        short num_levels = static_cast<short>(e->second.size());
	        WRITE_OR_FAIL(&num_levels, 2, 1);
        for(std::set<int>::const_iterator f = e->second.begin(); f != e->second.end(); f++)
	        {
	            // Level index
	            index = static_cast<short>(*f);
	            WRITE_OR_FAIL(&index, 2, 1);
	        }
	    }

    SDL_RWclose(outfile);

    // Sync to persistent storage (IDBFS on web)
    sync_filesystem();

	TRACE("save", "SaveData::save complete");
    last_io_error_ = SaveDataIoError::None;
#undef WRITE_OR_FAIL
	return 1;
}

SaveDataIoError SaveData::load_with_error(const std::string& filename)
{
    load(filename);
    return last_io_error_;
}

SaveDataIoError SaveData::save_with_error(const std::string& filename)
{
    save(filename);
    return last_io_error_;
}
#else // OPENGLAD_HEADLESS
// Headless mode: save/load not supported (no SDL_RWops).
// These paths should not be reached in normal headless simulation.
bool SaveData::load(const std::string& filename)
{
    LogWarn("SaveData::load('{}') called in headless mode — not supported\n", filename);
    return false;
}

bool SaveData::save(const std::string& filename)
{
    LogWarn("SaveData::save('{}') called in headless mode — not supported\n", filename);
    return false;
}
#endif // !OPENGLAD_HEADLESS



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
    
    return static_cast<int>(e->second.size());
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
