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

extern long costlist[NUM_FAMILIES];  // These come from picker.cpp
extern long statcosts[NUM_FAMILIES][6];
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
	next = NULL;
}

// Set defaults for various types
guy::guy(char whatfamily)
{

	family = whatfamily;
	kills = 0;
	level_kills = 0;
	total_damage = total_hits = total_shots = 0;
	next = NULL;
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

guy::~guy()
{
	// If I have a link, delete it (recursively)
	//if (next)
	//     delete next;
	next = NULL;
}

long guy::query_heart_value() // how much are we worth?
{
	guy *normal = new guy(family); // for base comparisons
	long cost=0, temp;

	if (!normal)
		return 0l;

	// Get strength cost ..
	temp = strength - normal->strength; // difference..
	temp = MAX(temp,0);
	cost += (long) (pow( temp, RAISE)
	                * (long)statcosts[(int)family][0]);

	// Get dexterity cost ..
	temp = dexterity - normal->dexterity; // difference..
	temp = MAX(temp,0);
	cost += (long) (pow( temp, RAISE)
	                * (long)statcosts[(int)family][1]);

	// Get constitution cost ..
	temp = constitution - normal->constitution; // difference..
	temp = MAX(temp,0);
	cost += (long) (pow( temp, RAISE)
	                * (long)statcosts[(int)family][2]);

	// Get intelligence cost ..
	temp = intelligence - normal->intelligence; // difference..
	temp = MAX(temp,0);
	cost += (long) (pow( temp, RAISE)
	                * (long)statcosts[(int)family][3]);

	// Get armor cost ..
	temp = armor - normal->armor; // difference..
	temp = MAX(temp,0);
	cost += (long) (pow( temp, RAISE)
	                * (long)statcosts[(int)family][4]);

	// Add in the base cost value for the guy ..
	cost += (long) costlist[(int)family];

	return cost;

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

