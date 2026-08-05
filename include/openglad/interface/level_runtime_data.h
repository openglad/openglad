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
#include <list>
#include <memory>
#include <string>
#include <utility>

// Forward-declare Order enum class (defined in base.h)
enum class Order : unsigned char;

class screen;
class statistics;
class SaveData;
class IRandom;
class cfg_store;
struct LevelDataHooks;
namespace og::sim { class SimEventLog; }

#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/core/pixdefs.h>

class CampaignData
{
public:
    enum class IoError
    {
        None = 0,
        PackageMountFailed,
        OpenReadFailed,
        OpenWriteFailed,
        PackageUnpackFailed,
        PackageRepackFailed,
        ParseFailed
    };

    std::string id;
    std::string title;
    float rating;
    std::string version;
    std::string authors;
    std::string contributors;
    std::list<std::string> description;
    // campaign.yaml identity axes (see campaign_yaml.h). Carried through the
    // editor's load/save round-trip so repacking a campaign no longer drops
    // `mode:` (the documented Tier-B gap) or `matchup:`. Empty = absent.
    std::string mode;
    std::string matchup;
    int suggested_power;
    int first_level;

    int num_levels;

    PixieData icondata;

    CampaignData(const std::string& campaign_id);
    ~CampaignData();

    bool load();
    bool save();
    bool save_as(const std::string& new_id);
    [[nodiscard]] IoError load_with_error();
    [[nodiscard]] IoError save_with_error();
    [[nodiscard]] IoError save_as_with_error(const std::string& new_id);
    [[nodiscard]] IoError last_io_error() const { return last_io_error_; }

    std::string get_description_line(int i);
    std::string getDescriptionLine(int i);

private:
    IoError last_io_error_ = IoError::None;
};



class LevelRuntimeData
{
public:
    using WalkerList = std::list<std::unique_ptr<walker>>;

    class LivingCountForwarder
    {
    public:
        explicit LivingCountForwarder(LevelRuntimeData* owner)
            : owner_(owner)
        {
        }

        LivingCountForwarder(const LivingCountForwarder&) = delete;
        LivingCountForwarder& operator=(const LivingCountForwarder&) = delete;
        LivingCountForwarder(LivingCountForwarder&&) = delete;
        LivingCountForwarder& operator=(LivingCountForwarder&&) = delete;

        operator int&() { return owner_->world().living_count; }
        operator const int&() const { return owner_->world().living_count; }

        LivingCountForwarder& operator=(int value)
        {
            owner_->world().living_count = value;
            return *this;
        }

        LivingCountForwarder& operator+=(int value)
        {
            owner_->world().living_count += value;
            return *this;
        }

        LivingCountForwarder& operator-=(int value)
        {
            owner_->world().living_count -= value;
            return *this;
        }

        LivingCountForwarder& operator++()
        {
            ++owner_->world().living_count;
            return *this;
        }

        int operator++(int)
        {
            const int old_value = owner_->world().living_count;
            ++owner_->world().living_count;
            return old_value;
        }

        LivingCountForwarder& operator--()
        {
            --owner_->world().living_count;
            return *this;
        }

        int operator--(int)
        {
            const int old_value = owner_->world().living_count;
            --owner_->world().living_count;
            return old_value;
        }

    private:
        LevelRuntimeData* owner_ = nullptr;
    };

    class LevelDoneForwarder
    {
    public:
        explicit LevelDoneForwarder(LevelRuntimeData* owner)
            : owner_(owner)
        {
        }

        LevelDoneForwarder(const LevelDoneForwarder&) = delete;
        LevelDoneForwarder& operator=(const LevelDoneForwarder&) = delete;
        LevelDoneForwarder(LevelDoneForwarder&&) = delete;
        LevelDoneForwarder& operator=(LevelDoneForwarder&&) = delete;

        operator short&() { return owner_->world().level_done; }
        operator const short&() const { return owner_->world().level_done; }

        LevelDoneForwarder& operator=(short value)
        {
            owner_->world().level_done = value;
            return *this;
        }

    private:
        LevelRuntimeData* owner_ = nullptr;
    };

    enum class IoError
    {
        None = 0,
        OpenReadFailed,
        OpenWriteFailed,
        InvalidHeader,
        ParseFailed,
        UnsupportedVersion,
        SerializeFailed
    };

    static constexpr char TYPE_CAN_EXIT_WHENEVER = 0x1;  // Can exit without defeating all enemies
    static constexpr char TYPE_MUST_DESTROY_GENERATORS = 0x2;  // Must destroy generators to exit
    static constexpr char TYPE_MUST_PROTECT_NAMED_NPCS = 0x4;  // Must protect named NPCs or else you lose

    std::string grid_file;
    LevelDoneForwarder level_done;

    LivingCountForwarder numobs;

    std::list<std::string> description;

    // Provenance mark from the .fss header (SCEN_TYPE_GENERATED): the scen
    // was emitted by a campaign generator, so hand edits will be
    // overwritten on the next regeneration. Loaded from and saved back to
    // LevelFileMetadata::generated; the level editor warns on open when
    // set. Never sim state.
    bool generated = false;

    LevelRuntimeData(int level_id);
    LevelRuntimeData(int level_id, const LevelDataHooks* hooks);
    LevelRuntimeData(int level_id, bool headless);  // Headless constructor (no tile graphics)
    LevelRuntimeData(int level_id, bool headless, const LevelDataHooks* hooks);
    LevelRuntimeData(int level_id, bool headless, const LevelDataHooks* hooks,
                     og::gameplay::ILevelVisuals* visuals);
    ~LevelRuntimeData();

    bool load();
    bool save();
    [[nodiscard]] IoError save_with_error();
    [[nodiscard]] IoError load_with_error();
    [[nodiscard]] IoError last_io_error() const { return last_io_error_; }

    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);
    short remove_ob(walker  *ob);

    // Collision/passability queries (moved from screen)
    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);
    bool query_passable(std::int32_t x, std::int32_t y, walker* ob) { return query_passable(static_cast<float>(x), static_cast<float>(y), ob); }
    bool query_object_passable(std::int32_t x, std::int32_t y, walker* ob) { return query_object_passable(static_cast<float>(x), static_cast<float>(y), ob); }
    bool query_grid_passable(std::int32_t x, std::int32_t y, walker* ob) { return query_grid_passable(static_cast<float>(x), static_cast<float>(y), ob); }

    // Entity search (moved from screen)
    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    walker* find_nearest_blood(walker* who);
    walker* find_nearest_player(walker* ob);
    std::list<walker*> find_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foes_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foe_weapons_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_friends_in_range(const std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);

    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void delete_objects();
    void clear();

    void set_draw_pos(std::int32_t new_topx, std::int32_t new_topy);
    void add_draw_pos(std::int32_t dx, std::int32_t dy);
    void draw(screen* scr);

    std::string get_description_line(int i) const;
    bool is_headless() const { return headless_; }
    GameWorld& world() { return *world_; }
    const GameWorld& world() const { return *world_; }
    LevelVisuals& level_visuals() { return *static_cast<LevelVisuals*>(level_visuals_); }
    const LevelVisuals& level_visuals() const { return *static_cast<const LevelVisuals*>(level_visuals_); }
    void attach_world(GameWorld* world);

    // Legacy transitional hook kept for call-site compatibility. Snapshot
    // application still needs a per-world gameplay context, so this stores the
    // world/save/events/config bindings used to rebuild one on demand.
    void set_sim_context(SaveData* save, std::int32_t* enemy_freeze,
                         og::sim::SimEventLog* events, IRandom* rng,
                         cfg_store* config);

private:
    IoError last_io_error_ = IoError::None;
    bool headless_ = false;  // When true, skip render component creation
    const LevelDataHooks* hooks_ = nullptr;
    SaveData* sim_context_save_ = nullptr;
    og::sim::SimEventLog* sim_context_events_ = nullptr;
    cfg_store* sim_context_config_ = nullptr;
    GameWorld owned_world_;
    GameWorld* world_ = nullptr;
    LevelVisuals owned_level_visuals_;
    og::gameplay::ILevelVisuals* level_visuals_ = nullptr;
};

// Read a scenario title from a .fss file. Returns "none" on failure.
std::string get_scenario_title(const char* filename);

// Count living foes not friendly to the given walker.
short remaining_foes(LevelRuntimeData& level, walker* myguy);
