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
//walker.cpp

/* ChangeLog
	buffers: 7/31/02: *deleted some redundant headers
*/
#include <cstdint>
#include <openglad/core/combat_math.h>
#include <openglad/gameplay/dirty_field_bits.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/render_component_base.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/constants.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/util.h>
#include <openglad/core/sound_ids.h>
#include "sim_difficulty.h"
// pixieN include not needed here; render bridge is in walker_render_bridge.cpp
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <format>
#include <limits>
#include <span>
#include <openglad/gameplay/possession.h>

// ************************************************************
//  WALKER -- graphics routines
//
//  WALKER is a PIXIEN with automatic frame() changing when
//  the direction it moves is changed.  This allows for
//  the concept of "facings", though currently no query-able
//  variable allows for external functions to learn the facing.
// ************************************************************


// From picker.cpp
extern std::int32_t calculate_level(std::uint32_t temp_exp);

short exp_from_action(ExpAction action, walker* w, walker* target, short value);

std::uint32_t query_difficulty_percent()
{
    // rule: The game difficulty is a percentage applied to hostile stats; an unset or non-positive
    //   difficulty (and no world) means normal, 100 percent.
    // tests: WalkerMore.walker_base_hooks_and_set_difficulty_scaling
    // tests: SemWalker1.an_unset_difficulty_or_no_world_means_one_hundred_percent
    if (current_game == nullptr || current_game->world == nullptr)
        return 100u;
    const int difficulty = current_game->world->difficulty;
    return (difficulty > 0) ? static_cast<std::uint32_t>(difficulty) : 100u;
}

namespace
{
std::int32_t scale_los_circular(std::int32_t value)
{
    constexpr std::int64_t kNumerator = 309;
    constexpr std::int64_t kDenominator = 256;
    const std::int64_t scaled =
        (static_cast<std::int64_t>(value) * kNumerator) / kDenominator;
    return static_cast<std::int32_t>(std::clamp(
        scaled,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

std::uint32_t query_generator_rate_percent()
{
    const int rate = current_game->world->generator_rate;
    // rule: The generator-rate setting scales how often generators spawn; unset (<=0) means 100
    //   percent and any value is clamped to 25..400 percent even when a save or snapshot carries a
    //   wild value.
    // tests: WalkerCoreMore.generator_rate_200_spawns_more_than_rate_100_by_fixed_tick
    // tests: SimWorldHeadless.generator_rate_default_keeps_rng_stream_identical
    // tests: SemWalker1.a_wild_generator_rate_runs_as_the_nearest_bound
    if (rate <= 0)
        return 100u;
    // Sim-side robustness clamp mirroring the lobby sanitize range: the value
    // also arrives unclamped from hand-edited v12 saves and crafted snapshots.
    return static_cast<std::uint32_t>(std::clamp(rate, 25, 400));
}

IRandom& walker_rng()
{
    // plumbing: walker_rng(): picks which RNG seeds a new walker's path_check_counter (test
    //   override, world rng, session rng, fallback world rng); no game rule, determinism plumbing.
    if (IRandom* override_rng = gameplay_rng_override())
        return *override_rng;

    // plumbing: walker_rng(): picks which RNG seeds a new walker's path_check_counter (test
    //   override, world rng, session rng, fallback world rng); no game rule, determinism plumbing.
    if (gameplay_world_rng_active() &&
        current_game != nullptr && current_game->world != nullptr)
        return current_game->world->rng_;

    // plumbing: walker_rng(): picks which RNG seeds a new walker's path_check_counter (test
    //   override, world rng, session rng, fallback world rng); no game rule, determinism plumbing.
    if (IRandom* session_rng = gameplay_session_rng())
        return *session_rng;

    // plumbing: walker_rng(): picks which RNG seeds a new walker's path_check_counter (test
    //   override, world rng, session rng, fallback world rng); no game rule, determinism plumbing.
    if (current_game != nullptr && current_game->world != nullptr)
        return current_game->world->rng_;

    assert(false && "walker construction requires an injected RNG");
    std::abort();
}

int next_path_check_counter(IRandom& rng)
{
    // path_check_counter is a cosmetic AI-recheck cadence. Master draws it from
    // the C-library rand() (`5 + rand()%10`); the branch draws it from the
    // construction-time gameplay/session RNG (`rng`) so it stays deterministic
    // for snapshot/replay/preview. When the parity harness installs a libc-rand
    // cosmetic override, draw from that instead so the captured dump matches
    // master's dual-RNG-stream behavior — without disturbing production
    // determinism or the preview/session-RNG path encoded in `rng`.
    // plumbing: next_path_check_counter: the parity harness's libc-rand cosmetic override replaces
    //   the draw so the dump matches the old game's dual RNG stream; production always draws 5 +
    //   rng.next(10).
    if (IRandom* cos = cosmetic_rng_override())
        return 5 + static_cast<int>(cos->next(10));
    return 5 + static_cast<int>(rng.next(10));
}
} // namespace

// Common initialization shared by both constructors
void walker_init_common(walker* w, IRandom* rng)
{
	w->set_curdir(FACE_DOWN);
	w->set_enddir(FACE_DOWN);
	w->set_lastx(0);
	w->set_lasty(0);
	w->set_collide_ob(nullptr);
	w->set_collide_ob_id(0);
	w->set_cycle(0);
	w->ani = nullptr;
	w->ani_count = 0;
	w->set_ani_type(0);
	w->set_busy(0);
	w->set_foe(nullptr);
	w->set_foe_id(0);
	w->set_leader(nullptr);
	w->set_leader_id(0);
	w->set_owner(nullptr);
	w->set_owner_id(0);
	w->myguy = nullptr;
	w->set_shifter_down(0);
	w->set_view_all(0);
	w->set_keys(0);
	w->set_action(0);
	w->set_ignore(0);
	w->set_current_weapon(FAMILY_KNIFE);
	w->set_default_weapon(w->current_weapon());
	w->set_yo_delay(0);
	w->set_speed_bonus(0);
	w->set_speed_bonus_left(0);
	w->set_outline(0);
	w->set_drawcycle(0);
	w->set_skip_exit(0);
	w->set_weapons_left(1);
	w->set_current_special(0);
	w->set_in_act(false);
	// plumbing: walker_init_common: a detached weapon-profile probe (rng == nullptr) gets
	//   path_check_counter 0 so it never draws from the world RNG (asserted by
	//   tests/unit/test_modes_soccer.cpp: TEST_F(ModesSoccer,
	//   recovery_range_probe_never_touches_the_world_obmap) rng_before==state).
	w->set_path_check_counter(rng != nullptr ? next_path_check_counter(*rng) : 0);
	w->set_regen_delay(0);
	w->set_hurt_flash(false);
	w->set_attack_lunge(0.0f);
	w->set_hit_recoil(0.0f);
	w->set_last_hitpoints(0.0f);
}

walker::walker(const PixieData& data)
    : SimEntity()
{
	// Create render component from PixieData
	attach_render(data);

	// Set our stats ..
	stats_ = std::make_unique<statistics>(this);
	myself_ = this;

	IRandom& rng = walker_rng();
	walker_init_common(this, &rng);

	set_act_type_state(ACT_RANDOM);
	set_frame(0);
}

walker::walker()
    : SimEntity()
{
	// Headless constructor - no render component
	stats_ = std::make_unique<statistics>(this);
	myself_ = this;

	IRandom& rng = walker_rng();
	walker_init_common(this, &rng);

	set_act_type_state(ACT_RANDOM);
}

walker::walker(WeaponProfileProbeTag)
    : SimEntity()
{
	stats_ = std::make_unique<statistics>(this);
	myself_ = this;
	// Direct state assignment is intentional: set_dormant(true) consults the
	// active obmap, while this detached profile must never touch it at all.
	dormant_ = true;
	walker_init_common(this, nullptr);
	set_act_type_state(ACT_RANDOM);
}

// attach_render, set_data, bmp_data, set_frame are in
// src/runtime/walker_render_bridge.cpp (require pixieN full definition).

short walker::next_frame()
{
	// frames is 0 for a default/test-built or moved-from walker; guard the
	// modulo to avoid integer division-by-zero UB (mirrors pixieN::next_frame).
	// plumbing: next_frame: frames==0 guard against modulo by zero for test-built walkers.
	if (frames <= 0) return 0;
	return set_frame(static_cast<short>(frame() % frames));
}

void walker::set_myguy_view(guy* guy_view)
{
	owned_myguy_.reset();
	myguy = guy_view;
}

void walker::set_owned_myguy(std::unique_ptr<guy> owned_guy)
{
	owned_myguy_ = std::move(owned_guy);
	myguy = owned_myguy_.get();
}

void walker::clear_myguy()
{
	owned_myguy_.reset();
	myguy = nullptr;
}

void walker::move_myguy_to(walker* target)
{
	// plumbing: move_myguy_to: hands the guy record (owned or view) to another body
	//   (transform/possession); null target keeps it.
	if (target == nullptr)
		return;

	// plumbing: move_myguy_to: hands the guy record (owned or view) to another body
	//   (transform/possession); null target keeps it.
	if (owned_myguy_)
		target->set_owned_myguy(std::move(owned_myguy_));
	else
		target->set_myguy_view(myguy);

	myguy = nullptr;
}

void walker::set_foe(walker* target)
{
	foe_ = target;
	// plumbing: set_foe/set_leader/set_owner/set_collide_ob mirror the pointer into a stable entity
	//   id for snapshots (null -> 0).
	foe_id_ = (target != nullptr) ? target->entity_id() : 0;
	mark_dirty(og::dirty::BIT_FOE_ID);
}

void walker::set_leader(walker* target)
{
	leader_ = target;
	// plumbing: set_foe/set_leader/set_owner/set_collide_ob mirror the pointer into a stable entity
	//   id for snapshots (null -> 0).
	leader_id_ = (target != nullptr) ? target->entity_id() : 0;
	mark_dirty(og::dirty::BIT_LEADER_ID);
}

void walker::set_owner(walker* target)
{
	owner_ = target;
	// plumbing: set_foe/set_leader/set_owner/set_collide_ob mirror the pointer into a stable entity
	//   id for snapshots (null -> 0).
	owner_id_ = (target != nullptr) ? target->entity_id() : 0;
	mark_dirty(og::dirty::BIT_OWNER_ID);
}

void walker::set_collide_ob(walker* target)
{
	collide_ob_ = target;
	// plumbing: set_foe/set_leader/set_owner/set_collide_ob mirror the pointer into a stable entity
	//   id for snapshots (null -> 0).
	collide_ob_id_ = (target != nullptr) ? target->entity_id() : 0;
	mark_dirty(og::dirty::BIT_COLLIDE_OB_ID);
}

void walker::sync_ids_from_pointers()
{
	auto sync_link = [this](walker* target,
	                        std::uint32_t& stored_id,
	                        std::uint8_t bit,
	                        auto clear_pointer) {
		std::uint32_t next_id = 0;
		// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose id does
		//   not resolve to the same entity (or a detached entity) is cleared; controller id
		//   likewise.
		if (target != nullptr)
		{
			// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose id
			//   does not resolve to the same entity (or a detached entity) is cleared; controller
			//   id likewise.
			if (owning_world_ == nullptr)
			{
				clear_pointer();
				return;
			}

			// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose id
			//   does not resolve to the same entity (or a detached entity) is cleared; controller
			//   id likewise.
			if (stored_id != 0)
			{
				// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose
				//   id does not resolve to the same entity (or a detached entity) is cleared;
				//   controller id likewise.
				if (owning_world_->find_by_id(stored_id) != target)
				{
					clear_pointer();
					return;
				}
				next_id = stored_id;
			}
			else
			{
				next_id = owning_world_->tracked_entity_id(target);
				// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose
				//   id does not resolve to the same entity (or a detached entity) is cleared;
				//   controller id likewise.
				if (next_id == 0)
				{
					clear_pointer();
					return;
				}
			}
		}

		// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose id does
		//   not resolve to the same entity (or a detached entity) is cleared; controller id
		//   likewise.
		if (stored_id != next_id)
		{
			stored_id = next_id;
			mark_dirty(bit);
		}
	};

	sync_link(foe_, foe_id_, og::dirty::BIT_FOE_ID, [this]() { set_foe(nullptr); });
	sync_link(leader_, leader_id_, og::dirty::BIT_LEADER_ID,
	          [this]() { set_leader(nullptr); });
	sync_link(owner_, owner_id_, og::dirty::BIT_OWNER_ID,
	          [this]() { set_owner(nullptr); });
	sync_link(collide_ob_, collide_ob_id_, og::dirty::BIT_COLLIDE_OB_ID,
	          [this]() { set_collide_ob(nullptr); });

	// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose id does not
	//   resolve to the same entity (or a detached entity) is cleared; controller id likewise.
	if (statistics* const entity_stats = stats(); entity_stats != nullptr)
	{
		walker* controller = entity_stats->controller();
		// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose id does
		//   not resolve to the same entity (or a detached entity) is cleared; controller id
		//   likewise.
		if (controller != nullptr)
		{
			// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose id
			//   does not resolve to the same entity (or a detached entity) is cleared; controller
			//   id likewise.
			if (owning_world_ == nullptr)
			{
				entity_stats->set_controller(nullptr);
			}
			else
			{
				std::uint32_t controller_id = 0;
				// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose
				//   id does not resolve to the same entity (or a detached entity) is cleared;
				//   controller id likewise.
				if (entity_stats->controller_id() != 0)
				{
					// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link
					//   whose id does not resolve to the same entity (or a detached entity) is
					//   cleared; controller id likewise.
					if (owning_world_->find_by_id(entity_stats->controller_id()) !=
					    controller)
					{
						entity_stats->set_controller(nullptr);
					}
					else
					{
						controller_id = entity_stats->controller_id();
					}
				}
				else
				{
					controller_id = owning_world_->tracked_entity_id(controller);
					// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link
					//   whose id does not resolve to the same entity (or a detached entity) is
					//   cleared; controller id likewise.
					if (controller_id == 0)
						entity_stats->set_controller(nullptr);
				}

				// plumbing: sync_ids_from_pointers: snapshot-apply id reconciliation; a link whose
				//   id does not resolve to the same entity (or a detached entity) is cleared;
				//   controller id likewise.
				if (entity_stats->controller_id() != controller_id)
				{
					entity_stats->set_controller_id(controller_id);
					mark_dirty(og::dirty::BIT_CONTROLLER_ID);
				}
			}
		}
		else if (entity_stats->controller_id() != 0)
		{
			entity_stats->set_controller_id(0);
			mark_dirty(og::dirty::BIT_CONTROLLER_ID);
		}
	}
}

bool
walker::reset(void)
{

	// double comments we're needed to make it work, maybe they are
	// not needed??

	//  curdir = 0;  // We are facing UP
	//  enddir = 0;  // We are trying to face UP
	//  lastx = 0;
	//  lasty = 0;
	//  act_type = ACT_RANDOM;
	//  collide_ob = nullptr;

	// // cycle = 0;

	//  ani = nullptr;

	// //  set_team_num(0);
	// //  ani_type = 0;
	// //  busy = 0;

	//  foe = nullptr;
	//  leader = nullptr;
	//  owner = nullptr;
	//  myguy = nullptr;
	//  myself = this;
	//  ani = nullptr;
	set_dead(0); // we're alive

	set_death_called(0);


	//  set_bonus_rounds(0);
	//  shifter_down = 0; // the player's shifter/alternate is NOT pressed
	//  view_all = 0;     // by default can't see treasures, etc. on radar
	//  keys = 0; // no keys

	//  action = 0; // no special action mode
	set_ignore(0); // don't ignore us! Collide with us...
	//  default_weapon = current_weapon = FAMILY_KNIFE; // just in case ..
	//  set_user(-1); // default user() status = no user()
	// Set our stats ..
	//  set_frame(0);

	//  yo_delay = 0;

	set_flight_left(0);
	//  set_invulnerable_left(0);
	//  set_invisibility_left(0);
	//  outline = 0;
	//  drawcycle = 0;
	set_in_act(false);

	//  skip_exit = 0;
	//  xpos() = ypos() = -1; //this to correct a problem with these not being alloced?

	//  weapons_left = 1; // default, used for fighters
	set_path_check_counter(next_path_check_counter(walker_rng()));
    set_regen_delay(0);

	// plumbing: reset(): stats_ null guard before clearing bit_flags.
	if (stats_)
		stats_->set_bit_flags(0);

	set_hurt_flash(false);
	set_attack_lunge(0.0f);
	set_hit_recoil(0.0f);

	set_last_hitpoints(0.0f);
	
	return 1;
}

// ~walker() is in src/runtime/walker_render_bridge.cpp (requires pixieN for render_.reset()).

// Movement/facing/turning methods moved to walker_movement.cpp.

// This is the function you actually call when you want something
// to fire.  It initializes the animation if animation is valid
// and checks to see if the object is too busy.
bool walker::init_fire()
{
	return init_fire(static_cast<short>(lastx()), static_cast<short>(lasty()));
}

bool walker::init_fire(short xdir, short ydir)
{
	// Turn if we want to fire another direction

	// If a non-player fires in a set direction, turn!

	// rule: A living whose curdir differs from enddir turns instead of shooting: a bot spends the
	//   call turning one step (turn(enddir)), a player-controlled walker simply does not fire this
	//   call. enddir is overwritten with the fire direction only when that direction differs from
	//   curdir, so a walk-requested turn still pending (living.cpp sets enddir; walker::turn in
	//   walker_movement.cpp advances curdir and never writes enddir) also blocks a shot in
	//   the direction the walker ALREADY faces, and a bot then turns toward the walk direction, not
	//   the fire direction. Non-livings (generators) fire in any direction without turning.
	// tests: WalkerUnit.walker_r11_myguy_move_and_init_fire_paths
	// tests: WalkerCombat.walker_batch7_init_fire_and_animate_edge_paths
	// tests: WalkerCoreMore.walker_act_generate_zero_vector_and_hp_cap_paths
	// tests: SemWalker1.classic_a_living_facing_away_or_mid_turn_turns_instead_of_firing
	if (facing(xdir, ydir) != curdir())
	{
		set_enddir(static_cast<char>(facing(xdir, ydir)));
	}
	if (curdir() != enddir() && query_order() == Order::Living)
	{
		//if (family()==FAMILY_TOWER1)
		//  enddir = curdir;
		// rule: A living whose curdir differs from enddir turns instead of shooting.
		// tests: WalkerUnit.walker_r11_myguy_move_and_init_fire_paths
		// tests: WalkerCombat.walker_batch7_init_fire_and_animate_edge_paths
		// tests: WalkerCoreMore.walker_act_generate_zero_vector_and_hp_cap_paths
		// tests: SemWalker1.classic_a_living_facing_away_or_mid_turn_turns_instead_of_firing
		if (act_type() == ACT_CONTROL)
			return 0;
		else
			return turn(enddir());
	}

	// rule: A walker that fired recently is busy and cannot fire again until busy drains; each
	//   successful start adds the family fire_frequency to busy.
	// tests: WalkerUnit.walker_r11_myguy_move_and_init_fire_paths
	// tests: WalkerCombat.walker_batch7_init_fire_and_animate_edge_paths
	// tests: WalkerMore.walker_init_fire_and_fire_check_gate_branches
	// tests: StatsCommands.stats_do_command_fire_denied_without_foe_and_spends_busy_when_allowed
	if (busy() > 0.0f)
		return 0;  // Too busy

	set_busy(busy() + fire_frequency()); // This pauses a few rounds

	//  if (ani_type == ANI_WALK && query_order() == Order::Living)
	// rule: busy is charged (busy += fire_frequency, above) BEFORE this split, so every
	//   call that passes the busy gate pays the full fire delay whether or not a weapon ever
	//   appears. A walker in its walk animation starts an attack animation and the shot leaves only
	//   when the animation completes (below; that completion fire() may still fail for
	//   mp, and the delay stays paid); a walker already in another animation fires immediately, and
	//   a refused shot (no magic) still costs the delay.
	// tests: WalkerUnit.walker_r11_myguy_move_and_init_fire_paths
	// tests: WalkerMore.walker_init_fire_and_fire_check_gate_branches
	// tests: SemWalker1.a_walker_in_another_animation_fires_at_once_and_pays_the_delay_either_way
	if (ani_type() == ANI_WALK)  // This should allow generators to animate
	{
		set_ani_type(ANI_ATTACK);
		set_cycle(0);
		animate();
		return 1;
	}
	else
	{
		// rule: busy is charged (busy += fire_frequency, above) BEFORE this split, so
		//   every call that passes the busy gate pays the full fire delay whether or not a weapon
		//   ever appears.
		// tests: WalkerUnit.walker_r11_myguy_move_and_init_fire_paths
		// tests: WalkerMore.walker_init_fire_and_fire_check_gate_branches
		// tests: SemWalker1.a_walker_in_another_animation_fires_at_once_and_pays_the_delay_either_way
		if (fire())
			return 1;
		else
			return 0;
	}
}

walker  * walker::fire()
{
	walker  *weapon = nullptr;
	signed char waver;
	//short xp, yp;

	// Do we have enough spellpoints for our weapon
	// rule: A walker cannot attack (ranged or melee) without the weapon's magic cost in hand; the
	//   cost is paid once the weapon is built, before range/melee is decided (so melee swings and
	//   BIT_NO_RANGED discards also pay).
	// tests: WalkerCoreMore.walker_round6_fire_and_friendliness_paths
	// tests: WalkerUnit.fire_with_an_unbuildable_weapon_spends_no_magic
	if (stats_->magicpoints() < stats_->weapon_cost())
		return nullptr;

	weapon = create_weapon();
	// plumbing: fire(): create_weapon returned null (unbuildable family) -> no shot and no charge;
	//   asserted by tests/integration/test_walker_unit.cpp: TEST(WalkerUnit,
	//   fire_with_an_unbuildable_weapon_spends_no_magic).
	if (!weapon)
		return nullptr;

	stats_->set_magicpoints(	stats_->magicpoints() - stats_->weapon_cost());

	// Determine how much the thrown weapon can 'waver'
	// Clamp before the signed-char narrowing: stepsize is replicated from
	// network snapshots, so a hostile value > 254 would make the float->signed
	// char conversion undefined. Normal stepsizes are well under 254, so this
	// is byte-identical for legitimate game data.
	waver = static_cast<signed char>(std::clamp((weapon->stepsize())/2, 0.0f, 127.0f)); // Absolute amount ..
	waver = static_cast<signed char>(current_game->world->rng_.next(static_cast<std::uint32_t>(waver+1)) - static_cast<std::uint32_t>(waver/2));

	short pad_x = 0, pad_y = 0;
	weapon_pad(*weapon, pad_x, pad_y);
	// rule: A projectile flies along the shooter's facing at the weapon's stepsize per tick, with a
	//   random 'waver' across the line: cardinal shots waver perpendicular (+w on the cross axis),
	//   diagonal shots waver along a fixed per-diagonal pattern (UP_RIGHT +w,+w; UP_LEFT -w,+w on
	//   x,y; DOWN_RIGHT -w on x,+w on y; DOWN_LEFT +w on x,+w on y). waver = rng(h+1) - h/2 with h
	//   = stepsize/2 (skewed toward +). Same rule as below (set_weapon_heading).
	// tests: StatsCommands.stats_do_command_quick_fire_walks_and_fires_in_one_tick
	// tests: SemWalker1.a_shot_flies_along_the_facing_with_its_waver_across_or_along_the_diagonal
	switch (facing(lastx(), lasty()))
	{
		case FACE_RIGHT:
			weapon->set_lastx(weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_LEFT:
			weapon->set_lastx(-weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_DOWN:
			weapon->set_lasty(weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP:
			weapon->set_lasty(- weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP_RIGHT:
			weapon->set_lastx(weapon->stepsize() + waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_UP_LEFT:
			weapon->set_lastx(-weapon->stepsize() - waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_DOWN_RIGHT:
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(weapon->stepsize() - waver);
			break;
		case FACE_DOWN_LEFT:
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(-weapon->stepsize() + waver);
			break;
	}
	weapon->setxy(pad_x, pad_y);

	weapon->set_frame(frame());
	// Make sure our current direction is wrong so first walk
	// will just be draw (grumble curse)
	weapon->set_curdir(static_cast<char>((frame()+1)%2));

	//xp = weapon->xpos();
	//yp = weapon->ypos();

	// Delayed spawns: a dormant walker is intangible (absent from the obmap),
	// so the pad probe below cannot see it — a generator would stack its
	// spawn on top of a walker that has not entered the world yet. Treat a
	// dormant-occupied pad exactly like a blocked pad: discard this round's
	// spawn and retry on the next cadence — no melee (there is nothing
	// tangible to attack), no pad side effects, and the dormant walker is
	// never consumed. Only ever true when a dormant walker overlaps the pad,
	// so stock levels (which never author spawn_delay) keep byte-identical
	// behavior and RNG streams. Generator-gated: livings' weapons must keep
	// flying straight over dormant cells.
	// rule: A generator never spawns on top of a delayed-spawn (dormant) walker that has not
	//   entered the world yet: the round's spawn is discarded with no melee and the dormant walker
	//   survives; projectiles of livings still fly over dormant cells.
	// tests: DelayedSpawnWake.generator_pad_dormant_walker_survives
	// tests: SemWalker1.a_livings_shot_flies_over_a_dormant_walker
	if (query_order() == Order::Generator &&
	    current_game->world->dormant_occupies_spot(weapon))
	{
		weapon->set_dead(1);
		return nullptr;
	}

	// Actual combat
	// rule: When the shot's spawn pad is blocked, there is no projectile: the walker melees
	//   whatever live body blocks the pad (attack() with the shooter's damage), and a wall-blocked
	//   pad wastes the attack. Applies to generators too: a generator whose pad is held by a foe
	//   strikes that foe instead of spawning.
	// tests: QuietProbe.fire_pad_clear_matches_fires_own_pad, parity:generator_owner_cascade_scen99
	// unreachable: a dead body never holds a pad (query_object_passable and ob_pass_check skip dead
	//   walkers, obmap.cpp); the only blocker that dies inside the probe is a keyed door, and a
	//   fresh weapon or generator spawn carries no keys
	if (!current_game->world->query_passable(weapon->xpos(), weapon->ypos(), weapon))
	{
		// *** Melee combat ***
		// rule: When the shot's spawn pad is blocked, there is no projectile. Here:
		//   `weapon->collide_ob() && !weapon->collide_ob()->dead()`.
		// tests: QuietProbe.fire_pad_clear_matches_fires_own_pad
		// tests: parity:generator_owner_cascade_scen99
		if (weapon->collide_ob() && !weapon->collide_ob()->dead())
		{
			// rule: A landed melee hit clangs; a living attacker lunges toward its facing and its
			//   family's on_melee_hit Lua hook runs on the victim (no core pack registers one; mods
			//   may). A generator's landed hit clangs but neither lunges nor runs the hook.
			// tests: SemWalker1.a_landed_blow_clangs_and_a_living_attacker_lunges_and_runs_its_hook
			if (attack(weapon->collide_ob()))
			{
				og::sim::emit_positional_sound(current_game->sim_events, this, SOUND_CLANG);

                // rule: A landed melee hit clangs; a living attacker lunges toward its facing and
                //   its family's on_melee_hit Lua hook runs on the victim (no core pack registers
                //   one; mods may). Here: `query_order() == Order::Living`.
                // tests: SemWalker1.a_landed_blow_clangs_and_a_living_attacker_lunges_and_runs_its_hook
                if(query_order() == Order::Living)
                {
	                    set_attack_lunge(1.0f);
	                    set_attack_lunge_angle(get_current_angle());
                }

				// Family-specific melee hit callback
				// rule: A landed melee hit clangs; a living attacker lunges toward its facing and
				//   its family's on_melee_hit Lua hook runs on the victim (no core pack registers
				//   one; mods may). Here: `order() == Order::Living`.
				// tests: SemWalker1.a_landed_blow_clangs_and_a_living_attacker_lunges_and_runs_its_hook
				if (order() == Order::Living)
				{
					const auto* fd = get_family_descriptor(family());
					og::script::hooks::on_melee_hit(fd, this, weapon->collide_ob());
				}
			}
			// rule: A player-guy's melee swing at a live LIVING body counts as a shot (total_shots
			//   and scen_shots +1) whether or not the hit lands. attack()
			//   (walker_combat.cpp) runs BEFORE this increment and, for a target that
			//   passes its dead/friendly/treasure/invincible guards but is not Order::Living (a
			//   hostile generator, weapon or effect on the pad), does
			//   attacker->myguy->total_shots-- / scen_shots-- ('we already counted it'), so such a
			//   swing nets 0. Treasure, friendly and invulnerable/invincible/hidden targets return
			//   0 from attack() before that decrement and net +1. Swings into walls or corpses do
			//   not reach this line.
			// tests: SemWalker1.a_players_swing_at_a_body_counts_as_a_shot_except_a_generator_it_hits
			if (myguy)
            {
				myguy->total_shots++; // record that we fired/attacked
				myguy->scen_shots++;
            }
			}
			weapon->set_dead(1);
			return nullptr;
		}
		else if (stats_->query_bit_flags(BIT_NO_RANGED))  // rule: A melee-only family
			//   (BIT_NO_RANGED, e.g. orc) never launches a projectile: with a clear pad the weapon
			//   is discarded, yet the weapon cost has already been paid and nothing is recorded as
			//   a shot.
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.fire_with_an_unbuildable_weapon_spends_no_magic
			// tests: SemWalker1.a_melee_only_walker_pays_for_a_shot_it_never_launches
		{
			weapon->set_dead(1);
			return nullptr;
		}
	else  // rule: A melee-only family (BIT_NO_RANGED, e.g. orc) never launches a projectile.
		// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
		// tests: WalkerUnit.fire_with_an_unbuildable_weapon_spends_no_magic
		// tests: SemWalker1.a_melee_only_walker_pays_for_a_shot_it_never_launches
	{
		// rule: A living's family on_fire_weapon Lua hook may veto or modify the launch: the
		//   soldier vetoes when its returning knives are all out (weapons_left <= 0, refunding the
		//   mp and killing the weapon) and otherwise spends one weapons_left; the archmage drains
		//   1/20 of its magic (capped) into the shot's damage. A veto returns before the shot is
		//   counted or any fire sound.
		// tests: FamilyBehaviors.soldier_weapons_left_limits_fire
		// tests: FamilyBehaviors.archmage_weapon_damage_boost
		// tests: QuietProbe.fire_pad_clear_matches_fires_own_pad
		if (order() == Order::Living)
		{
			const auto* fd = get_family_descriptor(family());
			// rule: A living's family on_fire_weapon Lua hook may veto or modify the launch. Here:
			//   `hook_result = on_fire_weapon(...)`.
			// tests: FamilyBehaviors.soldier_weapons_left_limits_fire
			// tests: FamilyBehaviors.archmage_weapon_damage_boost
			// tests: QuietProbe.fire_pad_clear_matches_fires_own_pad
			if (auto hook_result = og::script::hooks::on_fire_weapon(fd, this, weapon))
			{
				// rule: A veto (on_fire_weapon answers false) kills the weapon fire() built and fires nothing.
				// tests: FamilyBehaviors.soldier_weapons_left_limits_fire, FamilyBehaviors.archmage_weapon_damage_boost
				// tests: QuietProbe.fire_pad_clear_matches_fires_own_pad, SemWalker1.a_shot_its_family_hook_vetoes_leaves_no_live_weapon
				if (!*hook_result)
				{
					weapon->set_dead(1);
					return nullptr;
				}
			}
		}
		// Record our shot ..
		// rule: Every ranged launch by a player guy counts as a shot (total_shots and scen_shots),
		//   feeding accuracy stats.
		// tests: SemWalker1.every_launch_by_a_player_counts_as_a_shot
		if (myguy)
        {
			myguy->total_shots++;
			myguy->scen_shots++;
        }

			// *** Ranged combat ***
			{
					const auto* wfd = get_weapon_family_descriptor(weapon->family());
					// rule: A ranged launch makes its weapon family's fire sound (bow for arrows,
					//   blast for fireballs, ...), FWIP when the family declares none. For a
					//   generator spawn the same line runs with weapon = the SPAWNED LIVING, so the
					//   descriptor is looked up by the spawn's living family id in the weapon
					//   table: the spawn makes the fire_sound of whichever weapon family shares
					//   that number (FAMILY_MAGE=3 -> weapon slot 3 FAMILY_FIREBALL,
					//   FAMILY_SKELETON=4 -> weapon slot 4, FAMILY_ELF=1 -> FAMILY_ROCK), FWIP only
					//   when no weapon descriptor sits at that slot.
					// tests: SemWalker1.classic_a_launch_sounds_by_its_weapon_family_number_spawns_included
					og::sim::emit_positional_sound(current_game->sim_events, this,
					                               static_cast<std::uint32_t>(wfd ? wfd->fire_sound : SOUND_FWIP));
				}
			// rule: Each generator spawn is a living of the generator's default_weapon family whose
			//   level is rolled uniformly in 1..generator level and whose stats are
			//   difficulty-scaled once at that level (fix A12b removed a second scaling at the
			//   generator's level), then a scripted customize_spawn may post-process it.
			// tests: DifficultyScaling.generator_spawn_gets_single_difficulty_application
			// tests: WalkerUnit.fire_with_an_unbuildable_weapon_spends_no_magic
			// unreachable: current: a generator with no descriptor spawns as add_ob made it (no
			//   level roll, no lifetime); the old game's fire() switch default treats any unknown
			//   generator as a tent (lifetime 800+11L, level roll, set_difficulty); test not
			//   written | triage: Unreachable. require_core_families_installed throws when any core
			//   generator slot is undeclared (family_registry.cpp at e12043db).
			if (order() == Order::Generator)
			{
			const auto* gfd = get_generator_family_descriptor(family());
			if (gfd)
			{
				// rule: Some generators' spawns arrive with a special entry animation: the mage
				//   tower's mages teleport in (ANI_TELE_IN); other generators' spawns just walk.
				// tests: WalkerCoreMore.walker_generator_fire_sets_weapon_lifetime_or_owner_paths
				// tests: WalkerUnit.walker_r15_generator_fire_and_heading_branches
				// tests: SemWalker1.a_tent_spawn_walks_in_for_a_while_and_tower_and_treehouse_spawns_stay
				if (gfd->spawn_ani_type != 0)
					weapon->set_ani_type(gfd->spawn_ani_type);
				// rule: Spawns of tents/bones-type generators are temporary: they live 800 +
				//   11*generator level ticks; tower and treehouse spawns are permanent.
				// tests: GameplayFixesCharm.a_charmed_tent_troop_fights_for_its_charmer
				// tests: SemWalker1.a_tent_spawn_walks_in_for_a_while_and_tower_and_treehouse_spawns_stay
				if (gfd->has_lifetime)
					weapon->set_lifetime(800 + stats_->level()*11);
				weapon->stats()->set_level(static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(stats_->level()))) + 1);
				weapon->set_difficulty(static_cast<std::uint32_t>(weapon->stats()->level()));
				// rule: Tower and treehouse spawns are free agents (no owner), while tent-type
				//   spawns stay owned by their generator (escort semantics, owner chain for score
				//   and orders).
				// tests: WalkerCoreMore.walker_generator_fire_sets_weapon_lifetime_or_owner_paths
				// tests: WalkerUnit.walker_r15_generator_fire_and_heading_branches
				if (gfd->clear_owner)
					weapon->set_owner(nullptr);
			}
			// Scripted generators post-process the configured spawn
			// (inert unless a pack registered customize_spawn).
			og::script::hooks::generator_customize_spawn(family(), this,
			                                             weapon);
		}
		// Living-family() weapon modifications handled by on_fire_weapon above
		return weapon;
	}

}

void walker::set_weapon_heading(walker *weapon)
{
	signed char waver;

	// Determine how much the thrown weapon can 'waver'
	// Clamp before the signed-char narrowing: stepsize is replicated from
	// network snapshots, so a hostile value > 254 would make the float->signed
	// char conversion undefined. Normal stepsizes are well under 254, so this
	// is byte-identical for legitimate game data.
	waver = static_cast<signed char>(std::clamp((weapon->stepsize())/2, 0.0f, 127.0f)); // Absolute amount ..
	waver = static_cast<signed char>(current_game->world->rng_.next(static_cast<std::uint32_t>(waver+1)) - static_cast<std::uint32_t>(waver/2));

	short pad_x = 0, pad_y = 0;
	weapon_pad(*weapon, pad_x, pad_y);  // fire()'s pad: one table
	weapon->setxy(pad_x, pad_y);
	// rule: Same rule as above: the bot's fire-check probe weapon is placed on fire()'s
	//   pad and headed along the OWNER's lastx/lasty facing with the same waver pattern, so the
	//   probe predicts the real shot.
	// tests: QuietProbe.set_weapon_heading_lands_on_fires_pad
	// tests: WalkerUnit.walker_r11_fire_check_create_weapon_and_angles
	// tests: WalkerUnit.walker_r15_generator_fire_and_heading_branches
	switch (facing(lastx(), lasty()))  // these are from the 'owner'
	{
		case FACE_RIGHT:
			weapon->set_lastx(weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_LEFT:
			weapon->set_lastx(-weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_DOWN:
			weapon->set_lasty(weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP:
			weapon->set_lasty(- weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP_RIGHT:
			weapon->set_lastx(weapon->stepsize() + waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_UP_LEFT:
			weapon->set_lastx(-weapon->stepsize() - waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_DOWN_RIGHT:
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(weapon->stepsize() - waver);
			break;
		case FACE_DOWN_LEFT:
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(-weapon->stepsize() + waver);
			break;
	}

}

// To avoid problems with limited precision. Used by score_panel.cpp and
// glad.cpp via extern declaration.
bool float_eq(float a, float b)
{
    return (a == b || (a - 0.000001f < b && a + 0.000001f > b));
}

walker::DamageNumber::DamageNumber(float x_,
                                   float y_,
                                   float value_,
                                   unsigned char color_,
                                   std::uint32_t created_tick_)
    : x(x_)
    , y(y_)
    , t(1.0f)
    , value(value_)
    , created_tick(created_tick_)
    , color(color_)
{}

void walker::compute_outline(const walker* viewer_control,
                             bool mark_player_controls)
{
	unsigned char next_outline = outline();
	// rule: A walker with no status (not named, no invisibility, flight or invulnerability) has no
	//   outline.
	// tests: WalkerDeath.walker_compute_outline_no_status
	// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
	if (stats_->query_bit_flags( BIT_NAMED ) || invisibility_left() || flight_left() || invulnerable_left())
	{
		// rule: A walker with several statuses blinks between their outlines, advancing one step
		//   per call from the outline it currently shows. From INVULNERABLE: flying wins (FLYING),
		//   else named-to-an-enemy-viewer (NAMED); then, unless NAMED was chosen, invisibility
		//   overrides to team colour (so invulnerable+flying+invisible shows team colour, and NAMED
		//   beats invisible only in this branch). From FLYING: named(enemy viewer) else
		//   invisible(team colour) else invulnerable. From NAMED: invisible else invulnerable else
		//   flying. From team colour: invulnerable else flying else named. Anything else:
		//   invisible, flying, invulnerable, named in that order. Named is shown only to a viewer
		//   of another team. Render-only value. In play the chain rarely advances past one step:
		//   living::act (living.cpp) does set_outline(0) EVERY sim tick for a living that
		//   is not invisible, so for a non-invisible living the chain restarts from the 'other (0)'
		//   arm each tick and the from-INVULNERABLE / FLYING / NAMED / team-colour arms are reached
		//   only between compute_outline calls inside one tick (several viewscreens or render
		//   frames per tick), or for invisible livings and non-living walkers whose outline
		//   persists. Some states leave the outline unchanged: from NAMED with only BIT_NAMED it
		//   stays NAMED (any viewer, including same-team); from FLYING with flight expired and only
		//   BIT_NAMED seen by a same-team viewer it stays FLYING; from INVULNERABLE with nothing
		//   else it stays INVULNERABLE.
		// tests: WalkerCoreMore.walker_compute_outline_state_transitions
		// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
		// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
		// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
		// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
		// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
		// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
		// tests: WalkerMovement.walker_draw_with_invisibility
		// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
		if (next_outline == OUTLINE_INVULNERABLE)
		{
			// rule: A walker with several statuses blinks between their outlines, advancing one
			//   step per call from the outline it currently shows.
			// tests: WalkerCoreMore.walker_compute_outline_state_transitions
			// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
			// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
			// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
			// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
			// tests: WalkerMovement.walker_draw_with_invisibility
			// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
			if      (flight_left())
				next_outline = OUTLINE_FLYING;
			else if (viewer_control)
				// rule: A walker with several statuses blinks between their outlines, advancing one
				//   step per call from the outline it currently shows.
				// tests: WalkerCoreMore.walker_compute_outline_state_transitions
				// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
				// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
				// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
				// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
				// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
				// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
				// tests: WalkerMovement.walker_draw_with_invisibility
				// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num()!=viewer_control->team_num()))
					next_outline = OUTLINE_NAMED;

			// rule: A walker with several statuses blinks between their outlines, advancing one
			//   step per call from the outline it currently shows.
			// tests: WalkerCoreMore.walker_compute_outline_state_transitions
			// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
			// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
			// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
			// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
			// tests: WalkerMovement.walker_draw_with_invisibility
			// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
			if (next_outline != OUTLINE_NAMED)
				// rule: A walker with several statuses blinks between their outlines, advancing one
				//   step per call from the outline it currently shows.
				// tests: WalkerCoreMore.walker_compute_outline_state_transitions
				// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
				// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
				// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
				// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
				// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
				// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
				// tests: WalkerMovement.walker_draw_with_invisibility
				// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
				if (invisibility_left())
					next_outline = query_team_color();
		}
		else if (next_outline == OUTLINE_FLYING)
		{
			// rule: A walker with several statuses blinks between their outlines, advancing one
			//   step per call from the outline it currently shows.
			// tests: WalkerCoreMore.walker_compute_outline_state_transitions
			// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
			// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
			// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
			// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
			// tests: WalkerMovement.walker_draw_with_invisibility
			// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
			if (viewer_control)
				// rule: A walker with several statuses blinks between their outlines, advancing one
				//   step per call from the outline it currently shows.
				// tests: WalkerCoreMore.walker_compute_outline_state_transitions
				// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
				// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
				// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
				// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
				// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
				// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
				// tests: WalkerMovement.walker_draw_with_invisibility
				// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
				if      (stats_->query_bit_flags (BIT_NAMED) && (team_num()!=viewer_control->team_num()))
					next_outline = OUTLINE_NAMED;

			// rule: A walker with several statuses blinks between their outlines, advancing one
			//   step per call from the outline it currently shows.
			// tests: WalkerCoreMore.walker_compute_outline_state_transitions
			// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
			// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
			// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
			// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
			// tests: WalkerMovement.walker_draw_with_invisibility
			// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
			if (next_outline != OUTLINE_NAMED)
			{
				// rule: A walker with several statuses blinks between their outlines, advancing one
				//   step per call from the outline it currently shows.
				// tests: WalkerCoreMore.walker_compute_outline_state_transitions
				// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
				// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
				// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
				// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
				// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
				// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
				// tests: WalkerMovement.walker_draw_with_invisibility
				// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
				if (invisibility_left())
					next_outline = query_team_color();
				else if (invulnerable_left())
					next_outline = OUTLINE_INVULNERABLE;
			}
		}
		else if (next_outline == OUTLINE_NAMED)
		{
			// rule: A walker with several statuses blinks between their outlines, advancing one
			//   step per call from the outline it currently shows.
			// tests: WalkerCoreMore.walker_compute_outline_state_transitions
			// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
			// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
			// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
			// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
			// tests: WalkerMovement.walker_draw_with_invisibility
			// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
			if      (invisibility_left())
				next_outline = query_team_color();
			else if (invulnerable_left())
				next_outline = OUTLINE_INVULNERABLE;
			else if (flight_left())
				next_outline = OUTLINE_FLYING;
		}
		else if (next_outline == query_team_color())
		{
			// rule: A walker with several statuses blinks between their outlines, advancing one
			//   step per call from the outline it currently shows.
			// tests: WalkerCoreMore.walker_compute_outline_state_transitions
			// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
			// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
			// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
			// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
			// tests: WalkerMovement.walker_draw_with_invisibility
			// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
			if      (invulnerable_left())
				next_outline = OUTLINE_INVULNERABLE;
			else if (flight_left())
				next_outline = OUTLINE_FLYING;
			else if (viewer_control)
				// rule: A walker with several statuses blinks between their outlines, advancing one
				//   step per call from the outline it currently shows.
				// tests: WalkerCoreMore.walker_compute_outline_state_transitions
				// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
				// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
				// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
				// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
				// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
				// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
				// tests: WalkerMovement.walker_draw_with_invisibility
				// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num()!=viewer_control->team_num()))
					next_outline = OUTLINE_NAMED;
		}
		else
		{
			// rule: A walker with several statuses blinks between their outlines, advancing one
			//   step per call from the outline it currently shows.
			// tests: WalkerCoreMore.walker_compute_outline_state_transitions
			// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
			// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
			// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
			// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
			// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
			// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
			// tests: WalkerMovement.walker_draw_with_invisibility
			// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
			if      (invisibility_left())
				next_outline = query_team_color();
			else if (flight_left())
				next_outline = OUTLINE_FLYING;
			else if (invulnerable_left())
				next_outline = OUTLINE_INVULNERABLE;
			else if (viewer_control)
				// rule: A walker with several statuses blinks between their outlines, advancing one
				//   step per call from the outline it currently shows.
				// tests: WalkerCoreMore.walker_compute_outline_state_transitions
				// tests: WalkerCoreMore.walker_round7a_compute_outline_and_friendliness_edge_paths
				// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
				// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
				// tests: WalkerUnit.walker_r15_compute_outline_and_next_frame_and_generate_paths
				// tests: WalkerMore.walker_myguy_move_and_weapon_heading_and_outline_named
				// tests: WalkerPathing.compute_outline_state_machine_and_draw_ages_damage_numbers
				// tests: WalkerMovement.walker_draw_with_invisibility
				// tests: SemWalker1.a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num()!=viewer_control->team_num()))
					next_outline = OUTLINE_NAMED;
		}
	}
	else
	{
	    next_outline = 0;
	}

    // Team-color "player control" marker for same-team OTHER players. Gated on
    // mark_player_controls: only genuine networked play opts in. In local
    // split-screen this stays off — co-players already have their own pane, and
    // because `outline` is a render-only value the network layer rewrites every
    // sim tick, leaving it on there made each player's own character flicker.
    // rule: In networked play only, another human player on your team is marked with your team
    //   colour outline when they show no status outline; local split-screen never marks co-players.
    // tests: WalkerCoreMore.compute_outline_player_control_marker_is_gated
    // tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
    // tests: SemWalker1.the_networked_teammate_marker_skips_bots_and_enemies
    if (mark_player_controls && next_outline == 0 && user() != -1 && viewer_control
        && this != viewer_control
        && this->team_num() == viewer_control->team_num())
    {
        next_outline = query_team_color();
    }
	set_outline(next_outline);
}

bool walker::act()
{
	short temp;

	// Make sure everyone we're pointing to is valid
	// rule: A walker forgets dead foes, dead leaders and dead owners at the start of its tick (a
	//   summon whose owner died becomes ownerless).
	// tests: CoverageMisc.coverage_r18_walker_act_decay_cleanup_and_guard_branches
	// tests: SemWalker1.a_walker_keeps_its_living_foe_leader_and_owner
	if (foe() && foe()->dead())
		set_foe(nullptr);
	if (leader() && leader()->dead())
		set_leader(nullptr);
	if (owner() && owner()->dead())
		set_owner(nullptr);

	set_collide_ob(nullptr); // always start with no collison..

	// Complete previous animations (like firing)
	// rule: While a walker plays a non-walk animation (attack swing, teleport, grow), the tick only
	//   advances the animation: no freeze countdown, no busy drain, no commands, no AI.
	// tests: SemWalker1.an_animating_walker_only_advances_its_animation
	if (ani_type() != ANI_WALK)
		return animate();

	// Are we frozen?
	// rule: A frozen walker does nothing for its tick except count one frozen tick down; it neither
	//   moves, drains busy, nor acts.
	// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
	// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
	if (stats_->frozen_delay())
	{
		stats_->set_frozen_delay(	stats_->frozen_delay() - 1);
		return 1;
	}

	// rule: Fire delay (busy) drains by one per walking, unfrozen tick.
	// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
	// tests: WalkerUnit.walker_r14_lines_518_557_563_602_607_outline_and_act_counters
	// tests: SemWalker1.classic_the_fire_delay_drains_only_while_positive_and_is_not_clamped
	if (busy() > 0.0f)
		set_busy(busy() - 1.0f); // This allows busy to be our FIRING delay.
	// Find new action

	// Turn if you want to
	//  if (curdir != enddir && query_order() == Order::Living)
	//       return turn(enddir);


	// No actions for us if we are ACT_CONTROL!
	//  if (act_type == ACT_CONTROL)
	//              stats_->clear_command();


	// Are we performing some action?
	// rule: A queued order (walk, search, attack, fire...) runs before the walker's own AI; when
	//   the order consumed the tick, nothing else happens this tick (no lunge/recoil decay, no
	//   act_type behaviour).
	// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
	// tests: SemWalker1.an_order_that_runs_takes_the_whole_tick
	if (stats_->has_commands())
	{
		temp = stats_->do_command();
		// rule: A queued order (walk, search, attack, fire...) runs before the walker's own AI.
		//   Here: `temp = do_command()`.
		// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
		// tests: SemWalker1.an_order_that_runs_takes_the_whole_tick
		if (temp)
			return 1;
	}
	
	// rule: The cosmetic attack lunge decays by 0.4 and hit recoil by 0.6 per acting tick, floored
	//   at 0 (render offsets only).
	// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
	// tests: CoverageMisc.coverage_r18_walker_act_decay_cleanup_and_guard_branches
	if(attack_lunge() > 0.0f)
    {
        float next_lunge = attack_lunge() - 0.4f;
        // rule: The cosmetic attack lunge decays by 0.4 and hit recoil by 0.6 per acting tick,
        //   floored at 0 (render offsets only). Here: `next < 0`.
        // tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
        // tests: CoverageMisc.coverage_r18_walker_act_decay_cleanup_and_guard_branches
        if(next_lunge < 0.0f)
            next_lunge = 0.0f;
        set_attack_lunge(next_lunge);
    }

	// rule: The cosmetic attack lunge decays by 0.4 and hit recoil by 0.6 per acting tick, floored
	//   at 0 (render offsets only). Here: `hit_recoil() > 0`.
	// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
	// tests: CoverageMisc.coverage_r18_walker_act_decay_cleanup_and_guard_branches
	if(hit_recoil() > 0.0f)
    {
        float next_recoil = hit_recoil() - 0.6f;
        // rule: The cosmetic attack lunge decays by 0.4 and hit recoil by 0.6 per acting tick,
        //   floored at 0 (render offsets only). Here: `next < 0`.
        // tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
        // tests: CoverageMisc.coverage_r18_walker_act_decay_cleanup_and_guard_branches
        if(next_recoil < 0.0f)
            next_recoil = 0.0f;
        set_hit_recoil(next_recoil);
    }

	// rule: What a walker does on a free tick depends on its act type: a player-controlled walker
	//   does nothing here (input drives it), a generator runs its spawn cadence, a projectile
	//   flies, a guard holds its post, ACT_DIE dies at once, ACT_RANDOM is the roaming bot
	//   (below); an unknown act type does nothing.
	// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
	// tests: WalkerUnit.walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths
	// tests: CoverageMisc.coverage_r18_walker_act_decay_cleanup_and_guard_branches
	// tests: WalkerUnit.walker_reset_compute_outline_and_act_paths
	// tests: SemWalker1.a_controlled_or_unknown_act_type_does_nothing_on_its_own
	switch (act_type())
	{
			// We are the control character
		case ACT_CONTROL:
			{
				return 1;

				//break;
			}
			// We are a generator
		case ACT_GENERATE:
			{
				act_generate();
				break;
			}
			// We are a weapon
		case ACT_FIRE:
			{
				act_fire();
				return 1;
				//break;
			}
		case ACT_GUARD:
			{
				act_guard();
				break;
			}
		case ACT_DIE:
			{
				this->set_dead(1);
				return 1;
			}
			// We are randomly walking toward enemy
		case ACT_RANDOM:
			{
				// rule: A roaming bot, on 1 tick in 80, tries its current special directly (no AI
				//   gate); if the special does not go off it wanders: a COMMAND_WALK of 0..29 ticks
				//   in a random one-of-9 direction. On the other 19 of those 20 ticks (1 in 4
				//   overall minus that) it runs act_random (living.cpp).
				// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
				// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
				// tests: SemWalker1.a_roaming_walker_that_casts_on_its_wander_tick_does_not_wander
				if (!current_game->world->rng_.next(4) )
				{
					// rule: A roaming bot, on 1 tick in 80, tries its current special directly (no
					//   AI gate). Here: `!rng(20)`.
					// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
					// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
					// tests: SemWalker1.a_roaming_walker_that_casts_on_its_wander_tick_does_not_wander
					if (!current_game->world->rng_.next(20))   // a 1 in 4 then 1 in 20 chance of rand walk
					{
						// rule: A roaming bot, on 1 tick in 80, tries its current special directly
						//   (no AI gate). Here: `!special()`.
						// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
						// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
						// tests: SemWalker1.a_roaming_walker_that_casts_on_its_wander_tick_does_not_wander
						if (!special())
							stats_->try_command(COMMAND_WALK, static_cast<std::int32_t>(current_game->world->rng_.next(30)),
							                   static_cast<std::int32_t>(current_game->world->rng_.next(3)-1), static_cast<std::int32_t>(current_game->world->rng_.next(3)-1));
						return 1;
					}
					act_random(); //1 in 4 followed by 19 in 20 of doing this
				}
				else    //3 of 4 times // rule: On 3 ticks in 4 a roaming bot without a foe acquires
					//   the nearest visible foe (living or generator, invisible foes usually
					//   skipped); with a foe in hand it queues a long hunt (COMMAND_SEARCH for 500
					//   ticks); with no foe at all it idles this tick.
					// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
					// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
					// tests: WalkerCoreMore.walker_round16_act_random_no_foe_far_search_fallback_path
					// tests: SemWalker1.a_roaming_walker_with_nobody_to_hunt_idles
				{
					// rule: On 3 ticks in 4 a roaming bot without a foe acquires the nearest
					//   visible foe (living or generator, invisible foes usually skipped).
					// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
					// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
					// tests: WalkerCoreMore.walker_round16_act_random_no_foe_far_search_fallback_path
					// tests: SemWalker1.a_roaming_walker_with_nobody_to_hunt_idles
					if (!foe())
					{
						set_foe(current_game->world->find_nearest_foe(this));
					}
					// rule: On 3 ticks in 4 a roaming bot without a foe acquires the nearest
					//   visible foe (living or generator, invisible foes usually skipped).
					// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
					// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
					// tests: WalkerCoreMore.walker_round16_act_random_no_foe_far_search_fallback_path
					// tests: SemWalker1.a_roaming_walker_with_nobody_to_hunt_idles
					if (foe())
						//stats_->try_command(COMMAND_SEARCH, 60, 0, 0);
						stats_->try_command(COMMAND_SEARCH, 500, 0, 0);
					return 1;
				}
			}  // END RANDOM
			break;
		default:
			{
				Log("No act type set.\n");
				return 0;
			}
	}  // END SWITCH
	return 0;
}

short walker::set_act_type(short num)
{
	set_old_act_type(act_type());
	set_act_type_state(static_cast<char>(num));
	return num;
}

short walker::restore_act_type()
{
	set_act_type_state(old_act_type());
	return old_act_type();
}


bool walker::collide(walker  *ob)
{
	set_collide_ob(ob);
	return 1;
}



bool walker::animate()
{
	// plumbing: animate(): no table, out-of-range facing/ani_type, ani_index past the family table
	//   (snapshot/save OOB guard), missing sequence, bad sequence length, negative cycle -> clamp
	//   or reset to walk.
	if (!ani)
		return 0;

	// Guard against stale/invalid signed-char indices under sanitizer builds.
	int dir_index = static_cast<int>(static_cast<unsigned char>(curdir()));
	// plumbing: animate(): no table, out-of-range facing/ani_type, ani_index past the family table
	//   (snapshot/save OOB guard), missing sequence, bad sequence length, negative cycle -> clamp
	//   or reset to walk.
	if (dir_index < 0 || dir_index >= NUM_FACINGS)
		dir_index = 0;
	int type_index = static_cast<int>(ani_type());
	// plumbing: animate(): no table, out-of-range facing/ani_type, ani_index past the family table
	//   (snapshot/save OOB guard), missing sequence, bad sequence length, negative cycle -> clamp
	//   or reset to walk.
	if (type_index < ANI_WALK || type_index > ANI_SLIME_SPLIT)
	{
		type_index = ANI_WALK;
		set_ani_type(static_cast<char>(ANI_WALK));
		set_cycle(0);
	}

	const int ani_index = dir_index + type_index * NUM_FACINGS;
	// For real entities the loader records this family's table length (ani_count).
	// Animation tables vary in length (most hold only ANI_WALK/ANI_ATTACK rows), so
	// an untrusted snapshot/save can set an ani_type the short table doesn't contain
	// and drive ani_index past the end. Treat that exactly like "no such sequence"
	// (reset to walk, stop) instead of reading out of bounds. ani_count is 0 only
	// for test-constructed walkers that assign `ani` directly; those keep the legacy
	// direct-index behavior.
	// plumbing: animate(): no table, out-of-range facing/ani_type, ani_index past the family table
	//   (snapshot/save OOB guard), missing sequence, bad sequence length, negative cycle -> clamp
	//   or reset to walk.
	if (ani_count > 0 && ani_index >= ani_count)
	{
		set_ani_type(ANI_WALK);
		set_cycle(0);
		return 0;
	}
	const signed char* seq = ani[ani_index];
	// plumbing: animate(): no table, out-of-range facing/ani_type, ani_index past the family table
	//   (snapshot/save OOB guard), missing sequence, bad sequence length, negative cycle -> clamp
	//   or reset to walk.
	if (!seq)
	{
		set_ani_type(ANI_WALK);
		set_cycle(0);
		return 0;
	}

	// Animation sequences are sentinel-terminated with -1, but `cycle` is a signed
	// char and can wrap/retain stale values. Compute a safe sequence length and
	// clamp `cycle` to avoid reading past the sentinel. (ASan found an OOB read
	// here during test-driven menu/game loops.)
	int seq_len = 0;
	// plumbing: animate(): sentinel scan for the sequence length (see above guards).
	while (seq_len < 128 && seq[seq_len] != -1)
		seq_len++;
	// plumbing: animate(): no table, out-of-range facing/ani_type, ani_index past the family table
	//   (snapshot/save OOB guard), missing sequence, bad sequence length, negative cycle -> clamp
	//   or reset to walk.
	if (seq_len <= 0 || seq_len >= 128)
	{
		set_ani_type(ANI_WALK);
		set_cycle(0);
		return 0;
	}

	int c = static_cast<int>(cycle());
	// plumbing: animate(): no table, out-of-range facing/ani_type, ani_index past the family table
	//   (snapshot/save OOB guard), missing sequence, bad sequence length, negative cycle -> clamp
	//   or reset to walk.
	if (c < 0)
		c = 0;

	// If cycle is already past the end (e.g. walk() advanced it beyond the
	// current animation's bounds), treat as end-of-animation rather than
	// restarting from frame() 0.  Resetting to 0 loops forever when walk()
	// keeps pushing cycle forward each frame() (attack-while-running bug).
	bool at_end;
	// rule: An animation whose cycle has already run past its last frame is treated as finished
	//   (its completion fires now) rather than restarting from frame 0, so an attack started while
	//   running still lands.
	// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
	// tests: WalkerCoreMore.walker_round18_animate_teleport_and_skelgrow_completion_paths
	// tests: WalkerMovement.walker_animate_attack
	if (c >= seq_len)
	{
		at_end = true;
		set_cycle(0);
	}
	else
	{
		set_frame(seq[c]);
		c++;
		at_end = (c >= seq_len);
		set_cycle(static_cast<signed char>(c));
	}

	// rule: The shot (or melee swing) leaves at the END of the attack animation: completing
	//   ANI_ATTACK calls fire(), then the walker returns to walking. Generators use the same path
	//   for spawns.
	// tests: GameplayFixesSoldier.reporter_charge_refused_by_a_foe_in_front_swings_only_with_the_setting_off
	// tests: WalkerMovement.walker_animate_attack, parity:weapon_knife_emission_scen99
	if (at_end)
	{
		//          if (ani_type == ANI_ATTACK &&
		//                        query_order() == Order::Living)
		// rule: The shot (or melee swing) leaves at the END of the attack animation: completing
		//   ANI_ATTACK calls fire(), then the walker returns to walking. Here: `ani_type() ==
		//   ANI_ATTACK`.
		// tests: GameplayFixesSoldier.reporter_charge_refused_by_a_foe_in_front_swings_only_with_the_setting_off
		// tests: WalkerMovement.walker_animate_attack, parity:weapon_knife_emission_scen99
		if (ani_type() == ANI_ATTACK)
		{
			fire();
			set_ani_type(ANI_WALK);
			set_cycle(0);
			return 1;
		}
		// rule: A family that is born from the ground (init animation SKEL_GROW, e.g. skeleton)
		//   starts walking once its grow animation finishes; other families completing SKEL_GROW
		//   fall through to their on_ani_complete hook.
		// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
		// tests: WalkerCoreMore.walker_round18_animate_teleport_and_skelgrow_completion_paths
		// tests: WalkerUnit.walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths
		if (ani_type() == ANI_SKEL_GROW && order() == Order::Living)
		{
			const auto* fd = get_family_descriptor(family());
			// rule: A family that is born from the ground (init animation SKEL_GROW, e.g. skeleton)
			//   starts walking once its grow animation finishes. Here: `fd->init_ani_type ==
			//   ANI_SKEL_GROW`.
			// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
			// tests: WalkerCoreMore.walker_round18_animate_teleport_and_skelgrow_completion_paths
			// tests: WalkerUnit.walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths
			if (fd && fd->init_ani_type == ANI_SKEL_GROW)
			{
				set_ani_type(ANI_WALK);
				set_cycle(0);
				return 1;
			}
		}
		// rule: When a teleport-out animation finishes, a family with a teleport handler (mage,
		//   archmage: random teleport; skeleton: ranged tunnel of level*18) reappears via TELE_IN;
		//   any other family just stops and walks.
		// tests: WalkerSpecials.mage_teleport_drives_simulation
		// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
		// tests: WalkerUnit.walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths
		// tests: WalkerCoreMore.walker_round18_animate_teleport_and_skelgrow_completion_paths
		// tests: SemWalker1.a_refused_teleport_leaves_the_walker_walking_where_it_stood
		if (ani_type() == ANI_TELE_OUT && order() == Order::Living)
		{
			const auto* fd = get_family_descriptor(family());
			// rule: When a teleport-out animation finishes, a family with a teleport handler (mage,
			//   archmage. Here: `handle_teleport(fd,this).value_or(false)`.
			// tests: WalkerSpecials.mage_teleport_drives_simulation
			// tests: WalkerUnit.walker_r11_act_and_animate_extra_cases
			// tests: WalkerUnit.walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths
			// tests: WalkerCoreMore.walker_round18_animate_teleport_and_skelgrow_completion_paths
			// tests: SemWalker1.a_refused_teleport_leaves_the_walker_walking_where_it_stood
			if (og::script::hooks::handle_teleport(fd, this).value_or(false))
				return 1;
			// Default: no teleport handler, just stop
			set_ani_type(ANI_WALK);
			set_cycle(0);
			return 0;
		}
		// rule: Any other finished living animation is offered to the family's on_ani_complete Lua
		//   hook (slime split into two small slimes, etc.); if the hook does not claim it the
		//   walker returns to walking.
		// tests: parity:special_slime_1_scen99, parity:ai_slime_split_scen99
		// tests: WalkerUnit.walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths
		// tests: SemWalker1.an_animation_the_hook_declines_returns_the_walker_to_walking
		if (order() == Order::Living)
		{
			const auto* fd2 = get_family_descriptor(family());
			// rule: Any other finished living animation is offered to the family's on_ani_complete
			//   Lua hook (slime split into two small slimes, etc.). Here:
			//   `on_ani_complete(fd2,this).value_or(false)`.
			// tests: parity:special_slime_1_scen99, parity:ai_slime_split_scen99
			// tests: WalkerUnit.walker_r14_lines_769_771_817_823_827_834_teleport_and_ani_complete_paths
			// tests: SemWalker1.an_animation_the_hook_declines_returns_the_walker_to_walking
			if (og::script::hooks::on_ani_complete(fd2, this).value_or(false))
				return 1;
		}

		set_ani_type(ANI_WALK);
		set_cycle(0);
	}
	return 1;
}

bool walker::set_frame_from_current_walk_animation()
{
	// rule: The walk-animation loop, run in the SIM on every walk step
	//   (walker_movement.cpp/357-360/369-370/379/478, living.cpp/533-534,
	//   sim_input_handler.cpp all bump cycle then call it): the frame shown is the walk row
	//   of the current facing at the current cycle, and a cycle that runs off the end of the WALK
	//   row wraps to 0. The wrap is always against the walk row ani[curdir], even during
	//   ANI_ATTACK, so walking while attacking bumps and wraps the snapshotted cycle that animate()
	//   (above) reads for at_end (the 'attack-while-running' interaction of
	//   above).
	// tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
	// tests: SimInputUnit.sim_input_r11_animate_movement_and_bit_animate_paths
	// tests: SemWalker1.the_walk_frame_helper_leaves_a_walker_without_a_walk_row_alone
	// tests: SemWalker1.classic_a_walk_step_during_an_attack_uses_the_walk_row
	if (!ani)
		return false;

	const int dir_index = static_cast<int>(curdir());
	// rule: The walk-animation loop, run in the SIM on every walk step
	//   (walker_movement.cpp/357-360/369-370/379/478, living.cpp/533-534,
	//   sim_input_handler.cpp all bump cycle then call it). Here: `dir_index out of range`.
	// tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
	// tests: SimInputUnit.sim_input_r11_animate_movement_and_bit_animate_paths
	// tests: SemWalker1.the_walk_frame_helper_leaves_a_walker_without_a_walk_row_alone
	// tests: SemWalker1.classic_a_walk_step_during_an_attack_uses_the_walk_row
	if (dir_index < 0 || dir_index >= NUM_FACINGS)
		return false;

	const signed char* seq = ani[dir_index];
	// rule: The walk-animation loop, run in the SIM on every walk step
	//   (walker_movement.cpp/357-360/369-370/379/478, living.cpp/533-534,
	//   sim_input_handler.cpp all bump cycle then call it). Here: `!seq`.
	// tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
	// tests: SimInputUnit.sim_input_r11_animate_movement_and_bit_animate_paths
	// tests: SemWalker1.the_walk_frame_helper_leaves_a_walker_without_a_walk_row_alone
	// tests: SemWalker1.classic_a_walk_step_during_an_attack_uses_the_walk_row
	if (!seq)
		return false;

	int seq_len = 0;
	// plumbing: set_frame_from_current_walk_animation: sentinel scan for length.
	while (seq_len < 128 && seq[seq_len] != -1)
		seq_len++;

	int c = static_cast<int>(cycle());
	// rule: The walk-animation loop, run in the SIM on every walk step
	//   (walker_movement.cpp/357-360/369-370/379/478, living.cpp/533-534,
	//   sim_input_handler.cpp all bump cycle then call it). Here: `c < 0 || c >= seq_len ->
	//   set_cycle(0)`.
	// tests: SimInputHandler.sim_input_yell_movement_and_idle_animate_branches
	// tests: SimInputUnit.sim_input_r11_animate_movement_and_bit_animate_paths
	// tests: SemWalker1.the_walk_frame_helper_leaves_a_walker_without_a_walk_row_alone
	// tests: SemWalker1.classic_a_walk_step_during_an_attack_uses_the_walk_row
	if (c < 0 || c >= seq_len)
	{
		set_cycle(0);
		c = 0;
	}

	set_frame(seq[c]);
	return true;
}

bool walker::set_order_family(Order neworder, char newfamily)
{
	set_order(neworder);
	set_family(newfamily);
	return 1;
}

void walker::configure_weapon_profile_base(walker* weapon, float heading_x,
                                           float heading_y)
{
	weapon->set_team_num(team_num());
	weapon->set_owner(this);
	weapon->set_floor(floor());
	weapon->set_difficulty(static_cast<std::uint32_t>(stats_->level()));
	weapon->set_damage(
		(weapon->damage() * (static_cast<float>(stats_->level()) + 3.0f)) /
		4.0f);
	// rule: Shot strength: every shot scales base damage by (level+3)/4 and gains level/3 range; a
	//   player guy's shot then adds strength/7 damage and strength/23 + dexterity/31 range, while a
	//   bot's (no guy) shot multiplies damage by its level instead.
	// tests: WalkerMore.walker_create_weapon_myguy_branch_vs_level_branch_and_cardinal_scaling
	if (myguy)
	{
		weapon->set_lineofsight(
			weapon->lineofsight() + (myguy->strength / 23) +
			(myguy->dexterity / 31));
		weapon->set_damage(weapon->damage() + (myguy->strength / 7.0f));
	}
	else
	{
		weapon->set_damage(weapon->damage() *
		                   static_cast<float>(stats_->level()));
	}
	weapon->set_lineofsight(weapon->lineofsight() + (stats_->level() / 3));
	// rule: Ranges are 'circular': a cardinal shot moves 362/256 (~1.414) times its stepsize per
	//   tick so it is as fast as a diagonal one, and its lineofsight is scaled by 309/256 (~1.207);
	//   diagonal shots keep the family values.
	// tests: WalkerMore.walker_create_weapon_myguy_branch_vs_level_branch_and_cardinal_scaling
	// tests: parity:weapon_knife_emission_scen99
	switch (facing(heading_x, heading_y)) // make 'circular' ranges
	{
		case FACE_UP:
		case FACE_RIGHT:
		case FACE_DOWN:
		case FACE_LEFT:
			// this will multiply by 1.207 ..
			weapon->set_lineofsight(scale_los_circular(weapon->lineofsight()));
			// this will multiply by 1.414
			weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);
			break;
		default:
			break;
	}
}

void walker::finalize_weapon_profile(walker* weapon, float heading_x,
                                     float heading_y)
{
	configure_weapon_profile_base(weapon, heading_x, heading_y);
	// rule: Only a living's family customize_weapon hook rewrites its shot (e.g. cleric glow-grow,
	//   archer/level hooks); non-living shooters keep the base profile.
	// tests: WalkerMore.walker_create_weapon_myguy_branch_vs_level_branch_and_cardinal_scaling
	// tests: ModesSoccer.recovery_range_conservatively_rejects_weapon_customization
	// unreachable: create_weapon sends generators down their own path and only livings and
	//   generators call fire()/fire_check(); no stock path has a weapon, effect or treasure create
	//   a shot, so the non-living arm of finalize_weapon_profile is never reached
	if (order() != Order::Living)
		return;

	const auto* fd = get_family_descriptor(family());
	og::script::hooks::customize_weapon(fd, this, weapon);
}

namespace
{
std::int32_t finalized_weapon_reach(const walker& weapon)
{
	return static_cast<std::int32_t>(weapon.stepsize()) *
	       static_cast<std::int32_t>(weapon.lineofsight());
}

bool weapon_profile_requires_hook(const walker& owner)
{
	const FamilyDescriptor* descriptor =
		get_family_descriptor(owner.family());
	// Family weapon behaviour is Lua-only: the registration mask below says it all.
	// rule: The mode AI's reach probe refuses to predict the range of any class whose shot is
	//   shaped or vetoed by Lua (customize_weapon or on_fire_weapon registered, or no descriptor at
	//   all), reporting reach 0 rather than guessing; it reads only the hook mask, never runs a
	//   hook.
	// tests: ModesSoccer.recovery_range_rejects_on_fire_hooks_without_dispatch
	// tests: ModesSoccer.recovery_range_conservatively_rejects_weapon_customization
	// tests: ModesSoccer.recovery_range_matches_every_ranged_core_class_profile
	// tests: SemWalker2.a_family_with_no_descriptor_has_no_predictable_reach
	if (descriptor == nullptr)
		return true;

	// This predicate is reached from a Lua method, so the current world's VM
	// is already active. Inspect only its registration mask: dispatching an
	// arbitrary weapon hook on a detached probe would not be pure. on_fire may
	// also veto the launch or rewrite the projectile after this profile step.
	const og::script::WorldScripts& scripts =
		og::script::active_world_scripts();
	return scripts.has_hook(Order::Living, owner.family(),
	                        og::script::FamilyHook::CustomizeWeapon) ||
	       scripts.has_hook(Order::Living, owner.family(),
	                        og::script::FamilyHook::OnFireWeapon);
}

bool footprint_touches_water(GameWorld& world, const walker& mover,
                             std::int32_t x, std::int32_t y)
{
	const std::int32_t right = x + mover.sizex() - 1;
	const std::int32_t bottom = y + mover.sizey() - 1;
	const std::int32_t tile_left = x / GRID_SIZE;
	const std::int32_t tile_top = y / GRID_SIZE;
	const std::int32_t tile_right = right / GRID_SIZE;
	const std::int32_t tile_bottom = bottom / GRID_SIZE;
	smoother& terrain = world.smoother_for_floor(mover.floor());
	// plumbing: footprint_touches_water: tile scan over the mover's footprint (see
	//   below).
	for (std::int32_t ty = tile_top; ty <= tile_bottom; ++ty)
	{
		// plumbing: footprint_touches_water: tile scan over the mover's footprint (see
		//   below).
		for (std::int32_t tx = tile_left; tx <= tile_right; ++tx)
		{
			// rule: A body touches water if any grid tile under its footprint at that position is
			//   water (on its own floor).
			// tests: ModesWaterRealCampaign.the_shoreline_veto_bends_for_flight_and_refuses_unreachable_objectives
			// tests: ModesWaterRealCampaign.causeway_recovery_selects_reachable_archer_and_shoots_from_dry_land
			if (terrain.query_genre_x_y(tx, ty) == TYPE_WATER)
				return true;
		}
	}
	return false;
}

bool water_blocks_mover(const walker& mover)
{
	const statistics* stats = mover.stats();
	// rule: Water stops walkers except ethereal ones, fliers (family flag or flight potion) and
	//   swimmers.
	// tests: ModesWaterRealCampaign.the_shoreline_veto_bends_for_flight_and_refuses_unreachable_objectives
	// tests: SemWalker2.water_stops_a_planned_approach_except_for_fliers_and_swimmers
	if (stats->query_bit_flags(BIT_ETHEREAL))
		return false;
	if (stats->query_bit_flags(BIT_FLYING) || mover.flight_left())
		return false;
	return !stats->query_bit_flags(BIT_SWIMMING);
}

std::int32_t step_along(std::int32_t from, std::int32_t direction,
                        std::int32_t amount)
{
	return from + direction * amount;
}
} // namespace

bool walker::query_prospective_weapon_reach(short xdelta, short ydelta,
                                             std::int32_t& reach)
{
	reach = 0;
	// rule: Only a living's reach can be predicted: reach = stepsize * lineofsight of the shot it
	//   would fire now in that direction, built on a detached probe that never enters the world.
	// tests: ModesSoccer.recovery_range_rejects_on_fire_hooks_without_dispatch
	// tests: ModesSoccer.recovery_range_distinguishes_invalid_from_valid_zero_profiles
	// tests: ModesSoccer.recovery_range_matches_every_ranged_core_class_profile
	// tests: ModesSoccer.recovery_range_probe_never_touches_the_world_obmap
	// tests: SemWalker2.only_a_living_in_a_world_has_a_predictable_reach
	if (query_order() != Order::Living || stats_ == nullptr ||
	    current_game == nullptr || current_game->world == nullptr ||
	    weapon_profile_requires_hook(*this))
	{
		return false;
	}

	weap probe(WeaponProfileProbeTag{});
	const PixieData* data = current_game->world->configure_existing_entity(
		probe, Order::Weapon,
		static_cast<std::int32_t>(static_cast<char>(current_weapon())));
	// rule: Only a living's reach can be predicted. Here: `data == nullptr`.
	// tests: ModesSoccer.recovery_range_rejects_on_fire_hooks_without_dispatch
	// tests: ModesSoccer.recovery_range_distinguishes_invalid_from_valid_zero_profiles
	// tests: ModesSoccer.recovery_range_matches_every_ranged_core_class_profile
	// tests: ModesSoccer.recovery_range_probe_never_touches_the_world_obmap
	// tests: SemWalker2.only_a_living_in_a_world_has_a_predictable_reach
	if (data == nullptr)
		return false;
	probe.set_data(*data);
	configure_weapon_profile_base(&probe, static_cast<float>(xdelta),
	                              static_cast<float>(ydelta));
	reach = finalized_weapon_reach(probe);
	return true;
}

std::int32_t walker::prospective_weapon_reach(short xdelta, short ydelta)
{
	std::int32_t reach = 0;
	(void)query_prospective_weapon_reach(xdelta, ydelta, reach);
	return reach;
}

bool walker::can_approach_weapon_range(const walker* objective)
{
	// rule: A bot can only plan to shoot an objective that is alive, present (not dormant or
	//   hidden) and on its own floor.
	// tests: ModesWaterRealCampaign.the_shoreline_veto_bends_for_flight_and_refuses_unreachable_objectives
	// tests: SemWalker2.a_bot_plans_no_shot_at_an_absent_dormant_or_hidden_objective
	if (objective == nullptr || objective->dead() || objective->dormant() || objective->hidden() ||
	    objective->floor() != floor() || query_order() != Order::Living ||
	    current_game == nullptr || current_game->world == nullptr)
	{
		return false;
	}

	std::int32_t x = xpos();
	std::int32_t y = ypos();
	const std::int32_t target_x = objective->xpos();
	const std::int32_t target_y = objective->ypos();
	const std::int32_t movement_step =
		static_cast<std::int32_t>(stepsize());
	const bool blocked_by_water = water_blocks_mover(*this);
	std::int32_t cardinal_reach = 0;
	std::int32_t diagonal_reach = 0;
	bool cardinal_profile_ready = false;
	bool diagonal_profile_ready = false;
	const std::int32_t initial_distance =
		std::abs(target_x - x) + std::abs(target_y - y);

	// A one-pixel fallback is the slowest real walkstep can advance after its
	// full family step is denied, so initial_distance + 1 is a hard bound.
	// COMMAND_ATTACK may next try a perpendicular slide. We deliberately do
	// not speculate about that stateful wall-follow route: rejecting at the
	// direct shoreline can omit a usable shooter, but cannot reserve one that
	// must enter water before reaching ordinary weapon range.
	// plumbing: can_approach_weapon_range: simulated approach, bounded by the initial Manhattan
	//   distance; rules below.
	for (std::int32_t attempt = 0; attempt <= initial_distance; ++attempt)
	{
		std::int32_t dx = target_x - x;
		std::int32_t dy = target_y - y;
		// rule: The approach is predicted the way a bot actually walks at a target: a target mostly
		//   along one axis (more than 3:1) is approached straight along that axis, otherwise
		//   diagonally.
		// tests: SemWalker2.a_planned_approach_goes_straight_past_three_to_one_and_diagonal_otherwise
		if (std::abs(dx) > std::abs(3 * dy))
			dy = 0;
		if (std::abs(dy) > std::abs(3 * dx))
			dx = 0;
		const short sx = static_cast<short>((dx > 0) - (dx < 0));
		const short sy = static_cast<short>((dy > 0) - (dy < 0));
		const bool cardinal = sx == 0 || sy == 0;
		// rule: Range is judged with the shot the bot would actually take from each step: cardinal
		//   shots use the cardinal profile (longer, circular scaling) and diagonal shots the
		//   diagonal one; an ineligible profile ends the plan with false.
		// tests: ModesSoccer.recovery_range_rejects_on_fire_hooks_without_dispatch
		// tests: ModesSoccer.recovery_range_distinguishes_invalid_from_valid_zero_profiles
		// tests: SemWalker2.a_planned_shot_uses_the_cardinal_or_diagonal_profile_of_its_heading
		std::int32_t& reach = cardinal ? cardinal_reach : diagonal_reach;
		bool& profile_ready = cardinal ? cardinal_profile_ready
		                               : diagonal_profile_ready;
		if (!profile_ready)
		{
			// rule: Range is judged with the shot the bot would actually take from each step. Here:
			//   `!query_prospective_weapon_reach`.
			// tests: ModesSoccer.recovery_range_rejects_on_fire_hooks_without_dispatch
			// tests: ModesSoccer.recovery_range_distinguishes_invalid_from_valid_zero_profiles
			// tests: SemWalker2.a_planned_shot_uses_the_cardinal_or_diagonal_profile_of_its_heading
			if (!query_prospective_weapon_reach(sx, sy, reach))
				return false;
			profile_ready = true;
		}
		const std::int32_t distance =
			std::abs(target_x - x) + std::abs(target_y - y);
		// rule: The plan succeeds as soon as the Manhattan distance is within the weapon's reach; a
		//   bot that cannot move (stepsize 0) or is already on the target point with no reach
		//   fails.
		// tests: ModesSoccer.recovery_range_distinguishes_invalid_from_valid_zero_profiles
		// tests: ModesWaterRealCampaign.the_shoreline_veto_bends_for_flight_and_refuses_unreachable_objectives
		if (distance <= reach)
			return true;
		if ((sx == 0 && sy == 0) || movement_step <= 0)
			return false;

		std::int32_t next_x = step_along(x, sx, movement_step);
		std::int32_t next_y = step_along(y, sy, movement_step);
		// rule: A walker that water blocks cannot plan a shot that requires wading: if its next
		//   full step and even a one-pixel step would enter water before it is in range, the plan
		//   fails at the shoreline.
		// tests: ModesWaterRealCampaign.causeway_recovery_selects_reachable_archer_and_shoots_from_dry_land
		// tests: ModesWaterRealCampaign.the_shoreline_veto_bends_for_flight_and_refuses_unreachable_objectives
		if (blocked_by_water &&
		    footprint_touches_water(*current_game->world, *this,
		                            next_x, next_y))
		{
			// walkstep's real second try is a one-pixel move. If even that
			// enters water, the bot has reached this approach's shoreline.
			next_x = step_along(x, sx, 1);
			next_y = step_along(y, sy, 1);
			// rule: A walking (non-flying, non-ethereal) bot plans its approach to an objective one
			//   real walkstep at a time; if its full family step would wet its footprint it falls
			//   back to the one-pixel step walkstep would take, and if even that touches water the
			//   bot has reached the shoreline and the objective is NOT approachable (no
			//   shooter/carrier is reserved for it). Continues the 1360 water gate in chunk c01.
			// tests: ModesWaterRealCampaign.the_shoreline_veto_bends_for_flight_and_refuses_unreachable_objectives
			// tests: ModesWaterRealCampaign.causeway_recovery_selects_reachable_archer_and_shoots_from_dry_land
			// tests: SemWalker2.a_planned_approach_creeps_one_pixel_to_the_shoreline
			if (footprint_touches_water(*current_game->world, *this,
			                            next_x, next_y))
			{
				return false;
			}
		}
		x = next_x;
		y = next_y;
	}
	return false;
}

walker  *walker::create_weapon()
{
	walker  *weapon;


	// Special case for generators
	// rule: What a shooter emits: a generator's 'weapon' is a LIVING troop of its default_weapon
	//   family, on the generator's team, owned by the generator, on its floor, with no difficulty
	//   scaling applied here (A12b: fire() applies set_difficulty once at the rolled level); every
	//   other walker emits an Order::Weapon of its current_weapon family with the finalized weapon
	//   profile (team, owner, floor, difficulty, damage scaling, myguy STR/DEX range).
	// tests: WalkerCoreMore.walker_generator_create_weapon_spawns_owned_living_unscaled
	// tests: DifficultyScaling.generator_spawn_gets_single_difficulty_application
	// tests: parity:generator_tent_emission_scen99, parity:generator_bones_emission_scen99
	// tests: WalkerMore.walker_create_weapon_myguy_branch_vs_level_branch_and_cardinal_scaling
	// tests: SemWalker2.a_living_shoots_its_current_weapon_scaled_by_its_heading
	if (query_order() == Order::Generator)
	{
		weapon = current_game->world->add_ob(
			Order::Living, static_cast<char>(default_weapon()));
		// plumbing: create_weapon: add_ob returned null (object table full) for the generator troop
		//   or the weapon -> no emission.
		if (!weapon) return nullptr;
		weapon->set_team_num(team_num());
		weapon->set_owner(this);
		weapon->set_floor(floor());  // summons spawn on the summoner's floor
		// A12b: no set_difficulty here. fire()'s Order::Generator branch rolls
		// the spawn's real level and applies set_difficulty once; the legacy
		// 2002 code ALSO applied it here at the generator's own level, so
		// every spawn compounded two additive hp/regen boosts and a squared
		// difficulty multiplier (e.g. genL=5 rolled L3 base-10hp: 384 vs the
		// intended 109). Deliberate semantic fix; affected parity goldens
		// regenerated (see commit).
		return weapon;
	}
	// Normally, only livings fire
	weapon = current_game->world->add_ob(
		Order::Weapon, static_cast<char>(current_weapon()));
	// plumbing: create_weapon: add_ob returned null (object table full) for the generator troop
	//   or the weapon -> no emission.
	if (!weapon) return nullptr;
	finalize_weapon_profile(weapon, lastx(), lasty());
	return weapon;
}

bool walker::query_next_to()
{
	float newx = static_cast<float>(xpos());
	float newy = static_cast<float>(ypos());

	// rule: 'Is a body right beside me in the direction I face': probe one body-width along the
	//   heading's x sign and one body-height along y; the y else-arm also covers lasty == 0, so an
	//   east/west-facing walker probes the diagonal cell ABOVE (classic quirk). Returns 1 when that
	//   spot is object-blocked.
	// tests: WalkerCoreMore.walker_query_next_to_and_generator_fire_check_paths
	// tests: WalkerCoreMore.walker_round17_query_next_to_and_fire_check_early_branches
	// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
	// unreachable: nothing in src/, packs/ or campaigns/ calls walker::query_next_to (tests only);
	//   the lastx<0 and lastx==0 probes have no game observable
	if (lastx() > 0.0f)
		newx += static_cast<float>(sizex());
	else if (lastx() < 0.0f)
		newx -= static_cast<float>(sizex());
	if (lasty() > 0.0f)
		newy += static_cast<float>(sizey());
	else //if (lasty < 0)
		newy -= static_cast<float>(sizey());

	// rule: 'Is a body right beside me in the direction I face'.
	// tests: WalkerCoreMore.walker_query_next_to_and_generator_fire_check_paths
	// tests: WalkerCoreMore.walker_round17_query_next_to_and_fire_check_early_branches
	// tests: WalkerUnit.walker_r11_fire_query_next_to_and_outline_branches
	if (!current_game->world->query_object_passable(newx, newy, this))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

bool walker::fire_check(short xdelta, short ydelta, FireCheckDenial* denial)
{
	walker  *weapon = nullptr;
	//  short newx=0, newy=0;
	std::int32_t i;
	std::int32_t distance;
	short targetdir;

	// Reports the denial stage to callers that ask (default: nobody).
	// Same checks, same order, same RNG draws as always.
	const auto deny = [denial](FireCheckDenial why) {
		// plumbing: fire_check's deny() lambda writes the denial stage only when the caller passed
		//   an out-param; no game decision.
		if (denial != nullptr)
			*denial = why;
	};
	deny(FireCheckDenial::None);

	// Allow generators to 'always' succeed
	// rule: A generator's fire check always succeeds: it never needs a foe, range, mana, facing or
	//   a clear ray to emit a troop (its cadence is act_generate's roll).
	// tests: WalkerCoreMore.walker_round17_query_next_to_and_fire_check_early_branches
	// tests: WalkerCombat.walker_fire_check_all_dirs
	// tests: WalkerCoreMore.walker_query_next_to_and_generator_fire_check_paths
	// tests: WalkerUnit.walker_death_save_all_and_misc_paths
	// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
	// tests: SemWalker2.a_generator_always_passes_fire_check_without_building_a_probe
	if (order() == Order::Generator)
		return 1;

	weapon = create_weapon();
	// plumbing: create_weapon() returned null (object table full / no configurator): the check
	//   fails.
	if (!weapon)
	{
		deny(FireCheckDenial::NoFoe);
		return 0;
	}
	set_weapon_heading(weapon); // set lastx, lasty based on our facing...
	weapon->set_collide_ob(nullptr);
	// Based on facing, we alter the weapon's proposed
	//   size so the collision check is fooled into checking
	//   a std::int32_t strip equal to the lineofsight times the size
	//   of the weapon.
	// rule: A shooter with no current foe never fires (the probe weapon is discarded).
	// tests: WalkerCombat.walker_fire_check_all_dirs
	if (!foe())     // nobody to fire at?
	{
		//Log("fire check, no foe.\n");
		//this does happen! but it appears harmless
		deny(FireCheckDenial::NoFoe);
		return 0;
	}

	// The reach gate runs before the NO_RANGED / magic gates so that a
	// NoRanged or NoMagic denial always means "the foe IS within this
	// weapon's reach" — the COMMAND_ATTACK melee loop keys off that to
	// face a bump-range foe instead of sliding around it (guard-standoff
	// fix, 2026-07-07). Every reordered gate returns the same 0 and no
	// gate consumes RNG, so all other callers see identical behavior.
	distance = distance_to_ob(foe());
	// rule: A shot is only taken at a foe within the weapon's reach (Manhattan distance <=
	//   finalized reach = the weapon's step x line of sight after the shooter's profile). The reach
	//   gate runs BEFORE the NO_RANGED and mana gates, so a NoRanged or NoMagic denial always means
	//   'the foe IS in reach' (COMMAND_ATTACK's guard-standoff fix keys off this to face a
	//   bump-range foe instead of sliding).
	// tests: ModesSoccer.fire_check_targets_only_live_hostile_ignored_foes
	// tests: WalkerCombat.walker_fire_check_all_dirs
	// tests: SemWalker2.a_foe_beyond_reach_is_out_of_range_before_any_other_denial
	if (distance > finalized_weapon_reach(*weapon))
	{
		weapon->set_dead(1);
		deny(FireCheckDenial::OutOfRange);
		return 0;
	}

	// rule: A NO_RANGED family (ghost, slimes, ...) never takes a ranged shot even at a foe in
	//   reach; the NoRanged denial tells COMMAND_ATTACK to face/bump instead.
	// tests: WalkerMore.walker_init_fire_and_fire_check_gate_branches
	// tests: WalkerCoreMore.walker_round17_query_next_to_and_fire_check_early_branches
	// tests: SemWalker2.a_no_ranged_shooter_is_denied_no_ranged_with_its_foe_in_reach
	if (stats_->query_bit_flags(BIT_NO_RANGED))
	{
		weapon->set_dead(1);
		deny(FireCheckDenial::NoRanged);
		return 0;
	}

	// rule: A shooter cannot fire a weapon whose mana cost exceeds its current magic points.
	// tests: WalkerMore.walker_init_fire_and_fire_check_gate_branches
	// tests: SemWalker2.a_shot_costing_more_than_the_magic_left_is_denied_no_magic
	if (stats_->weapon_cost() > stats_->magicpoints())
	{
		weapon->set_dead(1);
		deny(FireCheckDenial::NoMagic);
		return 0;
	}

	targetdir = facing(xdelta,ydelta);
	// rule: A shooter only fires along the direction it already faces; a foe in another of the 8
	//   directions is denied (Facing) and the caller must turn first (COMMAND_ATTACK snap-faces on
	//   this denial).
	// tests: WalkerCombat.walker_fire_check_all_dirs
	// tests: MeleeStandoff.facing_denial_snaps_to_face_the_foe_in_one_tick
	// tests: StatsCommands.stats_do_command_attack_without_foe_pops_and_faces_a_foe_it_cannot_shoot
	if (targetdir != curdir())
	{
		//         turn(targetdir);
		weapon->set_dead(1);
		deny(FireCheckDenial::Facing);
		return 0;
	}

	// Ignored entities deliberately stay out of the obmap. A mode may still
	// name one as the explicit attack objective (for example, a waterlogged
	// ball), so let the ordinary ray recognize that one hostile bounding box.
	// The normal range, mana, facing and terrain gates above/below remain the
	// authority; generic ignored effects remain non-colliding.
	// rule: Only a live, visible, same-floor, hostile ignored foe (a mode ball) is the fire ray's
	//   extra target; see the ray's hit test below.
	// tests: ModesSoccer.fire_check_targets_only_live_hostile_ignored_foes
	// tests: SemWalker2.a_hidden_ignored_foe_is_not_hit_by_the_fire_check_ray
	walker* const ignored_foe =
		foe()->ignore() && !foe()->dead() && !foe()->dormant() && !foe()->hidden() &&
		foe()->floor() == weapon->floor() && !weapon->is_friendly(foe())
			? foe()
			: nullptr;

	// Run weapon through where it would go if all went well ..
	// plumbing: fire_check's ray walk over the weapon's lineofsight steps (cumulative i*lastx,
	//   classic triangular stepping); rules below.
	for (i=0; i < weapon->lineofsight(); i++)
	{
		weapon->setxy(weapon->xpos() + static_cast<float>(i) * weapon->lastx(),
		              weapon->ypos() + static_cast<float>(i) * weapon->lasty());
		// rule: A shot whose straight flight would meet a wall (terrain the weapon cannot pass, per
		//   its own family e.g. FORESTWALK rocks pass trees) before any body is not taken.
		// tests: WalkerCombat.walker_fire_check_blocks_on_intermediate_step
		// tests: ModesSoccer.fire_check_targets_only_live_hostile_ignored_foes
		// tests: WalkerCombat.walker_fire_check_all_dirs
		if ( !current_game->world->query_grid_passable(weapon->xpos(), weapon->ypos(), weapon) )
		{
			// we hit a wall, so fail
			weapon->set_dead(1);
			deny(FireCheckDenial::WallBlocked);
			return 0;
		}
		// rule: An objective that lives outside the collision table (an ignored entity such as a
		//   mode ball) still counts as hit by the fire ray when it is the shooter's explicit, live,
		//   visible, same-floor, hostile foe; any other ignored entity stays non-colliding.
		// tests: ModesSoccer.fire_check_targets_only_live_hostile_ignored_foes
		// tests: SemWalker2.a_hidden_ignored_foe_is_not_hit_by_the_fire_check_ray
		if (ignored_foe != nullptr &&
		    ::collide(weapon->xpos(), weapon->ypos(),
		            weapon->sizex(), weapon->sizey(),
		            ignored_foe->xpos(), ignored_foe->ypos(),
		            ignored_foe->sizex(), ignored_foe->sizey()))
		{
			weapon->set_dead(1);
			return 1;
		}
		// rule: The ray succeeds at the first body that would stop the weapon (an enemy body;
		//   own-team bodies pass per the weapon's friendliness rules) — not necessarily the foe
		//   itself; if the whole line of sight runs out with nothing met, the shot is not taken
		//   (RayMiss).
		// tests: WalkerMore.walker_init_fire_and_fire_check_gate_branches
		// tests: WalkerCombat.walker_fire_check_all_dirs
		// tests: ModesSoccer.fire_check_targets_only_live_hostile_ignored_foes
		if ( !current_game->world->query_object_passable(weapon->xpos(), weapon->ypos(), weapon) )
		{
			// we hit an enemy, so good!
			weapon->set_dead(1);
			return 1;
		}
	}
	// By this point, we should have won or lost .. fail if we went our
	// range and didn't hit anyone ..
	weapon->set_dead(1);
	deny(FireCheckDenial::RayMiss);
	return 0;
}

/****************************************************
*
*  Act routines (static)
*
****************************************************/

bool
walker::act_generate()
{
	// Generator spawn-rate percent scales the cadence COMPARISON, not the
	// draw bounds: `level_draw * rate > threshold_draw * 100`. At the default
	// (100) both sides carry the same factor, so the outcome AND the whole
	// RNG stream are byte-identical to the legacy `level_draw >
	// threshold_draw` form. A non-default rate leaves every draw bound
	// untouched too, so there is no SimRandom::next(0) trap at any rate and
	// level-1 generators stay live on Calm (a scaled BOUND of level*3*50/100
	// == 1 could only ever draw 0 and never fire). uint64 keeps the products
	// exact even for crafted stats levels. Never changes the number or order
	// of rng_ draws.
	const std::uint64_t generator_rate_percent = query_generator_rate_percent();
	// rule: A generator spawns on a weighted roll each act: higher generator level spawns more
	//   often, a crowded world (more livings) spawns less often, the session's generator-rate
	//   percent (Calm 50 / Normal 100 / Frenzy 200, clamped 25..400; 0 = 100) scales the
	//   comparison, not the draw bounds (so the RNG stream is identical at every rate); no spawn at
	//   all once the world holds MAXOBS livings.
	// tests: WalkerCoreMore.generator_rate_200_spawns_more_than_rate_100_by_fixed_tick
	// tests: parity:generator_saturation_scen99
	// tests: WalkerCoreMore.walker_act_generate_zero_vector_and_hp_cap_paths
	// tests: SemWalker2.a_failed_emission_roll_still_draws_twice_and_a_full_world_draws_none
	if ( current_game->world->living_count < MAXOBS &&
	        (current_game->world->rng_.next(static_cast<std::uint32_t>(stats_->level() * 3)) * generator_rate_percent > current_game->world->rng_.next(static_cast<std::uint32_t>(300 + (current_game->world->living_count * 8))) * std::uint64_t{100} )
	   )
	{
		set_lastx(static_cast<float>(1 - static_cast<std::int32_t>(current_game->world->rng_.next(3))));
		set_lasty(static_cast<float>(1 - static_cast<std::int32_t>(current_game->world->rng_.next(3))));
		// rule: A generator's random spawn direction (each axis -1/0/1) is never the null vector:
		//   (0,0) becomes due east.
		// tests: WalkerCoreMore.walker_act_generate_zero_vector_and_hp_cap_paths
		// tests: SemWalker2.a_generator_emits_in_the_direction_it_rolled
		if (lastx() == 0.0f && lasty() == 0.0f)
			set_lastx(1.0f);
		init_fire(static_cast<short>(lastx()), static_cast<short>(lasty()));
		//    lastx = 0;
		//    lasty = 0;
		stats_->set_hitpoints(	stats_->hitpoints() + 1);
		// rule: Every successful spawn heals the generator by 1 hp, never above its max hp (with
		//   #178's max_hp = hp stamp a damaged post regenerates while it spawns; classic max_hp was
		//   0 for tower/bones/treehouse so the heal never took).
		// tests: DifficultyScaling.damaged_generator_regenerates_while_it_spawns
		// tests: WalkerCoreMore.walker_act_generate_zero_vector_and_hp_cap_paths
		// tests: WalkerCombat.walker_act_with_commands
		if (stats_->hitpoints() > stats_->max_hitpoints())
			stats_->set_hitpoints(	stats_->hitpoints() - 1);
	}
	return 1;
}

bool
walker::act_fire()
{
	// Z-axis: arc the projectile's height (visual) and drop it through "air"
	// holes to a lower floor (multi-floor). Strictly inert for flat weapons
	// (vz==0, worldz==0) on single-floor levels — worldz never affects the 2D
	// path and is not part of the parity dump.
	// plumbing: Null guards: no world / no weapon family descriptor (gravity 0).
	GameWorld* const zw = (current_game != nullptr) ? current_game->world : nullptr;
	// rule: An arcing projectile (a family with init vz, e.g. ROCK vz 0.7 gravity 0.09) rises and
	//   falls each tick by its family gravity and comes to rest on the floor plane (worldz clamps
	//   to 0, vz zeroed); flat weapons never change height. Height affects only the 3D cylinder
	//   collision overlap and rendering, not the 2D path or the parity dump.
	// tests: SemWalker2.a_lobbed_rock_arcs_lands_and_then_stays_on_the_ground
	if (vz() != 0.0f || worldz() != 0.0f)
	{
		set_worldz(worldz() + vz());
		const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(family());
		// plumbing: Null guards: no world / no weapon family descriptor (gravity 0).
		set_vz(vz() - (wfd != nullptr ? wfd->gravity : 0.0f));
		// rule: An arcing projectile (a family with init vz, e.g. ROCK vz 0.7 gravity 0.09) rises
		//   and falls each tick by its family gravity and comes to rest on the floor plane (worldz
		//   clamps to 0, vz zeroed).
		// tests: SemWalker2.a_lobbed_rock_arcs_lands_and_then_stays_on_the_ground
		if (worldz() < 0.0f)
		{
			set_worldz(0.0f);
			set_vz(0.0f);
		}
	}
	// rule: On a multi-floor level a projectile whose family can drop floors (ROCK) falls through
	//   an air tile to the floor below and keeps flying there; over air on floor 0 it is destroyed
	//   (death()). Families that cannot drop floors fly over air unaffected; single-floor levels
	//   never do this.
	// tests: ZAxis.a_projectile_over_air_drops_a_floor_and_dies_on_the_lowest_one
	// tests: SemWalker2.only_a_droppable_shot_falls_through_air_and_lands_at_height_0
	// tests: SemWalker2.a_single_floor_level_drops_no_shot_through_an_air_tile
	if (zw != nullptr && zw->floor_count() > 1 && !dead())
	{
		const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(family());
		// rule: On a multi-floor level a projectile whose family can drop floors (ROCK) falls
		//   through an air tile to the floor below and keeps flying there.
		// tests: ZAxis.a_projectile_over_air_drops_a_floor_and_dies_on_the_lowest_one
		// tests: SemWalker2.only_a_droppable_shot_falls_through_air_and_lands_at_height_0
		// tests: SemWalker2.a_single_floor_level_drops_no_shot_through_an_air_tile
		if (wfd != nullptr && wfd->can_drop_floors)
		{
			smoother& sm = zw->smoother_for_floor(floor());
			const std::int32_t g = sm.query_genre_x_y(
				(xpos() + sizex() / 2) / GRID_SIZE,
				(ypos() + sizey() / 2) / GRID_SIZE);
			// rule: On a multi-floor level a projectile whose family can drop floors (ROCK) falls
			//   through an air tile to the floor below and keeps flying there.
			// tests: ZAxis.a_projectile_over_air_drops_a_floor_and_dies_on_the_lowest_one
			// tests: SemWalker2.only_a_droppable_shot_falls_through_air_and_lands_at_height_0
			// tests: SemWalker2.a_single_floor_level_drops_no_shot_through_an_air_tile
			if (g == TYPE_AIR)
			{
				// rule: On a multi-floor level a projectile whose family can drop floors (ROCK)
				//   falls through an air tile to the floor below and keeps flying there.
				// tests: ZAxis.a_projectile_over_air_drops_a_floor_and_dies_on_the_lowest_one
				// tests: SemWalker2.only_a_droppable_shot_falls_through_air_and_lands_at_height_0
				// tests: SemWalker2.a_single_floor_level_drops_no_shot_through_an_air_tile
				if (floor() > 0)
				{
					change_floor(static_cast<short>(floor() - 1));
				}
				else
				{
					set_dead(1);
					death();
					return 1;
				}
			}
		}
	}

	const auto remaining_range = lineofsight();
	set_lineofsight(remaining_range - 1);
	// rule: A projectile's line of sight is its range in ticks: each act spends one, and the act
	//   that finds it already at 0 kills the weapon without moving (death() runs its family
	//   on_death, e.g. WAVE promotes to WAVE2, ROCK bounces).
	// tests: WalkerCombat.walker_act_with_commands, WeapBehavior.weap_act_fire
	// tests: parity:weapon_wave_promote_wave2_scen99
	// tests: WalkerMore.walker_act_fire_collision_arm_attacks_the_blocking_body
	// tests: parity:weapon_knife_emission_scen99
	// tests: SemWalker2.a_spent_projectile_dies_without_taking_its_step
	if (!remaining_range) // this is the range of the weapon
	{
		set_dead(1);
		death();
	}
	else if (!walk() || stats_->query_bit_flags(BIT_NO_COLLIDE))  // rule: A projectile that cannot take
		//   its step (it met a body or a wall), or a NO_COLLIDE projectile on every tick, strikes
		//   the live body its step met (attack: damage, score, effects); a wall or an already-dead
		//   body takes no hit. Cross-reference below: every stock NO_COLLIDE weapon
		//   family (wave/wave2/wave3/circle_protection) is also IMMORTAL, so the 'every tick' arm
		//   holds for stock content only because 1654 keeps them alive after the hit.
		// tests: WalkerMore.walker_act_fire_collision_arm_attacks_the_blocking_body
		// tests: WalkerCombat.walker_act_with_commands
		// tests: SemWalker2.a_shot_stopped_by_a_wall_or_a_corpse_strikes_nothing
	{
		// Hit the collide_ob;
		// rule: A projectile that cannot take its step (it met a body or a wall), or a NO_COLLIDE
		//   projectile on every tick, strikes the live body its step met (attack.
		// tests: WalkerMore.walker_act_fire_collision_arm_attacks_the_blocking_body
		// tests: WalkerCombat.walker_act_with_commands
		// tests: SemWalker2.a_shot_stopped_by_a_wall_or_a_corpse_strikes_nothing
		if (collide_ob() && !collide_ob()->dead())
		{
			attack(collide_ob());
		}
		// rule: An ordinary projectile is spent by its hit (dies); an IMMORTAL one (WAVE family,
		//   circles of protection) survives the hit and keeps flying until its range runs out. See
		//   above: the stock NO_COLLIDE weapons are all IMMORTAL, which is what lets them
		//   hit every tick.
		// tests: WalkerMore.walker_act_fire_collision_arm_attacks_the_blocking_body
		// tests: WalkerCombat.walker_act_with_commands
		if (!stats_->query_bit_flags(BIT_IMMORTAL))
		{
			set_dead(1);
			death();
		}
	}
	return 1;
}

bool
walker::act_guard()
{

	// Check all directions for foes
	//   if we find one, fire
	//              if (fire_check(lastx, lasty) ||
	//                       fire_check(lasty, lastx) ||
	//                       fire_check(-lasty, -lastx) ||
	//                       fire_check(-lastx, -lasty))
	set_foe(current_game->world->find_near_foe(this));
	// rule: A posted guard re-acquires the nearest foe every act (no range/sight limit) and,
	//   whenever it has one, tries a COMMAND_FIRE aimed at that foe (rng(30) duration; carries the
	//   foe delta so the facing gate can pass in any direction — 2026-08-11 directional guard fire,
	//   ungated); with no foe it does nothing (returns 0).
	// tests: WalkerCoreMore.walker_round6_act_guard_faces_wakes_and_fires_directionally
	// tests: ImaginationsTwitchers.hold_post_guard_fires_on_adjacent_east_enemy
	// tests: WalkerCombat.walker_act_with_commands
	// tests: SemWalker2.a_guard_draws_once_for_its_fire_command_and_never_without_a_foe
	if (foe())
	{
		const short xdist = foe()->xpos() - xpos();
		const short ydist = foe()->ypos() - ypos();
		// 2026-07-10 guard facing gate (docs/GAMEPLAY_FIXES_FROM_CLASSIC.md).
		// find_near_foe has no range or sight limit, so a guard posted
		// beside a tree band pivoted to face the 2D-nearest foe THROUGH the
		// wall — the "orc nose-to-trees" statue look (14% of all guard
		// ticks on Westlands L2). Gate ONLY the facing turn: foe
		// acquisition, the COMMAND_FIRE attempt, and every RNG draw are
		// unchanged; the guard just keeps its prior facing unless the foe
		// is within its family sight range AND the straight cell ray to it
		// is clear (both deterministic and RNG-free). Any foe the guard
		// could actually engage has a clear ray, so firing is unaffected.
		// rule: A guard only turns to face a foe it can genuinely see: within its family sight
		//   range AND with a clear straight cell ray (tree crowns/trunks and wall faces opaque;
		//   water and boulders transparent). Otherwise it keeps its prior facing (no
		//   nose-into-trees pivot). Facing uses face_delta (curdir, enddir and lastx/lasty all
		//   pinned). Ungated 2026-07-10 fix.
		// tests: GuardFacing.guard_does_not_pivot_to_face_foe_through_tree_band
		// tests: GuardFacing.guard_faces_foe_with_clear_sight
		// tests: GuardFacing.guard_faces_foe_across_water
		// tests: GuardFacing.guard_ignores_foe_beyond_sight_range
		if (distance_to_ob(foe()) <= lineofsight() * GRID_SIZE &&
		    current_game->world->clear_sight_line(this, foe()))
		{
			// 2026-08-11 (directional guard fire, see below): the facing
			// turn is face_delta, not a bare set_curdir. A posted guard
			// never walks, so curdir alone was UNDONE next tick by
			// living::act's pre-command turn toward the stale enddir (the
			// posted-guard pivot twitch), and its lastx/lasty — the heading
			// walker::fire() aims the released weapon by — stayed at the
			// spawn default, launching any shot that DID release due north.
			// face_delta pins all three channels; it consumes no RNG and
			// never moves the walker.
			face_delta(xdist, ydist);
			// 2026-07-11 guard wake rule (docs/GAMEPLAY_FIXES_FROM_CLASSIC.md).
			// Genuine sighting — the same deterministic range+ray test as
			// the facing gate — WAKES the guard: it converts to ACT_RANDOM
			// and fights like any other AI from the next tick on (this tick
			// still runs the classic face+fire below, so the wake itself
			// draws no extra RNG). A hold-post guard (npc_flags bit 1, the
			// per-guard policy selector) never converts: it is the classic
			// stationary sentry, used to pin posted NPCs in place.
			// rule: A guard that genuinely sights a foe (same test as above) WAKES: it
			//   converts to ACT_RANDOM and hunts from the next tick; a hold-post guard (npc_flags
			//   bit 1) never converts and stays a stationary sentry.
			// tests: GuardFacing.hold_post_guard_keeps_its_post_but_still_faces_and_fires
			// tests: GuardFacing.woken_guard_pursues_its_foe
			// tests: ImaginationsTwitchers.posted_hostiles_wake_and_attack_adjacent_enemy
			if (!guard_hold_post())
				set_act_type(ACT_RANDOM);
		}
		// 2026-08-11 directional guard fire (docs/GAMEPLAY_FIXES_FROM_CLASSIC.md).
		// The classic parting COMMAND_FIRE carried no direction, so
		// fire_check(0, 0) resolved facing UP and the facing gate denied
		// every shot except due north — a hold-post guard could not defend
		// its post from any other direction. Pass the foe delta the way
		// act_random does; the rng(30) draw (and the whole draw order) is
		// unchanged, the delta computation is RNG-free.
		stats_->try_command(COMMAND_FIRE,static_cast<std::int32_t>(current_game->world->rng_.next(30)), xdist, ydist);
		return 1;
	}
	else
		return 0;
}

bool
walker::act_random()
{
	short newx, newy;
	short xdist, ydist;

	// Specially put in to attempt to make enemy harder
	//if (current_game->world->rng_.next(sizex()/GRID_SIZE)) return 0;

	// Find our foe
	// rule: Base random-act AI: keep the current foe, but re-pick the nearest foe
	//   (find_nearest_foe) on a 1-in-70 roll or whenever it has none. Reached only by a plain
	//   walker (the generator class) whose act_type is ACT_RANDOM — livings override act_random
	//   (living.cpp) and weapons/treasure/effects have their own act(); no stock content does
	//   this.
	// tests: WalkerCombat.walker_act_random_generator_paths, WalkerCombat.walker_act_with_commands
	// tests: SemWalker2.a_wandering_walker_keeps_its_foe_unless_the_one_in_seventy_roll_hits
	if (!current_game->world->rng_.next(70) || (!foe()))
		set_foe(current_game->world->find_nearest_foe(this));
	// rule: With no foe anywhere the base AI wanders: try_random_walk(20). Reached only by a plain
	//   walker (the generator class) whose act_type is ACT_RANDOM — livings override act_random
	//   (living.cpp) and weapons/treasure/effects have their own act(); no stock content does
	//   this.
	// tests: SemWalker2.a_wandering_walker_with_no_foe_queues_a_twenty_tick_random_walk
	if (!foe())
		return stats_->try_random_walk(20);

	xdist = foe()->xpos() - xpos();
	ydist = foe()->ypos() - ypos();

	// If foe is in firing range, turn and fire
	// rule: Base AI fires when the foe is inside its square sight box and fire_check passes:
	//   init_fire plus COMMAND_FIRE for rng(24) ticks; otherwise it steps toward the foe. For a
	//   generator-class walker fire_check always passes (above), so an ACT_RANDOM
	//   generator spawns whenever a foe is in its sight box. Reached only by a plain walker (the
	//   generator class) whose act_type is ACT_RANDOM — livings override act_random
	//   (living.cpp) and weapons/treasure/effects have their own act(); no stock content does
	//   this.
	// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
	// tests: SemWalker2.a_wandering_walker_steps_toward_a_foe_outside_its_firing_box
	if (abs(xdist) < lineofsight() * GRID_SIZE &&
	        abs(ydist) < lineofsight() * GRID_SIZE)
	{
		// rule: Base AI fires when the foe is inside its square sight box and fire_check passes:
		//   init_fire plus COMMAND_FIRE for rng(24) ticks; otherwise it steps toward the foe.
		// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
		// tests: SemWalker2.a_wandering_walker_steps_toward_a_foe_outside_its_firing_box
		if (fire_check(xdist, ydist))
		{
			init_fire(xdist, ydist);
			stats_->set_command(COMMAND_FIRE, static_cast<std::int32_t>(current_game->world->rng_.next(24)), xdist, ydist);
			return 1;
		}
	}

	// Otherwise, try to walk toward foe
	newx = xdist;    // total horizontal distance..
	// rule: When not firing, the base AI walks one step along the sign of the foe delta on each
	//   axis (8-way approach, sliding handled by walkstep). Reached only by a plain walker (the
	//   generator class) whose act_type is ACT_RANDOM — livings override act_random
	//   (living.cpp) and weapons/treasure/effects have their own act(); no stock content does
	//   this.
	// tests: SemWalker2.a_wandering_walker_steps_toward_a_foe_outside_its_firing_box
	if (newx)                      // If it's not 0, then get
		newx = (newx > 0) ? 1 : -1;       // the normal of it..

	newy = ydist;
	// rule: When not firing, the base AI walks one step along the sign of the foe delta on each
	//   axis (8-way approach, sliding handled by walkstep).
	// tests: SemWalker2.a_wandering_walker_steps_toward_a_foe_outside_its_firing_box
	if (newy)
		newy = (newy > 0) ? 1 : -1;

	// If blocked
	set_collide_ob(nullptr);

	// We can slide now, so always just walkstep, NOT using
	// stepsize ..
	return walkstep(newx, newy);
	//    return 1;
}

// Returns the spaces 'clear' around us, out of a maximum
// of eight ..
short walker::spaces_clear()
{
	short count = 0;
	short i, j;

	// plumbing: spaces_clear's 3x3 neighbourhood iteration; rule below.
	for (i=-1; i < 2; i++)
		// plumbing: spaces_clear's 3x3 neighbourhood iteration; rule below.
		for (j=-1; j < 2; j++)
			// rule: Count how many of the 8 body-sized neighbour spots around a walker it could
			//   step into (terrain and bodies); the slime grows only when more than 7 are clear
			//   (living-08-slime.lua: spaces_clear() > 7 -> transform_to the grown family).
			// tests: WalkerCombat.walker_spaces_clear
			// tests: WalkerSpecials.small_slime_grow_drives_simulation
			// tests: SemWalker2.a_neighbour_spot_blocked_by_a_wall_or_a_body_is_not_clear
			if (i || j) // don't check our own location
				// rule: Count how many of the 8 body-sized neighbour spots around a walker it could
				//   step into (terrain and bodies).
				// tests: WalkerCombat.walker_spaces_clear
				// tests: WalkerSpecials.small_slime_grow_drives_simulation
				// tests: SemWalker2.a_neighbour_spot_blocked_by_a_wall_or_a_body_is_not_clear
				if (current_game->world->query_passable(
				        static_cast<float>(xpos() + (i * sizex())),
				        static_cast<float>(ypos() + (j * sizey())),
				        this))
					count++;

	return count;
}

void walker::transfer_stats(walker  *newob)
{
	short i;

	// First do the 'stats' stuff ..
	newob->stats()->set_hitpoints(stats_->hitpoints());
	newob->stats()->set_max_hitpoints(stats_->max_hitpoints());
	newob->stats()->set_heal_per_round(stats_->heal_per_round());
	newob->stats()->set_max_heal_delay(stats_->max_heal_delay());
	// Magic..
	newob->stats()->set_magicpoints(stats_->magicpoints());
	newob->stats()->set_max_magicpoints(stats_->max_magicpoints());
	newob->stats()->set_magic_per_round(stats_->magic_per_round()/2);
	newob->stats()->set_max_magic_delay(stats_->max_magic_delay());

	newob->stats()->set_level(stats_->level());
	newob->stats()->set_frozen_delay(stats_->frozen_delay_raw()); // raw: thaw immunity transfers with the body (§3.3)
	// plumbing: transfer_stats copies the five special_cost slots; plumbing of below's
	//   stat transfer.
	for (i=0; i < 5; i++)
		newob->stats()->set_special_cost(i, stats_->special_cost(i));
	newob->stats()->set_weapon_cost(stats_->weapon_cost());

	newob->stats()->set_bit_flags(stats_->bit_flags());
	newob->stats()->set_delete_me(stats_->delete_me());

	// Do we have a 'guy' ?
	// rule: When a body is replaced (slime split/grow, stain, cleric raise, promotion), the new
	//   body takes the old one's hp/max hp, regen, mp/max mp, HALF its magic regen, level, frozen
	//   delay (thaw immunity), special and weapon costs, bit flags, delete flag, and — when it was
	//   a saved character — a copy of its character record.
	// tests: WalkerCombat.walker_transfer_stats, WalkerCombat.walker_transfer_stats_with_guy
	// tests: SemWalker2.transferred_stats_copy_the_character_halve_magic_regen_and_keep_the_freeze
	if (myguy)
	{
		auto newguy = std::make_unique<guy>(*myguy);
		newob->set_owned_myguy(std::move(newguy));
	}
}

// change picture, etc. but NOT stats (use transfer_stats for that)

void walker::transform_to(Order whatorder, std::int32_t whatfamily)
{
	short xcenter, ycenter;
	short tempxpos, tempypos;
	short reset = 0;
	short tempact = act_type();

	// First remove us from the collision table..
	// plumbing: transform_to removes the walker from the obmap only when a world/obmap exists.
	if (current_game && current_game->world && current_game->world->myobmap)
		current_game->world->myobmap->remove(this);

	// rule: A transform within the same order (slime grow, WAVE->WAVE2, editor family swap) keeps
	//   the walker's act type; a transform across orders takes the new order's default act type. On
	//   EVERY transform (both arms) the walker's centre point is preserved across the size change
	//   (below) and its bit flags are cleared (below) before the new
	//   family's configuration is applied.
	// tests: WalkerCombat.walker_transform_to_same_order
	// tests: SemWalker2.a_transform_keeps_the_centre_resets_the_flags_and_takes_a_new_orders_act
	if (order() == whatorder) // same object type
	{
		reset = 1;
		tempact = act_type();
	}

	// Reset bit flags
	stats_->clear_bit_flags();

	// plumbing: transform_to without a world or when configure_existing_entity returns null
	//   returns early.
	if (current_game == nullptr || current_game->world == nullptr)
		return;

	// Do this before resetting graphic so illegal
	//  family() values don't try to set graphics.
	//  order() and family() are only set if legal
	const PixieData* data = current_game->world->configure_existing_entity(*this, whatorder, whatfamily);
	// plumbing: transform_to without a world or when configure_existing_entity returns null
	//   returns early.
	if (data == nullptr)
		return;

	// Reset the graphics
	const PixieData& new_data = *data;

	// Save center before resize (uses old sizex()/sizey())
	xcenter = xpos() + sizex()/2;
	ycenter = ypos() + sizey()/2;

	// Update sim fields + sync render component
	set_data(new_data);
	set_direct_frame(0);
	set_cycle(0);

	tempxpos = xcenter - sizex()/2;
	tempypos = ycenter - sizey()/2;


	// rule: A transform within the same order (slime grow, WAVE->WAVE2, editor family swap) keeps
	//   the walker's act type.
	// tests: WalkerCombat.walker_transform_to_same_order
	// tests: SemWalker2.a_transform_keeps_the_centre_resets_the_flags_and_takes_a_new_orders_act
	if (reset)
		set_act_type(tempact);

	setxy(tempxpos, tempypos);  // automatically re-adds us to the list ..
	// set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
	// Don't manually set the frame() here -- it can break circles
	// of protection, etc., which are special cases .. instead:
	set_frame(0);
	animate();
}


// death is called when an object dies (or weapon destructed, etc.)
// for special effects ..
bool walker::death()
{
	// Note that the 'dead()' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)
	walker  *newob = nullptr;
	std::int32_t i;

	// rule: A walker's death effects run once: a second death() call is a no-op (no second heart,
	//   stain, explosion, hook or event).
	// tests: WalkerDeath.double_call
	// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
	if (death_called())
		return 0;
	// New Specials: a possession ends here (the host frees its rider, or the
	// rider frees its host), and a REASSEMBLE ward cancels the death outright.
	// rule: New Specials: a death can be cancelled — a REASSEMBLE ward revives the skeleton instead
	//   of dying, and a possession ends here (a dying host frees its rider / a dying rider frees
	//   its host) — before any heart, stain or obmap removal.
	// tests: KitSeams.ward_revives_before_the_gem_and_the_obmap_removal
	if (current_game != nullptr && current_game->world != nullptr &&
	    og::sim::kit_on_death(*current_game->world, *this))
		return 0;

	set_death_called(1);
	fall_stories_ = 0; // a corpse's cascade ends here (classic respawn revives this same object)

	// Ensure we are removed from collision bookkeeping as soon as we "die".
	// This prevents stale pointers in the obmap when callers manage walker
	// lifetimes outside LevelRuntimeData's owning lists (common in tests).
	// plumbing: death() removes the corpse from the obmap when one exists (stale-pointer hygiene).
	obmap* active = (current_game != nullptr && current_game->world != nullptr)
	    ? current_game->world->myobmap.get()
	    : nullptr;
	if (active != nullptr)
		active->remove(this);

	// rule: A saved character (a walker with a character record) drops a life gem (heart) on death
	//   worth 37.5% of its heart value (75% halved because end-of-level score doubles), on its
	//   team, on the floor it died on, centred on its body; a non-character drops none.
	// tests: WalkerDeath.myguy_present, WalkerDeath.soldier_and_kin_drop_heart_and_bloodspot
	// tests: WalkerSpecials.character_death_drops_one_heart_and_one_stain
	if (myguy) // were we a real character?  Then make a heart ..
	{
			newob = current_game->world->add_ob(Order::Treasure, FAMILY_LIFE_GEM);
			// plumbing: add_ob for the life gem failed (object table full): no gem.
			if (newob)
			{
			newob->stats()->set_hitpoints(static_cast<float>(myguy->query_heart_value()));
			newob->stats()->set_hitpoints(
			    newob->stats()->hitpoints() * (0.75f / 2.0f));  // 75%, divided by 2, since score is doubled at end of level
			// Permadeath off (keep_fallen_heroes mirror on the world): the
			// fallen hero returns with their growth intact, so the gem is
			// consolation, not salvage — half value. Default 0 (permadeath
			// on) keeps the legacy full-value math byte-identical.
			// rule: With permadeath off (fallen heroes come back with their growth) the heart is
			//   consolation, not salvage: half value (18.75% of heart value). Permadeath on keeps
			//   the full 37.5%.
			// tests: WalkerDeath.myguy_present
			// tests: SimWorldHeadless.life_gem_halves_when_permadeath_is_off
			if (current_game->world->keep_fallen_heroes != 0)
				newob->stats()->set_hitpoints(
				    newob->stats()->hitpoints() * 0.5f);
			newob->set_team_num(team_num());
			newob->set_floor(floor());  // heart drops on the floor we died on (A8)
			newob->center_on(this);
			}
		}

	// rule: On a SAVE-ALL mission, the death of a protected walker fails the mission (EndGame
	//   event, a=SCEN_TYPE_SAVE_ALL, b=-1). Protected = when any placed NPC carries npc_flags bit
	//   2, exactly the flagged walkers (even while charmed); otherwise any NAMED living of the
	//   player's colour that is not a charmed foe. Summoned walkers (illusions, elementals, raised
	//   dead) never count. A mission-failing death returns before the corpse is handled: no stain,
	//   no family on_death, no level death hook (the heart, if any, still drops).
	// tests: WalkerUnit.walker_death_save_all_and_misc_paths
	// tests: SaveAllScoping.legacy_named_team0_death_still_fails_mission
	// tests: SaveAllScoping.protected_flag_scopes_the_watch_to_flagged_walkers
	// tests: SaveAllScoping.summoned_walker_death_never_fails_mission
	// tests: SaveAllScoping.do_summon_marks_the_conjured_walker
	// tests: SaveAllScoping.charmed_foe_death_never_fails_mission
	// tests: SaveAllScoping.foreign_color_company_hero_death_does_not_fail_mission
	// tests: SemWalker2.a_mission_losing_death_leaves_no_corpse_and_any_other_leaves_one
	// tests: SemWalker2.a_named_team_0_death_off_a_save_all_level_is_an_ordinary_death
	// tests: SemWalker2.a_mission_losing_death_skips_the_level_death_hook_and_any_other_runs_it
	switch (order())
	{
		case Order::Living:
			// SCEN_TYPE_SAVE_ALL mission-loss check. Two scopes (Wave F2):
			// summoned walkers (archmage "Phantom" illusions, elementals,
			// cleric raises) and charmed foes (real_team_num != 255) are
			// ammunition, never characters — their deaths never fail the
			// mission. When any placed NPC carries npc_flags bit 2
			// ("protected"), ONLY flagged walkers are watched; when none
			// does, the legacy rule applies (any named team-0 living).
			// (The charm exemption lives in the legacy branch only: a
			// protected walker stays mission-critical even while charmed.)
			// The 2002 inline gloss was simply "our team"; selectable roster
			// colors make that world.my_team, never hardcoded red or myguy.
			// rule: On a SAVE-ALL mission, the death of a protected walker fails the mission
			//   (EndGame event, a=SCEN_TYPE_SAVE_ALL, b=-1).
			// tests: WalkerUnit.walker_death_save_all_and_misc_paths
			// tests: SaveAllScoping.legacy_named_team0_death_still_fails_mission
			// tests: SaveAllScoping.protected_flag_scopes_the_watch_to_flagged_walkers
			// tests: SaveAllScoping.summoned_walker_death_never_fails_mission
			// tests: SaveAllScoping.do_summon_marks_the_conjured_walker
			// tests: SaveAllScoping.charmed_foe_death_never_fails_mission
			// tests: SaveAllScoping.foreign_color_company_hero_death_does_not_fail_mission
			// tests: SemWalker2.a_mission_losing_death_leaves_no_corpse_and_any_other_leaves_one
			// tests: SemWalker2.a_named_team_0_death_off_a_save_all_level_is_an_ordinary_death
			// tests: SemWalker2.a_mission_losing_death_skips_the_level_death_hook_and_any_other_runs_it
			if (   (current_game->world->type & SCEN_TYPE_SAVE_ALL)
			        && !summoned()               // conjured ammunition
			        && (current_game->world->has_save_all_protected()
			                ? save_all_protected()
				                : (team_num() == static_cast<unsigned char>(
				                       current_game->world->my_team) // protected side
				                   && real_team_num() == 255  // not a charmed foe
			                   && stats_->name.size()))   // we were named
			   )
			{
				// Emit EndGame event instead of calling screen directly.
				// The runtime layer handles this after the sim tick.
				og::sim::emit_event(current_game->sim_events, og::sim::EventKind::EndGame,
				                    SCEN_TYPE_SAVE_ALL, static_cast<std::uint32_t>(-1));
				return true;
			}
			{
				auto* fd = get_family_descriptor(family());
				// rule: A dying living's corpse: a family Lua on_death hook that answers takes over
				//   (slime splits, fire elemental explodes, ...); otherwise a family flagged
				//   leaves_bloodspot=false (ghost, skeleton, ...) leaves nothing; otherwise a blood
				//   stain (Order::Treasure FAMILY_STAIN, ignored, carrying the dead body's
				//   stats/order/family, team and floor) is left where it fell. The level-script
				//   death hook then runs in every non-mission-failing case.
				// tests: WalkerDeath.soldier_and_kin_drop_heart_and_bloodspot
				// tests: WalkerSpecials.character_death_drops_one_heart_and_one_stain
				// tests: WalkerDeath.ghost, WalkerDeath.skeleton, WalkerDeath.large_slime
				// tests: WalkerDeath.medium_slime
				if (og::script::hooks::on_death(fd, this).has_value())
				{
					// Scripted or family-specific death handling ran.
				}
				else if (fd && !fd->leaves_bloodspot)
				{
					// Ghost, skeleton, tower etc. -- no bloodspot
				}
				else
				{
					generate_bloodspot();
				}
				// Level-script per-entity / level-wide death hooks (inert
				// without registered level scripts).
				og::script::hooks::level_entity_death(this);
			}  // end of family() dispatch
			break;  // end of order() livings case
		// rule: A destroyed generator goes up in flames: four EXPLOSION effects on its team, at its
		//   level, damage 2 x level, on its floor, scattered inside its box (span clamped at 0 for
		//   sprites under 8px), each with an explosion sound placed at the generator; then the
		//   level-script death hook runs. The old game played the explosion sound only while the
		//   generator was on screen; each machine now plays it only where one of its views shows
		//   it (#445, #468).
		// tests: SimWorldHeadless.generator_death_scatter_clamps_undersized_sprite_span
		// tests: SimWorldHeadless.generator_death_scatter_unchanged_for_normal_sprites
		// tests: ZAxisFxFloor.generator_death_explosions_inherit_generator_floor
		// tests: WalkerCoreMore.walker_round7b_base_act_guard_random_and_death_paths
		// tests: LevelScriptsTest.generator_deaths_dispatch_entity_death
		// tests: SemPresentation.a_dying_generators_explosions_sound_at_the_generator
		case Order::Generator:  // go up in flames :>
			// plumbing: The four-explosion loop of the generator death; rule above.
			for (i=0; i < 4; i++)
			{
				newob = current_game->world->add_ob(Order::FX, FAMILY_EXPLOSION);
				// plumbing: Explosion add_ob failed (object table full): stop spawning explosions.
				if (!newob) // failsafe
					break;
				newob->set_team_num(team_num());
				newob->stats()->set_level(stats_->level());
				newob->set_ani_type(ANI_EXPLODE);
				newob->set_floor(floor());  // burn on the generator's floor (A8)
				newob->setxy(static_cast<std::uint32_t>(xpos())+current_game->world->rng_.next(static_cast<std::uint32_t>(std::max<int>(sizex()-8, 0)))+4, static_cast<std::uint32_t>(ypos()+4)+current_game->world->rng_.next(static_cast<std::uint32_t>(std::max<int>(sizey()-8, 0))) );  // sizex/sizey ride in on sprite metadata AND network snapshots; a sprite narrower than 8px made sizex()-8 negative, and IRandom::next takes uint32 -- so it wrapped to a ~4e9 scatter bound. Clamping the SPAN (never the coordinate) is a no-op at every size >= 8, where next(0) == 0 already pins the offset.
					newob->set_damage(static_cast<float>(stats_->level()) * 2.0f);
					newob->set_frame(static_cast<short>(current_game->world->rng_.next(3)));
				og::sim::emit_positional_sound(current_game->sim_events, this, SOUND_EXPLODE);
			}
			// Generators are the canonical scripted "object": a level script
			// must be able to react to a tent/tower/pillar falling, exactly
			// as it reacts to a living death. Emitted after the death FX so
			// the hook observes the same world a living-death hook would.
			og::script::hooks::level_entity_death(this);
			break;
		// plumbing: FX and every other order: nothing on death in walker::death (FX dying here only
		//   logs 'Effect dying in walker?'; effect/treasure/weap override death).
		case Order::FX:
			//case Order::Treasure:
			Log("Effect dying in walker?\n");
			break;          // end of effect object case
		// plumbing: FX and every other order: nothing on death in walker::death (FX dying here only
		//   logs 'Effect dying in walker?'; effect/treasure/weap override death).
		default:
			break;
	}  // end of switching orders

	return 1;
}

// Generates bloodspot for desired walker...
void walker::generate_bloodspot()
{
	walker  *bloodstain;
	//char  *data;
	// Make permanent stain:

	set_dead(1); // just in case ..

	bloodstain = current_game->world->add_fx_ob(Order::Treasure, FAMILY_STAIN);
	// plumbing: generate_bloodspot: add_fx_ob for the stain failed — no stain.
	if (!bloodstain) return;
	bloodstain->set_ignore(1);
	transfer_stats(bloodstain);

	bloodstain->set_order(Order::Treasure);
	bloodstain->set_family(FAMILY_STAIN);
	bloodstain->stats()->set_old_order(order());
	bloodstain->stats()->set_old_family(family());

	bloodstain->set_team_num(team_num());
	bloodstain->set_dead(0);
	bloodstain->set_floor(floor());  // stain marks the floor we died on (A8)
	bloodstain->setxy(xpos(), ypos());
	//data = myscreen->myloader->graphics[PIX(Order::Treasure, FAMILY_STAIN)];
	// We can't select other 'bloodspot' frames, because set_frame
	// appears to check the order() and family() and reset our picture
	// to a living guy .. we need to find a way around this ..
	bloodstain->set_frame(static_cast<short>(current_game->world->rng_.next(4)));  // has no effect yet ..
	bloodstain->set_ani_type(ANI_WALK);
	//bloodstain->bmp = (char *) (data+3); // our image

}

bool walker::eat_me(walker  * eater)
{
	// plumbing: walker::eat_me base: logs 'EATING A NON-TREASURE!' and returns 0; a non-treasure is
	//   never eaten (asserted WalkerUnit.walker_death_save_all_and_misc_paths).
	if (eater)
		Log("EATING A NON-TREASURE!\n");
	return 0;
}

// set_direct_frame is in src/runtime/walker_render_bridge.cpp.

walker* walker::do_summon(char whatfamily, std::int32_t summon_lifetime)
{
	// plumbing: walker::do_summon base: logs and returns nullptr; living overrides it.
	if (whatfamily || summon_lifetime)
		Log("Should not be hitting walker::do_summon!\n");
	return nullptr;
}

bool walker::check_special()
{
	Log("Should not be hitting walker::check_special\n");
	return 0;
}

// Delayed-spawn dormancy (see walker.h). Mirrors setxy's obmap bookkeeping:
// entering dormancy pulls the walker out of the collision table (it cannot be
// hit, collided with, or block movement), and waking re-registers it the same
// way a freshly spawned unit enters — an obmap registration at its spot.
void walker::set_dormant(bool value)
{
	// rule: A delayed-spawn (dormant) walker is out of the collision table: it cannot be hit,
	//   collided with or block movement; waking re-registers it at the spot it already occupies
	//   (unless it is an ignored entity, which never registers). Setting the same state twice
	//   changes nothing.
	// tests: NpcScenarioFlags.dormant_foe_cell_is_walk_through_until_wake
	// tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
	// tests: SemWalker2.waking_an_ignored_walker_keeps_it_out_and_a_repeat_changes_nothing
	if (dormant_ == value)
		return;
	dormant_ = value;

	// plumbing: set_dormant without a world/obmap: only the flag changes.
	obmap* map = (current_game != nullptr && current_game->world != nullptr)
	    ? current_game->world->myobmap.get()
	    : nullptr;
	if (map == nullptr)
		return;
	// rule: A delayed-spawn (dormant) walker is out of the collision table.
	// tests: NpcScenarioFlags.dormant_foe_cell_is_walk_through_until_wake
	// tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
	// tests: SemWalker2.waking_an_ignored_walker_keeps_it_out_and_a_repeat_changes_nothing
	if (dormant_)
		map->remove(this);
	else if (!ignore())
	{
		// add(), not move(): move() short-circuits when the coordinates are
		// unchanged, and a waking walker re-enters at the spot it already
		// occupies (add de-duplicates any stale registration first).
		map->add(this, xpos(), ypos());
	}
}

// Center us on target walker
// Relocate this entity to a different stacked floor, re-bucketing it in the
// single floor-keyed obmap (remove from the old floor's buckets, then re-add at
// the new floor). Lands on the new floor plane (worldz 0).
void walker::change_floor(short new_floor)
{
	// rule: Changing floor moves a walker into the new floor's collision buckets and onto that
	//   floor's plane (worldz 0); dormant, hidden and ignored walkers stay unregistered across the
	//   move. Asking for the floor it is already on changes nothing (worldz kept).
	// tests: ZAxis.collision_is_floor_separated
	// tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
	// tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
	// tests: SemWalker2.a_floor_change_lands_at_height_0_leaves_the_old_floor_and_same_floor_is_a_no_op
	if (new_floor == floor())
		return;
	// plumbing: change_floor null guards for world and obmap.
	GameWorld* w = (current_game != nullptr) ? current_game->world : nullptr;
	obmap* m = (w != nullptr) ? w->myobmap.get() : nullptr;
	if (m != nullptr)
		m->remove(this);
	set_floor(new_floor);
	set_worldz(0.0f);
	// rule: Changing floor moves a walker into the new floor's collision buckets and onto that
	//   floor's plane (worldz 0).
	// tests: ZAxis.collision_is_floor_separated
	// tests: NpcScenarioFlags.repositioning_a_dormant_walker_keeps_it_unregistered
	// tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
	// tests: SemWalker2.a_floor_change_lands_at_height_0_leaves_the_old_floor_and_same_floor_is_a_no_op
	if (m != nullptr && !ignore() && !dormant() && !hidden()) // dormant: stay unregistered
		m->add(this, xpos(), ypos());
}

// A5/B2: deterministic arrival nudge for cross-floor landings. When the
// aligned arrival spot on the target floor is blocked (wall top, or an
// occupant), probe outward from the walker's centre cell in a FIXED
// ring-by-ring order (row-major within each ring — no RNG, so replays and
// parity captures stay deterministic) for the nearest clear cell on the
// target floor, out to `radius` rings (A5 air falls use kFallNudgeRadius;
// B2 blocked stair arrivals use the tighter kStairNudgeRadius). `avoid_air`
// (stairs only) additionally skips PIX_AIR cells: air is grid-passable by
// design, but a stair arrival nudged onto it would immediately fall back to
// the source floor — an up-fall loop that defeats the ascent. Falls keep air
// candidates (cascading further down is their normal behavior). Performs the
// floor change + nudge (set_floor before setxy: the obmap is floor-keyed)
// and returns true when a cell is found.
inline constexpr std::int32_t kFallNudgeRadius = 4;
inline constexpr std::int32_t kStairNudgeRadius = 2;
static bool land_on_nearest_clear_cell(GameWorld& w, walker& faller,
                                       short target_floor,
                                       std::int32_t cx, std::int32_t cy,
                                       std::int32_t radius, bool avoid_air)
{
	const PixieData& g = w.grid_for_floor(target_floor);
	// plumbing: land_on_nearest_clear_cell: no grid for the target floor -> no landing; ring
	//   cells off the grid skipped.
	if (!g.valid())
		return false;
	// plumbing: Ring-by-ring, row-major scan of the landing nudge; rule below.
	for (std::int32_t r = 0; r <= radius; ++r)
	{
		// plumbing: Ring-by-ring, row-major scan of the landing nudge; rule below.
		for (std::int32_t dy = -r; dy <= r; ++dy)
		{
			// plumbing: Ring-by-ring, row-major scan of the landing nudge; rule below.
			for (std::int32_t dx = -r; dx <= r; ++dx)
			{
				// rule: A blocked cross-floor arrival is nudged to the NEAREST clear cell of the
				//   target floor, searched deterministically ring by ring outward (ring 0 = own
				//   cell, then ring 1 row-major from the north-west, ...) up to the radius (falls 4
				//   rings, stairs 2); a cell counts only if grid-passable and free of blocking
				//   bodies (no eating); stair arrivals additionally refuse air cells (no
				//   fall-straight-back loop). No RNG.
				// tests: ZAxis.air_fall_nudge_probes_rings_outward_deterministically
				// tests: ZAxis.stair_nudge_never_lands_on_air
				// tests: ZAxis.stair_up_into_occupied_cell_lands_beside_the_blocker
				// tests: ZAxis.air_fall_over_wall_lands_on_nearest_passable_cell
				if (std::max(std::abs(dx), std::abs(dy)) != r)
					continue; // ring perimeter only
				const std::int32_t nx = cx + dx;
				const std::int32_t ny = cy + dy;
				// plumbing: land_on_nearest_clear_cell: no grid for the target floor -> no
				//   landing; ring cells off the grid skipped.
				if (nx < 0 || ny < 0 || nx >= g.w || ny >= g.h)
					continue;
				// rule: A blocked cross-floor arrival is nudged to the NEAREST clear cell of the
				//   target floor, searched deterministically ring by ring outward (ring 0 = own
				//   cell, then ring 1 row-major from the north-west, ...) up to the radius (falls 4
				//   rings, stairs 2).
				// tests: ZAxis.air_fall_nudge_probes_rings_outward_deterministically
				// tests: ZAxis.stair_nudge_never_lands_on_air
				// tests: ZAxis.stair_up_into_occupied_cell_lands_beside_the_blocker
				// tests: ZAxis.air_fall_over_wall_lands_on_nearest_passable_cell
				if (avoid_air && g.data[static_cast<std::size_t>(nx + ny * g.w)] == PIX_AIR)
					continue;
				const std::int32_t px = nx * GRID_SIZE;
				const std::int32_t py = ny * GRID_SIZE;
				// rule: A blocked cross-floor arrival is nudged to the NEAREST clear cell of the
				//   target floor, searched deterministically ring by ring outward (ring 0 = own
				//   cell, then ring 1 row-major from the north-west, ...) up to the radius (falls 4
				//   rings, stairs 2).
				// tests: ZAxis.air_fall_nudge_probes_rings_outward_deterministically
				// tests: ZAxis.stair_nudge_never_lands_on_air
				// tests: ZAxis.stair_up_into_occupied_cell_lands_beside_the_blocker
				// tests: ZAxis.air_fall_over_wall_lands_on_nearest_passable_cell
				if (!w.floor_landing_clear(&faller, static_cast<float>(px),
				                           static_cast<float>(py),
				                           target_floor))
					continue;
				faller.change_floor(target_floor);
				faller.setxy(px, py);
				return true;
			}
		}
	}
	return false;
}

// B1: arm the stair re-trigger latch at the walker's CURRENT centre cell —
// called immediately after a stair transition moves the walker (aligned
// arrival keeps the departure cell; a B2 nudge lands elsewhere, so the cell is
// recomputed from the post-move position rather than assumed). See the
// z_stair_latched_ field comment in walker.h for the persistence contract
// (server-only transient, the z_cooldown_ precedent).
void walker::latch_stair_arrival()
{
	z_stair_latched_ = true;
	z_latch_cx_ = (xpos() + sizex() / 2) / GRID_SIZE;
	z_latch_cy_ = (ypos() + sizey() / 2) / GRID_SIZE;
}

// Fall-damage settle: called ONCE when an air cascade ends on a non-air cell
// of the arrival floor. The first story is free (campaign fall routes are
// designed traversal — the Westlands E5 fall-line rule); deeper falls cost
// min(15% x (stories-1), 50%) of max HP, clamped up to at least 1 hp, so a
// full-health unit survives ANY fall (a fall has no attributable attacker —
// no kill credit is possible — so an instant-kill would be a degenerate MP
// tactic; knockback-off-ledge stays a finisher, not a one-shot). Damage uses
// the flight-expiry idiom (living.cpp), NOT do_combat_damage: its FAMILY_HIT
// FX consumes one rng_.next(3) per landing, which would perturb every
// multifloor RNG stream for a cosmetic — this path draws ZERO RNG. Armor is
// deliberately NOT applied (percent-of-max already scales with toughness;
// stacking get_damage_reduction would double-scale). Invulnerability is
// deliberately RESPECTED — a divergence from the environmental precedents
// (pit death / flight expiry check nothing): the potion's promise is "no
// damage", and fall damage is damage; pit death stays unprotected (a void,
// not damage). See docs/z-axis-design.md.
void walker::resolve_fall_landing()
{
	const int extra = fall_stories_ - 1; // first story is free
	fall_stories_ = 0;                   // reset first, unconditionally
	// rule: The first story of a fall is free (designed traversal): a one-story cascade settles
	//   with no damage. The story counter is always reset to 0 at settle.
	// tests: ZAxis.fall_one_story_free, ZAxis.fall_two_stories_costs_15pct
	if (extra <= 0)
	{
		TRACE("zaxis", "fall settle: 1 story, free, hp %.2f",
		      static_cast<double>(stats_->hitpoints()));
		return;
	}
	// rule: Invulnerable walkers (invulnerability potion or innate INVINCIBLE) take no fall damage
	//   (unlike pit death, which still kills them).
	// tests: ZAxis.invulnerable_skip, ZAxis.fall_two_stories_costs_15pct
	if (stats_->query_bit_flags(BIT_INVINCIBLE) || invulnerable_left() != 0)
	{
		TRACE("zaxis", "fall settle: %d stories, invulnerable, no damage",
		      extra + 1);
		return;
	}
	float dmg = std::min(0.15f * static_cast<float>(extra), 0.50f) *
	            stats_->max_hitpoints();
	// rule: Fall damage is min(15% x (stories-1), 50%) of max hp, armour ignored, no RNG, and never
	//   rounds away to nothing: at least 1 hp. Damage sets regen_delay 50, shows a red damage
	//   number and plays SOUND_CLANG.
	// tests: ZAxis.a_fall_that_would_round_to_nothing_still_costs_one_hitpoint
	// tests: ZAxis.fall_two_stories_costs_15pct, ZAxis.fall_caps_at_50pct
	// tests: SemWalker3.a_fall_costs_fifteen_percent_of_max_hp_per_story_past_the_first
	if (dmg < 1.0f)
		dmg = 1.0f; // nonzero fall damage never rounds away to nothing
	set_last_hitpoints(stats_->hitpoints());
	stats_->set_hitpoints(stats_->hitpoints() - dmg);
	damage_numbers.push_back(DamageNumber(
	    static_cast<float>(xpos() + sizex() / 2), static_cast<float>(ypos()),
	    dmg, RED, current_game->world->tick_count_));
	set_regen_delay(50);
	// rule: A saved character's fall damage counts in its per-scenario damage-taken tally and
	//   lowers its lowest-hp watermark.
	// tests: SemWalker3.a_characters_fall_damage_counts_in_its_scenario_record
	if (myguy != nullptr)
	{
		myguy->scen_damage_taken += dmg;
		// rule: A saved character's fall damage counts in its per-scenario damage-taken tally and
		//   lowers its lowest-hp watermark.
		// tests: SemWalker3.a_characters_fall_damage_counts_in_its_scenario_record
		if (myguy->scen_min_hp > stats_->hitpoints())
			myguy->scen_min_hp = stats_->hitpoints();
	}
	og::sim::emit_positional_sound(current_game->sim_events, this, SOUND_CLANG);
	TRACE("zaxis", "fall settle: %d stories, dmg %.2f, hp %.2f", extra + 1,
	      static_cast<double>(dmg), static_cast<double>(stats_->hitpoints()));
	// rule: A fall can kill only an already-wounded walker (full health always survives, cap 50%);
	//   the death awards no score and no kill credit.
	// tests: ZAxis.lethal_when_wounded, ZAxis.knockback_off_ledge_kill, ZAxis.fall_caps_at_50pct
	// tests: SemWalker3.a_lethal_fall_credits_no_score_and_no_kill
	if (stats_->hitpoints() <= 0)
	{
		TRACE("zaxis", "fall lethal");
		set_dead(1);
		death(); // no score, no kill credit; unwinds via living::act's dead() gate
	}
}

// Per-tick vertical handling for stacked floors: fall through "air" tiles to the
// floor below, and take Z-stairs up/down. Strictly a no-op on single-floor
// levels (floor_count()<=1), so it never affects legacy levels or parity.
// Every floor change validates its destination with floor_landing_clear —
// unvalidated arrivals used to bounce climbers off occupied stairs (A2),
// entangle walkers in unresolvable obmap overlaps (A3), and let running
// walkers materialize inside walls/torches on the other floor (A4/A5).
void walker::apply_z_motion()
{
	// plumbing: apply_z_motion: world pointer null guard (ternary).
	GameWorld* w = (current_game != nullptr) ? current_game->world : nullptr;
	// rule: Single-floor levels have no vertical motion at all (no falls, no stairs) — legacy
	//   levels and parity are untouched.
	// tests: ZAxis.walker_falls_through_air, SemWalker3.a_single_floor_level_has_no_vertical_motion
	if (w == nullptr || w->floor_count() <= 1)
		return;
	// rule: Corpses and ignored entities never fall or take stairs.
	// tests: ZAxis.walker_falls_through_air, SemWalker3.a_corpse_or_an_ignored_walker_never_falls
	if (dead() || ignore())
		return;

	// rule: After a floor change (fall 2 ticks, stair 6 ticks, denied stair/hover 6 ticks) the
	//   walker waits out a cooldown before any further vertical change; this paces cascades one
	//   story per few ticks.
	// tests: ZAxis.fall_two_stories_costs_15pct
	// tests: ZAxis.stair_with_no_clear_arrival_within_radius_denied_until_clear
	// tests: SemWalker3.a_running_z_cooldown_holds_the_walker_on_its_floor
	if (z_cooldown_ > 0)
	{
		--z_cooldown_;
		return;
	}

	const bool flying = (stats_ != nullptr) &&
		(stats_->query_bit_flags(BIT_FLYING) || flight_left() != 0);

	smoother& sm = w->smoother_for_floor(floor());
	const std::int32_t cx = (xpos() + sizex() / 2) / GRID_SIZE;
	const std::int32_t cy = (ypos() + sizey() / 2) / GRID_SIZE;

	// B1: the stair re-trigger latch clears the tick the walker's centre
	// FULLY leaves the arrival cell — the same centre-cell criterion the
	// stair trigger below uses, so latch and trigger can never disagree.
	// rule: A walker that just arrived by stair stands on the paired stair tile but is not bounced
	//   back: the stair re-arms only once its centre leaves the arrival cell; stepping back on is
	//   deliberate and triggers normally.
	// tests: ZAxis.arrival_on_paired_stair_does_not_bounce_back
	// tests: ZAxis.leaving_the_stair_cell_rearms_the_transition
	// tests: ZAxis.nudged_arrival_leaves_the_paired_stair_armed
	if (z_stair_latched_ && (cx != z_latch_cx_ || cy != z_latch_cy_))
		z_stair_latched_ = false;

	const std::int32_t genre = sm.query_genre_x_y(cx, cy);

	// Fall-damage bookkeeping: any non-air footing (stairs, ordinary ground,
	// the hover-walk-off stale case) ends a cascade with no settle charge —
	// forgiving by design. See resolve_fall_landing above.
	// rule: Any non-air footing (stairs, ground, a hover that walked off) ends a fall cascade with
	//   no settle charge — forgiving by design.
	// tests: ZAxis.hover_walkoff_silent_reset
	// tests: ZAxis.stair_reset_next_fall_charges_only_its_own_stories
	// tests: ZAxis.cascade_no_partial_damage, ZAxis.fall_two_stories_costs_15pct
	if (genre != TYPE_AIR)
		fall_stories_ = 0;

	// Z-stairs: relocate one floor up/down (vertically aligned). Flyers never
	// change floors (per design).
	// rule: A non-flying walker whose centre is on a Z-stair is carried one floor up (UP tile, not
	//   on the top floor) or down (DOWN tile, not on floor 0), vertically aligned. Flyers
	//   (BIT_FLYING or a flight potion) never change floors. An UP tile on the top floor / DOWN on
	//   floor 0 does nothing (no cooldown).
	// tests: ZAxis.zstair_up_moves_to_floor_above
	// tests: ZAxis.leaving_the_stair_cell_rearms_the_transition
	// tests: SemWalker3.a_stair_with_no_floor_beyond_it_does_nothing_and_flyers_ignore_stairs
	if (genre == TYPE_ZSTAIRS && !flying)
	{
		// B1: freshly arrived — a stair transition lands you standing on the
		// vertically-aligned PAIRED stair tile, which used to bounce you
		// straight back once z_cooldown_ expired (even standing still, and
		// any tiny in-cell movement re-triggered it). Latched: no re-trigger
		// until the centre cell leaves the stair cell; stepping back on is a
		// deliberate act and triggers normally.
		// rule: A walker that just arrived by stair stands on the paired stair tile but is not
		//   bounced back.
		// tests: ZAxis.arrival_on_paired_stair_does_not_bounce_back
		// tests: ZAxis.leaving_the_stair_cell_rearms_the_transition
		// tests: ZAxis.nudged_arrival_leaves_the_paired_stair_armed
		if (z_stair_latched_)
			return;
		const std::int32_t tile = sm.query_x_y(cx, cy);
		short target = floor();
		// rule: A non-flying walker whose centre is on a Z-stair is carried one floor up (UP tile,
		//   not on the top floor) or down (DOWN tile, not on floor 0), vertically aligned.
		// tests: ZAxis.zstair_up_moves_to_floor_above
		// tests: ZAxis.leaving_the_stair_cell_rearms_the_transition
		// tests: SemWalker3.a_stair_with_no_floor_beyond_it_does_nothing_and_flyers_ignore_stairs
		if (tile == PIX_ZSTAIR_UP && (floor() + 1) < w->floor_count())
			target = static_cast<short>(floor() + 1);
		else if (tile == PIX_ZSTAIR_DOWN && floor() > 0)
			target = static_cast<short>(floor() - 1);
		// rule: A non-flying walker whose centre is on a Z-stair is carried one floor up (UP tile,
		//   not on the top floor) or down (DOWN tile, not on floor 0), vertically aligned.
		// tests: ZAxis.zstair_up_moves_to_floor_above
		// tests: ZAxis.leaving_the_stair_cell_rearms_the_transition
		// tests: SemWalker3.a_stair_with_no_floor_beyond_it_does_nothing_and_flyers_ignore_stairs
		if (target != floor())
		{
			// A2/A3/A4 + B2: the aligned landing spot on the target floor
			// must be grid-passable and free of blocking entities. When it
			// is blocked (e.g. a guard posted ON the paired stair tile), the
			// walker is nudged to the nearest clear cell of the target floor
			// within kStairNudgeRadius rings (deterministic spiral, passable
			// + obmap-clear, no-eat — the same probe as the A5 fall nudge),
			// so a blocker can no longer permanently seal a staircase. Only
			// when NO clear cell exists within the radius is the transition
			// DENIED — the walker stays on its current floor (free to walk
			// off the stair) and re-probes after the cooldown, taking the
			// stair as soon as the far side clears.
			// rule: A stair arrival lands aligned when the spot above/below is clear; when blocked
			//   (a guard on the paired stair) the climber is nudged to the nearest clear non-air
			//   cell within 2 rings, so no blocker can seal a staircase; only with nothing clear is
			//   the climb denied (stay, retry after cooldown 6). Every attempt (landed or denied)
			//   sets cooldown 6.
			// tests: ZAxis.stair_up_into_occupied_cell_lands_beside_the_blocker
			// tests: ZAxis.stair_with_no_clear_arrival_within_radius_denied_until_clear
			// tests: ZAxis.arrival_on_paired_stair_does_not_bounce_back
			// tests: ZAxis.nudged_arrival_leaves_the_paired_stair_armed
			// tests: ZAxis.running_over_stair_with_walled_destination_never_lands_in_walls
			// tests: SemWalker3.a_denied_stair_climb_retries_after_six_ticks
			if (w->floor_landing_clear(this, static_cast<float>(xpos()),
			                           static_cast<float>(ypos()), target))
			{
				change_floor(target);
				latch_stair_arrival();
			}
			else if (land_on_nearest_clear_cell(*w, *this, target, cx, cy,
			                                    kStairNudgeRadius,
			                                    /*avoid_air=*/true))
			{
				latch_stair_arrival();
			}
			z_cooldown_ = 6;
		}
		return;
	}

	// Air: non-flying livings fall to the floor below; falling past floor 0 is a
	// pit death. Weapons get their own fall via act_fire (vz/gravity).
	// rule: A non-flying, non-weapon walker over an air tile falls to the floor below; over air on
	//   floor 0 it falls out of the world — pit death, which even invulnerability does not prevent.
	//   Weapons fall by act_fire's own rule (above). The !dead() re-check is always true
	//   here (dead walkers returned above).
	// tests: ZAxis.walker_falls_through_air, ZAxis.flyer_hovers_over_air, ZAxis.pit_death_unchanged
	// tests: ZAxis.flyer_exempt_hp
	if (genre == TYPE_AIR && !flying && order() != Order::Weapon)
	{
		// rule: A non-flying, non-weapon walker over an air tile falls to the floor below.
		// tests: ZAxis.walker_falls_through_air, ZAxis.flyer_hovers_over_air
		// tests: ZAxis.pit_death_unchanged, ZAxis.flyer_exempt_hp
		if (floor() > 0)
		{
			const short below = static_cast<short>(floor() - 1);
			bool landed = false;
			// rule: A faller lands straight down when that spot is clear, otherwise at the nearest
			//   clear cell within 4 rings of the floor below (never inside a wall top or a body);
			//   with nothing clear it does not fall at all — it hovers on the air tile and
			//   re-probes after 6 ticks. A successful fall sets cooldown 2.
			// tests: ZAxis.walker_falls_through_air
			// tests: ZAxis.air_fall_over_wall_lands_on_nearest_passable_cell
			// tests: ZAxis.air_fall_nudge_probes_rings_outward_deterministically
			// tests: ZAxis.air_fall_with_no_clear_landing_hovers_instead_of_falling
			// tests: SemWalker3.a_fall_lands_in_place_and_paces_two_ticks_and_a_hover_six
			if (w->floor_landing_clear(this, static_cast<float>(xpos()),
			                           static_cast<float>(ypos()), below))
			{
				// The spot straight down is clear: land in place.
				change_floor(below);
				z_cooldown_ = 2;
				landed = true;
			}
			else if (land_on_nearest_clear_cell(*w, *this, below, cx, cy,
			                                    kFallNudgeRadius,
			                                    /*avoid_air=*/false))
			{
				// A5: straight down is a wall top / occupied — nudged to
				// the nearest clear cell of the floor below instead of
				// materializing inside the blockage.
				z_cooldown_ = 2;
				landed = true;
			}
			else
			{
				// No clear landing within the nudge radius: do not fall
				// at all (hover on the air tile) and re-probe later.
				z_cooldown_ = 6;
			}
			// rule: Each story fallen adds one to the cascade; the cascade settles (fall damage
			//   charged once for the whole drop) only on reaching non-air footing, probed at the
			//   post-nudge centre cell; landing on more air keeps falling without partial charges.
			// tests: ZAxis.cascade_no_partial_damage, ZAxis.fall_two_stories_costs_15pct
			if (landed)
			{
				// One story fallen. Settle-probe the RECOMPUTED post-move
				// centre cell (the A5 nudge moves up to 4 cells): non-air
				// settles the cascade here; air keeps accumulating after
				// z_cooldown_ (the cascade continues).
				++fall_stories_;
				const std::int32_t lcx = (xpos() + sizex() / 2) / GRID_SIZE;
				const std::int32_t lcy = (ypos() + sizey() / 2) / GRID_SIZE;
				// rule: Each story fallen adds one to the cascade.
				// tests: ZAxis.cascade_no_partial_damage, ZAxis.fall_two_stories_costs_15pct
				if (w->smoother_for_floor(floor()).query_genre_x_y(lcx, lcy) !=
				    TYPE_AIR)
					resolve_fall_landing();
			}
		}
		else if (!dead())
		{
			set_dead(1);
			death();
		}
	}
}

void walker::center_on(walker  *target)
{
	// First get the center of our target ..
	const std::int32_t newx = target->xpos() + target->sizex()/2 - sizex()/2;
	const std::int32_t newy = target->ypos() + target->sizey()/2 - sizey()/2;

	// Now set our position ..
	setxy(newx, newy);
}

void walker::set_difficulty(std::uint32_t whatlevel)
{
	std::uint32_t temp, dif1;

	dif1 = query_difficulty_percent();

	// rule: A generator's hit points are 100 x level scaled by the difficulty percent, and its max
	//   hp is stamped to the same value (#178: it fights, regenerates and draws its hp bar against
	//   a real denominator; classic left max hp at the loader base, 0 for tower/bones/treehouse).
	// tests: DifficultyScaling.generator_set_difficulty_stamps_hp_and_its_denominator
	// tests: WalkerMore.walker_base_hooks_and_set_difficulty_scaling
	switch (order())
	{
		case Order::Generator:
			temp = 100*whatlevel;
			temp = (temp * dif1) / 100;
			stats_->set_hitpoints(static_cast<float>(temp));
			// A generator's fighting HP is also its denominator. Classic set
			// hitpoints alone and left max_hitpoints on the loader base — 0
			// for tower/bones/treehouse — so every generator ran at hp >
			// max_hp: act_generate's per-spawn regen could never take, the HP
			// bar had no fraction to draw, and an hp = max_hp restore emptied
			// the post instead of filling it.
			stats_->set_max_hitpoints(static_cast<float>(temp));
			break;
		// rule: Non-living, non-generator entities (shots at creation via configure_weapon_profile_base,
		//   treasures, FX) scale max hp, max mp and damage by the difficulty percent off team 0; a
		//   team-0 shot of a Living with no player character does too (#473), never at 100%.
		// tests: WalkerMore.walker_base_hooks_and_set_difficulty_scaling, DifficultyScaling.a_team0_npcs_arrow_scales_with_its_body
		// tests: DifficultyScaling.a_player_characters_arrow_never_scales, DifficultyScaling.an_unowned_team0_weapon_never_scales
		default:  // adjust standard settings for the rest ..
			// rule: Off team 0 everything takes the percent; a team-0 shot takes it with its shooter's body.
			// tests: WalkerMore.walker_base_hooks_and_set_difficulty_scaling, DifficultyScaling.a_team0_npcs_arrow_scales_with_its_body
			// tests: DifficultyScaling.a_player_characters_arrow_never_scales, DifficultyScaling.an_unowned_team0_weapon_never_scales
			if (team_num() != 0 || og::sim::team0_shot_takes_difficulty(*this, dif1))  // do all EXCEPT player characters
			{
				const float dif = static_cast<float>(dif1);
				stats_->set_max_hitpoints((stats_->max_hitpoints() * dif) / 100.0f);
				stats_->set_max_magicpoints((stats_->max_magicpoints() * dif) / 100.0f);
				set_damage((damage() * dif) / 100.0f);
			}
			break;
	}

	return;
}

std::int32_t walker::distance_to_ob(const walker  * target) const
{
	//std::int32_t xdelta,ydelta;

	//xdelta = static_cast<std::int32_t>(target->xpos() - xpos()) +
	//         static_cast<std::int32_t>( (target->sizex() - sizex()) / 2 );
	//ydelta = static_cast<std::int32_t>(target->ypos() - ypos()) +
	//         static_cast<std::int32_t>( (target->sizey() - sizey()) / 2 );
	//return static_cast<std::int32_t>(xdelta*xdelta + ydelta*ydelta);
	return ( abs(target->xpos() - xpos()) + abs(target->ypos() - ypos()) );

}

std::int32_t walker::distance_to_ob_center(const walker * target) const
{
	std::int32_t xdelta,ydelta;

	xdelta = static_cast<std::int32_t>(target->xpos() - xpos()) +
	         static_cast<std::int32_t>( (target->sizex() - sizex()) / 2 );
	ydelta = static_cast<std::int32_t>(target->ypos() - ypos()) +
	         static_cast<std::int32_t>( (target->sizey() - sizey()) / 2 );
	return static_cast<std::int32_t>(xdelta*xdelta + ydelta*ydelta);
}

unsigned char walker::query_team_color() const
{
	// Debugging ..
	//if (foe && !foe->dead())
	return og::sim::team_ramp_base(team_num());
	//else
	//  return static_cast<unsigned char>(7*16 + 40);
}

std::int32_t walker::is_friendly(const walker *target) const
{
	// is_friendly determines if _target_ is "friendly"
	// towards this walker.
	const walker *headus, *headtarget;

	// In case we're passed a null pointer somehow,
	// we're always unfriendly :)
	// plumbing: is_friendly(nullptr) -> 0 (unfriendly).
	if (target == nullptr)
		return 0;
	// If either of us is dead(), we're also unfriendly :)
	// rule: Nothing is friendly with a corpse: if either walker is dead they are treated as
	//   unfriendly (a dead walker's projectiles/bodies no longer pass friendly-through).
	// tests: WalkerMore.walker_round6_friendliness_null_dead_owner_chain_and_allied_modes
	// tests: SemWalker3.nothing_is_friendly_with_a_corpse_either_way
	if (dead() || target->dead())
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	// rule: Alliance is decided by team colour of the heads of both owner chains (weapon -> summon
	//   -> caster), each read from walker::alliance_head; saved-character status and seats never
	//   make different colours friendly. Classic: the climb stops at a dead owner (the walker
	//   stands for itself) and at a self-loop, and passes through a charm, so a charmed troop
	//   answers for its (uncharmed) owner's side. With Gameplay Fixes on it is the one climb (fix
	//   AP): through a dead owner, stopping at a self-loop and at a charmed walker (fix AD), so a
	//   charmed tent troop or summon fights for the colour it wears, and a dead caster's summon
	//   or shot keeps the caster's side.
	// tests: GameplayFixesCharm.a_charmed_tent_troop_fights_for_its_charmer
	// tests: GameplayFixesCharm.a_mind_controlled_tent_troop_fights_for_its_master
	// tests: GameplayFixesCharm.a_charmed_summon_of_an_enemy_caster_fights_for_its_charmer
	// tests: GameplayFixesCharm.an_uncharmed_tent_troop_answers_for_its_tent_on_both_values
	// tests: WalkerMore.walker_round6_friendliness_null_dead_owner_chain_and_allied_modes
	// tests: GameplayFixesChain.a_dead_casters_summon_keeps_its_side_for_friendliness_and_score
	headus = alliance_head();
	// Now our target ..
	headtarget = target->alliance_head();

	// The 2002 code narrated its old split as "Now, if we or the target don't
	// contain a 'myguy' pointer, then we don't care about allied_mode," then
	// concluded that in friendly mode "everyone with a 'myguy' pointer ... is
	// friendly to each other." Those lines remain as history; Company Base Camp
	// made the saved-character test an invalid alliance rule.
	//
	// Team color is the single combat-alliance authority. `myguy` means that
	// a walker persists in a company save; it must never make different colors
	// friendly. Likewise, Base Camp seat assignments choose a player's control
	// team; they never make a red walker friendly to a yellow walker. Following
	// owner chains keeps weapons, summons, and effects on their living owner's
	// current team; with Gameplay Fixes AD a charmed walker heads its own chain,
	// and with AP a dead owner's summon or shot keeps its owner's side.
	return headus->team_num() == headtarget->team_num();
}

std::int32_t walker::is_friendly_to_team(unsigned char team) const
{
	// is_friendly_to_team determines if _team_ is "friendly"
	// towards this walker.
	const walker *headus;
	
	// If dead(), we're also unfriendly :)
	// rule: A dead walker is friendly to no team (same rule as above): completion,
	//   enemy-freeze and respawn decisions never count a corpse for any side.
	// tests: WalkerMore.walker_round6_friendliness_null_dead_owner_chain_and_allied_modes
	// tests: WalkerMore.walker_friendliness_null_dead_and_strict_team_paths
	if (dead())
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	// rule: Same alliance rule as above against a bare team number, used by level
	//   completion ('foes remain'), enemy freeze and respawn: a walker counts for the team its
	//   chain head wears (walker::alliance_head: with fixes on the one climb, through a dead owner
	//   and stopping at a charmed head).
	// tests: GameplayFixesCharm.an_uncharmed_tent_troop_answers_for_its_tent_on_both_values
	// tests: GameplayFixesCharm.a_charmed_tent_troop_fights_for_its_charmer
	// tests: GameplayFixesChain.a_dead_casters_summon_keeps_its_side_for_friendliness_and_score
	headus = alliance_head();
	
	// Keep completion, enemy-freeze, and respawn decisions on exactly the same
	// alliance rule as targeting and damage.
	return headus->team_num() == team;
}

std::string_view entity_display_name(const walker* w, std::string_view fallback)
{
	// rule: How a walker is named in notices ('X ate a corpse.', MIND CONTROL, captain HOWL,
	//   possession, curses roster): its character name first, then its authored NPC name, else the
	//   family fallback the caller passes.
	// tests: PickerCommon.scenario_report_names_follow_the_shared_precedence
	// tests: WalkerSpecials.archmage_mind_control_stats_name_path
	// tests: SemWalker3.a_nameless_walker_is_called_by_the_callers_fallback
	if (w->myguy && !w->myguy->name.empty())
		return w->myguy->name;
	if (!w->stats()->name.empty())
		return w->stats()->name;
	return fallback;
}

// #160: arm the exit-pad re-trigger latch with the PAD's bbox — called by
// ob_pass_check on the first movement probe that eats an exit pad. See the
// exit_latched_ field comment in walker.h for the persistence contract
// (server-only transient, the z_stair_latched_ precedent).
void walker::latch_exit_contact(const walker* pad)
{
	exit_latched_ = true;
	exit_latch_x_ = static_cast<std::int16_t>(pad->xpos());
	exit_latch_y_ = static_cast<std::int16_t>(pad->ypos());
	exit_latch_w_ = static_cast<std::int16_t>(pad->sizex());
	exit_latch_h_ = static_cast<std::int16_t>(pad->sizey());
}

// #160: clear the exit latch once this walker's bbox no longer overlaps the
// latched pad's rect. Runs every living::act tick. The test is EXACT bbox
// overlap — deliberately one pixel wider than the shrunken obmap collide()
// that arms the latch — so the latch always clears strictly AFTER movement
// probes have stopped eating the pad, never before (edge jitter can not
// slip a re-prompt through the gap).
void walker::update_exit_latch()
{
	// rule: An exit pad prompts once per contact: the latch armed on first eaten contact clears
	//   only when the walker's own box no longer overlaps the pad's rect at all (one pixel wider
	//   than the eat test), so stepping fully off and back on prompts again while pacing or jitter
	//   on the pad does not.
	// tests: SimWorldHeadless.exit_prompts_once_per_contact_not_per_cooldown
	// tests: SimWorldHeadless.exit_reprompts_after_stepping_fully_off_and_back
	// tests: SimWorldHeadless.exit_latch_holds_while_standing_still
	// tests: SimWorldHeadless.foes_remain_toast_fires_once_per_contact
	// tests: SimWorldHeadless.two_control_walkers_latch_one_pad_independently
	if (!exit_latched_)
		return;
	const std::int16_t x = static_cast<std::int16_t>(xpos());
	const std::int16_t y = static_cast<std::int16_t>(ypos());
	const std::int16_t w = static_cast<std::int16_t>(sizex());
	const std::int16_t h = static_cast<std::int16_t>(sizey());
	const bool overlaps = x < static_cast<std::int16_t>(exit_latch_x_ + exit_latch_w_) &&
	                      exit_latch_x_ < static_cast<std::int16_t>(x + w) &&
	                      y < static_cast<std::int16_t>(exit_latch_y_ + exit_latch_h_) &&
	                      exit_latch_y_ < static_cast<std::int16_t>(y + h);
	if (!overlaps)
		exit_latched_ = false;
}

// attach_render, set_data, bmp_data, set_frame, set_direct_frame, ~walker
// are all in src/runtime/walker_render_bridge.cpp.

// New Specials: hide or reveal (see walker.h). The same collision-table
// bookkeeping as set_dormant: a hidden walker is out of the obmap, and a
// revealed one re-enters at the spot it already occupies.
void walker::set_hidden(bool value)
{
	// rule: New Specials hiding (DIG IN, possession rider): a hidden walker carries KIT_HIDDEN,
	//   loses its outline and leaves the collision table (cannot be hit, collided with, found by
	//   finders); revealing re-registers it at its spot unless it is ignored or dormant.
	//   Re-applying the same state is a no-op.
	// tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
	// tests: KitLuaTest.kit_state_bindings_read_and_write_the_walker
	// tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
	// tests: WorldSnapshot.hidden_bit_apply_keeps_the_mirror_obmap_in_step
	// tests: SemWalker3.hiding_drops_the_outline_and_a_repeat_or_dormant_reveal_changes_nothing
	if (hidden() == value)
		return;
	// A possessed body never hides. A possession tells its rider from its
	// host by which side is hidden (possession.cpp), and a hidden host would
	// stop acting, so its countdown would stall and Switch Character could
	// not find it. A walker whose partner is already hidden is a host, and
	// is turned away (a possessed skeleton trying to DIG IN). A link to a
	// walker that is gone is left alone: the host's next act drops it.
	// rule: A possessed body never hides: a walker whose possession partner is already hidden is
	//   the host, and its hide request (e.g. a possessed skeleton's DIG IN) is turned away; a link
	//   to a walker that no longer exists is ignored (the host's next act drops it).
	// tests: KitPossession.a_possessed_body_never_hides
	// tests: KitLuaTest.set_kit_state_cannot_hide_a_possessed_body
	// tests: SemWalker3.a_possession_link_to_a_gone_walker_does_not_stop_a_hide
	if (value && possess_link() != 0 && current_game != nullptr &&
	    current_game->world != nullptr)
	{
		const walker* partner = current_game->world->find_by_id(possess_link());
		// rule: A possessed body never hides: a walker whose possession partner is already hidden
		//   is the host, and its hide request (e.g. a possessed skeleton's DIG IN) is turned away.
		// tests: KitPossession.a_possessed_body_never_hides
		// tests: KitLuaTest.set_kit_state_cannot_hide_a_possessed_body
		// tests: SemWalker3.a_possession_link_to_a_gone_walker_does_not_stop_a_hide
		if (partner != nullptr && partner->hidden())
			return;
	}
	const std::uint8_t state = kit_state();
	// rule: New Specials hiding (DIG IN, possession rider).
	// tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
	// tests: KitLuaTest.kit_state_bindings_read_and_write_the_walker
	// tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
	// tests: WorldSnapshot.hidden_bit_apply_keeps_the_mirror_obmap_in_step
	// tests: SemWalker3.hiding_drops_the_outline_and_a_repeat_or_dormant_reveal_changes_nothing
	set_kit_state(static_cast<std::uint8_t>(
	    value ? (state | KIT_HIDDEN) : (state & ~KIT_HIDDEN)));
	set_outline(0);

	// plumbing: set_hidden without a world/obmap: only the flag and outline change.
	obmap* map = (current_game != nullptr && current_game->world != nullptr)
	    ? current_game->world->myobmap.get()
	    : nullptr;
	if (map == nullptr)
		return;
	// rule: New Specials hiding (DIG IN, possession rider).
	// tests: KitSeams.hidden_walker_is_inert_and_skipped_by_every_finder
	// tests: KitLuaTest.kit_state_bindings_read_and_write_the_walker
	// tests: KitSkeleton.dig_in_sinks_four_ticks_then_hides_at_frame_27_without_tunnelling
	// tests: WorldSnapshot.hidden_bit_apply_keeps_the_mirror_obmap_in_step
	// tests: SemWalker3.hiding_drops_the_outline_and_a_repeat_or_dormant_reveal_changes_nothing
	if (value)
		map->remove(this);
	else if (!ignore() && !dormant())
		map->add(this, xpos(), ypos());
}

// The pad table of fire(): where the weapon it creates is placed, one pixel
// outside this walker's box on the side facing(lastx(), lasty()) names.
// fire(), set_weapon_heading() and fire_pad_answer() read it: one rule.
// Appended at EOF so the parity canary pins above never shift.
void walker::weapon_pad(const walker& weapon, short& px, short& py)
{
	std::int32_t x = weapon.xpos();
	std::int32_t y = weapon.ypos();
	// rule: Where a fired weapon appears: one pixel outside the shooter's box on the side its
	//   heading faces — centred on the other axis for the four cardinals, at the matching outer
	//   corner for the four diagonals. fire(), set_weapon_heading() and the quiet pad probe
	//   (fire_pad_answer) all use this one table.
	// tests: QuietProbe.fire_pad_clear_matches_fires_own_pad
	// tests: QuietProbe.set_weapon_heading_lands_on_fires_pad
	// tests: QuietProbe.the_probe_pad_is_fires_pad_for_every_family_with_a_weapon_hook
	switch (facing(lastx(), lasty()))
	{
		case FACE_RIGHT:
			x = xpos()+sizex()+1;
			y = ypos()+(sizey() - weapon.sizey())/2;
			break;
		case FACE_LEFT:
			x = xpos() - weapon.sizex()-1;
			y = ypos()+(sizey()-weapon.sizey())/2;
			break;
		case FACE_DOWN:
			x = xpos()+(sizex()-weapon.sizex())/2;
			y = ypos()+sizey()+1;
			break;
		case FACE_UP:
			x = xpos()+(sizex()-weapon.sizex())/2;
			y = ypos() - weapon.sizey()-1;
			break;
		case FACE_UP_RIGHT:
			x = xpos()+sizex()+1;
			y = ypos()-weapon.sizey()-1;
			break;
		case FACE_UP_LEFT:
			x = xpos() - weapon.sizex()-1;
			y = ypos()-weapon.sizey()-1;
			break;
		case FACE_DOWN_RIGHT:
			x = xpos()+sizex()+1;
			y = ypos() + sizey()+1;
			break;
		case FACE_DOWN_LEFT:
			x = xpos() - weapon.sizex()-1;
			y = ypos()+sizey()+1;
			break;
	}
	// setxy(std::int32_t, std::int32_t) narrows the same way.
	px = static_cast<short>(x);
	py = static_cast<short>(y);
}

// The pad fire() would place a weapon on, probed quietly: the same spot
// (weapon_pad), the same weapon rules (own-team bodies pass; an enemy living
// or a generator blocks; an enemy door, tree or weapon in flight passes a
// quiet weapon probe, the weapon-vs-weapon arm coming before the door arm),
// no entity, no draw (an arrow slit is passable at the pad's distance),
// nothing eaten or opened. The grid half is asked first, then the bodies, as
// the loud query_passable asks them. A bot gate's question.
// Appended at EOF so the parity canary pins above never shift.
walker::PadAnswer walker::fire_pad_answer(std::int32_t weapon_family)
{
	// rule: Only a living can ask whether its shot's pad is clear, and only about a real weapon
	//   family; anything else gets 'no probe' (bot gates read that as 'cannot fire'; fire_pad_clear
	//   is false).
	// tests: QuietProbe.fire_pad_clear_is_false_for_a_non_living
	// tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
	if (query_order() != Order::Living || current_game == nullptr || current_game->world == nullptr)
		return PadAnswer::NoProbe;
	if (weapon_family >= NUM_FAMILY_SLOTS)
		return PadAnswer::NoProbe;
	const std::int32_t family = weapon_family >= 0
		? weapon_family
		: static_cast<std::int32_t>(static_cast<char>(current_weapon()));
	weap probe(WeaponProfileProbeTag{});
	const PixieData* data = current_game->world->configure_existing_entity(
		probe, Order::Weapon, family);
	if (data == nullptr)
		return PadAnswer::NoProbe;
	probe.set_data(*data);
	probe.set_owner(this);          // the chain create_weapon gives it: is_friendly reads it
	probe.set_team_num(team_num());
	probe.set_floor(floor());
	short px = 0, py = 0;
	weapon_pad(probe, px, py);
	probe.set_xpos(px);             // SimEntity's plain setters: never setxy,
	probe.set_ypos(py);             // which would register the probe in the obmap
	// rule: Would the shot fly: the pad fire() would use (weapon_pad) is asked, quietly (nothing
	//   eaten, opened, drawn or announced), first against terrain, then against bodies under the
	//   weapon's own rules (own-team bodies pass; an enemy living or generator blocks; enemy doors,
	//   trees and weapons in flight pass a weapon probe; an arrow slit passes at pad distance). A
	//   negative family means 'my current weapon'. Bot payoff gates (gates.lua pad_clear, Gameplay
	//   Fixes V etc.) refuse casts whose pad is blocked.
	// tests: QuietProbe.fire_pad_answer_names_what_the_pad_holds
	// tests: QuietProbe.fire_pad_clear_takes_a_weapon_family
	// tests: QuietProbe.fire_pad_clear_leaves_the_world_alone
	// tests: QuietProbe.an_arrow_slit_passes_a_quiet_weapon_probe_and_draws_nothing
	// tests: GameplayFixesElf.a_volley_at_a_wall_spends_nothing_with_the_setting_on
	if (!current_game->world->query_grid_passable(px, py, &probe, floor(), /*quiet=*/true))
		return PadAnswer::Terrain;
	if (!current_game->world->query_object_passable(px, py, &probe, floor(), /*quiet=*/true))
		return PadAnswer::Body;
	return PadAnswer::Clear;
}

// fire_pad_answer's yes: the weapon would fly.
bool walker::fire_pad_clear(std::int32_t weapon_family)
{
	return fire_pad_answer(weapon_family) == PadAnswer::Clear;
}

// The walker a hit or a kill is credited to (#411): the topmost LIVING on
// this walker's owner chain (a weapon's thrower, a summon's caster, a tent
// or bones troop itself: its owner is a post, not a body), or nullptr when
// no living is on the chain (a scripted ball fx). It climbs by chain_next,
// attack()'s own climb (fix AP): through a dead owner, stopping at a
// self-loop and, with Gameplay Fixes AD, at a charmed walker. The kill stamp
// and on_kill read it; classic score and experience keep the chain's root.
// Appended at the end of the file so the parity canary pins above never
// shift.
walker* walker::credit_head()
{
	walker* top = nullptr;
	// plumbing: credit_head's climb; chain_next holds the stop rule (fix AP)
	for (walker* w = this; w != nullptr; w = w->chain_next())
	{
		// rule: Who gets the hit/kill (#411, kill stamp and on_kill): the topmost LIVING on the
		//   owner chain — a weapon's thrower, a summon's caster, but a tent or bones troop itself
		//   (its owner is a post, not a body); nullptr when no living is on the chain (a scripted
		//   ball FX). Classic score and experience still use the chain root.
		// tests: ModesOnslaught.a_tent_troops_kill_pays_its_colour
		// tests: ModesTdm.a_tent_troops_kill_counts_a_frag
		// tests: ModesTdm.a_charmed_summons_kill_credits_its_charmed_colour
		// tests: GameplayFixesCharm.a_charmed_summon_of_an_enemy_caster_fights_for_its_charmer
		// tests: SemWalker3.a_blow_with_no_living_on_its_chain_is_credited_to_nobody
		if (w->query_order() == Order::Living)
			top = w;
	}
	return top;
}
