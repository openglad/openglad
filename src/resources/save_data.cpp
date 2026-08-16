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

#include <openglad/resources/save_data.h>
#include <openglad/core/campaign_ids.h>
#include <openglad/core/util.h>
#include <openglad/core/test_trace.h>
#include <format>

#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/resources/campaign_io.h>
#include <openglad/resources/filesystem_sync.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>


#ifdef USE_TOUCH_INPUT
#define DISABLE_MULTIPLAYER
#endif

namespace
{
constexpr unsigned char kMaxLegacyPlayers = kMaxLegacySavePlayers;
// GTL readers before player count became session state still consume the byte
// at offset 132. Keep it readable as one player without encoding this
// session's actual choice into the company.
constexpr std::uint8_t kLegacyPlayerCountCompatibility = 1;
constexpr int kMaxLegacyLevels = 500;
const std::string kDefaultCampaign{og::kDefaultCampaignId};
}


SaveData::SaveData()
    : current_campaign(kDefaultCampaign), scen_num(1), score(0), totalcash(0), totalscore(0), my_team(0), numplayers(1), allied_mode(1)
{
    completed_levels.insert(std::make_pair(kDefaultCampaign, std::set<int>()));
    current_levels.insert(std::make_pair(kDefaultCampaign, 1));

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
	current_campaign = kDefaultCampaign;
	completed_levels.clear();
    current_levels.clear();
    completed_levels.insert(std::make_pair(kDefaultCampaign, std::set<int>()));
    current_levels.insert(std::make_pair(kDefaultCampaign, 1));
    campaign_state.clear();

	score = totalcash = totalscore = 0;
    for (size_t i = 0; i < std::size(m_score); i++)
	{
		m_score[i] = 0;
		m_totalcash[i] = 5000;
		m_totalscore[i] = 0;
	}

	for(int i = 0; i < team_size; i++)
    {
        team_list[static_cast<std::size_t>(i)].reset();
    }
	team_size = 0;

	scen_num = 1;
	my_team = 0;
    //numplayers = 1;
	//allied_mode = 1;
}

bool SaveData::load(const std::string& filename)
{
    last_io_error_ = SaveDataIoError::None;
	TRACE("load", "SaveData::load file=%s", filename.c_str());
    if (!is_safe_virtual_basename(filename))
    {
        LogError("Rejected unsafe save file name for load: {}\n", filename);
        last_io_error_ = SaveDataIoError::OpenReadFailed;
        return false;
    }
	std::array<char, 50> filler = {'G', 'T', 'L', 'G', 'T', 'L', 'G', 'T', 'L',
		'G', 'T', 'L', 'G', 'T', 'L', 'G', 'T', 'L', 'G', 'T', 'L', 'G', 'T',
		'L', 'G', 'T', 'L', 'G', 'T', 'L', 'G', 'T', 'L', 'G', 'T', 'L', 'G',
		'T', 'L', 'G', 'T', 'L'}; // for RESERVED

	std::array<char, 10> temptext = {'G', 'T', 'L'};
	std::array<char, 41> savedgame;
	std::fill_n(savedgame.data(), savedgame.size(), '\0');
	std::array<char, 41> temp_campaign;
	snprintf(temp_campaign.data(), temp_campaign.size(), "%s", kDefaultCampaign.c_str());
	temp_campaign[40] = '\0';
	std::uint8_t temp_version = 9;
	std::uint32_t newcash;
	std::uint32_t newscore = 0;
	//  short numguys;
	std::int16_t listsize = 0;

	std::array<char, 12> tempname = {'F', 'R', 'E', 'D'};
	std::array<char, 12> guyname = {'J', 'O', 'E'};
	std::uint8_t temp_order = 0;
	char temp_family;
	std::int16_t temp_str = 0;
	std::int16_t temp_dex = 0;
	std::int16_t temp_con = 0;
	std::int16_t temp_short = 0;
	std::int16_t temp_arm = 0;
	std::int16_t temp_lev = 0;
	std::uint8_t legacy_numplayers = 0;
	std::uint32_t temp_exp;
	std::int16_t temp_kills = 0;
	std::int32_t temp_level_kills;
	std::int32_t temp_td, temp_th, temp_ts;
	std::int16_t temp_teamnum = 0; // version 5+
	std::int16_t temp_allied = 0;            // v.7+
	std::int16_t temp_registered = 0;        // v.7+
	std::int64_t temp_last_played = 0;       // v.14+
	std::uint8_t temp_deployed = 1;          // v.14+

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
	// This is now a legacy compatibility marker; the runtime count is not
	// saved, and old files may contain their historical value.
	// 31-bytes RESERVED, reinterpreted by version 14+ as:
	//   8-bytes (Sint64) last-played unix seconds (offset 133)  // Version 14+
	//   23-bytes RESERVED (zero-filled by v14+ writers)
	// (v13 and older files carry 'GTL' filler here — the two fields are
	// hard-gated on temp_version >= 14 and never sniffed from content)
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
	// 2*4 = 8 bytes RESERVED, reinterpreted by version 14+ as:
	//   1-byte deployed flag (0 = held back), guy offset +50  // Version 14+
	//   7-bytes RESERVED (zero-filled by v14+ writers)
	// List of 200 or 500 (max levels) 1-byte scenario-level status  // Versions 1-7
	// 2-bytes Number of campaigns in list      // Version 8+
	// List of n campaigns                      // Version 8+
	//   40-bytes Campaign ID string
	//   2-bytes Current level in this campaign
	//   2-bytes Number of level indices in list
	//   List of n level indices
	//     2-bytes Level index
	// 2-bytes CTF team count                   // Version 10+
	// 2-bytes CTF capture limit (0 = map/default)   // Version 10+
	// 2-bytes CTF respawn ticks (0 = default)  // Version 10+
	// 2-bytes CTF strip-scenario-troops flag   // Version 11+
	// 2-bytes respawn mode (0 = off)           // Version 12+
	// 2-bytes generator rate (0 = default)     // Version 12+
	// 2-bytes keep-fallen-heroes flag (0 = permadeath)  // Version 12+
	// 2-bytes Tower best floor climbed         // Version 13+
	// 2-bytes Tower run seed, low 16 bits      // Version 13+
	// 2-bytes Tower run seed, high 16 bits     // Version 13+
	// 2-bytes Number of campaigns with scripted state  // Version 15+
	// List of n campaign-state blocks          // Version 15+
	//   40-bytes Campaign ID string
	//   2-bytes Number of key/value entries (sorted by key)
	//   List of n entries
	//     1-byte key length (1..32)
	//     N-bytes key ([a-z0-9_], no NUL, no padding)
	//     4-bytes (Sint32) value

    Log("Loading save: {}\n", filename);
	std::string temp_filename = std::format("{}.gtl", filename); // gladiator team list

	og::io::OgFilePtr infile = og::io::og_open_read("save/", temp_filename.c_str());
	if (!infile)
	{
		LogError("Failed to open save file: {}\n", filename);
        last_io_error_ = SaveDataIoError::OpenReadFailed;
		return 0;
	}

#define READ_OR_FAIL(dst, size, count) \
    do { \
        if(!og::io::og_read_exact(*infile, (dst), (size), (count))) { \
            LogError("Failed to read save file: {} ({} bytes x {})\n", filename, (int)(size), (int)(count)); \
            last_io_error_ = SaveDataIoError::ReadFailed; \
            return 0; \
        } \
    } while(0)

    completed_levels.clear();
    current_levels.clear();
    campaign_state.clear();

	for(int i = 0; i < team_size; i++)
    {
        team_list[static_cast<std::size_t>(i)].reset();
    }
    team_size = 0;

	// Read id header
	READ_OR_FAIL(temptext.data(), 3, 1);
	if ( std::string(temptext.data()) != "GTL")
	{
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
			{
				READ_OR_FAIL(savedgame.data(), 40, 1); // load save name from fixed-width (40-byte) field
				savedgame[40] = '\0';
			}
		else
		{
			LogError("Save file version not supported: {}\n", filename);
            last_io_error_ = SaveDataIoError::UnsupportedVersion;
			return 0;
		}
	}
	save_name = savedgame.data();

    // Read campaign ID
	if (temp_version >= 8)
	{
		READ_OR_FAIL(temp_campaign.data(), 1, 40);
		temp_campaign[40] = '\0';
        const std::string loaded_campaign = temp_campaign.data();
		if(loaded_campaign.size() > 3 && is_safe_campaign_id(loaded_campaign))
            // Saves written before the reverse-DNS purge store
            // "org.openglad.<name>"; fold them onto the plain id.
            current_campaign = std::string(og::normalize_legacy_id(loaded_campaign));
        else
            current_campaign = kDefaultCampaign;
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
    const bool invalid_team_size = (listsize < 0) || (listsize > MAX_TEAM_SIZE);
    if (invalid_team_size)
    {
        LogError("save_load_team_size_invalid file={} listsize={} max={}\n",
            filename, listsize, MAX_TEAM_SIZE);
        last_io_error_ = SaveDataIoError::ReadFailed;
        return false;
    }

	// Read the # of players
	// Validate the historical byte so malformed legacy files keep the same
	// rejection behavior. Player count is session state: loading a company
	// must not replace the live runtime projection.
	READ_OR_FAIL(&legacy_numplayers, 1, 1);
    if (legacy_numplayers > kMaxLegacyPlayers)
    {
        LogError("save_load_numplayers_invalid file={} numplayers={} max={}\n",
            filename, (int)legacy_numplayers, (int)kMaxLegacyPlayers);
        last_io_error_ = SaveDataIoError::ReadFailed;
        return 0;
    }
	#ifdef DISABLE_MULTIPLAYER
	numplayers = 1;
	#endif

	// Read the reserved area, 31 bytes. Version 14+ reinterprets the first
	// 8 bytes as the last-played timestamp (offset 133); older files carry
	// filler there, so the field is hard-gated on the version byte.
	if (temp_version >= 14)
	{
		READ_OR_FAIL(&temp_last_played, 8, 1);
		READ_OR_FAIL(filler.data(), 23, 1);
		last_played_unix_s = temp_last_played;
	}
	else
	{
		READ_OR_FAIL(filler.data(), 31, 1);
		last_played_unix_s = 0;
	}

	// Okay, we've read header .. now read the team list data ..
    for(int i = 0; i < listsize; i++)
    {
        guy* temp_guy_ptr = nullptr;
        if (i < MAX_TEAM_SIZE)
        {
            auto temp_guy = std::make_unique<guy>();
            temp_guy_ptr = temp_guy.get();
            team_list[static_cast<std::size_t>(i)] = std::move(temp_guy);
            team_size++;
        }

		// Get temp values to be read
		temp_order = static_cast<unsigned char>(Order::Living); // may be changed later
		// Read name of current guy...
		std::fill_n(guyname.data(), 12, '\0');
		snprintf(guyname.data(), guyname.size(), "%s", tempname.data());
		// Now write all those values
		READ_OR_FAIL(&temp_order, 1, 1);
		READ_OR_FAIL(&temp_family,1, 1);
		READ_OR_FAIL(guyname.data(), 12, 1);
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

		// "And the filler," as the 2002 reader put it. Version 14+
		// reinterprets the first byte as the
		// mission-deploy flag (guy offset +50); older files hold filler.
		temp_deployed = 1;
		if (temp_version >= 14)
		{
			READ_OR_FAIL(&temp_deployed, 1, 1);
			READ_OR_FAIL(filler.data(), 7, 1);
		}
		else
		{
			READ_OR_FAIL(filler.data(), 8, 1);
		}
			// Now set the values ..
            if (temp_guy_ptr != nullptr)
            {
			    temp_guy_ptr->family       = temp_family;
			    temp_guy_ptr->name.assign(guyname.data(), strnlen(guyname.data(), guyname.size()));
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
			    // v14+ carries the deploy flag; older versions default to
			    // deployed (every legacy character was always brought)
			    temp_guy_ptr->deployed =
			        (temp_version >= 14) ? (temp_deployed != 0) : true;
            }
		}

    if (invalid_team_size)
    {
        last_io_error_ = SaveDataIoError::ReadFailed;
        return false;
    }

    // Make sure the default campaign is included
	completed_levels.insert(std::make_pair(kDefaultCampaign, std::set<int>()));
	current_levels.insert(std::make_pair(kDefaultCampaign, 1));

    if(temp_version < 8)
    {
        std::array<char, kMaxLegacyLevels> levelstatus{};

        if (temp_version >= 5)
            READ_OR_FAIL(levelstatus.data(), kMaxLegacyLevels, 1);
        else
            READ_OR_FAIL(levelstatus.data(), 200, 1);

        // Guaranteed to be the default campaign if version < 8
        for(int i = 0; i < kMaxLegacyLevels; i++)
        {
            if(levelstatus[static_cast<size_t>(i)])
                add_level_completed(current_campaign, i);
        }
    }
    else
    {
        short num_campaigns = 0;
        std::array<char, 41> campaign;
        short num_levels = 0;
        constexpr short kMaxSavedCampaigns = 128;
        constexpr short kMaxSavedLevels = 1000;
        // How many campaigns are stored?
        READ_OR_FAIL(&num_campaigns, 2, 1);
        if (num_campaigns < 0 || num_campaigns > kMaxSavedCampaigns)
        {
            LogError("save_load_num_campaigns_invalid file={} num_campaigns={} max={}\n",
                filename, num_campaigns, kMaxSavedCampaigns);
            last_io_error_ = SaveDataIoError::ReadFailed;
            return false;
        }
        for(int i = 0; i < num_campaigns; i++)
        {
            // Get the campaign ID (40 chars)
            READ_OR_FAIL(campaign.data(), 1, 40);
            campaign[40] = '\0';
            if (!is_safe_campaign_id(campaign.data()))
            {
                LogError("Rejected unsafe campaign id in save: {}\n", campaign.data());
                last_io_error_ = SaveDataIoError::ReadFailed;
                return false;
            }

            // Legacy "org.openglad." keys fold onto their plain id. When a
            // save carries both spellings of one campaign the progress
            // merges conservatively: the campaign cursor keeps the furthest
            // level, and completed sets union (add_level_completed below is
            // a set insert). New writes only ever emit plain keys.
            const std::string campaign_key(
                og::normalize_legacy_id(campaign.data()));

            short index = 1;
            // Get the current level for this campaign
            READ_OR_FAIL(&index, 2, 1);
            const auto [cursor, inserted] =
                current_levels.emplace(campaign_key, index);
            if (!inserted)
                cursor->second = std::max(cursor->second, static_cast<int>(index));

            // Get the number of cleared levels
            READ_OR_FAIL(&num_levels, 2, 1);
            if (num_levels < 0 || num_levels > kMaxSavedLevels)
            {
                LogError("save_load_num_levels_invalid file={} num_levels={} max={}\n",
                    filename, num_levels, kMaxSavedLevels);
                last_io_error_ = SaveDataIoError::ReadFailed;
                return false;
            }
            for(int j = 0; j < num_levels; j++)
            {
                // Get the level index
                READ_OR_FAIL(&index, 2, 1);

                // Add it to our list
                add_level_completed(campaign_key, index);
            }
        }
    }

    // Versions 10+ append the CTF match settings
    if (temp_version >= 10)
    {
        std::int16_t temp_ctf_teams = 2;
        std::int16_t temp_ctf_caps = 0;
        std::int16_t temp_ctf_respawn = 0;
        READ_OR_FAIL(&temp_ctf_teams, 2, 1);
        READ_OR_FAIL(&temp_ctf_caps, 2, 1);
        READ_OR_FAIL(&temp_ctf_respawn, 2, 1);
        ctf_team_count = temp_ctf_teams;
        ctf_capture_limit = temp_ctf_caps;
        ctf_respawn_ticks = temp_ctf_respawn;
    }
    else
    {
        ctf_team_count = 0; // Auto
        ctf_capture_limit = 0;
        ctf_respawn_ticks = 0;
    }

    // Versions 11+ append the CTF scenario-troops strip flag
    if (temp_version >= 11)
    {
        std::int16_t temp_ctf_strip = 0;
        READ_OR_FAIL(&temp_ctf_strip, 2, 1);
        ctf_strip_scenario_troops = temp_ctf_strip;
    }
    else
    {
        ctf_strip_scenario_troops = 0; // keep authored troops
    }

    // Versions 12+ append the difficulty submenu settings
    if (temp_version >= 12)
    {
        std::int16_t temp_respawn_mode = 0;
        std::int16_t temp_generator_rate = 0;
        std::int16_t temp_keep_fallen = 0;
        READ_OR_FAIL(&temp_respawn_mode, 2, 1);
        READ_OR_FAIL(&temp_generator_rate, 2, 1);
        READ_OR_FAIL(&temp_keep_fallen, 2, 1);
        respawn_mode = temp_respawn_mode;
        generator_rate = temp_generator_rate;
        keep_fallen_heroes = temp_keep_fallen;
    }
    else
    {
        respawn_mode = 0;       // respawns off
        generator_rate = 0;     // default rate
        keep_fallen_heroes = 0; // permadeath on win
    }

    // Versions 13+ append the Tower Climb fields (best floor + run seed,
    // seed carried as 2 x int16 lo/hi)
    if (temp_version >= 13)
    {
        std::int16_t temp_tower_best = 0;
        std::int16_t temp_tower_seed_lo = 0;
        std::int16_t temp_tower_seed_hi = 0;
        READ_OR_FAIL(&temp_tower_best, 2, 1);
        READ_OR_FAIL(&temp_tower_seed_lo, 2, 1);
        READ_OR_FAIL(&temp_tower_seed_hi, 2, 1);
        tower_best_floor = temp_tower_best;
        tower_run_seed =
            static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(temp_tower_seed_lo)) |
            (static_cast<std::uint32_t>(
                 static_cast<std::uint16_t>(temp_tower_seed_hi))
             << 16);
    }
    else
    {
        tower_best_floor = 0; // no climb recorded
        tower_run_seed = 0;   // no run in progress
    }

    // Versions 15+ append the per-campaign scripted state block (campaign
    // scripting, issue #206). Bounds are hard rejections in the existing
    // style: a hostile count, campaign id or key fails the whole load.
    // campaign_state was cleared at the top, so older versions stay empty.
    if (temp_version >= 15)
    {
        std::int16_t num_state_campaigns = 0;
        READ_OR_FAIL(&num_state_campaigns, 2, 1);
        if (num_state_campaigns < 0 ||
            num_state_campaigns > kCampaignStateMaxCampaigns)
        {
            LogError("save_load_campaign_state_campaigns_invalid file={} count={} max={}\n",
                filename, num_state_campaigns, kCampaignStateMaxCampaigns);
            last_io_error_ = SaveDataIoError::ReadFailed;
            return false;
        }
        std::array<char, 41> state_campaign;
        for (int i = 0; i < num_state_campaigns; i++)
        {
            READ_OR_FAIL(state_campaign.data(), 1, 40);
            state_campaign[40] = '\0';
            if (!is_safe_campaign_id(state_campaign.data()))
            {
                LogError("Rejected unsafe campaign id in save campaign state: {}\n",
                    state_campaign.data());
                last_io_error_ = SaveDataIoError::ReadFailed;
                return false;
            }
            std::int16_t num_state_entries = 0;
            READ_OR_FAIL(&num_state_entries, 2, 1);
            if (num_state_entries < 0 ||
                num_state_entries > kCampaignStateMaxEntries)
            {
                LogError("save_load_campaign_state_entries_invalid file={} campaign={} count={} max={}\n",
                    filename, state_campaign.data(), num_state_entries,
                    kCampaignStateMaxEntries);
                last_io_error_ = SaveDataIoError::ReadFailed;
                return false;
            }
            auto& state_entries = campaign_state[state_campaign.data()];
            state_entries.clear();
            state_entries.reserve(static_cast<std::size_t>(num_state_entries));
            for (int j = 0; j < num_state_entries; j++)
            {
                std::uint8_t key_len = 0;
                READ_OR_FAIL(&key_len, 1, 1);
                std::array<char, og::script::hooks::kCampaignVarNameMax>
                    key_buf;
                if (key_len == 0 ||
                    key_len > static_cast<std::uint8_t>(
                                  og::script::hooks::kCampaignVarNameMax))
                {
                    LogError("save_load_campaign_state_key_len_invalid file={} campaign={} len={}\n",
                        filename, state_campaign.data(), key_len);
                    last_io_error_ = SaveDataIoError::ReadFailed;
                    return false;
                }
                READ_OR_FAIL(key_buf.data(), 1, key_len);
                const std::string state_key(key_buf.data(), key_len);
                if (!og::script::hooks::valid_campaign_var_name(state_key))
                {
                    LogError("save_load_campaign_state_key_invalid file={} campaign={}\n",
                        filename, state_campaign.data());
                    last_io_error_ = SaveDataIoError::ReadFailed;
                    return false;
                }
                std::int32_t state_value = 0;
                READ_OR_FAIL(&state_value, 4, 1);
                // Sorted insert keeps the entry-list invariant; a
                // duplicated key folds (last one wins), so a re-save is
                // always deterministic.
                const auto pos = std::lower_bound(
                    state_entries.begin(), state_entries.end(), state_key,
                    [](const std::pair<std::string, std::int32_t>& entry,
                       const std::string& key) { return entry.first < key; });
                if (pos != state_entries.end() && pos->first == state_key)
                    pos->second = state_value;
                else
                    state_entries.insert(pos,
                                         std::make_pair(state_key, state_value));
            }
        }
    }

	Log("Loading campaign: {}\n", current_campaign);
    int current_level = load_campaign(current_campaign, current_levels);
    if(current_level < 0)
    {
        LogError("Failed to load current campaign {} from save {} (error code {})\n",
            current_campaign, filename, current_level);
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

	TRACE("load", "SaveData::load complete: scen=%d team_size=%d", scen_num, team_size);
    last_io_error_ = SaveDataIoError::None;
#undef READ_OR_FAIL
	return 1;
}

std::int32_t calculate_level(std::uint32_t temp_exp);

void SaveData::update_guys(const std::list<std::unique_ptr<walker>>& oblist,
                           bool preserve_exp_level)
{
    // #213 (versus arenas persist no XP): remember the pre-fold exp/level
    // per guy id BEFORE the roster is torn down, so the rebuild below can
    // restore them. The copy constructor preserves guy::id, so a walker's
    // myguy (a copy of the roster entry) still carries its roster identity.
    std::map<int, std::pair<std::uint32_t, short>> prior_exp_level;
    if (preserve_exp_level)
    {
        for (int i = 0; i < team_size; i++)
        {
            const guy* prior = team_list[static_cast<std::size_t>(i)].get();
            if (prior != nullptr)
                prior_exp_level.emplace(prior->id,
                                        std::make_pair(prior->exp, prior->level));
        }
    }

    // Pass 0 (docs/company-basecamp-design.md §3.3): held-back characters
    // (deployed == false) never entered the level's oblist, so rebuilding the
    // roster purely from the oblist would silently delete them. Move them
    // aside first and reserve their slots. 24-cap priority rule (pinned by
    // test): held-back characters always survive; newly-acquired recruits
    // are dropped first when the cap binds. With every guy deployed (all
    // legacy flows) passes 0 and 2 are empty and behavior is byte-identical.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> held_back;
    int held_back_count = 0;
    for (int i = 0; i < team_size; i++)
    {
        if (team_list[static_cast<std::size_t>(i)] != nullptr && !team_list[static_cast<std::size_t>(i)]->deployed)
        {
            held_back[static_cast<std::size_t>(held_back_count)] = std::move(team_list[static_cast<std::size_t>(i)]);
            held_back_count++;
        }
    }

    // Delete our old guys
	for(int i = 0; i < team_size; i++)
    {
        team_list[static_cast<std::size_t>(i)].reset();
    }
    team_size = 0;

    const int survivor_capacity = MAX_TEAM_SIZE - held_back_count;

    // Pass 1: remove new (or existing) "guys" from the list and store them in
    // this SaveData to be saved and trained.
    // Permadeath (keep_fallen_heroes == 0, the default) keeps only living guys;
    // with the toggle set, fallen heroes stay on the roster too.
    for(auto& uptr : oblist)
	{
	    walker* ob = uptr.get();
        if (ob && ob->myguy && (!ob->dead() || keep_fallen_heroes != 0))
		{
            if (team_size >= survivor_capacity)
            {
                continue;
            }
		    // Take this one
			team_list[team_size] = std::make_unique<guy>(*ob->myguy);
			// #213: in a versus arena a known roster member keeps its prior
			// exp AND level — the upgrade below (and its stat gains) is
			// skipped entirely, so the company file reads back byte-identical.
			const auto prior = preserve_exp_level
			    ? prior_exp_level.find(team_list[team_size]->id)
			    : prior_exp_level.end();
			if (prior != prior_exp_level.end())
			{
				team_list[team_size]->exp = prior->second.first;
				team_list[team_size]->level = prior->second.second;
			}
			else
			{
				// Update his level from the experience
				std::uint32_t exp = team_list[team_size]->exp;
				team_list[team_size]->upgrade_to_level(static_cast<short>(calculate_level(team_list[team_size]->exp)));
				team_list[team_size]->exp = exp;
			}
			team_size++;
		}
	}

    // Pass 2: move-append the held-back entries (their guy objects preserved,
    // stats untouched). NOTE: this changes roster ORDER when deploy toggles
    // are in use — UI must not hold positional slot indices across a win.
    for (int i = 0; i < held_back_count; i++)
    {
        team_list[team_size] = std::move(held_back[static_cast<std::size_t>(i)]);
        team_size++;
    }
}

void SaveData::merge_owned_guys_from(
    const std::list<std::unique_ptr<walker>>& oblist,
    std::uint8_t owner_player_index,
    bool preserve_exp_level)
{
    if (owner_player_index == guy::kNoOwner)
        return;

    const std::array<std::uint8_t, 1> owners = {owner_player_index};
    merge_owned_guys_from(oblist, std::span<const std::uint8_t>(owners),
                          preserve_exp_level);
}

void SaveData::merge_owned_guys_from(
    const std::list<std::unique_ptr<walker>>& oblist,
    std::span<const std::uint8_t> owner_player_indices,
    bool preserve_exp_level)
{
    // A machine with N local seats owns N players' characters; merge them all
    // in one pass so the load/rebuild/save cycle happens exactly once.
    const auto owned_by_this_machine = [owner_player_indices](std::uint8_t owner) {
        if (owner == guy::kNoOwner)
            return false;
        return std::find(owner_player_indices.begin(),
                         owner_player_indices.end(),
                         owner) != owner_player_indices.end();
    };
    const bool any_real_owner = std::any_of(
        owner_player_indices.begin(),
        owner_player_indices.end(),
        [](std::uint8_t owner) { return owner != guy::kNoOwner; });
    if (!any_real_owner)
        return;

    // This SaveData holds the owner's untouched pre-session roster (their real
    // save0), dense in [0, team_size). Rebuild it from the session outcome,
    // keyed by each brought character's original save slot (owner_save_slot):
    //   - a brought character that SURVIVED overlays its slot with its grown self
    //   - a brought character that DIED is DROPPED — death sticks across a win,
    //     matching solo update_guys (which keeps only living guys) — UNLESS
    //     keep_fallen_heroes is set, in which case the pre-merge roster entry
    //     is preserved (pre-level stats; the fallen hero's growth is lost)
    //   - a character that was never brought is preserved exactly
    // The result is re-densified, because SaveData::save dereferences
    // team_list[i] for i < team_size without a null check.
    std::array<const guy*, MAX_TEAM_SIZE> survived = {};
    std::array<bool, MAX_TEAM_SIZE> died = {};
    for (auto& uptr : oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->myguy == nullptr)
            continue;
        if (!owned_by_this_machine(ob->myguy->owner_player_index))
            continue;

        const unsigned int slot = ob->myguy->owner_save_slot;
        if (slot >= static_cast<unsigned int>(MAX_TEAM_SIZE))
            continue;

        if (ob->dead())
            died[slot] = true;
        else
            survived[slot] = ob->myguy;
    }

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> rebuilt;
    int rebuilt_size = 0;
    for (int slot = 0; slot < team_size && slot < MAX_TEAM_SIZE; ++slot)
    {
        std::unique_ptr<guy> entry;
        if (survived[static_cast<std::size_t>(slot)] != nullptr)
        {
            entry = std::make_unique<guy>(*survived[static_cast<std::size_t>(slot)]);
            const guy* disk = team_list[static_cast<std::size_t>(slot)].get();
            if (preserve_exp_level && disk != nullptr)
            {
                // #213 (versus arenas): the merged roster keeps the DISK
                // exp/level — the session's growth is not persisted and the
                // re-level (with its stat gains) never runs.
                entry->exp = disk->exp;
                entry->level = disk->level;
            }
            else
            {
                const std::uint32_t exp = entry->exp;
                entry->upgrade_to_level(
                    static_cast<short>(calculate_level(entry->exp)));
                entry->exp = exp;
            }
        }
        else if (died[static_cast<std::size_t>(slot)] && keep_fallen_heroes == 0)
        {
            continue; // brought and died -> drop so the death sticks
        }
        else
        {
            // not brought -> keep; brought-and-died with keep_fallen_heroes
            // set -> keep the pre-merge (pre-level) roster entry
            entry = std::move(team_list[static_cast<std::size_t>(slot)]);
            if (entry == nullptr)
                continue;
        }
        rebuilt[static_cast<std::size_t>(rebuilt_size++)] = std::move(entry);
    }

    for (auto& slot : team_list)
        slot.reset();
    team_size = static_cast<unsigned char>(rebuilt_size);
    for (int i = 0; i < rebuilt_size; ++i)
    {
        team_list[static_cast<std::size_t>(i)] =
            std::move(rebuilt[static_cast<std::size_t>(i)]);
    }
}


bool SaveData::save(const std::string& filename)
{
    last_io_error_ = SaveDataIoError::None;
	TRACE("save", "SaveData::save file=%s", filename.c_str());
    if (!is_safe_virtual_basename(filename))
    {
        LogError("Rejected unsafe save file name for save: {}\n", filename);
        last_io_error_ = SaveDataIoError::OpenWriteFailed;
        return false;
    }
	// The 2002 writer filled RESERVED with literal "GTLGTLGTL..." bytes and
	// labeled the array simply "for RESERVED." v14 writes real fields plus
	// zero-filled reserved bytes instead (see §3.1).
	std::array<char, 41> savedgame;
	std::fill_n(savedgame.data(), savedgame.size(), '\0');
	std::array<char, 41> temp_campaign;
	std::fill_n(temp_campaign.data(), temp_campaign.size(), '\0');

	std::array<char, 10> temptext = {'G', 'T', 'L'};
	std::uint8_t temp_version = 15;

	std::uint32_t newcash = totalcash;
	std::uint32_t newscore = totalscore;
	//  short numguys;
	std::int16_t listsize = 0;

	std::array<char, 12> guyname = {'J', 'O', 'E'};
	std::uint8_t temp_order = 0;
	char temp_family;
	std::int16_t temp_str = 0;
	std::int16_t temp_dex = 0;
	std::int16_t temp_con = 0;
	std::int16_t temp_short = 0;
	std::int16_t temp_arm = 0;
	std::int16_t temp_lev = 0;
	constexpr std::uint8_t legacy_numplayers =
	    kLegacyPlayerCountCompatibility;
	std::uint32_t temp_exp;
	std::int16_t temp_kills = 0;
	std::int32_t temp_level_kills;
	std::int32_t temp_td, temp_th, temp_ts;
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
	// This is now a legacy compatibility marker (always 1); the live runtime
	// count is session state and is never serialized.
	// 31-bytes RESERVED, reinterpreted by version 14+ as:
	//   8-bytes (Sint64) last-played unix seconds (offset 133)  // Version 14+
	//   23-bytes RESERVED (zero-filled)
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
	// 2*4 = 8 bytes RESERVED, reinterpreted by version 14+ as:
	//   1-byte deployed flag (0 = held back), guy offset +50  // Version 14+
	//   7-bytes RESERVED (zero-filled)
	// List of 500 (max scenarios) 1-byte scenario-level status  // Versions 1-7
	// 2-bytes Number of campaigns in list      // Version 8+
	// List of n campaigns                      // Version 8+
	//   40-bytes Campaign ID string
	//   2-bytes Current level in this campaign
	//   2-bytes Number of level indices in list
	//   List of n level indices
	//     2-bytes Level index
	// 2-bytes CTF team count                   // Version 10+
	// 2-bytes CTF capture limit (0 = map/default)   // Version 10+
	// 2-bytes CTF respawn ticks (0 = default)  // Version 10+
	// 2-bytes CTF strip-scenario-troops flag   // Version 11+
	// 2-bytes respawn mode (0 = off)           // Version 12+
	// 2-bytes generator rate (0 = default)     // Version 12+
	// 2-bytes keep-fallen-heroes flag (0 = permadeath)  // Version 12+
	// 2-bytes Tower best floor climbed         // Version 13+
	// 2-bytes Tower run seed, low 16 bits      // Version 13+
	// 2-bytes Tower run seed, high 16 bits     // Version 13+
	// 2-bytes Number of campaigns with scripted state  // Version 15+
	// List of n campaign-state blocks          // Version 15+
	//   40-bytes Campaign ID string
	//   2-bytes Number of key/value entries (sorted by key)
	//   List of n entries
	//     1-byte key length (1..32)
	//     N-bytes key ([a-z0-9_], no NUL, no padding)
	//     4-bytes (Sint32) value

	//strcpy(temp_filename, scen_directory);
	Log("Saving save: {}\n", filename);
	std::string temp_filename = std::format("{}.gtl", filename); // gladiator team list

	og::io::OgFilePtr outfile = og::io::og_open_write("save/", temp_filename.c_str());
	if (!outfile) // open for write
	{
		LogError("Failed to write team file: {}\n", filename);
        last_io_error_ = SaveDataIoError::OpenWriteFailed;
		return 0;
	}

#define WRITE_OR_FAIL(src, size, count) \
    do { \
        if(!og::io::og_write_exact(*outfile, (src), (size), (count))) { \
            LogError("Failed to write save file: {} ({} bytes x {})\n", filename, (int)(size), (int)(count)); \
            last_io_error_ = SaveDataIoError::WriteFailed; \
            return 0; \
        } \
    } while(0)

	// Write id header
	WRITE_OR_FAIL(temptext.data(), 3, 1);

	// Write version number
	WRITE_OR_FAIL(&temp_version, 1, 1);

	// Versions 7+ include a mark for registered or not
	temp_registered = 1;
	WRITE_OR_FAIL(&temp_registered, 2, 1);

	// Write the name
	snprintf(savedgame.data(), savedgame.size(), "%s", save_name.c_str());
	WRITE_OR_FAIL(savedgame.data(), 40, 1);

	// Write current campaign
    if (!is_safe_campaign_id(current_campaign))
    {
        LogError("Rejected unsafe current campaign id for save: {}\n",
                 current_campaign);
        last_io_error_ = SaveDataIoError::WriteFailed;
        return false;
    }
	Log("Saving campaign status: {}\n", current_campaign);
	snprintf(temp_campaign.data(), temp_campaign.size(), "%s", current_campaign.c_str());
	WRITE_OR_FAIL(temp_campaign.data(), 40, 1);

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

	WRITE_OR_FAIL(&legacy_numplayers, 1, 1);

	// Write the former 31-byte reserved area: v14+ stores the last-played
	// timestamp in the first 8 bytes (offset 133) and zero-fills the rest.
	// save() only serializes the field — company_autosave stamps it (§3.2).
	std::int64_t temp_last_played = last_played_unix_s;
	WRITE_OR_FAIL(&temp_last_played, 8, 1);
	std::array<char, 23> reserved_header{};
	WRITE_OR_FAIL(reserved_header.data(), 23, 1);

	// Okay, we've written header .. now dump the data ..
	for(int team_idx = 0; team_idx < team_size; team_idx++)
	{
	    guy* temp_guy = team_list[static_cast<std::size_t>(team_idx)].get();

        // Get temp values to be saved
        temp_order = static_cast<unsigned char>(Order::Living);
        temp_family= temp_guy->family;
        // Write name of current guy...
        std::fill_n(guyname.data(), 12, '\0');
        snprintf(guyname.data(), guyname.size(), "%s", temp_guy->name.c_str());
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
        WRITE_OR_FAIL(guyname.data(), 12, 1);
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
        // And the filler: v14+ stores the mission-deploy flag in the first
        // byte of the former 8-byte guy filler (offset +50), then zero-fills
        // the other 7.
        std::uint8_t temp_deployed = temp_guy->deployed ? 1 : 0;
        WRITE_OR_FAIL(&temp_deployed, 1, 1);
        std::array<char, 7> reserved_guy{};
        WRITE_OR_FAIL(reserved_guy.data(), 7, 1);
	}

	// Write the completed levels

	// Make sure our current level is saved
	auto cur = current_levels.find(current_campaign);
	if(cur != current_levels.end())
    {
        cur->second = scen_num;
    }
    else
    {
        current_levels.insert(std::make_pair(current_campaign, scen_num));
    }

	// Number of campaigns
	const std::size_t raw_campaign_count = completed_levels.size();
	if (raw_campaign_count > 32767)
	{
	    LogError("save_write_too_many_campaigns {}\n", raw_campaign_count);
	    last_io_error_ = SaveDataIoError::WriteFailed;
	    return false;
	}
	short num_campaigns = static_cast<short>(raw_campaign_count);
    WRITE_OR_FAIL(&num_campaigns, 2, 1);
	for(auto e = completed_levels.begin(); e != completed_levels.end(); e++)
    {
        if (!is_safe_campaign_id(e->first))
        {
            LogError("Rejected unsafe completed-level campaign id for save: {}\n",
                     e->first);
            last_io_error_ = SaveDataIoError::WriteFailed;
            return false;
        }
        // Campaign ID
        std::array<char, 41> campaign;
        std::fill_n(campaign.data(), campaign.size(), '\0');
        snprintf(campaign.data(), campaign.size(), "%s", e->first.c_str());
        WRITE_OR_FAIL(campaign.data(), 1, 40);

	        short index = 1;
	        auto g = current_levels.find(e->first);
	        if(g != current_levels.end())
	            index = static_cast<short>(g->second);
	        WRITE_OR_FAIL(&index, 2, 1);

	        // Number of levels
	        const std::size_t raw_level_count = e->second.size();
	        if (raw_level_count > 32767)
	        {
	            LogError("save_write_too_many_levels {}\n", raw_level_count);
	            last_io_error_ = SaveDataIoError::WriteFailed;
	            return false;
	        }
	        short num_levels = static_cast<short>(raw_level_count);
	        WRITE_OR_FAIL(&num_levels, 2, 1);
        for(auto f = e->second.begin(); f != e->second.end(); f++)
	        {
	            // Level index
	            index = static_cast<short>(*f);
	            WRITE_OR_FAIL(&index, 2, 1);
	        }
	    }

	// Versions 10+ append the CTF match settings
	std::int16_t temp_ctf_teams = ctf_team_count;
	std::int16_t temp_ctf_caps = ctf_capture_limit;
	std::int16_t temp_ctf_respawn = ctf_respawn_ticks;
	WRITE_OR_FAIL(&temp_ctf_teams, 2, 1);
	WRITE_OR_FAIL(&temp_ctf_caps, 2, 1);
	WRITE_OR_FAIL(&temp_ctf_respawn, 2, 1);

	// Versions 11+ append the CTF scenario-troops strip flag
	std::int16_t temp_ctf_strip = ctf_strip_scenario_troops;
	WRITE_OR_FAIL(&temp_ctf_strip, 2, 1);

	// Versions 12+ append the difficulty submenu settings
	std::int16_t temp_respawn_mode = respawn_mode;
	std::int16_t temp_generator_rate = generator_rate;
	std::int16_t temp_keep_fallen = keep_fallen_heroes;
	WRITE_OR_FAIL(&temp_respawn_mode, 2, 1);
	WRITE_OR_FAIL(&temp_generator_rate, 2, 1);
	WRITE_OR_FAIL(&temp_keep_fallen, 2, 1);

	// Versions 13+ append the Tower Climb fields (seed as 2 x int16 lo/hi)
	std::int16_t temp_tower_best = tower_best_floor;
	std::int16_t temp_tower_seed_lo = static_cast<std::int16_t>(
	    static_cast<std::uint16_t>(tower_run_seed & 0xFFFFu));
	std::int16_t temp_tower_seed_hi = static_cast<std::int16_t>(
	    static_cast<std::uint16_t>(tower_run_seed >> 16));
	WRITE_OR_FAIL(&temp_tower_best, 2, 1);
	WRITE_OR_FAIL(&temp_tower_seed_lo, 2, 1);
	WRITE_OR_FAIL(&temp_tower_seed_hi, 2, 1);

	// Versions 15+ append the per-campaign scripted state block. Entry
	// lists are kept sorted by key (campaign_state_set inserts in order),
	// so re-serialization is byte-identical. The bounds below can only be
	// exceeded by direct field manipulation (campaign_state_set is the
	// choke); refuse to write rather than emit a file the reader rejects.
	const std::size_t raw_state_campaigns = campaign_state.size();
	if (raw_state_campaigns >
	    static_cast<std::size_t>(kCampaignStateMaxCampaigns))
	{
	    LogError("save_write_campaign_state_too_many_campaigns {}\n",
	             raw_state_campaigns);
	    last_io_error_ = SaveDataIoError::WriteFailed;
	    return false;
	}
	std::int16_t num_state_campaigns =
	    static_cast<std::int16_t>(raw_state_campaigns);
	WRITE_OR_FAIL(&num_state_campaigns, 2, 1);
	for (const auto& state_block : campaign_state)
	{
	    if (!is_safe_campaign_id(state_block.first))
	    {
	        LogError("Rejected unsafe campaign-state campaign id for save: {}\n",
	                 state_block.first);
	        last_io_error_ = SaveDataIoError::WriteFailed;
	        return false;
	    }
	    if (state_block.second.size() >
	        static_cast<std::size_t>(kCampaignStateMaxEntries))
	    {
	        LogError("save_write_campaign_state_too_many_entries campaign={} count={}\n",
	                 state_block.first, state_block.second.size());
	        last_io_error_ = SaveDataIoError::WriteFailed;
	        return false;
	    }
	    std::array<char, 41> state_campaign;
	    std::fill_n(state_campaign.data(), state_campaign.size(), '\0');
	    snprintf(state_campaign.data(), state_campaign.size(), "%s",
	             state_block.first.c_str());
	    WRITE_OR_FAIL(state_campaign.data(), 1, 40);
	    std::int16_t num_state_entries =
	        static_cast<std::int16_t>(state_block.second.size());
	    WRITE_OR_FAIL(&num_state_entries, 2, 1);
	    for (const auto& state_entry : state_block.second)
	    {
	        if (!og::script::hooks::valid_campaign_var_name(state_entry.first))
	        {
	            LogError("Rejected invalid campaign-state key for save: {}\n",
	                     state_entry.first);
	            last_io_error_ = SaveDataIoError::WriteFailed;
	            return false;
	        }
	        const std::uint8_t key_len =
	            static_cast<std::uint8_t>(state_entry.first.size());
	        WRITE_OR_FAIL(&key_len, 1, 1);
	        WRITE_OR_FAIL(state_entry.first.data(), 1, key_len);
	        std::int32_t state_value = state_entry.second;
	        WRITE_OR_FAIL(&state_value, 4, 1);
	    }
	}

    // unique_ptr auto-closes outfile

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


bool SaveData::is_level_completed(int level_index) const
{
    auto e = completed_levels.find(current_campaign);
    // Campaign not found?  Then this level is not done.
    if(e == completed_levels.end())
        return false;

    // If the level is listed, then it is completed.
    auto f = e->second.find(level_index);
    return (f != e->second.end());
}

int SaveData::get_num_levels_completed(const std::string& campaign) const
{
    auto e = completed_levels.find(campaign);
    // Campaign not found?
    if(e == completed_levels.end())
        return 0;

    return static_cast<int>(e->second.size());
}

void SaveData::add_level_completed(const std::string& campaign, int level_index)
{
    auto e = completed_levels.find(campaign);

    // Campaign not found?  Add it in.
    if(e == completed_levels.end())
        e = completed_levels.insert(std::make_pair(campaign, std::set<int>())).first;

    // Add the completed level
    e->second.insert(level_index);
}

void SaveData::reset_campaign(const std::string& campaign)
{
    auto e = completed_levels.find(campaign);

    if(e != completed_levels.end())
        e->second.clear();

    // Campaign scripting (issue #206): a campaign RESET also forgets that
    // campaign's scripted decision book.
    campaign_state.erase(campaign);
}

std::int32_t SaveData::campaign_state_get(const std::string& campaign,
                                          const std::string& key) const
{
    const auto campaign_it = campaign_state.find(campaign);
    if (campaign_it == campaign_state.end())
        return 0;

    const auto& entries = campaign_it->second;
    const auto entry_it = std::lower_bound(
        entries.begin(), entries.end(), key,
        [](const std::pair<std::string, std::int32_t>& entry,
           const std::string& wanted) { return entry.first < wanted; });
    if (entry_it == entries.end() || entry_it->first != key)
        return 0;
    return entry_it->second;
}

bool SaveData::campaign_state_set(const std::string& campaign,
                                  const std::string& key, std::int32_t value)
{
    // The WRITE choke (docs/campaign-scripting-design.md "Persistent
    // campaign state"): every rejection happens BEFORE any mutation, so a
    // script bug can never brick the save file. Campaign ids longer than
    // the serialized 40-byte field would silently truncate on disk and
    // detach the state from its campaign on reload — refuse them here.
    if (!og::script::hooks::valid_campaign_var_name(key) ||
        campaign.size() > 40)
        return false;

    auto campaign_it = campaign_state.find(campaign);
    if (campaign_it == campaign_state.end())
    {
        if (campaign_state.size() >=
            static_cast<std::size_t>(kCampaignStateMaxCampaigns))
            return false;
        campaign_it =
            campaign_state
                .emplace(campaign,
                         std::vector<std::pair<std::string, std::int32_t>>())
                .first;
    }

    auto& entries = campaign_it->second;
    const auto entry_it = std::lower_bound(
        entries.begin(), entries.end(), key,
        [](const std::pair<std::string, std::int32_t>& entry,
           const std::string& wanted) { return entry.first < wanted; });
    if (entry_it != entries.end() && entry_it->first == key)
    {
        // Setting an existing key — 0 included — keeps the entry (the
        // documented deterministic rule; see save_data.h).
        entry_it->second = value;
        return true;
    }
    if (entries.size() >= static_cast<std::size_t>(kCampaignStateMaxEntries))
        return false;

    entries.insert(entry_it, std::make_pair(key, value));
    return true;
}
