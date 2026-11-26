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
#ifndef __STATS_H
#define __STATS_H

// Definition of STATS class

#include "base.h"
#include <list>

//
// Include file for the stats object
//

// These are for the bit-flags
#define BIT_FLYING     (Sint32)     1  // fly over water, trees
#define BIT_SWIMMING   (Sint32)     2  // move over water
#define BIT_ANIMATE    (Sint32)     4  // animate even when not moving
#define BIT_INVINCIBLE (Sint32)     8  // can't be harmed
#define BIT_NO_RANGED  (Sint32)    16  // no ranged attack
#define BIT_IMMORTAL   (Sint32)    32  // for weapons that don't die when
//   they hit
#define BIT_NO_COLLIDE (Sint32)    64  // fly through walkers
#define BIT_PHANTOM    (Sint32)   128  // use phantomputbuffer instead of
//   walkerputbuffer
#define BIT_NAMED      (Sint32)   256  // has a name (will have outline)
#define BIT_FORESTWALK (Sint32)   512  // can walk through forests
#define BIT_MAGICAL    (Sint32)  1024  // generally for magical weapons
#define BIT_FIRE       (Sint32)  2048  // for any flame weapons
#define BIT_ETHEREAL   (Sint32)  4096  // fly "through" walls
#define BIT_LAST       (Sint32)  8192
// Other special effects, etc.
#define FAERIE_FREEZE_TIME    40


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
		bool has_commands();
		void clear_command();
		void clear_command_for_control_switch();
		short do_command();
		void hit_response(walker * who);
		void yell_for_help(walker *foe);  // yell and run away
		short query_bit_flags(Sint32 myvalue);
        void clear_bit_flags();
		void set_bit_flags(Sint32 someflag, short newvalue); // sets a single flag
		// quiet: the right-hand walk's probes asked without a side effect
		// (Gameplay Fixes Y; port of the current game's af9ff6cd)
		bool right_blocked(bool quiet = false); // is our right blocked?
		bool right_forward_blocked(bool quiet = false);
		bool right_back_blocked(bool quiet = false);
		// quiet: the same question asked without a draw or a side effect
		// (port of the current game's 15053f4a)
		bool forward_blocked(bool quiet = false); // are we blocked in front?
		// The right-hand walk's four probes, counted from the facing.
		enum class Side : short { Front = 0, RightFront = 1, Right = 2, RightBack = 3 };
		// Gameplay Fixes Y: one of the four probes asked with `quiet`, then
		// the bump the classic loud probe gave: with the setting on, a
		// melee-only bot (BIT_NO_RANGED) whose own live foe stands where the
		// probe looked runs into it (living::collide: collide_ob, a swing
		// along its heading). Everything else the probe met stays untouched.
		// Off, the probe alone. See right_walk. (Port of the current game's
		// 5f946150.)
		bool path_probe(Side side, bool quiet);
		//short distance_to_foe(); //not in use???????
		bool right_walk();      // walk using right-hand rule
		bool direct_walk(); // walk in a line toward foe ..
		bool walk_to_foe(); // try to walk intelligently towards foe
		// The turn toward a foe at `aim` (a facing) that walk_to_foe's near
		// branch and living::act_random take; answers whether the bot already
		// faced it. Classic, walker::turn alone. With Gameplay Fixes on and the
		// foe not faced the turn is kept: a tower faces it at once, heading
		// included, any other bot keeps the aim as enddir and turns a notch.
		// (Port of the current game's 404ae76d8, the kept turn, Gameplay
		// Fixes BE, #513; turn_to_foe was near_turn when fix BB shipped.)
		bool turn_to_foe(short aim);
		// Gameplay Fixes BJ, a bot treats an enemy that only a locked door
		// keeps it from as if it were not there (port of the current game's
		// f97bc3ec7 and ec95086d1). keeps_foe, asked at the top of a SEARCH
		// and an ATTACK round of a live held foe: a foe only a locked door
		// keeps the bot from is let go (no foe, the orders and the leader
		// cleared, the route cleared: LetGo); one its shot lands on from
		// where it stands is kept only by the shot (ByShot); every other is
		// kept (Kept, also for every walker asks does not name). Draws
		// nothing.
		enum class FoeKeep { Kept, ByShot, LetGo };
		FoeKeep keeps_foe();
		// The SEARCH order's round with a live held foe: keeps_foe, then for
		// a foe kept only by the shot the bot faces it (face_delta along the
		// 3:1-snapped raw delta) and asks direct_walk's shot test
		// (shot_attacks) with no step; else walk_to_foe as before.
		void search_round();
		// The ATTACK order's end (its count to 0) when an order is left to
		// end: after a let-go the queue is empty.
		void end_order();
		// The ATTACK order's two steps toward its foe: neither is taken by a
		// bot that keeps its foe only by its shot.
		void attack_step(short xdelta, short ydelta);
		// direct_walk's shot test, written once: fire_check along the delta;
		// on a pass the orders are wiped, the bot turns toward its foe and
		// starts an ATTACK of 30+random(25) rounds.
		bool shot_attacks(float xdelta, float ydelta);
		// The wander of a bot with no foe (living.cpp: the seek's coin and
		// act_random's): `bot` is the living that is deciding. A bot shut out
		// by a locked door (shut_out) queues no wander and makes neither of
		// its two direction draws: it is handed the locked door in its way
		// (breaks, by hand_door; Gameplay Fixes BL), and only when there is
		// none it stays where it is. Every other walker wanders as before.
		short idle_walk(walker *bot, short rounds);
		// walk_to_foe's near branch: how many of `foes` are not door-shut for
		// the bot (one door_asker).
		short foes_not_door_shut(std::list<walker*>& foes);
		// Gameplay Fixes BJ and BL: the let-go of a held foe: no foe, the
		// orders cleared (clear_command, which clears the leader too), the
		// route cleared. Draws nothing.
		void let_go();
		// Gameplay Fixes BL, a bot with no enemy it can get at breaks down
		// the locked door in its way (port of the current game's cfe528040,
		// 8a8061876, 4b1ad8ef9, e36c6ccf1, 3f843abcc and e3be286c2).
		// keeps_door: the keep of a held door, asked at the top of a SEARCH
		// and an ATTACK round (keeps_foe) and after every act (screen::act),
		// of a door holder only (holds_door); a door the keep no longer
		// allows (::keeps_door answers NULL: the bot gets at an enemy, is
		// shut out no more, or no door leads to a hostile) is let go
		// (LetGo); while the conditions hold and only another door
		// qualifies, that door is handed (hand_door). Every other walker
		// keeps its foe (Kept). Draws nothing.
		FoeKeep keeps_door();
		// holds_door: is `bot` a bot asks names whose foe `foe` is a live
		// door, barring it or not (a door holder)? Written once for its
		// askers: the keep, living::act_random (a door holder keeps its door
		// through the one-in-80 retarget) and walks_into_door. This game's
		// statistics always name their walker, so the current game's owner
		// test (statistics::keep_asked_of) has nothing to ask here.
		static bool holds_door(walker *bot, walker *foe);
		// walks_into_door: is `bot` a door holder (holds_door) that is
		// melee-only (BIT_NO_RANGED)? Such a bot walks into its door: the
		// chase's near branch never takes it (it plans its route into the
		// door at any distance), it re-plans on its next chase after it is
		// handed a door (hand_door), and its route's last hop meets the door
		// from every side (walker::follow_path_to_foe).
		static bool walks_into_door(walker *bot, walker *foe);
		// hand_door: the one hand-over of a door (idle_walk, keeps_door's
		// swap and the hand-out in screen::act): the door is set as the
		// bot's foe, and a bot walks_into_door names has its re-plan counter
		// set to 0.
		static void hand_door(walker *bot, walker *door);

		char name[12]; // for NPC's, normally ..
		char old_order, old_family;
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
		bool forced;
};

#endif

