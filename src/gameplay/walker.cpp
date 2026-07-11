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
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/generator_family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/render_component_base.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/sound_ids.h>
// pixieN include not needed here; render bridge is in walker_render_bridge.cpp
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <format>
#include <limits>
#include <span>

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

short query_allied_mode()
{
    if (current_game == nullptr || current_game->world == nullptr)
        return 0;
    return current_game->world->allied_mode;
}

std::uint32_t query_difficulty_percent()
{
    if (current_game == nullptr || current_game->world == nullptr)
        return 100u;
    const int difficulty = current_game->world->difficulty;
    return (difficulty > 0) ? static_cast<std::uint32_t>(difficulty) : 100u;
}

std::uint32_t query_generator_rate_percent()
{
    if (current_game == nullptr || current_game->world == nullptr)
        return 100u;
    const int rate = current_game->world->generator_rate;
    if (rate <= 0)
        return 100u;
    // Sim-side robustness clamp mirroring the lobby sanitize range: the value
    // also arrives unclamped from hand-edited v12 saves and crafted snapshots.
    return static_cast<std::uint32_t>(std::clamp(rate, 25, 400));
}

IRandom& walker_rng()
{
    if (IRandom* override_rng = gameplay_rng_override())
        return *override_rng;

    if (gameplay_world_rng_active() &&
        current_game != nullptr && current_game->world != nullptr)
        return current_game->world->rng_;

    if (IRandom* session_rng = gameplay_session_rng())
        return *session_rng;

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
    if (IRandom* cos = cosmetic_rng_override())
        return 5 + static_cast<int>(cos->next(10));
    return 5 + static_cast<int>(rng.next(10));
}
} // namespace

// Common initialization shared by both constructors
void walker_init_common(walker* w, IRandom& rng)
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
	w->set_path_check_counter(next_path_check_counter(rng));
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

	walker_init_common(this, walker_rng());

	set_act_type_state(ACT_RANDOM);
	set_frame(0);
}

walker::walker()
    : SimEntity()
{
	// Headless constructor - no render component
	stats_ = std::make_unique<statistics>(this);
	myself_ = this;

	walker_init_common(this, walker_rng());

	set_act_type_state(ACT_RANDOM);
}

// attach_render, set_data, bmp_data, set_frame are in
// src/runtime/walker_render_bridge.cpp (require pixieN full definition).

short walker::next_frame()
{
	// frames is 0 for a default/test-built or moved-from walker; guard the
	// modulo to avoid integer division-by-zero UB (mirrors pixieN::next_frame).
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
	if (target == nullptr)
		return;

	if (owned_myguy_)
		target->set_owned_myguy(std::move(owned_myguy_));
	else
		target->set_myguy_view(myguy);

	myguy = nullptr;
}

void walker::set_foe(walker* target)
{
	foe_ = target;
	foe_id_ = (target != nullptr) ? target->entity_id() : 0;
	mark_dirty(og::dirty::BIT_FOE_ID);
}

void walker::set_leader(walker* target)
{
	leader_ = target;
	leader_id_ = (target != nullptr) ? target->entity_id() : 0;
	mark_dirty(og::dirty::BIT_LEADER_ID);
}

void walker::set_owner(walker* target)
{
	owner_ = target;
	owner_id_ = (target != nullptr) ? target->entity_id() : 0;
	mark_dirty(og::dirty::BIT_OWNER_ID);
}

void walker::set_collide_ob(walker* target)
{
	collide_ob_ = target;
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
		if (target != nullptr)
		{
			if (owning_world_ == nullptr)
			{
				clear_pointer();
				return;
			}

			if (stored_id != 0)
			{
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
				if (next_id == 0)
				{
					clear_pointer();
					return;
				}
			}
		}

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

	if (statistics* const entity_stats = stats(); entity_stats != nullptr)
	{
		walker* controller = entity_stats->controller();
		if (controller != nullptr)
		{
			if (owning_world_ == nullptr)
			{
				entity_stats->set_controller(nullptr);
			}
			else
			{
				std::uint32_t controller_id = 0;
				if (entity_stats->controller_id() != 0)
				{
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
					if (controller_id == 0)
						entity_stats->set_controller(nullptr);
				}

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

	if (facing(xdir, ydir) != curdir())
	{
		set_enddir(static_cast<char>(facing(xdir, ydir)));
	}
	if (curdir() != enddir() && query_order() == Order::Living)
	{
		//if (family()==FAMILY_TOWER1)
		//  enddir = curdir;
		if (act_type() == ACT_CONTROL)
			return 0;
		else
			return turn(enddir());
	}

	if (busy() > 0.0f)
		return 0;  // Too busy

	set_busy(busy() + fire_frequency()); // This pauses a few rounds

	//  if (ani_type == ANI_WALK && query_order() == Order::Living)
	if (ani_type() == ANI_WALK)  // This should allow generators to animate
	{
		set_ani_type(ANI_ATTACK);
		set_cycle(0);
		animate();
		return 1;
	}
	else
	{
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
	if (stats_->magicpoints() < stats_->weapon_cost())
		return nullptr;

	weapon = create_weapon();
	if (!weapon)
		return nullptr;

	stats_->set_magicpoints(	stats_->magicpoints() - stats_->weapon_cost());

	// Determine how much the thrown weapon can 'waver'
	// Clamp before the signed-char narrowing: stepsize is replicated from
	// network snapshots, so a hostile value > 254 would make the float->signed
	// char conversion undefined. Normal stepsizes are well under 254, so this
	// is byte-identical for legitimate game data.
	waver = static_cast<signed char>(std::clamp((weapon->stepsize())/2, 0.0f, 127.0f)); // Absolute amount ..
	waver = static_cast<signed char>(current_game->world->rng_.next(waver+1) - waver/2);

	switch (facing(lastx(), lasty()))
	{
		case FACE_RIGHT:
			weapon->setxy(xpos()+sizex()+1,ypos()+(sizey() - weapon->sizey())/2);
			weapon->set_lastx(weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_LEFT:
			weapon->setxy(xpos() - weapon->sizex()-1, ypos()+(sizey()-weapon->sizey())/2);
			weapon->set_lastx(-weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_DOWN:
			weapon->setxy(xpos()+(sizex()-weapon->sizex())/2, ypos()+sizey()+1);
			weapon->set_lasty(weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP:
			weapon->setxy(xpos()+(sizex()-weapon->sizex())/2, ypos() - weapon->sizey()-1);
			weapon->set_lasty(- weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP_RIGHT:
			weapon->setxy(xpos()+sizex()+1, ypos()-weapon->sizey()-1);
			weapon->set_lastx(weapon->stepsize() + waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_UP_LEFT:
			weapon->setxy(xpos() - weapon->sizex()-1, ypos()-weapon->sizey()-1);
			weapon->set_lastx(-weapon->stepsize() - waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_DOWN_RIGHT:
			weapon->setxy(xpos()+sizex()+1, ypos() + sizey()+1);
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(weapon->stepsize() - waver);
			break;
		case FACE_DOWN_LEFT:
			weapon->setxy(xpos() - weapon->sizex()-1, ypos()+sizey()+1);
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(-weapon->stepsize() + waver);
			break;
	}

	weapon->set_frame(frame());
	// Make sure our current direction is wrong so first walk
	// will just be draw (grumble curse)
	weapon->set_curdir(static_cast<char>((frame()+1)%2));

	//xp = weapon->xpos();
	//yp = weapon->ypos();

	// Actual combat
	if (!current_game->world->query_passable(weapon->xpos(), weapon->ypos(), weapon))
	{
		// *** Melee combat ***
		if (weapon->collide_ob() && !weapon->collide_ob()->dead())
		{
			if (attack(weapon->collide_ob()))
			{
				og::sim::emit_positional_sound(current_game->sim_events, this, SOUND_CLANG);

                if(query_order() == Order::Living)
                {
	                    set_attack_lunge(1.0f);
	                    set_attack_lunge_angle(get_current_angle());
                }

				// Family-specific melee hit callback
				if (order() == Order::Living)
				{
					const auto* fd = get_family_descriptor(family());
					if (fd && fd->on_melee_hit)
						fd->on_melee_hit(this, weapon->collide_ob());
				}
			}
			if (myguy)
            {
				myguy->total_shots++; // record that we fired/attacked
				myguy->scen_shots++;
            }
			}
			weapon->set_dead(1);
			return nullptr;
		}
		else if (stats_->query_bit_flags(BIT_NO_RANGED))
		{
			weapon->set_dead(1);
			return nullptr;
		}
	else
	{
		if (order() == Order::Living)
		{
			const auto* fd = get_family_descriptor(family());
			if (fd && fd->on_fire_weapon)
			{
				if (!fd->on_fire_weapon(this, weapon))
					return nullptr;
			}
		}
        
		// Record our shot ..
		if (myguy)
        {
			myguy->total_shots++;
			myguy->scen_shots++;
        }

			// *** Ranged combat ***
			{
					const auto* wfd = get_weapon_family_descriptor(weapon->family());
					og::sim::emit_positional_sound(current_game->sim_events, this,
					                               static_cast<std::uint32_t>(wfd ? wfd->fire_sound : SOUND_FWIP));
				}
			if (order() == Order::Generator)
			{
			const auto* gfd = get_generator_family_descriptor(family());
			if (gfd)
			{
				if (gfd->spawn_ani_type != 0)
					weapon->set_ani_type(gfd->spawn_ani_type);
				if (gfd->has_lifetime)
					weapon->set_lifetime(800 + stats_->level()*11);
				weapon->stats()->set_level(static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(stats_->level()))) + 1);
				weapon->set_difficulty(static_cast<std::uint32_t>(weapon->stats()->level()));
				if (gfd->clear_owner)
					weapon->set_owner(nullptr);
			}
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
	waver = static_cast<signed char>(current_game->world->rng_.next(waver+1) - waver/2);

	switch (facing(lastx(), lasty()))  // these are from the 'owner'
	{
		case FACE_RIGHT:
			weapon->setxy(xpos()+sizex()+1,ypos()+(sizey() - weapon->sizey())/2);
			weapon->set_lastx(weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_LEFT:
			weapon->setxy(xpos() - weapon->sizex()-1, ypos()+(sizey()-weapon->sizey())/2);
			weapon->set_lastx(-weapon->stepsize());
			weapon->set_lasty(waver);
			break;
		case FACE_DOWN:
			weapon->setxy(xpos()+(sizex()-weapon->sizex())/2, ypos()+sizey()+1);
			weapon->set_lasty(weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP:
			weapon->setxy(xpos()+(sizex()-weapon->sizex())/2, ypos() - weapon->sizey()-1);
			weapon->set_lasty(- weapon->stepsize());
			weapon->set_lastx(waver);
			break;
		case FACE_UP_RIGHT:
			weapon->setxy(xpos()+sizex()+1, ypos()-weapon->sizey()-1);
			weapon->set_lastx(weapon->stepsize() + waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_UP_LEFT:
			weapon->setxy(xpos() - weapon->sizex()-1, ypos()-weapon->sizey()-1);
			weapon->set_lastx(-weapon->stepsize() - waver);
			weapon->set_lasty(-weapon->stepsize() + waver);
			break;
		case FACE_DOWN_RIGHT:
			weapon->setxy(xpos()+sizex()+1, ypos() + sizey()+1);
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(weapon->stepsize() - waver);
			break;
		case FACE_DOWN_LEFT:
			weapon->setxy(xpos() - weapon->sizex()-1, ypos()+sizey()+1);
			weapon->set_lasty(weapon->stepsize() + waver);
			weapon->set_lastx(-weapon->stepsize() + waver);
			break;
	}

}

// Used by score_panel.cpp and glad.cpp via extern declaration.
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
	if (stats_->query_bit_flags( BIT_NAMED ) || invisibility_left() || flight_left() || invulnerable_left())
	{
		if (next_outline == OUTLINE_INVULNERABLE)
		{
			if      (flight_left())
				next_outline = OUTLINE_FLYING;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num()!=viewer_control->team_num()))
					next_outline = OUTLINE_NAMED;

			if (next_outline != OUTLINE_NAMED)
				if (invisibility_left())
					next_outline = query_team_color();
		}
		else if (next_outline == OUTLINE_FLYING)
		{
			if (viewer_control)
				if      (stats_->query_bit_flags (BIT_NAMED) && (team_num()!=viewer_control->team_num()))
					next_outline = OUTLINE_NAMED;

			if (next_outline != OUTLINE_NAMED)
			{
				if (invisibility_left())
					next_outline = query_team_color();
				else if (invulnerable_left())
					next_outline = OUTLINE_INVULNERABLE;
			}
		}
		else if (next_outline == OUTLINE_NAMED)
		{
			if      (invisibility_left())
				next_outline = query_team_color();
			else if (invulnerable_left())
				next_outline = OUTLINE_INVULNERABLE;
			else if (flight_left())
				next_outline = OUTLINE_FLYING;
		}
		else if (next_outline == query_team_color())
		{
			if      (invulnerable_left())
				next_outline = OUTLINE_INVULNERABLE;
			else if (flight_left())
				next_outline = OUTLINE_FLYING;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num()!=viewer_control->team_num()))
					next_outline = OUTLINE_NAMED;
		}
		else
		{
			if      (invisibility_left())
				next_outline = query_team_color();
			else if (flight_left())
				next_outline = OUTLINE_FLYING;
			else if (invulnerable_left())
				next_outline = OUTLINE_INVULNERABLE;
			else if (viewer_control)
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
	if (foe() && foe()->dead())
		set_foe(nullptr);
	if (leader() && leader()->dead())
		set_leader(nullptr);
	if (owner() && owner()->dead())
		set_owner(nullptr);

	set_collide_ob(nullptr); // always start with no collison..

	// Complete previous animations (like firing)
	if (ani_type() != ANI_WALK)
		return animate();

	// Are we frozen?
	if (stats_->frozen_delay())
	{
		stats_->set_frozen_delay(	stats_->frozen_delay() - 1);
		return 1;
	}

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
	if (stats_->has_commands())
	{
		temp = stats_->do_command();
		if (temp)
			return 1;
	}
	
	if(attack_lunge() > 0.0f)
    {
        float next_lunge = attack_lunge() - 0.4f;
        if(next_lunge < 0.0f)
            next_lunge = 0.0f;
        set_attack_lunge(next_lunge);
    }

	if(hit_recoil() > 0.0f)
    {
        float next_recoil = hit_recoil() - 0.6f;
        if(next_recoil < 0.0f)
            next_recoil = 0.0f;
        set_hit_recoil(next_recoil);
    }

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
				if (!current_game->world->rng_.next(4) )
				{
					if (!current_game->world->rng_.next(20))   // a 1 in 4 then 1 in 20 chance of rand walk
					{
						if (!special())
							stats_->try_command(COMMAND_WALK, current_game->world->rng_.next(30),
							                   current_game->world->rng_.next(3)-1, current_game->world->rng_.next(3)-1);
						return 1;
					}
					act_random(); //1 in 4 followed by 19 in 20 of doing this
				}
				else    //3 of 4 times
				{
					if (!foe())
					{
						set_foe(current_game->world->find_far_foe(this));
					}
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
	if (!ani)
		return 0;

	// Guard against stale/invalid signed-char indices under sanitizer builds.
	int dir_index = static_cast<int>(static_cast<unsigned char>(curdir()));
	if (dir_index < 0 || dir_index >= NUM_FACINGS)
		dir_index = 0;
	int type_index = static_cast<int>(ani_type());
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
	if (ani_count > 0 && ani_index >= ani_count)
	{
		set_ani_type(ANI_WALK);
		set_cycle(0);
		return 0;
	}
	const signed char* seq = ani[ani_index];
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
	while (seq_len < 128 && seq[seq_len] != -1)
		seq_len++;
	if (seq_len <= 0 || seq_len >= 128)
	{
		set_ani_type(ANI_WALK);
		set_cycle(0);
		return 0;
	}

	int c = static_cast<int>(cycle());
	if (c < 0)
		c = 0;

	// If cycle is already past the end (e.g. walk() advanced it beyond the
	// current animation's bounds), treat as end-of-animation rather than
	// restarting from frame() 0.  Resetting to 0 loops forever when walk()
	// keeps pushing cycle forward each frame() (attack-while-running bug).
	bool at_end;
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

	if (at_end)
	{
		//          if (ani_type == ANI_ATTACK &&
		//                        query_order() == Order::Living)
		if (ani_type() == ANI_ATTACK)
		{
			fire();
			set_ani_type(ANI_WALK);
			set_cycle(0);
			return 1;
		}
		if (ani_type() == ANI_SKEL_GROW && order() == Order::Living)
		{
			const auto* fd = get_family_descriptor(family());
			if (fd && fd->init_ani_type == ANI_SKEL_GROW)
			{
				set_ani_type(ANI_WALK);
				set_cycle(0);
				return 1;
			}
		}
		if (ani_type() == ANI_TELE_OUT && order() == Order::Living)
		{
			const auto* fd = get_family_descriptor(family());
			if (fd && fd->handle_teleport && fd->handle_teleport(this))
				return 1;
			// Default: no teleport handler, just stop
			set_ani_type(ANI_WALK);
			set_cycle(0);
			return 0;
		}
		if (order() == Order::Living)
		{
			const auto* fd2 = get_family_descriptor(family());
			if (fd2 && fd2->on_ani_complete && fd2->on_ani_complete(this))
				return 1;
		}

		set_ani_type(ANI_WALK);
		set_cycle(0);
	}
	return 1;
}

bool walker::set_frame_from_current_walk_animation()
{
	if (!ani)
		return false;

	const int dir_index = static_cast<int>(curdir());
	if (dir_index < 0 || dir_index >= NUM_FACINGS)
		return false;

	const signed char* seq = ani[dir_index];
	if (!seq)
		return false;

	int seq_len = 0;
	while (seq_len < 128 && seq[seq_len] != -1)
		seq_len++;

	if (seq_len <= 0)
	{
		set_cycle(0);
		set_frame(seq[0]);
		return true;
	}

	int c = static_cast<int>(cycle());
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

walker  *walker::create_weapon()
{
	walker  *weapon;
	short weapon_type;


	// Special case for generators
	if (query_order() == Order::Generator)
	{
			weapon = current_game->world->add_ob(Order::Living, static_cast<char>(default_weapon()));
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
		weapon_type = current_weapon();

	weapon = current_game->world->add_ob(Order::Weapon, static_cast<char>(weapon_type));
	if (!weapon) return nullptr;
	weapon->set_team_num(team_num());
	weapon->set_owner(this);
	weapon->set_floor(floor());  // projectile travels on the shooter's floor
	weapon->set_difficulty(static_cast<std::uint32_t>(stats_->level()));
	weapon->set_damage((weapon->damage() * (static_cast<float>(stats_->level()) + 3.0f)) / 4.0f);
	if (myguy)
	{
			weapon->set_lineofsight(weapon->lineofsight() + (myguy->strength / 23) + (myguy->dexterity / 31));
			weapon->set_damage(weapon->damage() + (myguy->strength / 7.0f));
	}
	else
	{
			weapon->set_damage(weapon->damage() * static_cast<float>(stats_->level()));
		}
		weapon->set_lineofsight(weapon->lineofsight() + (stats_->level() / 3));
		switch (facing(lastx(), lasty())) // make 'circular' ranges
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
		default :
			break;
	}

	if (order() == Order::Living)
	{
		const auto* fd = get_family_descriptor(family());
		if (fd && fd->customize_weapon)
			fd->customize_weapon(this, weapon);
	}
	return weapon;
}

bool walker::query_next_to()
{
	float newx = static_cast<float>(xpos());
	float newy = static_cast<float>(ypos());

	if (lastx() > 0.0f)
		newx += static_cast<float>(sizex());
	else if (lastx() < 0.0f)
		newx -= static_cast<float>(sizex());
	if (lasty() > 0.0f)
		newy += static_cast<float>(sizey());
	else //if (lasty < 0)
		newy -= static_cast<float>(sizey());

	if (!current_game->world->query_object_passable(newx, newy, this))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

bool walker::fire_check(short xdelta, short ydelta)
{
	walker  *weapon = nullptr;
	//  short newx=0, newy=0;
	std::int32_t i;
	std::int32_t distance;
	short targetdir;

	// Allow generators to 'always' succeed
	if (order() == Order::Generator)
		return 1;

	weapon = create_weapon();
	if (!weapon)
		return 0;
	set_weapon_heading(weapon); // set lastx, lasty based on our facing...
	weapon->set_collide_ob(nullptr);
	// Based on facing, we alter the weapon's proposed
	//   size so the collision check is fooled into checking
	//   a std::int32_t strip equal to the lineofsight times the size
	//   of the weapon.
	if (!foe())     // nobody to fire at?
	{
		//Log("fire check, no foe.\n");
		//this does happen! but it appears harmless
		return 0;
	}

	if (stats_->query_bit_flags(BIT_NO_RANGED))
	{
		weapon->set_dead(1);
		return 0;
	}

	if (stats_->weapon_cost() > stats_->magicpoints())
	{
		weapon->set_dead(1);
		return 0;
	}

	distance = distance_to_ob(foe());
	if (distance > static_cast<std::int32_t>( static_cast<std::int32_t>(weapon->stepsize()) * static_cast<std::int32_t>(weapon->lineofsight())) )
	{
		weapon->set_dead(1);
		return 0;
	}

	targetdir = facing(xdelta,ydelta);
	if (targetdir != curdir())
	{
		//         turn(targetdir);
		weapon->set_dead(1);
		return 0;
	}

	// Run weapon through where it would go if all went well ..
	for (i=0; i < weapon->lineofsight(); i++)
	{
		weapon->setxy(weapon->xpos() + static_cast<float>(i) * weapon->lastx(),
		              weapon->ypos() + static_cast<float>(i) * weapon->lasty());
		if ( !current_game->world->query_grid_passable(weapon->xpos(), weapon->ypos(), weapon) )
		{
			// we hit a wall, so fail
			weapon->set_dead(1);
			return 0;
		}
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
	if ( current_game->world->living_count < MAXOBS &&
	        (current_game->world->rng_.next(static_cast<std::uint32_t>(stats_->level() * 3)) * generator_rate_percent > current_game->world->rng_.next(static_cast<std::uint32_t>(300 + (current_game->world->living_count * 8))) * std::uint64_t{100} )
	   )
	{
		set_lastx(static_cast<float>(1 - static_cast<std::int32_t>(current_game->world->rng_.next(3))));
		set_lasty(static_cast<float>(1 - static_cast<std::int32_t>(current_game->world->rng_.next(3))));
		if (!lastx() && !lasty())
			set_lastx(1.0f);
		init_fire(static_cast<short>(lastx()), static_cast<short>(lasty()));
		//    lastx = 0;
		//    lasty = 0;
		stats_->set_hitpoints(	stats_->hitpoints() + 1);
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
	GameWorld* const zw = (current_game != nullptr) ? current_game->world : nullptr;
	if (vz() != 0.0f || worldz() != 0.0f)
	{
		set_worldz(worldz() + vz());
		const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(family());
		set_vz(vz() - (wfd != nullptr ? wfd->gravity : 0.0f));
		if (worldz() < 0.0f)
		{
			set_worldz(0.0f);
			set_vz(0.0f);
		}
	}
	if (zw != nullptr && zw->floor_count() > 1 && !dead())
	{
		const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(family());
		if (wfd != nullptr && wfd->can_drop_floors)
		{
			smoother& sm = zw->smoother_for_floor(floor());
			const std::int32_t g = sm.query_genre_x_y(
				(xpos() + sizex() / 2) / GRID_SIZE,
				(ypos() + sizey() / 2) / GRID_SIZE);
			if (g == TYPE_AIR)
			{
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
	if (!remaining_range) // this is the range of the weapon
	{
		set_dead(1);
		death();
	}
	else if (!walk() || stats_->query_bit_flags(BIT_NO_COLLIDE))
	{
		// Hit the collide_ob;
		if (collide_ob() && !collide_ob()->dead())
		{
			attack(collide_ob());
		}
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
	if (foe())
	{
		set_curdir(static_cast<signed char>(facing(foe()->xpos() - xpos(), foe()->ypos()-ypos())));
		stats_->try_command(COMMAND_FIRE,current_game->world->rng_.next(30));
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
	if (!current_game->world->rng_.next(70) || (!foe()))
		set_foe(current_game->world->find_far_foe(this));
	if (!foe())
		return stats_->try_command(COMMAND_RANDOM_WALK,20);

	xdist = foe()->xpos() - xpos();
	ydist = foe()->ypos() - ypos();

	// If foe is in firing range, turn and fire
	if (abs(xdist) < lineofsight() * GRID_SIZE &&
	        abs(ydist) < lineofsight() * GRID_SIZE)
	{
		if (fire_check(xdist, ydist))
		{
			init_fire(xdist, ydist);
			stats_->set_command(COMMAND_FIRE, current_game->world->rng_.next(24), xdist, ydist);
			return 1;
		}
		else
			// Nearest foe is blocked
			//foe = nullptr;
			turn(facing(xdist,ydist));
	}

	// Otherwise, try to walk toward foe
	newx = 0;
	newy = 0;

	if (foe())
	{
			newx = xdist;    // total horizontal distance..
			if (newx)                      // If it's not 0, then get
				newx = (newx > 0) ? 1 : -1;       // the normal of it..

			newy = ydist;
			if (newy)
				newy = (newy > 0) ? 1 : -1;
	}  // end of if we had a foe ..
		else
		{
			while ( !newx && !newy)
			{
				newx = static_cast<short>(1 - static_cast<std::int32_t>(current_game->world->rng_.next(3)));   // Walk in some random direction
				newy = static_cast<short>(1 - static_cast<std::int32_t>(current_game->world->rng_.next(3)));   // other than 0,0 :)
			}
		}

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

	for (i=-1; i < 2; i++)
		for (j=-1; j < 2; j++)
			if (i || j) // don't check our own location
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
	newob->stats()->set_frozen_delay(stats_->frozen_delay());
	for (i=0; i < 5; i++)
		newob->stats()->set_special_cost(i, stats_->special_cost(i));
	newob->stats()->set_weapon_cost(stats_->weapon_cost());

	newob->stats()->set_bit_flags(stats_->bit_flags());
	newob->stats()->set_delete_me(stats_->delete_me());

	// Do we have a 'guy' ?
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
	if (current_game && current_game->world && current_game->world->myobmap)
		current_game->world->myobmap->remove(this);

	if (order() == whatorder) // same object type
	{
		reset = 1;
		tempact = act_type();
	}

	// Reset bit flags
	stats_->clear_bit_flags();

	if (current_game == nullptr || current_game->world == nullptr)
		return;

	// Do this before resetting graphic so illegal
	//  family() values don't try to set graphics.
	//  order() and family() are only set if legal
	const PixieData* data = current_game->world->configure_existing_entity(*this, whatorder, whatfamily);
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

	if (death_called())
		return 0;

	set_death_called(1);

	// Ensure we are removed from collision bookkeeping as soon as we "die".
	// This prevents stale pointers in the obmap when callers manage walker
	// lifetimes outside LevelRuntimeData's owning lists (common in tests).
	obmap* active = (current_game != nullptr && current_game->world != nullptr)
	    ? current_game->world->myobmap.get()
	    : nullptr;
	if (active != nullptr)
		active->remove(this);

	if (myguy) // were we a real character?  Then make a heart ..
	{
			newob = current_game->world->add_ob(Order::Treasure, FAMILY_LIFE_GEM);
			if (newob)
			{
			newob->stats()->set_hitpoints(static_cast<float>(myguy->query_heart_value()));
			newob->stats()->set_hitpoints(
			    newob->stats()->hitpoints() * (0.75f / 2.0f));  // 75%, divided by 2, since score is doubled at end of level
			newob->set_team_num(team_num());
			newob->set_floor(floor());  // heart drops on the floor we died on (A8)
			newob->center_on(this);
			}
		}

	switch (order())
	{
		case Order::Living:
			if (   (team_num() == 0 || myguy) // our team
			        && (current_game->world->type & SCEN_TYPE_SAVE_ALL)
			        && (stats_->name.size()) // we were named
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
				if (fd && fd->on_death)
				{
					fd->on_death(this);
				}
				else if (fd && !fd->leaves_bloodspot)
				{
					// Ghost, skeleton, tower etc. -- no bloodspot
				}
				else
				{
					generate_bloodspot();
				}
			}  // end of family() dispatch
			break;  // end of order() livings case
		case Order::Generator:  // go up in flames :>
			for (i=0; i < 4; i++)
			{
				newob = current_game->world->add_ob(Order::FX, FAMILY_EXPLOSION);
				if (!newob) // failsafe
					break;
				newob->set_team_num(team_num());
				newob->stats()->set_level(stats_->level());
				newob->set_ani_type(ANI_EXPLODE);
				newob->set_floor(floor());  // burn on the generator's floor (A8)
				newob->setxy(xpos()+current_game->world->rng_.next(sizex()-8)+4, ypos()+4+current_game->world->rng_.next(sizey()-8) );
					newob->set_damage(static_cast<float>(stats_->level()) * 2.0f);
					newob->set_frame(static_cast<short>(current_game->world->rng_.next(3)));
				og::sim::emit_sound(current_game->sim_events, SOUND_EXPLODE);
			}
			break;
		case Order::FX:
			//case Order::Treasure:
			Log("Effect dying in walker?\n");
			break;          // end of effect object case
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
	if (eater)
		Log("EATING A NON-TREASURE!\n");
	return 0;
}

// set_direct_frame is in src/runtime/walker_render_bridge.cpp.

walker* walker::do_summon(char whatfamily, std::int32_t summon_lifetime)
{
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
	if (dormant_ == value)
		return;
	dormant_ = value;

	obmap* map = (current_game != nullptr && current_game->world != nullptr)
	    ? current_game->world->myobmap.get()
	    : nullptr;
	if (map == nullptr)
		return;
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
	if (new_floor == floor())
		return;
	GameWorld* w = (current_game != nullptr) ? current_game->world : nullptr;
	obmap* m = (w != nullptr) ? w->myobmap.get() : nullptr;
	if (m != nullptr)
		m->remove(this);
	set_floor(new_floor);
	set_worldz(0.0f);
	if (m != nullptr && !ignore())
		m->add(this, xpos(), ypos());
}

// A5: deterministic landing nudge for air falls. When the spot directly below
// a faller is blocked (wall top on the lower floor, or an occupant), probe
// outward from the faller's centre cell in a FIXED ring-by-ring order
// (row-major within each ring — no RNG, so replays and parity captures stay
// deterministic) for the nearest clear cell on the target floor, out to
// kFallNudgeRadius rings. Performs the floor change + nudge (set_floor before
// setxy: the obmap is floor-keyed) and returns true when a cell is found.
static bool land_on_nearest_clear_cell(GameWorld& w, walker& faller,
                                       short target_floor,
                                       std::int32_t cx, std::int32_t cy)
{
	constexpr std::int32_t kFallNudgeRadius = 4;
	const PixieData& g = w.grid_for_floor(target_floor);
	if (!g.valid())
		return false;
	for (std::int32_t r = 0; r <= kFallNudgeRadius; ++r)
	{
		for (std::int32_t dy = -r; dy <= r; ++dy)
		{
			for (std::int32_t dx = -r; dx <= r; ++dx)
			{
				if (std::max(std::abs(dx), std::abs(dy)) != r)
					continue; // ring perimeter only
				const std::int32_t nx = cx + dx;
				const std::int32_t ny = cy + dy;
				if (nx < 0 || ny < 0 || nx >= g.w || ny >= g.h)
					continue;
				const std::int32_t px = nx * GRID_SIZE;
				const std::int32_t py = ny * GRID_SIZE;
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

// Per-tick vertical handling for stacked floors: fall through "air" tiles to the
// floor below, and take Z-stairs up/down. Strictly a no-op on single-floor
// levels (floor_count()<=1), so it never affects legacy levels or parity.
// Every floor change validates its destination with floor_landing_clear —
// unvalidated arrivals used to bounce climbers off occupied stairs (A2),
// entangle walkers in unresolvable obmap overlaps (A3), and let running
// walkers materialize inside walls/torches on the other floor (A4/A5).
void walker::apply_z_motion()
{
	GameWorld* w = (current_game != nullptr) ? current_game->world : nullptr;
	if (w == nullptr || w->floor_count() <= 1)
		return;
	if (dead() || ignore())
		return;

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
	const std::int32_t genre = sm.query_genre_x_y(cx, cy);

	// Z-stairs: relocate one floor up/down (vertically aligned). Flyers never
	// change floors (per design).
	if (genre == TYPE_ZSTAIRS && !flying)
	{
		const std::int32_t tile = sm.query_x_y(cx, cy);
		short target = floor();
		if (tile == PIX_ZSTAIR_UP && (floor() + 1) < w->floor_count())
			target = static_cast<short>(floor() + 1);
		else if (tile == PIX_ZSTAIR_DOWN && floor() > 0)
			target = static_cast<short>(floor() - 1);
		if (target != floor())
		{
			// A2/A3/A4: the aligned landing spot on the target floor must
			// be grid-passable and free of blocking entities, else the
			// transition is DENIED — the walker stays on its current floor
			// (free to walk off the stair) and re-probes after the same
			// cooldown, taking the stair as soon as the far side clears.
			if (w->floor_landing_clear(this, static_cast<float>(xpos()),
			                           static_cast<float>(ypos()), target))
				change_floor(target);
			z_cooldown_ = 6;
		}
		return;
	}

	// Air: non-flying livings fall to the floor below; falling past floor 0 is a
	// pit death. Weapons get their own fall via act_fire (vz/gravity).
	if (genre == TYPE_AIR && !flying && order() != Order::Weapon)
	{
		if (floor() > 0)
		{
			const short below = static_cast<short>(floor() - 1);
			if (w->floor_landing_clear(this, static_cast<float>(xpos()),
			                           static_cast<float>(ypos()), below))
			{
				// The spot straight down is clear: land in place.
				change_floor(below);
				z_cooldown_ = 2;
			}
			else if (land_on_nearest_clear_cell(*w, *this, below, cx, cy))
			{
				// A5: straight down is a wall top / occupied — nudged to
				// the nearest clear cell of the floor below instead of
				// materializing inside the blockage.
				z_cooldown_ = 2;
			}
			else
			{
				// No clear landing within the nudge radius: do not fall
				// at all (hover on the air tile) and re-probe later.
				z_cooldown_ = 6;
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

	switch (order())
	{
		case Order::Generator:
			temp = 100*whatlevel;
			temp = (temp * dif1) / 100;
			stats_->set_hitpoints(static_cast<float>(temp));
			break;
		default:  // adjust standard settings for the rest ..
			if (team_num() != 0)  // do all EXCEPT player characters
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
	return static_cast<unsigned char>(team_num()*16+40);
	//else
	//  return static_cast<unsigned char>(7*16 + 40);
}

std::int32_t walker::is_friendly(const walker *target) const
{
	// is_friendly determines if _target_ is "friendly"
	// towards this walker.
	//short allied_mode;
	short has_myguy;
	const walker *headguy;
	const walker *headus, *headtarget;

	// In case we're passed a null pointer somehow,
	// we're always unfriendly :)
	if (target == nullptr)
		return 0;
	// If either of us is dead(), we're also unfriendly :)
	if (dead() || target->dead())
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	headguy = this;
	while (headguy->owner() && (headguy->owner()->dead() == 0) && (headguy->owner() != headguy) )
		headguy = headguy->owner();
	headus = headguy;
	// Now our target ..
	headguy = target;
	while (headguy->owner() && (headguy->owner()->dead() == 0) && (headguy->owner() != headguy) )
		headguy = headguy->owner();
	headtarget = headguy;

	// First, get our allied setting from screen ..
	// 0 is "enemy," and non-zero is "friendly"
	//allied_mode = myscreen->allied_mode;

	// Now, if we or the target don't contain a "myguy" pointer,
	// then we don't care about allied_mode, and we'll
	// treat our state as always in 'enemy' mode
	if (headtarget->myguy == nullptr && headus->myguy == nullptr)
		has_myguy = 0;
    else if(headtarget->myguy == nullptr || headus->myguy == nullptr)
        has_myguy = 2;
	else
		has_myguy = 1;

	// Is allied mode set to zero (enemy)?
	// If so, then if our team numbers don't match,
	// we are not friendly
	if (query_allied_mode() == 0 || has_myguy == 0)
	{
		return headus->team_num() == headtarget->team_num();
	}
	
	// Allied
	if(has_myguy == 2)
    {
        // One person is missing a myguy pointer.
        // The one with a myguy pointer is owned by a player.
        // If the other person belongs to team 0 (red), then they are friendly.
        return (headtarget->myguy == nullptr && headtarget->team_num() == 0) ||
               (headus->myguy == nullptr && headus->team_num() == 0);
    }

	// If we're in 'friendly' mode, then everyone with
	// a "myguy" pointer (a real, saved character)
	// is friendly to each other ..
	// By now we know that both us and the target have
	// myguy's, so we're friendly
	return 1;
}

std::int32_t walker::is_friendly_to_team(unsigned char team) const
{
	// is_friendly_to_team determines if _team_ is "friendly"
	// towards this walker.
	//short allied_mode;
	short has_myguy;
	const walker *headguy;
	const walker *headus;
	
	// If dead(), we're also unfriendly :)
	if (dead())
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	headguy = this;
	while (headguy->owner() && (headguy->owner()->dead() == 0) && (headguy->owner() != headguy) )
		headguy = headguy->owner();
	headus = headguy;
	
	// First, get our allied setting from screen ..
	// 0 is "enemy," and non-zero is "friendly"
	//allied_mode = myscreen->allied_mode;

	// Now, if we or the target don't contain a "myguy" pointer,
	// then we don't care about allied_mode, and we'll
	// treat our state as always in 'enemy' mode
	if (headus->myguy == nullptr)
		has_myguy = 0;
	else
		has_myguy = 1;

	// Is allied mode set to zero (enemy) or were we not hired (!myguy)?
	// If so, then our team number must match.
	if (query_allied_mode() == 0 || has_myguy == 0)
	{
		return headus->team_num() == team;
	}
	
	// If we're a hired guy in allied mode, then we're friendly with team 0 (red)
	return (has_myguy == 1 && team == 0);
}

std::string_view entity_display_name(const walker* w, std::string_view fallback)
{
	if (w->myguy && !w->myguy->name.empty())
		return w->myguy->name;
	if (!w->stats()->name.empty())
		return w->stats()->name;
	return fallback;
}

// attach_render, set_data, bmp_data, set_frame, set_direct_frame, ~walker
// are all in src/runtime/walker_render_bridge.cpp.
