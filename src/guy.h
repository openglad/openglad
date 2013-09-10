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
#ifndef __GUY_H
#define __GUY_H

#include "base.h"

// Holds attributes for characters.
// Used to store character data in SaveData's team_list.
// Used as walker::myguy in-game for various attribute-dependent effects for walkers who are on the player team.
class guy
{
	public:
		guy ();
		guy (char whatfamily);
		guy (const guy& copy);
		~guy();
		Sint32 query_heart_value(); // how much are we worth?
		walker* create_walker(screen* myscreen);
		walker* create_and_add_walker(screen* myscreen);

		char name[12];
		char family;  // our family
		short strength;
		short dexterity;
		short constitution;
		short intelligence;
		short level;
		short armor;
		Uint32 exp;
		short kills;       // version 3+
		Sint32 level_kills;  // version 3+
		Sint32 total_damage; // version 4+
		Sint32 total_hits;   // version 4+
		Sint32 total_shots;  // version 4+
		short teamnum;     // version 5+
        
        // In ID for comparing old guys with their duplicated counterparts after battle
        int id;
};

#endif

