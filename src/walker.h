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
		walker(const PixieData& data);
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
		short init_fire();
		short init_fire(short xdir, short ydir);
		void set_weapon_heading(walker *weapon);
		walker  * fire();
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
		short fire_check(short xdelta, short ydelta);
		short query_next_to();
		short special();
		short teleport();
		short teleport_ranged(Sint32 range);
		Sint32  turn_undead(Sint32 range, Sint32 power);
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
		short regen_delay;  // Delay after being hit
		// Zardus: ADD: in_act should be set while in an action
		bool in_act;
		obmap* myobmap;
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

	protected:
		short act_generate();
		short act_fire();
		short act_guard();
		virtual short act_random();
		char act_type,old_act_type;
		char enddir;  // Proposed direction facing
		char order, family;


};

#endif
