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

#include "SDL.h"

inline constexpr int FD_NUM_SPECIALS = 6;

class walker;
class living;
class statistics;
class guy;

struct FamilyDescriptor {
    int family_id;
    const char* name;                          // "SOLDIER", "ELF", etc.

    // Base stats from guy.cpp statlist[]
    Sint32 base_stats[6];                      // STR, DEX, CON, INT, ARMOR, LVL

    // Hiring cost from guy.cpp costlist[]
    Sint32 hiring_cost;

    // Derived bonuses from guy.cpp derived_bonuses[]
    float derived_bonuses[8];                  // HP, MP, ATK, RATK, RNG, DEF, SPD, ATKSPD

    // Stat upgrade costs from guy.cpp statcosts[]
    Sint32 stat_costs[6];                      // STR, DEX, CON, INT, ARMOR, LVL

    // Data from gloader.cpp set_walker()
    unsigned short special_cost[FD_NUM_SPECIALS]; // cost of each special (index 0 unused)
    short weapon_cost;                         // cost to fire weapon
    int default_weapon;                        // weapon family ID
    Sint32 init_bit_flags;                     // BIT_ flags to set on creation
    char init_ani_type;                        // 0=default, ANI_SKEL_GROW for skeleton
    float init_max_magicpoints;                // override max MP (0 = use default)

    // Special ability display names from screen.cpp
    const char* special_names[FD_NUM_SPECIALS];
    const char* alternate_names[FD_NUM_SPECIALS];

    // Flags
    bool leaves_bloodspot;                     // false for ghost/skeleton/tower

    // Behavioral callbacks (nullptr = use legacy switch)
    bool (*do_special)(walker* self);
    bool (*check_special_ai)(living* self);
    void (*hit_response)(statistics* stats, walker* who);
    void (*set_difficulty)(living* self, Uint32 level);
    void (*level_up)(guy* self, Sint32 level_diff);
    bool (*on_death)(walker* self);            // return true = handled
};
