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
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <thread>

// Forward declarations
enum class Order : unsigned char;
class walker;
class obmap;
struct SaveData;
class IRandom;
class cfg_store;

#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>

namespace og::sim {
class SimEventLog;

// Deterministic LCG RNG used by simulation logic.
class SimRandom final {
public:
    explicit SimRandom(std::uint32_t seed = 0) : state_(seed) {}

    std::uint32_t next(std::uint32_t max_exclusive) {
        if (max_exclusive == 0) return 0;
        state_ = state_ * 1103515245u + 12345u;
        return (state_ >> 16) % max_exclusive;
    }

    std::uint32_t state_;
};

#ifdef TESTING
extern std::int32_t g_test_level_tick_limit_override;
#endif
} // namespace og::sim

namespace og::gameplay {

// GameWorld: owns entity lists, spatial data, level metadata, and game logic.
//
// Central game state container for a single level. Owns entity storage,
// collision grid, tick logic, and entity factory callbacks.
class GameWorld {
public:
    static const char TYPE_CAN_EXIT_WHENEVER = 0x1;  // Can exit without defeating all enemies
    static const char TYPE_MUST_DESTROY_GENERATORS = 0x2;  // Must destroy generators to exit
    static const char TYPE_MUST_PROTECT_NAMED_NPCS = 0x4;  // Must protect named NPCs or else you lose

    GameWorld();
    ~GameWorld();

    // Non-copyable, non-moveable (entity lists contain unique_ptrs)
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;
    GameWorld(GameWorld&&) = delete;
    GameWorld& operator=(GameWorld&&) = delete;

    // Entity storage (moved from LevelData).
    // Threading contract: entity lists are main-thread-owned. Simulation tick
    // and rendering are phase-separated on that thread; no concurrent access.
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> weaplist;
    std::list<std::unique_ptr<walker>> fxlist;
    std::list<std::unique_ptr<walker>> dead_list;
    int living_count = 0;  // Count of Order::Living entities only (was numobs)

    // Spatial data (moved from LevelData in Phase 1b)
    std::unique_ptr<obmap> myobmap;
    PixieData grid;
    smoother mysmoother;
    std::int32_t pixmaxx = 0;
    std::int32_t pixmaxy = 0;

    // Level metadata (moved from LevelData in Phase 1b)
    int id = 1;
    char type = 0;
    std::string title = "New Level";
    short par_value = 1;
    short time_bonus_limit = 4000;
    short difficulty = 100;
    short my_team = 0;
    unsigned char allied_mode = 0;
    short current_scenario = 0;
    std::set<int> completed_levels;
    std::uint32_t m_score[4] = {};

    // Game state flags (moved from screen in Phase 3)
    short level_done = 0;
    bool game_ended = false;
    short next_level = -1;
    short ending = 0;
    std::int32_t enemy_freeze = 0;
    signed char timer_wait = 6;
    char end = 0;
    bool retry = false;
    float control_hp = 0;
    bool withdraw_requested = false;
    bool create_hit_effects = true;

    std::uint32_t tick_count_ = 0;
    og::sim::SimRandom rng_;
    std::function<std::unique_ptr<walker>(Order, int)> entity_factory;
    std::function<walker*(walker*, Order, int)> entity_configure;
    std::function<void(walker*, Order, int)> entity_derived_stats;
    std::function<const PixieData*(Order, int)> entity_graphics;

    // Hook called by delete_objects() before entity lists are cleared.
    // Used by the platform layer (screen) to clear stale view control pointers.
    std::function<void(GameWorld*)> on_pre_delete_objects;

    // Transitional sim-context bridge used by legacy callers/tests.
    void set_sim_context(SaveData* save, std::int32_t* enemy_freeze,
                         og::sim::SimEventLog* events, IRandom* rng,
                         cfg_store* config);

    // Clear all entity lists, reset living_count, invoke on_pre_delete_objects
    // hook, and clean up obmap.
    void delete_objects();

    // Count living foes not friendly to the given walker.
    short remaining_foes(walker* myguy) const;

    // Entity creation — delegates to platform-wired factory callback.
    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);
    short remove_ob(walker* ob);
    walker* configure_entity(walker* ob, Order order, std::int32_t family);
    void apply_derived_stats(walker* ob, Order order, std::int32_t family);
    const PixieData* lookup_entity_graphics(Order order, std::int32_t family) const;

    // Collision/passability queries
    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);
    bool query_passable(std::int32_t x, std::int32_t y, walker* ob)
    {
        return query_passable(static_cast<float>(x), static_cast<float>(y), ob);
    }
    bool query_object_passable(std::int32_t x, std::int32_t y, walker* ob)
    {
        return query_object_passable(static_cast<float>(x), static_cast<float>(y), ob);
    }
    bool query_grid_passable(std::int32_t x, std::int32_t y, walker* ob)
    {
        return query_grid_passable(static_cast<float>(x), static_cast<float>(y), ob);
    }

    // Entity search
    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    walker* find_nearest_blood(walker* who);
    walker* find_nearest_player(walker* ob);
    std::list<walker*> find_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                     std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                          std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                                 std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                             std::int32_t range, std::int32_t* howmany, walker* ob);

    // Simulation tick (migrated from GameWorld in Phase 2).
    void tick();

    // Grid/world lifecycle
    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void clear();

private:
    void clear_single_backlink(walker* source, walker* victim);
    void clear_backlinks_to(walker* victim);
#ifndef NDEBUG
    void assert_entity_thread_() const;
    std::thread::id entity_thread_owner_{};
#endif
    std::uint32_t level_tick_count_ = 0;
    int last_level_id_ = -1;
};

} // namespace og::gameplay
