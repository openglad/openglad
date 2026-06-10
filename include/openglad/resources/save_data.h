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

#include <cstdint>
#include <array>
#include <string>
#include <map>
#include <set>
#include <list>
#include <memory>

class guy;
class walker;

inline constexpr int MAX_TEAM_SIZE = 24; // max # of guys on a team

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
    std::uint32_t score;
    std::uint32_t m_score[4];
    std::uint32_t totalcash;
    std::uint32_t m_totalcash[4];
    std::uint32_t totalscore;
    std::uint32_t m_totalscore[4];
    short my_team;
    // Guys used for training and stuff.  After a mission, the team is picked from the LevelRuntimeData's oblist for saving.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;
    unsigned char team_size;
    unsigned char numplayers; //numviews
    short allied_mode;
    // CTF match settings (only TYPE_CTF maps read them; 0 = map/default value).
    short ctf_team_count = 2;
    short ctf_capture_limit = 0;
    short ctf_respawn_ticks = 0;

    SaveData();
    ~SaveData();
    
    void reset();
    
    void update_guys(const std::list<std::unique_ptr<walker>>& oblist);  // Copy team from the guys in an oblist
    // Networked "as if played alone" save: overlay only the characters owned by
    // owner_player_index (matched via guy::owner_player_index) back into their
    // own save slots (guy::owner_save_slot), updating progress while leaving
    // every other slot — other players' characters and this player's
    // not-brought characters — untouched. Keeps the roster dense; campaign and
    // score fields are intentionally left as-is (only character growth persists).
    void merge_owned_guys_from(const std::list<std::unique_ptr<walker>>& oblist,
                               std::uint8_t owner_player_index);
    bool load(const std::string& filename);
    bool save(const std::string& filename);
    [[nodiscard]] SaveDataIoError load_with_error(const std::string& filename);
    [[nodiscard]] SaveDataIoError save_with_error(const std::string& filename);
    [[nodiscard]] SaveDataIoError last_io_error() const { return last_io_error_; }
    
    bool is_level_completed(int level_index) const;
    int get_num_levels_completed(const std::string& campaign) const;
    void add_level_completed(const std::string& campaign, int level_index);
    void reset_campaign(const std::string& campaign);

private:
    SaveDataIoError last_io_error_ = SaveDataIoError::None;
};
