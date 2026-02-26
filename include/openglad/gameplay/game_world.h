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

// Forward-declare Order enum class (defined in base.h)
enum class Order : unsigned char;

class LevelData;
class walker;

// Phase 1a shell for gameplay-owned entity lists.
// LevelData temporarily forwards to this object while loader/render data
// remains on LevelData.
class GameWorld
{
public:
    int living_count = 0;
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> fxlist;
    std::list<std::unique_ptr<walker>> weaplist;
    std::list<std::unique_ptr<walker>> dead_list;

    void set_level_data(LevelData* level_data);
    LevelData* level_data() { return level_data_; }
    const LevelData* level_data() const { return level_data_; }

    walker* add_ob(Order order, std::int32_t family, bool atstart = false);
    walker* add_fx_ob(Order order, std::int32_t family);
    walker* add_weap_ob(Order order, std::int32_t family);
    short remove_ob(walker* ob);

    void delete_objects();
    short remaining_foes(walker* myguy) const;

private:
    walker* add_to_list(Order order, std::int32_t family,
                        std::list<std::unique_ptr<walker>>& target_list,
                        bool count_living, bool atstart);

    LevelData* level_data_ = nullptr;
};
