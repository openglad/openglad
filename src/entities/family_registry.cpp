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
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

// Forward declarations of family descriptor providers
const FamilyDescriptor& describe_family_golem();
const FamilyDescriptor& describe_family_giant_skeleton();
const FamilyDescriptor& describe_family_tower1();
const FamilyDescriptor& describe_family_big_orc();

static bool s_registry_initialized = false;
static FamilyDescriptor s_registry[NUM_FAMILIES];

#define BASE_GUY_HP 30

void init_family_registry()
{
    if (s_registry_initialized)
        return;

    // Default all fields to safe values
    for (int i = 0; i < NUM_FAMILIES; i++)
    {
        auto& d = s_registry[i];
        d.family_id = i;
        d.name = "BEAST";
        for (int j = 0; j < 6; j++) d.base_stats[j] = 0;
        d.hiring_cost = 0;
        for (int j = 0; j < 8; j++) d.derived_bonuses[j] = 0;
        for (int j = 0; j < 6; j++) d.stat_costs[j] = 0;
        for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_cost[j] = 5000;
        d.weapon_cost = 1;  // default from gloader create_walker_owned
        d.default_weapon = FAMILY_KNIFE;
        d.init_bit_flags = 0;
        d.init_ani_type = 0;
        d.init_max_magicpoints = 0;
        for (int j = 0; j < FD_NUM_SPECIALS; j++) d.special_names[j] = "NONE";
        for (int j = 0; j < FD_NUM_SPECIALS; j++) d.alternate_names[j] = "NONE";
        d.leaves_bloodspot = true;
        d.do_special = nullptr;
        d.check_special_ai = nullptr;
        d.hit_response = nullptr;
        d.set_difficulty = nullptr;
        d.level_up = nullptr;
        d.on_death = nullptr;
    }

    // === FAMILY_SOLDIER (0) ===
    {
        auto& d = s_registry[FAMILY_SOLDIER];
        d.name = "SOLDIER";
        d.base_stats[0] = 12; d.base_stats[1] = 6; d.base_stats[2] = 12;
        d.base_stats[3] = 8; d.base_stats[4] = 9; d.base_stats[5] = 1;
        d.hiring_cost = 250;
        d.derived_bonuses[0] = BASE_GUY_HP+90; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 20; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 4; d.derived_bonuses[7] = 6;
        d.stat_costs[0] = 6; d.stat_costs[1] = 10; d.stat_costs[2] = 6;
        d.stat_costs[3] = 25; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 25;  // charge
        d.special_cost[2] = 100; // boomerang
        d.special_cost[3] = 120; // whirlwind
        d.special_cost[4] = 150; // disarm
        d.weapon_cost = 2;
        d.default_weapon = FAMILY_KNIFE;
        d.special_names[1] = "CHARGE";
        d.special_names[2] = "BOOMERANG";
        d.special_names[3] = "WHIRLWIND";
        d.special_names[4] = "DISARM";
    }

    // === FAMILY_ELF (1) ===
    {
        auto& d = s_registry[FAMILY_ELF];
        d.name = "ELF";
        d.base_stats[0] = 5; d.base_stats[1] = 14; d.base_stats[2] = 5;
        d.base_stats[3] = 12; d.base_stats[4] = 8; d.base_stats[5] = 1;
        d.hiring_cost = 150;
        d.derived_bonuses[0] = BASE_GUY_HP+45; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 12; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 4; d.derived_bonuses[7] = 5;
        d.stat_costs[0] = 25; d.stat_costs[1] = 6; d.stat_costs[2] = 12;
        d.stat_costs[3] = 8; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 10;
        d.special_cost[2] = 20;
        d.special_cost[3] = 30;
        d.special_cost[4] = 40;
        d.default_weapon = FAMILY_ROCK;
        d.init_bit_flags = BIT_FORESTWALK;
        d.special_names[1] = "ROCKS";
        d.special_names[2] = "BOUNCING ROCKS";
        d.special_names[3] = "LOTS OF ROCKS";
        d.special_names[4] = "MEGA ROCKS";
    }

    // === FAMILY_ARCHER (2) ===
    {
        auto& d = s_registry[FAMILY_ARCHER];
        d.name = "ARCHER";
        d.base_stats[0] = 6; d.base_stats[1] = 12; d.base_stats[2] = 6;
        d.base_stats[3] = 10; d.base_stats[4] = 5; d.base_stats[5] = 1;
        d.hiring_cost = 350;
        d.derived_bonuses[0] = BASE_GUY_HP+60; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 8; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 4; d.derived_bonuses[7] = 5;
        d.stat_costs[0] = 15; d.stat_costs[1] = 6; d.stat_costs[2] = 9;
        d.stat_costs[3] = 10; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 20;  // fire arrows
        d.special_cost[2] = 60;  // barrage
        d.special_cost[3] = 70;  // exploding bolt
        d.default_weapon = FAMILY_ARROW;
        d.special_names[1] = "FIRE ARROWS";
        d.special_names[2] = "BARRAGE";
        d.special_names[3] = "EXPLODING BOLT";
    }

    // === FAMILY_MAGE (3) ===
    {
        auto& d = s_registry[FAMILY_MAGE];
        d.name = "MAGE";
        d.base_stats[0] = 4; d.base_stats[1] = 6; d.base_stats[2] = 4;
        d.base_stats[3] = 16; d.base_stats[4] = 5; d.base_stats[5] = 1;
        d.hiring_cost = 450;
        d.derived_bonuses[0] = BASE_GUY_HP+60; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 4; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 2; d.derived_bonuses[7] = 4;
        d.stat_costs[0] = 20; d.stat_costs[1] = 15; d.stat_costs[2] = 16;
        d.stat_costs[3] = 6; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 15;  // teleport
        d.special_cost[2] = 60;  // warp space
        d.special_cost[3] = 500; // freeze time
        d.special_cost[4] = 70;  // energy wave
        d.special_cost[5] = 100; // heartburst
        d.weapon_cost = 5;
        d.default_weapon = FAMILY_FIREBALL;
        d.special_names[1] = "TELEPORT";
        d.special_names[2] = "WARP SPACE";
        d.special_names[3] = "FREEZE TIME";
        d.special_names[4] = "ENERGY WAVE";
        d.special_names[5] = "HEARTBURST";
        d.alternate_names[1] = "TELEPORT MARKER";
    }

    // === FAMILY_SKELETON (4) ===
    {
        auto& d = s_registry[FAMILY_SKELETON];
        d.name = "SKELETON";
        d.base_stats[0] = 9; d.base_stats[1] = 14; d.base_stats[2] = 9;
        d.base_stats[3] = 6; d.base_stats[4] = 6; d.base_stats[5] = 1;
        d.hiring_cost = 300;
        d.derived_bonuses[0] = BASE_GUY_HP+30; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 4; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 6; d.derived_bonuses[7] = 4.5f;
        d.stat_costs[0] = 15; d.stat_costs[1] = 6; d.stat_costs[2] = 16;
        d.stat_costs[3] = 25; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 10;  // tunnel
        d.default_weapon = FAMILY_BONE;
        d.weapon_cost = 0;
        d.init_ani_type = ANI_SKEL_GROW;
        d.leaves_bloodspot = false;
        d.special_names[1] = "TUNNEL";
    }

    // === FAMILY_CLERIC (5) ===
    {
        auto& d = s_registry[FAMILY_CLERIC];
        d.name = "CLERIC";
        d.base_stats[0] = 6; d.base_stats[1] = 7; d.base_stats[2] = 6;
        d.base_stats[3] = 14; d.base_stats[4] = 7; d.base_stats[5] = 1;
        d.hiring_cost = 400;
        d.derived_bonuses[0] = BASE_GUY_HP+90; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 12; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 2; d.derived_bonuses[7] = 7.5f;
        d.stat_costs[0] = 15; d.stat_costs[1] = 15; d.stat_costs[2] = 9;
        d.stat_costs[3] = 6; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 2;   // heal / mystic mace
        d.special_cost[2] = 20;  // raise skeleton
        d.special_cost[3] = 50;  // raise ghost
        d.special_cost[4] = 150; // resurrect
        d.weapon_cost = 8;
        d.default_weapon = FAMILY_GLOW;
        d.special_names[1] = "HEAL";
        d.special_names[2] = "RAISE UNDEAD";
        d.special_names[3] = "RAISE GHOST";
        d.special_names[4] = "RESURRECT";
        d.alternate_names[1] = "MYSTIC MACE";
        d.alternate_names[2] = "TURN UNDEAD";
        d.alternate_names[3] = "TURN UNDEAD";
    }

    // === FAMILY_FIREELEMENTAL (6) ===
    {
        auto& d = s_registry[FAMILY_FIREELEMENTAL];
        d.name = "ELEMENTAL";
        d.base_stats[0] = 14; d.base_stats[1] = 10; d.base_stats[2] = 14;
        d.base_stats[3] = 14; d.base_stats[4] = 9; d.base_stats[5] = 1;
        d.hiring_cost = 600;
        d.derived_bonuses[0] = BASE_GUY_HP+70; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 28; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 4; d.derived_bonuses[7] = 5;
        d.stat_costs[0] = 7; d.stat_costs[1] = 10; d.stat_costs[2] = 14;
        d.stat_costs[3] = 12; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 50;  // starburst
        d.default_weapon = FAMILY_METEOR;
        d.init_bit_flags = BIT_ANIMATE;
        d.init_max_magicpoints = 150;
        d.special_names[1] = "STARBURST";
    }

    // === FAMILY_FAERIE (7) ===
    {
        auto& d = s_registry[FAMILY_FAERIE];
        d.name = "FAERIE";
        d.base_stats[0] = 3; d.base_stats[1] = 8; d.base_stats[2] = 3;
        d.base_stats[3] = 14; d.base_stats[4] = 2; d.base_stats[5] = 1;
        d.hiring_cost = 450;
        d.derived_bonuses[0] = BASE_GUY_HP+45; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 5; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 4; d.derived_bonuses[7] = 9;
        d.stat_costs[0] = 25; d.stat_costs[1] = 6; d.stat_costs[2] = 12;
        d.stat_costs[3] = 8; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.weapon_cost = 2;
        d.default_weapon = FAMILY_SPRINKLE;
        d.init_bit_flags = BIT_ANIMATE | BIT_FLYING;
    }

    // === FAMILY_SLIME (8) ===
    {
        auto& d = s_registry[FAMILY_SLIME];
        d.name = "SLIME";
        d.base_stats[0] = 18; d.base_stats[1] = 2; d.base_stats[2] = 18;
        d.base_stats[3] = 7; d.base_stats[4] = 6; d.base_stats[5] = 1;
        d.hiring_cost = 700;
        d.derived_bonuses[0] = BASE_GUY_HP+120; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 28; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 3; d.derived_bonuses[7] = 11;
        d.stat_costs[0] = 20; d.stat_costs[1] = 20; d.stat_costs[2] = 8;
        d.stat_costs[3] = 14; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 30;  // split
        d.default_weapon = FAMILY_BLOB;
        d.weapon_cost = 0;
        d.init_bit_flags = BIT_ANIMATE;
        d.init_max_magicpoints = 50;
        d.special_names[1] = "SPLIT";
    }

    // === FAMILY_SMALL_SLIME (9) ===
    {
        auto& d = s_registry[FAMILY_SMALL_SLIME];
        d.name = "SLIME";
        d.base_stats[0] = 18; d.base_stats[1] = 2; d.base_stats[2] = 18;
        d.base_stats[3] = 7; d.base_stats[4] = 6; d.base_stats[5] = 1;
        d.hiring_cost = 700;
        d.derived_bonuses[0] = BASE_GUY_HP+50; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 12; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 2; d.derived_bonuses[7] = 12;
        d.stat_costs[0] = 20; d.stat_costs[1] = 20; d.stat_costs[2] = 8;
        d.stat_costs[3] = 14; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 30;  // grow
        d.default_weapon = FAMILY_BLOB;
        d.weapon_cost = 0;
        d.init_bit_flags = BIT_ANIMATE | BIT_NO_RANGED;
        d.init_max_magicpoints = 50;
        d.special_names[1] = "GROW";
    }

    // === FAMILY_MEDIUM_SLIME (10) ===
    {
        auto& d = s_registry[FAMILY_MEDIUM_SLIME];
        d.name = "SLIME";
        d.base_stats[0] = 18; d.base_stats[1] = 2; d.base_stats[2] = 18;
        d.base_stats[3] = 7; d.base_stats[4] = 6; d.base_stats[5] = 1;
        d.hiring_cost = 700;
        d.derived_bonuses[0] = BASE_GUY_HP+80; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 20; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 2; d.derived_bonuses[7] = 10;
        d.stat_costs[0] = 20; d.stat_costs[1] = 20; d.stat_costs[2] = 8;
        d.stat_costs[3] = 14; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 30;  // grow
        d.default_weapon = FAMILY_BLOB;
        d.weapon_cost = 0;
        d.init_bit_flags = BIT_ANIMATE;
        d.init_max_magicpoints = 50;
        d.special_names[1] = "GROW";
    }

    // === FAMILY_THIEF (11) ===
    {
        auto& d = s_registry[FAMILY_THIEF];
        d.name = "THIEF";
        d.base_stats[0] = 9; d.base_stats[1] = 12; d.base_stats[2] = 12;
        d.base_stats[3] = 10; d.base_stats[4] = 5; d.base_stats[5] = 1;
        d.hiring_cost = 400;
        d.derived_bonuses[0] = BASE_GUY_HP+45; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 12; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 5; d.derived_bonuses[7] = 5;
        d.stat_costs[0] = 15; d.stat_costs[1] = 6; d.stat_costs[2] = 9;
        d.stat_costs[3] = 10; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 35;  // bomb
        d.special_cost[2] = 125; // cloak
        d.special_cost[3] = 100; // taunt
        d.special_cost[4] = 150; // poison cloud
        d.default_weapon = FAMILY_KNIFE;
        d.special_names[1] = "DROP BOMB";
        d.special_names[2] = "CLOAK";
        d.special_names[3] = "TAUNT ENEMY";
        d.special_names[4] = "POISON CLOUD";
        d.alternate_names[3] = "CHARM OPPONENT";
    }

    // === FAMILY_GHOST (12) ===
    {
        auto& d = s_registry[FAMILY_GHOST];
        d.name = "GHOST";
        d.base_stats[0] = 6; d.base_stats[1] = 12; d.base_stats[2] = 18;
        d.base_stats[3] = 10; d.base_stats[4] = 15; d.base_stats[5] = 1;
        d.hiring_cost = 600;
        d.derived_bonuses[0] = BASE_GUY_HP+20; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 12; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 4; d.derived_bonuses[7] = 7;
        d.stat_costs[0] = 16; d.stat_costs[1] = 16; d.stat_costs[2] = 16;
        d.stat_costs[3] = 16; d.stat_costs[4] = 45; d.stat_costs[5] = 200;
        d.special_cost[1] = 30;  // scare
        d.default_weapon = FAMILY_KNIFE;
        d.weapon_cost = 0;
        d.init_bit_flags = BIT_ANIMATE | BIT_FLYING | BIT_ETHEREAL | BIT_NO_RANGED;
        d.leaves_bloodspot = false;
        d.special_names[1] = "SCARE";
    }

    // === FAMILY_DRUID (13) ===
    {
        auto& d = s_registry[FAMILY_DRUID];
        d.name = "DRUID";
        d.base_stats[0] = 7; d.base_stats[1] = 8; d.base_stats[2] = 14;
        d.base_stats[3] = 12; d.base_stats[4] = 7; d.base_stats[5] = 1;
        d.hiring_cost = 350;
        d.derived_bonuses[0] = BASE_GUY_HP+80; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 10; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 3; d.derived_bonuses[7] = 9;
        d.stat_costs[0] = 15; d.stat_costs[1] = 15; d.stat_costs[2] = 7;
        d.stat_costs[3] = 6; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 15;  // grow tree
        d.special_cost[2] = 80;  // summon faerie
        d.special_cost[3] = 150; // reveal
        d.special_cost[4] = 200; // protection
        d.weapon_cost = 4;
        d.default_weapon = FAMILY_LIGHTNING;
        d.special_names[1] = "GROW TREE";
        d.special_names[2] = "SUMMON FAERIE";
        d.special_names[3] = "REVEAL";
        d.special_names[4] = "PROTECTION";
    }

    // === FAMILY_ORC (14) ===
    {
        auto& d = s_registry[FAMILY_ORC];
        d.name = "ORC";
        d.base_stats[0] = 18; d.base_stats[1] = 8; d.base_stats[2] = 16;
        d.base_stats[3] = 5; d.base_stats[4] = 11; d.base_stats[5] = 1;
        d.hiring_cost = 300;
        d.derived_bonuses[0] = BASE_GUY_HP+110; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 23; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 3; d.derived_bonuses[7] = 7;
        d.stat_costs[0] = 6; d.stat_costs[1] = 15; d.stat_costs[2] = 5;
        d.stat_costs[3] = 40; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 25;  // howl
        d.special_cost[2] = 20;  // eat corpse
        d.weapon_cost = 2;
        d.default_weapon = FAMILY_ROCK;
        d.init_bit_flags = BIT_NO_RANGED;
        d.special_names[1] = "HOWL";
        d.special_names[2] = "EAT CORPSE";
    }

    // === FAMILY_BIG_ORC (15) — defined in families/family_big_orc.cpp ===
    s_registry[FAMILY_BIG_ORC] = describe_family_big_orc();

    // === FAMILY_BARBARIAN (16) ===
    {
        auto& d = s_registry[FAMILY_BARBARIAN];
        d.name = "BARBARIAN";
        d.base_stats[0] = 14; d.base_stats[1] = 5; d.base_stats[2] = 14;
        d.base_stats[3] = 8; d.base_stats[4] = 8; d.base_stats[5] = 1;
        d.hiring_cost = 350;
        d.derived_bonuses[0] = BASE_GUY_HP+120; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 25; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 3; d.derived_bonuses[7] = 5.5f;
        d.stat_costs[0] = 5; d.stat_costs[1] = 35; d.stat_costs[2] = 5;
        d.stat_costs[3] = 35; d.stat_costs[4] = 50; d.stat_costs[5] = 200;
        d.special_cost[1] = 20;  // hurl boulder
        d.special_cost[2] = 30;  // exploding boulder
        d.weapon_cost = 2;
        d.default_weapon = FAMILY_HAMMER;
        d.special_names[1] = "HURL BOULDER";
        d.special_names[2] = "EXPLODING BOULDER";
    }

    // === FAMILY_ARCHMAGE (17) ===
    {
        auto& d = s_registry[FAMILY_ARCHMAGE];
        d.name = "ARCHMAGE";
        d.base_stats[0] = 4; d.base_stats[1] = 6; d.base_stats[2] = 4;
        d.base_stats[3] = 16; d.base_stats[4] = 5; d.base_stats[5] = 1;
        d.hiring_cost = 450;
        d.derived_bonuses[0] = BASE_GUY_HP+120; d.derived_bonuses[1] = 0;
        d.derived_bonuses[2] = 8; d.derived_bonuses[3] = 0;
        d.derived_bonuses[4] = 0; d.derived_bonuses[5] = 0;
        d.derived_bonuses[6] = 3; d.derived_bonuses[7] = 1;
        d.stat_costs[0] = 30; d.stat_costs[1] = 20; d.stat_costs[2] = 25;
        d.stat_costs[3] = 7; d.stat_costs[4] = 55; d.stat_costs[5] = 200;
        d.special_cost[1] = 10;  // teleport
        d.special_cost[2] = 80;  // heartburst
        d.special_cost[3] = 500; // summon elemental
        d.special_cost[4] = 150; // mind control
        d.weapon_cost = 12;
        d.default_weapon = FAMILY_FIREBALL;
        d.special_names[1] = "TELEPORT";
        d.special_names[2] = "HEARTBURST";
        d.special_names[3] = "SUMMON IMAGE";
        d.special_names[4] = "MIND CONTROL";
        d.alternate_names[1] = "TELEPORT MARKER";
        d.alternate_names[2] = "CHAIN LIGHTNING";
        d.alternate_names[3] = "SUMMON ELEMENTAL";
    }

    // === FAMILY_GOLEM (18) — defined in families/family_golem.cpp ===
    s_registry[FAMILY_GOLEM] = describe_family_golem();

    // === FAMILY_GIANT_SKELETON (19) — defined in families/family_giant_skeleton.cpp ===
    s_registry[FAMILY_GIANT_SKELETON] = describe_family_giant_skeleton();

    // === FAMILY_TOWER1 (20) — defined in families/family_tower1.cpp ===
    s_registry[FAMILY_TOWER1] = describe_family_tower1();

    s_registry_initialized = true;
}

const FamilyDescriptor* get_family_descriptor(int family_id)
{
    if (family_id < 0 || family_id >= NUM_FAMILIES)
        return nullptr;

    if (!s_registry_initialized)
        init_family_registry();

    return &s_registry[family_id];
}
