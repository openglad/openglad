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

  if (someguy) controller = someguy;
  else
  {
     controller = NULL;
     printf("made a stats with no controller!\n");
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

  old_order = controller->order;
  old_family= controller->family;

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
  command *here;
  here = commandlist;
  while(here)
  {
	 commandlist = here;
	 here = here->next;
	 delete commandlist;
	 commandlist = NULL;
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
	 printf("following\n");
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
    if (info1 > 1) info1 = 1;
    if (info1 < -1) info1 = -1;
    if (info2 > 1) info2 = 1;
    if (info2 < -1) info2 = -1;
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
    if (info1 > 1) info1 = 1;
    if (info1 < -1) info1 = -1;
    if (info2 > 1) info2 = 1;
    if (info2 < -1) info2 = -1;
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
	 printf("BLLLLLLLLLLLLLAAAAAAAAAAHHHHHHH!\n");

  force_command(whatcommand, iterations, info1, info2);

}

// Do the current command
short statistics::do_command()
{
  command *here;
  short commandtype, commandcount, com1, com2;
  short i;
//  short ret1, ret2, ret3; //old code
  walker * target;
  short deltax, deltay;
  long distance;

  static short newx, newy;
  #ifdef PROFILING
  profiler docommprofiler("stats::do_command");
  #endif

  //if (!controller || controller->dead)
  if (!controller) // allow dead controllers for now
  {
	 printf("STATS:DO_COM: No controller!\n");
	 wait_for_key(SDLK_z);
	 return 0;
  }

  // Get next command;
  if (commandlist)
  {
	 commandtype = commandlist->commandtype;
	 commandcount = commandlist->commandcount--;
	 com1 = commandlist->com1;
	 com2 = commandlist->com2;
  }
  else
	 return 0;
  if (commandcount < 2) // Last iteration!
  {
	 here = commandlist;
	 commandlist = commandlist->next;
	 if (endlist == here)
		endlist = NULL;
	 delete here;
	 here = NULL;
  }

  switch (commandtype)
  {
	 case COMMAND_WALK:
		//if (controller->query_order() == ORDER_LIVING)
		//{
		  controller->walkstep(com1, com2);
		//}
		break;
	 case COMMAND_FIRE:
		if (!(controller->query_order() == ORDER_LIVING))
		{
		  printf("commanding a non-living to fire?");
		  break;
		}
		if (!controller->fire_check(com1,com2))
		{
		  commandcount = 0;
		  return 0;
		}
		controller->init_fire(com1, com2);
		break;
	 case COMMAND_DIE:  // debugging, not currently used
		if (!controller->dead)
		  printf("Trying to make a living ob die!\n");
		if (commandcount < 2)  // then delete us ..
		  delete_me = 1;
		break;
	 case COMMAND_FOLLOW:   // follow the leader
		if (controller->foe) // if we have foe, don't follow this round
		{
		  commandcount = 0;
		  controller->leader = NULL;
		  return 0;
		}
		if (!controller->leader)
		{
		  if (controller->screenp->numviews == 1)
			 controller->leader = controller->screenp->viewob[0]->control;
		  else
		  {
			 if (controller->screenp->viewob[0]->control->yo_delay)
				controller->leader = controller->screenp->viewob[0]->control;
			 else if (controller->screenp->viewob[1]->control->yo_delay)
				controller->leader = controller->screenp->viewob[1]->control;
			 else
			 {
				commandcount = 0;
				controller->leader = NULL;
				return 0;
			 }
		  }
		}
		if (controller->leader)
		{
		  distance = controller->distance_to_ob(controller->leader);
		  if (distance < 60)
		  {
			 //controller->leader = NULL;
			 return 1;  // don't get too close
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
		if (commandcount < 2)
		  controller->leader = NULL;
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
		  commandcount = 0;
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
		  commandcount = 0;
		  return 1;
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
  commandcount--;       // reduce # of times left
  return 1;
 }

// Determines what to do when we're hit by 'who'
// 'controller' is our parent walker object
void statistics::hit_response(walker  *who)
{
  long distance, i;
  short myfamily;
  long deltax, deltay;
  walker *foe; // who is attacking us?
  long possible_specials[NUM_SPECIALS];
  long threshold; // for hitpoint 'running away'
  char message[80];
  oblink *helplist, *newlist, *here; // for calling for help
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
	newlist = myscreen->find_foes_in_range(myscreen->oblist,
		   200, &howmany, controller);
	// Delete the list to save memory, not needed now
	/* This clean-up code is broken .. why?
	if (howmany && newlist)
	{
	  here = newlist->next;
	  while (here)
	  {
	    delete newlist;
	    newlist = here;
	    here = here->next;
	  }
	  delete newlist;
	}
	*/
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
  oblink *helplist;
  short howmany;
  long deltax, deltay;
  char message[80];

  controller->yo_delay += 80;
  helplist = controller->screenp->find_friends_in_range(
    controller->screenp->oblist, 160, &howmany, controller);
  while (helplist)
  {
    helplist->ob->leader = controller;
      if (foe != helplist->ob->foe)
	helplist->ob->stats->last_distance = helplist->ob->stats->current_distance = 32000;
    helplist->ob->foe = foe;
    //if (helplist->ob->query_act_type() != ACT_CONTROL)
    //  helplist->ob->stats->force_command(COMMAND_FOLLOW, 80, 0, 0);
    helplist = helplist->next;
  }
  deltax = (controller->xpos - foe->xpos);
  if (deltax)
    deltax = (deltax / abs(deltax));
  deltay =  (controller->ypos - foe->ypos);
  if (deltay)
    deltay =  (deltay / abs(deltay));
  // Run away
  force_command(COMMAND_WALK, 16, deltax, deltay);
  // Notify friends of need ...
  if (controller->myguy && (controller->team_num == 0) )
  {
    sprintf(message, "%s yells for help!", controller->myguy->name);
    controller->screenp->do_notify(message, controller);
  }

}

short statistics::query_bit_flags(long myvalue)
{
  return (short) (myvalue & bit_flags);
}

void statistics::set_bit_flags(long someflag, short newvalue)
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
short statistics::right_blocked()
{
  screen  *screenp = controller->screenp;
  short xdelta, ydelta;
  short controlx = controller->xpos, controly = controller->ypos;
  short mystep = (short) controller->stepsize;

  mystep = CHECK_STEP_SIZE;
  switch (controller->curdir)
  {
	 case FACE_UP:          xdelta = mystep; ydelta = 0; break;
	 case FACE_UP_RIGHT:    xdelta = mystep; ydelta = mystep; break;
	 case FACE_RIGHT:       xdelta = 0; ydelta = mystep; break;
	 case FACE_DOWN_RIGHT:  xdelta = (short)(-mystep); ydelta = mystep; break;
	 case FACE_DOWN:        xdelta = (short)(-mystep); ydelta = 0; break;
	 case FACE_DOWN_LEFT:   xdelta = (short)(-mystep); ydelta = (short)(-mystep); break;
	 case FACE_LEFT:        xdelta = 0; ydelta = (short)(-mystep); break;
	 case FACE_UP_LEFT:     xdelta = mystep; ydelta = (short)(-mystep); break;
	 default:          xdelta = 0; ydelta = 0; break;
  }

  return !screenp->query_passable((short) (controlx + xdelta),
				      (short) (controly + ydelta),
				  controller);
}

// Returns whether our right-forward is blocked
short statistics::right_forward_blocked()
{
  screen  *screenp = controller->screenp;
  short controlx = controller->xpos, controly = controller->ypos;
  short mystep = (short) controller->stepsize;

  mystep = CHECK_STEP_SIZE;
  switch (controller->curdir)
  {
    case FACE_UP: return !screenp->query_passable((short) (controlx+mystep),
		  (short) (controly-mystep), controller);
    case FACE_UP_RIGHT:    return !screenp->query_passable(controlx+mystep,
      (short) controly, controller);

    case FACE_RIGHT: return !screenp->query_passable((short) (controlx+mystep),
		(short) (controly+mystep), controller);
    case FACE_DOWN_RIGHT:  return !screenp->query_passable((short) controlx,
      controly+mystep, controller);
    case FACE_DOWN: return !screenp->query_passable((short) (controlx-mystep),
	(short) (controly+mystep), controller);
    case FACE_DOWN_LEFT:   return !screenp->query_passable((short) (controlx-mystep),
      (short) (controly), controller);
    case FACE_LEFT:        return !screenp->query_passable((short) (controlx-mystep),
      (short) (controly-mystep), controller);
    case FACE_UP_LEFT:     return !screenp->query_passable((short) (controlx),
      (short) (controly-mystep), controller);
    default: break;

  }
  return 0;
}

// Returns whether our right-back is blocked
short statistics::right_back_blocked()
{
  screen  *screenp = controller->screenp;
  short controlx = controller->xpos, controly = controller->ypos;
  short mystep = (short) controller->stepsize;

  mystep = CHECK_STEP_SIZE;
  switch (controller->curdir)
  {
	 case FACE_UP:          return !screenp->query_passable((short) (controlx+mystep),
		(short) (controly+mystep), controller);
	 case FACE_UP_RIGHT:    return !screenp->query_passable(controlx,
									(short) (controly+mystep),
									controller);

	 case FACE_RIGHT:       return !screenp->query_passable((short) (controlx-mystep),
		(short) (controly+mystep), controller);
	 case FACE_DOWN_RIGHT:  return !screenp->query_passable((short) (controlx-mystep), controly,
		controller);
	 case FACE_DOWN:        return !screenp->query_passable((short) (controlx-mystep), (short) (controly-mystep),
		controller);
	 case FACE_DOWN_LEFT:   return !screenp->query_passable(controlx, (short) (controly-mystep),
		controller);
	 case FACE_LEFT:        return !screenp->query_passable((short) (controlx+mystep),
		(short) (controly-mystep), controller);
	 case FACE_UP_LEFT:     return !screenp->query_passable((short) (controlx+mystep),
		controly, controller);
	 default: break;
  }

	return 0;
}

// Returns whether our front is blocked
short statistics::forward_blocked()
{
  screen  *screenp = controller->screenp;
  short xdelta, ydelta;
  short controlx = controller->xpos, controly = controller->ypos;
  short mystep = CHECK_STEP_SIZE;

  switch (controller->curdir)
  {
	 case FACE_UP:          xdelta = 0; ydelta = (short) (-mystep); break;
	 case FACE_UP_RIGHT:    xdelta = mystep; ydelta = (short) (-mystep); break;
	 case FACE_RIGHT:       xdelta = mystep; ydelta = 0; break;
	 case FACE_DOWN_RIGHT:  xdelta = mystep; ydelta = mystep; break;
	 case FACE_DOWN:        xdelta = 0; ydelta = mystep; break;
	 case FACE_DOWN_LEFT:   xdelta = (short) (-mystep); ydelta = mystep; break;
	 case FACE_LEFT:        xdelta = (short) (-mystep); ydelta = 0; break;
	 case FACE_UP_LEFT:     xdelta = (short) (-mystep); ydelta = (short) (-mystep); break;
	 default:          xdelta = 0; ydelta = 0; break;
  }

  return !screenp->query_passable((short) (controlx + xdelta), (short) (controly + ydelta),
				   controller);
}

short statistics::right_walk()
{
  short xdelta, ydelta;

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
		if (xdelta) xdelta /= abs(xdelta);
		if (ydelta) ydelta /= abs(ydelta);
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
		 case FACE_UP:          xdelta = 0; ydelta = -1; break;
		 case FACE_UP_RIGHT:    xdelta = 1; ydelta = -1; break;
		 case FACE_RIGHT:       xdelta = 1; ydelta = 0; break;
		 case FACE_DOWN_RIGHT:  xdelta = 1; ydelta = 1; break;
		 case FACE_DOWN:        xdelta = 0; ydelta = 1; break;
		 case FACE_DOWN_LEFT:   xdelta = -1; ydelta = 1; break;
		 case FACE_LEFT:        xdelta = -1; ydelta = 0; break;
		 case FACE_UP_LEFT:     xdelta = -1; ydelta = -1; break;
		 default:          xdelta = 0; ydelta = 0; break;
	  }
	  add_command(COMMAND_WALK, 1, xdelta, ydelta);
  }
  else
  {
	  if (!direct_walk()) //we can't even walk straight to our foe
	  {
		 switch (controller->curdir)
		 {
			case FACE_UP:          xdelta = 0; ydelta = -1; break;
			case FACE_UP_RIGHT:    xdelta = 1; ydelta = -1; break;
			case FACE_RIGHT:       xdelta = 1; ydelta = 0; break;
			case FACE_DOWN_RIGHT:  xdelta = 1; ydelta = 1; break;
			case FACE_DOWN:        xdelta = 0; ydelta = 1; break;
			case FACE_DOWN_LEFT:   xdelta = -1; ydelta = 1; break;
			case FACE_LEFT:        xdelta = -1; ydelta = 0; break;
			case FACE_UP_LEFT:     xdelta = -1; ydelta = -1; break;
			default:          xdelta = 0; ydelta = 0; break;
		 }
		 //if (controller->spaces_clear() > 6)
			return controller->walkstep(xdelta, ydelta);
	  }
  }
  return 1;
}

short statistics::direct_walk()
{
  walker * foe = controller->foe;
  short xdest, ydest;
  short xdelta, ydelta;
  short xdeltastep, ydeltastep;
  short controlx = controller->xpos, controly = controller->ypos;
//  short xdistance, ydistance;
//  unsigned long tempdistance;
//  char olddir = controller->curdir;
//  short oldlastx = controller->lastx;
//  short oldlasty = controller->lasty;

  if (!foe)  return 0;

  
  xdest = foe->xpos;
  ydest = foe->ypos;

  xdelta = (short) (xdest - controller->xpos);
  ydelta = (short) (ydest - controller->ypos);
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
	xdelta = (short) (xdelta / abs(xdelta));
  if (ydelta)
	 ydelta = (short) (ydelta / abs(ydelta));

  xdeltastep = (short) (xdelta*controller->stepsize);
  ydeltastep = (short) (ydelta*controller->stepsize);

  // Tom's note on 8/3/97: I think these would work better if
  // replaced by some sort of single "if forward_blocked()"
  // check, otherwise I'm not sure if this works regardless of
  // current facing ...
  if (!controller->screenp->query_grid_passable(controlx+xdeltastep, controly+ydeltastep, controller) )
  {
    if (!controller->screenp->query_grid_passable(controlx+xdeltastep,controly+0,controller) )
    {
      if (!controller->screenp->query_grid_passable(controlx+0,controly+ydeltastep,controller) )
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

short statistics::walk_to_foe()
{
  walker * foe = controller->foe;
  short xdest, ydest;
  short xdelta,ydelta;
  unsigned long tempdistance;
  static unsigned short do_check;
  short howmany;
  oblink  * foelist;

  if (!foe || !random(300) ) //random just to be sure this gets reset sometime
  {
    last_distance = current_distance = 15000L;
    return 0;
  }

  do_check++;
  // This makes us only check every few rounds, to save
  // processing time
  if (!(do_check % 3)) 
  {
    xdest = foe->xpos;
    ydest = foe->ypos;

    xdelta = (short) (xdest - controller->xpos);
    ydelta = (short) (ydest - controller->ypos);

    tempdistance = (unsigned long) controller->distance_to_ob(foe);
    if (tempdistance < 200 || (tempdistance < last_distance) )
    {
      foelist = controller->screenp->find_foes_in_range(controller->screenp->oblist,
		   200, &howmany, controller);
      if (howmany > 0)
      {
	clear_command();
	controller->turn(controller->facing(xdelta, ydelta));
	controller->stats->try_command(COMMAND_ATTACK,(short) (30+ random(25)), 1, 1);
	controller->screenp->find_near_foe(controller);
	if (!controller->foe && foelist->ob)
	{
	  controller->foe = foelist->ob;
	  last_distance = controller->distance_to_ob(foe);
	}
	controller->init_fire();
	return 1;
      }
      else // our foe has moved; we need a new one
      {
	        // Zardus: PORT: needs to check for commandlist
		if (commandlist)
			commandlist->commandcount = 0;
	//clear_command();
      }
    }
  } //end if do_check

/*
  if (tempdistance <= last_distance)// are we closer than we've ever been?
    last_distance = tempdistance;   // then set our checking distance ..

  if (!direct_walk())               // Can we walk in a direct line to foe?
  {
    right_walk();                   //   If not, use right-hand walking
  }
*/

  if (tempdistance < last_distance)// are we closer than we've ever been?
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
    //clear_command();
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
