/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>

#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/irandom.h>

// Forward-declare Order enum class (defined in base.h)
enum class Order : unsigned char;

class obmap;
class walker;
namespace og::sim { class SimEventLog; }

namespace og::sim {

// Simple LCG random number generator.
// Given the same seed, produces the same deterministic sequence.
class SimRandom final : public IRandom {
public:
    explicit SimRandom(std::uint32_t seed = 0) : state_(seed) {}

    std::uint32_t next(std::uint32_t max_exclusive) override {
        if (max_exclusive == 0) return 0;
        // LCG: same constants as glibc.
        state_ = state_ * 1103515245u + 12345u;
        return (state_ >> 16) % max_exclusive;
    }

    std::uint32_t state_;
};

#ifdef TESTING
extern std::int32_t g_test_level_tick_limit_override;
#endif

} // namespace og::sim

// Phase 1a shell for gameplay-owned entity lists.
// LevelRuntimeData temporarily forwards to this object while loader/render data
// remains on LevelRuntimeData.
class GameWorld
{
public:
    static constexpr char TYPE_CAN_EXIT_WHENEVER = 0x1;
    static constexpr char TYPE_MUST_DESTROY_GENERATORS = 0x2;
    static constexpr char TYPE_MUST_PROTECT_NAMED_NPCS = 0x4;

    explicit GameWorld(std::uint32_t seed = 0);
    ~GameWorld();

    // Level metadata
    int id = 0;
    std::string title = "New Level";
    char type = 0;
    short par_value = 1;
    short time_bonus_limit = 4000;
    short difficulty = 100;

    int living_count = 0;
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> fxlist;
    std::list<std::unique_ptr<walker>> weaplist;
    std::list<std::unique_ptr<walker>> dead_list;

    // Spatial data
    std::unique_ptr<obmap> myobmap;
    PixieData grid;
    smoother mysmoother;
    std::int32_t pixmaxx = 0;
    std::int32_t pixmaxy = 0;

    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);
    short remove_ob(walker* ob);
    const PixieData* configure_existing_entity(walker& entity, Order order, std::int32_t family);
    void set_entity_derived_stats(walker* entity, Order order, std::int32_t family);
    void set_detach_callback(std::function<void()> callback);

    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);

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

    void create_new_grid();
    void resize_grid(int width, int height);
    void delete_grid();
    void delete_objects();
    void clear();
    short remaining_foes(walker* myguy) const;

    // Reset per-level run counters when starting a fresh mission attempt.
    // Without this, same-level retries can inherit timeout progress.
    void reset_level_progress();

    // Run one simulation tick.
    void tick();

    std::uint32_t tick_count_ = 0;
    og::sim::SimRandom rng_;

    // Game state flags written in-place by tick().
    short level_done = 0;
    bool game_ended = false;
    short next_level = -1;
    short ending = 0;
    std::int32_t enemy_freeze = 0;
    signed char timer_wait = 6;
    char end = 0;
    bool retry = false;
    float control_hp = 0.0f;
    bool withdraw_requested = false;
    short withdraw_level = -1;

    // Gameplay-relevant values that will migrate off SaveData/session.
    std::uint32_t m_score[4] = {};
    short my_team = 0;
    short allied_mode = 0;
    short current_scenario = 0;
    int guy_id_counter = 0;
    std::set<int> completed_levels;
    std::function<std::unique_ptr<walker>(Order, std::int32_t)> entity_factory;
    std::function<const PixieData*(walker&, Order, std::int32_t)> entity_configurator;
    std::function<void(walker*, Order, std::int32_t)> entity_derived_stats;

private:
    walker* add_to_list(Order order, std::int32_t family,
                        std::list<std::unique_ptr<walker>>& target_list,
                        bool count_living, bool atstart);

    std::uint32_t level_tick_count_ = 0;
    int last_level_id_ = -1;
    std::function<void()> detach_callback_;
};
