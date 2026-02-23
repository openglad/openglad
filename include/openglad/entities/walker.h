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
//
// walker inherits from SimEntity (SDL-free) for position, size, identity,
// state, and animation frame tracking. Rendering data lives in an optional
// pixieN component (render_) created when graphics are available.

#include <openglad/sim/sim_entity.h>
#include <openglad/entities/obmap.h>
#include <cstdint>
#include <list>
#include <memory>
#include <string_view>
#include <vector>

// Forward declarations
class PixieData;
class WalkerRender;
class guy;
class statistics;

// Opaque state type used by MicroPather for pathfinding nodes.
// States are encoded grid coordinates (not real pointers), but the
// micropather library API requires void*.
using MicroPatherState = void*;

class walker : public og::sim::SimEntity
{
	public:
		walker(const PixieData& data);
		walker();  // Headless constructor (no rendering data)
		~walker() override;
		walker(const walker&) = delete;
		walker& operator=(const walker&) = delete;
		walker(walker&&) = delete;
		walker& operator=(walker&&) = delete;

		// Render component management
		void attach_render(const PixieData& data);
		void set_data(const PixieData& data);  // Update render graphics (for editor)
		bool has_render() const { return render_ != nullptr; }
		const unsigned char* bmp_data() const;
		WalkerRender* render_component() { return render_.get(); }
		const WalkerRender* render_component() const { return render_.get(); }

		// Animation frame management (sim state in SimEntity::frame/frames;
		// render bmp pointer updated via render component)
		short set_frame(short framenum);
		short query_frame() const { return frame; }
		short next_frame();

		void set_myguy_view(guy* guy_view);
		void set_owned_myguy(std::unique_ptr<guy> owned_guy);
		void clear_myguy();
		void move_myguy_to(walker* target);
		bool reset(void);
			short move(short x, short y);
			void worldmove(float x, float y);
			virtual bool setxy(short x, short y);
			// Overloads to avoid implicit narrowing at call sites. These forward to the virtual short-based API.
			bool setxy(std::int32_t x, std::int32_t y) { return setxy(static_cast<short>(x), static_cast<short>(y)); }
			bool setxy(std::uint32_t x, std::uint32_t y) { return setxy(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			bool setxy(float x, float y) { return setxy(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			void setworldxy(float x, float y);
			float worldx() const { return worldx_; }
			float worldy() const { return worldy_; }
			bool walk();
			bool walkstep(float x, float y);
			// Convenience overloads to avoid implicit int->float conversions at call sites.
			bool walkstep(std::int32_t x, std::int32_t y) { return walkstep(static_cast<float>(x), static_cast<float>(y)); }
			bool walkstep(short x, short y) { return walkstep(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			virtual bool walk(float x, float y);
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
		bool teleport_ranged(std::int32_t range);
		std::int32_t  turn_undead(std::int32_t range, std::int32_t power);
		virtual short shove(walker  *target, short x, short y);
		virtual bool eat_me(walker  *eater);
		virtual void set_direct_frame(short whichframe);
		bool turn(short targetdir);
		short spaces_clear(); // how many (of 8) spaces around us are clear
		void transfer_stats(walker  *newob); // transfer values to new walker
		void transform_to(Order whatorder, std::int32_t whatfamily); // change picture, etc.
		virtual bool death(); // called when death/destruction occurs ..
		void generate_bloodspot(); // make a permanent stain ..
			virtual walker* do_summon(char whatfamily, std::int32_t summon_lifetime);
		virtual bool check_special();
		void center_on(walker  *target);  // center us on target
		virtual void set_difficulty(std::uint32_t whatlevel);
			std::int32_t distance_to_ob(const walker * target) const;
			std::int32_t distance_to_ob_center(const walker * target) const;
			virtual short facing(short x, short y);
			// Convenience overloads to avoid implicit float->short conversions at call sites.
			// These forward to the virtual short-based implementations.
			short facing(std::int32_t x, std::int32_t y) { return facing(static_cast<short>(x), static_cast<short>(y)); }
			short facing(float x, float y) { return facing(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			short shove(walker* target, std::int32_t x, std::int32_t y) { return shove(target, static_cast<short>(x), static_cast<short>(y)); }
			short shove(walker* target, float x, float y) { return shove(target, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			unsigned char query_team_color() const;
		std::int32_t is_friendly(const walker *target) const;
		std::int32_t is_friendly_to_team(unsigned char team) const;
		inline short query_type(Order oval, char fval) const
		{
			if (oval == order && fval == family)
				return 1;
			else
				return 0;
		};


		// stats (unique_ptr ownership is protected; getter returns raw pointer)
		statistics* stats() const { return stats_.get(); }

		// TODO: Move this to screen class so it doesn't get overlapped by other walkers drawing
		class DamageNumber
		{
        public:
            float x, y;
            float t;
            float value;

            unsigned char color;

	            DamageNumber(float x_, float y_, float value_, unsigned char color_);
		};

		void compute_outline(const walker* viewer_control);
		float get_current_angle();
        void do_heal_effects(walker* healer, walker* target, short amount);
        void do_hit_effects(walker* attacker, walker* target, short tempdamage);
        void do_combat_damage(walker* attacker, walker* target, short tempdamage);

		// Public data members (fields NOT in SimEntity base)
		short skip_exit;               // cycles after failed exit choice
		guy  *myguy;                   // Non-owning view of character data; ownership, when present, lives in owned_myguy_
		walker *foe;
		walker *leader;
		walker *owner;                 // for weapons
		std::uint32_t keys;                   // used to open doors
		short view_all;                // used for seeing treasures, etc. on radar
		short weapons_left;            // for fighter's blades
		float lastx, lasty;
		signed char cycle;
		char action;                   // no special action mode
		char ani_type;
		float stepsize;
		float normal_stepsize;         // used for elven forestwalk
		std::int32_t lineofsight;
		float damage;
		float fire_frequency;
		float busy;
		char ignore;                   // for non-colliding objects
		unsigned short current_weapon;
		std::int32_t lifetime;               // how much life summoned guys have ..
			float speed_bonus;             // Additional stepsize while speed potions are active
			std::int32_t speed_bonus_left;        // Cycles remaining for speed bonus
		walker* collide_ob;
		unsigned short default_weapon;
		signed char curdir;            // Current direction facing
		signed char** ani;
		unsigned char drawcycle;
		char current_special;
		unsigned char outline;
		short shifter_down;            // is our shifter/alternate key pressed?
		short yo_delay;
		bool in_act;                   // set while in an action
		obmap* myobmap;
		int path_check_counter;
		std::vector<MicroPatherState> path_to_foe;  // Result from pathfinding
		bool hurt_flash;
		float attack_lunge;
		float attack_lunge_angle;
		float hit_recoil;
		float hit_recoil_angle;
		float last_hitpoints;
		std::list<DamageNumber> damage_numbers;
		char enddir;                   // Proposed direction facing

		// Accessors for protected fields used by family callbacks
		void set_charm_left(short value) { charm_left_ = value; }
		short charm_left() const { return charm_left_; }

	protected:
		bool act_generate();
		bool act_fire();
		bool act_guard();
		virtual bool act_random();
		char act_type,old_act_type;
		short charm_left_;             // If we're still being charmed
			std::int32_t regen_delay_;           // Delay after being hit
		walker * myself_;
		std::unique_ptr<statistics> stats_;
		std::unique_ptr<guy> owned_myguy_;
		std::unique_ptr<WalkerRender> render_;  // Optional render component (null for headless)
};

// Returns the best display name for an entity: myguy name if available,
// then stats name, then the provided fallback.
std::string_view entity_display_name(const walker* w, std::string_view fallback = "");
