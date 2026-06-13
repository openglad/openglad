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
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/pixdefs.h>

#include <algorithm>
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

bool is_tracked_entity(const GameWorld& world, const walker* candidate)
{
    if (candidate == nullptr)
        return false;

    return contains_walker_ptr(world.oblist, candidate)
        || contains_walker_ptr(world.fxlist, candidate)
        || contains_walker_ptr(world.weaplist, candidate)
        || contains_walker_ptr(world.dead_list, candidate);
}

void sanitize_owner_chain_link(const GameWorld& world, walker* entity)
{
    if (entity == nullptr)
        return;

    constexpr int kMaxOwnerDepth = 16;
    walker* current = entity;
    for (int depth = 0; depth < kMaxOwnerDepth; ++depth)
    {
        walker* owner = current->owner();
        if (owner == nullptr || owner == current)
            return;
        if (!is_tracked_entity(world, owner))
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
    if (entity.foe() == &entity)
        entity.set_foe(&entity);
    if (entity.leader() == &entity)
        entity.set_leader(&entity);
    if (entity.owner() == &entity)
        entity.set_owner(&entity);
    if (entity.collide_ob() == &entity)
        entity.set_collide_ob(&entity);

    if (statistics* stats = entity.stats();
        stats != nullptr && stats->controller() == &entity)
    {
        stats->set_controller(&entity);
    }
}
}

namespace og::sim {
// Test hook to shorten mission timeout checks in deterministic harnesses.
std::int32_t g_test_level_tick_limit_override = 0;

namespace {
thread_local IRandom** g_sim_random_override_ref = nullptr;
}

IRandom* sim_random_override()
{
    return (g_sim_random_override_ref != nullptr)
        ? *g_sim_random_override_ref
        : nullptr;
}

void set_sim_random_override(IRandom** rng_ref)
{
    g_sim_random_override_ref = rng_ref;
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

GameWorld::~GameWorld()
{
    if (detach_callback_)
        detach_callback_();
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
    return entities_;
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
    if (entities_.empty())
        return;

    prepare_remove(entities_.back().get());
    entities_.pop_back();
    invalidate_owner();
}

void GameWorld::EntityList::pop_front()
{
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
    if (this == &other || other.entities_.empty())
        return;

    prepare_insert(other.entities_);
    entities_.splice(position, other.entities_);
    other.invalidate_owner();
    invalidate_owner();
}

void GameWorld::EntityList::splice(const_iterator position, Storage& other)
{
    if (other.empty())
        return;

    prepare_insert(other);
    entities_.splice(position, other);
    invalidate_owner();
}

void GameWorld::EntityList::splice_into(Storage& destination)
{
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
    if (owner_ == nullptr || entity == nullptr)
        return;

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
    if (entity == nullptr)
        return;

    if (owner_ != nullptr && participates_in_id_index_ && entity->entity_id() != 0)
        owner_->removed_entity_ids_.push_back(entity->entity_id());
}

void GameWorld::EntityList::prepare_insert(Storage& entities)
{
    for (auto& uptr : entities)
        prepare_insert(uptr.get());
}

void GameWorld::EntityList::prepare_remove(Storage& entities)
{
    for (auto& uptr : entities)
        prepare_remove(uptr.get());
}

void GameWorld::EntityList::invalidate_owner()
{
    if (owner_ != nullptr)
        owner_->invalidate_entity_tracking();
}

const PixieData* GameWorld::configure_existing_entity(walker& entity, Order order, std::int32_t family)
{
    if (!entity_configurator)
        return nullptr;
    return entity_configurator(entity, order, family);
}

void GameWorld::set_entity_derived_stats(walker* entity, Order order, std::int32_t family)
{
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

    if ((context.save == nullptr ||
         context.sim_events == nullptr ||
         context.config == nullptr) &&
        current_game != nullptr &&
        current_game->world == this)
    {
        if (context.save == nullptr)
            context.save = current_game->save;
        if (context.sim_events == nullptr)
            context.sim_events = current_game->sim_events;
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
    if (entity.entity_id() == 0)
        entity.set_entity_id(next_entity_id_++);
    else if (entity.entity_id() >= next_entity_id_)
        next_entity_id_ = entity.entity_id() + 1;

    return entity.entity_id();
}

void GameWorld::index_entity(walker& entity)
{
    attach_entity_to_world(entity);
    if (entity.entity_id() == 0)
        return;

    id_index_[entity.entity_id()] = &entity;
}

void GameWorld::remove_from_id_index(const walker* entity)
{
    if (entity == nullptr || entity->entity_id() == 0)
        return;

    if (entity_tracking_dirty_)
        rebuild_id_index();

    const auto it = id_index_.find(entity->entity_id());
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
        for (auto& uptr : entities)
        {
            walker* entity = uptr.get();
            if (entity == nullptr)
                continue;

            attach_entity_to_world(*entity);
            if (entity->entity_id() >= next_entity_id_)
                next_entity_id_ = entity->entity_id() + 1;
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
    if (entity == nullptr)
        return 0;

    auto* mutable_world = const_cast<GameWorld*>(this);
    if (entity_tracking_dirty_)
        mutable_world->rebuild_id_index();

    const auto it = std::find_if(
        id_index_.begin(), id_index_.end(),
        [entity](const auto& entry) {
            return entry.second == entity;
        });
    return (it == id_index_.end()) ? 0 : it->first;
}

const walker* GameWorld::find_by_id(std::uint32_t entity_id) const
{
    if (entity_id == 0)
        return nullptr;

    auto* mutable_world = const_cast<GameWorld*>(this);
    if (entity_tracking_dirty_)
        mutable_world->rebuild_id_index();

    auto it = id_index_.find(entity_id);
    if (it == id_index_.end() ||
        it->second == nullptr ||
        it->second->entity_id() != entity_id ||
        it->second->owning_world_ != this)
    {
        mutable_world->rebuild_id_index();
        it = mutable_world->id_index_.find(entity_id);
    }

    return (it == id_index_.end()) ? nullptr : it->second;
}

void GameWorld::move_entities_from(GameWorld& source)
{
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
    if (!entity_factory)
        return nullptr;

    auto w = entity_factory(order, family);
    if (!w)
        return nullptr;

    if (count_living)
        living_count++;

    auto& entries = target_list.raw_mutable();
    walker* raw = w.get();
    assign_entity_id(*raw);
    refresh_self_reference_ids(*raw);
    raw->mark_all_dirty();
    if (atstart)
        entries.push_front(std::move(w));
    else
        entries.push_back(std::move(w));
    index_entity(*raw);
    return raw;
}

walker* GameWorld::add_ob(Order order, std::int32_t family, bool atstart)
{
    if (order == Order::Weapon)
        return add_to_list(order, family, weaplist, false, atstart);

    return add_to_list(order, family, oblist, order == Order::Living, atstart);
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

    auto& weapons = weaplist.raw_mutable();
    auto e = std::find_if(weapons.begin(), weapons.end(), pred);
    if (e != weapons.end())
    {
        remove_from_id_index(e->get());
        weapons.erase(e);
        return 1;
    }

    auto& effects = fxlist.raw_mutable();
    auto f = std::find_if(effects.begin(), effects.end(), pred);
    if (f != effects.end())
    {
        remove_from_id_index(f->get());
        effects.erase(f);
        return 1;
    }

    auto& objects = oblist.raw_mutable();
    auto g = std::find_if(objects.begin(), objects.end(), pred);
    if (g != objects.end())
    {
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
    if (ob == nullptr)
        return false;

    std::int32_t xtrax = 1;
    std::int32_t xtray = 1;
    const std::int32_t x_i = static_cast<std::int32_t>(x);
    const std::int32_t y_i = static_cast<std::int32_t>(y);
    const std::int32_t xover = x_i + ob->sizex();
    const std::int32_t yover = y_i + ob->sizey();

    if (x_i < 0 || y_i < 0 || xover >= pixmaxx || yover >= pixmaxy)
        return false;

    if (ob->stats()->query_bit_flags(BIT_ETHEREAL))
        return true;

    if (!grid.valid())
        return false;

    if ((xover % GRID_SIZE) == 0)
        xtrax = 0;
    if ((yover % GRID_SIZE) == 0)
        xtray = 0;

    const std::int32_t xtarg = (xover / GRID_SIZE) + xtrax;
    const std::int32_t ytarg = (yover / GRID_SIZE) + xtray;

    for (std::int32_t i = x_i / GRID_SIZE; i < xtarg; ++i)
    {
        for (std::int32_t j = y_i / GRID_SIZE; j < ytarg; ++j)
        {
            switch (static_cast<unsigned char>(grid.data[i + grid.w * j]))
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
                case PIX_TREE_M1:
                case PIX_TREE_ML:
                case PIX_TREE_MR:
                case PIX_TREE_MT:
                case PIX_TREE_T1:
                    if (ob->stats()->query_bit_flags(BIT_FORESTWALK))
                        break;
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    return false;
                case PIX_TREE_B1:
                    if (ob->query_order() == Order::Weapon ||
                        ob->stats()->query_bit_flags(BIT_FORESTWALK))
                    {
                        break;
                    }
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    return false;

                case PIX_H_WALL1:
                case PIX_WALL2:
                case PIX_WALL3:
                case PIX_WALL_LL:
                case PIX_WALLTOP_H:
                    return false;

                case PIX_WALL4:
                case PIX_WALL5:
                case PIX_WALL_ARROW_GRASS:
                case PIX_WALL_ARROW_FLOOR:
                case PIX_WALL_ARROW_GRASS_DARK:
                    if (ob->query_order() == Order::Living)
                        return false;

                    // Authored non-living entities (e.g. reserved-team
                    // Specials wandering via ACT_RANDOM) path with no owner;
                    // the shooter-distance gamble below is meaningless
                    // without one, so the arrow wall stays solid for them.
                    if (ob->owner() == nullptr)
                        return false;

                    if (std::abs(ob->xpos() - ob->owner()->xpos()) >
                        std::abs(ob->ypos() - ob->owner()->ypos()))
                    {
                        std::int32_t dist = std::abs(ob->xpos() - ob->owner()->xpos());
                        dist -= (GRID_SIZE / 2);
                        if (dist < GRID_SIZE)
                            dist += GRID_SIZE;
                        if (rng_.next(dist / GRID_SIZE))
                            return false;
                    }
                    else
                    {
                        std::int32_t dist = std::abs(ob->ypos() - ob->owner()->ypos());
                        dist -= (GRID_SIZE / 2);
                        if (dist < GRID_SIZE)
                            dist += GRID_SIZE;
                        if (rng_.next(dist / GRID_SIZE))
                            return false;
                    }
                    [[fallthrough]];
                case PIX_WATER1:
                case PIX_WATER2:
                case PIX_WATER3:
                case PIX_WATERGRASS_LL:
                case PIX_WATERGRASS_LR:
                case PIX_WATERGRASS_UL:
                case PIX_WATERGRASS_UR:
                case PIX_WATERGRASS_U:
                case PIX_WATERGRASS_L:
                case PIX_WATERGRASS_R:
                case PIX_WATERGRASS_D:
                case PIX_WALLSIDE_L:
                case PIX_WALLSIDE1:
                case PIX_WALLSIDE_R:
                case PIX_WALLSIDE_C:
                case PIX_WALLSIDE_CRACK_C1:
                case PIX_TORCH1:
                case PIX_TORCH2:
                case PIX_TORCH3:
                case PIX_BRAZIER1:
                case PIX_COLUMN1:
                case PIX_COLUMN2:
                case PIX_BOULDER_1:
                case PIX_BOULDER_2:
                case PIX_BOULDER_3:
                case PIX_BOULDER_4:
                    if (ob->query_order() == Order::Weapon)
                        break;
                    if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left())
                        break;
                    return false;
                default:
                    return false;
            }
        }
    }

    return true;
}

bool GameWorld::query_object_passable(float x, float y, walker* ob)
{
    if (ob == nullptr)
        return false;

    if (ob->dead())
        return true;

    if (!myobmap)
        return false;

    return myobmap->query_list(ob, static_cast<short>(x), static_cast<short>(y));
}

bool GameWorld::query_passable(float x, float y, walker* ob)
{
    return query_grid_passable(x, y, ob) && query_object_passable(x, y, ob);
}

walker* GameWorld::find_near_foe(walker* ob)
{
    if (!ob)
    {
        Log("no ob in find near foe.\n");
        return nullptr;
    }

    if (!myobmap)
        return find_far_foe(ob);

    short targx = ob->xpos();
    short targy = ob->ypos();
    short spread = 1;
    short xchange = 0;
    short resolution = myobmap->obmapres;

    while (spread < MAX_SPREAD)
    {
        for (short loop = 0; loop < spread; ++loop)
        {
            if ((xchange % 2) == 0)
            {
                targx += resolution;
                if (targx <= 0 || targx >= pixmaxx)
                    return find_far_foe(ob);
            }
            else
            {
                targy += resolution;
                if (targy <= 0 || targy >= pixmaxy)
                    return find_far_foe(ob);
            }

            std::list<walker*>& ls = myobmap->obmap_get_list(targx, targy);
	            for (auto it = ls.begin(); it != ls.end(); )
            {
                walker* w = *it;
                if (!is_tracked_entity(*this, w))
                {
                    it = ls.erase(it);
                    continue;
                }
                sanitize_owner_chain_link(*this, ob);
                sanitize_owner_chain_link(*this, w);
                if (!w->dead() && ob->is_friendly(w) == 0 &&
                    rng_.next(w->invisibility_left() / 20) == 0)
                {
                    if (w->query_order() == Order::Living ||
                        w->query_order() == Order::Generator)
                    {
                        return w;
                    }
                }
                ++it;
            }
        }

        ++xchange;
        if ((xchange % 2) == 0)
        {
            resolution = static_cast<short>(-resolution);
            ++spread;
        }
    }

    return find_far_foe(ob);
}

walker* GameWorld::find_far_foe(walker* ob)
{
    if (!ob)
    {
        Log("no ob in find far foe.\n");
        return nullptr;
    }

    walker* endfoe = nullptr;
    std::int32_t distance = 10000;
    ob->stats()->set_last_distance(10000);
    sanitize_owner_chain_link(*this, ob);

    for (auto& uptr : oblist)
    {
        walker* foe = uptr.get();
        if (foe == nullptr || foe->dead())
            continue;

        sanitize_owner_chain_link(*this, foe);

        if (ob->is_friendly(foe) == 0 &&
            (foe->query_order() == Order::Living ||
             foe->query_order() == Order::Generator) &&
            (rng_.next(foe->invisibility_left() / 20) == 0))
        {
            const std::int32_t tempdistance = ob->distance_to_ob(foe);
            if (tempdistance < distance)
            {
                distance = tempdistance;
                endfoe = foe;
            }
        }
    }

    return endfoe;
}

walker* GameWorld::find_nearest_blood(walker* who)
{
    if (!who)
        return nullptr;

    std::int32_t distance = 800;
    walker* returnob = nullptr;

    for (auto& uptr : fxlist)
    {
        walker* w = uptr.get();
        if (w && w->query_order() == Order::Treasure &&
            w->family() == FAMILY_STAIN && !w->dead())
        {
            const std::int32_t newdistance =
                static_cast<std::int32_t>(who->distance_to_ob_center(w));
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
    if (!ob)
        return nullptr;

    walker* returnob = nullptr;
    std::uint32_t distance = 32000;

    for (auto& uptr : oblist)
    {
        walker* w = uptr.get();
        if (w && w->user() != -1)
        {
            const std::uint32_t tempdistance = ob->distance_to_ob(w);
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
    if (howmany != nullptr)
        *howmany = 0;

    if (!ob || howmany == nullptr)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() && ob->distance_to_ob(w) <= range)
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
    if (howmany != nullptr)
        *howmany = 0;

    if (!ob || howmany == nullptr)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() &&
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
    if (howmany != nullptr)
        *howmany = 0;

    if (!ob || howmany == nullptr)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() &&
            w->query_order() == Order::Weapon &&
            ob->is_friendly(w) &&
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
    if (howmany != nullptr)
        *howmany = 0;

    if (!ob || howmany == nullptr)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() &&
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
    pixmaxx = 0;
    pixmaxy = 0;
    mysmoother.reset();
}

void GameWorld::create_new_grid()
{
    clear_grid_dirty_tiles();
    grid.free();

    grid.frames = 1;
    grid.w = 40;
    grid.h = 60;
    pixmaxx = grid.w * GRID_SIZE;
    pixmaxy = grid.h * GRID_SIZE;

    const int size = grid.w * grid.h;
    grid.data = std::make_unique<unsigned char[]>(size);
    for (int i = 0; i < size; i++)
    {
        switch (rng_.next(4))
        {
            case 0: grid.data[i] = PIX_GRASS1; break;
            case 1: grid.data[i] = PIX_GRASS2; break;
            case 2: grid.data[i] = PIX_GRASS3; break;
            case 3: grid.data[i] = PIX_GRASS4; break;
        }
    }

    mysmoother.set_target(grid);
}

void GameWorld::resize_grid(int width, int height)
{
    // Size is limited to one byte in the file format
    if (width < 3 || height < 3 || width > 255 || height > 255)
    {
        Log("Can't resize grid to these dimensions: {}x{}\n", width, height);
        return;
    }

    auto random_grass_tile = [this]() -> unsigned char {
        switch (rng_.next(4))
        {
            case 0: return PIX_GRASS1;
            case 1: return PIX_GRASS2;
            case 2: return PIX_GRASS3;
            case 3: return PIX_GRASS4;
        }
        return PIX_GRASS1;
    };

    const int size = width * height;
    auto new_grid = std::make_unique<unsigned char[]>(size);

    for (int i = 0; i < width; i++)
    {
        for (int j = 0; j < height; j++)
        {
            if (i < grid.w && j < grid.h)
                new_grid[j * width + i] = grid.data[j * grid.w + i];
            else
                new_grid[j * width + i] = random_grass_tile();
        }
    }

    grid.free();
    grid.data = std::move(new_grid);
    grid.frames = 1;
    grid.w = static_cast<unsigned char>(width);
    grid.h = static_cast<unsigned char>(height);
    clear_grid_dirty_tiles();
    pixmaxx = grid.w * GRID_SIZE;
    pixmaxy = grid.h * GRID_SIZE;

    mysmoother.set_target(grid);

    const int x = 0;
    const int y = 0;
    const int w = grid.w * GRID_SIZE;
    const int h = grid.h * GRID_SIZE;
    auto off_map = [x, y, w, h](const std::unique_ptr<walker>& uptr) {
        walker* ob = uptr.get();
        return ob == nullptr || (x > ob->xpos() || ob->xpos() >= x + w ||
                                 y > ob->ypos() || ob->ypos() >= y + h);
    };

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

    if (!myobmap)
    {
        myobmap = std::make_unique<obmap>();
        return;
    }

    // Walkers can outlive a world-bound obmap during tests and editor flows.
    // After list teardown, clear any stale spatial index entries defensively.
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
}

char GameWorld::damage_tile(short xloc, short yloc)
{
    if (!grid.valid())
        return 0;

    const short xover = static_cast<short>(xloc / GRID_SIZE);
    const short yover = static_cast<short>(yloc / GRID_SIZE);

    if (xover < 0 || yover < 0)
        return 0;
    if (xover >= grid.w || yover >= grid.h)
        return 0;

    const int gridloc = yover * grid.w + xover;
    unsigned char next_value = grid.data[gridloc];
    switch (static_cast<unsigned char>(grid.data[gridloc]))
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

    if (grid.data[gridloc] != next_value)
    {
        grid.data[gridloc] = next_value;
        const std::pair<short, short> coord{xover, yover};
        if (std::find(grid_dirty_tiles_.begin(), grid_dirty_tiles_.end(), coord) ==
            grid_dirty_tiles_.end())
        {
            grid_dirty_tiles_.push_back(coord);
        }
    }

    return static_cast<char>(grid.data[gridloc]);
}

short GameWorld::remaining_foes(walker* myguy) const
{
    if (!myguy)
        return 0;

    short myfoes = 0;
    for (auto& uptr : oblist)
    {
        walker* w = uptr.get();
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
    if (current_game == nullptr || current_game->sim_events == nullptr)
        return;

    og::sim::SimEventLog& events = *current_game->sim_events;

    game_ended = false;
    next_level = -1;
    ending = 0;
    level_done = 2; // unless we find valid foes while looping

    tick_count_++;
    events.current_tick_ = tick_count_;
    if (last_level_id_ != id)
    {
        last_level_id_ = id;
        level_tick_count_ = 0;
    }
    level_tick_count_++;

    std::uint32_t max_level_ticks = 36000;
    if (og::sim::g_test_level_tick_limit_override > 0)
        max_level_ticks = static_cast<std::uint32_t>(og::sim::g_test_level_tick_limit_override);
    if (level_tick_count_ > max_level_ticks)
    {
        // Hard mission timeout safety net to avoid unbounded gameplay loops.
        game_ended = true;
        ending = 1;
        next_level = -1;
        events.push_notification("Mission timed out. Retreating.", 40);
        return;
    }

    if (enemy_freeze)
        enemy_freeze--;
    if (enemy_freeze == 1)
    {
        current_palette_id = 0;
        events.push(og::sim::EventKind::SetPalette, 0, 0);
    }

    // --- Entity act phase ---
    bool printed_time = false;
    for (auto& uptr : oblist)
    {
        if (withdraw_requested)
            break;

        walker* ob = uptr.get();
        if (!enemy_freeze) // normal functionality
        {
            if (ob && !ob->dead())
            {
                ob->set_in_act(true);
                ob->act();
                ob->set_in_act(false);
                if (ob && !ob->dead())
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                        ob->query_order() == Order::Living)
                        level_done = 0;
                    if (ob->foe() == nullptr && ob->leader() == nullptr)
                        ob->set_foe(find_far_foe(ob));
                }
            }
        }
        else // enemy livings are frozen
        {
            if (!(enemy_freeze % 10) && !printed_time)
            {
                std::string obmessage = std::format("TIME LEFT: {}", enemy_freeze);
                events.push_notification(obmessage, 10);
                printed_time = true;
            }
            if (ob && !ob->dead() &&
                (((ob->query_order() != Order::Living) &&
                  (ob->query_order() != Order::Generator)) ||
                 ob->is_friendly_to_team(static_cast<unsigned char>(my_team))))
            {
                ob->act();
                if (ob && !ob->dead())
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                        ob->query_order() == Order::Living)
                        level_done = 0;
                }
            }
        }
    }

    if (withdraw_requested)
        return;

    // --- Weapon act phase ---
    for (auto& uptr : weaplist)
    {
        if (withdraw_requested)
            break;

        walker* ob = uptr.get();
        if (ob && !ob->dead())
        {
            ob->act();
            if (ob && !ob->dead())
            {
                if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                    ob->query_order() == Order::Living)
                    level_done = 0;
            }
        }
    }

    if (withdraw_requested)
        return;

    // --- Check background for exits ---
    for (auto& uptr : fxlist)
    {
        if (withdraw_requested)
            break;

        walker* ob = uptr.get();
        if (ob && !ob->dead())
        {
            if (ob->query_order() == Order::Treasure &&
                ob->family() == FAMILY_EXIT && level_done != 0)
            {
                level_done = 1;
            }
        }
    }

    if (withdraw_requested)
        return;

    // --- Level completion check ---
    // A TYPE_CTF map that failed activation (init attempted, <2 flag teams)
    // falls through to the classic completion rules.
    if ((type & TYPE_CTF) && !(ctf.init_attempted && !ctf.active))
    {
        og::sim::ctf_run_tick(*this);
        if (game_ended)
            return;
    }
    else if (level_done == 2)
    {
        game_ended = true;
        ending = 0;
        next_level = static_cast<short>(id + 1);
        return;
    }

    if (end)
    {
        game_ended = true;
        return;
    }

    // --- Cleanup stale pointers ---
    auto clear_stale_cross_refs = [](walker* ob) {
        if (ob == nullptr)
            return;

        if (ob->foe() && ob->foe()->dead())
            ob->set_foe(nullptr);
        if (ob->leader() && ob->leader()->dead())
            ob->set_leader(nullptr);
        if (ob->owner() && ob->owner()->dead())
            ob->set_owner(nullptr);
        if (ob->collide_ob() && ob->collide_ob()->dead())
            ob->set_collide_ob(nullptr);
        if (statistics* stats = ob->stats();
            stats != nullptr && stats->controller() && stats->controller()->dead())
            stats->set_controller(nullptr);
    };

    for (auto& uptr : oblist)
        clear_stale_cross_refs(uptr.get());

    for (auto& uptr : fxlist)
        clear_stale_cross_refs(uptr.get());

    for (auto& uptr : weaplist)
        clear_stale_cross_refs(uptr.get());

    // --- Remove dead entities ---
    // Note: viewscreen control pointer cleanup is handled by the display
    // runtime since viewscreens are a rendering concern.
    auto& objects = oblist.raw_mutable();
    auto& dead = dead_list.raw_mutable();
    for (auto e = objects.begin(); e != objects.end();)
    {
        walker* ob = e->get();
        if (ob && ob->dead() && ob->myguy == nullptr)
        {
            remove_from_id_index(ob);
            dead.push_back(std::move(*e));

            if (ob->query_order() == Order::Living)
                living_count--;

            e = objects.erase(e);
            continue;
        }
        e++;
    }

    std::erase_if(fxlist.raw_mutable(), [this](const auto& uptr) {
        walker* ob = uptr.get();
        if (!(ob && ob->dead()))
            return false;
        remove_from_id_index(ob);
        return true;
    });

    std::erase_if(weaplist.raw_mutable(), [this](const auto& uptr) {
        walker* ob = uptr.get();
        if (!(ob && ob->dead()))
            return false;
        remove_from_id_index(ob);
        return true;
    });
}
