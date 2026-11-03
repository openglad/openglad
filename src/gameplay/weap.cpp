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
//
// weap; a derived class of walker
//

#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <format>

weap::weap(const PixieData& data)
    : walker(data)
{
	set_do_bounce(0); // don't normally bounce :)
}

weap::weap()
    : walker()
{
	set_do_bounce(0);
}

weap::weap(WeaponProfileProbeTag tag)
    : walker(tag)
{
	set_do_bounce(0);
}

weap::~weap()
{
	//buffers: PORT: can't call destructor w/o obj: walker::~walker();
}

bool weap::act()
{

	// Make sure everyone we're pointing to is valid
	// rule: A weapon forgets dead foes and leaders, and a weapon whose owner died (or never had
	//   one) owns itself, so its hits are credited to itself.
	// tests: WeapBehavior.weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight
	if (foe() && foe()->dead())
		set_foe(nullptr);
	if (leader() && leader()->dead())
		set_leader(nullptr);
	if (owner() && owner()->dead())
		set_owner(nullptr);

	// rule: A weapon forgets dead foes and leaders, and a weapon whose owner died (or never had
	//   one) owns itself, so its hits are credited to itself.
	// tests: WeapBehavior.weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight
	if (!owner())
		set_owner(this); //to fix cases where our parent died!

	set_collide_ob(nullptr); // always start with no collision..

	// Complete previous animations (like firing)
		// rule: A weapon still playing a non-walk animation finishes it before doing anything else
		//   that tick.
		// tests: WeapBehavior.weap_act_sit_with_non_skipping_family_and_act_animate_shortcut
		if (ani_type() != ANI_WALK)
			return animate();

	//  Log("weap %d is ani %d\n", family, ani_type);

		// Legacy TREES arm kept byte-for-byte (including its pixel-coords-as-
		// grid-coords quirk); the decor arm (concealing SHRUB, walker's own
		// floor) uses the actual cell and is gated on plane validity, so
		// no-decor levels behave identically. Combined with || so a cell that
		// is both trees and shrub still decays lineofsight once per tick.
		// rule: A projectile flying through trees or concealing shrub loses one tick of range per
		//   tick (never below 0). The tree test keeps the classic quirk of passing pixel
		//   coordinates as grid coordinates.
		// tests: WeapBehavior.weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight
		// tests: Decor.shrub_conceals_for_living_and_weapon_but_bones_does_not
		// tests: Weap.a_shot_over_trees_spends_range_but_never_below_zero
		if (current_game->world->mysmoother.query_genre_x_y(xpos(), ypos()) == TYPE_TREES
		    || current_game->world->decor_conceals_at(floor(), xpos() / GRID_SIZE, ypos() / GRID_SIZE))
			// rule: A projectile flying through trees or concealing shrub loses one tick of range
			//   per tick (never below 0).
			// tests: WeapBehavior.weap_act_clears_dead_refs_and_defaults_owner_and_tree_lineofsight
			// tests: Decor.shrub_conceals_for_living_and_weapon_but_bones_does_not
			// tests: Weap.a_shot_over_trees_spends_range_but_never_below_zero
			if (lineofsight())
				set_lineofsight(lineofsight() - 1);

		// rule: A weapon in a living-only act mode does nothing: ACT_CONTROL returns at once,
		//   ACT_GENERATE and ACT_GUARD fall through to 'did nothing', and an unknown act notifies
		//   'No act type set for weapon'.
		// tests: WeapBehavior.weap_act_control_generate_guard_and_default_paths
		// tests: Weap.a_weapon_in_control_or_an_unknown_act_does_nothing
		switch (act_type())
	{
			// We are the control character
		case ACT_CONTROL:
			{
				Log("Weapon is act_control?\n");
				return 1;
			}
		// rule: Sitting scenery (trees, blood, doors and other skip-notify families) sits silently;
		//   any other sitting weapon reports 'Weapon sitting' (HUD standard time).
		// tests: WeapBehavior.weap_act_sit_tree
		// tests: WeapBehavior.weap_notices_carry_no_family_in_a_or_team_in_b
		// tests: WeapBehavior.weap_act_sit_with_non_skipping_family_and_act_animate_shortcut
		case ACT_SIT: // for things like trees
			{
				const auto* wfd = get_weapon_family_descriptor(family());
				// rule: Sitting scenery (trees, blood, doors and other skip-notify families) sits
				//   silently; any other sitting weapon reports 'Weapon sitting' (HUD standard
				//   time).
				// tests: WeapBehavior.weap_act_sit_tree
				// tests: WeapBehavior.weap_notices_carry_no_family_in_a_or_team_in_b
				// tests: WeapBehavior.weap_act_sit_with_non_skipping_family_and_act_animate_shortcut
				if (!wfd || !wfd->skip_sit_notify)
					og::sim::emit_event_text(current_game->sim_events,
					                         og::sim::EventKind::Notification,
					                         "Weapon sitting",
					                         0,   // a: HUD duration, 0 = STANDARD_TEXT_TIME (#351)
					                         0);  // b: unused for notifications
				return 1;
			}

			// We are a generator
		case ACT_GENERATE:
			{
				Log("Weapon is act_generate?\n");
				//act_generate();
				break;
			}

			// We are a weapon
		// rule: A fired weapon flies: it spends one point of range per tick and dies when range
		//   runs out.
		// tests: WeapBehavior.weap_act_fire
		// tests: Weap.a_fired_weapon_with_range_left_moves_along_its_heading
		case ACT_FIRE:
			{
				act_fire();
				return 1;
				//break;
			}

		case ACT_GUARD:
			{
				Log("Weapon on guard mode?\n");
				//              act_guard();
				break;
			}
		// rule: A weapon told to die is marked dead that tick.
		// tests: WeapBehavior.weap_act_die
		case ACT_DIE:
			{
				this->set_dead(1);
				return 1;
			}
			// We are randomly walking toward enemy
		// rule: A weapon in random-walk mode does not walk; it reports 'Weapon <family> doing act
		//   random?'.
		// tests: WeapBehavior.weap_act_random
		case ACT_RANDOM:
			{
				std::string msg = std::format("Weapon {} doing act random?",
				                              static_cast<int>(static_cast<unsigned char>(family())));
				//Log("Weapon doing act_random?\n");
				og::sim::emit_event_text(current_game->sim_events,
				                         og::sim::EventKind::Notification,
				                         msg,
				                         0,   // a: HUD duration, 0 = STANDARD_TEXT_TIME (#351)
				                         0);  // b: unused for notifications
				return 1;
			}  // END RANDOM
			//break;

		default:
			{
				//Log("No act type set for weapon.\n");
				og::sim::emit_notification(current_game->sim_events, "No act type set for weapon");
				return 0;
			}
	}  // END SWITCH
	return 0;
}

// death is called when an object dies (weapon destructed, etc.)
// for special effects ..
bool weap::death()
{
	// Note that the 'dead' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)

	// rule: A weapon's death effects (explosions, transforms, door opening) run exactly once.
	// tests: WeapBehavior.weap_death_is_idempotent, WeapBehavior.weap_death_fire_arrow_exploding
	// tests: Weap.a_second_weapon_death_runs_no_effect
	if (death_called())  // Make sure we don't get multiple deaths
		return 0;

	set_death_called(1);

	const auto* wfd = get_weapon_family_descriptor(family());
	og::script::hooks::weapon_on_death(wfd, this);

	return 1;

}

bool weap::animate()
{
	//walker  * newob;

	// We never use ani_type as  as I can tell; always use 0
	//  if (ani_type)
	//  {
	//       Log("weap ani_type = %d\n", ani_type);
	//       ani_type = 0;
	//  }

	const auto* wfd = get_weapon_family_descriptor(family());
	// rule: A weapon family with its own animation script drives its frames; when the script says
	//   the weapon is finished (false), the weapon dies; families without a script use the default
	//   facing-row animation.
	// tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
	// tests: WeapBehavior.weap_animate_knife, Weap.a_weapon_whose_animation_script_finishes_dies
	if (auto hook_result = og::script::hooks::weapon_on_animate(wfd, this))
	{
		// rule: A weapon family with its own animation script drives its frames.
		// tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
		// tests: WeapBehavior.weap_animate_knife
		// tests: Weap.a_weapon_whose_animation_script_finishes_dies
		if (!*hook_result)
			return death();
	}
	else
	{
		// Default animation. curdir/cycle may originate from an untrusted
		// snapshot, so bound every index against the facing range, this
		// family's table length, and the sequence sentinel before reading.
			set_ani_type(0);
			int dir_index = static_cast<int>(static_cast<unsigned char>(curdir()));
			// rule: The default weapon animation plays the row of its facing, frame by frame,
			//   wrapping at the end; an out-of-range facing plays row 0 and an out-of-range frame
			//   restarts the row (snapshot hardening).
			// tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
			// tests: WeapBehavior.weap_animate_knife
			// tests: Weap.the_default_animation_leaves_the_frame_on_a_missing_or_empty_row
			if (dir_index < 0 || dir_index >= NUM_FACINGS)
				dir_index = 0;
			// ani_count>0 (real entities): bound the row; ani_count==0 (test
			// weapons that assign `ani` directly): legacy direct index.
			const signed char* seq =
				(ani && (ani_count <= 0 || dir_index < ani_count)) ? ani[dir_index] : nullptr;
			if (seq)
			{
				int seq_len = 0;
				// rule: The default weapon animation plays the row of its facing, frame by frame,
				//   wrapping at the end.
				// tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
				// tests: WeapBehavior.weap_animate_knife
				// tests: Weap.the_default_animation_leaves_the_frame_on_a_missing_or_empty_row
				while (seq_len < 128 && seq[seq_len] != -1)
					seq_len++;
				// rule: The default weapon animation plays the row of its facing, frame by frame,
				//   wrapping at the end.
				// tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
				// tests: WeapBehavior.weap_animate_knife
				// tests: Weap.the_default_animation_leaves_the_frame_on_a_missing_or_empty_row
				if (seq_len > 0 && seq_len < 128)
				{
					int c = static_cast<int>(cycle());
					// rule: The default weapon animation plays the row of its facing, frame by
					//   frame, wrapping at the end.
					// tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
					// tests: WeapBehavior.weap_animate_knife
					// tests: Weap.the_default_animation_leaves_the_frame_on_a_missing_or_empty_row
					if (c < 0 || c >= seq_len)
						c = 0;
					set_frame(seq[c]);
					c++;
					// rule: The default weapon animation plays the row of its facing, frame by
					//   frame, wrapping at the end.
					// tests: WeapBehavior.weapon_animate_clamps_out_of_range_facing_and_cycle_to_row_zero
					// tests: WeapBehavior.weap_animate_knife
					// tests: Weap.the_default_animation_leaves_the_frame_on_a_missing_or_empty_row
					if (seq[c] == -1)
						c = 0;
					set_cycle(static_cast<signed char>(c));
				}
			}
	}

	return 1;
}

bool weap::setxy(short x, short y)
{
	return walker::setxy(x, y);
}
