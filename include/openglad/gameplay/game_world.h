/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <array>
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

#include <openglad/core/decordefs.h>
#include <openglad/core/tower_constants.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/ctf/ctf_state.h>
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

#ifdef TESTING
IRandom* sim_random_override();
void set_sim_random_override(IRandom** rng_ref);
#endif

// Simple LCG random number generator.
// Given the same seed, produces the same deterministic sequence.
class SimRandom final : public IRandom {
public:
    explicit SimRandom(std::uint32_t seed = 0) : state_(seed) {}

    std::uint32_t next(std::uint32_t max_exclusive) override {
        if (max_exclusive == 0) return 0;
#ifdef TESTING
        if (IRandom* override_rng = sim_random_override())
            return override_rng->next(max_exclusive);
#endif
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
    static constexpr char TYPE_CTF = 0x8;
    // Tower-authored level (SCEN_TYPE_TOWER in .fss files). Display-only in
    // v1 — the sole reader is floor_hud_label; never runtime-mutated. A level
    // authored with both TYPE_CTF and TYPE_TOWER resolves CTF-first (v1
    // content never authors both).
    static constexpr char TYPE_TOWER = 0x10;

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

    // Spatial data. floors_[0] is represented by the legacy single
    // grid/mysmoother/myobmap members below (kept as-is so single-floor levels
    // run byte-identical code). Stacked floors 1..N live in extra_floors_;
    // per-floor access goes through grid_for_floor/smoother_for_floor/
    // obmap_for_floor. All floors share pixmaxx/pixmaxy (same footprint).
    // See docs/z-axis-design.md.
    std::unique_ptr<obmap> myobmap;
    PixieData grid;
    // Decor plane for floor 0: bytes are decor ids (core/decordefs.h), 0 =
    // none, same dims as the floor's grid. Invalid (unallocated) for levels
    // without decor — every consumer gates on validity, so a no-decor level
    // runs byte-identical sim and render. See docs (tile layering, .fss v11).
    PixieData decor;
    smoother mysmoother;
    std::int32_t pixmaxx = 0;
    std::int32_t pixmaxy = 0;

    // Additional stacked floors (index 1..N). Empty for single-floor levels.
    // Collision uses ONE floor-keyed obmap (myobmap) whose spatial-hash buckets
    // incorporate the entity's floor (see obmap.cpp), so floor 0's buckets stay
    // byte-identical and entities on different floors never collide. Only grid +
    // smoother are genuinely per-floor.
    struct ExtraFloor {
        PixieData grid;
        PixieData decor;
        smoother floor_smoother;
    };
    std::vector<ExtraFloor> extra_floors_;

    [[nodiscard]] int floor_count() const noexcept
    {
        return 1 + static_cast<int>(extra_floors_.size());
    }
    [[nodiscard]] bool is_multifloor() const noexcept
    {
        return !extra_floors_.empty();
    }

    // True when any walker on the level carries the npc_flags bit-2
    // "protected" mark (loaded from v10+ level files). Scopes the
    // SCEN_TYPE_SAVE_ALL death check: with any protected walker present,
    // ONLY flagged walkers are mission-critical; with none, the legacy
    // any-named-team-0 rule applies. Scans oblist + dead_list (a corpse
    // keeps its flag), so the answer is stable across the fatal tick.
    [[nodiscard]] bool has_save_all_protected() const;

    // Per-floor accessors. An out-of-range floor falls back to floor 0; this
    // doubles as the untrusted-floor clamp for snapshot/save-supplied indices.
    [[nodiscard]] bool valid_floor(int floor) const noexcept
    {
        return floor > 0 && floor <= static_cast<int>(extra_floors_.size());
    }
    [[nodiscard]] PixieData& grid_for_floor(int floor) noexcept
    {
        return valid_floor(floor)
                   ? extra_floors_[static_cast<std::size_t>(floor - 1)].grid
                   : grid;
    }
    [[nodiscard]] const PixieData& grid_for_floor(int floor) const noexcept
    {
        return valid_floor(floor)
                   ? extra_floors_[static_cast<std::size_t>(floor - 1)].grid
                   : grid;
    }
    [[nodiscard]] PixieData& decor_for_floor(int floor) noexcept
    {
        return valid_floor(floor)
                   ? extra_floors_[static_cast<std::size_t>(floor - 1)].decor
                   : decor;
    }
    [[nodiscard]] const PixieData& decor_for_floor(int floor) const noexcept
    {
        return valid_floor(floor)
                   ? extra_floors_[static_cast<std::size_t>(floor - 1)].decor
                   : decor;
    }
    // True when the decor plane for `floor` is valid and the cell (grid
    // coords) carries a concealing decor id (registry `conceals`; SHRUB only
    // in v1). Shared by the four TYPE_TREES concealment consumers (living
    // forestwalk, weapon lineofsight decay, and the two draw-suppression
    // sites). Gated on plane validity + bounds: zero cost and zero RNG on
    // no-decor levels.
    [[nodiscard]] bool decor_conceals_at(int floor, std::int32_t grid_x,
                                         std::int32_t grid_y) const noexcept
    {
        const PixieData& dec = decor_for_floor(floor);
        if (!dec.valid())
            return false;
        if (grid_x < 0 || grid_y < 0 || grid_x >= dec.w || grid_y >= dec.h)
            return false;
        const unsigned char d =
            dec.data[static_cast<std::size_t>(grid_x + dec.w * grid_y)];
        return d < DECOR_MAX && kDecorRegistry[d].conceals;
    }
    [[nodiscard]] smoother& smoother_for_floor(int floor) noexcept
    {
        return valid_floor(floor)
                   ? extra_floors_[static_cast<std::size_t>(floor - 1)].floor_smoother
                   : mysmoother;
    }
    // Single floor-keyed obmap (the floor arg is accepted for symmetry but the
    // one obmap buckets all floors via ob->floor()).
    [[nodiscard]] obmap* obmap_for_floor(int /*floor*/) noexcept
    {
        return myobmap.get();
    }

    // Grow/shrink the floor stack to exactly `count` floors (>=1). Floor 0 is
    // the legacy members; extra floors get a fresh obmap + RNG-wired smoother.
    // Grids are sized/filled and smoothers re-targeted by the caller after.
    void set_floor_count(int count);

    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);
    short remove_ob(walker* ob);
    walker* find_by_id(std::uint32_t entity_id);
    const walker* find_by_id(std::uint32_t entity_id) const;
    std::uint32_t tracked_entity_id(const walker* entity) const;
    std::uint32_t level_tick_count() const noexcept { return level_tick_count_; }
    // An explicit tick count always belongs to the current level, so align the
    // level-change latch too; otherwise the first tick after applying a
    // mid-level snapshot would zero the restored counter.
    void set_level_tick_count(std::uint32_t value) noexcept
    {
        level_tick_count_ = value;
        last_level_id_ = id;
    }
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
    // Floor-explicit variants. The 3-arg forms above forward with ob->floor();
    // pathfinding uses these to probe a different target floor for stair edges.
    bool query_passable(float x, float y, walker* ob, int floor);
    bool query_object_passable(float x, float y, walker* ob, int floor);
    bool query_grid_passable(float x, float y, walker* ob, int floor);
    // Side-effect-free landing probe for floor transitions (Z-stairs, air
    // falls, cross-floor teleports): true when ob's bbox at (x, y) on
    // target_floor is grid-passable AND overlaps no blocking entity there.
    // Unlike query_object_passable it never routes through ob_pass_check, so
    // probing cannot eat treasures or fire collide() side effects ("no-eat").
    bool floor_landing_clear(walker* ob, float x, float y, int target_floor);

    // 2026-07-10 guard facing gate (docs/GAMEPLAY_FIXES_FROM_CLASSIC.md).
    // True when the straight cell ray between the two walkers' centers
    // crosses no sight-opaque tile (tree crowns/trunks and wall faces — the
    // grid bytes an unowned projectile can never pass). Water, lava,
    // boulders, columns and torches stay transparent (projectiles fly over
    // them), so ranged guards keep engaging across them. Deterministic and
    // RNG-free; cross-floor pairs are never in sight.
    bool clear_sight_line(const walker* from, const walker* to);

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

    // Per-level weather: GAMEPLAY-INERT render hint (see core/weather.h).
    // Defaults to None and resets on every level load; only the authoritative
    // side rolls (roll_weather), clients receive the kind via WorldSnapshot.
    // The sim must never branch on it.
    [[nodiscard]] WeatherKind weather() const noexcept { return weather_; }
    void set_weather(WeatherKind kind) noexcept { weather_ = kind; }
    // Authoritative-only roll from (level id, process-wide nonce). Never
    // called on client mirror worlds and never consumes the sim RNG.
    void roll_weather();

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
    short ctf_requested_team_count = 0;
    short ctf_requested_capture_limit = 0;
    short ctf_requested_respawn_ticks = 0;
    short ctf_requested_strip_scenario_troops = 0;
    // Classic (non-CTF) respawn mode: 0 = off (legacy), 1 = heroes respawn,
    // 2 = heroes + level-authored AI livings respawn ("endless battle").
    short respawn_mode = 0;
    // Generator spawn-rate percent: 0 = default (100). Scales the level-side
    // Bernoulli bound in walker::act_generate; 100 is an exact integer
    // identity, keeping the default RNG stream byte-identical.
    short generator_rate = 0;
    // Session permadeath mirror (SaveData::keep_fallen_heroes, synced by both
    // sync_world_from_save_data twins). 0 = permadeath on win (legacy): a
    // fallen hero is lost, so the life gem carries full salvage value.
    // Nonzero = fallen heroes return, so death() drops the gem at HALF value
    // (the hero's growth already survives — full salvage would double-dip).
    // Default 0 keeps parity scenarios and legacy content byte-identical.
    short keep_fallen_heroes = 0;
    // Networked control policy (protocol v8 / snapshot v9, company-basecamp
    // design §4.1/§4.4): 0 = legacy (any seat may claim any walker — every
    // local/solo session and cross-control-ON lobbies), 1 = owner-locked
    // (claims restricted to the owning machine's characters). Set only by
    // the networked host installs; the defaults keep local play and the
    // parity goldens byte-identical.
    std::uint8_t control_policy = 0;
    // Per-global-player machine map, indexed by global player index (16 ==
    // kMaxGlobalPlayers, net_transport.h). Low 7 bits = machine id (the
    // owning peer's lowest bound global player index); bit7 = "this player's
    // machine deployed >= 1 character this level"; 0xff = no player bound.
    std::array<std::uint8_t, 16> player_machine = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    og::sim::CtfState ctf;
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
    WeatherKind weather_ = WeatherKind::None;
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

// Floor-awareness HUD label. Empty => call sites draw NOTHING (single-floor
// frames stay byte-identical). Game modes re-label by editing ONLY this
// function; every surface (SDL FOES-box row, curses status line) updates
// automatically. Call sites key on non-empty, never on floor_count.
// walker_floor is clamped to [0, floor_count-1] (mirror int8 safety —
// attacker-controllable on a network mirror). Pure display: no RNG, no
// mutation, never on the tick path.
std::string floor_hud_label(const GameWorld& world, int walker_floor);
