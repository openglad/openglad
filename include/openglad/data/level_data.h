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
class LevelRender;
class loader;
class statistics;
class SaveData;
class IRandom;
class cfg_store;
struct LevelDataHooks;
namespace og::sim { class SimEventLog; }

#include <openglad/data/pixie_data.h>
#include <openglad/entities/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/legacy/pixdefs.h>

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



class LevelData
{
public:
    using WalkerList = std::list<std::unique_ptr<walker>>;

    class WalkerListForwarder
    {
    public:
        enum class Kind
        {
            Objects,
            Effects,
            Weapons,
            Dead
        };

        WalkerListForwarder(LevelData* owner, Kind kind)
            : owner_(owner), kind_(kind)
        {
        }

        WalkerListForwarder(const WalkerListForwarder&) = delete;
        WalkerListForwarder& operator=(const WalkerListForwarder&) = delete;
        WalkerListForwarder(WalkerListForwarder&&) = delete;
        WalkerListForwarder& operator=(WalkerListForwarder&&) = delete;

        operator WalkerList&() { return list(); }
        operator const WalkerList&() const { return list(); }

        WalkerList& list()
        {
            switch (kind_)
            {
                case Kind::Objects: return owner_->world().oblist;
                case Kind::Effects: return owner_->world().fxlist;
                case Kind::Weapons: return owner_->world().weaplist;
                case Kind::Dead: return owner_->world().dead_list;
            }
            return owner_->world().oblist;
        }

        const WalkerList& list() const
        {
            switch (kind_)
            {
                case Kind::Objects: return owner_->world().oblist;
                case Kind::Effects: return owner_->world().fxlist;
                case Kind::Weapons: return owner_->world().weaplist;
                case Kind::Dead: return owner_->world().dead_list;
            }
            return owner_->world().oblist;
        }

        auto begin() { return list().begin(); }
        auto begin() const { return list().begin(); }
        auto end() { return list().end(); }
        auto end() const { return list().end(); }
        auto rbegin() { return list().rbegin(); }
        auto rbegin() const { return list().rbegin(); }
        auto rend() { return list().rend(); }
        auto rend() const { return list().rend(); }

        bool empty() const { return list().empty(); }
        std::size_t size() const { return list().size(); }
        void clear() { list().clear(); }

        auto front() -> WalkerList::reference { return list().front(); }
        auto front() const -> WalkerList::const_reference { return list().front(); }
        auto back() -> WalkerList::reference { return list().back(); }
        auto back() const -> WalkerList::const_reference { return list().back(); }

        template <typename T>
        void push_back(T&& value)
        {
            list().push_back(std::forward<T>(value));
        }

        template <typename... Args>
        auto emplace_back(Args&&... args)
        {
            return list().emplace_back(std::forward<Args>(args)...);
        }

        void pop_back() { list().pop_back(); }

        template <typename... Args>
        auto erase(Args&&... args)
        {
            return list().erase(std::forward<Args>(args)...);
        }

        template <typename... Args>
        void splice(Args&&... args)
        {
            list().splice(std::forward<Args>(args)...);
        }

    private:
        LevelData* owner_ = nullptr;
        Kind kind_ = Kind::Objects;
    };

    class LivingCountForwarder
    {
    public:
        explicit LivingCountForwarder(LevelData* owner)
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
        LevelData* owner_ = nullptr;
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

    static const char TYPE_CAN_EXIT_WHENEVER = 0x1;  // Can exit without defeating all enemies
    static const char TYPE_MUST_DESTROY_GENERATORS = 0x2;  // Must destroy generators to exit
    static const char TYPE_MUST_PROTECT_NAMED_NPCS = 0x4;  // Must protect named NPCs or else you lose

    std::string grid_file;
    short level_done = 0;  // Set by sim tick: 0=foes remain, 1=all foes dead+exit, 2=no foes

    std::unique_ptr<loader> myloader;
    LivingCountForwarder numobs;
    WalkerListForwarder oblist;
    WalkerListForwarder fxlist;  // fx--explosions, etc.
    WalkerListForwarder weaplist;  // weapons
    // Keep a list of dead guys so weapons can still have valid owners
    WalkerListForwarder dead_list;

    std::list<std::string> description;

    // Drawing details
    PixieData pixdata[PIX_MAX];
    std::unique_ptr<LevelRender> renderer_;  // Tile rendering (null for headless)
    std::int32_t topx, topy;

    LevelData(int level_id);
    LevelData(int level_id, const LevelDataHooks* hooks);
    LevelData(int level_id, bool headless);  // Headless constructor (no tile graphics)
    LevelData(int level_id, bool headless, const LevelDataHooks* hooks);
    ~LevelData();

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
    std::list<walker*> find_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, std::int32_t range, std::int32_t* howmany, walker* ob);

    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void delete_objects();
    void clear();

    void set_draw_pos(std::int32_t new_topx, std::int32_t new_topy);
    void add_draw_pos(std::int32_t dx, std::int32_t dy);
    void draw(screen* scr);

    std::string get_description_line(int i);
    bool is_headless() const { return headless_; }
    GameWorld& world() { return *world_; }
    const GameWorld& world() const { return *world_; }
    void attach_world(GameWorld* world);
    void wire_entity(walker* w);  // Wire all stored sim context onto an entity.
    void wire_spawned_entity(walker* w); // Wire base context + transitional screen hooks.

    // Set sim context that will be auto-wired onto newly created entities.
    void set_sim_context(SaveData* save, std::int32_t* enemy_freeze,
                         og::sim::SimEventLog* events, IRandom* rng,
                         cfg_store* config);

private:
    IoError last_io_error_ = IoError::None;
    bool headless_ = false;  // When true, skip render component creation
    const LevelDataHooks* hooks_ = nullptr;
    GameWorld owned_world_;
    GameWorld* world_ = nullptr;

    // Sim context pointers for wiring newly created entities.
    SaveData*              sim_ctx_save_ = nullptr;
    std::int32_t*          sim_ctx_enemy_freeze_ = nullptr;
    og::sim::SimEventLog*  sim_ctx_events_ = nullptr;
    IRandom*               sim_ctx_rng_ = nullptr;
    cfg_store*             sim_ctx_config_ = nullptr;
};

// Read a scenario title from a .fss file. Returns "none" on failure.
std::string get_scenario_title(const char* filename);

// Count living foes not friendly to the given walker.
short remaining_foes(LevelData& level, walker* myguy);
