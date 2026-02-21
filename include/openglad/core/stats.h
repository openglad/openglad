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

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <cstdint>
#include <list>
#include <string>

class walker;
class command;

inline constexpr int NUM_SPECIALS = 6;

//
// Include file for the stats object
//

// These are for the bit-flags
inline constexpr std::int32_t BIT_FLYING     =     1;  // fly over water, trees
inline constexpr std::int32_t BIT_SWIMMING   =     2;  // move over water
inline constexpr std::int32_t BIT_ANIMATE    =     4;  // animate even when not moving
inline constexpr std::int32_t BIT_INVINCIBLE =     8;  // can't be harmed
inline constexpr std::int32_t BIT_NO_RANGED  =    16;  // no ranged attack
inline constexpr std::int32_t BIT_IMMORTAL   =    32;  // for weapons that don't die when
                                                        //   they hit
inline constexpr std::int32_t BIT_NO_COLLIDE =    64;  // fly through walkers
inline constexpr std::int32_t BIT_PHANTOM    =   128;  // use phantomputbuffer instead of
                                                        //   walkerputbuffer
inline constexpr std::int32_t BIT_NAMED      =   256;  // has a name (will have outline)
inline constexpr std::int32_t BIT_FORESTWALK =   512;  // can walk through forests
inline constexpr std::int32_t BIT_MAGICAL    =  1024;  // generally for magical weapons
inline constexpr std::int32_t BIT_FIRE       =  2048;  // for any flame weapons
inline constexpr std::int32_t BIT_ETHEREAL   =  4096;  // fly "through" walls
inline constexpr std::int32_t BIT_LAST       =  8192;
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
		short  try_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		short  try_command(std::int32_t whatcommand, std::int32_t iterations);
		void set_command(std::int32_t whatcommand, std::int32_t iterations);
		void set_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		void add_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		void force_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		bool has_commands() const;
		void clear_command();
		short do_command();
		void hit_response(walker * who);
		void yell_for_help(walker *foe);  // yell and run away
		short query_bit_flags(std::int32_t myvalue) const;
        void clear_bit_flags();
		void set_bit_flags(std::int32_t someflag, short newvalue); // sets a single flag
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
		std::uint32_t last_distance;
		std::int32_t current_distance;  // Distances (to foe) are used for AI walking
		std::int32_t bit_flags;         // holds (currently) 32 bit flags
		short delete_me;

		float hitpoints;
		float max_hitpoints;
		float magicpoints;
		float max_magicpoints;

		std::int32_t max_heal_delay;
		std::int32_t current_heal_delay;
		std::int32_t max_magic_delay;
		std::int32_t current_magic_delay;
		float magic_per_round; // magic we regain each round
		float heal_per_round;  // hp we regain each round
		float armor; // reduces damage against us

		std::int32_t level;
		short frozen_delay;              // use for paralyzing..
		unsigned short special_cost[NUM_SPECIALS];  // cost of our special ability
		short weapon_cost;                          // cost of our weapon
		walker  * controller;
		std::list<command> commands;
	private:
		//       short com1, com2;        // parameters to command
		std::int32_t walkrounds; //number of rounds we've spent rightwalking

};

class command
{
	public:
		command();
		std::int32_t commandtype;
		std::int32_t commandcount;
		std::int32_t com1;
		std::int32_t com2;
};
