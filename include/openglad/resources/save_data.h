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
#include <span>
#include <string>
#include <map>
#include <set>
#include <list>
#include <memory>

class guy;
class walker;

inline constexpr int MAX_TEAM_SIZE = 24; // max # of guys on a team
// Largest value accepted from GTL's retired player-count byte. The full
// reader and header scanner keep this legacy corruption check even though
// current sessions no longer adopt the value.
inline constexpr unsigned char kMaxLegacySavePlayers = 4;
// Preserve the public spelling used by older source integrations.
inline constexpr unsigned char kMaxSavePlayers = kMaxLegacySavePlayers;

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
    // Runtime-only local seat/view count. Company files retain one legacy
    // compatibility byte at this field's old offset, but load only validates
    // it and save always writes the canonical single-player marker.
    unsigned char numplayers; //numviews
    short allied_mode;
    // CTF match settings (only TYPE_CTF maps read them; 0 = map/default value).
    short ctf_team_count = 0; // 0 = Auto: every team the map authors
    short ctf_capture_limit = 0;
    short ctf_respawn_ticks = 0;
    short ctf_strip_scenario_troops = 0; // 0 = keep authored troops (classic)
    // Difficulty submenu settings (0 = legacy default behavior for all three).
    // 0 = off, 1 = heroes, 2 = everyone, 3 = Team 1 heroes only.
    short respawn_mode = 0;
    short generator_rate = 0;     // percent; 0 = default (100)
    short keep_fallen_heroes = 0; // 0 = permadeath on win (classic), 1 = keep
    // Cross-control (protocol v8, company-basecamp design §4.1): host-only
    // lobby setting; 0 = owner-locked networked control (default), 1 =
    // players may control other machines' characters. SESSION-ONLY — never
    // serialized to the GTL file (a lobby-negotiated match setting that is
    // meaningless outside a networked session), so save()/load() bytes are
    // untouched.
    short cross_control = 0;
    // Tower Climb persistence (GTL v13; docs/tower-triple-design.md D2/D6).
    // Floors climbed is DERIVED (scen_num - kTowerGateLevel), never stored as
    // a run counter; only the lifetime best and the current run's generation
    // seed persist. The seed is serialized as 2 x int16 (lo, hi).
    short tower_best_floor = 0;         // highest floor ever REACHED
    std::uint32_t tower_run_seed = 0;   // current run's generation seed
    // Company bookkeeping (GTL v14; docs/company-basecamp-design.md §3.1).
    // Wall-clock unix seconds when this company was last played; serialized
    // raw (host-endian, per format precedent) into the first 8 bytes of the
    // header's former reserved block at offset 133. NOT cleared by reset()
    // and stamped ONLY by company_autosave — save() just serializes it, so
    // SaveData::save() itself stays deterministic (§3.2).
    std::int64_t last_played_unix_s = 0;

    SaveData();
    ~SaveData();
    
    void reset();
    
    void update_guys(const std::list<std::unique_ptr<walker>>& oblist);  // Copy team from the guys in an oblist (dead heroes dropped unless keep_fallen_heroes)
    // Networked "as if played alone" save: overlay only the characters owned by
    // owner_player_index (matched via guy::owner_player_index) back into their
    // own save slots (guy::owner_save_slot), updating progress while leaving
    // every other slot — other players' characters and this player's
    // not-brought characters — untouched. Keeps the roster dense; campaign and
    // score fields are intentionally left as-is (only character growth persists).
    void merge_owned_guys_from(const std::list<std::unique_ptr<walker>>& oblist,
                               std::uint8_t owner_player_index);
    // Multi-seat variant: overlay the characters owned by ANY of the given
    // player indices in ONE load/merge/save cycle (a machine with N local
    // seats owns N players' characters). Entries equal to guy::kNoOwner are
    // ignored; an effectively empty list is a no-op.
    void merge_owned_guys_from(const std::list<std::unique_ptr<walker>>& oblist,
                               std::span<const std::uint8_t> owner_player_indices);
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
