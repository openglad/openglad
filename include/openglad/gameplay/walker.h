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

#include <openglad/gameplay/sim_entity.h>
#include <openglad/gameplay/gameplay_context.h>
#include <cstdint>
#include <list>
#include <memory>
#include <string_view>
#include <vector>

// Forward declarations
class PixieData;
class guy;
class statistics;
namespace og::gameplay {
class IRenderComponent;
}

// Opaque state type for pathfinding nodes. States are encoded grid coordinates
// (not real pointers); the A* solver treats them as opaque handles and never
// dereferences them. See <openglad/gameplay/astar.h>.
using PathState = void*;

class walker : public og::sim::SimEntity
{
	public:
#define OG_WALKER_DIRTY_FIELD(type, name, bit)                           \
    [[nodiscard]] type name() const noexcept { return name##_; }         \
    void set_##name(type value)                                          \
    {                                                                    \
        name##_ = value;                                                 \
        mark_dirty(bit);                                                 \
    }

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
		og::gameplay::IRenderComponent* render_component() { return render_.get(); }
		const og::gameplay::IRenderComponent* render_component() const { return render_.get(); }

		// Animation frame management (sim state in SimEntity::frame/frames;
		// render bmp pointer updated via render component)
		short set_frame(short framenum);
		short next_frame();

		void set_myguy_view(guy* guy_view);
		void set_owned_myguy(std::unique_ptr<guy> owned_guy);
		void clear_myguy();
		void move_myguy_to(walker* target);
		void set_foe(walker* target);
		walker* foe() const { return foe_; }
		void set_leader(walker* target);
		walker* leader() const { return leader_; }
		void set_owner(walker* target);
		walker* owner() const { return owner_; }
		void set_collide_ob(walker* target);
		walker* collide_ob() const { return collide_ob_; }
		void sync_ids_from_pointers();
		bool reset(void);
			short move(short x, short y);
			void worldmove(float x, float y);
			// Z-axis / multi-floor (no-ops / cheap on single-floor levels):
			void change_floor(short new_floor); // relocate to a stacked floor + re-bucket in obmap
			void apply_z_motion();              // per-tick fall-through-air / Z-stair transition
#ifdef TESTING
			// Test-only oracle for the fall-damage accumulator (fall_stories_
			// below); compiled out of production builds.
			int fall_stories_for_test() const { return fall_stories_; }
#endif
			virtual bool setxy(short x, short y);
			// Overloads to avoid implicit narrowing at call sites. These forward to the virtual short-based API.
			bool setxy(std::int32_t x, std::int32_t y) { return setxy(static_cast<short>(x), static_cast<short>(y)); }
			bool setxy(std::uint32_t x, std::uint32_t y) { return setxy(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			bool setxy(float x, float y) { return setxy(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			void setworldxy(float x, float y);
			float worldx() const { return SimEntity::worldx(); }
			float worldy() const { return SimEntity::worldy(); }
			bool walk();
			bool walkstep(float x, float y);
			// Convenience overloads to avoid implicit int->float conversions at call sites.
			bool walkstep(std::int32_t x, std::int32_t y) { return walkstep(static_cast<float>(x), static_cast<float>(y)); }
			bool walkstep(short x, short y) { return walkstep(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)); }
			virtual bool walk(float x, float y);
		void find_path_to_foe();
		void find_path_to_point(short x, short y);
		void follow_path_to_foe();
		bool init_fire();
		bool init_fire(short xdir, short ydir);
		void set_weapon_heading(walker *weapon);
		walker  * fire();
		virtual bool act();
		short set_act_type(short num);
		short restore_act_type();
		virtual bool collide(walker  *ob);
		bool attack(walker  *target);
		virtual bool animate();
		bool set_frame_from_current_walk_animation();
		bool set_order_family(Order order, char family);
		virtual Order query_order() const
		{
			return order();
		}
		walker  *create_weapon();
		// Why a fire_check() attempt was denied. Callers that care (the
		// COMMAND_ATTACK melee loop) distinguish the orientation denials —
		// Facing, and NoRanged at bump range ("the foe is in weapon reach
		// but we are pointed the wrong way / can only hit by facing") —
		// from every other denial, because the classic response to ANY
		// denial (walk toward the foe, sliding perpendicular when blocked)
		// deadlocks two adjacent fighters forever. The reach gate runs
		// before the NoRanged/NoMagic gates so those two denials imply the
		// foe is within reach. See docs/GAMEPLAY_FIXES_FROM_CLASSIC.md
		// (guard-standoff melee deadlock, 2026-07-07).
		enum class FireCheckDenial : std::uint8_t
		{
			None,        // check passed
			NoFoe,       // nothing to fire at
			NoRanged,    // BIT_NO_RANGED family (melee-only)
			NoMagic,     // weapon costs more magic than we have
			OutOfRange,  // foe beyond weapon stepsize*lineofsight
			Facing,      // in reach, but curdir is not toward the foe
			WallBlocked, // the shot ray hits terrain first
			RayMiss,     // the shot ray ran full range without a hit
		};
		bool fire_check(short xdelta, short ydelta,
		                FireCheckDenial* denial = nullptr);
		// Snap-face a (foe) direction: sets curdir AND enddir (so the
		// act() pre-turn doesn't fight it) AND lastx/lasty (the thrown-
		// weapon heading that set_weapon_heading() reads).
		void face_delta(short xdelta, short ydelta);
		bool query_next_to();
		bool special();
		bool teleport();
		bool teleport_ranged(std::int32_t range);
		std::int32_t  turn_undead(std::int32_t range, std::int32_t power);
		virtual short shove(walker  *target, short x, short y);
		virtual bool eat_me(walker  *eater);
		// #160 exit-pad re-trigger latch (full contract on exit_latched_ in
		// the private section). latch_exit_contact is called by ob_pass_check
		// on the first movement probe that eats an exit pad; while latched,
		// further exit-pad eats are skipped. update_exit_latch runs every
		// living::act tick and clears the latch once this walker's bbox has
		// fully left the latched pad's rect.
		void latch_exit_contact(const walker* pad);
		[[nodiscard]] bool exit_latched() const noexcept { return exit_latched_; }
		void update_exit_latch();
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


		// stats (unique_ptr ownership is protected; getter returns raw pointer)
		statistics* stats() const { return stats_.get(); }
		OG_WALKER_DIRTY_FIELD(float, lastx, og::dirty::BIT_LASTX);
		OG_WALKER_DIRTY_FIELD(float, lasty, og::dirty::BIT_LASTY);
		// Vertical velocity for projectile arcs/gravity (pixels/tick). Only
		// projectiles use it; 0 for everything else (legacy flat behavior).
		OG_WALKER_DIRTY_FIELD(float, vz, og::dirty::BIT_VZ);
		OG_WALKER_DIRTY_FIELD(float, stepsize, og::dirty::BIT_STEPSIZE);
		// used for elven forestwalk
		OG_WALKER_DIRTY_FIELD(float, normal_stepsize, og::dirty::BIT_NORMAL_STEPSIZE);
		// Current direction facing
		OG_WALKER_DIRTY_FIELD(signed char, curdir, og::dirty::BIT_CURDIR);
		// Proposed direction facing
		OG_WALKER_DIRTY_FIELD(char, enddir, og::dirty::BIT_ENDDIR);
		OG_WALKER_DIRTY_FIELD(float, damage, og::dirty::BIT_DAMAGE);
		OG_WALKER_DIRTY_FIELD(float, fire_frequency, og::dirty::BIT_FIRE_FREQUENCY);
		OG_WALKER_DIRTY_FIELD(float, busy, og::dirty::BIT_BUSY);
		OG_WALKER_DIRTY_FIELD(unsigned short, current_weapon, og::dirty::BIT_CURRENT_WEAPON);
		OG_WALKER_DIRTY_FIELD(unsigned short, default_weapon, og::dirty::BIT_DEFAULT_WEAPON);
		OG_WALKER_DIRTY_FIELD(float, attack_lunge, og::dirty::BIT_ATTACK_LUNGE);
		OG_WALKER_DIRTY_FIELD(float, attack_lunge_angle, og::dirty::BIT_ATTACK_LUNGE_ANGLE);
		OG_WALKER_DIRTY_FIELD(float, hit_recoil, og::dirty::BIT_HIT_RECOIL);
		OG_WALKER_DIRTY_FIELD(float, hit_recoil_angle, og::dirty::BIT_HIT_RECOIL_ANGLE);
		OG_WALKER_DIRTY_FIELD(float, last_hitpoints, og::dirty::BIT_LAST_HITPOINTS);
		OG_WALKER_DIRTY_FIELD(char, action, og::dirty::BIT_ACTION);
		[[nodiscard]] char act_type() const noexcept { return act_type_; }
		void set_act_type_state(char value)
		{
			act_type_ = value;
			mark_dirty(og::dirty::BIT_ACT_TYPE);
		}
		OG_WALKER_DIRTY_FIELD(char, old_act_type, og::dirty::BIT_OLD_ACT_TYPE);
		OG_WALKER_DIRTY_FIELD(char, ani_type, og::dirty::BIT_ANI_TYPE);
		OG_WALKER_DIRTY_FIELD(signed char, cycle, og::dirty::BIT_CYCLE);
		OG_WALKER_DIRTY_FIELD(unsigned char, drawcycle, og::dirty::BIT_DRAWCYCLE);
		OG_WALKER_DIRTY_FIELD(char, current_special, og::dirty::BIT_CURRENT_SPECIAL);
		// for non-colliding objects
		OG_WALKER_DIRTY_FIELD(char, ignore, og::dirty::BIT_IGNORE);
		// Zardus: ADD: in_act should be set while in an action
		OG_WALKER_DIRTY_FIELD(bool, in_act, og::dirty::BIT_IN_ACT);
		// is our shifter/alternate key pressed?
		OG_WALKER_DIRTY_FIELD(short, shifter_down, og::dirty::BIT_SHIFTER_DOWN);
		OG_WALKER_DIRTY_FIELD(short, yo_delay, og::dirty::BIT_YO_DELAY);
		// cycles after failed exit choice
		OG_WALKER_DIRTY_FIELD(short, skip_exit, og::dirty::BIT_SKIP_EXIT);
		OG_WALKER_DIRTY_FIELD(unsigned char, outline, og::dirty::BIT_OUTLINE);
		OG_WALKER_DIRTY_FIELD(bool, hurt_flash, og::dirty::BIT_HURT_FLASH);
		// how much life summoned guys have ..
		OG_WALKER_DIRTY_FIELD(std::int32_t, lifetime, og::dirty::BIT_LIFETIME);
		// These two are used for
		// speed potions, etc.
		OG_WALKER_DIRTY_FIELD(float, speed_bonus, og::dirty::BIT_SPEED_BONUS);
		OG_WALKER_DIRTY_FIELD(std::int32_t, speed_bonus_left, og::dirty::BIT_SPEED_BONUS_LEFT);
		// If we're still being charmed
		OG_WALKER_DIRTY_FIELD(short, charm_left, og::dirty::BIT_CHARM_LEFT);
		// for fighter's blades
		OG_WALKER_DIRTY_FIELD(short, weapons_left, og::dirty::BIT_WEAPONS_LEFT);
		// used to open doors
		OG_WALKER_DIRTY_FIELD(std::uint32_t, keys, og::dirty::BIT_KEYS);
		// used for seeing treasures, etc. on radar
		OG_WALKER_DIRTY_FIELD(short, view_all, og::dirty::BIT_VIEW_ALL);
		OG_WALKER_DIRTY_FIELD(std::int32_t, lineofsight, og::dirty::BIT_LINEOFSIGHT);
		OG_WALKER_DIRTY_FIELD(int, path_check_counter, og::dirty::BIT_PATH_CHECK_COUNTER);
		OG_WALKER_DIRTY_FIELD(std::uint32_t, foe_id, og::dirty::BIT_FOE_ID);
		OG_WALKER_DIRTY_FIELD(std::uint32_t, leader_id, og::dirty::BIT_LEADER_ID);
		OG_WALKER_DIRTY_FIELD(std::uint32_t, owner_id, og::dirty::BIT_OWNER_ID);
		OG_WALKER_DIRTY_FIELD(std::uint32_t, collide_ob_id, og::dirty::BIT_COLLIDE_OB_ID);
		// Level-entry spawn point: where this walker was deployed at level
		// start (marker, teleport scatter, or authored placement). Recorded
		// once at the deploy sites; the classic respawn engine revives
		// eligible walkers here. -1/-1 = never recorded (legacy default).
		OG_WALKER_DIRTY_FIELD(std::int16_t, spawn_x, og::dirty::BIT_SPAWN_X);
		OG_WALKER_DIRTY_FIELD(std::int16_t, spawn_y, og::dirty::BIT_SPAWN_Y);
		OG_WALKER_DIRTY_FIELD(std::uint8_t, spawn_floor, og::dirty::BIT_SPAWN_FLOOR);
		void set_spawn_point(short x, short y, std::uint8_t floor)
		{
			set_spawn_x(static_cast<std::int16_t>(x));
			set_spawn_y(static_cast<std::int16_t>(y));
			set_spawn_floor(floor);
		}
		std::int32_t regen_delay() const { return regen_delay_; }
		void set_regen_delay(std::int32_t value)
		{
			regen_delay_ = value;
			mark_dirty(og::dirty::BIT_REGEN_DELAY);
		}
		// Per-placed-NPC scenario extras (level file v10, reserved[3..5]).
		// Authoritative-side sim state only: mirrors never simulate, so these
		// are never replicated (no dirty bit, not in EntitySnapshot).
		//
		// specials_disabled: the walker never uses its family special — both
		// the direct walker::special() execution path and the AI's
		// living::check_special() decision are gated on it.
		[[nodiscard]] bool specials_disabled() const noexcept
		{
			return specials_disabled_;
		}
		void set_specials_disabled(bool value) noexcept
		{
			specials_disabled_ = value;
		}
		// spawn_delay: sim ticks past level start before a placed walker
		// enters the world. While the delay has not elapsed the walker is
		// dormant (see dormant()).
		[[nodiscard]] std::uint16_t spawn_delay() const noexcept
		{
			return spawn_delay_;
		}
		void set_spawn_delay(std::uint16_t value) noexcept
		{
			spawn_delay_ = value;
		}
		// save_all_protected: npc_flags bit 2 ("protected") from the level
		// file. When ANY placed walker on the level carries it, the
		// SCEN_TYPE_SAVE_ALL death check watches ONLY flagged walkers (the
		// mission-critical cast); when none does, the legacy rule applies
		// (any named team-0 living). Authoritative-side sim state only, like
		// the other per-placed-NPC extras above.
		[[nodiscard]] bool save_all_protected() const noexcept
		{
			return save_all_protected_;
		}
		void set_save_all_protected(bool value) noexcept
		{
			save_all_protected_ = value;
		}
		// guard_hold_post: npc_flags bit 1 ("hold post") from the level file.
		// Selects the per-guard wake policy: an ACT_GUARD living normally
		// converts to ACT_RANDOM pursuit the first time a foe is inside its
		// sight range with a clear sight line (walker::act_guard); a
		// hold-post guard never converts — it keeps the classic stationary
		// sentry behavior. Meaningless for non-guards. Authoritative-side
		// sim state only, like the other per-placed-NPC extras above.
		[[nodiscard]] bool guard_hold_post() const noexcept
		{
			return guard_hold_post_;
		}
		void set_guard_hold_post(bool value) noexcept
		{
			guard_hold_post_ = value;
		}
		// summoned: a runtime-conjured living (archmage illusion/elemental,
		// cleric raise — living::do_summon and friends). Ammunition, not a
		// character: never a SCEN_TYPE_SAVE_ALL mission loss, no matter how
		// it is named or teamed. Sticky for the walker's whole life — the
		// owner() link is severed when the summoner dies, so the exemption
		// cannot ride on it.
		[[nodiscard]] bool summoned() const noexcept { return summoned_; }
		void set_summoned(bool value) noexcept { summoned_ = value; }
		// dormant: a delayed-spawn walker that has not activated yet. It does
		// not act, is not drawn, sits outside the obmap (uncollidable and
		// untargetable), and is excluded from snapshot capture — but its team
		// still counts as alive for level-completion checks. set_dormant
		// maintains obmap membership (see walker.cpp); GameWorld::tick wakes
		// it once the level tick counter passes spawn_delay().
		[[nodiscard]] bool dormant() const noexcept { return dormant_; }
		void set_dormant(bool value);
		// Server-only transient (like path_to_foe): the world tick on which
		// this walker last began a SELF-teleport — walker::teleport /
		// teleport_ranged, i.e. spell blinks and marker beacons; map
		// teleporter pads never set it. Stamped the moment the teleport
		// begins (before any destination probing) and consumed by the CTF
		// phase of the same tick; always stale at tick boundaries, so
		// snapshots and replays are unaffected. Never replicated: no dirty
		// bit, not in EntitySnapshot.
		[[nodiscard]] std::uint32_t last_self_teleport_tick() const noexcept
		{
			return last_self_teleport_tick_;
		}
		void set_last_self_teleport_tick(std::uint32_t tick) noexcept
		{
			last_self_teleport_tick_ = tick;
		}

		// TODO: Move this to screen class so it doesn't get overlapped by other walkers drawing
		class DamageNumber
		{
        public:
            float x, y;
            float t;
            float value;
            std::uint32_t created_tick = 0u; // Simulation tick when this number was spawned.

            unsigned char color;

	            DamageNumber(float x_,
                         float y_,
                         float value_,
                         unsigned char color_,
                         std::uint32_t created_tick_ = 0u);
		};

		// mark_player_controls: when true, same-team OTHER player-controlled
		// characters get a team-color outline (a "this is a human-controlled
		// peer" marker). Only meaningful in genuine networked play; local
		// split-screen passes false so co-players aren't outlined (each already
		// has their own pane) — see walker_draw.cpp.
		void compute_outline(const walker* viewer_control,
		                     bool mark_player_controls = false);
		float get_current_angle();
        void do_heal_effects(walker* healer, walker* target, short amount);
        void do_hit_effects(walker* attacker, walker* target, short tempdamage);
        void do_combat_damage(walker* attacker, walker* target, short tempdamage);

		// Public data members (fields NOT in SimEntity base)
		guy  *myguy;                   // Non-owning view of character data; ownership, when present, lives in owned_myguy_
		const signed char * const * ani;
		// Number of (facing x ani_type) entries in `ani` for this family's table.
		// Animation tables vary in length per family; this bounds index math in
		// animate() so a snapshot/save-controlled ani_type/curdir cannot read past
		// the end of a short table. Set by loader::set_walker; 0 until then.
		int ani_count = 0;
		std::vector<PathState> path_to_foe;  // Result from pathfinding
		std::list<DamageNumber> damage_numbers;

	protected:
		bool act_generate();
		bool act_fire();
		bool act_guard();
		virtual bool act_random();
		std::int32_t regen_delay_ = 0;       // Delay after being hit
		walker * myself_ = nullptr;
		std::unique_ptr<statistics> stats_;
		std::unique_ptr<guy> owned_myguy_;
		std::unique_ptr<og::gameplay::IRenderComponent> render_;  // Optional render component (null for headless)

	private:
		float lastx_ = 0.0f;
		float lasty_ = 0.0f;
		float vz_ = 0.0f;
		float stepsize_ = 0.0f;
		float normal_stepsize_ = 0.0f;
		signed char curdir_ = 0;
		char enddir_ = 0;
		float damage_ = 0.0f;
		float fire_frequency_ = 0.0f;
		float busy_ = 0.0f;
		unsigned short current_weapon_ = 0;
		unsigned short default_weapon_ = 0;
		float attack_lunge_ = 0.0f;
		float attack_lunge_angle_ = 0.0f;
		float hit_recoil_ = 0.0f;
		float hit_recoil_angle_ = 0.0f;
		float last_hitpoints_ = 0.0f;
		char action_ = 0;
		char act_type_ = 0;
		char old_act_type_ = 0;
		char ani_type_ = 0;
		signed char cycle_ = 0;
		unsigned char drawcycle_ = 0;
		char current_special_ = 0;
		char ignore_ = 0;
		bool in_act_ = false;
		short shifter_down_ = 0;
		short yo_delay_ = 0;
		short skip_exit_ = 0;
		unsigned char outline_ = 0;
		bool hurt_flash_ = false;
		std::int32_t lifetime_ = 0;
		float speed_bonus_ = 0.0f;
		std::int32_t speed_bonus_left_ = 0;
		short charm_left_ = 0;
		short weapons_left_ = 0;
		std::uint32_t keys_ = 0;
		short view_all_ = 0;
		std::int32_t lineofsight_ = 0;
		int path_check_counter_ = 0;
		std::uint32_t foe_id_ = 0;
		std::uint32_t leader_id_ = 0;
		std::uint32_t owner_id_ = 0;
		std::uint32_t collide_ob_id_ = 0;
		std::int16_t spawn_x_ = -1;
		std::int16_t spawn_y_ = -1;
		std::uint8_t spawn_floor_ = 0;
		std::uint32_t last_self_teleport_tick_ = 0;
		// Server-only transient (not replicated, like path_to_foe): ticks until
		// the next Z transition is allowed, throttling fall/denial re-probes.
		// Always 0 on single-floor (apply_z_motion early-returns), so
		// parity-neutral.
		int z_cooldown_ = 0;
		// Stair re-trigger LATCH (B1). Ships with the same non-replicated
		// server-transient precedent as z_cooldown_/path_to_foe (and the
		// ani_count rule): snapshots never carry it, so a mirror/late joiner
		// re-arms cleared — worst case the walker can take the stair again one
		// deliberate step early, never a wedge. Armed with the ARRIVAL centre
		// cell on every stair transition; while the walker's centre stays in
		// that cell, stair tiles do not trigger (the paired vertically-aligned
		// stair you arrive on used to bounce you straight back once
		// z_cooldown_ expired — even standing still). Cleared on the first
		// Z-probe tick (cooldown expired) that finds the centre outside the
		// cell — the same centre-cell criterion the trigger itself uses, with
		// the cooldown adding a few ticks of hysteresis against cell-boundary
		// jitter. Never armed on single-floor levels, so parity-neutral.
		void latch_stair_arrival();
		bool z_stair_latched_ = false;
		std::int32_t z_latch_cx_ = -1;
		std::int32_t z_latch_cy_ = -1;
		// Exit-pad re-trigger LATCH (#160). Same non-replicated
		// server-transient contract as z_stair_latched_ above (no dirty bit,
		// absent from EntitySnapshot/saves/wire): a mirror or late joiner
		// re-arms cleared — worst case one extra exit prompt after a resync,
		// never a wedge. Exit pads are eaten from ob_pass_check on EVERY
		// movement probe, so held-direction walking on the pad used to re-run
		// the exit prompt / "Foes remain!" toast each time the skip_exit
		// cooldown expired (~10 ticks), and in MP each re-prompt froze the
		// sim for everyone. Armed with the PAD's bbox on the first eaten
		// contact; while this walker's own bbox still overlaps the stored
		// rect, exit-pad eats are skipped entirely (rect-based: one exit per
		// spot by construction; declining the prompt leaves you latched).
		// Cleared by update_exit_latch() (living::act, every tick) once the
		// walker has fully left the rect — stepping back on is a deliberate
		// act and prompts again. skip_exit stays untouched as a secondary
		// bound (teleporter/door toasts, and the game_server skip_exit==10
		// prompt-routing sniff).
		bool exit_latched_ = false;
		std::int16_t exit_latch_x_ = 0;
		std::int16_t exit_latch_y_ = 0;
		std::int16_t exit_latch_w_ = 0;
		std::int16_t exit_latch_h_ = 0;
		// Fall-damage accumulator: stories fallen in one uninterrupted air
		// cascade, resolved ONCE at settle by resolve_fall_landing(). Same
		// non-replicated server-transient acceptance as z_cooldown_ /
		// z_stair_latched_: snapshots never carry it, so a mirror or late
		// joiner mid-cascade resolves a shorter fall; hp self-corrects on the
		// next snapshot — no wire bump. Always 0 on single-floor levels
		// (apply_z_motion early-returns), so parity-neutral.
		int fall_stories_ = 0;
		void resolve_fall_landing();
		// Per-placed-NPC scenario extras (see the public accessors above).
		// Defaults reproduce legacy behavior exactly; every consumer branch is
		// gated on a non-default value, keeping parity goldens byte-identical.
		bool specials_disabled_ = false;
		std::uint16_t spawn_delay_ = 0;
		bool save_all_protected_ = false;
		bool guard_hold_post_ = false;
		bool summoned_ = false;
		bool dormant_ = false;
		walker* foe_ = nullptr;
		walker* leader_ = nullptr;
		walker* owner_ = nullptr;
		walker* collide_ob_ = nullptr;
};

#undef OG_WALKER_DIRTY_FIELD

// Returns the best display name for an entity: myguy name if available,
// then stats name, then the provided fallback.
std::string_view entity_display_name(const walker* w, std::string_view fallback = "");
