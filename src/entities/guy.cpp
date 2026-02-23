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
#include <cstdint>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/data/gloader.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <cmath>
#include <cstring>
#define RAISE 1.85  // please also change in picker.cpp

// Zardus: PORT, exception doesn't compile (dos thing?): int matherr(struct exception *);


const char* get_family_string(std::int32_t family);

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

std::int32_t guy::query_heart_value() // how much are we worth?
{
	guy normal(family); // for base comparisons
	std::int32_t cost=0, temp;

	auto* fd = get_family_descriptor(static_cast<int>(family));
	if (!fd)
		return 0;

	// Get strength cost ..
	temp = strength - normal.strength;
	temp = MAX(temp,0);
	cost += static_cast<std::int32_t>(pow( temp, RAISE) * static_cast<std::int32_t>(fd->stat_costs[0]));

	// Get dexterity cost ..
	temp = dexterity - normal.dexterity;
	temp = MAX(temp,0);
	cost += static_cast<std::int32_t>(pow( temp, RAISE) * static_cast<std::int32_t>(fd->stat_costs[1]));

	// Get constitution cost ..
	temp = constitution - normal.constitution;
	temp = MAX(temp,0);
	cost += static_cast<std::int32_t>(pow( temp, RAISE) * static_cast<std::int32_t>(fd->stat_costs[2]));

	// Get intelligence cost ..
	temp = intelligence - normal.intelligence;
	temp = MAX(temp,0);
	cost += static_cast<std::int32_t>(pow( temp, RAISE) * static_cast<std::int32_t>(fd->stat_costs[3]));

	// Get armor cost ..
	temp = armor - normal.armor;
	temp = MAX(temp,0);
	cost += static_cast<std::int32_t>(pow( temp, RAISE) * static_cast<std::int32_t>(fd->stat_costs[4]));

	// Add in the base cost value for the guy ..
	cost += fd->hiring_cost;

	return cost;

}

std::uint32_t calculate_exp(std::int32_t level);





std::int32_t calculate_level(std::uint32_t experience)
{
	std::int32_t result=1;

	while (calculate_exp(result) <= experience)
		result++;
	return (result-1);
}

std::uint32_t calculate_exp(std::int32_t level)
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

void apply_level_up(guy* self, std::int32_t level_diff, const LevelUpGains& g)
{
    self->strength = static_cast<short>(static_cast<std::int32_t>(self->strength) + g.str * level_diff);
    self->dexterity = static_cast<short>(static_cast<std::int32_t>(self->dexterity) + g.dex * level_diff);
    self->constitution = static_cast<short>(static_cast<std::int32_t>(self->constitution) + g.con * level_diff);
    self->intelligence = static_cast<short>(static_cast<std::int32_t>(self->intelligence) + g.intel * level_diff);
    self->armor = static_cast<short>(static_cast<std::int32_t>(self->armor) + g.armor * level_diff);
}

void apply_difficulty_scaling(living* self, std::uint32_t level, const DifficultyScaling& s)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->max_hitpoints   += s.hp * levmult;
    self->stats()->max_magicpoints += s.mp * levmult;
    self->damage += s.dmg * level_f;
    self->stats()->armor += s.armor * levmult;
}

void guy::upgrade_to_level(short new_level, bool set_xp)
{
    std::int32_t level_diff = static_cast<std::int32_t>(new_level) - static_cast<std::int32_t>(this->level);

    auto* fd = get_family_descriptor(family);
    if (fd && fd->level_up)
    {
        fd->level_up(this, level_diff);
    }
    else
    {
        apply_level_up(this, level_diff, kDefaultLevelUpGains);
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
        w->stats()->current_heal_delay = static_cast<std::int32_t>(heal_delay); // for purposes of calculation only
    }

    while (w->stats()->current_heal_delay > REGEN)
    {
        w->stats()->current_heal_delay -= REGEN;
        w->stats()->heal_per_round++;
    } // this takes care of the integer part, now calculate the fraction

    if (w->stats()->current_heal_delay > 1)
    {
        w->stats()->max_heal_delay /=
            static_cast<std::int32_t>(w->stats()->current_heal_delay + 1);
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
            static_cast<std::int32_t>(w->stats()->current_magic_delay + 1);
    }
    w->stats()->current_magic_delay = 0; //start off without magic regen

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our magic_per_round as one higher, and the math must
    //have been screwed up some how
    if (w->stats()->max_magic_delay < 2)
        w->stats()->max_magic_delay = 2;
}

// guy_create_walker_owned and guy_create_and_add_walker are free functions
// in src/runtime/guy_create.cpp (declared in runtime/guy_create.h) because
// they depend on screen* (runtime layer).
