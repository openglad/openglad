/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/game_world.h>

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/scenario_strip.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/placement.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <utility>

namespace
{
constexpr short MAX_SPREAD = 10;

template <typename WalkerList>
bool contains_walker_ptr(const WalkerList& list, const walker* candidate)
{
    return std::any_of(list.begin(), list.end(),
                       [candidate](const auto& entry) {
                           return entry.get() == candidate;
                       });
}

void sanitize_owner_chain_link(const GameWorld& world, walker* entity)
{
    constexpr int kMaxOwnerDepth = 16;
    walker* current = entity;
    // plumbing: owner-chain walk bounded at 16 hops; see below
    for (int depth = 0; depth < kMaxOwnerDepth; ++depth)
    {
        walker* owner = current->owner();
        // rule: Before any foe scan, a walker's owner chain is trusted only while every owner is
        //   still in the world: an owner that has left every entity list is cut (the summon/weapon
        //   becomes ownerless), and a chain longer than 16 hops (a save/snapshot loop) is cut at
        //   the hop the walk gave up on; a self-owner or a legal chain is left alone. Called from
        //   find_near_foe (below) and find_nearest_foe (below).
        // tests: GameWorldEntityIdsFixture.an_owner_loop_is_cut_and_legal_chains_survive
        // tests: SemWorld.a_self_owner_or_no_owner_survives_the_owner_chain_guard
        // tests: SemWorld.an_owner_that_left_the_world_is_cut_before_a_foe_scan
        if (owner == nullptr || owner == current)
            return;
        if (!world.tracks(owner))
        {
            current->set_owner(nullptr);
            return;
        }
        current = owner;
    }

    // Defensive cycle/depth break. Any deeper chain is suspicious in practice.
    current->set_owner(nullptr);
}

void refresh_self_reference_ids(walker& entity)
{
    // plumbing: refresh_self_reference_ids: re-runs setters for self-references so pointer/id
    //   mirror fields agree after an id assignment; no game semantic
    if (entity.foe() == &entity)
        entity.set_foe(&entity);
    if (entity.leader() == &entity)
        entity.set_leader(&entity);
    if (entity.owner() == &entity)
        entity.set_owner(&entity);
    if (entity.collide_ob() == &entity)
        entity.set_collide_ob(&entity);

    // plumbing: refresh_self_reference_ids: re-runs setters for self-references so pointer/id
    //   mirror fields agree after an id assignment; no game semantic
    if (statistics* stats = entity.stats();
        stats != nullptr && stats->controller() == &entity)
    {
        stats->set_controller(&entity);
    }
}
}

bool GameWorld::tracks(const walker* candidate) const noexcept
{
    // plumbing: tracks(nullptr) is false
    if (candidate == nullptr)
        return false;

    return contains_walker_ptr(oblist, candidate)
        || contains_walker_ptr(fxlist, candidate)
        || contains_walker_ptr(weaplist, candidate)
        || contains_walker_ptr(dead_list, candidate);
}

namespace og::sim {
// Test hook to shorten mission timeout checks in deterministic harnesses.
std::int32_t g_test_level_tick_limit_override = 0;

namespace {
thread_local IRandom** g_sim_random_override_ref = nullptr;
}

IRandom* sim_random_override()
{
    // plumbing: the thread-local RNG override (parity harness / tests) when one is installed, else
    //   none.
    return (g_sim_random_override_ref != nullptr)
        ? *g_sim_random_override_ref
        : nullptr;
}

IRandom** set_sim_random_override(IRandom** rng_ref)
{
    return std::exchange(g_sim_random_override_ref, rng_ref);
}
} // namespace og::sim

GameWorld::GameWorld(std::uint32_t seed)
    : oblist(this, true),
      fxlist(this, true),
      weaplist(this, true),
      dead_list(this, false),
      rng_(seed)
{
    mysmoother.set_rng(&rng_);
}

og::script::WorldScripts& GameWorld::scripts()
{
    // Rebuild when the mounted pack set changed (campaign packs mount after
    // world creation; tests remount freely). Rebuild timing is the first
    // dispatch after the change — identical on every peer given identical
    // mount sequencing, so this stays deterministic. The build generation
    // covers scripts AND og.use lib modules (pack_scripts_build_generation);
    // comparing against the script counter alone would mismatch forever
    // once a lib mutation happened and rebuild the VM on every dispatch.
    // plumbing: scripts(): rebuild the Lua VM when the mounted pack generation changed;
    //   deterministic rebuild timing, no game rule
    if (scripts_ != nullptr &&
        scripts_->built_generation() !=
            og::script::pack_scripts_build_generation())
        scripts_.reset();
    if (scripts_ == nullptr)
        scripts_ = std::make_unique<og::script::WorldScripts>();
    return *scripts_;
}

GameWorld::~GameWorld()
{
    // plumbing: destructor detach callback
    if (detach_callback_)
        detach_callback_();

    // Destroy the entity lists here, in the destructor body, while myobmap is
    // still alive: each ~walker deregisters from owning_world->myobmap, so the
    // obmap must outlive the walkers. Member destruction order alone would free
    // myobmap (declared last) before oblist, leaving ~walker to touch a freed
    // obmap. Worlds torn down via delete_objects()/clear() first hit empty
    // lists here, so this is a no-op for them.
    oblist.clear();
    fxlist.clear();
    weaplist.clear();
    dead_list.clear();
}

GameWorld::EntityList::EntityList(GameWorld* owner, bool participates_in_id_index)
    : owner_(owner),
      participates_in_id_index_(participates_in_id_index)
{
}

GameWorld::EntityList::const_iterator GameWorld::EntityList::begin() const noexcept
{
    return entities_.begin();
}

GameWorld::EntityList::const_iterator GameWorld::EntityList::end() const noexcept
{
    return entities_.end();
}

GameWorld::EntityList::const_iterator GameWorld::EntityList::cbegin() const noexcept
{
    return entities_.cbegin();
}

GameWorld::EntityList::const_iterator GameWorld::EntityList::cend() const noexcept
{
    return entities_.cend();
}

GameWorld::EntityList::const_reverse_iterator GameWorld::EntityList::rbegin() const noexcept
{
    return entities_.rbegin();
}

GameWorld::EntityList::const_reverse_iterator GameWorld::EntityList::rend() const noexcept
{
    return entities_.rend();
}

GameWorld::EntityList::const_reverse_iterator GameWorld::EntityList::crbegin() const noexcept
{
    return entities_.crbegin();
}

GameWorld::EntityList::const_reverse_iterator GameWorld::EntityList::crend() const noexcept
{
    return entities_.crend();
}

GameWorld::EntityList::operator const Storage&() const noexcept
{
    return raw();
}

bool GameWorld::EntityList::empty() const noexcept
{
    return entities_.empty();
}

GameWorld::EntityList::size_type GameWorld::EntityList::size() const noexcept
{
    return entities_.size();
}

const GameWorld::EntityList::value_type& GameWorld::EntityList::front() const
{
    return entities_.front();
}

const GameWorld::EntityList::value_type& GameWorld::EntityList::back() const
{
    return entities_.back();
}

void GameWorld::EntityList::push_back(value_type entity)
{
    prepare_insert(entity.get());
    entities_.push_back(std::move(entity));
    invalidate_owner();
}

void GameWorld::EntityList::push_front(value_type entity)
{
    prepare_insert(entity.get());
    entities_.push_front(std::move(entity));
    invalidate_owner();
}

void GameWorld::EntityList::pop_back()
{
    // plumbing: pop on empty EntityList is a no-op
    if (entities_.empty())
        return;

    prepare_remove(entities_.back().get());
    entities_.pop_back();
    invalidate_owner();
}

void GameWorld::EntityList::pop_front()
{
    // plumbing: pop on empty EntityList is a no-op
    if (entities_.empty())
        return;

    prepare_remove(entities_.front().get());
    entities_.pop_front();
    invalidate_owner();
}

GameWorld::EntityList::const_iterator GameWorld::EntityList::erase(const_iterator position)
{
    prepare_remove(position->get());
    const auto next = entities_.erase(position);
    invalidate_owner();
    return next;
}

void GameWorld::EntityList::clear()
{
    prepare_remove(entities_);
    entities_.clear();
    invalidate_owner();
}

void GameWorld::EntityList::splice(const_iterator position, EntityList& other)
{
    // plumbing: splice of self/empty list is a no-op
    if (this == &other || other.entities_.empty())
        return;

    prepare_insert(other.entities_);
    entities_.splice(position, other.entities_);
    other.invalidate_owner();
    invalidate_owner();
}

void GameWorld::EntityList::splice(const_iterator position, Storage& other)
{
    // plumbing: splice of self/empty list is a no-op
    if (other.empty())
        return;

    prepare_insert(other);
    entities_.splice(position, other);
    invalidate_owner();
}

void GameWorld::EntityList::splice_into(Storage& destination)
{
    // plumbing: splice of self/empty list is a no-op
    if (entities_.empty())
        return;

    prepare_remove(entities_);
    destination.splice(destination.end(), entities_);
    invalidate_owner();
}

GameWorld::EntityList::Storage& GameWorld::EntityList::raw_mutable() noexcept
{
    return entities_;
}

const GameWorld::EntityList::Storage& GameWorld::EntityList::raw() const noexcept
{
    return entities_;
}

void GameWorld::EntityList::prepare_insert(walker* entity)
{
    // plumbing: prepare_insert: id assignment and world attachment only for id-indexed lists
    //   (dead_list is not indexed)
    if (owner_ == nullptr || entity == nullptr)
        return;

    // plumbing: prepare_insert: id assignment and world attachment only for id-indexed lists
    //   (dead_list is not indexed)
    if (participates_in_id_index_)
    {
        owner_->assign_entity_id(*entity);
        refresh_self_reference_ids(*entity);
        entity->mark_all_dirty();
    }
    entity->owning_world_ = owner_;
}

void GameWorld::EntityList::prepare_remove(walker* entity)
{
    // plumbing: prepare_remove logs removed entity ids for the network delta
    if (entity == nullptr)
        return;

    // plumbing: prepare_remove logs removed entity ids for the network delta
    if (owner_ != nullptr && participates_in_id_index_ && entity->entity_id() != 0)
        owner_->removed_entity_ids_.push_back(entity->entity_id());
}

void GameWorld::EntityList::prepare_insert(Storage& entities)
{
    // plumbing: per-entity prepare_insert/prepare_remove over a spliced list; plumbing
    for (auto& uptr : entities)
        prepare_insert(uptr.get());
}

void GameWorld::EntityList::prepare_remove(Storage& entities)
{
    // plumbing: per-entity prepare_insert/prepare_remove over a spliced list; plumbing
    for (auto& uptr : entities)
        prepare_remove(uptr.get());
}

void GameWorld::EntityList::invalidate_owner()
{
    // plumbing: invalidate tracking only with an owner world
    if (owner_ != nullptr)
        owner_->invalidate_entity_tracking();
}

const PixieData* GameWorld::configure_existing_entity(walker& entity, Order order, std::int32_t family)
{
    // plumbing: optional configurator / derived-stats hooks absent -> no-op (asserted by
    //   optional_entity_hooks_preserve_null_and_forward_valid_calls)
    if (!entity_configurator)
        return nullptr;
    return entity_configurator(entity, order, family);
}

void GameWorld::set_entity_derived_stats(walker* entity, Order order, std::int32_t family)
{
    // plumbing: optional configurator / derived-stats hooks absent -> no-op (asserted by
    //   optional_entity_hooks_preserve_null_and_forward_valid_calls)
    if (!entity_derived_stats || entity == nullptr)
        return;
    entity_derived_stats(entity, order, family);
}

void GameWorld::set_detach_callback(std::function<void()> callback)
{
    detach_callback_ = std::move(callback);
}

void GameWorld::set_gameplay_context_bindings(SaveData* save,
                                              og::sim::SimEventLog* sim_events,
                                              cfg_store* config) noexcept
{
    gameplay_save_ = save;
    gameplay_sim_events_ = sim_events;
    gameplay_config_ = config;
}

bool GameWorld::populate_gameplay_context(GameplayContext& context) const noexcept
{
    context = GameplayContext{};
    context.world = const_cast<GameWorld*>(this);
    context.save = gameplay_save_;
    context.sim_events = gameplay_sim_events_;
    context.config = gameplay_config_;

    // plumbing: populate_gameplay_context falls back to the active current_game bindings for this
    //   world (asserted by gameplay_context_falls_back_to_matching_active_context); dependency
    //   wiring, no game rule
    if ((context.save == nullptr ||
         context.sim_events == nullptr ||
         context.config == nullptr) &&
        current_game != nullptr &&
        current_game->world == this)
    {
        // plumbing: populate_gameplay_context falls back to the active current_game bindings for
        //   this world (asserted by gameplay_context_falls_back_to_matching_active_context);
        //   dependency wiring, no game rule
        if (context.save == nullptr)
            context.save = current_game->save;
        // plumbing: populate_gameplay_context falls back to the active current_game bindings for
        //   this world (asserted by gameplay_context_falls_back_to_matching_active_context);
        //   dependency wiring, no game rule
        if (context.sim_events == nullptr)
            context.sim_events = current_game->sim_events;
        // plumbing: populate_gameplay_context falls back to the active current_game bindings for
        //   this world (asserted by gameplay_context_falls_back_to_matching_active_context);
        //   dependency wiring, no game rule
        if (context.config == nullptr)
            context.config = current_game->config;
        context.rng_override_ref = current_game->rng_override_ref;
        context.session_rng_ref = current_game->session_rng_ref;
        context.gameplay_active_ref = current_game->gameplay_active_ref;
    }

    return context.world != nullptr &&
           context.sim_events != nullptr &&
           context.config != nullptr;
}

std::vector<std::uint32_t> GameWorld::take_removed_entity_ids()
{
    auto removed = std::move(removed_entity_ids_);
    removed_entity_ids_.clear();
    return removed;
}

void GameWorld::clear_removed_entity_ids() noexcept
{
    removed_entity_ids_.clear();
}

std::vector<std::pair<short, short>> GameWorld::take_grid_dirty_tiles()
{
    auto dirty_tiles = std::move(grid_dirty_tiles_);
    grid_dirty_tiles_.clear();
    return dirty_tiles;
}

void GameWorld::clear_grid_dirty_tiles() noexcept
{
    grid_dirty_tiles_.clear();
}

void GameWorld::attach_entity_to_world(walker& entity)
{
    entity.owning_world_ = this;
}

std::uint32_t GameWorld::assign_entity_id(walker& entity)
{
    // plumbing: entity id allocation: fresh id for 0, next_entity_id_ bumped past adopted ids
    //   (asserted by assigns_unique_non_zero_ids_and_finds_entities /
    //   moving_entities_between_worlds_preserves_lookup_and_next_id)
    if (entity.entity_id() == 0)
        entity.set_entity_id(next_entity_id_++);
    else if (entity.entity_id() >= next_entity_id_)
        next_entity_id_ = entity.entity_id() + 1;

    return entity.entity_id();
}

void GameWorld::index_entity(walker& entity)
{
    attach_entity_to_world(entity);
    // plumbing: id-0 walkers are not indexed
    if (entity.entity_id() == 0)
        return;

    id_index_[entity.entity_id()] = &entity;
}

void GameWorld::remove_from_id_index(const walker* entity)
{
    // plumbing: remove_from_id_index guards + lazy rebuild + only erase the entry that points at
    //   this walker
    if (entity == nullptr || entity->entity_id() == 0)
        return;

    // plumbing: remove_from_id_index guards + lazy rebuild + only erase the entry that points at
    //   this walker
    if (entity_tracking_dirty_)
        rebuild_id_index();

    const auto it = id_index_.find(entity->entity_id());
    // plumbing: remove_from_id_index guards + lazy rebuild + only erase the entry that points at
    //   this walker
    if (it != id_index_.end() && it->second == entity)
    {
        removed_entity_ids_.push_back(entity->entity_id());
        id_index_.erase(it);
    }
}

void GameWorld::invalidate_entity_tracking()
{
    id_index_.clear();
    entity_tracking_dirty_ = true;
}

void GameWorld::rebuild_id_index()
{
    const auto reserve_size = oblist.size() + fxlist.size() + weaplist.size();
    id_index_.clear();
    id_index_.reserve(reserve_size);

    auto reindex_list = [this](auto& entities, bool include_in_index) {
        // plumbing: rebuild_id_index reindex loop: null skip, next-id bump, dead_list excluded from
        //   index; plumbing
        for (auto& uptr : entities)
        {
            walker* entity = uptr.get();
            // plumbing: rebuild_id_index reindex loop: null skip, next-id bump, dead_list excluded
            //   from index; plumbing
            if (entity == nullptr)
                continue;

            attach_entity_to_world(*entity);
            // plumbing: rebuild_id_index reindex loop: null skip, next-id bump, dead_list excluded
            //   from index; plumbing
            if (entity->entity_id() >= next_entity_id_)
                next_entity_id_ = entity->entity_id() + 1;
            // plumbing: rebuild_id_index reindex loop: null skip, next-id bump, dead_list excluded
            //   from index; plumbing
            if (include_in_index && entity->entity_id() != 0)
                id_index_[entity->entity_id()] = entity;
        }
    };

    reindex_list(oblist, true);
    reindex_list(fxlist, true);
    reindex_list(weaplist, true);
    reindex_list(dead_list, false);
    entity_tracking_dirty_ = false;
}

walker* GameWorld::find_by_id(std::uint32_t entity_id)
{
    return const_cast<walker*>(std::as_const(*this).find_by_id(entity_id));
}

std::uint32_t GameWorld::tracked_entity_id(const walker* entity) const
{
    // plumbing: tracked_entity_id reverse lookup with lazy rebuild
    if (entity == nullptr)
        return 0;

    auto* mutable_world = const_cast<GameWorld*>(this);
    // plumbing: tracked_entity_id reverse lookup with lazy rebuild
    if (entity_tracking_dirty_)
        mutable_world->rebuild_id_index();

    const auto it = std::find_if(
        id_index_.begin(), id_index_.end(),
        [entity](const auto& entry) {
            return entry.second == entity;
        });
    // plumbing: tracked_entity_id reverse lookup with lazy rebuild
    return (it == id_index_.end()) ? 0 : it->first;
}

const walker* GameWorld::find_by_id(std::uint32_t entity_id) const
{
    // plumbing: find_by_id: id 0 -> null, lazy rebuild, self-heal on a stale entry (asserted by
    //   direct_public_insert_assigns_id_and_rebuilds_id_index_on_lookup)
    if (entity_id == 0)
        return nullptr;

    auto* mutable_world = const_cast<GameWorld*>(this);
    // plumbing: find_by_id: id 0 -> null, lazy rebuild, self-heal on a stale entry (asserted by
    //   direct_public_insert_assigns_id_and_rebuilds_id_index_on_lookup)
    if (entity_tracking_dirty_)
        mutable_world->rebuild_id_index();

    auto it = id_index_.find(entity_id);
    // plumbing: find_by_id: id 0 -> null, lazy rebuild, self-heal on a stale entry (asserted by
    //   direct_public_insert_assigns_id_and_rebuilds_id_index_on_lookup)
    if (it == id_index_.end() ||
        it->second == nullptr ||
        it->second->entity_id() != entity_id ||
        it->second->owning_world_ != this)
    {
        mutable_world->rebuild_id_index();
        it = mutable_world->id_index_.find(entity_id);
    }

    // plumbing: find_by_id: id 0 -> null, lazy rebuild, self-heal on a stale entry (asserted by
    //   direct_public_insert_assigns_id_and_rebuilds_id_index_on_lookup)
    return (it == id_index_.end()) ? nullptr : it->second;
}

void GameWorld::move_entities_from(GameWorld& source)
{
    // plumbing: move_entities_from(self) is a no-op (asserted by
    //   empty_list_removals_and_self_move_are_noops)
    if (this == &source)
        return;

    oblist.splice(oblist.end(), source.oblist);
    fxlist.splice(fxlist.end(), source.fxlist);
    weaplist.splice(weaplist.end(), source.weaplist);
    dead_list.splice(dead_list.end(), source.dead_list);

    next_entity_id_ = std::max(next_entity_id_, source.next_entity_id_);
    source.next_entity_id_ = 1;
    source.clear_removed_entity_ids();
    source.id_index_.clear();
    source.entity_tracking_dirty_ = false;
    rebuild_id_index();
}

walker* GameWorld::add_to_list(Order order, std::int32_t family,
                               EntityList& target_list,
                               bool count_living, bool atstart)
{
    // plumbing: no entity factory / factory refused -> nullptr, world unchanged (asserted by
    //   rejected_factory_results_leave_world_unchanged)
    if (!entity_factory)
        return nullptr;

    // Create the walker
    auto w = entity_factory(order, family);
    // plumbing: no entity factory / factory refused -> nullptr, world unchanged (asserted by
    //   rejected_factory_results_leave_world_unchanged)
    if (!w)
        return nullptr;

    // rule: Only a Living added to the object list counts toward the world's living census
    //   (living_count); weapons, FX, treasures and generators do not. Same census as remove_ob
    //   (below), the dead sweep (below) and retire_corpse (below).
    // tests: ScreenExtended.screen_add_ob_living
    // tests: SemWorld.only_a_living_added_to_the_object_list_counts_as_living
    if (count_living)
        living_count++;

    auto& entries = target_list.raw_mutable();
    walker* raw = w.get();
    assign_entity_id(*raw);
    refresh_self_reference_ids(*raw);
    raw->mark_all_dirty();
    // rule: A walker added with atstart goes to the FRONT of its list, so it acts first on the next
    //   tick and is found first by list-order scans; the default appends it, acting last.
    // tests: ScreenExtended.screen_add_ob_treasure, ScreenExtended.screen_add_ob_living
    if (atstart)
        entries.push_front(std::move(w));
    else
        entries.push_back(std::move(w));
    index_entity(*raw);
    return raw;
}

walker* GameWorld::add_ob(Order order, std::int32_t family, bool atstart)
{
    // rule: add_ob files a Weapon-order walker in the weapon list (it acts in the weapon phase and
    //   is never counted as a living); every other order goes to the object list. Only sim-authored
    //   Livings and Generators fire the level script's on_entity_spawn hook; weapons, FX and
    //   treasures never do (snapshot/replay insertion bypasses add_ob and stays silent).
    // tests: ScreenExtended.screen_add_ob_weapon, ScreenExtended.screen_add_ob_effect
    // tests: ScreenExtended.screen_add_ob_treasure
    // tests: LevelScriptsTest.fx_spawns_do_not_fire_entity_spawn
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: SemAddOb.add_ob_files_weapons_apart_and_only_livings_fire_the_spawn_hook
    if (order == Order::Weapon)
        return add_to_list(order, family, weaplist, false, atstart);

    walker* added =
        add_to_list(order, family, oblist, order == Order::Living, atstart);
    // Level-script on_entity_spawn: sim-authored livings/generators only
    // (snapshot/replay insertion paths bypass add_ob and stay silent).
    // rule: add_ob files a Weapon-order walker in the weapon list (it acts in the weapon phase and
    //   is never counted as a living); every other order goes to the object list. Here: `added !=
    //   nullptr && (order == Living || order == Generator)`.
    // tests: ScreenExtended.screen_add_ob_weapon, ScreenExtended.screen_add_ob_effect
    // tests: ScreenExtended.screen_add_ob_treasure
    // tests: LevelScriptsTest.fx_spawns_do_not_fire_entity_spawn
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: SemAddOb.add_ob_files_weapons_apart_and_only_livings_fire_the_spawn_hook
    if (added != nullptr &&
        (order == Order::Living || order == Order::Generator))
        og::script::hooks::level_entity_spawn(added);
    return added;
}

walker* GameWorld::add_fx_ob(Order order, std::int32_t family)
{
    return add_to_list(order, family, fxlist, false, false);
}

walker* GameWorld::add_weap_ob(Order order, std::int32_t family)
{
    return add_to_list(order, family, weaplist, false, false);
}

short GameWorld::remove_ob(walker* ob)
{
    auto pred = [ob](const std::unique_ptr<walker>& p) { return p.get() == ob; };

    // most common case
    auto& weapons = weaplist.raw_mutable();
    auto e = std::find_if(weapons.begin(), weapons.end(), pred);
    // rule: remove_ob removes a walker from whichever live list holds it (weapons, then effects,
    //   then objects) and reports 1; a walker found in none reports 0. Removing a Living from the
    //   object list lowers the living census, never below zero (same census as add_to_list).
    // tests: GameWorldEntityIdsFixture.remove_ob_erases_entities_from_id_index
    // tests: ScreenExtended.screen_add_ob_living
    // tests: LevelDataCoverage.level_data_round7a_constructor_overloads_and_remove_paths
    // tests: LevelDataUnit.level_data_r11_basic_construction_remove_and_helpers
    // tests: SemWorld.removing_a_living_never_drops_the_living_census_below_zero
    if (e != weapons.end())
    {
        remove_from_id_index(e->get());
        weapons.erase(e);
        return 1;
    }

    auto& effects = fxlist.raw_mutable();
    auto f = std::find_if(effects.begin(), effects.end(), pred);
    // rule: remove_ob removes a walker from whichever live list holds it (weapons, then effects,
    //   then objects) and reports 1; a walker found in none reports 0. Here: `f != effects.end()`.
    // tests: GameWorldEntityIdsFixture.remove_ob_erases_entities_from_id_index
    // tests: ScreenExtended.screen_add_ob_living
    // tests: LevelDataCoverage.level_data_round7a_constructor_overloads_and_remove_paths
    // tests: LevelDataUnit.level_data_r11_basic_construction_remove_and_helpers
    // tests: SemWorld.removing_a_living_never_drops_the_living_census_below_zero
    if (f != effects.end())
    {
        remove_from_id_index(f->get());
        effects.erase(f);
        return 1;
    }

    auto& objects = oblist.raw_mutable();
    auto g = std::find_if(objects.begin(), objects.end(), pred);
    // rule: remove_ob removes a walker from whichever live list holds it (weapons, then effects,
    //   then objects) and reports 1; a walker found in none reports 0. Here: `g != objects.end()`.
    // tests: GameWorldEntityIdsFixture.remove_ob_erases_entities_from_id_index
    // tests: ScreenExtended.screen_add_ob_living
    // tests: LevelDataCoverage.level_data_round7a_constructor_overloads_and_remove_paths
    // tests: LevelDataUnit.level_data_r11_basic_construction_remove_and_helpers
    // tests: SemWorld.removing_a_living_never_drops_the_living_census_below_zero
    if (g != objects.end())
    {
        // rule: remove_ob removes a walker from whichever live list holds it (weapons, then
        //   effects, then objects) and reports 1; a walker found in none reports 0. Here: `ob &&
        //   Living && living_count > 0`.
        // tests: GameWorldEntityIdsFixture.remove_ob_erases_entities_from_id_index
        // tests: ScreenExtended.screen_add_ob_living
        // tests: LevelDataCoverage.level_data_round7a_constructor_overloads_and_remove_paths
        // tests: LevelDataUnit.level_data_r11_basic_construction_remove_and_helpers
        // tests: SemWorld.removing_a_living_never_drops_the_living_census_below_zero
        if (ob && ob->query_order() == Order::Living && living_count > 0)
            living_count--;
        remove_from_id_index(g->get());
        objects.erase(g);
        return 1;
    }

    return 0;
}

bool GameWorld::query_grid_passable(float x, float y, walker* ob)
{
    // plumbing: query_grid_passable overload defaults the floor to ob->floor()
    return query_grid_passable(x, y, ob, ob != nullptr ? ob->floor() : 0);
}

bool GameWorld::query_grid_passable(float x, float y, walker* ob, int floor, bool quiet)
{
    // plumbing: null walker is never passable
    if (ob == nullptr)
        return false;

    const PixieData& fg = grid_for_floor(floor);

    std::int32_t xtrax = 1;
    std::int32_t xtray = 1;
    const std::int32_t x_i = static_cast<std::int32_t>(x);
    const std::int32_t y_i = static_cast<std::int32_t>(y);
    const std::int32_t xover = x_i + ob->sizex();
    const std::int32_t yover = y_i + ob->sizey();

    // rule: No walker may stand where its box leaves the map: a negative corner, or a far edge
    //   reaching the map's last pixel column/row (xover = x+sizex >= pixmax), is impassable,
    //   checked before ethereal so even ethereal walkers stay on the map.
    // tests: ScreenFuncs.screen_query_grid_passable_out_of_bounds
    // tests: CoverageMisc.coverage_r18_walker_movement_blocked_user_paths
    // tests: LevelDataUnit.level_data_passable_and_range_queries
    if (x_i < 0 || y_i < 0 || xover >= pixmaxx || yover >= pixmaxy)
        return false;

    // Are we ethereal?
    // rule: An ethereal walker passes every terrain tile (walls, water, trees, decor) once it is on
    //   the map.
    // tests: ScreenFuncs.screen_query_grid_passable_flying
    // tests: ScreenFuncs.screen_query_grid_passable_center
    if (ob->stats()->query_bit_flags(BIT_ETHEREAL))
        return true;

    // Zardus: PORT: Does the grid exist?
    // rule: A floor with no grid loaded is impassable to everyone except ethereal walkers (ethereal
    //   is checked first).
    // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
    // tests: LevelDataUnit.level_data_r11_query_grid_passable_terrain_branches
    // tests: SemWorld.an_ethereal_walker_passes_a_floor_with_no_grid
    if (!fg.valid())
        return false;

    // Decor plane consult (core/decordefs.h) is gated on validity + matching
    // dims (the loader drops mismatched planes; the dim check also makes the
    // per-cell index provably in-bounds). No-decor levels pay one valid()
    // check and take identical control flow — parity-byte-identical.
    const PixieData& dec = decor_for_floor(floor);
    const bool has_decor =
        dec.valid() && dec.w == fg.w && dec.h == fg.h;

    // Check if our butt hangs over shorto next grid square
    // rule: A walker's box occupies every grid cell it overlaps; a box whose far edge lies exactly
    //   on a cell boundary does not touch the next cell, so a 16x16 walker aligned to a cell tests
    //   only that cell.
    // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
    // tests: SemWorld.a_box_ending_on_a_cell_boundary_does_not_touch_the_next_cell
    if ((xover % GRID_SIZE) == 0)
        xtrax = 0;
    if ((yover % GRID_SIZE) == 0)
        xtray = 0;

    // Check grid squares by simulated grid coords.
    const std::int32_t xtarg = (xover / GRID_SIZE) + xtrax;
    const std::int32_t ytarg = (yover / GRID_SIZE) + xtray;

    // plumbing: per-cell scan of the box; see below and the tile rows that follow
    for (std::int32_t i = x_i / GRID_SIZE; i < xtarg; ++i)
    {
        // plumbing: per-cell scan of the box; see below and the tile rows that follow
        for (std::int32_t j = y_i / GRID_SIZE; j < ytarg; ++j)
        {
            // rule: Ground tiles (every grass, dark/light grass edge, grass-water corner, pavement,
            //   cobble, floor paving, pave steps, carpet, dirt and path byte) are open to every
            //   walker of every order.
            // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
            // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
            // tests: SemWorld.every_ground_tile_is_open_to_a_plain_walker
            switch (static_cast<unsigned char>(fg.data[static_cast<std::size_t>(i + fg.w * j)]))
            {
                case PIX_GRASS1:
                case PIX_GRASS2:
                case PIX_GRASS3:
                case PIX_GRASS4:
                case PIX_GRASS_DARK_1:
                case PIX_GRASS_DARK_2:
                case PIX_GRASS_DARK_3:
                case PIX_GRASS_DARK_4:
                case PIX_GRASS_DARK_LL:
                case PIX_GRASS_DARK_UR:
                case PIX_GRASS_DARK_B1:
                case PIX_GRASS_DARK_B2:
                case PIX_GRASS_DARK_BR:
                case PIX_GRASS_DARK_R1:
                case PIX_GRASS_DARK_R2:
                case PIX_GRASS_RUBBLE:
                case PIX_GRASS1_DAMAGED:
                case PIX_GRASS_LIGHT_1:
                case PIX_GRASS_LIGHT_TOP:
                case PIX_GRASS_LIGHT_RIGHT_TOP:
                case PIX_GRASS_LIGHT_RIGHT:
                case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
                case PIX_GRASS_LIGHT_BOTTOM:
                case PIX_GRASS_LIGHT_LEFT_BOTTOM:
                case PIX_GRASS_LIGHT_LEFT:
                case PIX_GRASS_LIGHT_LEFT_TOP:
                case PIX_GRASSWATER_LL:
                case PIX_GRASSWATER_LR:
                case PIX_GRASSWATER_UL:
                case PIX_GRASSWATER_UR:
                case PIX_PAVEMENT1:
                case PIX_PAVEMENT2:
                case PIX_PAVEMENT3:
                case PIX_COBBLE_1:
                case PIX_COBBLE_2:
                case PIX_COBBLE_3:
                case PIX_COBBLE_4:
                case PIX_FLOOR_PAVEL:
                case PIX_FLOOR_PAVER:
                case PIX_FLOOR_PAVEU:
                case PIX_FLOOR_PAVED:
                case PIX_PAVESTEPS1:
                case PIX_PAVESTEPS2:
                case PIX_PAVESTEPS2L:
                case PIX_PAVESTEPS2R:
                case PIX_FLOOR1:
                case PIX_CARPET_LL:
                case PIX_CARPET_B:
                case PIX_CARPET_LR:
                case PIX_CARPET_UR:
                case PIX_CARPET_U:
                case PIX_CARPET_UL:
                case PIX_CARPET_L:
                case PIX_CARPET_M:
                case PIX_CARPET_M2:
                case PIX_CARPET_R:
                case PIX_CARPET_SMALL_HOR:
                case PIX_CARPET_SMALL_VER:
                case PIX_CARPET_SMALL_CUP:
                case PIX_CARPET_SMALL_CAP:
                case PIX_CARPET_SMALL_LEFT:
                case PIX_CARPET_SMALL_RIGHT:
                case PIX_CARPET_SMALL_TINY:
                case PIX_DIRT_1:
                case PIX_DIRTGRASS_UL1:
                case PIX_DIRTGRASS_UR1:
                case PIX_DIRTGRASS_LL1:
                case PIX_DIRTGRASS_LR1:
                case PIX_DIRT_DARK_1:
                case PIX_DIRTGRASS_DARK_UL1:
                case PIX_DIRTGRASS_DARK_UR1:
                case PIX_DIRTGRASS_DARK_LL1:
                case PIX_DIRTGRASS_DARK_LR1:
                case PIX_PATH_1:
                case PIX_PATH_2:
                case PIX_PATH_3:
                case PIX_PATH_4:
                    break;
                // trees are usually bad, but we can fly over them
                // rule: Tree crowns block ground walkers; a forest-walker passes them, and so does
                //   a permanent flyer or a walker with temporary flight left. Weapons are NOT
                //   exempt from crowns (only the trunk lets them through, below).
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.tree_crowns_pass_forestwalkers_and_flyers_only
                case PIX_TREE_M1:
                // rule: Tree crowns block ground walkers; a forest-walker passes them, and so does
                //   a permanent flyer or a walker with temporary flight left.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.tree_crowns_pass_forestwalkers_and_flyers_only
                case PIX_TREE_ML:
                // rule: Tree crowns block ground walkers; a forest-walker passes them, and so does
                //   a permanent flyer or a walker with temporary flight left.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.tree_crowns_pass_forestwalkers_and_flyers_only
                case PIX_TREE_MR:
                // rule: Tree crowns block ground walkers; a forest-walker passes them, and so does
                //   a permanent flyer or a walker with temporary flight left.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.tree_crowns_pass_forestwalkers_and_flyers_only
                case PIX_TREE_MT:
                // rule: Tree crowns block ground walkers; a forest-walker passes them, and so does
                //   a permanent flyer or a walker with temporary flight left.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.tree_crowns_pass_forestwalkers_and_flyers_only
                case PIX_TREE_T1:
                    // rule: Tree crowns block ground walkers; a forest-walker passes them, and so
                    //   does a permanent flyer or a walker with temporary flight left.
                    // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                    // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                    // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
                    // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                    // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                    // tests: SemWorld.tree_crowns_pass_forestwalkers_and_flyers_only
                    if (ob->stats()->query_bit_flags(BIT_FORESTWALK))
                        break;
                    // rule: Tree crowns block ground walkers; a forest-walker passes them, and so
                    //   does a permanent flyer or a walker with temporary flight left.
                    // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                    // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                    // tests: LevelDataCoverage.level_data_query_grid_passable_edge_cases
                    // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                    // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                    // tests: SemWorld.tree_crowns_pass_forestwalkers_and_flyers_only
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    return false;
                // rule: A tree trunk blocks ground walkers but lets weapons fly through, as well as
                //   forest-walkers and flyers (permanent or temporary).
                // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                // tests: SemWorld.a_tree_trunk_passes_weapons_forestwalkers_and_flyers
                case PIX_TREE_B1:
                    // rule: A tree trunk blocks ground walkers but lets weapons fly through, as
                    //   well as forest-walkers and flyers (permanent or temporary).
                    // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                    // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                    // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                    // tests: SemWorld.a_tree_trunk_passes_weapons_forestwalkers_and_flyers
                    if (ob->query_order() == Order::Weapon ||
                        ob->stats()->query_bit_flags(BIT_FORESTWALK))
                    {
                        break;
                    }
                    // rule: A tree trunk blocks ground walkers but lets weapons fly through, as
                    //   well as forest-walkers and flyers (permanent or temporary).
                    // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                    // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                    // tests: LevelDataCoverage.level_data_round13_grid_passability_tree_wall_water_and_object_guards
                    // tests: SemWorld.a_tree_trunk_passes_weapons_forestwalkers_and_flyers
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    return false;

                // walls bad, but we can "ethereal" through them by default
                // rule: Solid walls block everything that is not ethereal: ground walkers, flyers,
                //   swimmers and weapons alike.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
                // tests: ScreenFuncs.screen_query_grid_passable_flying
                // tests: Decor.decor_never_grants_passage
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.solid_walls_stop_everything_not_ethereal
                case PIX_H_WALL1:
                // rule: Solid walls block everything that is not ethereal: ground walkers, flyers,
                //   swimmers and weapons alike.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
                // tests: ScreenFuncs.screen_query_grid_passable_flying
                // tests: Decor.decor_never_grants_passage
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.solid_walls_stop_everything_not_ethereal
                case PIX_WALL2:
                // rule: Solid walls block everything that is not ethereal: ground walkers, flyers,
                //   swimmers and weapons alike.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
                // tests: ScreenFuncs.screen_query_grid_passable_flying
                // tests: Decor.decor_never_grants_passage
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.solid_walls_stop_everything_not_ethereal
                case PIX_WALL3:
                // rule: Solid walls block everything that is not ethereal: ground walkers, flyers,
                //   swimmers and weapons alike.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
                // tests: ScreenFuncs.screen_query_grid_passable_flying
                // tests: Decor.decor_never_grants_passage
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.solid_walls_stop_everything_not_ethereal
                case PIX_WALL_LL:
                // rule: Solid walls block everything that is not ethereal: ground walkers, flyers,
                //   swimmers and weapons alike.
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: WalkerMovementUnit.walker_movement_r12_stationary_slope_and_animate_paths
                // tests: ScreenFuncs.screen_query_grid_passable_flying
                // tests: Decor.decor_never_grants_passage
                // tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
                // tests: SemWorld.solid_walls_stop_everything_not_ethereal
                case PIX_WALLTOP_H:
                    return false;

                // Water is surface terrain: projectiles and airborne walkers
                // cross it as before, while explicitly swimming families can
                // now move through it without gaining passage through any of
                // the solid obstacle cases below.
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it. Swimming grants
                //   water only, never obstacles or lava.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATER1:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATER2:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATER3:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_LL:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_LR:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_UL:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_UR:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_U:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_L:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_R:
                // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                //   temporary) cross it, and a swimming family can wade through it.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                case PIX_WATERGRASS_D:
                    // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                    //   temporary) cross it, and a swimming family can wade through it.
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                    if (ob->query_order() == Order::Weapon)
                        break;
                    // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                    //   temporary) cross it, and a swimming family can wade through it.
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    // rule: Water blocks ground walkers; weapons fly over it, flyers (permanent or
                    //   temporary) cross it, and a swimming family can wade through it.
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                    if (ob->stats()->query_bit_flags(BIT_SWIMMING))
                        break;
                    return false;

                // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                //   living outright, flyers included. A non-living with no owner is blocked too. An
                //   owned missile gets through by a gamble that worsens with its distance from its
                //   shooter along the dominant axis: dist = |delta| - 8, raised by 16 when under
                //   16, and the slit blocks when rng.next(dist/16) is non-zero (adjacent shots
                //   always pass). A quiet probe skips the gamble and treats the slit as open (the
                //   loud answer at fire-pad range). Passing the gamble falls through to the
                //   obstacle rule (below): weapons pass, a non-weapon owned walker passes only
                //   flying.
                // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                case PIX_WALL4:
                // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                //   living outright, flyers included.
                // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                case PIX_WALL5:
                // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                //   living outright, flyers included.
                // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                case PIX_WALL_ARROW_GRASS:
                // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                //   living outright, flyers included.
                // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                case PIX_WALL_ARROW_FLOOR:
                // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                //   living outright, flyers included.
                // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                case PIX_WALL_ARROW_GRASS_DARK:
                    // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                    //   living outright, flyers included.
                    // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                    // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                    // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                    // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                    // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                    // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                    if (ob->query_order() == Order::Living)
                        return false;

                    // Authored non-living entities (e.g. reserved-team
                    // Specials wandering via ACT_RANDOM) path with no owner;
                    // the shooter-distance gamble below is meaningless
                    // without one, so the arrow wall stays solid for them.
                    // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                    //   living outright, flyers included.
                    // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                    // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                    // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                    // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                    // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                    // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                    if (ob->owner() == nullptr)
                        return false;

                    // A quiet probe never rolls the gamble and treats the slit
                    // as passable. That is the loud answer where a quiet weapon
                    // probe is made today (walker::fire_pad_clear: the pad one
                    // pixel outside a shooter up to 38 px a side gives dist /
                    // GRID_SIZE 0 or 1 below, and rng_.next(0) and rng_.next(1)
                    // both answer 0). A golem (48x36) or giant skeleton (64x64)
                    // pad can reach a bound of 2, where the loud roll blocks half.
                    // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block every
                    //   living outright, flyers included.
                    // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                    // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                    // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                    // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                    // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                    // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                    if (!quiet)
                    {
                        // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes) block
                        //   every living outright, flyers included.
                        // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                        // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                        // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                        // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                        // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                        // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                        if (std::abs(ob->xpos() - ob->owner()->xpos()) >
                            std::abs(ob->ypos() - ob->owner()->ypos()))
                        {
                            std::int32_t dist = std::abs(ob->xpos() - ob->owner()->xpos());
                            dist -= (GRID_SIZE / 2);
                            // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes)
                            //   block every living outright, flyers included.
                            // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                            // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                            // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                            // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                            // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                            // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                            if (dist < GRID_SIZE)
                                dist += GRID_SIZE;
                            // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes)
                            //   block every living outright, flyers included.
                            // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                            // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                            // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                            // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                            // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                            // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                            if (rng_.next(static_cast<std::uint32_t>(dist / GRID_SIZE)))
                                return false;
                        }
                        else
                        {
                            std::int32_t dist = std::abs(ob->ypos() - ob->owner()->ypos());
                            dist -= (GRID_SIZE / 2);
                            // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes)
                            //   block every living outright, flyers included.
                            // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                            // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                            // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                            // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                            // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                            // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                            if (dist < GRID_SIZE)
                                dist += GRID_SIZE;
                            // rule: Arrow-slit walls (WALL4, WALL5, the three WALL_ARROW bytes)
                            //   block every living outright, flyers included.
                            // tests: LevelDataCoverage.level_data_round7_wall_arrow_distance_axis_and_rng_paths
                            // tests: LevelDataCoverage.level_data_wall4_projectile_passability_distance_and_rng_paths
                            // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                            // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                            // tests: SimWorldHeadless.arrow_wall_is_solid_for_unowned_special
                            // tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
                            if (rng_.next(static_cast<std::uint32_t>(dist / GRID_SIZE)))
                                return false;
                        }
                    }
                    [[fallthrough]];
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers; weapons fly over them and
                //   flyers (permanent or temporary) cross them.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_WALLSIDE_L:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_WALLSIDE1:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_WALLSIDE_R:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_WALLSIDE_C:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_WALLSIDE_CRACK_C1:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_TORCH1:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_TORCH2:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_TORCH3:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_BRAZIER1:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_COLUMN1:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_COLUMN2:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_BOULDER_1:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_BOULDER_2:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_BOULDER_3:
                // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles) block
                //   ground walkers including swimmers and forest-walkers.
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                case PIX_BOULDER_4:
                    // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles)
                    //   block ground walkers including swimmers and forest-walkers.
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                    // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                    // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                    if (ob->query_order() == Order::Weapon)
                        break;
                    // rule: Low obstacles (wall sides, torches, braziers, columns, boulder tiles)
                    //   block ground walkers including swimmers and forest-walkers.
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    // tests: LevelDataCoverage.level_data_round6_wrapper_and_passability_edges
                    // tests: MassCoverage.screen_query_grid_passable_reads_the_tile_under_the_walker
                    // tests: SemWorld.low_obstacles_stop_ground_walkers_and_pass_weapons_and_flyers
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    return false;
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone; falling, drop direction and floor
                //   change are movement rules (apply_vertical), not passability.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_AIR:
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_GLASS:
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_DROPBLOCK_UP:
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_DROPBLOCK_RIGHT:
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_DROPBLOCK_DOWN:
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_DROPBLOCK_LEFT:
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_ZSTAIR_UP:
                // rule: Multi-floor bytes (air, glass, the four drop blocks, stair up/down) are
                //   walkable at the grid layer for everyone.
                // tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
                // tests: SemWorld.the_multi_floor_tiles_are_walkable_at_the_grid_layer
                case PIX_ZSTAIR_DOWN:
                    // Walkable at the grid layer. Air's fall, drop-block
                    // direction, and stair floor-change are handled in movement
                    // (apply_vertical); pathing avoids air for non-flyers
                    // separately. Single-floor grids never contain these bytes,
                    // so legacy levels take identical control flow.
                    break;
                // rule: Snow, marsh and ash are plain walkable ground.
                // tests: NewTiles.snow_marsh_ash_are_plain_walkable_ground
                case PIX_SNOW1:
                // rule: Snow, marsh and ash are plain walkable ground.
                // tests: NewTiles.snow_marsh_ash_are_plain_walkable_ground
                case PIX_SNOW2:
                // rule: Snow, marsh and ash are plain walkable ground.
                // tests: NewTiles.snow_marsh_ash_are_plain_walkable_ground
                case PIX_MARSH1:
                // rule: Snow, marsh and ash are plain walkable ground.
                // tests: NewTiles.snow_marsh_ash_are_plain_walkable_ground
                case PIX_MARSH2:
                // rule: Snow, marsh and ash are plain walkable ground.
                // tests: NewTiles.snow_marsh_ash_are_plain_walkable_ground
                case PIX_ASH1:
                // rule: Snow, marsh and ash are plain walkable ground.
                // tests: NewTiles.snow_marsh_ash_are_plain_walkable_ground
                case PIX_ASH2:
                    break; // plain walkable ground
                // rule: Lava is solid to ground walkers (swimmers included); projectiles fly over
                //   it and flyers cross it.
                // tests: NewTiles.lava_is_solid_to_ground_but_flyer_and_weapon_pass
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                case PIX_LAVA1:
                // rule: Lava is solid to ground walkers (swimmers included); projectiles fly over
                //   it and flyers cross it.
                // tests: NewTiles.lava_is_solid_to_ground_but_flyer_and_weapon_pass
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                case PIX_LAVA2:
                    // Molten: solid to ground walkers; projectiles fly over
                    // and flyers cross. Swimming does not grant lava access.
                    // rule: Lava is solid to ground walkers (swimmers included); projectiles fly
                    //   over it and flyers cross it.
                    // tests: NewTiles.lava_is_solid_to_ground_but_flyer_and_weapon_pass
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    if (ob->query_order() == Order::Weapon)
                        break;
                    // rule: Lava is solid to ground walkers (swimmers included); projectiles fly
                    //   over it and flyers cross it.
                    // tests: NewTiles.lava_is_solid_to_ground_but_flyer_and_weapon_pass
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    return false;
                // rule: Any tile byte the game does not know is solid to everyone (fail closed).
                // tests: LevelDataCoverage.level_data_round5_query_grid_passable_contiguous_block_paths
                // tests: LevelDataCoverage.level_data_round8_query_grid_treeb1_and_arrow_slit_variants
                // tests: LevelDataUnit.level_data_r12_save_null_entries_and_query_passable_branches
                default:
                    return false;
            }

            // Decor consultation, immediately AFTER the base switch so it
            // only runs for cells whose base tile did not already return
            // false. Decor only RESTRICTS (never grants passage) and consumes
            // no RNG; the arrow-wall gamble above is unreachable for decor
            // cells (the mapping never touches arrow walls). The id is
            // bounds-gated before subscripting the registry (plane bytes are
            // file data; the loader clamps, this guards hostile planes).
            // rule: Decor on a cell can only restrict passage, never grant it: decor with no
            //   passability rule (DecorPassability::None) changes nothing, and BlocksGround decor
            //   stops ground walkers (swimmers included) while weapons and flyers pass; those are
            //   the only two kinds (kDecorRegistry, decordefs.h). Out-of-range decor ids and a
            //   plane whose size mismatches the grid are ignored.
            // tests: Decor.blocks_ground_lets_weapons_and_flyers_pass
            // tests: Decor.decor_never_grants_passage
            // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
            // tests: SemWorld.a_mismatched_decor_plane_or_unknown_decor_id_is_ignored
            if (has_decor)
            {
                const unsigned char d =
                    dec.data[static_cast<std::size_t>(i + dec.w * j)];
                // rule: Decor on a cell can only restrict passage, never grant it: a cell with
                //   no decor (DECOR_NONE) and an id at or past DECOR_MAX change nothing.
                // tests: Decor.blocks_ground_lets_weapons_and_flyers_pass
                // tests: Decor.decor_never_grants_passage
                // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                // tests: SemWorld.a_mismatched_decor_plane_or_unknown_decor_id_is_ignored
                if (d != DECOR_NONE && d < DECOR_MAX)
                {
                    // rule: Decor on a cell can only restrict passage, never grant it: None
                    //   decor changes nothing, BlocksGround decor stops a ground walker.
                    // tests: Decor.blocks_ground_lets_weapons_and_flyers_pass
                    // tests: Decor.decor_never_grants_passage
                    // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                    // tests: SemWorld.a_mismatched_decor_plane_or_unknown_decor_id_is_ignored
                    switch (kDecorRegistry[d].pass)
                    {
                        case DecorPassability::None:
                            break;
                        case DecorPassability::BlocksGround:
                            // Exactly the legacy obstacle arm: weapons pass,
                            // flyers (permanent or temporary) pass, ground
                            // walkers (including swimmers) are blocked.
                            // rule: Decor on a cell can only restrict passage, never grant it: a
                            //   weapon crosses BlocksGround decor.
                            // tests: Decor.blocks_ground_lets_weapons_and_flyers_pass
                            // tests: Decor.decor_never_grants_passage
                            // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                            // tests: SemWorld.a_mismatched_decor_plane_or_unknown_decor_id_is_ignored
                            if (ob->query_order() == Order::Weapon)
                                break;
                            // rule: Decor on a cell can only restrict passage, never grant it: a
                            //   flyer (permanent or temporary flight) crosses BlocksGround decor.
                            // tests: Decor.blocks_ground_lets_weapons_and_flyers_pass
                            // tests: Decor.decor_never_grants_passage
                            // tests: LevelDataCoverage.swimming_grants_water_passage_but_not_obstacle_passage
                            // tests: SemWorld.a_mismatched_decor_plane_or_unknown_decor_id_is_ignored
                            if (ob->stats()->query_bit_flags(BIT_FLYING) ||
                                ob->flight_left())
                                break;
                            return false;
                    }
                }
            }
        }
    }

    return true;
}

bool GameWorld::query_object_passable(float x, float y, walker* ob)
{
    // plumbing: query_object_passable overload defaults floor
    return query_object_passable(x, y, ob, ob != nullptr ? ob->floor() : 0);
}

bool GameWorld::query_object_passable(float x, float y, walker* ob, int floor, bool quiet, bool weapons_block)
{
    // plumbing: null walker -> not passable
    if (ob == nullptr)
        return false;

    // rule: A dead walker never collides with anything: its object check always passes (and so
    //   never eats treasure or fires collide side effects).
    // tests: LevelDataCoverage.level_data_round16_remaining_foes_and_object_passable_collision_paths
    // tests: MassCoverage.screen_query_object_passable_reads_obmap_occupancy
    // tests: SemWorld.a_dead_walker_never_collides
    if (ob->dead())
        return true;

    obmap* om = obmap_for_floor(floor);
    // plumbing: no obmap for the floor -> not passable (asserted in
    //   boundary_queries_fail_closed_without_side_effects)
    if (!om)
        return false;

    // NOTE: obmap::query_list keys its floor band off ob->floor(), NOT the
    // `floor` argument, so this overload only correctly probes ob->floor()'s
    // occupancy. Every caller passes floor == ob->floor() (the 3-arg delegate,
    // query_passable, the Lua probes since #465), so the two agree. A genuine
    // cross-floor probe must NOT use this path — the cross-floor helpers
    // (floor_landing_clear / teleport_landing_clear / classic_spot_clear) do
    // their own manual obmap sweep at an explicit floor instead.
    return om->query_list(ob, static_cast<short>(x), static_cast<short>(y), quiet, weapons_block);
}

bool GameWorld::query_passable(float x, float y, walker* ob)
{
    // plumbing: query_passable overload defaults floor
    return query_passable(x, y, ob, ob != nullptr ? ob->floor() : 0);
}

bool GameWorld::query_passable(float x, float y, walker* ob, int floor, bool quiet)
{
    return query_grid_passable(x, y, ob, floor, quiet) &&
           query_object_passable(x, y, ob, floor, quiet);
}

namespace {

// 2026-07-10 guard facing gate (docs/GAMEPLAY_FIXES_FROM_CLASSIC.md).
// Sight-opaque grid bytes: exactly the tiles an UNOWNED Order::Weapon probe
// can never pass in query_grid_passable — tree crowns/trunks (the base
// PIX_TREE_B1 lets weapons through) and the wall faces (the unconditional
// walls plus the arrow-wall family, which is solid for owner-less probes;
// classifying it opaque here avoids the shooter-distance RNG gamble
// entirely). Everything weapons fly over — water, lava, boulders, columns,
// torches, wallside cells — is sight-transparent. Unknown bytes default to
// TRANSPARENT: the gate is facing-only, so an exotic tile keeps the classic
// always-face behavior instead of freezing a guard.
bool sight_opaque_tile(unsigned char tile)
{
    // rule: Sight is blocked by exactly the tiles an unowned missile can never cross: tree crowns
    //   (TREE_M1/ML/MR/MT/T1, not the trunk B1), solid walls and arrow-slit walls. Water, lava,
    //   boulders, columns, torches, wall sides and unknown bytes are transparent (an unknown tile
    //   keeps the classic always-face behaviour). Used by clear_sight_line (below) for the guard
    //   facing gate.
    // tests: KitFaerie.swap_trades_places_only_in_line_of_sight
    // tests: ScriptBindingPropsTest.kit_line_clear_is_blocked_by_a_wall_and_by_floors
    // tests: GuardFacing.guard_does_not_pivot_to_face_foe_through_tree_band
    // tests: GuardFacing.guard_faces_foe_across_water
    // tests: SemWorld.crowns_walls_and_arrow_walls_block_sight
    // tests: SemWorld.trunks_water_lava_and_low_obstacles_do_not_block_sight
    switch (tile)
    {
        case PIX_TREE_M1:
        case PIX_TREE_ML:
        case PIX_TREE_MR:
        case PIX_TREE_MT:
        case PIX_TREE_T1:
        case PIX_H_WALL1:
        case PIX_WALL2:
        case PIX_WALL3:
        case PIX_WALL_LL:
        case PIX_WALLTOP_H:
        case PIX_WALL4:
        case PIX_WALL5:
        case PIX_WALL_ARROW_GRASS:
        case PIX_WALL_ARROW_FLOOR:
        case PIX_WALL_ARROW_GRASS_DARK:
            return true;
        default:
            return false;
    }
}

} // namespace

bool GameWorld::clear_sight_line(const walker* from, const walker* to)
{
    // plumbing: null endpoint -> no sight (asserted in
    //   boundary_queries_fail_closed_without_side_effects)
    if (from == nullptr || to == nullptr)
        return false;
    // rule: Two walkers see each other only on the same floor, along a cell-Bresenham line between
    //   their centre cells; only the cells strictly between the endpoints are tested (each walker's
    //   own cell never blocks), an opaque tile (above) or a step off the grid breaks the line, and
    //   a world with no grid never occludes.
    // tests: ScriptBindingPropsTest.kit_line_clear_is_blocked_by_a_wall_and_by_floors
    // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
    // tests: KitFaerie.swap_trades_places_only_in_line_of_sight
    // tests: SemWorld.walkers_in_the_same_or_adjacent_cells_always_see_each_other
    if (from->floor() != to->floor())
        return false;

    const PixieData& fg = grid_for_floor(from->floor());
    // rule: Two walkers see each other only on the same floor, along a cell-Bresenham line between
    //   their centre cells. Here: `!fg.valid()`.
    // tests: ScriptBindingPropsTest.kit_line_clear_is_blocked_by_a_wall_and_by_floors
    // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
    // tests: KitFaerie.swap_trades_places_only_in_line_of_sight
    // tests: SemWorld.walkers_in_the_same_or_adjacent_cells_always_see_each_other
    if (!fg.valid())
        return true; // no grid to occlude (synthetic test worlds)

    const int w = fg.w;
    const int h = fg.h;
    int x0 = (static_cast<int>(from->xpos()) + from->sizex() / 2) / GRID_SIZE;
    int y0 = (static_cast<int>(from->ypos()) + from->sizey() / 2) / GRID_SIZE;
    const int x1 = (static_cast<int>(to->xpos()) + to->sizex() / 2) / GRID_SIZE;
    const int y1 = (static_cast<int>(to->ypos()) + to->sizey() / 2) / GRID_SIZE;

    // Bresenham over cells, testing the intermediate cells only (both
    // endpoints are the walkers' own — necessarily walkable — cells).
    // plumbing: Bresenham setup (abs/sign ternaries) and stepping loop; see above
    const int adx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int ady = y1 > y0 ? y0 - y1 : y1 - y0; // -abs(dy)
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = adx + ady;

    // plumbing: Bresenham setup (abs/sign ternaries) and stepping loop; see above
    while (x0 != x1 || y0 != y1)
    {
        const int e2 = 2 * err;
        // plumbing: Bresenham setup (abs/sign ternaries) and stepping loop; see above
        if (e2 >= ady)
        {
            err += ady;
            x0 += sx;
        }
        // plumbing: Bresenham setup (abs/sign ternaries) and stepping loop; see above
        if (e2 <= adx)
        {
            err += adx;
            y0 += sy;
        }
        // rule: Two walkers see each other only on the same floor, along a cell-Bresenham line
        //   between their centre cells. Here: `endpoint reached`.
        // tests: ScriptBindingPropsTest.kit_line_clear_is_blocked_by_a_wall_and_by_floors
        // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
        // tests: KitFaerie.swap_trades_places_only_in_line_of_sight
        // tests: SemWorld.walkers_in_the_same_or_adjacent_cells_always_see_each_other
        if (x0 == x1 && y0 == y1)
            break;
        // rule: Two walkers see each other only on the same floor, along a cell-Bresenham line
        //   between their centre cells. Here: `off-grid cell`.
        // tests: ScriptBindingPropsTest.kit_line_clear_is_blocked_by_a_wall_and_by_floors
        // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
        // tests: KitFaerie.swap_trades_places_only_in_line_of_sight
        // tests: SemWorld.walkers_in_the_same_or_adjacent_cells_always_see_each_other
        if (x0 < 0 || y0 < 0 || x0 >= w || y0 >= h)
            return false;
        // rule: Two walkers see each other only on the same floor, along a cell-Bresenham line
        //   between their centre cells. Here: `sight_opaque_tile(cell)`.
        // tests: ScriptBindingPropsTest.kit_line_clear_is_blocked_by_a_wall_and_by_floors
        // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
        // tests: KitFaerie.swap_trades_places_only_in_line_of_sight
        // tests: SemWorld.walkers_in_the_same_or_adjacent_cells_always_see_each_other
        if (sight_opaque_tile(static_cast<unsigned char>(
                fg.data[static_cast<std::size_t>(x0) +
                        static_cast<std::size_t>(w) *
                            static_cast<std::size_t>(y0)])))
            return false;
    }
    return true;
}

void GameWorld::set_floor_count(int count)
{
    // rule: A level always has at least one floor; asking for fewer clamps to one, and shrinking
    //   drops the upper floors' grids.
    // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
    // tests: WalkerSpecialsUnit.teleport_reaches_all_floors_on_multifloor_levels
    // tests: ZAxis.collision_is_floor_separated
    // tests: SemWorld.shrinking_the_floor_count_drops_the_upper_floors
    if (count < 1)
        count = 1;
    const std::size_t extra = static_cast<std::size_t>(count - 1);
    if (extra < extra_floors_.size())
    {
        extra_floors_.resize(extra);
        return;
    }
    // plumbing: grow extra floors; see above
    while (extra_floors_.size() < extra)
    {
        ExtraFloor new_floor;
        new_floor.floor_smoother.set_rng(&rng_);
        extra_floors_.push_back(std::move(new_floor));
    }
}

walker* GameWorld::find_near_foe(walker* ob)
{
    // plumbing: null searcher -> nullptr + log
    if (!ob)
    {
        Log("no ob in find near foe.\n");
        return nullptr;
    }

    // rule: The quick foe search walks an outward square spiral of obmap cells (one obmap
    //   resolution per step, legs growing 1,1,2,2,... up to MAX_SPREAD 10) starting at the
    //   searcher's own position, on the searcher's own floor. If the spiral touches the map edge,
    //   is exhausted, or the world has no obmap, it falls back to the full floor-blind nearest-foe
    //   scan (below). So the answer is the first foe in spiral order, not necessarily the nearest.
    // tests: LevelDataCoverage.level_data_round6_find_near_foe_boundary_fallback_path
    // tests: ZAxis.find_near_foe_probes_the_searchers_floor
    // tests: SemWorld.the_quick_foe_search_answers_the_first_foe_in_spiral_order
    // tests: SemWorld.the_quick_foe_search_falls_back_when_the_spiral_is_exhausted
    // tests: SemWorld.the_quick_foe_search_without_an_obmap_answers_the_nearest
    if (!myobmap)
        return find_nearest_foe(ob);

    short targx = ob->xpos();
    short targy = ob->ypos();
    short spread = 1;
    short xchange = 0;
    short resolution = myobmap->obmapres;

    // plumbing: spiral legs and pile walk; see above and below
    while (spread < MAX_SPREAD)
    {
        // plumbing: spiral legs and pile walk; see above and below
        for (short loop = 0; loop < spread; ++loop)
        {
            // rule: The quick foe search walks an outward square spiral of obmap cells (one obmap
            //   resolution per step, legs growing 1,1,2,2,...
            // tests: LevelDataCoverage.level_data_round6_find_near_foe_boundary_fallback_path
            // tests: ZAxis.find_near_foe_probes_the_searchers_floor
            // tests: SemWorld.the_quick_foe_search_answers_the_first_foe_in_spiral_order
            // tests: SemWorld.the_quick_foe_search_falls_back_when_the_spiral_is_exhausted
            // tests: SemWorld.the_quick_foe_search_without_an_obmap_answers_the_nearest
            if ((xchange % 2) == 0)
            {
                targx += resolution;
                // rule: The quick foe search walks an outward square spiral of obmap cells (one
                //   obmap resolution per step, legs growing 1,1,2,2,...
                // tests: LevelDataCoverage.level_data_round6_find_near_foe_boundary_fallback_path
                // tests: ZAxis.find_near_foe_probes_the_searchers_floor
                // tests: SemWorld.the_quick_foe_search_answers_the_first_foe_in_spiral_order
                // tests: SemWorld.the_quick_foe_search_falls_back_when_the_spiral_is_exhausted
                // tests: SemWorld.the_quick_foe_search_without_an_obmap_answers_the_nearest
                if (targx <= 0 || targx >= pixmaxx)
                    return find_nearest_foe(ob);
            }
            else
            {
                targy += resolution;
                // rule: The quick foe search walks an outward square spiral of obmap cells (one
                //   obmap resolution per step, legs growing 1,1,2,2,...
                // tests: LevelDataCoverage.level_data_round6_find_near_foe_boundary_fallback_path
                // tests: ZAxis.find_near_foe_probes_the_searchers_floor
                // tests: SemWorld.the_quick_foe_search_answers_the_first_foe_in_spiral_order
                // tests: SemWorld.the_quick_foe_search_falls_back_when_the_spiral_is_exhausted
                // tests: SemWorld.the_quick_foe_search_without_an_obmap_answers_the_nearest
                if (targy <= 0 || targy >= pixmaxy)
                    return find_nearest_foe(ob);
            }

            // Probe the SEARCHER's floor. obmap_get_list's floor parameter
            // defaults to 0, so this spiral used to scan the ground floor's
            // piles no matter where `ob` stood: upper-floor walkers were
            // blind to foes standing beside them and latched whatever was on
            // the ground floor beneath their 2D position instead. Cross-floor
            // acquisition stays available via find_nearest_foe (the floor-blind
            // full-list scan). ob->floor()==0 on single-floor levels, so
            // legacy behavior is byte-identical.
            std::list<walker*>& ls =
                myobmap->obmap_get_list(targx, targy, ob->floor());
	            // plumbing: spiral legs and pile walk; see above and below
	            for (auto it = ls.begin(); it != ls.end(); )
            {
                walker* w = *it;
                // plumbing: an obmap pile entry no longer tracked by the world is erased and
                //   skipped (stale pointer guard)
                if (!tracks(w))
                {
                    it = ls.erase(it);
                    continue;
                }
                sanitize_owner_chain_link(*this, ob);
                sanitize_owner_chain_link(*this, w);
                // rule: In the quick search a candidate is acquired only if it is alive, not
                //   friendly to the searcher, a Living or a Generator (never a weapon, treasure or
                //   effect), and its cloak roll succeeds: invisibility_left/20 is the die size and
                //   only a 0 reveals it (0..19 always visible, negative clamped to visible). Same
                //   acquisition rule as find_nearest_foe (below).
                // tests: SimWorldHeadless.negative_invisibility_does_not_hide_a_foe_from_search
                // tests: SimWorldHeadless.foe_search_unchanged_for_non_negative_invisibility
                // tests: SemWorld.the_quick_foe_search_acquires_a_hostile_generator
                // tests: SemWorld.the_quick_foe_search_skips_friends_corpses_and_weapons
                // tests: SemWorld.the_quick_foe_search_skips_a_cloaked_foe_that_wins_its_roll
                if (!w->dead() && ob->is_friendly(w) == 0 &&
                    rng_.next(static_cast<std::uint32_t>(std::max<int>(w->invisibility_left(), 0) / 20)) == 0)  // clamp: invisibility_left is snapshot/Lua-writable and a negative would wrap to a ~4e9 next() bound, hiding the foe forever. No-op for every value >= 0.
                {
                    // rule: In the quick search a candidate is acquired only if it is alive, not
                    //   friendly to the searcher, a Living or a Generator (never a weapon, treasure
                    //   or effect), and its cloak roll succeeds. Here: `Living || Generator`.
                    // tests: SimWorldHeadless.negative_invisibility_does_not_hide_a_foe_from_search
                    // tests: SimWorldHeadless.foe_search_unchanged_for_non_negative_invisibility
                    // tests: SemWorld.the_quick_foe_search_acquires_a_hostile_generator
                    // tests: SemWorld.the_quick_foe_search_skips_friends_corpses_and_weapons
                    // tests: SemWorld.the_quick_foe_search_skips_a_cloaked_foe_that_wins_its_roll
                    if (w->query_order() == Order::Living ||
                        w->query_order() == Order::Generator)
                    {
                        return w;
                    }
                }
                ++it;
            }
        }

        // change whether we do x or y in each for loop
        ++xchange;
        // rule: The quick foe search walks an outward square spiral of obmap cells (one obmap
        //   resolution per step, legs growing 1,1,2,2,...
        // tests: LevelDataCoverage.level_data_round6_find_near_foe_boundary_fallback_path
        // tests: ZAxis.find_near_foe_probes_the_searchers_floor
        // tests: SemWorld.the_quick_foe_search_answers_the_first_foe_in_spiral_order
        // tests: SemWorld.the_quick_foe_search_falls_back_when_the_spiral_is_exhausted
        // tests: SemWorld.the_quick_foe_search_without_an_obmap_answers_the_nearest
        if ((xchange % 2) == 0)
        {
            // reverse direction around the search every other for
            resolution = static_cast<short>(-resolution);
            // increase the search width every other for
            ++spread;
        }
    }

    return find_nearest_foe(ob);
}

walker* GameWorld::find_nearest_foe(walker* ob)
{
    // plumbing: null searcher -> nullptr + log
    if (!ob)
    {
        Log("no ob in find nearest foe.\n");
        return nullptr;
    }

    // Set our 'default' foe to NULL
    walker* endfoe = nullptr;
    std::int32_t distance = 10000;
    ob->stats()->set_last_distance(10000);
    sanitize_owner_chain_link(*this, ob);

    // plumbing: oblist scan; see below
    for (auto& uptr : oblist)
    {
        walker* foe = uptr.get();
        // rule: The full foe search (also the fallback of find_near_foe and the per-tick foe force
        //   below) returns the nearest-by-distance walker in the object list that is alive,
        //   awake (not dormant), visible (not hidden), not friendly to the searcher, a Living or
        //   Generator, and wins its cloak roll; it is floor-blind and returns nothing when no foe
        //   qualifies. Ties keep the earlier walker in list order (strict <). It also resets the
        //   searcher's last_distance to 10000.
        // tests: LevelDataCoverage.level_data_round13_find_helpers_selection_and_filters
        // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
        // tests: SimWorldHeadless.foe_search_unchanged_for_non_negative_invisibility
        // tests: SemWorld.the_full_foe_scan_breaks_a_tie_by_list_order
        // tests: SemWorld.the_full_foe_scan_finds_a_foe_on_another_floor
        if (foe == nullptr || foe->dead() || foe->dormant() || foe->hidden())
            continue;

        sanitize_owner_chain_link(*this, foe);

        // rule: The full foe search (also the fallback of find_near_foe and the per-tick foe force
        //   below) returns the nearest-by-distance walker in the object list that is alive,
        //   awake (not dormant), visible (not hidden), not friendly to the searcher, a Living or
        //   Generator, and wins its cloak roll. Here: `is_friendly==0 && (Living||Generator) &&
        //   cloak roll`.
        // tests: LevelDataCoverage.level_data_round13_find_helpers_selection_and_filters
        // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
        // tests: SimWorldHeadless.foe_search_unchanged_for_non_negative_invisibility
        // tests: SemWorld.the_full_foe_scan_breaks_a_tie_by_list_order
        // tests: SemWorld.the_full_foe_scan_finds_a_foe_on_another_floor
        if (ob->is_friendly(foe) == 0 &&
            (foe->query_order() == Order::Living ||
             foe->query_order() == Order::Generator) &&
            (rng_.next(static_cast<std::uint32_t>(std::max<int>(foe->invisibility_left(), 0) / 20)) == 0))  // same clamp as find_near_foe: a negative cloak counter must not wrap into a ~4e9 next() bound.
        {
            const std::int32_t tempdistance = ob->distance_to_ob(foe);
            // rule: The full foe search (also the fallback of find_near_foe and the per-tick foe
            //   force below) returns the nearest-by-distance walker in the object list that is
            //   alive, awake (not dormant), visible (not hidden), not friendly to the searcher, a
            //   Living or Generator, and wins its cloak roll. Here: `tempdistance < distance`.
            // tests: LevelDataCoverage.level_data_round13_find_helpers_selection_and_filters
            // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
            // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
            // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
            // tests: SimWorldHeadless.foe_search_unchanged_for_non_negative_invisibility
            // tests: SemWorld.the_full_foe_scan_breaks_a_tie_by_list_order
            // tests: SemWorld.the_full_foe_scan_finds_a_foe_on_another_floor
            if (tempdistance < distance)
            {
                distance = tempdistance;
                endfoe = foe;
            }
        }
    }

    return endfoe;
}

// This can be slow, so don't call it much
walker* GameWorld::find_nearest_blood(walker* who)
{
    // plumbing: null -> nullptr
    if (!who)
        return nullptr;

    std::int32_t distance = 800;
    walker* returnob = nullptr;

    // plumbing: fxlist scan; see below
    for (auto& uptr : fxlist)
    {
        walker* w = uptr.get();
        // rule: The corpse finder returns the nearest live blood stain in the effect list on the
        //   finder's own floor whose squared centre distance from it is under 800 (about 28 px); a
        //   stain on another floor is never answered and never hides one on the finder's floor
        //   (#456); dead stains and non-stain treasures are ignored; ties keep fxlist order.
        // tests: QuietProbe.find_blood_in_reach_walks_the_finders_stains_nearest_first
        // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
        // tests: SemWorld.the_corpse_finder_reaches_only_under_squared_distance_800
        // tests: SemWorld.the_corpse_finder_reaches_only_a_stain_on_the_finders_floor
        // tests: SemOrc.an_orc_on_a_balcony_cannot_eat_the_corpse_below
        if (w && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_STAIN && !w->dead() && w->floor() == who->floor())
        {
            const std::int32_t newdistance =
                static_cast<std::int32_t>(who->distance_to_ob_center(w));
            // rule: The corpse finder returns the nearest live blood stain on its own floor whose
            //   squared centre distance is under 800. Here: `newdistance < distance (start 800)`.
            // tests: QuietProbe.find_blood_in_reach_walks_the_finders_stains_nearest_first
            // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
            // tests: SemWorld.the_corpse_finder_reaches_only_under_squared_distance_800
            // tests: SemCleric.classic_a_raise_never_reaches_a_corpse_on_another_floor
            if (newdistance < distance)
            {
                distance = newdistance;
                returnob = w;
            }
        }
    }

    return returnob;
}

walker* GameWorld::find_nearest_player(walker* ob)
{
    // plumbing: null -> nullptr
    if (!ob)
        return nullptr;

    walker* returnob = nullptr;
    std::uint32_t distance = 32000;

    // plumbing: oblist scan; see below
    for (auto& uptr : oblist)
    {
        walker* w = uptr.get();
        // rule: The leader finder for ACTION_FOLLOW (living.cpp) returns the nearest
        //   seat-driven walker (user != -1) that is awake and visible, whatever its team and
        //   whether or not it is dead; nothing when no seat-driven walker exists.
        // tests: LevelDataCoverage.level_data_round13_find_helpers_selection_and_filters
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: LevelDataCoverage.level_data_round5_find_helpers_contiguous_block_paths
        // tests: SemWorld.classic_the_player_finder_takes_a_dead_hero_with_its_seat
        // tests: SemWorld.the_player_finder_skips_a_dormant_seat_walker
        if (w && !w->dormant() && !w->hidden() && w->user() != -1)
        {
            const std::uint32_t tempdistance = static_cast<std::uint32_t>(ob->distance_to_ob(w));
            // rule: The leader finder for ACTION_FOLLOW (living.cpp) returns the nearest
            //   seat-driven walker (user != -1) that is awake and visible, whatever its team and
            //   whether or not it is dead. Here: `tempdistance < distance (start 32000)`.
            // tests: LevelDataCoverage.level_data_round13_find_helpers_selection_and_filters
            // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
            // tests: LevelDataCoverage.level_data_round5_find_helpers_contiguous_block_paths
            // tests: SemWorld.classic_the_player_finder_takes_a_dead_hero_with_its_seat
            // tests: SemWorld.the_player_finder_skips_a_dormant_seat_walker
            if (tempdistance < distance)
            {
                distance = tempdistance;
                returnob = w;
            }
        }
    }

    return returnob;
}

std::list<walker*> GameWorld::find_in_range(const std::list<std::unique_ptr<walker>>& somelist,
                                            std::int32_t range, std::int32_t* howmany,
                                            walker* ob)
{
    std::list<walker*> result;
    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (howmany != nullptr)
        *howmany = 0;

    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (!ob || howmany == nullptr)
        return result;

    // plumbing: range-finder list scans; see below
    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        // rule: find_in_range returns every live, awake, visible walker of the given list within
        //   range of the searcher (inclusive), including the searcher itself and friends; howmany
        //   is the count.
        // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: SemWorld.find_in_range_is_inclusive_counts_the_searcher_and_skips_the_inert
        if (w && !w->dead() && !w->dormant() && !w->hidden() && ob->distance_to_ob(w) <= range)
        {
            result.push_back(w);
            (*howmany)++;
        }
    }

    return result;
}

std::list<walker*> GameWorld::find_foes_in_range(const std::list<std::unique_ptr<walker>>& somelist,
                                                 std::int32_t range, std::int32_t* howmany,
                                                 walker* ob)
{
    std::list<walker*> result;
    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (howmany != nullptr)
        *howmany = 0;

    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (!ob || howmany == nullptr)
        return result;

    // plumbing: range-finder list scans; see above and below
    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        // rule: find_foes_in_range returns the live, awake, visible, non-friendly Livings and
        //   Generators within range (inclusive); no cloak roll, so an invisible foe IS found here.
        // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: SemWorld.find_foes_in_range_keeps_hostile_livings_and_generators_only
        if (w && !w->dead() && !w->dormant() && !w->hidden() &&
            (w->query_order() == Order::Living ||
             w->query_order() == Order::Generator) &&
            (ob->is_friendly(w) == 0) &&
            ob->distance_to_ob(w) <= range)
        {
            result.push_back(w);
            (*howmany)++;
        }
    }

    return result;
}

std::list<walker*> GameWorld::find_foe_weapons_in_range(const std::list<std::unique_ptr<walker>>& somelist,
                                                        std::int32_t range, std::int32_t* howmany,
                                                        walker* ob)
{
    std::list<walker*> result;
    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (howmany != nullptr)
        *howmany = 0;

    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (!ob || howmany == nullptr)
        return result;

    // plumbing: range-finder list scans; see above and below
    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        // rule: find_foe_weapons_in_range returns the hostile weapons (missiles) within range;
        //   friendly weapons and non-weapons are excluded.
        // tests: LevelDataCoverage.level_data_round5_find_helpers_contiguous_block_paths
        // tests: LevelDataCoverage.level_data_round13_find_helpers_selection_and_filters
        // tests: LevelDataCoverage.level_data_round14_find_helper_exclusion_branches
        // tests: SemWorld.find_foe_weapons_in_range_leaves_out_a_hostile_living
        if (w && !w->dead() && !w->dormant() && !w->hidden() &&
            w->query_order() == Order::Weapon &&
            !ob->is_friendly(w) &&
            ob->distance_to_ob(w) <= range)
        {
            result.push_back(w);
            (*howmany)++;
        }
    }

    return result;
}

std::list<walker*> GameWorld::find_friends_in_range(const std::list<std::unique_ptr<walker>>& somelist,
                                                    std::int32_t range, std::int32_t* howmany,
                                                    walker* ob)
{
    std::list<walker*> result;
    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (howmany != nullptr)
        *howmany = 0;

    // plumbing: range finders: zero the count, null searcher or null count pointer -> empty result
    if (!ob || howmany == nullptr)
        return result;

    // plumbing: range-finder list scans; see above and below
    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        // rule: find_friends_in_range returns the live, awake, visible friendly Livings within
        //   range, never the searcher itself and never generators or weapons.
        // tests: LevelDataCoverage.level_data_round13_find_helpers_selection_and_filters
        // tests: LevelDataCoverage.level_data_range_helpers_positive_selection_paths
        // tests: SemWorld.the_ally_finder_returns_friendly_livings_never_generators_or_weapons
        if (w && w != ob && !w->dead() && !w->dormant() && !w->hidden() &&
            w->query_order() == Order::Living &&
            ob->is_friendly(w) &&
            ob->distance_to_ob(w) <= range)
        {
            result.push_back(w);
            (*howmany)++;
        }
    }

    return result;
}

void GameWorld::delete_grid()
{
    clear_grid_dirty_tiles();
    grid.free();
    decor.free();
    pixmaxx = 0;
    pixmaxy = 0;
    mysmoother.reset();
}

void GameWorld::create_new_grid()
{
    clear_grid_dirty_tiles();
    grid.free();
    decor.free();

    grid.frames = 1;
    grid.w = 40;
    grid.h = 60;
    pixmaxx = grid.w * GRID_SIZE;
    pixmaxy = grid.h * GRID_SIZE;

    const int size = grid.w * grid.h;
    grid.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(size));
    // rule: A new blank level is a 40x60 grid of the four grass variants, each cell chosen by one
    //   world-RNG draw.
    // tests: SemWorld.a_new_blank_level_is_forty_by_sixty_grass_one_draw_per_cell
    for (int i = 0; i < size; i++)
    {
        // rule: A new blank level is a 40x60 grid of the four grass variants, each cell chosen by
        //   one world-RNG draw.
        // tests: SemWorld.a_new_blank_level_is_forty_by_sixty_grass_one_draw_per_cell
        switch (rng_.next(4))
        {
            case 0: grid.data[static_cast<std::size_t>(i)] = PIX_GRASS1; break;
            case 1: grid.data[static_cast<std::size_t>(i)] = PIX_GRASS2; break;
            case 2: grid.data[static_cast<std::size_t>(i)] = PIX_GRASS3; break;
            case 3: grid.data[static_cast<std::size_t>(i)] = PIX_GRASS4; break;
        }
    }

    mysmoother.set_target(grid);
}

void GameWorld::resize_grid(int width, int height)
{
    // Size is limited to one byte in the file format
    // rule: Resizing a level keeps every tile inside the old bounds, fills newly added cells with
    //   random grass, and refuses sizes below 3 or above 255 (one byte in the file format) leaving
    //   the level unchanged.
    // tests: LevelDataCoverage.level_data_round17_grid_resize_campaign_wrappers_and_delete_object_hooks
    // tests: SemWorld.a_resize_below_three_or_above_255_leaves_the_level_unchanged
    if (width < 3 || height < 3 || width > 255 || height > 255)
    {
        Log("Can't resize grid to these dimensions: {}x{}\n", width, height);
        return;
    }

    auto random_grass_tile = [this]() -> unsigned char {
        // rule: Resizing a level keeps every tile inside the old bounds, fills newly added cells
        //   with random grass, and refuses sizes below 3 or above 255 (one byte in the file format)
        //   leaving the level unchanged.
        // tests: LevelDataCoverage.level_data_round17_grid_resize_campaign_wrappers_and_delete_object_hooks
        // tests: SemWorld.a_resize_below_three_or_above_255_leaves_the_level_unchanged
        switch (rng_.next(4))
        {
            case 0: return PIX_GRASS1;
            case 1: return PIX_GRASS2;
            case 2: return PIX_GRASS3;
            case 3: return PIX_GRASS4;
        }
        return PIX_GRASS1;
    };

    // Create new grid
    const int size = width * height;
    auto new_grid = std::make_unique<unsigned char[]>(static_cast<std::size_t>(size));

    // Copy the map data
    // plumbing: resize copy loops; see above
    for (int i = 0; i < width; i++)
    {
        // plumbing: resize copy loops; see above
        for (int j = 0; j < height; j++)
        {
            // rule: Resizing a level keeps every tile inside the old bounds, fills newly added
            //   cells with random grass, and refuses sizes below 3 or above 255 (one byte in the
            //   file format) leaving the level unchanged.
            // tests: LevelDataCoverage.level_data_round17_grid_resize_campaign_wrappers_and_delete_object_hooks
            // tests: SemWorld.a_resize_below_three_or_above_255_leaves_the_level_unchanged
            if (i < grid.w && j < grid.h)
                new_grid[static_cast<std::size_t>(j * width + i)] = grid.data[static_cast<std::size_t>(j * grid.w + i)];
            else
                new_grid[static_cast<std::size_t>(j * width + i)] = random_grass_tile();
        }
    }

    // Delete the old, use the new
    grid.free();
    grid.data = std::move(new_grid);
    grid.frames = 1;
    grid.w = static_cast<unsigned char>(width);
    grid.h = static_cast<unsigned char>(height);
    clear_grid_dirty_tiles();
    pixmaxx = grid.w * GRID_SIZE;
    pixmaxy = grid.h * GRID_SIZE;

    mysmoother.set_target(grid);

    // Delete objects that fell off the map
    const int x = 0;
    const int y = 0;
    const int w = grid.w * GRID_SIZE;
    const int h = grid.h * GRID_SIZE;
    auto off_map = [w, h](const std::unique_ptr<walker>& uptr) {
        walker* ob = uptr.get();
        return ob == nullptr || (x > ob->xpos() || ob->xpos() >= x + w ||
                                 y > ob->ypos() || ob->ypos() >= y + h);
    };

    // rule: Shrinking a level deletes every walker, effect and weapon whose position falls outside
    //   the new map.
    // tests: LevelDataCoverage.level_data_round17_grid_resize_campaign_wrappers_and_delete_object_hooks
    std::erase_if(oblist.raw_mutable(), [this, &off_map](const auto& uptr) {
        const bool erase = off_map(uptr);
        if (erase)
            remove_from_id_index(uptr.get());
        return erase;
    });
    std::erase_if(fxlist.raw_mutable(), [this, &off_map](const auto& uptr) {
        const bool erase = off_map(uptr);
        if (erase)
            remove_from_id_index(uptr.get());
        return erase;
    });
    std::erase_if(weaplist.raw_mutable(), [this, &off_map](const auto& uptr) {
        const bool erase = off_map(uptr);
        if (erase)
            remove_from_id_index(uptr.get());
        return erase;
    });
}

void GameWorld::delete_objects()
{
    oblist.clear();
    fxlist.clear();
    weaplist.clear();
    dead_list.clear();
    id_index_.clear();
    entity_tracking_dirty_ = false;
    next_entity_id_ = 1;
    living_count = 0;

    // plumbing: delete_objects: create obmap if missing, clear stale spatial index
    if (!myobmap)
    {
        myobmap = std::make_unique<obmap>();
        return;
    }

    // Walkers can outlive a world-bound obmap during tests and editor flows.
    // After list teardown, clear any stale spatial index entries defensively.
    // plumbing: delete_objects: create obmap if missing, clear stale spatial index
    if (!myobmap->walker_to_pos.empty())
        Log("obmap::walker_to_pos has {} elements left.\n", myobmap->walker_to_pos.size());
    myobmap->pos_to_walker.clear();
    myobmap->walker_to_pos.clear();
}

void GameWorld::clear()
{
    delete_objects();
    clear_removed_entity_ids();
    clear_grid_dirty_tiles();
    delete_grid();
    next_entity_id_ = 1;

    myobmap = std::make_unique<obmap>();

    title = "New Level";
    type = 0;
    par_value = 1;
    time_bonus_limit = 4000;
    difficulty = 100;
    level_done = 0;
    game_ended = false;
    completion_events_emitted = false;
    next_level = -1;
    ending = 0;
    enemy_freeze = 0;
    timer_wait = 6;
    end = 0;
    retry = false;
    control_hp = 0.0f;
    withdraw_requested = false;
    withdraw_level = -1;
    // plumbing: clear(): reset four team scores
    for (int i = 0; i < 4; ++i)
        m_score[i] = 0;
    my_team = 0;
    allied_mode = 0;
    current_scenario = 0;
    current_palette_id = 0;
    pending_exit_prompt = false;
    paused = false;
    pause_player_index = 0xff;
    completed_levels.clear();
    // Networked control policy (design §4.4): reset both scalars to the
    // legacy defaults whenever a world is cleared. Snapshot v9 stamps them
    // onto mirror worlds during networked play and nothing else ever writes
    // them back, so a cleared-and-reused world must not keep an owner-locked
    // policy or a stale machine map. NOTE this alone does not cover the SDL
    // display world (its level loads rebuild state via
    // replace_loaded_world_state, which deliberately preserves the scalars
    // for the dedicated server's in-session transitions) — the local install
    // in reset_local_transport_shadow additionally stamps legacy on the
    // authoritative world. The networked installs stamp AFTER clear/load
    // (install_control_policy) and replay playback re-stamps from every
    // applied recorded snapshot, so both remain correct.
    control_policy = 0;
    player_machine.fill(0xff);
}

char GameWorld::damage_tile(short xloc, short yloc)
{
    // rule: A blast off the map (including a negative pixel that would truncate into cell 0) or on
    //   a level with no grid damages no tile and folds no dirty tile.
    // tests: GameWorldEntityIdsFixture.damage_tile_rejects_negative_pixels_before_dividing
    // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
    // tests: ScreenExtended.screen_damage_tile_various
    // tests: SemWorld.a_blast_on_a_level_with_no_grid_damages_no_tile
    if (!grid.valid())
        return 0;

    // rule: A blast off the map (including a negative pixel that would truncate into cell 0) or on
    //   a level with no grid damages no tile and folds no dirty tile. Here: `xloc<0||yloc<0`.
    // tests: GameWorldEntityIdsFixture.damage_tile_rejects_negative_pixels_before_dividing
    // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
    // tests: ScreenExtended.screen_damage_tile_various
    // tests: SemWorld.a_blast_on_a_level_with_no_grid_damages_no_tile
    if (xloc < 0 || yloc < 0)
        return 0;
    const short xover = static_cast<short>(xloc / GRID_SIZE);
    const short yover = static_cast<short>(yloc / GRID_SIZE);

    // rule: A blast off the map (including a negative pixel that would truncate into cell 0) or on
    //   a level with no grid damages no tile and folds no dirty tile. Here:
    //   `xover>=grid.w||yover>=grid.h`.
    // tests: GameWorldEntityIdsFixture.damage_tile_rejects_negative_pixels_before_dividing
    // tests: GameWorldEntityIdsFixture.boundary_queries_fail_closed_without_side_effects
    // tests: ScreenExtended.screen_damage_tile_various
    // tests: SemWorld.a_blast_on_a_level_with_no_grid_damages_no_tile
    if (xover >= grid.w || yover >= grid.h)
        return 0;

    const int gridloc = yover * grid.w + xover;

    // Decor shields the ground: a decorated cell is exempt from the
    // grass->charred transform. Legacy combined tiles (PIX_BOULDER_* etc.)
    // never transformed under bombs; after base+decor migration their base
    // byte is grass and WOULD — this gated skip restores exact legacy
    // behavior and defines the semantic for new levels. Returning 0 also
    // skips the caller's no-op dirty-tile fold (game_server truthiness gate).
    // rule: Decor shields the ground: a decorated cell is never charred by a blast.
    // tests: Decor.damage_tile_skips_decorated_cells, ScreenExtended.screen_damage_tile_various
    if (decor.valid() && decor.w == grid.w && decor.h == grid.h &&
        decor.data[static_cast<std::size_t>(gridloc)] != DECOR_NONE)
        return 0;

    unsigned char next_value = grid.data[static_cast<std::size_t>(gridloc)];
    // rule: A blast chars plain grass (the four grass variants) to damaged grass; every other tile,
    //   including dark/light grass and already-damaged grass, is untouched. The return value is the
    //   cell's resulting byte.
    // tests: ScreenExtended.screen_damage_tile_various
    // tests: GameWorldEntityIdsFixture.damage_tile_rejects_negative_pixels_before_dividing
    // tests: SemWorld.a_blast_chars_each_plain_grass_variant_and_nothing_else
    switch (static_cast<unsigned char>(grid.data[static_cast<std::size_t>(gridloc)]))
    {
        case PIX_GRASS1:
        case PIX_GRASS2:
        case PIX_GRASS3:
        case PIX_GRASS4:
            next_value = PIX_GRASS1_DAMAGED;
            break;
        default:
            break;
    }

    // plumbing: dirty-tile fold for the network delta: only a changed byte is recorded, each cell
    //   once
    if (grid.data[static_cast<std::size_t>(gridloc)] != next_value)
    {
        grid.data[static_cast<std::size_t>(gridloc)] = next_value;
        const std::pair<short, short> coord{xover, yover};
        // plumbing: dirty-tile fold for the network delta: only a changed byte is recorded, each
        //   cell once
        if (std::find(grid_dirty_tiles_.begin(), grid_dirty_tiles_.end(), coord) ==
            grid_dirty_tiles_.end())
        {
            grid_dirty_tiles_.push_back(coord);
        }
    }

    return static_cast<char>(grid.data[static_cast<std::size_t>(gridloc)]);
}

short GameWorld::remaining_foes(walker* myguy) const
{
    // plumbing: null -> 0 foes
    if (!myguy)
        return 0;

    short myfoes = 0;
    // plumbing: oblist scan; see below
    for (auto& uptr : oblist)
    {
        walker* w = uptr.get();
        // rule: The HUD foe count is the number of live Livings in the object list not friendly to
        //   the given walker; generators, weapons and the dead do not count, and dormant/hidden
        //   foes DO count (no filter).
        // tests: LevelDataCoverage.level_data_round16_remaining_foes_and_object_passable_collision_paths
        // tests: LevelDataCoverage.level_data_batch2_misc_uncovered_paths_smoke
        // tests: SemWorld.the_foe_count_skips_generators_and_counts_dormant_and_hidden_foes
        if (w && !w->dead() &&
            (w->query_order() == Order::Living) &&
            !myguy->is_friendly(w))
        {
            myfoes++;
        }
    }
    return myfoes;
}

void GameWorld::reset_level_progress()
{
    level_tick_count_ = 0;
    last_level_id_ = -1;
    completion_events_emitted = false;
}

void GameWorld::tick()
{
    // plumbing: no active context / event log -> the world does not tick
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;

    og::sim::SimEventLog& events = *current_game->sim_events;

    game_ended = false;
    next_level = -1;
    ending = 0;
    level_done = 2; // unless we find valid foes while looping

    tick_count_++;
    events.current_tick_ = tick_count_;
    run_pending_level_on_load();
    level_tick_count_++;

    // rule: A level a seat plays never times out (the old game has no limit); a level nobody plays
    //   (level_unattended) is lost past 36000 ticks: a retreat (ending 1, no next level), a 'Mission
    //   timed out. Retreating.' notice, pending classic respawns flushed, nothing acts that tick.
    // tests: SimWorld.r15_reset_level_progress_clears_timeout_for_same_level
    // tests: ClassicRespawn.timeout_end_shape_flushes_pending_heroes
    // tests: SemWorld.a_played_level_never_times_out_and_an_unattended_one_retreats
    std::uint32_t max_level_ticks = level_unattended ? 36000u : 0u;  // 0: no limit (the old game)
    if (og::sim::g_test_level_tick_limit_override > 0)  // the test hook limits every level
        max_level_ticks = static_cast<std::uint32_t>(og::sim::g_test_level_tick_limit_override);
    if (max_level_ticks != 0 && level_tick_count_ > max_level_ticks)
    {
        game_ended = true;
        ending = 1;
        next_level = -1;
        events.push_notification("Mission timed out. Retreating.", 40);
        // A loss persists nothing, but flush pending classic respawns anyway
        // so the results screen shows revived heroes instead of mid-respawn
        // corpses. No-op (and RNG-free) when respawn_mode == 0.
        og::sim::classic_respawn_flush_pending(*this);
        return;
    }

    // Level-script on_tick: fixed pre-act point, after the timeout guard.
    og::script::hooks::level_tick(this);

    // rule: FREEZE TIME counts down one per tick; on the tick it reaches 1 the normal palette is
    //   restored and exactly one SetPalette(0) event is emitted (the HUD draws the countdown; no
    //   notification spam).
    // tests: SimWorld.r15_freeze_tick_and_level_done_paths
    // tests: RuntimeCoveragePaths.sim_world_freeze_countdown_is_hud_state_and_weap_cleanup
    // tests: SemWorld.freeze_time_counts_down_and_only_its_last_tick_restores_the_palette
    if (enemy_freeze)
        enemy_freeze--;
    if (enemy_freeze == 1)
    {
        current_palette_id = 0;
        events.push(og::sim::EventKind::SetPalette, 0, 0);
    }

    // Wake a delayed-spawn walker: re-enter the obmap the way a spawned unit
    // does, then emit the teleporter-pad flash flourish at its spot. Only ever
    // reached when spawn_delay > 0 (non-default), so parity is untouched.
    //
    // The authored spot can be occupied by wake time: the walker was
    // intangible while dormant, so nothing kept the spot free — an
    // equal-delay formation member that woke earlier this same tick, a
    // generator spawn, or a wanderer can be standing there. Entering the
    // obmap regardless stacked live bodies permanently (a packed formation
    // "merged" into one visible walker). So: wake in place when the spot is
    // clear of LIVE blockers (dormant walkers never block — the first member
    // of a mutual-overlap group always wakes in place, which is the no-
    // deadlock guarantee); otherwise relocate to the nearest clear cell
    // (deterministic ring scan, no RNG); when the whole nudge radius is
    // blocked, defer — the walker stays dormant, keeps counting as alive
    // below, and retries next tick. No wake path ever consumes a walker.
    // BIT_NO_COLLIDE wakers keep the legacy wake-in-place (overlap is legal
    // for them in ob_pass_check).
    auto wake_delayed_spawn = [this](walker* ob) {
        const bool no_collide =
            ob->stats() != nullptr &&
            ob->stats()->query_bit_flags(BIT_NO_COLLIDE);
        // rule: A delayed-spawn walker wakes in place when no live blocker overlaps its spot;
        //   otherwise it moves to the nearest clear cell within 4 rings (below); if none is clear
        //   it stays dormant (still counted alive) and retries next tick with a 'delayed spawn
        //   deferred' trace. A BIT_NO_COLLIDE waker always wakes in place. Every wake spawns an
        //   expanding FAMILY_FLASH on the waker's floor centred on it and traces 'delayed spawn:
        //   family=.. team=.. delay=.. tick=..'.
        // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
        // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
        // tests: DelayedSpawnWake.blocked_wake_defers_then_completes
        // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
        // tests: SemWorld.a_no_collide_sleeper_wakes_in_place_on_a_held_spot
        // tests: SemWorld.a_wake_flashes_once_on_the_wakers_floor
        if (!no_collide && wake_spot_blocked(ob) &&
            !relocate_to_nearest_wake_spot(ob))
        {
            TRACE("game", "delayed spawn deferred: spot blocked, tick=%u",
                  static_cast<unsigned>(level_tick_count_));
            return;
        }
        ob->set_dormant(false);
        walker* flash = add_ob(Order::FX, FAMILY_FLASH);
        if (flash != nullptr)
        {
            flash->set_ani_type(ANI_EXPAND_8);
            flash->set_floor(ob->floor());  // flash on the waker's floor (A8)
            flash->center_on(ob);
        }
        TRACE("game", "delayed spawn: family=%d team=%d delay=%u tick=%u",
              static_cast<int>(ob->family()), static_cast<int>(ob->team_num()),
              static_cast<unsigned>(ob->spawn_delay()),
              static_cast<unsigned>(level_tick_count_));
    };

    // --- Entity act phase ---
    // plumbing: entity act phase over oblist; see below
    for (auto& uptr : oblist)
    {
        // rule: Once a withdraw is requested during a tick (an exit pad accepted, a level script's
        //   withdraw), no further walker or weapon acts that tick and the rest of the tick (exit
        //   scan, completion, cleanup, dead sweep) is skipped.
        // tests: ModeTick.withdraw_raised_by_a_weapon_hit_stops_the_rest_of_the_weapon_phase
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: SemWorld.a_withdraw_raised_in_the_object_phase_ends_the_tick
        if (withdraw_requested)
            break;

        walker* ob = uptr.get();

        // Delayed spawns: dormant walkers do not act (and are absent from the
        // obmap, draw, and snapshots), but their team still counts as alive so
        // a defenders-arrive-later scenario doesn't end before they appear.
        // rule: A dormant (delayed-spawn) or hidden walker does not act, but a hostile Living among
        //   them still holds the level open. A dormant walker wakes on the first tick its level
        //   tick count passes its spawn delay, and if the wake succeeds it acts that same tick.
        // tests: SimWorldHeadless.freeze_still_counts_a_dormant_hostile
        // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
        // tests: DelayedSpawnWake.blocked_wake_defers_then_completes
        // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
        // tests: SemWorld.a_hidden_friend_does_not_act_or_hold_the_level
        // tests: SemWorld.a_hidden_foe_holds_the_level_without_acting
        // tests: SemWorld.a_sleeper_acts_first_on_the_tick_it_wakes
        if (ob != nullptr && !ob->dead() && (ob->dormant() || ob->hidden()))
        {
            // rule: A dormant (delayed-spawn) or hidden walker does not act, but a hostile Living
            //   among them still holds the level open.
            // tests: SimWorldHeadless.freeze_still_counts_a_dormant_hostile
            // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
            // tests: DelayedSpawnWake.blocked_wake_defers_then_completes
            // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
            // tests: SemWorld.a_hidden_friend_does_not_act_or_hold_the_level
            // tests: SemWorld.a_hidden_foe_holds_the_level_without_acting
            // tests: SemWorld.a_sleeper_acts_first_on_the_tick_it_wakes
            if (ob->dormant() && level_tick_count_ > ob->spawn_delay())
                wake_delayed_spawn(ob);
            // rule: A dormant (delayed-spawn) or hidden walker does not act, but a hostile Living
            //   among them still holds the level open.
            // tests: SimWorldHeadless.freeze_still_counts_a_dormant_hostile
            // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
            // tests: DelayedSpawnWake.blocked_wake_defers_then_completes
            // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
            // tests: SemWorld.a_hidden_friend_does_not_act_or_hold_the_level
            // tests: SemWorld.a_hidden_foe_holds_the_level_without_acting
            // tests: SemWorld.a_sleeper_acts_first_on_the_tick_it_wakes
            if (ob->dormant() || ob->hidden())
            {
                // rule: A dormant (delayed-spawn) or hidden walker does not act, but a hostile
                //   Living among them still holds the level open.
                // tests: SimWorldHeadless.freeze_still_counts_a_dormant_hostile
                // tests: NpcScenarioFlags.dormant_walker_hidden_until_spawn_tick_then_enters_world
                // tests: DelayedSpawnWake.blocked_wake_defers_then_completes
                // tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
                // tests: SemWorld.a_hidden_friend_does_not_act_or_hold_the_level
                // tests: SemWorld.a_hidden_foe_holds_the_level_without_acting
                // tests: SemWorld.a_sleeper_acts_first_on_the_tick_it_wakes
                if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                    ob->query_order() == Order::Living)
                    level_done = 0;
                continue;
            }
        }

        // rule: During FREEZE TIME a Living or Generator not on the player's side skips its turn:
        //   with Gameplay Fixes on (fix AF) one not friendly to the player's team (the head of its
        //   owner chain does not wear my_team); off (the old game) one not on team 0 itself. Every
        //   other order (effects, treasures, weapons in oblist) still acts. Outside a freeze
        //   everything acts with in_act set.
        // tests: SimWorld.r15_freeze_tick_and_level_done_paths
        // tests: SimWorld.r15_freeze_uses_friendliness_not_team_zero
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: parity:enemy_freeze_mage_scen99
        // tests: SemWorld.freeze_time_stops_hostile_generators_but_not_effects_or_treasures
        // tests: SimWorld.r15_freeze_lets_only_team_zero_act_with_gameplay_fixes_off
        // tests: SemWorld.freeze_time_asks_team_zero_with_the_fixes_off_and_the_owner_chain_with_them_on
        if (!enemy_freeze) // normal functionality
        {
            // rule: A level stays unfinished while any Living not friendly to the player's team is
            //   alive after its turn, in the object list, whether it acted or was frozen.
            //   Generators never hold the level open. A walker that dies during its own act does
            //   not count.
            // tests: SimWorldHeadless.freeze_keeps_level_unfinished_with_a_live_foe
            // tests: SimWorldHeadless.freeze_does_not_autocomplete_a_kill_all_level
            // tests: parity:enemy_freeze_mage_scen99
            // tests: RuntimeCoveragePaths.sim_world_batch5_game_end_paths
            // tests: SemWorld.a_foe_that_dies_in_its_own_act_does_not_hold_the_level
            // tests: SemWorld.hostile_generators_alone_never_hold_the_level_open
            if (ob && !ob->dead())
            {
                // Zardus: while acting, in_act is set
                ob->set_in_act(true);
                ob->act();
                ob->set_in_act(false);
                // rule: A level stays unfinished while any Living not friendly to the player's team
                //   is alive after its turn, in the object list, whether it acted or was frozen.
                // tests: SimWorldHeadless.freeze_keeps_level_unfinished_with_a_live_foe
                // tests: SimWorldHeadless.freeze_does_not_autocomplete_a_kill_all_level
                // tests: parity:enemy_freeze_mage_scen99
                // tests: RuntimeCoveragePaths.sim_world_batch5_game_end_paths
                // tests: SemWorld.a_foe_that_dies_in_its_own_act_does_not_hold_the_level
                // tests: SemWorld.hostile_generators_alone_never_hold_the_level_open
                if (ob && !ob->dead())
                {
                    // rule: A level stays unfinished while any Living not friendly to the player's
                    //   team is alive after its turn, in the object list, whether it acted or was
                    //   frozen.
                    // tests: SimWorldHeadless.freeze_keeps_level_unfinished_with_a_live_foe
                    // tests: SimWorldHeadless.freeze_does_not_autocomplete_a_kill_all_level
                    // tests: parity:enemy_freeze_mage_scen99
                    // tests: RuntimeCoveragePaths.sim_world_batch5_game_end_paths
                    // tests: SemWorld.a_foe_that_dies_in_its_own_act_does_not_hold_the_level
                    // tests: SemWorld.hostile_generators_alone_never_hold_the_level_open
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                        ob->query_order() == Order::Living)
                        level_done = 0; // an awake foe holds the level open
                    // Testing .. trying to FORCE foes :)
                    // rule: After acting, every live walker in the object list (any order, any
                    //   team) with neither a foe nor a leader is handed the nearest qualifying foe
                    //   (above) - the classic 'FORCE foes'. A walker with a leader keeps no forced
                    //   foe.
                    // tests: SemWorld.a_walker_without_foe_or_leader_is_handed_the_nearest_foe
                    if (ob->foe() == nullptr && ob->leader() == nullptr)
                        ob->set_foe(find_nearest_foe(ob));
                }
            }
        }
        else // enemy livings are frozen
        {
            // The countdown is drawn by the HUD from enemy_freeze (#232); it
            // used to be pushed into the notification feed every tenth tick,
            // which evicted every other message for the whole freeze.
            // rule: During FREEZE TIME a Living or Generator not on the player's side skips its
            //   turn (fix AF: not friendly to my_team with Gameplay Fixes on, not on team 0 off).
            // tests: SimWorld.r15_freeze_tick_and_level_done_paths
            // tests: SimWorld.r15_freeze_uses_friendliness_not_team_zero
            // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
            // tests: parity:enemy_freeze_mage_scen99
            // tests: SemWorld.freeze_time_stops_hostile_generators_but_not_effects_or_treasures
            // tests: SimWorld.r15_freeze_lets_only_team_zero_act_with_gameplay_fixes_off
            // tests: SemWorld.freeze_time_asks_team_zero_with_the_fixes_off_and_the_owner_chain_with_them_on
            if (ob && !ob->dead())
            {
                // Only the census is hoisted out of the act gate: a foe that
                // is frozen is still a foe (#231), exactly as the dormant
                // branch above already counts one.
                // rule: During FREEZE TIME a Living or Generator may act only on the player's side:
                //   with Gameplay Fixes on (fix AF, #92) when it is friendly to the player's team
                //   (the head of its owner chain wears my_team); off (the old game) when it is on
                //   team 0 itself, which freezes a non-red player's own side (#86). Every other
                //   order always acts.
                // tests: SimWorld.r15_freeze_uses_friendliness_not_team_zero
                // tests: SimWorld.r15_freeze_lets_only_team_zero_act_with_gameplay_fixes_off
                // tests: SemWorld.freeze_time_asks_team_zero_with_the_fixes_off_and_the_owner_chain_with_them_on
                // tests: SemWorld.freeze_time_stops_hostile_generators_but_not_effects_or_treasures
                const bool may_act =
                    ((ob->query_order() != Order::Living) &&
                     (ob->query_order() != Order::Generator)) ||
                    (gameplay_fixes != 0
                         ? ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) != 0
                         : ob->team_num() == 0);
                // rule: During FREEZE TIME a Living or Generator not on the player's side skips
                //   its turn (the gate above).
                // tests: SimWorld.r15_freeze_tick_and_level_done_paths
                // tests: SimWorld.r15_freeze_uses_friendliness_not_team_zero
                // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
                // tests: parity:enemy_freeze_mage_scen99
                // tests: SemWorld.freeze_time_stops_hostile_generators_but_not_effects_or_treasures
                // tests: SimWorld.r15_freeze_lets_only_team_zero_act_with_gameplay_fixes_off
                // tests: SemWorld.freeze_time_asks_team_zero_with_the_fixes_off_and_the_owner_chain_with_them_on
                if (may_act)
                    ob->act();
                // rule: A level stays unfinished while any Living not friendly to the player's team
                //   is alive after its turn, in the object list, whether it acted or was frozen.
                // tests: SimWorldHeadless.freeze_keeps_level_unfinished_with_a_live_foe
                // tests: SimWorldHeadless.freeze_does_not_autocomplete_a_kill_all_level
                // tests: parity:enemy_freeze_mage_scen99
                // tests: RuntimeCoveragePaths.sim_world_batch5_game_end_paths
                // tests: SemWorld.a_foe_that_dies_in_its_own_act_does_not_hold_the_level
                // tests: SemWorld.hostile_generators_alone_never_hold_the_level_open
                if (ob && !ob->dead())
                {
                    // rule: A level stays unfinished while any Living not friendly to the player's
                    //   team is alive after its turn, in the object list, whether it acted or was
                    //   frozen.
                    // tests: SimWorldHeadless.freeze_keeps_level_unfinished_with_a_live_foe
                    // tests: SimWorldHeadless.freeze_does_not_autocomplete_a_kill_all_level
                    // tests: parity:enemy_freeze_mage_scen99
                    // tests: RuntimeCoveragePaths.sim_world_batch5_game_end_paths
                    // tests: SemWorld.a_foe_that_dies_in_its_own_act_does_not_hold_the_level
                    // tests: SemWorld.hostile_generators_alone_never_hold_the_level_open
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                        ob->query_order() == Order::Living)
                        level_done = 0;
                }
            }
        }
    }

    // rule: Once a withdraw is requested during a tick (an exit pad accepted, a level script's
    //   withdraw), no further walker or weapon acts that tick and the rest of the tick (exit scan,
    //   completion, cleanup, dead sweep) is skipped.
    // tests: ModeTick.withdraw_raised_by_a_weapon_hit_stops_the_rest_of_the_weapon_phase
    // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
    // tests: SemWorld.a_withdraw_raised_in_the_object_phase_ends_the_tick
    if (withdraw_requested)
        return;

    // --- Weapon act phase ---
    // plumbing: weapon act phase; see below
    for (auto& uptr : weaplist)
    {
        // rule: Once a withdraw is requested during a tick (an exit pad accepted, a level script's
        //   withdraw), no further walker or weapon acts that tick and the rest of the tick (exit
        //   scan, completion, cleanup, dead sweep) is skipped.
        // tests: ModeTick.withdraw_raised_by_a_weapon_hit_stops_the_rest_of_the_weapon_phase
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: SemWorld.a_withdraw_raised_in_the_object_phase_ends_the_tick
        if (withdraw_requested)
            break;

        walker* ob = uptr.get();
        // rule: Every live walker in the weapon list acts once per tick after the object list,
        //   freeze or not (missiles are never frozen); a hostile Living filed there (a pack can do
        //   this) holds the level open like one in the object list.
        // tests: SimWorldHeadless.hostile_living_in_the_weapon_list_holds_the_level_open
        // tests: SemWorld.live_missiles_act_through_freeze_time_and_dead_ones_never
        if (ob && !ob->dead())
        {
            ob->act();
            // rule: Every live walker in the weapon list acts once per tick after the object list,
            //   freeze or not (missiles are never frozen).
            // tests: SimWorldHeadless.hostile_living_in_the_weapon_list_holds_the_level_open
            // tests: SemWorld.live_missiles_act_through_freeze_time_and_dead_ones_never
            if (ob && !ob->dead())
            {
                // rule: Every live walker in the weapon list acts once per tick after the object
                //   list, freeze or not (missiles are never frozen).
                // tests: SimWorldHeadless.hostile_living_in_the_weapon_list_holds_the_level_open
                // tests: SemWorld.live_missiles_act_through_freeze_time_and_dead_ones_never
                if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                    ob->query_order() == Order::Living)
                    level_done = 0;
            }
        }
    }

    // rule: Once a withdraw is requested during a tick (an exit pad accepted, a level script's
    //   withdraw), no further walker or weapon acts that tick and the rest of the tick (exit scan,
    //   completion, cleanup, dead sweep) is skipped.
    // tests: ModeTick.withdraw_raised_by_a_weapon_hit_stops_the_rest_of_the_weapon_phase
    // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
    // tests: SemWorld.a_withdraw_raised_in_the_object_phase_ends_the_tick
    if (withdraw_requested)
        return;

    // --- Check background for exits ---
    // plumbing: fxlist exit scan; see below
    for (auto& uptr : fxlist)
    {
        walker* ob = uptr.get();
        // rule: When no foes remain, a live exit pad on the level turns 'no foes, no exit' (2) into
        //   'cleared, walk to the exit' (1), so the level does not auto-advance; with foes alive it
        //   stays 0.
        // tests: RuntimeCoveragePaths.sim_world_tick_branches_for_end_freeze_and_cleanup
        // tests: SimWorld.r15_end_flag_and_auto_advance_paths
        // tests: SimWorld.r15_freeze_tick_and_level_done_paths
        // tests: SemWorld.a_dead_exit_pad_does_not_keep_a_cleared_level_waiting
        if (ob && !ob->dead())
        {
            // rule: When no foes remain, a live exit pad on the level turns 'no foes, no exit' (2)
            //   into 'cleared, walk to the exit' (1), so the level does not auto-advance.
            // tests: RuntimeCoveragePaths.sim_world_tick_branches_for_end_freeze_and_cleanup
            // tests: SimWorld.r15_end_flag_and_auto_advance_paths
            // tests: SimWorld.r15_freeze_tick_and_level_done_paths
            // tests: SemWorld.a_dead_exit_pad_does_not_keep_a_cleared_level_waiting
            if (ob->query_order() == Order::Treasure &&
                ob->family() == FAMILY_EXIT && level_done != 0)
            {
                // 0 => foes, 1 => no foes but exit, 2 => no foes or exit
                level_done = 1;
            }
        }
    }

    // --- Level completion check ---
    // A TYPE_SCRIPTED map whose campaign pack registered no on_mode_init
    // (or whose init errored) falls through to the classic completion rules
    // starting the NEXT tick (the four-state activation discipline).
    // rule: A scripted (mode) level hands completion to its mode script every tick; if the mode
    //   ended the game the tick stops there. A scripted level whose mode init refused runs its one
    //   lineup stage step on the tick the init fails (unless a stager already claimed it), makes no
    //   completion decision that tick, and from the next tick on follows the classic completion
    //   rules.
    // tests: ModeTick.scripted_bit_without_hooks_falls_classic_next_tick
    // tests: LineupStageLazyArm.refused_mode_world_runs_the_step_once
    // tests: ModeTick.declare_winner_is_one_shot_and_reasserted_every_tick
    // tests: SemWorld.an_active_mode_owns_completion_and_kill_all_never_fires
    // tests: SemWorld.a_mode_win_ends_the_tick_before_the_dead_sweep
    if ((type & TYPE_SCRIPTED) && !(mode.init_attempted && !mode.active))
    {
        og::sim::mode_run_tick(*this);
        // rule: A scripted (mode) level hands completion to its mode script every tick; if the mode
        //   ended the game the tick stops there. Here: `game_ended -> return`.
        // tests: ModeTick.scripted_bit_without_hooks_falls_classic_next_tick
        // tests: LineupStageLazyArm.refused_mode_world_runs_the_step_once
        // tests: ModeTick.declare_winner_is_one_shot_and_reasserted_every_tick
        // tests: SemWorld.an_active_mode_owns_completion_and_kill_all_never_fires
        // tests: SemWorld.a_mode_win_ends_the_tick_before_the_dead_sweep
        if (game_ended)
            return;
        // The mode-less stage step for a scripted level whose init REFUSED
        // (docs/lineup-design.md C2), the tick-path mirror of match_stage
        // step 8b: the stager runs mode_stage_init and then the lineup step
        // on the same world, and an un-staged world has to do the same on
        // the tick its lazy init fails — a failed init owns this tick, and
        // from the next one on this world takes the classic arm below past
        // its tick-1 gate, so without this the refused level would never
        // get its one stage step. The claim discipline is the classic arm's:
        // an adopted world never reaches here (its init latch is already
        // set, so tick 1 takes the classic arm and consumes the claim), and
        // a snapshot-seeded twin arrives latched too. No completion decision
        // follows on this tick — the classic arm's is next tick's, against
        // the world the step built — so the fielded squads are counted
        // before a kill-all map can declare anything.
        // rule: A scripted (mode) level hands completion to its mode script every tick; if the mode
        //   ended the game the tick stops there. Here: `init refused && !claim ->
        //   run_lineup_stage_step`.
        // tests: ModeTick.scripted_bit_without_hooks_falls_classic_next_tick
        // tests: LineupStageLazyArm.refused_mode_world_runs_the_step_once
        // tests: ModeTick.declare_winner_is_one_shot_and_reasserted_every_tick
        // tests: SemWorld.an_active_mode_owns_completion_and_kill_all_never_fires
        // tests: SemWorld.a_mode_win_ends_the_tick_before_the_dead_sweep
        if (mode.init_attempted && !mode.active &&
            !consume_staged_lineup_stage_claim())
        {
            run_lineup_stage_step();
        }
    }
    else
    {
        // "TROOPS: OWN" on a classic map. Latch-once on this level's first
        // tick: the scripted modes do their own strip from Lua at
        // on_mode_init, so this arm covers exactly the maps that have no
        // mode script. Runs BEFORE the completion decision and before
        // classic_respawn_run_tick, so a stripped fighter can neither count
        // as a live foe nor be queued for respawn.
        // Any request above 0 means OWN: the menus write 2, a save or a
        // peer from a build with the retired middle state hands us a 1, and
        // the >= 1 latch intentionally admits 3 (kTroopsMatched, "TROOPS:
        // FAIR") too — matched fill exists only in mode Lua, so on a
        // classic map FAIR degenerates to plain OWN (matched-teams D32).
        // A kill-all level then completes on the NEXT tick, when the foe scan
        // reruns against the stripped world — the documented consequence of
        // the setting, not something this block decides for itself.
        // rule: TROOPS: OWN (any request >= 1, including the retired middle state and FAIR) on a
        //   classic map retires every authored Living and Generator (protected NPCs excepted) on
        //   the level's first tick, before the completion decision and before respawn scanning; a
        //   kill-all level then completes on the next tick.
        // tests: ScenarioStripClassic.tick_latches_the_sweep_once_on_a_classic_map
        // tests: ScenarioStripClassic.tick_leaves_the_world_alone_for_keep
        // tests: ScenarioStripClassic.tick_sweeps_for_the_retired_middle_state_too
        // tests: ScenarioStripClassic.tick_sweeps_for_the_matched_state_too
        // tests: ScenarioStripClassic.scripted_maps_are_left_to_their_mode_script
        // tests: ScenarioStripClassic.removal_leaves_no_corpse_for_classic_respawn
        if (level_tick_count_ == 1 && ctf_requested_strip_scenario_troops >= 1)
            og::sim::classic_strip_authored_troops(*this);
        // The mode-less stage step (docs/lineup-design.md C2) — the LAZY arm,
        // exactly mirroring mode_stage_init's: a world the stager assembled
        // arrives with the claim latched and skips this, an un-staged world
        // (solo play, a level entered without a MatchStage) runs the step
        // here on its first tick, where the classic strip runs.
        // rule: An un-staged classic level (solo play) runs its lineup stage step on its first
        //   tick; if that step fielded fighters, the level cannot complete on that tick (it
        //   re-decides next tick against the fielded squads). A staged world's claim suppresses the
        //   step.
        // tests: LineupStageLazyArm.a_step_that_fields_fighters_holds_the_level_open
        // tests: LineupStageLazyArm.un_staged_world_runs_the_step_on_its_first_tick
        // tests: MatchStageTest.adopted_world_does_not_re_run_the_lineup_stage
        // tests: SemWorld.a_lineup_step_that_fields_nobody_lets_tick_one_complete
        if (level_tick_count_ == 1 && !consume_staged_lineup_stage_claim())
        {
            const std::size_t entities_before = oblist.size();
            // rule: An un-staged classic level (solo play) runs its lineup stage step on its first
            //   tick. Here: `run_lineup_stage_step() && oblist grew -> level_done = 0`.
            // tests: LineupStageLazyArm.a_step_that_fields_fighters_holds_the_level_open
            // tests: LineupStageLazyArm.un_staged_world_runs_the_step_on_its_first_tick
            // tests: MatchStageTest.adopted_world_does_not_re_run_the_lineup_stage
            // tests: SemWorld.a_lineup_step_that_fields_nobody_lets_tick_one_complete
            if (run_lineup_stage_step() && oblist.size() > entities_before)
            {
                // The step FIELDED fighters, and this tick's foe scan (fused
                // into the act loops above) has already run — so a level
                // that looked finished must not complete before the squads
                // it just gained are counted. Re-decide on the next tick,
                // against the world the step built. The strip half needs no
                // such note: it retires by marking dead, never by erasing,
                // so it can only ever shrink oblist's live population and a
                // stale scan may stand for one more tick exactly as it does
                // for the classic strip above.
                level_done = 0;
            }
        }
        // A foe merely awaiting a classic respawn (queued, or a corpse the
        // death scan below will queue) still counts as alive: "endless
        // battle" (respawn_mode 2) cannot be won by killing every live foe
        // inside one respawn window. Always false when the engine is off.
        // rule: A classic level with no live foes and no exit pad completes as a win and advances
        //   to the next sequential level - unless a hostile foe is only waiting on a classic
        //   respawn (endless battle), which still counts as alive.
        // tests: SimWorld.r15_end_flag_and_auto_advance_paths
        // tests: RuntimeCoveragePaths.sim_world_batch5_game_end_paths
        // tests: ClassicRespawn.mode2_pending_hostile_ai_blocks_extermination_win
        // tests: RuntimeCoveragePaths.sim_world_tick_branches_for_end_freeze_and_cleanup
        // tests: SimWorldHeadless.freeze_does_not_autocomplete_a_kill_all_level
        if (level_done == 2 &&
            !og::sim::classic_respawn_pending_hostile_foe(*this))
        {
            game_ended = true;
            ending = 0;
            next_level = static_cast<short>(id + 1);
        }
        // A session-layer end (world.end) latches game_ended below; fold it
        // in here so the classic end-of-level revive-all covers end-driven
        // completions too (observable state is unchanged: the fold only
        // reorders the same latch+return ahead of a no-op hook by default).
        // rule: A session-requested end (world.end) stops the level: game_ended latches without
        //   choosing a next level, on classic and scripted levels alike; on classic levels the
        //   respawn engine still runs that tick so its end-of-level revive-all sees the end. An
        //   ended tick skips the stale-reference cleanup and dead sweep.
        // tests: SimWorld.r15_end_flag_and_auto_advance_paths
        // tests: SimWorldHeadless.world_end_stops_a_scripted_level_without_choosing_a_level
        // tests: ClassicRespawn.end_driven_completion_flushes_and_drops_pending_ai
        // tests: ClassicRespawn.end_of_level_revives_pending_heroes_and_clears_queue
        if (end)
            game_ended = true;
        // Classic respawn engine: AFTER the completion decision (its
        // end-of-level revive-all must observe game_ended this same tick,
        // before the session layer stops ticking and persists the roster)
        // and BEFORE the dead sweep (the death scan needs this tick's
        // corpses). No-op when respawn_mode == 0, so the default path stays
        // byte-identical.
        og::sim::classic_respawn_run_tick(*this);
        if (game_ended)
            return;
    }

    // rule: A session-requested end (world.end) stops the level.
    // tests: SimWorld.r15_end_flag_and_auto_advance_paths
    // tests: SimWorldHeadless.world_end_stops_a_scripted_level_without_choosing_a_level
    // tests: ClassicRespawn.end_driven_completion_flushes_and_drops_pending_ai
    // tests: ClassicRespawn.end_of_level_revives_pending_heroes_and_clears_queue
    if (end)
    {
        game_ended = true;
        return;
    }

    // --- Cleanup stale pointers ---
    auto clear_stale_cross_refs = [](walker* ob) {
        // rule: At the end of every unfinished tick, every walker, effect and weapon drops its
        //   links (foe, leader, owner, collide target, stats controller) to walkers that are dead,
        //   so nobody chases, follows or is owned by a corpse into the next tick.
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
        // tests: SemWorld.links_to_live_walkers_survive_the_stale_link_cleanup
        // tests: SemWorld.a_stats_controller_that_died_is_dropped_at_the_end_of_the_tick
        if (ob == nullptr)
            return;

        // rule: At the end of every unfinished tick, every walker, effect and weapon drops its
        //   links (foe, leader, owner, collide target, stats controller) to walkers that are dead,
        //   so nobody chases, follows or is owned by a corpse into the next tick.
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
        // tests: SemWorld.links_to_live_walkers_survive_the_stale_link_cleanup
        // tests: SemWorld.a_stats_controller_that_died_is_dropped_at_the_end_of_the_tick
        if (ob->foe() && ob->foe()->dead())
            ob->set_foe(nullptr);
        // rule: At the end of every unfinished tick, every walker, effect and weapon drops its
        //   links (foe, leader, owner, collide target, stats controller) to walkers that are dead,
        //   so nobody chases, follows or is owned by a corpse into the next tick.
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
        // tests: SemWorld.links_to_live_walkers_survive_the_stale_link_cleanup
        // tests: SemWorld.a_stats_controller_that_died_is_dropped_at_the_end_of_the_tick
        if (ob->leader() && ob->leader()->dead())
            ob->set_leader(nullptr);
        // rule: At the end of every unfinished tick, every walker, effect and weapon drops its
        //   links (foe, leader, owner, collide target, stats controller) to walkers that are dead,
        //   so nobody chases, follows or is owned by a corpse into the next tick.
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
        // tests: SemWorld.links_to_live_walkers_survive_the_stale_link_cleanup
        // tests: SemWorld.a_stats_controller_that_died_is_dropped_at_the_end_of_the_tick
        if (ob->owner() && ob->owner()->dead())
            ob->set_owner(nullptr);
        // rule: At the end of every unfinished tick, every walker, effect and weapon drops its
        //   links (foe, leader, owner, collide target, stats controller) to walkers that are dead,
        //   so nobody chases, follows or is owned by a corpse into the next tick.
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
        // tests: SemWorld.links_to_live_walkers_survive_the_stale_link_cleanup
        // tests: SemWorld.a_stats_controller_that_died_is_dropped_at_the_end_of_the_tick
        if (ob->collide_ob() && ob->collide_ob()->dead())
            ob->set_collide_ob(nullptr);
        // rule: At the end of every unfinished tick, every walker, effect and weapon drops its
        //   links (foe, leader, owner, collide target, stats controller) to walkers that are dead,
        //   so nobody chases, follows or is owned by a corpse into the next tick.
        // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
        // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
        // tests: SemWorld.links_to_live_walkers_survive_the_stale_link_cleanup
        // tests: SemWorld.a_stats_controller_that_died_is_dropped_at_the_end_of_the_tick
        if (statistics* stats = ob->stats();
            stats != nullptr && stats->controller() && stats->controller()->dead())
            stats->set_controller(nullptr);
    };

    // plumbing: stale-reference sweep over the three lists; see above
    for (auto& uptr : oblist)
        clear_stale_cross_refs(uptr.get());

    // plumbing: stale-reference sweep over the three lists; see above
    for (auto& uptr : fxlist)
        clear_stale_cross_refs(uptr.get());

    // plumbing: stale-reference sweep over the three lists; see above
    for (auto& uptr : weaplist)
        clear_stale_cross_refs(uptr.get());

    // --- Remove dead entities ---
    // Note: viewscreen control pointer cleanup is handled by the display
    // runtime since viewscreens are a rendering concern.
    auto& objects = oblist.raw_mutable();
    auto& dead = dead_list.raw_mutable();
    // plumbing: dead sweep over oblist; see below
    for (auto e = objects.begin(); e != objects.end();)
    {
        walker* ob = e->get();
        // A corpse whose respawn entry is still pending stays in the world:
        // the fire path resolves it by id and a cleric can still raise its
        // stain. retire_corpse() disposes of it on the fire tick.
        // rule: At tick end a dead walker leaves the object list for the dead list (so ids and Lua
        //   handles stay valid) and a Living lowers the living census, EXCEPT a hero corpse (it has
        //   a guy record: the roster keeps it) and a corpse whose respawn is still pending (it
        //   stays so the respawn can resolve it and a cleric can raise its stain).
        // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
        // tests: SemWorld.hero_corpses_and_corpses_awaiting_respawn_stay_in_the_world
        if (ob && ob->dead() && ob->myguy == nullptr &&
            !og::sim::respawn_pending_for(*this, ob))
        {
            remove_from_id_index(ob);
            dead.push_back(std::move(*e));

            // rule: At tick end a dead walker leaves the object list for the dead list (so ids and
            //   Lua handles stay valid) and a Living lowers the living census, EXCEPT a hero corpse
            //   (it has a guy record. Here: `Living -> living_count--`.
            // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
            // tests: SemWorld.hero_corpses_and_corpses_awaiting_respawn_stay_in_the_world
            if (ob->query_order() == Order::Living)
                living_count--;

            e = objects.erase(e);
            continue;
        }
        e++;
    }

    // rule: Dead effects and dead weapons are destroyed at tick end (no dead list for them); live
    //   ones stay.
    // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
    // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
    std::erase_if(fxlist.raw_mutable(), [this](const auto& uptr) {
        walker* ob = uptr.get();
        if (!(ob && ob->dead()))
            return false;
        remove_from_id_index(ob);
        return true;
    });

    // rule: Dead effects and dead weapons are destroyed at tick end (no dead list for them); live
    //   ones stay.
    // tests: RuntimeCoveragePaths.sim_world_batch6_cleanup_and_erase_paths_with_hostiles_present
    // tests: SimWorld.r15_normal_tick_cleanup_and_dead_entity_removal
    std::erase_if(weaplist.raw_mutable(), [this](const auto& uptr) {
        walker* ob = uptr.get();
        if (!(ob && ob->dead()))
            return false;
        remove_from_id_index(ob);
        return true;
    });
}

void GameWorld::run_pending_level_on_load()
{
    // rule: A level's on_load script runs exactly once per level id, on the world's first tick of
    //   that level (fresh start or mid-level join), and resets the level tick count; a staged world
    //   that claimed the latch does not run it again.
    // tests: LevelScriptsTest.run_pending_level_on_load_dispatches_once_untensed
    // tests: LevelScriptsTest.claim_level_load_latch_suppresses_the_tick_dispatch
    if (last_level_id_ == id)
        return;
    last_level_id_ = id;
    level_tick_count_ = 0;
    // Level-script on_load: fires on each peer's first tick of a level
    // (fresh start or mid-level join alike — scripts derive state from
    // the world, not from "how long the level existed elsewhere"). A staged
    // lobby world runs this before its first tick instead; the latch above
    // keeps the dispatch once-only either way.
    og::script::hooks::level_load(this);
}

bool GameWorld::run_lineup_stage_step()
{
    // C2: only when NO mode owns the level. A level has exactly one stage
    // step — mode_stage_init's on_mode_init when a mode claimed it, this one
    // when none did (a classic map, or a scripted map whose init refused and
    // fell back to classic rules).
    // rule: A level has exactly one stage step: when a mode owns the level the lineup hook never
    //   runs.
    // tests: LineupStageLazyArm.un_registered_world_dispatches_nothing
    // tests: LineupStageLazyArm.a_step_that_fields_fighters_holds_the_level_open
    // tests: SemWorld.the_lineup_step_never_runs_where_a_mode_owns_the_level
    if (mode.active)
        return false;
    return og::script::hooks::level_lineup_stage(this);
}

// --- Floor-transition landing probe (Z-axis / multi-floor) ------------------
//
// Side-effect-free "can ob stand at (x, y) on target_floor?" used by
// walker::apply_z_motion before every change_floor (and available to any
// other cross-floor arrival). Mirrors the CTF spawn probe (ctf.cpp
// spawn_spot_clear / classic_spot_clear): query_passable would route through
// ob_pass_check, which EATS treasures on overlap and fires collide() side
// effects — a probe must never do either. Grid probe plus a manual
// blocking-entity sweep over the TARGET floor's obmap buckets instead.
// Only ever called on multi-floor code paths, so single-floor levels stay
// byte-identical.

namespace
{

[[nodiscard]] bool landing_blocked_by(const walker* other, const walker* ob,
                                      std::int32_t x, std::int32_t y)
{
    // rule: A spot is blocked for arrival (floor change, wake, relocation) only by an overlapping
    //   live, visible Living, Generator, or solid scenery weapon (door, tree, boulder, or a pack
    //   weapon declaring blocks_placement); missiles, treasures, effects, the dead, the hidden and
    //   the arriving walker itself never block.
    // tests: KitSeamsSmall.blocks_placement_weapon_stops_teleport_spawn_and_floor_landing
    // tests: KitSeams.landing_probes_ignore_a_hidden_walker
    // tests: ZAxis.stair_up_into_occupied_cell_lands_beside_the_blocker
    // tests: SemWorld.only_bodies_generators_and_solid_scenery_block_an_arrival
    if (other == nullptr || other == ob || other->dead() || other->hidden())
        return false;
    const Order order = other->query_order();
    const bool blocking =
        order == Order::Living || order == Order::Generator ||
        (order == Order::Weapon &&
         (other->family() == FAMILY_DOOR || other->family() == FAMILY_TREE ||
          other->family() == FAMILY_BOULDER ||
          og::sim::declares_blocks_placement(*other)));
    if (!blocking)
        return false;
    return x + ob->sizex() > other->xpos() &&
           x < other->xpos() + other->sizex() &&
           y + ob->sizey() > other->ypos() &&
           y < other->ypos() + other->sizey();
}

} // namespace

bool GameWorld::floor_landing_clear(walker* ob, float x, float y,
                                    int target_floor)
{
    // plumbing: floor_landing_clear: null walker -> false; no obmap -> grid answer only
    if (ob == nullptr)
        return false;
    // rule: A walker may arrive at a spot on a floor only if the terrain there admits it (same tile
    //   rules as walking, above) and no blocking entity (above) on that floor overlaps it; the
    //   probe never eats treasure or fires collisions.
    // tests: ZAxis.stair_with_no_clear_arrival_within_radius_denied_until_clear
    // tests: KitSeams.landing_probes_ignore_a_hidden_walker
    // tests: SemWorld.the_landing_probe_never_eats_the_treasure_on_the_spot
    if (!query_grid_passable(x, y, ob, target_floor))
        return false;
    // plumbing: floor_landing_clear: null walker -> false; no obmap -> grid answer only
    if (myobmap == nullptr)
        return true;

    const std::int32_t xi = static_cast<std::int32_t>(x);
    const std::int32_t yi = static_cast<std::int32_t>(y);
    // Walkers register in every 32px obmap bucket their bbox spans; scan all
    // buckets the landing bbox touches on the target floor (a boolean check
    // needs no dedup). Dormant walkers are out of the obmap by design and
    // correctly never block a landing.
    constexpr std::int32_t kBucket = 32;
    // plumbing: bucket sweep over the landing bbox on the target floor; see above
    for (std::int32_t bx = xi; bx < xi + ob->sizex() + kBucket; bx += kBucket)
    {
        // plumbing: bucket sweep over the landing bbox on the target floor; see above
        for (std::int32_t by = yi; by < yi + ob->sizey() + kBucket;
             by += kBucket)
        {
            const std::list<walker*>* pile = myobmap->find_list(
                static_cast<short>(bx), static_cast<short>(by), target_floor);
            // plumbing: bucket sweep over the landing bbox on the target floor; see
            //   above
            if (pile == nullptr)
                continue;
            // plumbing: bucket sweep over the landing bbox on the target floor; see
            //   above
            for (const walker* other : *pile)
            {
                // rule: A walker may arrive at a spot on a floor only if the terrain there admits
                //   it (same tile rules as walking, above) and no blocking entity (above) on that
                //   floor overlaps it. Here: `landing_blocked_by(other) -> false`.
                // tests: ZAxis.stair_with_no_clear_arrival_within_radius_denied_until_clear
                // tests: KitSeams.landing_probes_ignore_a_hidden_walker
                // tests: SemWorld.the_landing_probe_never_eats_the_treasure_on_the_spot
                if (landing_blocked_by(other, ob, xi, yi))
                    return false;
            }
        }
    }
    return true;
}

// --- Delayed-spawn wake placement (see game_world.h) ------------------------
//
// Both helpers are only ever reached when a dormant walker is involved (the
// wake path itself, or a generator pad holding one), so stock levels and all
// parity goldens — none of which author spawn_delay — stay byte-identical.

namespace
{

// Live dormant walker overlapping self's bbox at (x, y) on `floor`.
bool dormant_overlaps_box(const GameWorld& world, const walker* self,
                          std::int32_t x, std::int32_t y, int floor)
{
    // plumbing: oblist scan; see below
    for (const auto& uptr : world.oblist)
    {
        const walker* other = uptr.get();
        // rule: A spot is 'reserved' by a still-dormant walker on the same floor whose box overlaps
        //   it; wake relocation avoids such spots and a generator pad holding one waits
        //   (dormant_occupies_spot).
        // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
        // tests: DelayedSpawnWake.generator_pad_dormant_walker_survives
        // tests: SemWorld.only_a_live_sleeper_on_the_same_floor_reserves_a_spot
        if (other == nullptr || other == self || other->dead() ||
            !other->dormant() || static_cast<int>(other->floor()) != floor)
            continue;
        if (x + self->sizex() > other->xpos() &&
            x < other->xpos() + other->sizex() &&
            y + self->sizey() > other->ypos() &&
            y < other->ypos() + other->sizey())
            return true;
    }
    return false;
}

// Same nudge reach as the air-fall landing search (kFallNudgeRadius): four
// 16px rings around the authored cell.
inline constexpr std::int32_t kWakeNudgeRadius = 4;

} // namespace

bool GameWorld::dormant_occupies_spot(const walker* probe) const
{
    return dormant_overlaps_box(*this, probe, probe->xpos(), probe->ypos(),
                                probe->floor());
}

bool GameWorld::wake_spot_blocked(walker* ob)
{
    // plumbing: wake_spot_blocked: null walker or no obmap -> not blocked
    if (ob == nullptr || myobmap == nullptr)
        return false;
    const std::int32_t xi = ob->xpos();
    const std::int32_t yi = ob->ypos();
    // Same bucket sweep + blocking-entity shape as floor_landing_clear, minus
    // its grid half (see game_world.h). The waker itself is dormant — never
    // registered — so it cannot self-block.
    constexpr std::int32_t kBucket = 32;
    // plumbing: bucket sweep on the waker's floor; see below
    for (std::int32_t bx = xi; bx < xi + ob->sizex() + kBucket; bx += kBucket)
    {
        // plumbing: bucket sweep on the waker's floor; see below
        for (std::int32_t by = yi; by < yi + ob->sizey() + kBucket;
             by += kBucket)
        {
            const std::list<walker*>* pile = myobmap->find_list(
                static_cast<short>(bx), static_cast<short>(by), ob->floor());
            // plumbing: bucket sweep on the waker's floor; see below
            if (pile == nullptr)
                continue;
            // plumbing: bucket sweep on the waker's floor; see below
            for (const walker* other : *pile)
            {
                // rule: A delayed spawn's own spot counts as blocked by exactly the arrival
                //   blockers of above (live visible Livings, Generators, solid scenery) on its
                //   floor; terrain is not consulted (the authored spot is trusted).
                // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
                // tests: DelayedSpawnWake.blocked_wake_defers_then_completes
                // tests: SemWorld.a_missile_on_a_sleepers_spot_does_not_block_the_wake
                if (landing_blocked_by(other, ob, xi, yi))
                    return true;
            }
        }
    }
    return false;
}

bool GameWorld::relocate_to_nearest_wake_spot(walker* ob)
{
    const PixieData& g = grid_for_floor(ob->floor());
    // rule: A blocked waker moves to the first clear grid-aligned cell on the nearest ring (0..4
    //   cells out from its centre cell), scanning each ring row by row (top-left first), skipping
    //   cells off the grid, cells whose terrain or occupants refuse it, and cells a still-dormant
    //   walker reserves; no RNG. With no clear cell (or no grid) it does not move.
    // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
    // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
    // tests: SemWorld.a_sleeper_walled_in_beyond_four_rings_stays_put
    if (!g.valid())
        return false;
    const std::int32_t cx = (ob->xpos() + ob->sizex() / 2) / GRID_SIZE;
    const std::int32_t cy = (ob->ypos() + ob->sizey() / 2) / GRID_SIZE;
    for (std::int32_t r = 0; r <= kWakeNudgeRadius; ++r)
    {
        // rule: A blocked waker moves to the first clear grid-aligned cell on the nearest ring
        //   (0..4 cells out from its centre cell), scanning each ring row by row (top-left first),
        //   skipping cells off the grid, cells whose terrain or occupants refuse it, and cells a
        //   still-dormant walker reserves.
        // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
        // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
        // tests: SemWorld.a_sleeper_walled_in_beyond_four_rings_stays_put
        for (std::int32_t dy = -r; dy <= r; ++dy)
        {
            // rule: A blocked waker moves to the first clear grid-aligned cell on the nearest ring
            //   (0..4 cells out from its centre cell), scanning each ring row by row (top-left
            //   first), skipping cells off the grid, cells whose terrain or occupants refuse it,
            //   and cells a still-dormant walker reserves.
            // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
            // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
            // tests: SemWorld.a_sleeper_walled_in_beyond_four_rings_stays_put
            for (std::int32_t dx = -r; dx <= r; ++dx)
            {
                // rule: A blocked waker moves to the first clear grid-aligned cell on the nearest
                //   ring (0..4 cells out from its centre cell), scanning each ring row by row
                //   (top-left first), skipping cells off the grid, cells whose terrain or occupants
                //   refuse it, and cells a still-dormant walker reserves.
                // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
                // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
                // tests: SemWorld.a_sleeper_walled_in_beyond_four_rings_stays_put
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue; // ring perimeter only
                const std::int32_t nx = cx + dx;
                const std::int32_t ny = cy + dy;
                // rule: A blocked waker moves to the first clear grid-aligned cell on the nearest
                //   ring (0..4 cells out from its centre cell), scanning each ring row by row
                //   (top-left first), skipping cells off the grid, cells whose terrain or occupants
                //   refuse it, and cells a still-dormant walker reserves.
                // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
                // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
                // tests: SemWorld.a_sleeper_walled_in_beyond_four_rings_stays_put
                if (nx < 0 || ny < 0 || nx >= g.w || ny >= g.h)
                    continue;
                const std::int32_t px = nx * GRID_SIZE;
                const std::int32_t py = ny * GRID_SIZE;
                // rule: A blocked waker moves to the first clear grid-aligned cell on the nearest
                //   ring (0..4 cells out from its centre cell), scanning each ring row by row
                //   (top-left first), skipping cells off the grid, cells whose terrain or occupants
                //   refuse it, and cells a still-dormant walker reserves.
                // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
                // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
                // tests: SemWorld.a_sleeper_walled_in_beyond_four_rings_stays_put
                if (!floor_landing_clear(ob, static_cast<float>(px),
                                         static_cast<float>(py), ob->floor()))
                    continue;
                // Keep relocations off still-dormant footprints too: the
                // displaced neighbour would only have to move again on its
                // own wake tick.
                // rule: A blocked waker moves to the first clear grid-aligned cell on the nearest
                //   ring (0..4 cells out from its centre cell), scanning each ring row by row
                //   (top-left first), skipping cells off the grid, cells whose terrain or occupants
                //   refuse it, and cells a still-dormant walker reserves.
                // tests: DelayedSpawnWake.packed_formation_equal_delays_all_wake
                // tests: DelayedSpawnWake.an_edge_wake_skips_off_grid_and_still_dormant_cells
                // tests: SemWorld.a_sleeper_walled_in_beyond_four_rings_stays_put
                if (dormant_overlaps_box(*this, ob, px, py, ob->floor()))
                    continue;
                // setxy on a dormant walker performs no obmap bookkeeping
                // (dormancy is the "never in the obmap" state); the caller's
                // set_dormant(false) registers it at this new spot.
                ob->setxy(static_cast<short>(px), static_cast<short>(py));
                return true;
            }
        }
    }
    return false;
}

// SCEN_TYPE_SAVE_ALL scoping probe (see game_world.h). Only ever consulted
// when a Living dies on a SAVE_ALL level, so the linear scan is cheap; the
// flag is only ever set by the level loader (npc_flags bit 2), never at
// runtime, so summons/generator output can never widen the watched set.
bool GameWorld::has_save_all_protected() const
{
    // plumbing: scans oblist then dead_list; see below
    for (const auto& uptr : oblist)
    {
        // rule: On a SAVE_ALL level, the mission's 'protect everyone' rule is scoped to the
        //   loader-flagged NPCs whenever at least one exists (alive or already dead); with none
        //   flagged the legacy whole-team rule applies.
        // tests: SaveAllScoping.protected_flag_scopes_the_watch_to_flagged_walkers
        // tests: SaveAllScoping.legacy_named_team0_death_still_fails_mission
        // tests: SaveAllScoping.foreign_color_company_hero_death_does_not_fail_mission
        // tests: SemWorld.a_dead_protected_npc_still_scopes_the_save_all_rule
        if (uptr != nullptr && uptr->save_all_protected())
            return true;
    }
    for (const auto& uptr : dead_list)
    {
        // rule: On a SAVE_ALL level, the mission's 'protect everyone' rule is scoped to the
        //   loader-flagged NPCs whenever at least one exists (alive or already dead).
        // tests: SaveAllScoping.protected_flag_scopes_the_watch_to_flagged_walkers
        // tests: SaveAllScoping.legacy_named_team0_death_still_fails_mission
        // tests: SaveAllScoping.foreign_color_company_hero_death_does_not_fail_mission
        // tests: SemWorld.a_dead_protected_npc_still_scopes_the_save_all_rule
        if (uptr != nullptr && uptr->save_all_protected())
            return true;
    }
    return false;
}

// Floor-awareness HUD label (contract in game_world.h). D5: game modes
// re-label by branching HERE and only here — both inputs (world.type,
// world.id) are GameWorld-visible, so no campaign lookup is ever needed.
// Formats: "" (single-floor non-tower, and the Gate); "FLR: f/n" (normal
// multifloor, 1-indexed); "FLR: N" (tower single-story floor N); "F N: f/n"
// as "F{N}: {f}/{n}" (tower multi-story). Appended at EOF: the parity canary
// pins in this file (1680/1682/1803) sit far above and must never shift.
std::string floor_hud_label(const GameWorld& world, int walker_floor)
{
    const int floors = world.floor_count();
    // The Gate (id == kTowerGateLevel) deliberately falls through to the
    // non-tower branch: it is floor 0 of the climb, not a numbered floor.
    const bool tower = (world.type & GameWorld::TYPE_TOWER) != 0
        && world.id > og::kTowerGateLevel;
    const int f = std::clamp(walker_floor, 0, floors - 1);
    // rule: The HUD floor label is empty on single-floor levels and the tower Gate, 'FLR: f/n'
    //   (1-based, clamped) on multi-floor levels, 'FLR: N' on a single-story tower floor N (id -
    //   gate), and 'FN: f/n' on a multi-story tower floor.
    // tests: FloorHudLabel.empty_on_single_floor, FloorHudLabel.multifloor_is_one_indexed
    // tests: FloorHudLabel.clamps_out_of_range_walker_floor
    // tests: FloorHudLabel.tower_single_story_shows_absolute_floor
    // tests: FloorHudLabel.tower_multi_story_combines_floor_and_story
    // tests: FloorHudLabel.tower_gate_is_unlabeled
    if (!tower)
    {
        // rule: The HUD floor label is empty on single-floor levels and the tower Gate, 'FLR.
        // tests: FloorHudLabel.empty_on_single_floor, FloorHudLabel.multifloor_is_one_indexed
        // tests: FloorHudLabel.clamps_out_of_range_walker_floor
        // tests: FloorHudLabel.tower_single_story_shows_absolute_floor
        // tests: FloorHudLabel.tower_multi_story_combines_floor_and_story
        // tests: FloorHudLabel.tower_gate_is_unlabeled
        if (floors <= 1)
            return "";
        return std::format("FLR: {}/{}", f + 1, floors);
    }
    if (floors <= 1)
        return std::format("FLR: {}", world.id - og::kTowerGateLevel);
    return std::format("F{}: {}/{}", world.id - og::kTowerGateLevel,
                       f + 1, floors);
}

// Fire-path disposal of a corpse the dead sweep left in oblist while its
// respawn entry was pending (contract in game_world.h): the sweep's exact
// motion, run on the fire tick instead — unindex, move the body to
// dead_list so raw borrows and Lua walker handles stay valid, and keep
// living_count honest. Appended at EOF so the parity canary pins above
// never shift.
void GameWorld::retire_corpse(walker* ob)
{
    auto& objects = oblist.raw_mutable();
    auto& dead = dead_list.raw_mutable();
    // plumbing: oblist scan for the corpse; see below
    for (auto e = objects.begin(); e != objects.end(); ++e)
    {
        // rule: On a respawn fire tick the held corpse is retired exactly as the dead sweep would
        //   have done it (moved to dead_list, unindexed, living census lowered for a Living); a
        //   walker not in the object list is ignored.
        // tests: ModesSoccer.slain_bot_leaves_a_raisable_corpse_until_respawn
        // tests: SemWorld.retiring_a_held_corpse_files_it_dead_exactly_once
        if (e->get() != ob)
            continue;
        remove_from_id_index(ob);
        dead.push_back(std::move(*e));
        if (ob->query_order() == Order::Living)
            living_count--;
        objects.erase(e);
        return;
    }
}

// Staged-lobby adoption (#218): the destination takes the staged world's
// scripting VM wholesale — see the header comment. Appended at EOF so the
// parity canary pins above never shift.
void GameWorld::adopt_scripts_from(GameWorld& source) noexcept
{
    // plumbing: adopt_scripts_from(self) is a no-op
    if (&source == this)
        return;
    scripts_ = std::move(source.scripts_);
}

// The walk of find_nearest_blood, keeping every stain it would consider
// instead of the nearest alone, on who's own floor, sorted nearest first by
// the same squared centre distance; a stable sort, so equal distances keep
// fxlist order. find_nearest_blood answers the nearest alone (the classic
// raises read it). Appended below the last parity canary pin in this file
// so the pins above never shift.
std::vector<walker*> GameWorld::find_blood_in_reach(walker* who)
{
    std::vector<std::pair<std::int32_t, walker*>> found;
    // plumbing: null finder -> empty
    if (!who)
        return {};

    // plumbing: fxlist scan and result copy; see below
    for (auto& uptr : fxlist)
    {
        walker* w = uptr.get();
        // rule: The new-specials raise finder lists every live blood stain on the finder's own
        //   floor within squared centre distance under 800, nearest first, ties in effect-list
        //   order, drawing no RNG. Like find_nearest_blood (above) it is floor-scoped.
        // tests: QuietProbe.find_blood_in_reach_walks_the_finders_stains_nearest_first
        if (w && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_STAIN && !w->dead() &&
            w->floor() == who->floor())
        {
            const std::int32_t distance =
                static_cast<std::int32_t>(who->distance_to_ob_center(w));
            // rule: The new-specials raise finder lists every live blood stain on the finder's own
            //   floor within squared centre distance under 800, nearest first, ties in effect-list
            //   order, drawing no RNG. Here: `distance < 800`.
            // tests: QuietProbe.find_blood_in_reach_walks_the_finders_stains_nearest_first
            if (distance < 800)
                found.emplace_back(distance, w);
        }
    }

    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<walker*> stains;
    stains.reserve(found.size());
    // plumbing: fxlist scan and result copy; see above
    for (const auto& entry : found)
        stains.push_back(entry.second);
    return stains;
}
