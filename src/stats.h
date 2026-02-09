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

// Definition of STATS class

#include "base.h"
#include <list>

//
// Include file for the stats object
//

// These are for the bit-flags
inline constexpr Sint32 BIT_FLYING     =     1;  // fly over water, trees
inline constexpr Sint32 BIT_SWIMMING   =     2;  // move over water
inline constexpr Sint32 BIT_ANIMATE    =     4;  // animate even when not moving
inline constexpr Sint32 BIT_INVINCIBLE =     8;  // can't be harmed
inline constexpr Sint32 BIT_NO_RANGED  =    16;  // no ranged attack
inline constexpr Sint32 BIT_IMMORTAL   =    32;  // for weapons that don't die when
                                                  //   they hit
inline constexpr Sint32 BIT_NO_COLLIDE =    64;  // fly through walkers
inline constexpr Sint32 BIT_PHANTOM    =   128;  // use phantomputbuffer instead of
                                                  //   walkerputbuffer
inline constexpr Sint32 BIT_NAMED      =   256;  // has a name (will have outline)
inline constexpr Sint32 BIT_FORESTWALK =   512;  // can walk through forests
inline constexpr Sint32 BIT_MAGICAL    =  1024;  // generally for magical weapons
inline constexpr Sint32 BIT_FIRE       =  2048;  // for any flame weapons
inline constexpr Sint32 BIT_ETHEREAL   =  4096;  // fly "through" walls
inline constexpr Sint32 BIT_LAST       =  8192;
// Other special effects, etc.
inline constexpr int FAERIE_FREEZE_TIME = 40;


// Class statistics,
// for (guess what?) controlling stats, etc ..
//
class statistics
{
	public:
		statistics(walker  *);
		~statistics();
		short  try_command(short whatcommand, short iterations, short info1, short info2);
		short  try_command(short whatcommand, short iterations);
		void set_command(short whatcommand, short iterations);
		void set_command(short whatcommand, short iterations, short info1, short info2);
		void add_command(short whatcommand, short iterations, short info1, short info2);
		void force_command(short whatcommand, short iterations, short info1, short info2);
		bool has_commands() const;
		void clear_command();
		short do_command();
		void hit_response(walker * who);
		void yell_for_help(walker *foe);  // yell and run away
		short query_bit_flags(Sint32 myvalue) const;
        void clear_bit_flags();
		void set_bit_flags(Sint32 someflag, short newvalue); // sets a single flag
		bool right_blocked(); // is our right blocked?
		bool right_forward_blocked();
		bool right_back_blocked();
		bool forward_blocked(); // are we blocked in front?
		//short distance_to_foe(); //not in use???????
		bool right_walk();      // walk using right-hand rule
		bool direct_walk(); // walk in a line toward foe ..
		bool walk_to_foe(); // try to walk intelligently towards foe

		std::string name; // for NPC's, normally ..
		Order old_order;
		char old_family;
		Uint32 last_distance;
		Sint32 current_distance;  // Distances (to foe) are used for AI walking
		Sint32 bit_flags;         // holds (currently) 32 bit flags
		short delete_me;
		
		float hitpoints;
		float max_hitpoints;
		float magicpoints;
		float max_magicpoints;
		
		Sint32 max_heal_delay;
		Sint32 current_heal_delay;
		Sint32 max_magic_delay;
		Sint32 current_magic_delay;
		Sint32 magic_per_round; //magic we regain each round
		Sint32 heal_per_round; //hp we regain each round
		float armor; // reduces damage against us
		
		unsigned short level;
		short frozen_delay;              // use for paralyzing..
		unsigned short special_cost[NUM_SPECIALS];  // cost of our special ability
		short weapon_cost;                          // cost of our weapon
		walker  * controller;
		std::list<command> commands;
	private:
		//       short com1, com2;        // parameters to command
		Sint32 walkrounds; //number of rounds we've spent rightwalking

};

class command
{
	public:
		command();
		short commandtype;
		short commandcount;
		short com1;
		short com2;
};

