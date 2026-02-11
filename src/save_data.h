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

#pragma once

#include "SDL.h"
#include <array>
#include <string>
#include <map>
#include <set>
#include <list>
#include <memory>

class guy;
class walker;

#define MAX_TEAM_SIZE 24 //max # of guys on a team

enum class SaveDataIoError
{
    None = 0,
    OpenReadFailed,
    OpenWriteFailed,
    ReadFailed,
    WriteFailed,
    InvalidHeader,
    UnsupportedVersion,
    CampaignLoadFailed
};

class SaveData
{
public:
    
    std::string save_name;
    std::string current_campaign;
    short scen_num;
    std::map<std::string, std::set<int> > completed_levels;
    std::map<std::string, int> current_levels;
    Uint32 score;
    Uint32 m_score[4];
    Uint32 totalcash;
    Uint32 m_totalcash[4];
    Uint32 totalscore;
    Uint32 m_totalscore[4];
    short my_team;
    // Guys used for training and stuff.  After a mission, the team is picked from the LevelData's oblist for saving.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;
    unsigned char team_size;
    unsigned char numplayers; //numviews
    short allied_mode;
    
    SaveData();
    ~SaveData();
    
    void reset();
    
    void update_guys(std::list<std::unique_ptr<walker>>& oblist);  // Copy team from the guys in an oblist
    bool load(const std::string& filename);
    bool save(const std::string& filename);
    SaveDataIoError load_with_error(const std::string& filename);
    SaveDataIoError save_with_error(const std::string& filename);
    SaveDataIoError last_io_error() const { return last_io_error_; }
    
    bool is_level_completed(int level_index) const;
    int get_num_levels_completed(const std::string& campaign) const;
    void add_level_completed(const std::string& campaign, int level_index);
    void reset_campaign(const std::string& campaign);

private:
    SaveDataIoError last_io_error_ = SaveDataIoError::None;
};
