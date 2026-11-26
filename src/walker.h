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
#ifndef __WALKER_H
#define __WALKER_H

// Definition of WALKER class

#include "base.h"
#include "pixien.h"
#include "obmap.h"

class walker : public pixieN
{
	public:
		friend class statistics;
		friend class command;
		// detached_probe: a body that is only asked about (the raise's body
		// probe, the weapon pad probe): never filed in the obmap and no rand()
		// for the path cadence, since random() reads the same rand() stream
		// and one extra call would move every later draw. (port of the
		// current game's 15053f4a, its WeaponProfileProbeTag constructor)
		// It is kept in `detached` (below).
		walker(const PixieData& data, bool detached_probe = false);
		virtual ~walker();
		short reset(void);
		short move(short x, short y);
		void worldmove(float x, float y);
		virtual short setxy(short x, short y);
		void setworldxy(float x, float y);
		bool walk();
		bool walkstep(float x, float y);
		virtual bool walk(float x, float y);
		short draw(viewscreen  *view_buf);
		short draw_tile(viewscreen  *view_buf);
		void draw_path(viewscreen* view_buf);
		void find_path_to_foe();
		void follow_path_to_foe();
		// What this walker gets to, asked afresh for each question (port of
		// the current game's og::reach maps, 3d9dcdcd8; this game keeps no
		// map). A cell is a 16 px cell of the level's grid, (cx, cy).
		// cell_stands: this walker's box, its corner on the cell's corner,
		// passes the route finder's cell test; with `shut`, also no door
		// that bars it (door_bars with `keys`, its own foe not left out)
		// meets the box. reach_fill: the cells a breadth-first fill from
		// (cx, cy) reaches over the route finder's steps (the neighbour rule
		// of Map::AdjacentCost) on the cells that stand in that mode, the
		// start cell included; one char a cell, row by row (cy * grid.w +
		// cx), 1 reached. Nothing is kept and nothing is drawn.
		bool cell_stands(short cx, short cy, bool shut, Uint32 keys);
		std::vector<char> reach_fill(short cx, short cy, bool shut, Uint32 keys);
		// Gameplay Fixes BI (port of the current game's 708b27b91,
		// og::reach::start_cell): the cell a walker holding `keys` starts
		// from, the cell of its corner; but when that cell stands with the
		// doors open and not with them shut (only a door that bars it
		// closes it), the cell standing with the doors shut that its box
		// overlaps most (the cells its box meets row by row, left to right;
		// a tie keeps the first); with none, the corner's cell.
		void start_cell(short *cx, short *cy, Uint32 keys);
		// The approach cells of body b in a mode (port of the current game's
		// og::reach approach cells): every cell standing in that mode on
		// which this walker's box, grown by one cell on every side, meets
		// b's box (collide), row by row, left to right.
		void approach_cells(walker *b, bool shut, Uint32 keys, std::vector<std::pair<short, short> > &out);
		// The nearest approach cell of b that a fill from the cell
		// (scx, scy) in the mode reaches: fewest cells from it (Manhattan),
		// the first of equal distance in the scan's order. False when none.
		// (port of the current game's og::reach nearest_approach)
		bool nearest_approach(walker *b, short scx, short scy, bool shut, Uint32 keys, short *ocx, short *ocy);
		short init_fire();
		short init_fire(short xdir, short ydir);
		// fire_check's test weapon: placed and headed by the heading table
		// below for the way this walker faces (facing(lastx, lasty)), its
		// waver drawn as fire() draws a shot's.
		void set_weapon_heading(walker *weapon);
		// The heading table, the facing and the waver given: the weapon set
		// on the pad for that facing (weapon_pad) and headed along it, the
		// waver across the heading. Draws nothing. (port of the current
		// game's 3d9dcdcd8, set_weapon_heading(weapon, dir, waver))
		void set_weapon_heading(walker *weapon, short dir, signed char waver);
		walker  * fire();
		// Where fire() puts the weapon it creates: the top-left corner of
		// `weapon`'s box one pixel outside this walker, on the side
		// facing(lastx, lasty) names. The one table fire() and
		// set_weapon_heading() read.
		void weapon_pad(walker *weapon, short *px, short *py);
		// The same table for the facing `dir` (port of the current game's
		// 3d9dcdcd8, weapon_pad with a facing).
		void weapon_pad(walker *weapon, short *px, short *py, short dir);
		// What the pad fire() would place a weapon on holds, probed quietly
		// with a detached weapon: 0 clear (the weapon would fly); 1 terrain
		// (the grid refuses it: a wall, the map edge, a tree's crown or trunk
		// for a weapon that neither flies nor walks the forest; every weapon
		// crosses water); 2 a body (an enemy living or a generator, which
		// fire() would strike in melee); 3 no probe (a non-living caster, or a
		// weapon the loader cannot make); 4 an enemy weapon-order body (a
		// weapon in flight, a door, a grown tree), asked strictly
		// (weapons_block; port of the current game's b5e29f3c, Gameplay Fixes
		// AV, #451). Own-team bodies pass, as the weapon
		// passes them; an enemy door, tree or weapon in flight is passed by a
		// weapon probe too (obmap.cpp's weapon-vs-weapon arm) and answers
		// clear, though the loud fire() blocks on it 4 times in 10. weapon -1
		// probes current_weapon; a cast that swaps a weapon in for its fire()
		// (the archer's EXPLODING BOLT) passes its family. No walker made, no
		// draw, nothing eaten or opened. A bot gate's question. (port of the
		// current game's 15053f4a and 6ae833d7)
		short fire_pad_answer(short weapon = -1, bool weapons_block = false);
		// fire_pad_answer(weapon, weapons_block) == 0
		bool fire_pad_clear(short weapon = -1, bool weapons_block = false);
		// port of the current game's bf79ee0f (Gameplay Fixes AR, #435): room on fire()'s pad for a summon of this walker's own
		bool pad_room(char order, char family, short weapon = -1);
		// Gameplay Fixes AD (port of the current game's 55596283): true while
		// this walker's charm runs (charm_left > 0) with the setting on. Every
		// owner-chain climb stops here: a charmed troop or summon heads its
		// own chain and fights for the colour it wears.
		bool charmed_head();
		// Gameplay Fixes AP (port of the current game's d7e20962): the head
		// of the one owner-chain climb: up through every owner, a dead one
		// included, stopping at a self-loop and at a charmed head (the climb
		// walker::attack takes for its score gate's hero walks the same way).
		// Read with the setting on by sides_friendly and is_friendly_to_team
		// for whose walker is a hero's (the allied mode); since Gameplay Fixes
		// AZ no colour is read from it.
		walker* chain_head();
		// Gameplay Fixes T (port of the current game's d80a7faa, its
		// packs/core/lib/corpses.lua raise_spot): the stain a raise takes and
		// the spot its body stands on. The stains find_nearest_blood would
		// answer, nearest first; the first inside `range` (distance_to_ob, the
		// cast's compare) with room for the body within 16 px of its corner
		// (screen::body_spot_near) wins. body_family -1 is RESURRECT: the
		// corpse's own family when the stain is friendly, else a ghost.
		// Draw-free and eat-free; 0 when nothing can be raised.
		short raise_spot(Sint32 range, short body_family, walker **stain, short *sx, short *sy);
		virtual short act();
		short set_act_type(short num);
		short restore_act_type();
		short query_act_type();
		short set_old_act_type(short num);
		short query_old_act_type();
		virtual short collide(walker  *ob);
		short attack(walker  *target);
		virtual short animate();
		short set_order_family(char order, char family);
		virtual char query_order()
		{
			return order;
		}
		char query_family()
		{
			return family;
		}
		walker  *create_weapon();
		// The profile create_weapon gives the shot it makes, for a heading
		// (heading_x, heading_y): this walker's team and owner, the
		// difficulty and damage of its level, a guy's strength and dexterity,
		// the level's line of sight and the circular range of a cardinal
		// heading. The family's own part (the cleric's glow) is not in it.
		// (port of the current game's configure_weapon_profile_base)
		void configure_weapon_profile(walker *weapon, float heading_x, float heading_y);
		// The detached test weapon of this walker's current weapon: the
		// weapon as the loader makes it, profiled by
		// configure_weapon_profile alone for the heading given, built as a
		// detached probe (filed in no obmap, no rand() for the path cadence;
		// flown, it is offered no clash roll at an enemy weapon-order body
		// that is not a door, obmap.cpp). NULL for a walker that is no
		// living, or a weapon the loader cannot make. Draws nothing. The
		// caller deletes it. (port of the current game's
		// walker::unhooked_weapon)
		walker *unhooked_weapon(float heading_x, float heading_y);
		// Why a fire_check() attempt was denied. Callers that care (the
		// COMMAND_ATTACK melee loop) distinguish the orientation denials —
		// Facing, and NoRanged at bump range ("the foe is in weapon reach
		// but we are pointed the wrong way / can only hit by facing") —
		// from every other denial, because the classic response to ANY
		// denial (walk toward the foe, sliding perpendicular when blocked)
		// deadlocks two adjacent fighters forever. The reach gate runs
		// before the NoRanged/NoMagic gates so those two denials imply the
		// foe is within reach. Port of the current game's guard-standoff
		// melee deadlock fix (2026-07-07, c409e7c8).
		enum class FireCheckDenial : Uint8
		{
			None,        // check passed
			NoFoe,       // nothing to fire at
			NoRanged,    // BIT_NO_RANGED family (melee-only)
			NoMagic,     // weapon costs more magic than we have
			OutOfRange,  // foe beyond weapon stepsize*lineofsight
			Facing,      // in reach, but curdir is not toward the foe
			WallBlocked, // the shot ray hits terrain first, or with Gameplay Fixes on
			             // an enemy door that is not the foe (fix BF)
			RayMiss,     // the shot ray ran full range without a hit
		};
		short fire_check(short xdelta, short ydelta,
		                 FireCheckDenial* denial = NULL);
		// One step of a weapon in flight: walk() along its heading, and what
		// that met: 0 it flew, 1 it struck a live body, 2 it was stopped (a
		// wall, the map's edge, a corpse). act_fire takes it every act; with
		// Gameplay Fixes on, line_check_flight's test weapon takes the same
		// step (port of the current game's b0a1001d8 and 2b36b5d09, the line
		// check, Gameplay Fixes BD, #538).
		short flight_step();
		// Gameplay Fixes BD (#538): fire_check's ray, flown the way the shot
		// flies. -1 with the setting off (fire_check's classic loop asks);
		// else 1 the shot lands, 0 it does not, with the test weapon set dead
		// and *denial written. Gameplay Fixes BF (port of the current game's
		// e205957c6): an enemy door that is not the foe refuses it
		// (WallBlocked). With every_door_stops every live enemy door refuses
		// it, the shooter's foe included (port of the current game's
		// 3d9dcdcd8: the judgment of whether a shot lands from where a bot
		// stands asks it so; the line check itself does not).
		short line_check_flight(walker* weapon, FireCheckDenial* denial, bool every_door_stops = false);
		// A door: weapon order and the door's family (a golem's family number
		// is a door's). Gameplay Fixes BF asks it (port of the current game's
		// 3fe15190e). Defined after line_check_flight.
		bool is_door();
		// A bot: a living no player drives (user -1). The door rules ask it
		// (port of the current game's 3d9dcdcd8, walker::is_bot), and so does
		// Gameplay Fixes BF's bump (living::leaves_a_door_alone).
		bool is_bot();
		// The firing box: a body whose corner-to-corner delta is under
		// lineofsight * 16 px on both axes is inside it. living::act_random
		// fires only at a foe inside it, and the shot term of Gameplay Fixes
		// BJ (shoots_at) judges only a body inside it. (port of the current
		// game's f97bc3ec7, walker::in_firing_box)
		bool in_firing_box(short xdelta, short ydelta);
		// Snap-face a (foe) direction: sets curdir AND enddir (so the
		// act() pre-turn doesn't fight it) AND lastx/lasty (the thrown-
		// weapon heading that set_weapon_heading() reads).
		void face_delta(short xdelta, short ydelta);
		short query_next_to();
		short special();
		// Set by special() when the cast went through to its charge, cleared
		// when it starts: special() itself returns 0 either way, as it always
		// has. The bot act reads it with Gameplay Fixes W (port of the current
		// game's a202cb8a, whose special() answers whether it cast).
		bool special_charged;
		short teleport();
		short teleport_ranged(Sint32 range);
		Sint32  turn_undead(Sint32 range, Sint32 power);
		walker *nearest_charmable(std::list<walker*>& foes, short max_level); // port of the current game's 102f1003
		short undead_foes_in_range(Sint32 range); // port of the current game's 7a2ff3cc
		virtual short shove(walker  *target, short x, short y);
		virtual short eat_me(walker  *eater);
		virtual void set_direct_frame(short whichframe);
		bool turn(short targetdir);
		short spaces_clear(); // how many (of 8) spaces around us are clear
		void transfer_stats(walker  *newob); // transfer values to new walker
		void transform_to(char whatorder, char whatfamily); // change picture, etc.
		virtual short death(); // called when death/destruction occurs ..
		void generate_bloodspot(); // make a permanent stain ..
		virtual walker  *do_summon(char whatfamily, unsigned short lifetime);
		virtual short check_special();
		void center_on(walker  *target);  // center us on target
		virtual void set_difficulty(Uint32 whatlevel);
		Sint32 distance_to_ob(walker * target);
		Sint32 distance_to_ob_center(walker * target);
		virtual short facing(short x, short y);
		unsigned char query_team_color();
		Sint32 is_friendly(walker *target);
		// port of the current game's 497a97652 (Gameplay Fixes AZ, #502): the
		// side question of is_friendly without its null and dead tests, which a
		// blast asks (it strikes from its own death).
		Sint32 sides_friendly(walker *target);
		// port of the current game's 0cae0cd11 (Gameplay Fixes AZ, #502, its
		// walker::fights_for_team): the colour this walker fights for with the
		// setting on, its own, or for a thing that rides a live walker
		// (rides_owner) that walker's colour now. The one place the mark is read.
		unsigned char fights_for_team();
		Sint32 is_friendly_to_team(unsigned char team);
		inline short query_type(char oval, char fval)
		{
			if (oval == order && fval == family)
				return 1;
			else
				return 0;
		};


		Uint32 keys; // used to open doors
		short view_all;     // used for seeing treasures, etc. on radar
		short shifter_down; // is our shifter/alternate key pressed?
		short bonus_rounds; // used if an object has extra rounds this cycle
		short death_called; // if death has already been called
		short rides_owner; // Gameplay Fixes AZ (port of the current game's 497a97652 and 0cae0cd11, its KIT_RIDER): rides the walker who owns it and goes with him
		short weapons_left;   // for fighter's blades
		short yo_delay;
		float lastx, lasty;
		signed char curdir;  // Current direction facing
		signed char cycle;
		signed char  **ani;
		char action;
		// Zardus: FIX: lets make these unsigned so that real_team_num doesn't wrap around from 255 to -1 :-)
		unsigned char team_num;
		unsigned char real_team_num; // for 'Charm', etc.
		char ani_type;
		float worldx, worldy;  // Floating point buffer for movement
		float stepsize;
		float normal_stepsize; // used for elven forestwalk
		Sint32 lineofsight;
		float damage;
		float fire_frequency;
		float busy;
		statistics *stats;
		walker  *collide_ob;
		walker * foe;
		walker * leader;
		walker * owner;                // for weapons
		walker * myself;
		guy  *myguy;                   // our special stats..
		short dead;                    // safety check
		char ignore;                   // for non-colliding objects
		unsigned short default_weapon;
		unsigned short current_weapon;
		signed char user;              // are we being used by anyone?
		short flight_left;             // for bonus flight ..
		short invulnerable_left;
		short invisibility_left;
		short charm_left;              // If we're still being charmed
		unsigned char drawcycle;
		char current_special;
		Sint32 lifetime; // how much life summoned guys have ..
		short skip_exit; // cycles after failed exit choice
		unsigned char outline;
		short speed_bonus;             // These two are used for
		short speed_bonus_left;        // speed potions, etc.
		Sint32 special_rest_left;      // port of the current game's 54f5890f: a special's rest between casts, ticks left
		Sint32 special_rest_slot;      // port of the current game's 54f5890f: the 1-based special that rests (0 = none)
		short regen_delay;  // Delay after being hit
		// Zardus: ADD: in_act should be set while in an action
		bool in_act;
		obmap* myobmap;
		// Built as a detached probe (the constructor's detached_probe): it has
		// no obmap, so setxy files it nowhere, and a loud flight of it is
		// offered no clash roll at an enemy weapon-order body that is not a
		// door (ob_pass_check). (port of the current game's 3d9dcdcd8, its
		// dormant test weapon)
		bool detached;
		int path_check_counter;
		std::vector<void*> path_to_foe;  // Result from pathfinding
		
		// TODO: Move this to screen class so it doesn't get overlapped by other walkers drawing
		class DamageNumber
		{
        public:
            float x, y;
            float t;
            float value;
            
            unsigned char color;
            
            DamageNumber(float x, float y, float value, unsigned char color);
            void draw(viewscreen* view_buf);
		};
		std::list<DamageNumber> damage_numbers;
		
		bool hurt_flash;
		float attack_lunge;
		float attack_lunge_angle;
		float hit_recoil;
		float hit_recoil_angle;
		
		float last_hitpoints;
		
		float get_current_angle();
        void do_heal_effects(walker* healer, walker* target, short amount);
        void do_hit_effects(walker* attacker, walker* target, short tempdamage);
        void do_combat_damage(walker* attacker, walker* target, short tempdamage);

	protected:
		short act_generate();
		short act_fire();
		short act_guard();
		virtual short act_random();
		char act_type,old_act_type;
		char enddir;  // Proposed direction facing
		char order, family;


};

// Gameplay Fixes BI, a door a bot cannot open is a wall to it (port of the
// current game's 708b27b91, og::reach::door_is_a_wall_to): who plans its
// route, takes its straight walk and is pushed by an ally with such a door
// as a wall: only with the setting on, and only a bot (walker::is_bot), a
// tower and a guard at its post included.
bool door_is_a_wall_to(walker *w);

// Gameplay Fixes BJ, a bot treats an enemy that only a locked door keeps it
// from as if it were not there (port of the current game's f97bc3ec7 and
// ec95086d1, og::reach). Who is asked at the sites of this rule: only with
// the setting on, only a bot (walker::is_bot), not a tower and not a guard at
// its post. Every other walker gets the classic answer at every site.
// (port of the current game's og::reach::asks)
bool asks(walker *w);

// Does w's shot land on b from where w stands? Never for a melee-only bot
// (BIT_NO_RANGED). b must be inside w's firing box (walker::in_firing_box)
// and within the reach of w's weapon read without the family's own part
// (unhooked_weapon: its step times its line of sight, against the Manhattan
// distance), and the line check's flight (line_check_flight) must pass for
// that detached weapon set on fire()'s pad for the facing toward b, headed
// that way with no waver, every live enemy door stopping it, w's own foe
// included. A weapon the loader cannot make answers yes (not judged). Draws
// nothing. (port of the current game's og::reach::shoots_at)
bool shoots_at(walker *w, walker *b);

// The door question of one walker (port of the current game's
// og::reach::Asker). This game keeps no map: the start cell and the two
// fills (walker::reach_fill, the doors shut and open) are made on first need
// and kept only while one question lasts (a search, a count, a let-go), in
// which no body moves. Nothing is drawn.
//
// door_shut_exit: whether only a door keeps the walker from b, and which test
// answered, in this order, each early exit answering "no door keeps it" (the
// classic answer): not asked (asks); no live door filed in the obmap bars it
// (door_bars, its own keys); b is no live living or generator; its start
// cell (walker::start_cell) does not stand with the doors shut; some
// approach cell of b (walker::approach_cells) stands with the doors shut in
// the fill from the start cell (it walks to b); none does with them open (a
// wall or water keeps it from b, not a door); its shot lands on b
// (shoots_at: b is kept by the shot). Otherwise only a door keeps it from b.
// shut_out: some live hostile living or generator (screen::foe_candidate) is
// door-shut for the walker, cloaked or not, at any distance.
class door_asker
{
	public:
		enum class Exit
		{
			NotAsked, NoBarringDoor, NotAFighter, NoRegion, WalksShut, NoWayOpen, Shot, DoorShut
		};
		explicit door_asker(walker *asker);
		// The same question asked of the walker holding `keys` instead of
		// its own (keys 0: the keyless walker of Gameplay Fixes BK; the
		// current game's Asker(w, 0)).
		door_asker(walker *asker, Uint32 keys);
		Exit door_shut_exit(walker *b);
		bool door_shut(walker *b);
		bool shut_out();
		// Does the start cell (walker::start_cell, worked out once with
		// these keys) stand with the doors shut (it lies in a region)?
		bool start();
		// Some approach cell of b standing in the mode is reached by the
		// fill from the start cell in that mode (the current game's
		// og::reach walks_to); start() first.
		bool walks_to(walker *b, bool shut);
	private:
		bool barred();
		walker *w;
		Uint32 keys;
		short barring;  // -1 not asked yet, 0 no barring door, 1 some
		bool started;
		bool start_stands;
		short sx, sy;
		bool filled[2];
		std::vector<char> reached[2];
};

// One question of a fresh door_asker each.
door_asker::Exit door_shut_exit(walker *w, walker *b);
bool door_shut(walker *w, walker *b);
bool shut_out(walker *w);

// Gameplay Fixes BK, a bot that holds the key to a door on its way walks its
// route into the door (port of the current game's 0b5647c36 and d61656373,
// og::reach::opens_way): does only a key w holds open its way to b? Each exit
// answers no, in this order: w is not asked (asks); w holds the key of no
// live door filed in the obmap, of any side (holds_key_of_level, the one key
// test); its start cell does not stand with the doors that bar it shut; it
// does not walk to b with those doors shut; a walker of its box, flags and
// side holding no key (door_asker with keys 0, from that walker's own start
// cell) would walk to b too. Otherwise yes. Draws nothing.
bool opens_way(walker *w, walker *b);

#endif
