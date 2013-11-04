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

#include "graph.h"
#include "stats.h"      // for bit flags, etc.
#include <math.h>

#define CHECK_STEP_SIZE 1 // (controller->stepsize) // was 1

// COMMAND declarations defined at bottom

statistics::statistics(walker  * someguy)
{
	short i;

	if (someguy)
		controller = someguy;
	else
	{
		controller = NULL;
		Log("made a stats with no controller!\n");
	}
	hitpoints = max_hitpoints = 10;
	magicpoints = max_magicpoints = 50;
	level = 1;
	armor = 0; // no default armor
	max_heal_delay = current_heal_delay = 1000;  // default of 50 cycles / hitpoint
	max_magic_delay = current_magic_delay = 1000;
	magic_per_round = 0; //default to not regenerating magic
	heal_per_round = 0; //default to not regenerating hitpoints

	bit_flags = 0;           // clear all the bit flags
	frozen_delay = 0;     // start out able to move :)
	for (i=0; i < 5; i++)
		special_cost[i] = 0; // low default special ability cost in magicpoints
	weapon_cost = 0;

	delete_me = 0;

	// AI finding routine values ..
	last_distance = current_distance = 15000;

	// set head and end to null
	commandlist = NULL;
	endlist = NULL;

    if(controller != NULL)
    {
        old_order = controller->order;
        old_family= controller->family;
    }
    else
    {
        old_order = ORDER_LIVING;
        old_family = FAMILY_SOLDIER;
    }

	name[0] = 0; // set to null string
}

statistics::~statistics()
{
	command *here;
	here = commandlist;
	while(here)
	{
		commandlist = here;
		here = here->next;
		delete commandlist;
		commandlist = NULL;
	}
	controller = NULL;
	delete_me = 1;
}

void statistics::clear_command()
{
	command* here = commandlist;
	while(here)
	{
		command* temp = here;
		here = here->next;
		delete temp;
	}
	commandlist = NULL;
	endlist = NULL;
	// Make sure our weapon type is restored to normal ..
	controller->current_weapon = controller->default_weapon;
	// Make sure we're back to our real team
	if (controller->real_team_num != 255)
	{
		controller->team_num = controller->real_team_num;
		controller->real_team_num = 255;
	}
	controller->leader = NULL;
}

void statistics::add_command(short whatcommand, short iterations,
                             short info1, short info2)
{

	if (whatcommand == COMMAND_DIE)
	{
		delete_me = 1;
		return;
	}

	if (whatcommand == COMMAND_FOLLOW)
	{
		Log("following\n");
	}

	// Add command to end of list
	if (endlist)
	{
		endlist->next = new command;
		endlist = endlist->next;
	}
	else
	{
		endlist = new command;
	}


	// Connect to beginning of list if list was empty
	if (!commandlist)
		commandlist = endlist;

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
	endlist->com1 = info1;
	endlist->com2 = info2;
	endlist->commandtype = whatcommand;
	endlist->commandcount = iterations;
	//  endlist->next = NULL;
}

void statistics::force_command(short whatcommand, short iterations,
                               short info1, short info2)
{
	command *here;

	// Add command to start of list
	if (commandlist)
	{
		here = new command;
		here->next = commandlist;
		commandlist = here;
	}
	else
	{
		commandlist = new command;
	}

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
	commandlist->com1 = info1;
	commandlist->com2 = info2;
	commandlist->commandtype = whatcommand;
	commandlist->commandcount = iterations;
	//  commandlist->next = NULL;
}


// try_command will only set a command if there is
// none in the queue

short statistics::try_command(short whatcommand, short iterations)
{
	if (whatcommand == COMMAND_RANDOM_WALK)
		return try_command(COMMAND_WALK, iterations, (short) (random(3)-1), (short) (random(3)-1));
	else
		return try_command(whatcommand, iterations, 0, 0);
}

short statistics::try_command(short whatcommand, short iterations,
                              short info1, short info2)
{
	add_command(whatcommand, iterations, info1, info2);
	return 0;
}

void statistics::set_command(short whatcommand, short iterations)
{
	if (whatcommand == COMMAND_RANDOM_WALK)
		set_command(COMMAND_WALK, iterations, (short) (random(3)-1), (short) (random(3)-1));
	else
		set_command(whatcommand, iterations, 0, 0);
}

void statistics::set_command(short whatcommand, short iterations,
                             short info1, short info2)
{
	if (whatcommand == COMMAND_DIE)
		Log("BLLLLLLLLLLLLLAAAAAAAAAAHHHHHHH!\n");

	force_command(whatcommand, iterations, info1, info2);

}

// Do the current command
short statistics::do_command()
{
	command *here;
	short commandtype, com1, com2;
	short i;
	
	walker * target;
	short deltax, deltay;
	Sint32 distance;

	short newx = 0;
	short newy = 0;
#ifdef PROFILING

	profiler docommprofiler("stats::do_command");
#endif

	if (!controller) // allow dead controllers for now
	{
		Log("STATS:DO_COM: No controller!\n");
		wait_for_key(KEYSTATE_z);
		return 0;
	}

	// Get next command;
	if (!commandlist)
        return 0;
    
    commandtype = commandlist->commandtype;
    com1 = commandlist->com1;
    com2 = commandlist->com2;
    
    short result = 1;

	switch (commandtype)
	{
		case COMMAND_WALK:
			controller->walkstep(com1, com2);
			break;
		case COMMAND_FIRE:
			if (!(controller->query_order() == ORDER_LIVING))
			{
				Log("commanding a non-living to fire?");
				break;
			}
			if (!controller->fire_check(com1,com2))
			{
				commandlist->commandcount = 0;
				result = 0;
				break;
			}
			controller->init_fire(com1, com2);
			break;
		case COMMAND_DIE:  // debugging, not currently used
			if (!controller->dead)
				Log("Trying to make a living ob die!\n");
			if (commandlist->commandcount < 2)  // then delete us ..
				delete_me = 1;
			break;
		case COMMAND_FOLLOW:   // follow the leader
			if (controller->foe) // if we have foe, don't follow this round
			{
				commandlist->commandcount = 0;
				controller->leader = NULL;
				result = 0;
				break;
			}
			if (!controller->leader)
			{
				if (myscreen->numviews == 1)
					controller->leader = myscreen->viewob[0]->control;
				else
				{
					if (myscreen->viewob[0]->control->yo_delay)
						controller->leader = myscreen->viewob[0]->control;
					else if (myscreen->viewob[1]->control->yo_delay)
						controller->leader = myscreen->viewob[1]->control;
					else
					{
						commandlist->commandcount = 0;
						controller->leader = NULL;
						result = 0;
						break;
					}
				}
			}
			
			// Do we have a leader now?
			if(controller->leader)
			{
				distance = controller->distance_to_ob(controller->leader);
				if (distance < 60)
				{
					controller->leader = NULL;
					result = 1;  // don't get too close
					break;
				}
				newx = (short) (controller->leader->xpos - controller->xpos); // total horizontal distance..
				newy = (short) (controller->leader->ypos - controller->ypos);
				if (abs(newx) > abs(3*newy))
					newy = 0;
				if (abs(newy) > abs(3*newx))
					newx = 0;
				if (newx)                      // If it's not 0, then get
					newx = (short) (newx / abs(newx));     // the normal of it..
				if (newy)
					newy = (short) (newy / abs(newy));
			}  // end of if we had a foe ..
			
			controller->walkstep(newx, newy);
			if (commandlist->commandcount < 2)
            {
				controller->leader = NULL;
            }
			break;
		case COMMAND_QUICK_FIRE:
			controller->walkstep(com1, com2);
			controller->fire();
			break;
		case COMMAND_MULTIDO: // Lets you do <com1> commands in one round
			for(i=0;i<com1; i++)
				do_command();
			break;
		case COMMAND_RUSH:    // Fighter's special
			if (controller->query_order() == ORDER_LIVING)
			{
				controller->walkstep(com1, com2);
				controller->walkstep(com1, com2);
				controller->walkstep(com1, com2);
				if (controller->collide_ob) // We hit someone
				{
					target = controller->collide_ob;
					controller->attack(target);
					target->stats->clear_command();
					// A violent shove... we can't call shove since we
					//  made shove unable to shove enemies.
					target->stats->force_command(COMMAND_WALK,
					                             4,com1,com2 );
				}
			}
			break;
		case COMMAND_SET_WEAPON: // set weapon to specified type
			controller->current_weapon = com1;
			break;
		case COMMAND_RESET_WEAPON: // reset weapon to default type
			controller->current_weapon = controller->default_weapon;
			break;
		case COMMAND_SEARCH: // use right-hand rule to find foe
			if (controller->foe && !controller->foe->dead)
				walk_to_foe();
			else // stop trying to walk to this foe
            {
				commandlist->commandcount = 0;
            }
			break;
		case COMMAND_RIGHT_WALK: // right-hand-walk ONLY
			if (controller->foe)
			{
				distance = controller->distance_to_ob(controller->foe);
				if (distance > 120 && distance < 240)
					right_walk();
				else
					if (!direct_walk())
						right_walk();
			}
			break;
		case COMMAND_ATTACK: // attack a nearby, set foe
			if (!controller->foe || controller->foe->dead)
			{
				commandlist->commandcount = 0;
				result = 1;
				break;
			}
			// Try to walk toward foe, and/or attack ..
			deltax = (short) (controller->foe->xpos - controller->xpos);
			deltay = (short) (controller->foe->ypos - controller->ypos);
			if (abs(deltax) > abs(3*deltay))
				deltay = 0;
			if (abs(deltay) > abs(3*deltax))
				deltax = 0;
			if (deltax)
				deltax /= abs(deltax);
			if (deltay)
				deltay /= abs(deltay);
			if (!controller->fire_check(deltax,deltay))
				controller->walkstep(deltax, deltay);
			else // (controller->fire_check(deltax, deltay))
			{
				force_command(COMMAND_FIRE,(short) random(5),deltax,deltay);
				controller->init_fire(deltax,deltay);
			}
			break;
		case COMMAND_UNCHARM:
			/*
			  if (commandcount > 1)
			  {
			    add_command(COMMAND_UNCHARM, commandcount-1, 0, 0);
			    commandcount = 1;
			  }
			  else if (controller->real_team_num != 255)
			  {
			    controller->team_num = controller->real_team_num;
			    controller->real_team_num = 255;
			  }
			*/
			break;  // end of uncharm case
		default:
			break;
	}
	
	// NOTE: The commandlist ptr might be pointing at a different command than it was before the switch statement.
	// That would make this code decrement the wrong command.
	if(commandlist != NULL)
	{
        commandlist->commandcount--;       // reduce # of times left
        
        if(commandlist->commandcount < 1) // Last iteration!
        {
            commandlist->commandcount = 0;
            
            here = commandlist;
            commandlist = commandlist->next;
            if (endlist == here)
                endlist = NULL;
            delete here;
            here = NULL;
        }
	}
	
	return result;
}

// Determines what to do when we're hit by 'who'
// 'controller' is our parent walker object
void statistics::hit_response(walker  *who)
{
	Sint32 distance, i;
	short myfamily;
	Sint32 deltax, deltay;
	walker *foe; // who is attacking us?
	Sint32 possible_specials[NUM_SPECIALS];
	float threshold; // for hitpoint 'running away'
	short howmany;

	if (!who || !controller)
		return;
	if (who->dead || controller->dead)
		return;

	if (controller->query_act_type() == ACT_CONTROL)
		return;

	if (controller->query_order() != ORDER_LIVING)
		return;

	// Set quick-reference values ..
	myfamily = controller->query_family();
	if (who->query_order() == ORDER_WEAPON && who->owner)
		foe = who->owner;
	else
		foe = who;

	// Determine which specials we can do (by level and sp) ..
	for (i=0; i < NUM_SPECIALS; i++) // first initialize to CAN'T
		possible_specials[i] = 0;
	for (i=0; i <= (level+2)/3; i++) // for all our 'possibles' by level
		if ( (i < NUM_SPECIALS) &&
		        (magicpoints >= special_cost[i]) )
			possible_specials[i] = 1;    // then we can do it.

	switch (myfamily)
	{
		case FAMILY_MAGE:
			if (controller->myguy) // are we a player's character?
				threshold = (3 * max_hitpoints)/5; // then flee at 60%
			else                   // we're an enemy, so be braver :>
				threshold = (3 * max_hitpoints)/8; // flee at 3/8
			if ( (hitpoints < threshold) && possible_specials[1] )
			{
				// Clear old command ..
				// clear_command();
				// teleport
				controller->current_special = 1; // teleport to safety
				controller->shifter_down = 0;    // TELEPORT, not other
				controller->busy = 0; // force-allow us to special
				controller->special();
			}
			else
			{
				if (controller->foe != foe) // we're hit by a new enemy
				{
					controller->foe = foe;
					foe->foe = controller;
					last_distance = current_distance = 15000;
				}
			}
			break;
		case FAMILY_ARCHMAGE:
			controller->busy = 0; // yes, this is a cheat..
			if (controller->myguy) // are we a player's character?
				threshold = (3 * max_hitpoints)/5; // then flee at 60%
			else                   // we're an enemy, so be braver :>
				threshold = (3 * max_hitpoints)/8; // flee at 3/8
			if ( (hitpoints < threshold) && possible_specials[1] && random(3) )
			{
				// teleport
				controller->current_special = 1; // teleport to safety
				controller->shifter_down = 0;
				controller->busy = 0; // force-allow us to special
				controller->special();
			}
			else  // find out how many foes are around us, etc...
			{
				if (controller->foe != foe) // we're hit by a new enemy
				{
					controller->foe = foe;
					foe->foe = controller;
					last_distance = current_distance = 15000;
				}
				myscreen->find_foes_in_range(myscreen->level_data.oblist,
				                                       200, &howmany, controller);
                
				if (howmany) // foes within range?
				{
					if (possible_specials[3]) // can we summon illusion?
					{
						controller->current_special = 3;
						if (controller->special())
							return;
					} // end of do 3rd special
					if (possible_specials[2]) // heartburst, chain lightning, etc.
					{
						//if (howmany < 3) // 2 or fewer enemies, so lightning..
						if (random(2)) // 50/50 now
						{
							controller->shifter_down = 1; // lightning
							controller->current_special = 2;
							if (controller->special())
							{
								controller->shifter_down = 0;
								if (magicpoints >= special_cost[1])  // then leave! :)
								{
									controller->busy = 0;
									controller->special();
								}
								return;
							}
						} // end of lightning
						controller->shifter_down = 0;
						controller->current_special = 2;
						if (controller->special())
						{
							if (magicpoints >= special_cost[1])  // then leave! :)
							{
								controller->busy = 0;
								controller->special();
							}
							return;
						}
					} // end of burst or lightning
				} // end of some foes in range for special attack
			}
			break;
		case FAMILY_ARCHER: // stay at range ..
			{
				if (!controller->foe || controller->foe != foe)
				{
					controller->foe = foe;
					clear_command();
					last_distance = current_distance = 15000;
				}
				distance = controller->distance_to_ob(foe);
				if (distance < 64) // too close!
				{
					deltax = (short) (controller->xpos - foe->xpos);
					if (deltax)
						deltax = (short) (deltax / abs(deltax));
					deltay = (short) (controller->ypos - foe->ypos);
					if (deltay)
						deltay = (short) (deltay / abs(deltay));
					// Run away
					force_command(COMMAND_WALK, 8, deltax, deltay);
				}  // end of too-close check
			} // end of archer case
			break;
		default:  // attack our attacker
			// Chance of doing special ..
			if (controller->check_special() && !random(3) )
				controller->special();
			if (controller->myguy) // are we a player's character?
				threshold = (5 * max_hitpoints)/10; // then flee at 50%
			else                   // we're an enemy, so be braver :>
				threshold = (5 * max_hitpoints)/16; // flee at 5/16%
			if ( (hitpoints < threshold)
			        && !controller->yo_delay) // then yell for help & run ..
			{
				yell_for_help(foe);
			} // end of yell for help
			if (controller->foe != foe) // we're attacked by a new enemy
			{
				// Clear old commands ..
				clear_command();
				// Attack our attacker
				controller->foe = foe;
				foe->foe = controller;
				last_distance = current_distance = 32000;
			}
			break;
	}

}

void statistics::yell_for_help(walker *foe)
{
	short howmany;
	Sint32 deltax, deltay;
	char message[80];

	controller->yo_delay += 80;
	
	// Get AI-controlled allies to target my foe
	std::list<walker*> helplist = myscreen->find_friends_in_range(
	               myscreen->level_data.oblist, 160, &howmany, controller);
	for(auto e = helplist.begin(); e != helplist.end(); e++)
	{
	    walker* w = *e;
		w->leader = controller;
		if (foe != w->foe)
			w->stats->last_distance = w->stats->current_distance = 32000;
		w->foe = foe;
		//if (w->query_act_type() != ACT_CONTROL)
		//  w->stats->force_command(COMMAND_FOLLOW, 80, 0, 0);
	}
	
	// Force run in the opposite direction
	deltax = -(foe->xpos - controller->xpos);
	if (deltax)
		deltax = (deltax / abs(deltax));
	deltay =  -(foe->ypos - controller->ypos);
	if (deltay)
		deltay =  (deltay / abs(deltay));
	// Run away
	force_command(COMMAND_WALK, 16, deltax, deltay);
	// Notify friends of need ...
	if (controller->myguy && (controller->team_num == 0) )
	{
		sprintf(message, "%s yells for help!", controller->myguy->name);
		myscreen->do_notify(message, controller);
	}

}

short statistics::query_bit_flags(Sint32 myvalue)
{
	return (short) (myvalue & bit_flags);
}

void statistics::clear_bit_flags()
{
    bit_flags = 0;
}

void statistics::set_bit_flags(Sint32 someflag, short newvalue)
{
	if (newvalue)
	{
		bit_flags |= someflag;
	}
	else
	{
		bit_flags &= ~someflag;
	}
}

// Returns whether our right is blocked
bool statistics::right_blocked()
{
	float xdelta, ydelta;
	float controlx = controller->xpos, controly = controller->ypos;
	float mystep = controller->stepsize;

	mystep = CHECK_STEP_SIZE;
	switch (controller->curdir)
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

	return !myscreen->query_passable(controlx + xdelta, controly + ydelta, controller);
}

// Returns whether our right-forward is blocked
bool statistics::right_forward_blocked()
{
	float controlx = controller->xpos, controly = controller->ypos;
	float mystep = controller->stepsize;

	mystep = CHECK_STEP_SIZE;
	switch (controller->curdir)
	{
		case FACE_UP:
			return !myscreen->query_passable(controlx+mystep, controly-mystep, controller);
		case FACE_UP_RIGHT:
			return !myscreen->query_passable(controlx+mystep, controly, controller);

		case FACE_RIGHT:
			return !myscreen->query_passable(controlx+mystep, controly+mystep, controller);
		case FACE_DOWN_RIGHT:
			return !myscreen->query_passable(controlx, controly+mystep, controller);
		case FACE_DOWN:
			return !myscreen->query_passable(controlx-mystep, controly+mystep, controller);
		case FACE_DOWN_LEFT:
			return !myscreen->query_passable(controlx-mystep, controly, controller);
		case FACE_LEFT:
			return !myscreen->query_passable(controlx-mystep, controly-mystep, controller);
		case FACE_UP_LEFT:
			return !myscreen->query_passable(controlx, controly-mystep, controller);
		default:
			break;

	}
	return false;
}

// Returns whether our right-back is blocked
bool statistics::right_back_blocked()
{
	float controlx = controller->xpos, controly = controller->ypos;
	float mystep = controller->stepsize;

	mystep = CHECK_STEP_SIZE;
	switch (controller->curdir)
	{
		case FACE_UP:
			return !myscreen->query_passable(controlx+mystep, controly+mystep, controller);
		case FACE_UP_RIGHT:
			return !myscreen->query_passable(controlx, controly+mystep, controller);

		case FACE_RIGHT:
			return !myscreen->query_passable(controlx-mystep, controly+mystep, controller);
		case FACE_DOWN_RIGHT:
			return !myscreen->query_passable(controlx-mystep, controly, controller);
		case FACE_DOWN:
			return !myscreen->query_passable(controlx-mystep, controly-mystep, controller);
		case FACE_DOWN_LEFT:
			return !myscreen->query_passable(controlx, controly-mystep,
			                                controller);
		case FACE_LEFT:
			return !myscreen->query_passable(controlx+mystep, controly-mystep, controller);
		case FACE_UP_LEFT:
			return !myscreen->query_passable(controlx+mystep, controly, controller);
		default:
			break;
	}

	return false;
}

// Returns whether our front is blocked
bool statistics::forward_blocked()
{
	float xdelta, ydelta;
	float controlx = controller->xpos, controly = controller->ypos;
	float mystep = CHECK_STEP_SIZE;

	switch (controller->curdir)
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

	return !myscreen->query_passable(controlx + xdelta, controly + ydelta, controller);
}

bool statistics::right_walk()
{
	float xdelta, ydelta;

	//  if (walkrounds > 60)
	//    if (direct_walk()) return -1;
	//  walkrounds++;

	if (right_blocked() || right_forward_blocked() )
	{
		if (!forward_blocked())  // walk forward
		{
			xdelta = controller->lastx;
			ydelta = controller->lasty;
			if (abs(xdelta) > abs(3*ydelta))
				ydelta = 0;
			if (abs(ydelta) > abs(3*xdelta))
				xdelta = 0;
			if (xdelta)
				xdelta /= abs(xdelta);
			if (ydelta)
				ydelta /= abs(ydelta);
			//              return controller->walk();
			return controller->walkstep(xdelta, ydelta);
		}
		else  // turn left
		{
			controller->enddir = (char) ((controller->enddir+6) %8);  // turn left
			return controller->turn(controller->enddir);
		}
	}
	else if (forward_blocked())
	{
		controller->enddir = (char) ((controller->enddir+6) %8);  // turn left
		return controller->turn(controller->enddir);
	}
	else if (right_back_blocked())
	{
		controller->enddir = (char) ((controller->enddir+2) %8);  // turn right
		switch (controller->enddir)
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
		add_command(COMMAND_WALK, 1, xdelta, ydelta);
	}
	else
	{
		if (!direct_walk()) //we can't even walk straight to our foe
		{
			switch (controller->curdir)
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
			//if (controller->spaces_clear() > 6)
			return controller->walkstep(xdelta, ydelta);
		}
	}
	return true;
}

bool statistics::direct_walk()
{
	walker * foe = controller->foe;
	float xdest, ydest;
	float xdelta, ydelta;
	float xdeltastep, ydeltastep;
	float controlx = controller->xpos, controly = controller->ypos;
	//  short xdistance, ydistance;
	//  Uint32 tempdistance;
	//  char olddir = controller->curdir;
	//  short oldlastx = controller->lastx;
	//  short oldlasty = controller->lasty;

	if (!foe)
		return 0;


	xdest = foe->xpos;
	ydest = foe->ypos;

	xdelta = xdest - controller->xpos;
	ydelta = ydest - controller->ypos;
	if (abs(xdelta) > abs(3*ydelta))
		ydelta = 0;
	if (abs(ydelta) > abs(3*xdelta))
		xdelta = 0;

	if (controller->fire_check(xdelta,ydelta))
	{
		clear_command();
		controller->turn(controller->facing(xdelta, ydelta));
		add_command(COMMAND_ATTACK,(short) (30+random(25)),0,0);
		return 1;
	}

	if (xdelta)
		xdelta = xdelta / fabs(xdelta);
	if (ydelta)
		ydelta = ydelta / fabs(ydelta);

	xdeltastep = xdelta*controller->stepsize;
	ydeltastep = ydelta*controller->stepsize;

	// Tom's note on 8/3/97: I think these would work better if
	// replaced by some sort of single "if forward_blocked()"
	// check, otherwise I'm not sure if this works regardless of
	// current facing ...
	if (!myscreen->query_grid_passable(controlx+xdeltastep, controly+ydeltastep, controller) )
	{
		if (!myscreen->query_grid_passable(controlx+xdeltastep,controly+0,controller) )
		{
			if (!myscreen->query_grid_passable(controlx+0,controly+ydeltastep,controller) )
			{
				walkrounds = 0;
				return 0;

				//we cannot get there by ANY of the straight line moves which
				//take us directy toward out foe
			}
			else //y ok
			{
				if (!ydelta)
				{
					walkrounds = 0;
					return 0;
				}
				controller->walkstep(0,ydelta);
				return 1;
				//walk in the y direction
			}
		} //end if(xd,0)
		else //x ok
		{
			if (!xdelta)
			{
				walkrounds = 0;
				return 0;
			}
			controller->walkstep(xdelta,0);
			return 1;
			//walk in the x direction
		}

	}
	else //x and y ok
	{
		if (!xdelta && !ydelta)
		{
			walkrounds = 0;
			return 0;
		}
		controller->walkstep(xdelta, ydelta);
		return 1;
	} //walk in the x and y direction

}

#define PATHING_MIN_DISTANCE 100

// Note that obmap::size() counts dead things too, which don't do pathfinding
#define PATHING_SHORT_CIRCUIT_OBJECT_LIMIT 200

bool statistics::walk_to_foe()
{
    walker* foe = controller->foe;
	float xdest, ydest;
	float xdelta, ydelta;
	Uint32 tempdistance = 9999999L;
	short howmany;

	if (!foe || !random(300) ) //random just to be sure this gets reset sometime
	{
		last_distance = current_distance = 15000L;
		return 0;
	}

	controller->path_check_counter--;
	// This makes us only check every few rounds, to save
	// processing time
	if(controller->path_check_counter <= 0)
	{
	    controller->path_check_counter = 5 + rand()%10;
	    controller->path_to_foe.clear();
	    
		xdest = foe->xpos;
		ydest = foe->ypos;

		xdelta = xdest - controller->xpos;
		ydelta = ydest - controller->ypos;
        
		tempdistance = (Uint32) controller->distance_to_ob(foe);
		// Do simpler pathing if the distance is short or if there are too many walkers (pathfinding is expensive)
		if (tempdistance < PATHING_MIN_DISTANCE || myscreen->level_data.myobmap->size() > PATHING_SHORT_CIRCUIT_OBJECT_LIMIT)
		{
			std::list<walker*> foelist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
			          PATHING_MIN_DISTANCE, &howmany, controller);
			if (howmany > 0)
			{
			    walker* firstfoe = foelist.front();
				clear_command();
				controller->turn(controller->facing(xdelta, ydelta));
				controller->stats->try_command(COMMAND_ATTACK,(short) (30+ random(25)), 1, 1);
				myscreen->find_near_foe(controller);
				if (!controller->foe && firstfoe)
				{
					controller->foe = firstfoe;
					last_distance = controller->distance_to_ob(foe);
				}
				controller->init_fire();
				return 1;
			}
			else // our foe has moved; we need a new one
			{
				if (commandlist)
					commandlist->commandcount = 0;
			}
		}
		else
        {
            controller->find_path_to_foe();
        }
	} //end if do_check

    if(controller->path_to_foe.size() > 0)
    {
        controller->follow_path_to_foe();
        last_distance = (Uint32) controller->distance_to_ob(foe);
    }
    else if(tempdistance < last_distance)// are we closer than we've ever been?
	{
		last_distance = tempdistance;   // then set our checking distance ..

		if (!direct_walk())               // Can we walk in a direct line to foe?
		{
			right_walk();                   //   If not, use right-hand walking
		}
	}
	else
		right_walk();

	// Are we really really close? Stop searching, then :)
	if (tempdistance < 30 && commandlist)
	{
		commandlist->commandcount = 0;
	}

	return 1;
}

// Stuff for command?
command::command()
{
	commandtype = 0;
	commandcount = 0;
	com1 = com2 = 0;
	next = NULL;
}
