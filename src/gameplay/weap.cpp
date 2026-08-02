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
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
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

weap::~weap()
{
	//buffers: PORT: can't call destructor w/o obj: walker::~walker();
}

bool weap::act()
{

	// Make sure everyone we're pointing to is valid
	if (foe() && foe()->dead())
		set_foe(nullptr);
	if (leader() && leader()->dead())
		set_leader(nullptr);
	if (owner() && owner()->dead())
		set_owner(nullptr);

	if (!owner())
		set_owner(this); //to fix cases where our parent died!

	set_collide_ob(nullptr); // always start with no collision..

	// Complete previous animations (like firing)
		if (ani_type() != ANI_WALK)
			return animate();

	//  Log("weap %d is ani %d\n", family, ani_type);

		// Legacy TREES arm kept byte-for-byte (including its pixel-coords-as-
		// grid-coords quirk); the decor arm (concealing SHRUB, walker's own
		// floor) uses the actual cell and is gated on plane validity, so
		// no-decor levels behave identically. Combined with || so a cell that
		// is both trees and shrub still decays lineofsight once per tick.
		if (current_game->world->mysmoother.query_genre_x_y(xpos(), ypos()) == TYPE_TREES
		    || current_game->world->decor_conceals_at(floor(), xpos() / GRID_SIZE, ypos() / GRID_SIZE))
			if (lineofsight())
				set_lineofsight(lineofsight() - 1);

		switch (act_type())
	{
			// We are the control character
		case ACT_CONTROL:
			{
				Log("Weapon is act_control?\n");
				return 1;
			}
		case ACT_SIT: // for things like trees
			{
				const auto* wfd = get_weapon_family_descriptor(family());
				if (!wfd || !wfd->skip_sit_notify)
					og::sim::emit_event_text(current_game->sim_events,
					                         og::sim::EventKind::Notification,
					                         "Weapon sitting",
					                         static_cast<std::uint32_t>(family()),
					                         static_cast<std::uint32_t>(team_num()));
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
		case ACT_DIE:
			{
				this->set_dead(1);
				return 1;
			}
			// We are randomly walking toward enemy
		case ACT_RANDOM:
			{
				std::string msg = std::format("Weapon {} doing act random?",
				                              static_cast<int>(static_cast<unsigned char>(family())));
				//Log("Weapon doing act_random?\n");
				og::sim::emit_event_text(current_game->sim_events,
				                         og::sim::EventKind::Notification,
				                         msg,
				                         static_cast<std::uint32_t>(family()),
				                         static_cast<std::uint32_t>(team_num()));
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
	if (auto hook_result = og::script::hooks::weapon_on_animate(wfd, this))
	{
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
			if (dir_index < 0 || dir_index >= NUM_FACINGS)
				dir_index = 0;
			// ani_count>0 (real entities): bound the row; ani_count==0 (test
			// weapons that assign `ani` directly): legacy direct index.
			const signed char* seq =
				(ani && (ani_count <= 0 || dir_index < ani_count)) ? ani[dir_index] : nullptr;
			if (seq)
			{
				int seq_len = 0;
				while (seq_len < 128 && seq[seq_len] != -1)
					seq_len++;
				if (seq_len > 0 && seq_len < 128)
				{
					int c = static_cast<int>(cycle());
					if (c < 0 || c >= seq_len)
						c = 0;
					set_frame(seq[c]);
					c++;
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
