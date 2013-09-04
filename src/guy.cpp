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
#include "graph.h"
#include <math.h>
#define RAISE 1.85  // please also change in picker.cpp

extern Sint32 costlist[NUM_FAMILIES];  // These come from picker.cpp
extern Sint32 statcosts[NUM_FAMILIES][6];
// Zardus: PORT, exception doesn't compile (dos thing?): int matherr(struct exception *);

int MAX(int a,int b)
{
	if (a < b)
		return b;
	else
		return a;
}

guy::guy()
{
	strcpy(name, "SOLDIER");
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
}

// Set defaults for various types
guy::guy(char whatfamily)
{

	family = whatfamily;
	kills = 0;
	level_kills = 0;
	total_damage = total_hits = total_shots = 0;
	exp = 0;
	teamnum = 0;

	switch (whatfamily)
	{
		case FAMILY_SOLDIER:
			strcpy(name, "SOLDIER");
			strength = 12;
			dexterity = 6;
			constitution = 12;
			intelligence = 8;
			level = 1;
			armor = 9;
			break;
		case FAMILY_ELF:
			strcpy(name, "ELF");
			strength = 5;
			dexterity = 9;
			constitution = 5;
			intelligence = 12;
			level = 1;
			armor = 8;
			break;
		case FAMILY_ARCHER:
			strcpy(name, "ARCHER");
			strength = 6;
			dexterity = 12;
			constitution = 6;
			intelligence = 10;
			level = 1;
			armor = 5;
			break;
		case FAMILY_MAGE:
			strcpy(name, "MAGE");
			strength = 4;
			dexterity = 6;
			constitution = 4;
			intelligence = 16;
			level = 1;
			armor = 5;
			break;
		case FAMILY_ARCHMAGE:
			strcpy(name, "ARCHMAGE");
			strength = 8;
			dexterity = 12;
			constitution = 8;
			intelligence = 32;
			level = 1;
			armor = 10;
			break;
		case FAMILY_SKELETON:
			strcpy(name, "SKELETON");
			strength = 9;
			dexterity = 6;
			constitution = 9;
			intelligence = 6;
			level = 1;
			armor = 9;
			break;
		case FAMILY_CLERIC:
			strcpy(name, "CLERIC");
			strength = 6;
			dexterity = 7;
			constitution = 6;
			intelligence = 14;
			level = 1;
			armor = 7;
			break;
		case FAMILY_FIREELEMENTAL:
			strcpy(name, "ELEMENTAL");
			strength = 14;
			dexterity = 5;
			constitution = 30;
			intelligence = 6;
			level = 1;
			armor = 16;
			break;
		case FAMILY_FAERIE:
			strcpy(name, "FAERIE");
			strength = 3;
			dexterity = 8;
			constitution = 3;
			intelligence = 8;
			level = 1;
			armor = 4;
			break;
		case FAMILY_SLIME:
			strcpy(name, "SLIME");
			strength = 30;
			dexterity = 2;
			constitution = 30;
			intelligence = 4;
			level = 1;
			armor = 20;
			break;
		case FAMILY_SMALL_SLIME:
			strcpy(name, "SLIME");
			strength = 18;
			dexterity = 2;
			constitution = 18;
			intelligence = 4;
			level = 1;
			armor = 8;
			break;
		case FAMILY_MEDIUM_SLIME:
			strcpy(name, "SLIME");
			strength = 24;
			dexterity = 2;
			constitution = 24;
			intelligence = 4;
			level = 1;
			armor = 14;
			break;
		case FAMILY_THIEF:
			strcpy(name, "THIEF");
			strength = 9;
			dexterity = 12;
			constitution = 12;
			intelligence = 10;
			level = 1;
			armor = 5;
			break;
		case FAMILY_GHOST:
			strcpy(name, "GHOST");
			strength = 6;
			dexterity = 12;
			constitution = 18;
			intelligence = 10;
			level = 1;
			armor = 15;
			break;
		case FAMILY_DRUID:
			strcpy(name, "DRUID");
			strength = 7;
			dexterity = 8;
			constitution = 6;
			intelligence = 12;
			level = 1;
			armor = 7;
			break;
		case FAMILY_ORC:
			strcpy(name, "ORC");
			strength = 16;
			dexterity = 4;
			constitution = 14;
			intelligence = 2;
			level = 1;
			armor = 11;
			break;
		case FAMILY_BIG_ORC:
			strcpy(name, "ORCER");
			strength = 16;
			dexterity = 4;
			constitution = 14;
			intelligence = 2;
			level = 1;
			armor = 11;
			break;
		case FAMILY_BARBARIAN:
			strcpy(name, "BARBARIAN");
			strength = 14;
			dexterity = 5;
			constitution = 14;
			intelligence = 8;
			level = 1;
			armor = 8;
			break;
		default :
			strcpy(name, "UNKNOWN");
			family = FAMILY_SOLDIER;
			strength = 12;
			dexterity = 6;
			constitution = 12;
			intelligence = 8;
			level = 1;
			armor = 6;
			break;
	}
}


guy::guy(const guy& copy)
    : family(copy.family)
    , strength(copy.strength), dexterity(copy.dexterity), constitution(copy.constitution), intelligence(copy.intelligence)
    , level(copy.level), armor(copy.armor)
    , exp(copy.exp), kills(copy.kills), level_kills(copy.level_kills)
    , total_damage(copy.total_damage), total_hits(copy.total_hits), total_shots(copy.total_shots)
    , teamnum(copy.teamnum)
{
    strcpy(name, copy.name);
}

guy::~guy()
{
    
}

Sint32 guy::query_heart_value() // how much are we worth?
{
	guy *normal = new guy(family); // for base comparisons
	Sint32 cost=0, temp;

	if (!normal)
		return 0;

	// Get strength cost ..
	temp = strength - normal->strength; // difference..
	temp = MAX(temp,0);
	cost += (Sint32) (pow( temp, RAISE)
	                * (Sint32)statcosts[(int)family][0]);

	// Get dexterity cost ..
	temp = dexterity - normal->dexterity; // difference..
	temp = MAX(temp,0);
	cost += (Sint32) (pow( temp, RAISE)
	                * (Sint32)statcosts[(int)family][1]);

	// Get constitution cost ..
	temp = constitution - normal->constitution; // difference..
	temp = MAX(temp,0);
	cost += (Sint32) (pow( temp, RAISE)
	                * (Sint32)statcosts[(int)family][2]);

	// Get intelligence cost ..
	temp = intelligence - normal->intelligence; // difference..
	temp = MAX(temp,0);
	cost += (Sint32) (pow( temp, RAISE)
	                * (Sint32)statcosts[(int)family][3]);

	// Get armor cost ..
	temp = armor - normal->armor; // difference..
	temp = MAX(temp,0);
	cost += (Sint32) (pow( temp, RAISE)
	                * (Sint32)statcosts[(int)family][4]);

	// Add in the base cost value for the guy ..
	cost += (Sint32) costlist[(int)family];
	
	delete normal;

	return cost;

}

walker* guy::create_walker(screen* myscreen)
{
    guy* temp_guy = new guy(*this);
    walker* temp_walker = myscreen->level_data.myloader->create_walker(ORDER_LIVING, temp_guy->family, NULL);
    temp_walker->myguy = temp_guy;
    temp_walker->stats->level = temp_guy->level;

    // Set hitpoints based on stats:
    temp_walker->stats->max_hitpoints += (short)
                                        (10 + (temp_guy->constitution*3 +
                                               ((temp_guy->strength)/2) + (short) (25*temp_guy->level))  );
    temp_walker->stats->hitpoints = temp_walker->stats->max_hitpoints;

    // Set damage based on strength and level
    temp_walker->damage += (temp_guy->strength/4) + temp_guy->level
                           + (temp_guy->dexterity/11);
    // Set magicpoints based on stats:
    temp_walker->stats->max_magicpoints = (short)
                                          (10 + (temp_guy->intelligence*3) + ( 25 * temp_guy->level) +(short) (temp_guy->dexterity) );
    temp_walker->stats->magicpoints = temp_walker->stats->max_magicpoints;

    // Set our armor level ..
    temp_walker->stats->armor =(short)  ( temp_guy->armor + (temp_guy->dexterity / 14) + temp_guy->level );

    // Set the heal delay ..

    temp_walker->stats->max_heal_delay = REGEN;
    temp_walker->stats->current_heal_delay =
        (temp_guy->constitution) + (temp_guy->strength/6) +
        (temp_guy->level * 2) + 20; //for purposes of calculation only

    while (temp_walker->stats->current_heal_delay > REGEN)
    {
        temp_walker->stats->current_heal_delay -= REGEN;
        temp_walker->stats->heal_per_round++;
    } // this takes care of the integer part, now calculate the fraction

    if (temp_walker->stats->current_heal_delay > 1)
    {
        temp_walker->stats->max_heal_delay /=
            (Sint32) (temp_walker->stats->current_heal_delay + 1);
    }
    temp_walker->stats->current_heal_delay = 0; //start off without healing

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our heal_per_round as one higher, and the math must
    //have been screwed up some how
    if (temp_walker->stats->max_heal_delay < 2)
        temp_walker->stats->max_heal_delay = 2;

    // Set the magic delay ..
    temp_walker->stats->max_magic_delay = REGEN;
    temp_walker->stats->current_magic_delay = (Sint32)
            (temp_guy->intelligence * 45) + (temp_guy->level*60) +
            (temp_guy->dexterity * 15) + 200;

    while (temp_walker->stats->current_magic_delay > REGEN)
    {
        temp_walker->stats->current_magic_delay -= REGEN;
        temp_walker->stats->magic_per_round++;
    } // this takes care of the integer part, now calculate the fraction

    if (temp_walker->stats->current_magic_delay > 1)
    {
        temp_walker->stats->max_magic_delay /=
            (Sint32) (temp_walker->stats->current_magic_delay + 1);
    }
    temp_walker->stats->current_magic_delay = 0; //start off without magic regen

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our magic_per_round as one higher, and the math must
    //have been screwed up some how
    if (temp_walker->stats->max_magic_delay < 2)
        temp_walker->stats->max_magic_delay = 2;

    //stepsize makes us run faster, max for a non-weapon is 12
    temp_walker->stepsize += (temp_guy->dexterity/54);
    if (temp_walker->stepsize > 12)
        temp_walker->stepsize = 12;
    temp_walker->normal_stepsize = temp_walker->stepsize;

    //fire_frequency makes us fire faster, min is 1
    temp_walker->fire_frequency -= (temp_guy->dexterity / 47);
    if (temp_walker->fire_frequency < 1)
        temp_walker->fire_frequency = 1;

    // Fighters: limited weapons
    if (temp_walker->query_family() == FAMILY_SOLDIER)
        temp_walker->weapons_left = (short) ((temp_walker->stats->level+1) / 2);

    // Set our team number ..
    temp_walker->team_num = temp_guy->teamnum;
    temp_walker->real_team_num = 255;
    
    return temp_walker;
}

walker* guy::create_and_add_walker(screen* myscreen)
{
    guy* temp_guy = new guy(*this);
    walker* temp_walker = myscreen->add_ob(ORDER_LIVING, temp_guy->family);
    temp_walker->myguy = temp_guy;
    temp_walker->stats->level = temp_guy->level;

    // Set hitpoints based on stats:
    temp_walker->stats->max_hitpoints += (short)
                                        (10 + (temp_guy->constitution*3 +
                                               ((temp_guy->strength)/2) + (short) (25*temp_guy->level))  );
    temp_walker->stats->hitpoints = temp_walker->stats->max_hitpoints;

    // Set damage based on strength and level
    temp_walker->damage += (temp_guy->strength/4) + temp_guy->level
                           + (temp_guy->dexterity/11);
    // Set magicpoints based on stats:
    temp_walker->stats->max_magicpoints = (short)
                                          (10 + (temp_guy->intelligence*3) + ( 25 * temp_guy->level) +(short) (temp_guy->dexterity) );
    temp_walker->stats->magicpoints = temp_walker->stats->max_magicpoints;

    // Set our armor level ..
    temp_walker->stats->armor =(short)  ( temp_guy->armor + (temp_guy->dexterity / 14) + temp_guy->level );

    // Set the heal delay ..

    temp_walker->stats->max_heal_delay = REGEN;
    temp_walker->stats->current_heal_delay =
        (temp_guy->constitution) + (temp_guy->strength/6) +
        (temp_guy->level * 2) + 20; //for purposes of calculation only

    while (temp_walker->stats->current_heal_delay > REGEN)
    {
        temp_walker->stats->current_heal_delay -= REGEN;
        temp_walker->stats->heal_per_round++;
    } // this takes care of the integer part, now calculate the fraction

    if (temp_walker->stats->current_heal_delay > 1)
    {
        temp_walker->stats->max_heal_delay /=
            (Sint32) (temp_walker->stats->current_heal_delay + 1);
    }
    temp_walker->stats->current_heal_delay = 0; //start off without healing

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our heal_per_round as one higher, and the math must
    //have been screwed up some how
    if (temp_walker->stats->max_heal_delay < 2)
        temp_walker->stats->max_heal_delay = 2;

    // Set the magic delay ..
    temp_walker->stats->max_magic_delay = REGEN;
    temp_walker->stats->current_magic_delay = (Sint32)
            (temp_guy->intelligence * 45) + (temp_guy->level*60) +
            (temp_guy->dexterity * 15) + 200;

    while (temp_walker->stats->current_magic_delay > REGEN)
    {
        temp_walker->stats->current_magic_delay -= REGEN;
        temp_walker->stats->magic_per_round++;
    } // this takes care of the integer part, now calculate the fraction

    if (temp_walker->stats->current_magic_delay > 1)
    {
        temp_walker->stats->max_magic_delay /=
            (Sint32) (temp_walker->stats->current_magic_delay + 1);
    }
    temp_walker->stats->current_magic_delay = 0; //start off without magic regen

    //make sure we have at least a 2 wait, otherwise we should have
    //calculated our magic_per_round as one higher, and the math must
    //have been screwed up some how
    if (temp_walker->stats->max_magic_delay < 2)
        temp_walker->stats->max_magic_delay = 2;

    //stepsize makes us run faster, max for a non-weapon is 12
    temp_walker->stepsize += (temp_guy->dexterity/54);
    if (temp_walker->stepsize > 12)
        temp_walker->stepsize = 12;
    temp_walker->normal_stepsize = temp_walker->stepsize;

    //fire_frequency makes us fire faster, min is 1
    temp_walker->fire_frequency -= (temp_guy->dexterity / 47);
    if (temp_walker->fire_frequency < 1)
        temp_walker->fire_frequency = 1;

    // Fighters: limited weapons
    if (temp_walker->query_family() == FAMILY_SOLDIER)
        temp_walker->weapons_left = (short) ((temp_walker->stats->level+1) / 2);

    // Set our team number ..
    temp_walker->team_num = temp_guy->teamnum;
    temp_walker->real_team_num = 255;
    
    return temp_walker;
}

// Zardus: PORT: still no exception struct
//int matherr(struct exception *problem)
//{
//  char message[80];
//  // If we're a "pow" function with a <0 domain,
//  // just ignore it:
//  if (!strcmp("pow", problem->name) && problem->arg1 < 0)
//  {
//    problem->type = 0;
//    problem->retval = 0;
//    return 0;
//  }
//  // Otherwise, do nothing, but print a message
//  sprintf(message, "Error: %s (%d, %d)", problem->name,
//    problem->arg1, problem->arg2);
//  myscreen->do_notify(message, NULL);
//  return 0;
//}

