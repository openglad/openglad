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

// Definition of WALKER class

#include "base.h"
#include "pixien.h"
#include "obmap.h"
#include <memory>

// Opaque state type used by MicroPather for pathfinding nodes.
// States are encoded grid coordinates (not real pointers), but the
// micropather library API requires void*.
using MicroPatherState = void*;

class walker : public pixieN
{
	public:
		walker(const PixieData& data);
		~walker() override;
		walker(const walker&) = delete;
		walker& operator=(const walker&) = delete;
		walker(walker&&) = delete;
		walker& operator=(walker&&) = delete;
		bool reset(void);
		short move(short x, short y);
		void worldmove(float x, float y);
		virtual bool setxy(short x, short y);
		void setworldxy(float x, float y);
		bool walk();
		bool walkstep(float x, float y);
		virtual bool walk(float x, float y);
		bool draw(viewscreen  *view_buf);
		bool draw_tile(viewscreen  *view_buf);
		void draw_path(viewscreen* view_buf);
		void find_path_to_foe();
		void follow_path_to_foe();
		bool init_fire();
		bool init_fire(short xdir, short ydir);
		void set_weapon_heading(walker *weapon);
		walker  * fire();
		virtual bool act();
		short set_act_type(short num);
		short restore_act_type();
		short query_act_type() const;
		short set_old_act_type(short num);
		short query_old_act_type() const;
		virtual bool collide(walker  *ob);
		bool attack(walker  *target);
		virtual bool animate();
		bool set_order_family(Order order, char family);
		virtual Order query_order() const
		{
			return order;
		}
		char query_family() const
		{
			return family;
		}
		walker  *create_weapon();
		bool fire_check(short xdelta, short ydelta);
		bool query_next_to();
		bool special();
		bool teleport();
		bool teleport_ranged(Sint32 range);
		Sint32  turn_undead(Sint32 range, Sint32 power);
		virtual short shove(walker  *target, short x, short y);
		virtual bool eat_me(walker  *eater);
		virtual void set_direct_frame(short whichframe);
		bool turn(short targetdir);
		short spaces_clear(); // how many (of 8) spaces around us are clear
		void transfer_stats(walker  *newob); // transfer values to new walker
		void transform_to(Order whatorder, char whatfamily); // change picture, etc.
		virtual bool death(); // called when death/destruction occurs ..
		void generate_bloodspot(); // make a permanent stain ..
		virtual walker  *do_summon(char whatfamily, unsigned short lifetime);
		virtual bool check_special();
		void center_on(walker  *target);  // center us on target
		virtual void set_difficulty(Uint32 whatlevel);
		Sint32 distance_to_ob(const walker * target) const;
		Sint32 distance_to_ob_center(const walker * target) const;
		virtual short facing(short x, short y);
		unsigned char query_team_color() const;
		Sint32 is_friendly(const walker *target) const;
		Sint32 is_friendly_to_team(unsigned char team) const;
		inline short query_type(Order oval, char fval) const
		{
			if (oval == order && fval == family)
				return 1;
			else
				return 0;
		};


		// --- Encapsulated field accessors ---
		// stats (unique_ptr ownership is protected; getter returns raw pointer)
		statistics* stats() const { return stats_.get(); }
		// shifter_down
		short shifter_down() const { return shifter_down_; }
		void set_shifter_down(short s) { shifter_down_ = s; }
		// collide_ob
		walker* collide_ob() const { return collide_ob_; }
		void set_collide_ob(walker* c) { collide_ob_ = c; }
		// default_weapon
		unsigned short default_weapon() const { return default_weapon_; }
		void set_default_weapon(unsigned short w) { default_weapon_ = w; }
		// user
		signed char user() const { return user_; }
		void set_user(signed char u) { user_ = u; }
		// curdir
		signed char curdir() const { return curdir_; }
		void set_curdir(signed char d) { curdir_ = d; }
		// ani
		signed char** ani() const { return ani_; }
		void set_ani(signed char** a) { ani_ = a; }
		// drawcycle
		unsigned char drawcycle() const { return drawcycle_; }
		void set_drawcycle(unsigned char d) { drawcycle_ = d; }
		// current_special
		char current_special() const { return current_special_; }
		void set_current_special(char s) { current_special_ = s; }
		// outline
		unsigned char outline() const { return outline_; }
		void set_outline(unsigned char o) { outline_ = o; }
		// yo_delay
		short yo_delay() const { return yo_delay_; }
		void set_yo_delay(short y) { yo_delay_ = y; }
		// in_act
		bool in_act() const { return in_act_; }
		void set_in_act(bool a) { in_act_ = a; }
		// myobmap
		obmap* myobmap() const { return myobmap_; }
		void set_myobmap(obmap* m) { myobmap_ = m; }
		// path_check_counter
		int path_check_counter() const { return path_check_counter_; }
		void set_path_check_counter(int c) { path_check_counter_ = c; }
		// path_to_foe
		std::vector<MicroPatherState>& path_to_foe() { return path_to_foe_; }
		const std::vector<MicroPatherState>& path_to_foe() const { return path_to_foe_; }
		// hurt_flash
		bool hurt_flash() const { return hurt_flash_; }
		void set_hurt_flash(bool h) { hurt_flash_ = h; }
		// attack_lunge
		float attack_lunge() const { return attack_lunge_; }
		void set_attack_lunge(float v) { attack_lunge_ = v; }
		// attack_lunge_angle
		float attack_lunge_angle() const { return attack_lunge_angle_; }
		void set_attack_lunge_angle(float v) { attack_lunge_angle_ = v; }
		// hit_recoil
		float hit_recoil() const { return hit_recoil_; }
		void set_hit_recoil(float v) { hit_recoil_ = v; }
		// hit_recoil_angle
		float hit_recoil_angle() const { return hit_recoil_angle_; }
		void set_hit_recoil_angle(float v) { hit_recoil_angle_ = v; }
		// last_hitpoints
		float last_hitpoints() const { return last_hitpoints_; }
		void set_last_hitpoints(float v) { last_hitpoints_ = v; }
		// team_num / real_team_num
		unsigned char team_num() const { return team_num_; }
		void set_team_num(unsigned char t) { team_num_ = t; }
		unsigned char real_team_num() const { return real_team_num_; }
		void set_real_team_num(unsigned char t) { real_team_num_ = t; }
		// dead
		bool is_dead() const { return dead_ != 0; }
		void set_dead(short d) { dead_ = d; }
		// skip_exit
		short skip_exit() const { return skip_exit_; }
		void set_skip_exit(short s) { skip_exit_ = s; }
		// bonus_rounds
		short bonus_rounds() const { return bonus_rounds_; }
		void set_bonus_rounds(short b) { bonus_rounds_ = b; }
		// death_called
		short death_called() const { return death_called_; }
		void set_death_called(short d) { death_called_ = d; }
		// invulnerable_left
		short invulnerable_left() const { return invulnerable_left_; }
		void set_invulnerable_left(short v) { invulnerable_left_ = v; }
		// myguy
		guy* myguy() const { return myguy_; }
		void set_myguy(guy* g) { myguy_ = g; }
		// combat pointers
		walker* foe() const { return foe_; }
		void set_foe(walker* f) { foe_ = f; }
		walker* leader() const { return leader_; }
		void set_leader(walker* l) { leader_ = l; }
		walker* owner() const { return owner_; }
		void set_owner(walker* o) { owner_ = o; }
		// Group 5 accessors
		Uint32 keys() const { return keys_; }
		void set_keys(Uint32 k) { keys_ = k; }
		short view_all() const { return view_all_; }
		void set_view_all(short v) { view_all_ = v; }
		short weapons_left() const { return weapons_left_; }
		void set_weapons_left(short w) { weapons_left_ = w; }
		float lastx() const { return lastx_; }
		void set_lastx(float v) { lastx_ = v; }
		float lasty() const { return lasty_; }
		void set_lasty(float v) { lasty_ = v; }
		signed char cycle() const { return cycle_; }
		void set_cycle(signed char c) { cycle_ = c; }
		char action() const { return action_; }
		void set_action(char a) { action_ = a; }
		char ani_type() const { return ani_type_; }
		void set_ani_type(char a) { ani_type_ = a; }
		float stepsize() const { return stepsize_; }
		void set_stepsize(float s) { stepsize_ = s; }
		float normal_stepsize() const { return normal_stepsize_; }
		void set_normal_stepsize(float s) { normal_stepsize_ = s; }
		Sint32 lineofsight() const { return lineofsight_; }
		void set_lineofsight(Sint32 l) { lineofsight_ = l; }
		float damage() const { return damage_; }
		void set_damage(float d) { damage_ = d; }
		float fire_frequency() const { return fire_frequency_; }
		void set_fire_frequency(float f) { fire_frequency_ = f; }
		float busy() const { return busy_; }
		void set_busy(float b) { busy_ = b; }
		char ignore() const { return ignore_; }
		void set_ignore(char i) { ignore_ = i; }
		unsigned short current_weapon() const { return current_weapon_; }
		void set_current_weapon(unsigned short w) { current_weapon_ = w; }
		short flight_left() const { return flight_left_; }
		void set_flight_left(short f) { flight_left_ = f; }
		short invisibility_left() const { return invisibility_left_; }
		void set_invisibility_left(short v) { invisibility_left_ = v; }
		Sint32 lifetime() const { return lifetime_; }
		void set_lifetime(Sint32 l) { lifetime_ = l; }
		short speed_bonus() const { return speed_bonus_; }
		void set_speed_bonus(short s) { speed_bonus_ = s; }
		short speed_bonus_left() const { return speed_bonus_left_; }
		void set_speed_bonus_left(short s) { speed_bonus_left_ = s; }
		
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
		// damage_numbers
		std::list<DamageNumber>& damage_numbers() { return damage_numbers_; }
		const std::list<DamageNumber>& damage_numbers() const { return damage_numbers_; }

		float get_current_angle();
        void do_heal_effects(walker* healer, walker* target, short amount);
        void do_hit_effects(walker* attacker, walker* target, short tempdamage);
        void do_combat_damage(walker* attacker, walker* target, short tempdamage);

		char get_enddir() const { return enddir; }
		void set_enddir(char dir) { enddir = dir; }

	protected:
		bool act_generate();
		bool act_fire();
		bool act_guard();
		virtual bool act_random();
		char act_type,old_act_type;
		char enddir;  // Proposed direction facing
		Order order;
		char family;
		// Zardus: FIX: lets make these unsigned so that real_team_num doesn't wrap around from 255 to -1 :-)
		unsigned char team_num_;
		unsigned char real_team_num_; // for 'Charm', etc.
		short bonus_rounds_; // used if an object has extra rounds this cycle
		short dead_;                   // safety check
		short death_called_;           // if death has already been called
		short skip_exit_; // cycles after failed exit choice
		short invulnerable_left_;
		short charm_left_;             // If we're still being charmed
		short regen_delay_;            // Delay after being hit
		float worldx_, worldy_;        // Floating point buffer for movement
		walker * myself_;
		guy  *myguy_;                  // Non-owning — owned by SaveData::team_list
		walker * foe_;
		walker * leader_;
		walker * owner_;               // for weapons
		// Group 5 fields
		Uint32 keys_;                  // used to open doors
		short view_all_;               // used for seeing treasures, etc. on radar
		short weapons_left_;           // for fighter's blades
		float lastx_, lasty_;
		signed char cycle_;
		char action_;                  // no special action mode
		char ani_type_;
		float stepsize_;
		float normal_stepsize_;        // used for elven forestwalk
		Sint32 lineofsight_;
		float damage_;
		float fire_frequency_;
		float busy_;
		char ignore_;                  // for non-colliding objects
		unsigned short current_weapon_;
		short flight_left_;            // for bonus flight ..
		short invisibility_left_;
		Sint32 lifetime_;              // how much life summoned guys have ..
		short speed_bonus_;            // These two are used for
		short speed_bonus_left_;       // speed potions, etc.
		// Remaining encapsulated fields
		std::unique_ptr<statistics> stats_;
		walker* collide_ob_;
		unsigned short default_weapon_;
		signed char user_;             // are we being used by anyone?
		signed char curdir_;           // Current direction facing
		signed char** ani_;
		unsigned char drawcycle_;
		char current_special_;
		unsigned char outline_;
		short shifter_down_;           // is our shifter/alternate key pressed?
		short yo_delay_;
		bool in_act_;                  // set while in an action
		obmap* myobmap_;
		int path_check_counter_;
		std::vector<MicroPatherState> path_to_foe_;  // Result from pathfinding
		bool hurt_flash_;
		float attack_lunge_;
		float attack_lunge_angle_;
		float hit_recoil_;
		float hit_recoil_angle_;
		float last_hitpoints_;
		std::list<DamageNumber> damage_numbers_;


};

