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
#include <set>
#include <string>

// Forward declarations
enum class Order : unsigned char;
class walker;
class LevelData;
class obmap;

#include <openglad/data/pixie_data.h>
#include <openglad/data/smooth.h>

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

// GameWorld: owns entity lists and living_count.
//
// Phase 1a of the component architecture migration. Entity lists and
// living_count are moved here from LevelData. LevelData retains
// forwarding reference members so existing callers work unchanged.
//
// In later phases, spatial data, tick logic, RNG, and game state flags
// will also move here, eventually replacing both LevelData and GameWorld.
class GameWorld {
public:
    GameWorld();
    ~GameWorld();

    // Non-copyable, non-moveable (entity lists contain unique_ptrs)
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;
    GameWorld(GameWorld&&) = delete;
    GameWorld& operator=(GameWorld&&) = delete;

    // Entity storage (moved from LevelData)
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

    std::uint32_t tick_count_ = 0;
    og::sim::SimRandom rng_;

    // Clear all entity lists and reset living_count.
    // Note: this clears only entity storage. Hooks (e.g. stale view control
    // cleanup) and obmap cleanup remain on LevelData::delete_objects().
    void delete_objects();

    // Count living foes not friendly to the given walker.
    short remaining_foes(walker* myguy) const;

    // Entity creation — delegates to LevelData::myloader temporarily.
    // This circular delegation (GameWorld -> LevelData -> loader) is ugly
    // but temporary: Phase 4 eliminates sim_* pointer wiring, and Phase 6
    // replaces the loader path entirely with an entity_factory callback.
    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);

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
    void tick(og::sim::SimEventLog& events);

    // Grid/world lifecycle
    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void clear();

    // Temporary back-pointer to LevelData for loader delegation.
    // Set by LevelData's constructor; cleared by its destructor.
    void set_level_data(LevelData* ld) { level_data_ = ld; }

private:
    LevelData* level_data_ = nullptr;
    std::uint32_t level_tick_count_ = 0;
    int last_level_id_ = -1;
};

} // namespace og::gameplay
