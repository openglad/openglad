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

inline constexpr int FD_NUM_SPECIALS = 6;

class walker;
class living;
class statistics;
class guy;

struct FamilyDescriptor {
    int family_id;
    const char* name;                          // "SOLDIER", "ELF", etc.

    // Base stats from guy.cpp statlist[]
    std::int32_t base_stats[6];                      // STR, DEX, CON, INT, ARMOR, LVL

    // Hiring cost from guy.cpp costlist[]
    std::int32_t hiring_cost;

    // Derived bonuses from guy.cpp derived_bonuses[]
    float derived_bonuses[8];                  // HP, MP, ATK, RATK, RNG, DEF, SPD, ATKSPD

    // Stat upgrade costs from guy.cpp statcosts[]
    std::int32_t stat_costs[6];                      // STR, DEX, CON, INT, ARMOR, LVL

    // Data from gloader.cpp set_walker()
    unsigned short special_cost[FD_NUM_SPECIALS]; // cost of each special (index 0 unused)
    short weapon_cost;                         // cost to fire weapon
    int default_weapon;                        // weapon family ID
    std::int32_t init_bit_flags;                     // BIT_ flags to set on creation
    char init_ani_type;                        // 0=default, ANI_SKEL_GROW for skeleton
    float init_max_magicpoints;                // override max MP (0 = use default)

    // Special ability display names from screen.cpp
    const char* special_names[FD_NUM_SPECIALS];
    const char* alternate_names[FD_NUM_SPECIALS];

    // Flags
    bool leaves_bloodspot;                     // false for ghost/skeleton/tower
    float magic_damage_modifier;               // multiplier for incoming magical damage (1.0 = normal)
    bool is_stationary;                        // true for TOWER1: can face but not move
    bool has_returning_weapon;                  // true for SOLDIER: knife returns on death
    bool is_undead;                            // true for SKELETON, GHOST: affected by turn_undead

    // Class promotion (picker)
    int promotes_to;                           // family to promote to (-1 = no promotion)
    int promotion_level_req;                   // minimum level required for promotion
    short (*promotion_new_level)(int old_level); // calculate new level after promotion (nullptr = level 1)

    const char* death_message;                 // "SOLDIER SLAIN", etc. (fallback: "SOMEONE DIED")

    // Behavioral callbacks (nullptr = use legacy switch / default behavior)
    bool (*do_special)(walker* self);
    bool (*check_special_ai)(living* self);
    void (*hit_response)(statistics* stats, walker* who);
    void (*set_difficulty)(living* self, std::uint32_t level);
    void (*level_up)(guy* self, std::int32_t level_diff);
    bool (*on_death)(walker* self);            // return true = handled

    // Step 3 callbacks: remaining per-family behavioral dispatch
    void (*on_act_living)(living* self);            // periodic effects during act()
    void (*on_shoved)(walker* self);                // called when shoved by an ally
    bool (*on_fire_weapon)(walker* self, walker* weapon); // before firing; false = block
    bool (*handle_teleport)(walker* self);          // teleport-out complete; true = handled
    void (*on_create)(walker* self);                // called when walker created from guy
    void (*customize_weapon)(walker* self, walker* weapon); // tweak weapon after creation
    bool (*on_ani_complete)(walker* self);    // animation end; true = handled
};
