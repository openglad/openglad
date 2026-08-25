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

// Shared campaign-state bounds + key charset (kCampaignVarNameMax,
// valid_campaign_var_name): the write choke campaign_state_set and the
// GTL v15 load rejection enforce the same rules as og.campaign_state_set.
#include <openglad/gameplay/script/campaign_hooks.h>

#include <cstdint>
#include <array>
#include <span>
#include <string>
#include <map>
#include <set>
#include <list>
#include <memory>
#include <utility>
#include <vector>

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
    // Match settings (scripted maps read them via og.match_setting; 0 = map/default). Storage names keep the ctf_ prefix: renaming costs a save format bump.
    short ctf_team_count = 0; // 0 = Auto: every team the map authors
    short ctf_capture_limit = 0;
    short ctf_respawn_ticks = 0;
    short ctf_strip_scenario_troops = 0; // 0 = keep authored troops (classic); 2 = own; 3 = Fair (og::sim::kTroopsMatched)
    // Match time limit in SIM TICKS (12/s); 0 = the map's own value (#241).
    // NOT the .glad level field of the same name: that one is the par/time
    // BONUS clock and lands in GameWorld::time_bonus_limit
    // (level_file_io.cpp, "2-bytes time limit" v9+). This is the
    // lobby-negotiated override every scripted mode resolves against its
    // manifest row. Persisted since GTL v17; sanitize/clamp twins bound a
    // non-zero request to [720, 21600] ticks (1 min .. 30 min).
    short time_limit = 0;
    // Per-team bot squad preset ordinal and bot level (LINEUP §3.1, GTL v18).
    // Index is the team (0..3). 0 = AUTO on both (the map's own value), so an
    // all-zero pair is today's behaviour byte for byte. bot_squad: 1 = NONE,
    // 2.. = the campaign's preset ordinal. bot_level: 1..9 = that level.
    std::array<short, 4> bot_squad = {};
    std::array<short, 4> bot_level = {};
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
    // Infinite gold (protocol v11): host-only lobby setting; 0 = the classic
    // economy, 1 = hire/train purchases are FREE. The wallet is never
    // inflated and never written — turning the setting off restores exactly
    // the pre-toggle economy. SESSION-ONLY — never serialized to the GTL
    // file (like cross_control), which also keeps every company autosave
    // from baking a cheat balance into the player's file.
    short infinite_gold = 0;
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
    // Campaign scripting persistent state (GTL v15; issue #206,
    // docs/campaign-scripting-design.md "Persistent campaign state").
    // Per-campaign named int32 decisions, keyed by campaign id. Each
    // campaign's entry list is kept SORTED BY KEY inside the vector —
    // insertion order would not survive a save/load/save cycle, sorted
    // storage makes re-serialization byte-identical. All writes funnel
    // through campaign_state_set (the bounds choke below).
    std::map<std::string, std::vector<std::pair<std::string, std::int32_t>>>
        campaign_state;

    // Campaign scripted-state bounds, matching the existing
    // campaign-list bound (128) on both axes.
    static constexpr int kCampaignStateMaxCampaigns = 128;
    static constexpr int kCampaignStateMaxEntries = 128;

    // Replay excursion arm (#207; docs/camp-controls-design.md "Replay").
    // TRANSIENT session state, never serialized (no GTL change): reset()
    // and load() clear the pair, and the launch sites that round-trip a
    // session save through disk re-carry it explicitly (game.cpp,
    // local_transport_shadow, copy_headless_server_save_data — the
    // documented dropped-field pattern). replay_level == 0 means unarmed;
    // replay_origin is the campaign cursor as it stood when the arm was
    // set, the position a finished excursion restores.
    short replay_level = 0;
    short replay_origin = 0;

    SaveData();
    ~SaveData();
    
    void reset();
    
    // Copy team from the guys in an oblist (dead heroes dropped unless
    // keep_fallen_heroes). preserve_exp_level (#213, versus arenas): a
    // rebuilt entry keeps the PRIOR roster entry's exp and level (matched
    // by guy::id; the re-level-from-exp step is skipped, so level-up stat
    // gains never apply either) — cash/score folding is untouched. Guys
    // with no prior entry (mid-level recruits) re-level normally.
    void update_guys(const std::list<std::unique_ptr<walker>>& oblist,
                     bool preserve_exp_level = false);
    // Networked "as if played alone" save: overlay only the characters owned by
    // owner_player_index (matched via guy::owner_player_index) back into their
    // own save slots (guy::owner_save_slot), updating progress while leaving
    // every other slot — other players' characters and this player's
    // not-brought characters — untouched. Keeps the roster dense; campaign and
    // score fields are intentionally left as-is (only character growth persists).
    void merge_owned_guys_from(const std::list<std::unique_ptr<walker>>& oblist,
                               std::uint8_t owner_player_index,
                               bool preserve_exp_level = false);
    // Multi-seat variant: overlay the characters owned by ANY of the given
    // player indices in ONE load/merge/save cycle (a machine with N local
    // seats owns N players' characters). Entries equal to guy::kNoOwner are
    // ignored; an effectively empty list is a no-op. preserve_exp_level
    // (#213): a surviving overlay keeps the DISK slot's exp/level instead
    // of re-leveling from the session exp.
    void merge_owned_guys_from(const std::list<std::unique_ptr<walker>>& oblist,
                               std::span<const std::uint8_t> owner_player_indices,
                               bool preserve_exp_level = false);
    bool load(const std::string& filename);
    bool save(const std::string& filename);
    [[nodiscard]] SaveDataIoError load_with_error(const std::string& filename);
    [[nodiscard]] SaveDataIoError save_with_error(const std::string& filename);
    [[nodiscard]] SaveDataIoError last_io_error() const { return last_io_error_; }
    
    // Arm a replay of `level` (#207): remembers the current cursor as
    // replay_origin (a re-arm before the first excursion resolves keeps the
    // FIRST origin — scen_num already points into the excursion) and moves
    // scen_num onto the level so go_menu, the lobby publish and joiner
    // mounts work unchanged. The win fold restores the origin and clears
    // the arm; any other end restores at picker re-entry.
    void arm_replay(short level);
    void clear_replay_arm();
    // True when the arm covers `level`. The arm only means anything for
    // the level it names, and every plain cursor-set choke (PROGRESS
    // VISIT/GO, SET LEVEL, the camp's plain level rows, the terminal Set
    // Level tails) and every campaign switch calls clear_replay_arm() —
    // abandoning the excursion — before writing the cursor, so a stale arm
    // never skips a purge and an origin never leaks across campaigns. The
    // level check here is the backstop for writes outside those chokes.
    [[nodiscard]] bool replay_armed_for(int level) const;

    bool is_level_completed(int level_index) const;
    int get_num_levels_completed(const std::string& campaign) const;
    void add_level_completed(const std::string& campaign, int level_index);
    void reset_campaign(const std::string& campaign);

    // Scripted campaign state (GTL v15). get answers 0 when the campaign
    // or key is absent. set is the WRITE choke (og.campaign_state_set
    // raises when it answers false): it returns false WITHOUT mutating
    // when the key fails og::script::hooks::valid_campaign_var_name, when
    // the campaign already holds kCampaignStateMaxEntries entries and the
    // key is new, or when kCampaignStateMaxCampaigns campaigns hold state
    // and this campaign is new. Setting an existing key to 0 KEEPS the
    // entry — the simplest deterministic rule: an entry's presence never
    // depends on its value history.
    std::int32_t campaign_state_get(const std::string& campaign,
                                    const std::string& key) const;
    bool campaign_state_set(const std::string& campaign,
                            const std::string& key, std::int32_t value);

private:
    SaveDataIoError last_io_error_ = SaveDataIoError::None;
};
