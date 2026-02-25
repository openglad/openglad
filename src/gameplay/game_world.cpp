/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/data/level_data.h>
#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/entities/obmap.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/sim/sim_event_log.h>
#include <algorithm>
#include <cstdlib>
#include <format>

namespace {
constexpr short MAX_SPREAD = 10;

static walker* find_far_foe_for_tick(og::gameplay::GameWorld& world, walker* ob, og::sim::SimRandom& rng)
{
    if (!ob)
        return nullptr;

    walker* endfoe = nullptr;
    std::int32_t distance = 10000;
    ob->stats()->last_distance = 10000;

    for (auto& uptr : world.oblist)
    {
        walker* foe = uptr.get();
        if (foe == nullptr || foe->dead)
            continue;

        if (ob->is_friendly(foe) == 0)
        {
            if ((foe->query_order() == Order::Living ||
                 foe->query_order() == Order::Generator) &&
                (!(rng.next(foe->invisibility_left / 20))))
            {
                std::int32_t tempdistance = ob->distance_to_ob(foe);
                if (tempdistance < distance)
                {
                    distance = tempdistance;
                    endfoe = foe;
                }
            }
        }
    }
    return endfoe;
}
}

namespace og::sim {
#ifdef TESTING
std::int32_t g_test_level_tick_limit_override = 0;
#endif
} // namespace og::sim

namespace og::gameplay {

GameWorld::GameWorld()
    : myobmap(std::make_unique<obmap>())
{
}

GameWorld::~GameWorld() = default;

void GameWorld::delete_objects()
{
    oblist.clear();
    fxlist.clear();
    weaplist.clear();
    dead_list.clear();
    living_count = 0;
}

short GameWorld::remaining_foes(walker* myguy) const
{
    short myfoes = 0;
    for (const auto& uptr : oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            (w->query_order() == Order::Living) &&
            !myguy->is_friendly(w))
            myfoes++;
    }
    return myfoes;
}

void GameWorld::tick()
{
    if (!og::gameplay::current_game || og::gameplay::current_game->sim_events == nullptr)
        return;
    og::sim::SimEventLog& events = *og::gameplay::current_game->sim_events;

    game_ended = false;
    next_level = -1;
    ending = 0;
    tick_count_++;
    events.current_tick_ = tick_count_;
    if (last_level_id_ != id)
    {
        last_level_id_ = id;
        level_tick_count_ = 0;
    }
    level_tick_count_++;

    std::uint32_t max_level_ticks = 36000;
#ifdef TESTING
    if (og::sim::g_test_level_tick_limit_override > 0)
    {
        max_level_ticks = static_cast<std::uint32_t>(og::sim::g_test_level_tick_limit_override);
    }
#endif
    if (level_tick_count_ > max_level_ticks)
    {
        game_ended = true;
        ending = 1;
        next_level = -1;
        events.push_notification("Mission timed out. Retreating.", 40);
        return;
    }

    level_done = 2;

    if (enemy_freeze)
        enemy_freeze--;
    if (enemy_freeze == 1)
        events.push(og::sim::EventKind::SetPalette, 0, 0);

    bool printed_time = false;
    for (auto& uptr : oblist)
    {
        walker* ob = uptr.get();
        if (!enemy_freeze)
        {
            if (ob && !ob->dead)
            {
                ob->in_act = true;
                ob->act();
                ob->in_act = false;
                if (ob && !ob->dead)
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                        ob->query_order() == Order::Living)
                    {
                        level_done = 0;
                    }
                    if (ob->foe == nullptr && ob->leader == nullptr)
                        ob->foe = find_far_foe_for_tick(*this, ob, rng_);
                }
            }
        }
        else
        {
            if (!(enemy_freeze % 10) && !printed_time)
            {
                std::string obmessage = std::format("TIME LEFT: {}", enemy_freeze);
                events.push_notification(obmessage, 10);
                printed_time = true;
            }
            if (ob && !ob->dead &&
                (((ob->query_order() != Order::Living) &&
                  (ob->query_order() != Order::Generator)) ||
                 ob->is_friendly_to_team(static_cast<unsigned char>(my_team))))
            {
                ob->act();
                if (ob && !ob->dead)
                {
                    if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                        ob->query_order() == Order::Living)
                    {
                        level_done = 0;
                    }
                }
            }
        }
    }

    for (auto& uptr : weaplist)
    {
        walker* ob = uptr.get();
        if (ob && !ob->dead)
        {
            ob->act();
            if (ob && !ob->dead)
            {
                if (!ob->is_friendly_to_team(static_cast<unsigned char>(my_team)) &&
                    ob->query_order() == Order::Living)
                {
                    level_done = 0;
                }
            }
        }
    }

    for (auto& uptr : fxlist)
    {
        walker* ob = uptr.get();
        if (ob && !ob->dead)
        {
            if (ob->query_order() == Order::Treasure &&
                ob->family == FAMILY_EXIT &&
                level_done != 0)
            {
                level_done = 1;
            }
        }
    }

    if (level_done == 2)
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

    for (auto& uptr : oblist)
    {
        walker* ob = uptr.get();
        if (ob->foe && ob->foe->dead)
            ob->foe = nullptr;
        if (ob->leader && ob->leader->dead)
            ob->leader = nullptr;
        if (ob->owner && ob->owner->dead)
            ob->owner = nullptr;
        if (ob->collide_ob && ob->collide_ob->dead)
            ob->collide_ob = nullptr;
    }

    for (auto& uptr : weaplist)
    {
        walker* ob = uptr.get();
        if (ob->foe && ob->foe->dead)
            ob->foe = nullptr;
        if (ob->leader && ob->leader->dead)
            ob->leader = nullptr;
        if (ob->owner && ob->owner->dead)
            ob->owner = nullptr;
        if (ob->collide_ob && ob->collide_ob->dead)
            ob->collide_ob = nullptr;
    }

    for (auto e = oblist.begin(); e != oblist.end();)
    {
        walker* ob = e->get();
        if (ob && ob->dead && ob->myguy == nullptr)
        {
            dead_list.push_back(std::move(*e));

            if (ob->query_order() == Order::Living)
                living_count--;

            e = oblist.erase(e);
            continue;
        }
        e++;
    }

    std::erase_if(fxlist, [](const auto& uptr) {
        walker* ob = uptr.get();
        return ob && ob->dead;
    });

    std::erase_if(weaplist, [](const auto& uptr) {
        walker* ob = uptr.get();
        return ob && ob->dead;
    });

    return;
}

walker* GameWorld::add_ob(Order order, std::int32_t family, bool atstart)
{
    if (!level_data_) return nullptr;
    return level_data_->add_ob(order, family, atstart);
}

walker* GameWorld::add_fx_ob(Order order, std::int32_t family)
{
    if (!level_data_) return nullptr;
    return level_data_->add_fx_ob(order, family);
}

walker* GameWorld::add_weap_ob(Order order, std::int32_t family)
{
    if (!level_data_) return nullptr;
    return level_data_->add_weap_ob(order, family);
}

bool GameWorld::query_grid_passable(float x, float y, walker* ob)
{
    std::int32_t i, j;
    std::int32_t xtrax = 1;
    std::int32_t xtray = 1;
    std::int32_t xtarg;
    std::int32_t ytarg;
    std::int32_t dist;
    const std::int32_t x_i = static_cast<std::int32_t>(x);
    const std::int32_t y_i = static_cast<std::int32_t>(y);
    std::int32_t xover = x_i + ob->sizex;
    std::int32_t yover = y_i + ob->sizey;

    if (x_i < 0 || y_i < 0 || xover >= pixmaxx || yover >= pixmaxy)
        return 0;

    if (ob->stats()->query_bit_flags(BIT_ETHEREAL))
        return 1;

    if (!grid.valid())
        return 0;

    if (!((xover) % GRID_SIZE))
        xtrax = 0;
    if (!((yover) % GRID_SIZE))
        xtray = 0;

    xtarg = (xover / GRID_SIZE) + xtrax;
    ytarg = (yover / GRID_SIZE) + xtray;

    for (i = x_i / GRID_SIZE; i < xtarg; i++)
    {
        for (j = y_i / GRID_SIZE; j < ytarg; j++)
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
                    else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
                        break;
                    else
                        return 0;
                case PIX_TREE_B1:
                    if (ob->query_order() == Order::Weapon ||
                        ob->stats()->query_bit_flags(BIT_FORESTWALK))
                        break;
                    else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
                        break;
                    else
                        return 0;

                case PIX_H_WALL1:
                case PIX_WALL2:
                case PIX_WALL3:
                case PIX_WALL_LL:
                case PIX_WALLTOP_H:
                    return 0;

                case PIX_WALL4:
                case PIX_WALL5:
                case PIX_WALL_ARROW_GRASS:
                case PIX_WALL_ARROW_FLOOR:
                case PIX_WALL_ARROW_GRASS_DARK:
                    if (ob->query_order() == Order::Living)
                        return 0;

                    if (abs(ob->xpos - ob->owner->xpos) > abs(ob->ypos - ob->owner->ypos))
                        dist = abs(ob->xpos - ob->owner->xpos);
                    else
                        dist = abs(ob->ypos - ob->owner->ypos);

                    dist -= (GRID_SIZE / 2);
                    if (dist < GRID_SIZE)
                    {
                        dist += GRID_SIZE;
                    }

                    if (rng_.next(dist / GRID_SIZE))
                    {
                        return 0;
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
                    else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
                        break;
                    else
                        return 0;
                default:
                    return 0;
            }
        }
    }
    return 1;
}

bool GameWorld::query_object_passable(float x, float y, walker* ob)
{
    if (ob->dead)
        return 1;
    return myobmap->query_list(ob, static_cast<short>(x), static_cast<short>(y));
}

bool GameWorld::query_passable(float x, float y, walker* ob)
{
    return query_grid_passable(x, y, ob) && query_object_passable(x, y, ob);
}

walker* GameWorld::find_near_foe(walker* ob)
{
    short targx;
    short targy;
    short spread = 1;
    short xchange = 0;
    short loop = 0;
    short resolution = myobmap->obmapres;

    if (!ob)
    {
        Log("no ob in find near foe.\n");
        return nullptr;
    }
    targx = ob->xpos;
    targy = ob->ypos;

    while (spread < MAX_SPREAD)
    {
        for (loop = 0; loop < spread; loop++)
        {
            if (!(xchange % 2))
            {
                targx += resolution;
                if (targx <= 0)
                    return find_far_foe(ob);
                if (targx >= pixmaxx)
                    return find_far_foe(ob);
            }
            else
            {
                targy += resolution;
                if (targy <= 0)
                    return find_far_foe(ob);
                if (targy >= pixmaxy)
                    return find_far_foe(ob);
            }

            std::list<walker*>& ls = myobmap->obmap_get_list(targx, targy);
            for (auto* w : ls)
            {
                if (!(w->dead) && (ob->is_friendly(w) == 0) &&
                    (rng_.next(w->invisibility_left / 20) == 0))
                {
                    if (w->query_order() == Order::Living ||
                        w->query_order() == Order::Generator)
                        return w;
                }
            }
        }
        xchange++;
        if (!(xchange % 2))
        {
            resolution = static_cast<short>(-resolution);
            spread++;
        }
    }
    return find_far_foe(ob);
}

walker* GameWorld::find_far_foe(walker* ob)
{
    std::int32_t distance;
    std::int32_t tempdistance;
    walker* endfoe;

    if (!ob)
    {
        Log("no ob in find far foe.\n");
        return nullptr;
    }

    endfoe = nullptr;
    distance = 10000;
    ob->stats()->last_distance = 10000;

    for (auto& uptr : oblist)
    {
        walker* foe = uptr.get();
        if (foe == nullptr || foe->dead)
            continue;

        if (ob->is_friendly(foe) == 0)
        {
            if ((foe->query_order() == Order::Living ||
                 foe->query_order() == Order::Generator) &&
                (!(rng_.next(foe->invisibility_left / 20))))
            {
                tempdistance = ob->distance_to_ob(foe);
                if (tempdistance < distance)
                {
                    distance = tempdistance;
                    endfoe = foe;
                }
            }
        }
    }
    return endfoe;
}

walker* GameWorld::find_nearest_blood(walker* who)
{
    std::int32_t distance;
    std::int32_t newdistance;
    walker* returnob = nullptr;

    if (!who)
        return nullptr;

    distance = 800;

    for (auto& uptr : fxlist)
    {
        walker* w = uptr.get();
        if (w && w->query_order() == Order::Treasure &&
            w->family == FAMILY_STAIN && !w->dead)
        {
            newdistance = static_cast<std::int32_t>(who->distance_to_ob_center(w));
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
    walker* returnob = nullptr;
    std::uint32_t distance = 32000;
    std::uint32_t tempdistance;

    if (!ob)
        return nullptr;

    for (auto& uptr : oblist)
    {
        walker* w = uptr.get();
        if (w && (w->user != -1))
        {
            tempdistance = ob->distance_to_ob(w);
            if (tempdistance < distance)
            {
                distance = tempdistance;
                returnob = w;
            }
        }
    }

    return returnob;
}

std::list<walker*> GameWorld::find_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                            std::int32_t range, std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;

    *howmany = 0;

    if (!ob)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead)
        {
            if (ob->distance_to_ob(w) <= range)
            {
                result.push_back(w);
                (*howmany)++;
            }
        }
    }

    return result;
}

std::list<walker*> GameWorld::find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                                 std::int32_t range, std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;
    *howmany = 0;

    if (!ob)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            (w->query_order() == Order::Living ||
             w->query_order() == Order::Generator) &&
            (ob->is_friendly(w) == 0))
        {
            if (ob->distance_to_ob(w) <= range)
            {
                result.push_back(w);
                (*howmany)++;
            }
        }
    }

    return result;
}

std::list<walker*> GameWorld::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                                        std::int32_t range, std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;
    *howmany = 0;

    if (!ob)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead &&
            (w->query_order() == Order::Weapon) &&
            (ob->is_friendly(w)))
        {
            if (ob->distance_to_ob(w) <= range)
            {
                result.push_back(w);
                (*howmany)++;
            }
        }
    }

    return result;
}

std::list<walker*> GameWorld::find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist,
                                                    std::int32_t range, std::int32_t* howmany, walker* ob)
{
    std::list<walker*> result;
    *howmany = 0;

    if (!ob)
        return result;

    for (auto& uptr : somelist)
    {
        walker* w = uptr.get();
        if (w && !w->dead && w->query_order() == Order::Living &&
            (ob->is_friendly(w)))
        {
            if (ob->distance_to_ob(w) <= range)
            {
                result.push_back(w);
                (*howmany)++;
            }
        }
    }

    return result;
}

void GameWorld::delete_grid()
{
    grid.free();
    pixmaxx = 0;
    pixmaxy = 0;
    // mysmoother stores a raw pointer to grid data; keep it in sync.
    mysmoother.reset();
}

void GameWorld::create_new_grid()
{
    grid.free();

    grid.frames = 1;
    grid.w = 40;
    grid.h = 60;
    pixmaxx = grid.w * GRID_SIZE;
    pixmaxy = grid.h * GRID_SIZE;

    int size = grid.w * grid.h;
    grid.data = std::make_unique<unsigned char[]>(size);
    for (int i = 0; i < size; i++)
    {
        switch (rand() % 4)
        {
            case 0:
                grid.data[i] = PIX_GRASS1;
                break;
            case 1:
                grid.data[i] = PIX_GRASS2;
                break;
            case 2:
                grid.data[i] = PIX_GRASS3;
                break;
            case 3:
                grid.data[i] = PIX_GRASS4;
                break;
        }
    }

    // mysmoother stores a raw pointer to grid data; keep it in sync.
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

    auto random_grass_tile = []() -> unsigned char {
        switch (rand() % 4)
        {
            case 0: return PIX_GRASS1;
            case 1: return PIX_GRASS2;
            case 2: return PIX_GRASS3;
            case 3: return PIX_GRASS4;
        }
        return PIX_GRASS1;
    };

    // Create new grid
    int size = width * height;
    auto new_grid = std::make_unique<unsigned char[]>(size);

    // Copy the map data
    for (int i = 0; i < width; i++)
    {
        for (int j = 0; j < height; j++)
        {
            if (i < grid.w && j < grid.h)
            {
                new_grid[j * width + i] = grid.data[j * grid.w + i];
            }
            else
            {
                new_grid[j * width + i] = random_grass_tile();
            }
        }
    }

    // Delete the old, use the new
    grid.free();
    grid.data = std::move(new_grid);
    grid.frames = 1;
    grid.w = static_cast<unsigned char>(width);
    grid.h = static_cast<unsigned char>(height);
    pixmaxx = grid.w * GRID_SIZE;
    pixmaxy = grid.h * GRID_SIZE;

    // mysmoother stores a raw pointer to grid data; keep it in sync.
    mysmoother.set_target(grid);

    // Delete objects that fell off the map
    int x = 0;
    int y = 0;
    int w = grid.w * GRID_SIZE;
    int h = grid.h * GRID_SIZE;

    auto off_map = [x, y, w, h](const std::unique_ptr<walker>& uptr) {
        walker* ob = uptr.get();
        return ob == nullptr || (x > ob->xpos || ob->xpos >= x + w || y > ob->ypos || ob->ypos >= y + h);
    };

    std::erase_if(oblist, off_map);
    std::erase_if(fxlist, off_map);
    std::erase_if(weaplist, off_map);
}

void GameWorld::clear()
{
    delete_objects();
    delete_grid();
    myobmap = std::make_unique<obmap>();
    title = "New Level";
    type = 0;
    par_value = 1;
    time_bonus_limit = 4000;
    difficulty = 100;
    my_team = 0;
    allied_mode = 0;
    current_scenario = 0;
    completed_levels.clear();
    m_score[0] = m_score[1] = m_score[2] = m_score[3] = 0;
    level_done = 0;
    game_ended = false;
    next_level = -1;
    ending = 0;
    enemy_freeze = 0;
    timer_wait = 6;
    end = 0;
    retry = false;
    control_hp = 0;
}

} // namespace og::gameplay
