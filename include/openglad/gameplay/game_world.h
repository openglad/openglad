/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/irandom.h>

// Forward-declare Order enum class (defined in base.h)
enum class Order : unsigned char;

class obmap;
class walker;
class SaveData;
class cfg_store;
struct GameplayContext;
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
    class EntityList
    {
    public:
        using value_type = std::unique_ptr<walker>;
        using Storage = std::list<value_type>;
        using const_iterator = Storage::const_iterator;
        using const_reverse_iterator = Storage::const_reverse_iterator;
        using size_type = Storage::size_type;

        EntityList() = default;
        EntityList(GameWorld* owner, bool participates_in_id_index);
        EntityList(const EntityList&) = delete;
        EntityList& operator=(const EntityList&) = delete;
        EntityList(EntityList&&) = delete;
        EntityList& operator=(EntityList&&) = delete;

        const_iterator begin() const noexcept;
        const_iterator end() const noexcept;
        const_iterator cbegin() const noexcept;
        const_iterator cend() const noexcept;
        const_reverse_iterator rbegin() const noexcept;
        const_reverse_iterator rend() const noexcept;
        const_reverse_iterator crbegin() const noexcept;
        const_reverse_iterator crend() const noexcept;
        operator const Storage&() const noexcept;

        bool empty() const noexcept;
        size_type size() const noexcept;
        const value_type& front() const;
        const value_type& back() const;

        void push_back(value_type entity);
        void push_front(value_type entity);
        void pop_back();
        void pop_front();
        const_iterator erase(const_iterator position);
        void clear();
        void splice(const_iterator position, EntityList& other);
        void splice(const_iterator position, Storage& other);
        void splice_into(Storage& destination);

    private:
        friend class GameWorld;

        Storage& raw_mutable() noexcept;
        const Storage& raw() const noexcept;
        void prepare_insert(walker* entity);
        void prepare_remove(walker* entity);
        void prepare_insert(Storage& entities);
        void prepare_remove(Storage& entities);
        void invalidate_owner();

        GameWorld* owner_ = nullptr;
        bool participates_in_id_index_ = false;
        Storage entities_;
    };

    static constexpr char TYPE_CAN_EXIT_WHENEVER = 0x1;
    static constexpr char TYPE_MUST_DESTROY_GENERATORS = 0x2;
    static constexpr char TYPE_MUST_PROTECT_NAMED_NPCS = 0x4;

    explicit GameWorld(std::uint32_t seed = 0);
    ~GameWorld();
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;
    GameWorld(GameWorld&&) = delete;
    GameWorld& operator=(GameWorld&&) = delete;

    // Level metadata
    int id = 0;
    std::string title = "New Level";
    char type = 0;
    short par_value = 1;
    short time_bonus_limit = 4000;
    short difficulty = 100;

    int living_count = 0;
    EntityList oblist;
    EntityList fxlist;
    EntityList weaplist;
    EntityList dead_list;

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
    walker* find_by_id(std::uint32_t entity_id);
    const walker* find_by_id(std::uint32_t entity_id) const;
    std::uint32_t tracked_entity_id(const walker* entity) const;
    std::uint32_t level_tick_count() const noexcept { return level_tick_count_; }
    void set_level_tick_count(std::uint32_t value) noexcept { level_tick_count_ = value; }
    const std::vector<std::uint32_t>& removed_entity_ids() const noexcept { return removed_entity_ids_; }
    std::vector<std::uint32_t> take_removed_entity_ids();
    void clear_removed_entity_ids() noexcept;
    const std::vector<std::pair<short, short>>& grid_dirty_tiles() const noexcept { return grid_dirty_tiles_; }
    std::vector<std::pair<short, short>> take_grid_dirty_tiles();
    void clear_grid_dirty_tiles() noexcept;
    void move_entities_from(GameWorld& source);
    const PixieData* configure_existing_entity(walker& entity, Order order, std::int32_t family);
    void set_entity_derived_stats(walker* entity, Order order, std::int32_t family);
    void set_detach_callback(std::function<void()> callback);
    void set_gameplay_context_bindings(SaveData* save,
                                       og::sim::SimEventLog* sim_events,
                                       cfg_store* config) noexcept;
    bool populate_gameplay_context(GameplayContext& context) const noexcept;
    char damage_tile(short xloc, short yloc);

    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);

    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    walker* find_nearest_blood(walker* who);
    walker* find_nearest_player(walker* ob);
    std::list<walker*> find_in_range(const std::list<std::unique_ptr<walker>>& somelist,
                                     std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foes_in_range(const std::list<std::unique_ptr<walker>>& somelist,
                                          std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_foe_weapons_in_range(const std::list<std::unique_ptr<walker>>& somelist,
                                                 std::int32_t range, std::int32_t* howmany, walker* ob);
    std::list<walker*> find_friends_in_range(const std::list<std::unique_ptr<walker>>& somelist,
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
    bool completion_events_emitted = false;
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
    std::uint8_t current_palette_id = 0;
    bool pending_exit_prompt = false;
    bool paused = false;
    std::uint8_t pause_player_index = 0xff;
    std::set<int> completed_levels;
    std::function<std::unique_ptr<walker>(Order, std::int32_t)> entity_factory;
    std::function<const PixieData*(walker&, Order, std::int32_t)> entity_configurator;
    std::function<void(walker*, Order, std::int32_t)> entity_derived_stats;
    bool applying_snapshot_ = false;

private:
    walker* add_to_list(Order order, std::int32_t family,
                        EntityList& target_list,
                        bool count_living, bool atstart);
    void attach_entity_to_world(walker& entity);
    std::uint32_t assign_entity_id(walker& entity);
    void index_entity(walker& entity);
    void remove_from_id_index(const walker* entity);
    void invalidate_entity_tracking();
    void rebuild_id_index();

    std::uint32_t level_tick_count_ = 0;
    int last_level_id_ = -1;
    std::function<void()> detach_callback_;
    std::uint32_t next_entity_id_ = 1;
    bool entity_tracking_dirty_ = false;
    std::vector<std::uint32_t> removed_entity_ids_;
    std::vector<std::pair<short, short>> grid_dirty_tiles_;
    // Main-thread only. Future networking I/O must queue work onto the game loop
    // thread before reading or mutating GameWorld state. The cache is derived
    // from the active entity lists and repaired internally by GameWorld.
    std::unordered_map<std::uint32_t, walker*> id_index_;
    SaveData* gameplay_save_ = nullptr;
    og::sim::SimEventLog* gameplay_sim_events_ = nullptr;
    cfg_store* gameplay_config_ = nullptr;
};
