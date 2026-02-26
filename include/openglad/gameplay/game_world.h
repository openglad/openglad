/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include <openglad/data/pixie_data.h>
#include <openglad/data/smooth.h>

// Forward-declare Order enum class (defined in base.h)
enum class Order : unsigned char;

class LevelData;
class obmap;
class walker;

// Phase 1a shell for gameplay-owned entity lists.
// LevelData temporarily forwards to this object while loader/render data
// remains on LevelData.
class GameWorld
{
public:
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

    void set_level_data(LevelData* level_data);
    LevelData* level_data() { return level_data_; }
    const LevelData* level_data() const { return level_data_; }

    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);
    short remove_ob(walker* ob);

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

private:
    walker* add_to_list(Order order, std::int32_t family,
                        std::list<std::unique_ptr<walker>>& target_list,
                        bool count_living, bool atstart);

    LevelData* level_data_ = nullptr;
};
