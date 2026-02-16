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
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/data/gloader.h>
#include <openglad/legacy/base.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <cmath>
#include <cstring>
#define RAISE 1.85  // please also change in picker.cpp

extern Sint32 costlist[NUM_FAMILIES];
extern Sint32 statlist[NUM_FAMILIES][6];
extern Sint32 statcosts[NUM_FAMILIES][6];
// Zardus: PORT, exception doesn't compile (dos thing?): int matherr(struct exception *);


const char* get_family_string(Sint32 family);

static int guy_id_counter = 0;

int MAX(int a,int b)
{
	if (a < b)
		return b;
	else
		return a;
}

guy::guy()
{
	name = "SOLDIER";
	family = FAMILY_SOLDIER;
	strength = 0;
	dexterity = 0;
	constitution = 0;
	intelligence = 0;
	level = 1;
	armor = 0;
	exp = 0;
	kills = 0;
	level_kills = 0;
	total_damage = total_hits = total_shots = 0;
	teamnum = 0;
	scen_damage = 0;
    scen_kills = 0;
    scen_damage_taken = 0;
    scen_min_hp = 5000000;
    scen_shots = 0;
    scen_hits = 0;
	
	id = guy_id_counter++;
}

// Set defaults for various types
guy::guy(int whatfamily)
{

	family = static_cast<char>(whatfamily);
	kills = 0;
	level_kills = 0;
	total_damage = total_hits = total_shots = 0;
	exp = 0;
	teamnum = 0;
	
	scen_damage = 0;
    scen_kills = 0;
    scen_damage_taken = 0;
    scen_min_hp = 5000000;
    scen_shots = 0;
    scen_hits = 0;
	
	// Set stats from family registry
	auto* fd = get_family_descriptor(whatfamily);
	if (fd)
	{
        strength = static_cast<short>(fd->base_stats[0]);
        dexterity = static_cast<short>(fd->base_stats[1]);
        constitution = static_cast<short>(fd->base_stats[2]);
        intelligence = static_cast<short>(fd->base_stats[3]);
        armor = static_cast<short>(fd->base_stats[4]);
        level = static_cast<short>(fd->base_stats[5]);
        name = fd->name;
	}
	else
    {
        strength = 12;
        dexterity = 6;
        constitution = 12;
        intelligence = 8;
        armor = 6;
        level = 1;
        name = "BEAST";
    }
	
	id = guy_id_counter++;
}


guy::guy(const guy& copy)
    : family(copy.family)
    , strength(copy.strength), dexterity(copy.dexterity), constitution(copy.constitution), intelligence(copy.intelligence)
    , armor(copy.armor)
    , exp(copy.exp), kills(copy.kills), level_kills(copy.level_kills)
    , total_damage(copy.total_damage), total_hits(copy.total_hits), total_shots(copy.total_shots)
    , teamnum(copy.teamnum)
    , scen_damage(copy.scen_damage)
    , scen_kills(copy.scen_kills)
    , scen_damage_taken(copy.scen_damage_taken)
    , scen_min_hp(copy.scen_min_hp)
    , scen_shots(copy.scen_shots)
    , scen_hits(copy.scen_hits)
    , id(copy.id)
    , level(copy.level)
{
    name = copy.name;
}

guy::~guy()
{
    
}

Sint32 guy::query_heart_value() // how much are we worth?
{
	guy normal(family); // for base comparisons
	Sint32 cost=0, temp;

	auto* fd = get_family_descriptor(static_cast<int>(family));
	if (!fd)
		return 0;

	// Get strength cost ..
	temp = strength - normal.strength;
	temp = MAX(temp,0);
	cost += static_cast<Sint32>(pow( temp, RAISE) * static_cast<Sint32>(fd->stat_costs[0]));

	// Get dexterity cost ..
	temp = dexterity - normal.dexterity;
	temp = MAX(temp,0);
	cost += static_cast<Sint32>(pow( temp, RAISE) * static_cast<Sint32>(fd->stat_costs[1]));

	// Get constitution cost ..
	temp = constitution - normal.constitution;
	temp = MAX(temp,0);
	cost += static_cast<Sint32>(pow( temp, RAISE) * static_cast<Sint32>(fd->stat_costs[2]));

	// Get intelligence cost ..
	temp = intelligence - normal.intelligence;
	temp = MAX(temp,0);
	cost += static_cast<Sint32>(pow( temp, RAISE) * static_cast<Sint32>(fd->stat_costs[3]));

	// Get armor cost ..
	temp = armor - normal.armor;
	temp = MAX(temp,0);
	cost += static_cast<Sint32>(pow( temp, RAISE) * static_cast<Sint32>(fd->stat_costs[4]));

	// Add in the base cost value for the guy ..
	cost += fd->hiring_cost;

	return cost;

}

Uint32 calculate_exp(Sint32 level);

Sint32 costlist[NUM_FAMILIES] =
    {
        250,  // soldier
        150,  // elf
        350,  // archer
        450,  // mage
        300,  // skeleton
        400,  // cleric
        600, // fire elem
        450,  // faerie
        700, // slime          // can't buy
        700, // small slime
        700, // medium slime   // can't buy
        400,  // thief
        600, // ghost
        350,  // druid
        300,  // orc
        1000, // 'big' orc
        350,  // barbarian
        450,  // archmage, not used
    };

Sint32 statlist[NUM_FAMILIES][6] =
    {
      // STR, DEX, CON, INT, ARMOR, LEVEL
        {12,  6,   12,  8,   9,     1},  // soldier
        {5,   14,  5,   12,  8,     1},  // elf
        {6,   12,  6,   10,  5,     1},  // archer
        {4,   6,   4,   16,  5,     1},  // mage
        {9,   14,  9,   6,   6,     1},  // skeleton
        {6,   7,   6,   14,  7,     1},  // cleric
        {14,  10,  14,  14,  9,     1},  // fire elem
        {3,   8,   3,   14,  2,     1},  // faerie
        {18,  2,   18,  7,   6,     1},  // slime (big)
        {18,  2,   18,  7,   6,     1},  // small slime
        {18,  2,   18,  7,   6,     1},  // slime (medium)
        {9,   12,  12,  10,  5,     1},  // thief
        {6,   12,  18,  10,  15,    1},  // ghost
        {7,   8,   14,  12,  7,     1},  // druid
        {18,  8,   16,  5,   11,    1},  // orc
        {18,  8,   16,  5,   11,    1},  // 'big' orc
        {14,  5,   14,  8,   8,     1},  // barbarian
        {4,   6,   4,   16,  5,     1},  // archmage
    };

#define BASE_GUY_HP 30
float derived_bonuses[NUM_FAMILIES][8] =
    {
      // HP,  MP,  ATK,  RANGED ATK, RANGE, DEF, SPD,   ATK SPD (delay)
        {BASE_GUY_HP+90,  0,   20,   0,          0,     0,   4,     6},  // soldier
        {BASE_GUY_HP+45,  0,   12,   0,          0,     0,   4,     5},  // elf
        {BASE_GUY_HP+60,  0,   8,    0,          0,     0,   4,     5},  // archer
        {BASE_GUY_HP+60,  0,   4,    0,          0,     0,   2,     4},  // mage
        {BASE_GUY_HP+30,  0,   4,    0,          0,     0,   6,     4.5f},  // skeleton
        {BASE_GUY_HP+90,  0,   12,   0,          0,     0,   2,     7.5f},  // cleric
        {BASE_GUY_HP+70,  0,   28,   0,          0,     0,   4,     5},  // fire elem
        {BASE_GUY_HP+45,  0,   5,    0,          0,     0,   4,     9},  // faerie
        {BASE_GUY_HP+120, 0,   28,   0,          0,     0,   3,     11},  // slime (big)
        {BASE_GUY_HP+50,  0,   12,   0,          0,     0,   2,     12},  // small slime
        {BASE_GUY_HP+80,  0,   20,   0,          0,     0,   2,     10},  // slime (medium)
        {BASE_GUY_HP+45,  0,   12,   0,          0,     0,   5,     5},  // thief
        {BASE_GUY_HP+20,  0,   12,   0,          0,     0,   4,     7},  // ghost
        {BASE_GUY_HP+80,  0,   10,   0,          0,     0,   3,     9},  // druid
        {BASE_GUY_HP+110, 0,   23,   0,          0,     0,   3,     7},  // orc
        {BASE_GUY_HP+150, 0,   28,   0,          0,     0,   3,     6},  // 'big' orc
        {BASE_GUY_HP+120, 0,   25,   0,          0,     0,   3,     5.5f},  // barbarian
        {BASE_GUY_HP+120, 0,   8,    0,          0,     0,   3,     1},  // archmage
        {BASE_GUY_HP+270, 0,   60,   0,          0,     0,   8,     9},  // golem
        {BASE_GUY_HP+270, 0,   60,   0,          0,     0,   8,     7},  // giant skeleton
        {BASE_GUY_HP+100, 0,   0,    0,          0,     0,   0,     5},  // tower
    };

Sint32 statcosts[NUM_FAMILIES][6] =
    {
        // STR, DEX, CON, INT, ARMOR, LEVEL
        { 6,10, 6,25,50, 200},  // soldier
        {25, 6,12,8,50, 200},  // elf
        {15, 6, 9,10,50, 200},  // archer
        {20,15,16, 6,50, 200},  // mage
        {15, 6,16,25,50, 200},  // skeleton
        {15,15, 9, 6,50, 200},  // cleric
        {7, 10,14,12,50, 200},  // fire elem
        {25, 6,12,8,50, 200},  // faerie
        {20,20,8,14,50, 200},  // slime
        {20,20,8,14,50, 200},  // small slime
        {20,20,8,14,50, 200},  // medium slime
        {15, 6, 9,10,50, 200},  // thief
        {16,16,16,16,45, 200},  // ghost
        {15,15, 7, 6,50, 200},  // druid
        { 6,15, 5,40,50, 200},  // orc
        { 6,15, 5,40,50, 200},  // 'big' orc
        { 5,35, 5,35,50, 200},  // barbarian
        //  {25,15,20, 5,50, 200},  // archmage
        {30,20,25, 7,55, 200},  // archmage
    };




Sint32 calculate_level(Uint32 experience)
{
	Sint32 result=1;

	while (calculate_exp(result) <= experience)
		result++;
	return (result-1);
}

Uint32 calculate_exp(Sint32 level)
{


	/*
	
	fn = ( (8000*(level+10)) / 10) + calculate_exp(level-1);
	excel: =( (8000*(F4+10)) / 10) + G3
    Level	XP
    1	0
    2	9600
    3	20000
    4	31200
    5	43200
    6	56000
    7	69600
    8	84000
    9	99200
    10	115200
    This is practically linear, so each level costs about 10000 more than the previous.

	*/
	if(level <= 1)
        return 0;
    
    int level_1 = level - 1;
    int level_2 = level - 2;
    if(level_2 < 0)
        level_2 = 0;
    return 8000 + 2000*level_1 + 4000*level_2 + calculate_exp(level-1);
}

void guy::upgrade_to_level(short new_level, bool set_xp)
{
    Sint32 level_diff = static_cast<Sint32>(new_level) - static_cast<Sint32>(this->level);
    
    auto* fd = get_family_descriptor(family);
    if (fd && fd->level_up)
    {
        fd->level_up(this, level_diff);
    }
    else
    {
        // Default: base deltas with no modifier
        Sint32 s = 8 * level_diff;
        Sint32 d = 6 * level_diff;
        Sint32 c = 8 * level_diff;
        Sint32 it = 8 * level_diff;
        Sint32 a = 1 * level_diff;
        strength = static_cast<short>(static_cast<Sint32>(strength) + s);
        dexterity = static_cast<short>(static_cast<Sint32>(dexterity) + d);
        constitution = static_cast<short>(static_cast<Sint32>(constitution) + c);
        intelligence = static_cast<short>(static_cast<Sint32>(intelligence) + it);
        armor = static_cast<short>(static_cast<Sint32>(armor) + a);
    }
    
    this->level = new_level;
    if(set_xp)
        exp = calculate_exp(new_level);
}

// Derived stat calculations
float guy::get_hp_bonus() const
{
    return 10.0f + static_cast<float>(constitution) * 3.0f;
}

float guy::get_mp_bonus() const
{
    return 10.0f + static_cast<float>(intelligence) * 3.0f;
}

float guy::get_damage_bonus() const
{
    return strength/4.0f;
}

float guy::get_armor_bonus() const
{
    return armor;
}

float guy::get_speed_bonus() const
{
    return dexterity/54.0f;
}

float guy::get_fire_frequency_bonus() const
{
    return dexterity/47.0f;
}




void guy::update_derived_stats(walker* w)
{
    guy* temp_guy = w->myguy;
    w->sim_level->myloader->set_derived_stats(w, Order::Living, temp_guy->family);
    
    
    w->stats()->max_hitpoints += temp_guy->get_hp_bonus();
    w->stats()->hitpoints = w->stats()->max_hitpoints;
    
    // No class base value for MP...
    w->stats()->max_magicpoints = temp_guy->get_mp_bonus();
    w->stats()->magicpoints = w->stats()->max_magicpoints;

    w->damage = w->damage + temp_guy->get_damage_bonus();

    // No class base value for armor...
    w->stats()->armor = temp_guy->get_armor_bonus();

    //stepsize makes us run faster, max for a non-weapon is 12
    w->stepsize = w->stepsize + temp_guy->get_speed_bonus();
    if (w->stepsize > 12)
        w->stepsize = 12;
    w->normal_stepsize = w->stepsize;

    //fire_frequency makes us fire faster, min is 1
    w->fire_frequency = w->fire_frequency - temp_guy->get_fire_frequency_bonus();
    if (w->fire_frequency < 1)
        w->fire_frequency = 1;

    // Per-family walker creation hooks (e.g. soldier weapons_left)
    {
        const auto* fd = get_family_descriptor(w->query_family());
        if (fd && fd->on_create)
            fd->on_create(w);
    }

    // Set the heal delay ..
    w->stats()->max_heal_delay = REGEN;
    {
        float heal_delay = static_cast<float>(temp_guy->constitution) + static_cast<float>(temp_guy->strength) / 6.0f + 20.0f + 1000.0f;
        w->stats()->current_heal_delay = static_cast<Sint32>(heal_delay); // for purposes of calculation only
    }

    while (w->stats()->current_heal_delay > REGEN)
    {
        w->stats()->current_heal_delay -= REGEN;
        w->stats()->heal_per_round++;
    } // this takes care of the integer part, now calculate the fraction

    if (w->stats()->current_heal_delay > 1)
    {
        w->stats()->max_heal_delay /=
            static_cast<Sint32>(w->stats()->current_heal_delay + 1);
    }
    w->stats()->current_heal_delay = 0; //start off without healing

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our heal_per_round as one higher, and the math must
    //have been screwed up some how
    if (w->stats()->max_heal_delay < 2)
        w->stats()->max_heal_delay = 2;

    // Set the magic delay ..
    w->stats()->max_magic_delay = REGEN;
    w->stats()->current_magic_delay = temp_guy->intelligence * 45 + temp_guy->dexterity * 15 + 200;

    while (w->stats()->current_magic_delay > REGEN)
    {
        w->stats()->current_magic_delay -= REGEN;
        w->stats()->magic_per_round++;
    } // this takes care of the integer part, now calculate the fraction

    if (w->stats()->current_magic_delay > 1)
    {
        w->stats()->max_magic_delay /=
            static_cast<Sint32>(w->stats()->current_magic_delay + 1);
    }
    w->stats()->current_magic_delay = 0; //start off without magic regen

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our magic_per_round as one higher, and the math must
    //have been screwed up some how
    if (w->stats()->max_magic_delay < 2)
        w->stats()->max_magic_delay = 2;
}

// create_walker_owned and create_and_add_walker are in src/runtime/guy_create.cpp
// because they depend on screen* (runtime layer).
