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
// Stats.cpp
//

#include <openglad/gameplay/statistics.h>      // for bit flags, etc.
#include <openglad/core/util.h>
#include <openglad/gameplay/dirty_field_bits.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
// find_follow_leader is defined in the SDL build (screen.cpp) and stubbed
// by the text client. Returns a walker to follow, or nullptr if none.
walker* find_follow_leader();
#include <cmath>
#include <cstdint>
#include <format>
#include <openglad/gameplay/sim_event_log.h>

using Uint32 = std::uint32_t;
using Sint32 = std::int32_t;

static inline Uint32 rng(Uint32 max_exclusive) {
    return current_game->world->rng_.next(max_exclusive);
}

#define CHECK_STEP_SIZE 1 // (controller->stepsize()) // was 1

// COMMAND declarations defined at bottom

statistics::statistics(walker  * someguy)
{
	short i;

	if (someguy)
	{
		controller_ = someguy;
		owner_ = someguy;
		controller_id_ = someguy->entity_id();
	}
	else
	{
		controller_ = nullptr;
		owner_ = nullptr;
		controller_id_ = 0;
		Log("made a stats with no controller!\n");
	}
	hitpoints_ = max_hitpoints_ = 10;
	magicpoints_ = max_magicpoints_ = 50;
	level_ = 1;
	armor_ = 0; // no default armor
	max_heal_delay_ = current_heal_delay_ = 1000;  // default of 50 cycles / hitpoint
	max_magic_delay_ = current_magic_delay_ = 1000;
	magic_per_round_ = 0.0f; // default to not regenerating magic
	heal_per_round_ = 0.0f;  // default to not regenerating hitpoints

	bit_flags_ = 0;           // clear all the bit flags
	frozen_delay_ = 0;     // start out able to move :)
	for (i=0; i < NUM_SPECIALS; i++)
		special_cost_[i] = 0; // low default special ability cost in magicpoints
	weapon_cost_ = 0;

	delete_me_ = 0;

	// AI finding routine values ..
	last_distance_ = 15000;
	current_distance_ = 15000;

    if(controller_ != nullptr)
    {
        old_order_ = controller_->query_order();
        old_family_ = controller_->family();
    }
    else
    {
        old_order_ = Order::Living;
        old_family_ = FAMILY_SOLDIER;
    }

	name[0] = 0; // set to null string
}

statistics::~statistics()
{
	controller_ = nullptr;
	controller_id_ = 0;
	owner_ = nullptr;
	delete_me_ = 1;
}

void statistics::mark_dirty(std::uint8_t bit_index)
{
	walker* dirty_owner = owner_ != nullptr ? owner_ : controller_;
	if (dirty_owner != nullptr)
		dirty_owner->mark_dirty(bit_index);
}

void statistics::set_controller(walker* value)
{
	walker* dirty_owner = owner_;
	if (dirty_owner == nullptr)
		dirty_owner = controller_ ? controller_ : value;

	controller_ = value;
	controller_id_ = (value != nullptr) ? value->entity_id() : 0;

	if (owner_ == nullptr)
		owner_ = dirty_owner;
	if (dirty_owner != nullptr)
		dirty_owner->mark_dirty(og::dirty::BIT_CONTROLLER_ID);
}

void statistics::clear_command()
{
	commands.clear();
	// Make sure our weapon type is restored to normal ..
	controller_->set_current_weapon(controller_->default_weapon());
	// Make sure we're back to our real team
	if (controller_->real_team_num() != 255)
	{
		controller_->set_team_num(controller_->real_team_num());
		controller_->set_real_team_num(255);
	}
	controller_->set_leader(nullptr);
}

void statistics::add_command(Sint32 whatcommand, Sint32 iterations,
                             Sint32 info1, Sint32 info2)
{

	if (whatcommand == COMMAND_DIE)
	{
		set_delete_me(1);
		return;
	}

	if (whatcommand == COMMAND_FOLLOW)
	{
		Log("following\n");
	}

	// Add command to end of list
	commands.emplace_back();

	if (whatcommand == COMMAND_WALK)
	{
		if (info1 > 1)
			info1 = 1;
		if (info1 < -1)
			info1 = -1;
		if (info2 > 1)
			info2 = 1;
		if (info2 < -1)
			info2 = -1;
		if (!info1 && !info2)
			info1 = info2 = 1;
	}
	commands.back().com1 = info1;
	commands.back().com2 = info2;
	commands.back().commandtype = whatcommand;
	commands.back().commandcount = iterations;
}

void statistics::force_command(Sint32 whatcommand, Sint32 iterations,
                               Sint32 info1, Sint32 info2)
{
	commands.emplace_front();

	if (whatcommand == COMMAND_WALK)
	{
		if (info1 > 1)
			info1 = 1;
		if (info1 < -1)
			info1 = -1;
		if (info2 > 1)
			info2 = 1;
		if (info2 < -1)
			info2 = -1;
		if (!info1 && !info2)
			info1 = info2 = 1;
	}
	commands.front().com1 = info1;
	commands.front().com2 = info2;
	commands.front().commandtype = whatcommand;
	commands.front().commandcount = iterations;
}

bool statistics::has_commands() const
{
	return !commands.empty();
}


// try_command will only set a command if there is
// none in the queue

short statistics::try_command(Sint32 whatcommand, Sint32 iterations)
{
	if (whatcommand == COMMAND_RANDOM_WALK)
		return try_command(COMMAND_WALK, iterations, static_cast<Sint32>(rng(3)) - 1, static_cast<Sint32>(rng(3)) - 1);
	else
		return try_command(whatcommand, iterations, 0, 0);
}

short statistics::try_command(Sint32 whatcommand, Sint32 iterations,
                              Sint32 info1, Sint32 info2)
{
	add_command(whatcommand, iterations, info1, info2);
	return 0;
}

void statistics::set_command(Sint32 whatcommand, Sint32 iterations)
{
	if (whatcommand == COMMAND_RANDOM_WALK)
		set_command(COMMAND_WALK, iterations, static_cast<Sint32>(rng(3)) - 1, static_cast<Sint32>(rng(3)) - 1);
	else
		set_command(whatcommand, iterations, 0, 0);
}

void statistics::set_command(Sint32 whatcommand, Sint32 iterations,
                             Sint32 info1, Sint32 info2)
{
	if (whatcommand == COMMAND_DIE)
		Log("BLLLLLLLLLLLLLAAAAAAAAAAHHHHHHH!\n");

	force_command(whatcommand, iterations, info1, info2);

}

// Do the current command
short statistics::do_command()
{
	Sint32 commandtype, com1, com2;
	short i;
	
	walker * target;
	short deltax, deltay;
	Sint32 distance;

	short newx = 0;
	short newy = 0;
#ifdef PROFILING

	profiler docommprofiler("stats::do_command");
#endif

	if (!controller_) // allow dead controllers for now
	{
		Log("STATS:DO_COM: No controller!\n");
		// wait_for_key only available in SDL builds; skip in headless
		return 0;
	}

	// Get next command;
	if (commands.empty())
        return 0;
    
    commandtype = commands.front().commandtype;
    com1 = commands.front().com1;
    com2 = commands.front().com2;
    
    short result = 1;

	switch (commandtype)
	{
		case COMMAND_WALK:
			controller_->walkstep(com1, com2);
			break;
			case COMMAND_FIRE:
			if (!(controller_->query_order() == Order::Living))
			{
				Log("commanding a non-living to fire?");
				break;
			}
				if (!controller_->fire_check(static_cast<short>(com1), static_cast<short>(com2)))
				{
					commands.front().commandcount = 0;
					result = 0;
					break;
				}
				controller_->init_fire(static_cast<short>(com1), static_cast<short>(com2));
				break;
		case COMMAND_DIE:  // debugging, not currently used
			if (!controller_->dead())
				Log("Trying to make a living ob die!\n");
			if (commands.front().commandcount < 2)  // then delete us ..
				set_delete_me(1);
			break;
		case COMMAND_FOLLOW:   // follow the leader
			if (controller_->foe()) // if we have foe, don't follow this round
			{
				commands.front().commandcount = 0;
				controller_->set_leader(nullptr);
				result = 0;
				break;
			}
			if (!controller_->leader())
			{
				controller_->set_leader(find_follow_leader());
				if (!controller_->leader())
				{
					commands.front().commandcount = 0;
					result = 0;
					break;
				}
			}
			
			// Do we have a leader now?
			if(controller_->leader())
			{
				distance = controller_->distance_to_ob(controller_->leader());
				if (distance < 60)
				{
					controller_->set_leader(nullptr);
					result = 1;  // don't get too close
					break;
				}
				newx = static_cast<short>(controller_->leader()->xpos() - controller_->xpos()); // total horizontal distance..
				newy = static_cast<short>(controller_->leader()->ypos() - controller_->ypos());
				if (abs(newx) > abs(3*newy))
					newy = 0;
				if (abs(newy) > abs(3*newx))
					newx = 0;
				if (newx)                      // If it's not 0, then get
					newx = static_cast<short>(newx / abs(newx));     // the normal of it..
				if (newy)
					newy = static_cast<short>(newy / abs(newy));
			}  // end of if we had a foe ..
			
			controller_->walkstep(newx, newy);
			if (commands.front().commandcount < 2)
            {
				controller_->set_leader(nullptr);
            }
			break;
		case COMMAND_QUICK_FIRE:
			controller_->walkstep(com1, com2);
			controller_->fire();
			break;
			case COMMAND_MULTIDO: // Lets you do <com1> commands in one round
				{
					const Sint32 repeats = (com1 > 0) ? com1 : 0;
					// Consume MULTIDO first; otherwise do_command() would recurse on
					// the same front command indefinitely.
					commands.pop_front();
					for (i = 0; i < repeats && !commands.empty(); i++)
						do_command();
					return 1;
				}
		case COMMAND_RUSH:    // Fighter's special
			if (controller_->query_order() == Order::Living)
			{
				controller_->walkstep(com1, com2);
				controller_->walkstep(com1, com2);
				controller_->walkstep(com1, com2);
				if (controller_->collide_ob()) // We hit someone
				{
					target = controller_->collide_ob();
					controller_->attack(target);
					target->stats()->clear_command();
					// A violent shove... we can't call shove since we
					//  made shove unable to shove enemies.
					target->stats()->force_command(COMMAND_WALK,
					                             4,com1,com2 );
				}
			}
			break;
			case COMMAND_SET_WEAPON: // set weapon to specified type
				controller_->set_current_weapon(static_cast<unsigned short>(com1));
				break;
		case COMMAND_RESET_WEAPON: // reset weapon to default type
			controller_->set_current_weapon(controller_->default_weapon());
			break;
		case COMMAND_SEARCH: // use right-hand rule to find foe
			if (controller_->foe() && !controller_->foe()->dead())
				walk_to_foe();
			else // stop trying to walk to this foe
            {
				commands.front().commandcount = 0;
            }
			break;
		case COMMAND_RIGHT_WALK: // right-hand-walk ONLY
			if (controller_->foe())
			{
				distance = controller_->distance_to_ob(controller_->foe());
				if (distance > 120 && distance < 240)
					right_walk();
				else
					if (!direct_walk())
						right_walk();
			}
			break;
		case COMMAND_ATTACK: // attack a nearby, set foe
			if (!controller_->foe() || controller_->foe()->dead())
			{
				commands.front().commandcount = 0;
				result = 1;
				break;
			}
			// Try to walk toward foe, and/or attack ..
			deltax = static_cast<short>(controller_->foe()->xpos() - controller_->xpos());
			deltay = static_cast<short>(controller_->foe()->ypos() - controller_->ypos());
				if (abs(deltax) > abs(3*deltay))
					deltay = 0;
				if (abs(deltay) > abs(3*deltax))
					deltax = 0;
				if (deltax)
					deltax = (deltax > 0) ? 1 : -1;
				if (deltay)
					deltay = (deltay > 0) ? 1 : -1;
				if (!controller_->fire_check(deltax,deltay))
					controller_->walkstep(deltax, deltay);
				else // (controller_->fire_check(deltax, deltay))
				{
					force_command(COMMAND_FIRE,static_cast<short>(rng(5)),deltax,deltay);
					controller_->init_fire(deltax,deltay);
			}
			break;
		case COMMAND_UNCHARM:
			/*
			  if (commandcount > 1)
			  {
			    add_command(COMMAND_UNCHARM, commandcount-1, 0, 0);
			    commandcount = 1;
			  }
			  else if (controller_->real_team_num() != 255)
			  {
			    controller_->set_team_num(controller_->real_team_num());
			    controller_->set_real_team_num(255);
			  }
			*/
			break;  // end of uncharm case
		default:
			break;
	}
	
	// NOTE: The first command might be a different command than it was before the switch statement.
	// That would make this code decrement the wrong command.
	if(!commands.empty())
	{
        commands.front().commandcount--;       // reduce # of times left
        
        if(commands.front().commandcount < 1) // Last iteration!
        {
            commands.front().commandcount = 0;
            
			commands.pop_front();
        }
	}
	
	return result;
}

// Determines what to do when we're hit by 'who'
// 'controller' is our parent walker object
void statistics::hit_response(walker  *who)
{
	short myfamily;
	walker *foe; // who is attacking us?
	float threshold; // for hitpoint 'running away'

	if (!who || !controller_)
		return;
	if (who->dead() || controller_->dead())
		return;

	if (controller_->act_type() == ACT_CONTROL)
		return;

	if (controller_->query_order() != Order::Living)
		return;

	// Set quick-reference values ..
	myfamily = controller_->family();
	if (who->query_order() == Order::Weapon && who->owner())
		foe = who->owner();
	else
		foe = who;

	auto* fd = get_family_descriptor(myfamily);
	if (fd && fd->hit_response)
	{
		fd->hit_response(this, foe);
		return;
	}

	// Default: attack our attacker
	if (controller_->check_special() && !rng(3) )
		controller_->special();
	if (controller_->myguy) // are we a player's character?
		threshold = (5 * max_hitpoints_)/10; // then flee at 50%
	else                   // we're an enemy, so be braver :>
		threshold = (5 * max_hitpoints_)/16; // flee at 5/16%
	if ( (hitpoints_ < threshold)
	        && !controller_->yo_delay()) // then yell for help & run ..
	{
		yell_for_help(foe);
	} // end of yell for help
	if (controller_->foe() != foe) // we're attacked by a new enemy
	{
		// Clear old commands ..
		clear_command();
		// Attack our attacker
		controller_->set_foe(foe);
		foe->set_foe(controller_);
		set_last_distance(32000);
		set_current_distance(32000);
	}

}

void statistics::yell_for_help(walker *foe)
{
	Sint32 howmany;
	Sint32 deltax, deltay;

	controller_->set_yo_delay(controller_->yo_delay() + 80);
	
	// Get AI-controlled allies to target my foe
	std::list<walker*> helplist = current_game->world->find_friends_in_range(
	               current_game->world->oblist, 160, &howmany, controller_);
	for(auto* w : helplist)
	{
		w->set_leader(controller_);
		if (foe != w->foe())
		{
			w->stats()->set_current_distance(32000);
			w->stats()->set_last_distance(32000);
		}
		w->set_foe(foe);
		//if (w->act_type() != ACT_CONTROL)
		//  w->stats()->force_command(COMMAND_FOLLOW, 80, 0, 0);
	}
	
	// Force run in the opposite direction
	deltax = -(foe->xpos() - controller_->xpos());
	if (deltax)
		deltax = (deltax / abs(deltax));
	deltay =  -(foe->ypos() - controller_->ypos());
	if (deltay)
		deltay =  (deltay / abs(deltay));
	// Run away
	force_command(COMMAND_WALK, 16, deltax, deltay);
	// Notify friends of need ...
	if (controller_->myguy && (controller_->team_num() == 0) )
	{
		std::string message = std::format("{} yells for help!", controller_->myguy->name);
		if (current_game->sim_events)
			current_game->sim_events->push_notification(message, 10);
	}

}

short statistics::query_bit_flags(Sint32 myvalue) const
{
	return static_cast<short>(myvalue & bit_flags_);
}

void statistics::clear_bit_flags()
{
    set_bit_flags(0);
}

void statistics::set_bit_flags(Sint32 someflag, short newvalue)
{
	if (newvalue)
	{
		bit_flags_ |= someflag;
	}
	else
	{
		bit_flags_ &= ~someflag;
	}
	mark_dirty(og::dirty::BIT_BIT_FLAGS);
}

// Returns whether our right is blocked
bool statistics::right_blocked()
{
	float xdelta, ydelta;
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = controller_->stepsize();

	mystep = CHECK_STEP_SIZE;
	switch (controller_->curdir())
	{
		case FACE_UP:
			xdelta = mystep;
			ydelta = 0;
			break;
		case FACE_UP_RIGHT:
			xdelta = mystep;
			ydelta = mystep;
			break;
		case FACE_RIGHT:
			xdelta = 0;
			ydelta = mystep;
			break;
		case FACE_DOWN_RIGHT:
			xdelta = -mystep;
			ydelta = mystep;
			break;
		case FACE_DOWN:
			xdelta = -mystep;
			ydelta = 0;
			break;
		case FACE_DOWN_LEFT:
			xdelta = -mystep;
			ydelta = -mystep;
			break;
		case FACE_LEFT:
			xdelta = 0;
			ydelta = -mystep;
			break;
		case FACE_UP_LEFT:
			xdelta = mystep;
			ydelta = -mystep;
			break;
		default:
			xdelta = 0;
			ydelta = 0;
			break;
	}

	return !current_game->world->query_passable(controlx + xdelta, controly + ydelta, controller_);
}

// Returns whether our right-forward is blocked
bool statistics::right_forward_blocked()
{
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = controller_->stepsize();

	mystep = CHECK_STEP_SIZE;
	switch (controller_->curdir())
	{
		case FACE_UP:
			return !current_game->world->query_passable(controlx+mystep, controly-mystep, controller_);
		case FACE_UP_RIGHT:
			return !current_game->world->query_passable(controlx+mystep, controly, controller_);

		case FACE_RIGHT:
			return !current_game->world->query_passable(controlx+mystep, controly+mystep, controller_);
		case FACE_DOWN_RIGHT:
			return !current_game->world->query_passable(controlx, controly+mystep, controller_);
		case FACE_DOWN:
			return !current_game->world->query_passable(controlx-mystep, controly+mystep, controller_);
		case FACE_DOWN_LEFT:
			return !current_game->world->query_passable(controlx-mystep, controly, controller_);
		case FACE_LEFT:
			return !current_game->world->query_passable(controlx-mystep, controly-mystep, controller_);
		case FACE_UP_LEFT:
			return !current_game->world->query_passable(controlx, controly-mystep, controller_);
		default:
			break;

	}
	return false;
}

// Returns whether our right-back is blocked
bool statistics::right_back_blocked()
{
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = controller_->stepsize();

	mystep = CHECK_STEP_SIZE;
	switch (controller_->curdir())
	{
		case FACE_UP:
			return !current_game->world->query_passable(controlx+mystep, controly+mystep, controller_);
		case FACE_UP_RIGHT:
			return !current_game->world->query_passable(controlx, controly+mystep, controller_);

		case FACE_RIGHT:
			return !current_game->world->query_passable(controlx-mystep, controly+mystep, controller_);
		case FACE_DOWN_RIGHT:
			return !current_game->world->query_passable(controlx-mystep, controly, controller_);
		case FACE_DOWN:
			return !current_game->world->query_passable(controlx-mystep, controly-mystep, controller_);
		case FACE_DOWN_LEFT:
			return !current_game->world->query_passable(controlx, controly-mystep,
			                                controller_);
		case FACE_LEFT:
			return !current_game->world->query_passable(controlx+mystep, controly-mystep, controller_);
		case FACE_UP_LEFT:
			return !current_game->world->query_passable(controlx+mystep, controly, controller_);
		default:
			break;
	}

	return false;
}

// Returns whether our front is blocked
bool statistics::forward_blocked()
{
	float xdelta, ydelta;
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = CHECK_STEP_SIZE;

	switch (controller_->curdir())
	{
		case FACE_UP:
			xdelta = 0;
			ydelta = -mystep;
			break;
		case FACE_UP_RIGHT:
			xdelta = mystep;
			ydelta = -mystep;
			break;
		case FACE_RIGHT:
			xdelta = mystep;
			ydelta = 0;
			break;
		case FACE_DOWN_RIGHT:
			xdelta = mystep;
			ydelta = mystep;
			break;
		case FACE_DOWN:
			xdelta = 0;
			ydelta = mystep;
			break;
		case FACE_DOWN_LEFT:
			xdelta = -mystep;
			ydelta = mystep;
			break;
		case FACE_LEFT:
			xdelta = -mystep;
			ydelta = 0;
			break;
		case FACE_UP_LEFT:
			xdelta = -mystep;
			ydelta = -mystep;
			break;
		default:
			xdelta = 0;
			ydelta = 0;
			break;
	}

	return !current_game->world->query_passable(controlx + xdelta, controly + ydelta, controller_);
}

bool statistics::right_walk()
{
	float xdelta, ydelta;

	if (right_blocked() || right_forward_blocked() )
	{
		if (!forward_blocked())  // walk forward
		{
			xdelta = controller_->lastx();
			ydelta = controller_->lasty();
			if (std::fabs(xdelta) > std::fabs(3 * ydelta))
				ydelta = 0;
			if (std::fabs(ydelta) > std::fabs(3 * xdelta))
				xdelta = 0;
			if (xdelta)
				xdelta /= std::fabs(xdelta);
			if (ydelta)
				ydelta /= std::fabs(ydelta);
			//              return controller_->walk();
			return controller_->walkstep(xdelta, ydelta);
		}
		else  // turn left
		{
			controller_->set_enddir(static_cast<char>((controller_->enddir()+6) %8));  // turn left
			return controller_->turn(controller_->enddir());
		}
	}
	else if (forward_blocked())
	{
		controller_->set_enddir(static_cast<char>((controller_->enddir()+6) %8));  // turn left
		return controller_->turn(controller_->enddir());
	}
	else if (right_back_blocked())
	{
		controller_->set_enddir(static_cast<char>((controller_->enddir()+2) %8));  // turn right
		switch (controller_->enddir())
		{
			case FACE_UP:
				xdelta = 0;
				ydelta = -1;
				break;
			case FACE_UP_RIGHT:
				xdelta = 1;
				ydelta = -1;
				break;
			case FACE_RIGHT:
				xdelta = 1;
				ydelta = 0;
				break;
			case FACE_DOWN_RIGHT:
				xdelta = 1;
				ydelta = 1;
				break;
			case FACE_DOWN:
				xdelta = 0;
				ydelta = 1;
				break;
			case FACE_DOWN_LEFT:
				xdelta = -1;
				ydelta = 1;
				break;
			case FACE_LEFT:
				xdelta = -1;
				ydelta = 0;
				break;
			case FACE_UP_LEFT:
				xdelta = -1;
				ydelta = -1;
				break;
			default:
				xdelta = 0;
				ydelta = 0;
				break;
		}
			add_command(COMMAND_WALK, 1, static_cast<Sint32>(xdelta), static_cast<Sint32>(ydelta));
	}
	else
	{
		if (!direct_walk()) //we can't even walk straight to our foe
		{
			switch (controller_->curdir())
			{
				case FACE_UP:
					xdelta = 0;
					ydelta = -1;
					break;
				case FACE_UP_RIGHT:
					xdelta = 1;
					ydelta = -1;
					break;
				case FACE_RIGHT:
					xdelta = 1;
					ydelta = 0;
					break;
				case FACE_DOWN_RIGHT:
					xdelta = 1;
					ydelta = 1;
					break;
				case FACE_DOWN:
					xdelta = 0;
					ydelta = 1;
					break;
				case FACE_DOWN_LEFT:
					xdelta = -1;
					ydelta = 1;
					break;
				case FACE_LEFT:
					xdelta = -1;
					ydelta = 0;
					break;
				case FACE_UP_LEFT:
					xdelta = -1;
					ydelta = -1;
					break;
				default:
					xdelta = 0;
					ydelta = 0;
					break;
			}
			//if (controller_->spaces_clear() > 6)
			return controller_->walkstep(xdelta, ydelta);
		}
	}
	return true;
}

bool statistics::direct_walk()
{
	walker * foe = controller_->foe();
	float xdest, ydest;
	float xdelta, ydelta;
	float xdeltastep, ydeltastep;
	float controlx = controller_->xpos(), controly = controller_->ypos();
	//  short xdistance, ydistance;
	//  Uint32 tempdistance;
	//  char olddir = controller_->curdir();
	//  short oldlastx = controller_->lastx();
	//  short oldlasty = controller_->lasty();

	if (!foe)
		return 0;


	xdest = foe->xpos();
	ydest = foe->ypos();

	xdelta = xdest - controller_->xpos();
	ydelta = ydest - controller_->ypos();
	if (std::fabs(xdelta) > std::fabs(3 * ydelta))
		ydelta = 0;
	if (std::fabs(ydelta) > std::fabs(3 * xdelta))
		xdelta = 0;

	const short xdir = (xdelta > 0.0f) ? 1 : ((xdelta < 0.0f) ? -1 : 0);
	const short ydir = (ydelta > 0.0f) ? 1 : ((ydelta < 0.0f) ? -1 : 0);
	if (controller_->fire_check(xdir, ydir))
	{
		clear_command();
		controller_->turn(controller_->facing(xdelta, ydelta));
		add_command(COMMAND_ATTACK,static_cast<short>(30+rng(25)),0,0);
		return 1;
	}

	if (xdelta)
		xdelta = xdelta / std::fabs(xdelta);
	if (ydelta)
		ydelta = ydelta / std::fabs(ydelta);

	xdeltastep = xdelta*controller_->stepsize();
	ydeltastep = ydelta*controller_->stepsize();

	// Tom's note on 8/3/97: I think these would work better if
	// replaced by some sort of single "if forward_blocked()"
	// check, otherwise I'm not sure if this works regardless of
	// current facing ...
	if (!current_game->world->query_grid_passable(controlx+xdeltastep, controly+ydeltastep, controller_) )
	{
		if (!current_game->world->query_grid_passable(controlx+xdeltastep,controly+0,controller_) )
		{
			if (!current_game->world->query_grid_passable(controlx+0,controly+ydeltastep,controller_) )
			{
				return 0;

				//we cannot get there by ANY of the straight line moves which
				//take us directy toward out foe
			}
			else //y ok
			{
				if (!ydelta)
				{
					return 0;
				}
                                controller_->walkstep(0.0f, ydelta);
				return 1;
				//walk in the y direction
			}
		} //end if(xd,0)
		else //x ok
		{
			if (!xdelta)
			{
				return 0;
			}
                        controller_->walkstep(xdelta, 0.0f);
			return 1;
			//walk in the x direction
		}

	}
	else //x and y ok
	{
		if (!xdelta && !ydelta)
		{
			return 0;
		}
		controller_->walkstep(xdelta, ydelta);
		return 1;
	} //walk in the x and y direction

}

#define PATHING_MIN_DISTANCE 100

// Note that obmap::size() counts dead things too, which don't do pathfinding
#define PATHING_SHORT_CIRCUIT_OBJECT_LIMIT 200

bool statistics::walk_to_foe()
{
    walker* foe = controller_->foe();
	float xdest, ydest;
	float xdelta, ydelta;
	Uint32 tempdistance = 9999999L;
	Sint32 howmany;

	if (!foe || !rng(300) ) //random just to be sure this gets reset sometime
	{
		set_last_distance(15000L);
		set_current_distance(15000L);
		return 0;
	}

	controller_->set_path_check_counter(controller_->path_check_counter() - 1);
	// This makes us only check every few rounds, to save
	// processing time
	if(controller_->path_check_counter() <= 0)
	{
	    // Cosmetic AI-recheck cadence: master draws it from libc rand
	    // (`5 + rand()%10`), NOT the gameplay rng() helper it uses for every
	    // other AI decision here. Production draws from world.rng_ for
	    // snapshot/replay determinism; the parity harness's libc-rand cosmetic
	    // override (when installed) matches master's dual-RNG-stream behavior.
	    const std::uint32_t cadence = (cosmetic_rng_override() != nullptr)
	        ? cosmetic_rng_override()->next(10)
	        : rng(10);
	    controller_->set_path_check_counter(5 + static_cast<int>(cadence));
	    controller_->path_to_foe.clear();
	    
		xdest = foe->xpos();
		ydest = foe->ypos();

		xdelta = xdest - controller_->xpos();
		ydelta = ydest - controller_->ypos();
        
		tempdistance = static_cast<Uint32>(controller_->distance_to_ob(foe));
		// Do simpler pathing if the distance is short or if there are too many walkers (pathfinding is expensive)
		if (tempdistance < PATHING_MIN_DISTANCE || current_game->world->myobmap->size() > PATHING_SHORT_CIRCUIT_OBJECT_LIMIT)
		{
			std::list<walker*> foelist = current_game->world->find_foes_in_range(current_game->world->oblist,
			          PATHING_MIN_DISTANCE, &howmany, controller_);
			if (howmany > 0)
			{
			    walker* firstfoe = foelist.front();
				clear_command();
				controller_->turn(controller_->facing(xdelta, ydelta));
				controller_->stats()->try_command(COMMAND_ATTACK,static_cast<short>(30+ rng(25)), 1, 1);
				current_game->world->find_near_foe(controller_);
				if (!controller_->foe() && firstfoe)
				{
					controller_->set_foe(firstfoe);
					set_last_distance(static_cast<Uint32>(controller_->distance_to_ob(foe)));
				}
				controller_->init_fire();
				return 1;
			}
			else // our foe has moved; we need a new one
			{
				if (!commands.empty())
					commands.front().commandcount = 0;
			}
		}
		else
        {
            controller_->find_path_to_foe();
        }
	} //end if do_check

    if(controller_->path_to_foe.size() > 0)
    {
        controller_->follow_path_to_foe();
        set_last_distance(static_cast<Uint32>(controller_->distance_to_ob(foe)));
    }
    else if(tempdistance < last_distance())// are we closer than we've ever been?
	{
		set_last_distance(tempdistance);   // then set our checking distance ..

		if (!direct_walk())               // Can we walk in a direct line to foe?
		{
			right_walk();                   //   If not, use right-hand walking
		}
	}
	else
		right_walk();

	// Are we really really close? Stop searching, then :)
	if (tempdistance < 30 && !commands.empty())
	{
		commands.front().commandcount = 0;
	}

	return 1;
}

// Stuff for command?
command::command()
{
	commandtype = 0;
	commandcount = 0;
	com1 = com2 = 0;
}
