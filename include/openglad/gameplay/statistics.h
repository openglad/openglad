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
#include <openglad/gameplay/dirty_field_bits.h>
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
#define OG_STATS_DIRTY_FIELD(type, name, bit)                             \
        [[nodiscard]] type name() const noexcept { return name##_; }      \
        void set_##name(type value)                                       \
        {                                                                 \
            name##_ = value;                                              \
            mark_dirty(bit);                                              \
        }

		statistics(walker  *);
		~statistics();
		void set_controller(walker* value);
		walker* controller() const { return controller_; }
		short  try_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		short  try_command(std::int32_t whatcommand, std::int32_t iterations);
		void set_command(std::int32_t whatcommand, std::int32_t iterations);
		void set_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		void add_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		void force_command(std::int32_t whatcommand, std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		// Ghost-scare fright injection: merges into an existing forced walk
		// at the queue front instead of prepending (runaway-specials §3.2);
		// falls through to force_command(COMMAND_WALK, ...) otherwise.
		void force_fright(std::int32_t iterations, std::int32_t info1, std::int32_t info2);
		bool has_commands() const;
		void clear_command();
		// Character-switch / control-claim variant: preserves the leading run
		// of forced walk entries (fright/knockback) and does NOT un-charm
		// (runaway-specials §4); keeps the weapon reset and leader clear.
		void clear_command_for_control_switch();
		// Player-side frozen_delay drain step: decrements like the AI drains,
		// but the 1 -> 0 transition writes -kFreezeThawImmunityTicks
		// (runaway-specials §3.3 thaw immunity).
		void player_thaw_tick();
		short do_command();
		void hit_response(walker * who);
		void yell_for_help(walker *foe);  // yell and run away
		short query_bit_flags(std::int32_t myvalue) const;
        [[nodiscard]] std::int32_t bit_flags() const noexcept { return bit_flags_; }
        void set_bit_flags(std::int32_t value)
        {
            bit_flags_ = value;
            mark_dirty(og::dirty::BIT_BIT_FLAGS);
        }
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
		bool right_walk_to_point(short x, short y); // right-hand rule toward a point
		bool direct_walk_to_point(short x, short y); // walk in a line toward a point
		bool walk_to_point(short x, short y); // walk intelligently toward a point
		// Cross-floor fallback: when a foe on another floor has no A*-reachable
		// cell (common on sparse upper floors), route toward the nearest Z-stair
		// leading toward foe_floor instead of chasing the foe's x/y on our own
		// floor. Populates path_to_foe and returns true if a stair route exists.
		bool path_toward_stair(int foe_floor);

		std::string name; // for NPC's, normally ..
        OG_STATS_DIRTY_FIELD(Order, old_order, og::dirty::BIT_OLD_ORDER);
        OG_STATS_DIRTY_FIELD(char, old_family, og::dirty::BIT_OLD_FAMILY);
        OG_STATS_DIRTY_FIELD(std::uint32_t, last_distance, og::dirty::BIT_LAST_DISTANCE);
        OG_STATS_DIRTY_FIELD(std::int32_t, current_distance, og::dirty::BIT_CURRENT_DISTANCE);
        OG_STATS_DIRTY_FIELD(short, delete_me, og::dirty::BIT_DELETE_ME);

        OG_STATS_DIRTY_FIELD(float, hitpoints, og::dirty::BIT_HITPOINTS);
        OG_STATS_DIRTY_FIELD(float, max_hitpoints, og::dirty::BIT_MAX_HITPOINTS);
        OG_STATS_DIRTY_FIELD(float, magicpoints, og::dirty::BIT_MAGICPOINTS);
        OG_STATS_DIRTY_FIELD(float, max_magicpoints, og::dirty::BIT_MAX_MAGICPOINTS);

        OG_STATS_DIRTY_FIELD(std::int32_t, max_heal_delay, og::dirty::BIT_MAX_HEAL_DELAY);
        OG_STATS_DIRTY_FIELD(std::int32_t, current_heal_delay, og::dirty::BIT_CURRENT_HEAL_DELAY);
        OG_STATS_DIRTY_FIELD(std::int32_t, max_magic_delay, og::dirty::BIT_MAX_MAGIC_DELAY);
        OG_STATS_DIRTY_FIELD(std::int32_t, current_magic_delay, og::dirty::BIT_CURRENT_MAGIC_DELAY);
        OG_STATS_DIRTY_FIELD(float, magic_per_round, og::dirty::BIT_MAGIC_PER_ROUND);
        OG_STATS_DIRTY_FIELD(float, heal_per_round, og::dirty::BIT_HEAL_PER_ROUND);
        OG_STATS_DIRTY_FIELD(float, armor, og::dirty::BIT_ARMOR);

        OG_STATS_DIRTY_FIELD(std::int32_t, level, og::dirty::BIT_LEVEL);
        // frozen_delay: freeze/paralysis timer. NEGATIVE = thaw-immunity phase
        // (runaway-specials §3.3), written ONLY by the player-side drain.
        // ALWAYS read via frozen_delay() (masked — never returns negatives, so
        // every legacy guard/drain/HUD/snapshot/replay reader sees
        // master-identical values) or frozen_delay_raw(). Manual unpick of
        // OG_STATS_DIRTY_FIELD preserving dirty-bit semantics byte-for-byte
        // on set; negatives never reach the wire (snapshot capture reads the
        // masked getter).
        [[nodiscard]] short frozen_delay() const noexcept
        {
            return frozen_delay_ < 0 ? static_cast<short>(0) : frozen_delay_;
        }
        [[nodiscard]] short frozen_delay_raw() const noexcept { return frozen_delay_; }
        void set_frozen_delay(short value)
        {
            frozen_delay_ = value;
            mark_dirty(og::dirty::BIT_FROZEN_DELAY);
        }
        // Immunity climb: raw +1/tick toward 0. Called from living::act while
        // frozen_delay_raw() < 0, so a walker switched away mid-immunity
        // keeps climbing.
        void tick_freeze_immunity()
        {
            set_frozen_delay(static_cast<short>(frozen_delay_ + 1));
        }
        OG_STATS_DIRTY_FIELD(short, weapon_cost, og::dirty::BIT_WEAPON_COST);
        OG_STATS_DIRTY_FIELD(std::uint32_t, controller_id, og::dirty::BIT_CONTROLLER_ID);
        [[nodiscard]] unsigned short special_cost(int index) const noexcept
        {
            if (index < 0 || index >= NUM_SPECIALS)
                return 0;
            return special_cost_[index];
        }
        void set_special_cost(int index, unsigned short value)
        {
            if (index < 0 || index >= NUM_SPECIALS)
                return;
            special_cost_[index] = value;
            mark_dirty(og::dirty::BIT_SPECIAL_COST);
        }
        void adjust_hitpoints(float delta) { set_hitpoints(hitpoints_ + delta); }
        void adjust_max_hitpoints(float delta) { set_max_hitpoints(max_hitpoints_ + delta); }
        void adjust_magicpoints(float delta) { set_magicpoints(magicpoints_ + delta); }
        void adjust_max_magicpoints(float delta) { set_max_magicpoints(max_magicpoints_ + delta); }
        void adjust_magic_per_round(float delta) { set_magic_per_round(magic_per_round_ + delta); }
        void adjust_heal_per_round(float delta) { set_heal_per_round(heal_per_round_ + delta); }
        void adjust_armor(float delta) { set_armor(armor_ + delta); }
        void adjust_frozen_delay(short delta)
        {
            set_frozen_delay(static_cast<short>(frozen_delay_ + delta));
        }
		std::list<command> commands;

	private:
        void mark_dirty(std::uint8_t bit_index);

        Order old_order_ = Order::Living;
        char old_family_ = FAMILY_SOLDIER;
        std::uint32_t last_distance_ = 0;
        std::int32_t current_distance_ = 0;  // Distances (to foe) are used for AI walking
        std::int32_t bit_flags_ = 0;         // holds (currently) 32 bit flags
        short delete_me_ = 0;

        float hitpoints_ = 0.0f;
        float max_hitpoints_ = 0.0f;
        float magicpoints_ = 0.0f;
        float max_magicpoints_ = 0.0f;

        std::int32_t max_heal_delay_ = 0;
        std::int32_t current_heal_delay_ = 0;
        std::int32_t max_magic_delay_ = 0;
        std::int32_t current_magic_delay_ = 0;
        float magic_per_round_ = 0.0f; // magic we regain each round
        float heal_per_round_ = 0.0f;  // hp we regain each round
        float armor_ = 0.0f; // reduces damage against us

        std::int32_t level_ = 0;
        short frozen_delay_ = 0;              // use for paralyzing..
        unsigned short special_cost_[NUM_SPECIALS] = {};  // cost of our special ability
        short weapon_cost_ = 0;                          // cost of our weapon
        std::uint32_t controller_id_ = 0;
		walker* controller_ = nullptr;
		walker* owner_ = nullptr;
};

#undef OG_STATS_DIRTY_FIELD

class command
{
	public:
		command();
		// command to execute
		std::int32_t commandtype;
		// # times to execute command
		std::int32_t commandcount;
		// parameters to command
		std::int32_t com1;
		std::int32_t com2;
		// True only for entries injected via statistics::force_command
		// (fright/knockback/flee/shove); add_command leaves it false.
		// Never serialized: snapshot apply clears the queue
		// (world_snapshot.cpp) and no save/replay path persists commands
		// (docs/runaway-effects-design.md §3.2), so the flag is
		// wire/dump-free.
		bool forced = false;
};
