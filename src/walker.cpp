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

#include "graph.h"
#include "combat_math.h"
#include "game_context.h"
#include "smooth.h"
#include "test_trace.h"
#include <format>
#include <span>

// Shorthand for the injectable RNG
static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

// ************************************************************
//  WALKER -- graphics routines
//
//  WALKER is a PIXIEN with automatic frame changing when
//  the direction it moves is changed.  This allows for
//  the concept of "facings", though currently no query-able
//  variable allows for external functions to learn the facing.
// ************************************************************

bool debug_draw_paths = false;

// From picker.cpp
extern Sint32 calculate_level(Uint32 temp_exp);
extern Sint32 difficulty_level[DIFFICULTY_SETTINGS];
extern Sint32 current_difficulty;

// from glad.cpp
short remaining_foes(screen *myscreen, walker* myguy);

walker::walker(const PixieData& data)
    : pixieN(data)
{
	// Set our stats ..
	stats_ = std::make_unique<statistics>(this);

	curdir = FACE_DOWN;  // We are facing DOWN
	enddir = FACE_DOWN;  // We are trying to face DOWN
	lastx = 0;
	lasty = 0;
	act_type = ACT_RANDOM;
	collide_ob = nullptr;
	cycle = 0;
	ani = nullptr;
	team_num = 0;
	real_team_num = 255;  // to show nothing's changed
	ani_type = 0;
	busy = 0;
	foe = nullptr;
	leader = nullptr;
	owner = nullptr;
	myguy = nullptr;
	owns_myguy = false;
	myself_ = this;
	dead = 0; // we're alive

	death_called = 0;


	bonus_rounds = 0;
	shifter_down = 0; // the player's shifter/alternate is NOT pressed
	view_all = 0;     // by default can't see treasures, etc. on radar
	keys = 0; // no keys

	action = 0; // no special action mode
	ignore = 0; // don't ignore us! Collide with us...
	default_weapon = current_weapon = FAMILY_KNIFE; // just in case ..
	user = -1; // default user status = no user
	// Set our stats ..
	set_frame(0);

	yo_delay = 0;

	flight_left = 0;
	invulnerable_left = 0;
	invisibility_left = 0;
	speed_bonus = 0;
	speed_bonus_left = 0;
	regen_delay_ = 0;
	charm_left_ = 0;
	outline = 0;
	drawcycle = 0;

	skip_exit = 0;
	xpos = ypos = -1; //this to correct a problem with these not being alloced?
	worldx_ = worldy_ = -1;

	weapons_left = 1; // default, used for fighters

	myobmap = nullptr;
	if(myscreen != nullptr)
        myobmap = myscreen->level_data.myobmap.get();  // default obmap (spatial partitioning optimization?) changed when added to a list

	path_check_counter = 5 + rand()%10;
	hurt_flash = false;
	attack_lunge = 0.0f;
	hit_recoil = 0.0f;

	last_hitpoints = 0.0f;
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

	// //  team_num = 0;
	// //  ani_type = 0;
	// //  busy = 0;

	//  foe = nullptr;
	//  leader = nullptr;
	//  owner = nullptr;
	//  myguy = nullptr;
	//  myself = this;
	//  ani = nullptr;
	dead = 0; // we're alive

	death_called = 0;


	//  bonus_rounds = 0;
	//  shifter_down = 0; // the player's shifter/alternate is NOT pressed
	//  view_all = 0;     // by default can't see treasures, etc. on radar
	//  keys = 0; // no keys

	//  action = 0; // no special action mode
	ignore = 0; // don't ignore us! Collide with us...
	//  default_weapon = current_weapon = FAMILY_KNIFE; // just in case ..
	//  user = -1; // default user status = no user
	// Set our stats ..
	//  set_frame(0);

	//  yo_delay = 0;

	flight_left = 0;
	//  invulnerable_left = 0;
	//  invisibility_left = 0;
	//  outline = 0;
	//  drawcycle = 0;

	//  skip_exit = 0;
	//  xpos = ypos = -1; //this to correct a problem with these not being alloced?

	//  weapons_left = 1; // default, used for fighters
	path_check_counter = 5 + rand()%10;
    regen_delay_ = 0;

	if (stats_)
		stats_->bit_flags = 0;

	hurt_flash = false;
	attack_lunge = 0.0f;
	hit_recoil = 0.0f;

	last_hitpoints = 0.0f;
	
	return 1;
}

walker::~walker()
{
	//  Log("(Death) Removed ORDER %d FAMILY %d, pos %dx%d\n", order, family,
	//    xpos, ypos); //debugging memory
	foe = nullptr;
	leader = nullptr;
	owner = nullptr;
	collide_ob = nullptr;
	dead = 1;

	if(myobmap != nullptr)
        myobmap->remove(this); // remove ourselves from obmap lists

	stats_.reset();
	bmp = nullptr;
	if (owns_myguy && myguy)
		delete myguy;
	myguy = nullptr;
	owns_myguy = false;
	myself_ = nullptr;


}

short walker::move(short x, short y)
{
	return setxy(static_cast<short>(xpos+x), static_cast<short>(ypos+y));
}

void walker::worldmove(float x, float y)
{
	return setworldxy(worldx_+x, worldy_+y);
}

bool walker::setxy(short x, short y)
{
    worldx_ = x;
    worldy_ = y;
    
	if (!ignore)
		myobmap->move(this, x, y);
	else // just remove us, in case :)
		myobmap->remove(this);

	return pixie::setxy(x, y);
}

void walker::setworldxy(float x, float y)
{
    worldx_ = x;
    worldy_ = y;
    
	if (!ignore)
		myobmap->move(this, x, y);
	else // just remove us, in case :)
		myobmap->remove(this);

	pixie::setxy(x, y);
}

// WALK -- This function allows us to change facing when we walk.
// This includes an automatic frame change. It also redraws the background
// at the coords it used to occupy.
// It calls the lower level function MOVE.
bool walker::walk()
{
	return walker::walk(lastx, lasty);
}

short walker::facing(short x, short y)
{
	Sint32 bigy = y*1000;
	Sint32 slope;

	if (!x)
	{
		if (y>0)
			return FACE_DOWN;
		else
			return FACE_UP;
	}

	slope = bigy / x;

	if (x>0)
	{
		if (slope > 2414)
			return FACE_DOWN;
		if (slope > 414)
			return FACE_DOWN_RIGHT;
		if (slope > -414)
			return FACE_RIGHT;
		if (slope > -2414)
			return FACE_UP_RIGHT;
		return FACE_UP;
	}
	else
	{
		if (slope > 2414)
			return FACE_UP;
		if (slope > 414)
			return FACE_UP_LEFT;
		if (slope > -414)
			return FACE_LEFT;
		if (slope > -2414)
			return FACE_DOWN_LEFT;
		return FACE_DOWN;
	}
}

short walker::shove(walker  *target, short x, short y)
{

	// this code has been moved to living, we should only shove livings

	if (x || y || target)
		Log("Shoving a non-living. ORDER: {} FAMILY: {}\n", static_cast<int>(order), static_cast<int>(family));
	return -1;

}

bool walker::walkstep(float x, float y)
{
	short returnvalue;
	short ret1 = 0, ret2 = 0;
	short oldcurdir = curdir;
	float step = stepsize;
	float halfstep;
	Sint32 i;
	//walker *control1 = myscreen->viewob[0]->control;
	//walker *control2;
	short mycycle;

	// Repeat last walk.
	lastx = x*stepsize;
	lasty = y*stepsize;

	if (order==Order::Living && family==FAMILY_TOWER1)
	{
		curdir = facing(x, y);
		enddir = curdir;
		lastx = x;
		lasty = y;
		return 1;
	}
	returnvalue = walk(x*stepsize, y*stepsize);
	halfstep = 1;

	if (!returnvalue) // couldn't walk this direction ..
	{
		returnvalue = walk(x*halfstep, y*halfstep); // Now try a baby step
		if (!returnvalue) // if we still fail
		{
			if (user == -1) // means we are an npc
			{
				switch (facing(x, y))
				{
					case FACE_UP:    // For cardinal directions, fail if
						curdir = FACE_LEFT;
						ret1 = walk(-step, 0);
						break;
					case FACE_RIGHT: // we can't walk this direction
						curdir = FACE_UP;
						ret1 = walk(0, -step);
						break;
					case FACE_DOWN:
						curdir = FACE_RIGHT;
						ret1 = walk(step, 0);
						break;
					case FACE_LEFT:
						curdir = FACE_DOWN;
						ret1 = walk(0, step);
						break;
						//return returnvalue;
					case FACE_UP_RIGHT:
						curdir = FACE_UP;
						ret1 = walk(0, y*step);
						curdir = FACE_RIGHT;
						ret2 = walk(x*step, 0);
						break;
					case FACE_DOWN_RIGHT:
						curdir = FACE_DOWN;
						ret1 = walk(0, y*step);
						curdir = FACE_RIGHT;
						ret2 = walk(x*step, 0);
						break;
					case FACE_DOWN_LEFT:
						curdir = FACE_DOWN;
						ret1 = walk(0, y*step);
						curdir = FACE_LEFT;
						ret2 = walk(x*step, 0);
						break;
					case FACE_UP_LEFT:
						curdir = FACE_UP;
						ret1 = walk(0, y*step);
						curdir = FACE_LEFT;
						ret2 = walk(x*step, 0);
						break;
					default:
						ret1 = 0;
						ret2 = 0;
						break;
				}
			} // end of npc switch
			else // we're a user
			{
			    // We can't move where we want to.  Can we slide against the wall?
			    
				// Store our cycle
				mycycle = cycle;
				short myfacing = facing(x, y);
                bool gotup = false, gotover = false;
                short dx = 0, dy = 0;
                
				switch (myfacing)
				{
					case FACE_UP:    // For cardinal directions, fail if
					case FACE_RIGHT: // we can't walk this direction
					case FACE_DOWN:
					case FACE_LEFT:
						break;
					case FACE_UP_RIGHT:
					    dx = 1;
					    dy = -1;
						break;
					case FACE_DOWN_RIGHT:
					    dx = 1;
					    dy = 1;
						break;
					case FACE_DOWN_LEFT:
					    dx = -1;
					    dy = 1;
						break;
					case FACE_UP_LEFT:
					    dx = -1;
					    dy = -1;
						break;
					default:
						ret1 = 0;
						ret2 = 0;
						break;
				}
				
				if(dx != 0 || dy != 0)
                {
                    for (i = 0; i < step; i++)
                    {
                        if (myscreen->query_passable(xpos, ypos + dy, this))
                        {
                            worldmove(0, dy);  // walk without turning ..
                            gotup = true;
                        }
                        if (myscreen->query_passable(xpos + dx, ypos, this))
                        {
                            worldmove(dx, 0);
                            gotover = true;
                        }
                        if (!gotup && gotover)  // moved horizontally
                        {
                            if(dx > 0)
                                curdir = FACE_RIGHT;
                            else
                                curdir = FACE_LEFT;
                        }
                        else if (gotup && !gotover) // moved vertically
                        {
                            if(dy < 0)
                                curdir = FACE_UP;
                            else
                                curdir = FACE_DOWN;
                        }
                        if (gotup || gotover) // we moved somewhere?
                        {
                            cycle = mycycle;
                            cycle++;
                            if (ani[curdir][cycle] == -1)
                                cycle = 0;
                            set_frame(ani[curdir][cycle]);
                        }  // end of cycled us a frame
                    }
                }
			}

			curdir = static_cast<char>(oldcurdir);
			return ( ret1 || ret2 );
		}
	}
	return returnvalue;
}

bool walker::walk(float x, float y)
{
	short dir;

	dir = facing(x, y);

	if (order==Order::Living && family==FAMILY_TOWER1)
	{
		curdir = dir;
		return 1;
	}

	if ( !x && !y)
	{
		//Log("walker %d:%d walking 0,0\n",order, family);
		//this happens sometimes, and shouldn't, but it is non-fatal
		return 1;
	}
	if (curdir == dir)  // if continue direction
	{
		// check if off map
		if (x+xpos < 0 ||
		        x+xpos >= myscreen->level_data.grid.w*GRID_SIZE ||
		        y+ypos < 0 ||
		        y+ypos >= myscreen->level_data.grid.h*GRID_SIZE)
		{
			return 0;
		}

		// Here we check if the move is valid
		if (myscreen->query_passable(xpos+x, ypos+y, this))
		{
			// Control object does complete redraw anyway
			worldmove(x,y);
			cycle++;
			//if (!ani || (curdir*cycle > sizeof(ani)) )
			//  Log("WALKER::WALK: Bad ani!\n");
			if (ani[curdir][cycle] == -1)
				cycle = 0;
			set_frame(ani[curdir][cycle]);
			return 1;
		}
		else //Invalid move?
		{
			//we're not alive

			if (stats_->query_bit_flags(BIT_ANIMATE) )  // animate regardless..
			{
				cycle++;
				if (ani[curdir][cycle] == -1)
					cycle = 0;
				set_frame(ani[curdir][cycle]);
			}
			return 0;
		}
	}
	else  // changed direction
	{
		curdir = static_cast<char>(dir);
		cycle = 0;
		set_frame(ani[curdir][cycle]);
		worldmove(0,0);
	}
	return 1;
}

float walker::get_current_angle()
{
    switch (curdir)
    {
        case FACE_UP:
            return -M_PI_2;
        case FACE_UP_RIGHT:
            return -M_PI_4;
        case FACE_RIGHT:
            return 0.0f;
        case FACE_DOWN_RIGHT:
            return M_PI_4;
        case FACE_DOWN:
            return M_PI_2;
        case FACE_DOWN_LEFT:
            return 3*M_PI_4;
        case FACE_LEFT:
            return M_PI;
        case FACE_UP_LEFT:
            return 5*M_PI_4;
        default:
            return 0.0f;
    }
}

bool walker::turn(short targetdir)
{
	short distance;

	//   We use a clock-ordered
	//   of directions to numbers) to a clock-ordered
	//   mapping of directions so we can calculate what
	//   our next facing should be based on our current one.

	// Find how  we have to turn.
	distance = static_cast<short>(curdir - targetdir);

	// Figure out if we should turn clockwise or counterclockwise
	if ( ( (distance >= -4) && (distance < 0) ) || (distance >= 4) )
		curdir = static_cast<char>((curdir+1) %8);
	else
		curdir = static_cast<char>((curdir+7) %8);

	// Now set our lastx and lasty (facing) variables correctly
	if ( (order!=Order::Living) || (family!=FAMILY_TOWER1) )
	{
		switch (curdir)
		{
			case FACE_UP:
				lastx = 0;
				lasty = -stepsize;
				break;
			case FACE_UP_RIGHT:
				lastx = stepsize;
				lasty = -stepsize;
				break;
			case FACE_RIGHT:
				lastx = stepsize;
				lasty = 0;
				break;
			case FACE_DOWN_RIGHT:
				lastx = stepsize;
				lasty = stepsize;
				break;
			case FACE_DOWN:
				lastx = 0;
				lasty = stepsize;
				break;
			case FACE_DOWN_LEFT:
				lastx = -stepsize;
				lasty = stepsize;
				break;
			case FACE_LEFT:
				lastx = -stepsize;
				lasty = 0;
				break;
			case FACE_UP_LEFT:
				lastx = -stepsize;
				lasty = -stepsize;
				break;
			default :
				lastx = 0;
				lasty = -stepsize;
		}
	}
	cycle = 0;
	set_frame(ani[curdir][cycle]);
	worldmove(0,0);
	return true;
}

// This is the function you actually call when you want something
// to fire.  It initializes the animation if animation is valid
// and checks to see if the object is too busy.
bool walker::init_fire()
{
	return init_fire(lastx, lasty);
}

bool walker::init_fire(short xdir, short ydir)
{
	// Turn if we want to fire another direction

	// If a non-player fires in a set direction, turn!

	if (facing(xdir, ydir) != curdir)
	{
		enddir = static_cast<char>(facing(xdir, ydir));
	}
	if (curdir != enddir && query_order() == Order::Living)
	{
		//if (family==FAMILY_TOWER1)
		//  enddir = curdir;
		if (query_act_type() == ACT_CONTROL)
			return 0;
		else
			return turn(enddir);
	}

	if (busy > 0)
		return 0;  // Too busy

	busy += fire_frequency; // This pauses a few rounds

	//  if (ani_type == ANI_WALK && query_order() == Order::Living)
	if (ani_type == ANI_WALK)  // This should allow generators to animate
	{
		ani_type = ANI_ATTACK;
		cycle = 0;
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
	if (stats_->magicpoints < stats_->weapon_cost)
		return nullptr;

	weapon = create_weapon();
	if (!weapon)
		return nullptr;

	stats_->magicpoints -= stats_->weapon_cost;

	// Determine how much the thrown weapon can 'waver'
	waver = static_cast<signed char>((weapon->stepsize)/2); // Absolute amount ..
	waver = static_cast<signed char>(rng(waver+1) - waver/2);

	switch(facing(lastx, lasty))
	{
		case FACE_RIGHT:
			weapon->setxy(xpos+sizex+1,ypos+(sizey - weapon->sizey)/2);
			weapon->lastx = weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+(sizey-weapon->sizey)/2);
			weapon->lastx = -weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_DOWN:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos+sizey+1);
			weapon->lasty = weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos - weapon->sizey-1);
			weapon->lasty = - weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos-weapon->sizey-1);
			weapon->lastx = weapon->stepsize + waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_UP_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos-weapon->sizey-1);
			weapon->lastx = -weapon->stepsize - waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_DOWN_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos + sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = weapon->stepsize - waver;
			break;
		case FACE_DOWN_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = -weapon->stepsize + waver;
			break;
	}

	weapon->set_frame(frame);
	// Make sure our current direction is wrong so first walk
	// will just be draw (grumble curse)
	weapon->curdir = static_cast<char>((frame+1)%2);

	//xp = weapon->xpos;
	//yp = weapon->ypos;

	// Actual combat
	if (!myscreen->query_passable(weapon->xpos, weapon->ypos, weapon))
	{
		// *** Melee combat ***
		if (weapon->collide_ob && !weapon->collide_ob->dead)
		{
			if (attack(weapon->collide_ob) && on_screen() )
			{
				myscreen->soundp->play_sound(SOUND_CLANG);
				
                if(cfg.is_on("effects", "attack_lunge"))
                {
                    if(query_order() == Order::Living)
                    {
                        attack_lunge = 1.0f;
                        attack_lunge_angle = get_current_angle();
                    }
                }
			}
			if (myguy)
            {
				myguy->total_shots++; // record that we fired/attacked
				myguy->scen_shots++;
            }
		}
		weapon->dead = 1;
		return nullptr;
	}
	else if (stats_->query_bit_flags(BIT_NO_RANGED))
	{
		weapon->dead = 1;
		return nullptr;
	}
	else
	{
		if (order==Order::Living && family==FAMILY_SOLDIER)
        {
            if(weapons_left <= 0)
            {
                // Give back the magic it cost, since we didn't throw it
                stats_->magicpoints += stats_->weapon_cost;
                weapon->dead = 1;
                return nullptr;
            }
            else
                weapons_left--;
        }
        
		// Record our shot ..
		if (myguy)
        {
			myguy->total_shots++;
			myguy->scen_shots++;
        }

		// *** Ranged combat ***
		if (on_screen())
		{
			if (weapon->query_family() == FAMILY_FIREBALL)
				myscreen->soundp->play_sound(SOUND_BLAST);
			else if (weapon->query_family() == FAMILY_METEOR)
				//play_sound(SOUND_FIREBALL);
				myscreen->soundp->play_sound(SOUND_BLAST);
			else if (weapon->query_family() == FAMILY_SPRINKLE)
				//play_sound(SOUND_SPARKLE);
				myscreen->soundp->play_sound(SOUND_SPARKLE);
			else if (weapon->query_family() == FAMILY_ARROW)
				//play_sound(SOUND_FIRE);
				myscreen->soundp->play_sound(SOUND_BOW);
			else if (weapon->query_family() == FAMILY_FIRE_ARROW)
				//play_sound(SOUND_FIRE);
				myscreen->soundp->play_sound(SOUND_BOW);
			else if (weapon->query_family() == FAMILY_LIGHTNING)
				myscreen->soundp->play_sound(SOUND_BOLT);
			else //play_sound(SOUND_FWIP);
				myscreen->soundp->play_sound(SOUND_FWIP);
		}
		if (order == Order::Generator)
		{
			switch (family)
			{
				case FAMILY_TOWER: // mages, no lifetime
					weapon->ani_type = ANI_TELE_IN; // mages teleport
				case FAMILY_TREEHOUSE: // elves also no lifetime
					weapon->stats()->level = rng(stats_->level)+1;
					weapon->set_difficulty( static_cast<Uint32>(weapon->stats()->level) );
					weapon->owner = nullptr;
					break;
				default: // tents, bones, etc
					weapon->lifetime = 800 + stats_->level*11;
					weapon->stats()->level = rng(stats_->level)+1;
					weapon->set_difficulty(static_cast<Uint32>(weapon->stats()->level));
					break;
			}
		}
		else if (order == Order::Living)
		{
			switch (family)
			{
				case FAMILY_ARCHMAGE:
				    {
				        // ArchMage gets 1/20th of 'extra'
                        // magic for more damage ...
                        float extra = stats_->magicpoints / 20;
                        stats_->magicpoints -= extra;
                        weapon->damage += extra; // get this in damage
				    }
					break;
				default: // do nothing
					break;
			}  // end of switch for living family
		} // end of switch for living
		return weapon;
	}

}

void walker::set_weapon_heading(walker *weapon)
{
	signed char waver;

	// Determine how much the thrown weapon can 'waver'
	waver = static_cast<signed char>((weapon->stepsize)/2); // Absolute amount ..
	waver = static_cast<signed char>(rng(waver+1) - waver/2);

	switch(facing(lastx, lasty))  // these are from the 'owner'
	{
		case FACE_RIGHT:
			weapon->setxy(xpos+sizex+1,ypos+(sizey - weapon->sizey)/2);
			weapon->lastx = weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+(sizey-weapon->sizey)/2);
			weapon->lastx = -weapon->stepsize;
			weapon->lasty = waver;
			break;
		case FACE_DOWN:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos+sizey+1);
			weapon->lasty = weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP:
			weapon->setxy(xpos+(sizex-weapon->sizex)/2, ypos - weapon->sizey-1);
			weapon->lasty = - weapon->stepsize;
			weapon->lastx = waver;
			break;
		case FACE_UP_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos-weapon->sizey-1);
			weapon->lastx = weapon->stepsize + waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_UP_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos-weapon->sizey-1);
			weapon->lastx = -weapon->stepsize - waver;
			weapon->lasty = -weapon->stepsize + waver;
			break;
		case FACE_DOWN_RIGHT:
			weapon->setxy(xpos+sizex+1, ypos + sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = weapon->stepsize - waver;
			break;
		case FACE_DOWN_LEFT:
			weapon->setxy(xpos - weapon->sizex-1, ypos+sizey+1);
			weapon->lasty = weapon->stepsize + waver;
			weapon->lastx = -weapon->stepsize + waver;
			break;
	}

}

// To avoid problems with limited precision
bool float_eq(float a, float b)
{
    return (a == b || (a - 0.000001f < b && a + 0.000001f > b));
}

void draw_smallHealthBar(walker* w, viewscreen* view_buf)
{
    if(!cfg.is_on("effects", "mini_hp_bar"))
        return;
    
    if(w->query_order() != Order::Living && w->query_order() != Order::Generator)
        return;
    
	Sint32 xscreen = static_cast<Sint32>(w->xpos - view_buf->topx + view_buf->xloc);
	Sint32 yscreen = static_cast<Sint32>(w->ypos - view_buf->topy + view_buf->yloc);
    
    
    Sint32 walkerstartx = xscreen;
    Sint32 walkerstarty = yscreen;
    Sint32 portstartx = view_buf->xloc;
    Sint32 portstarty = view_buf->yloc;
    Sint32 portendx = view_buf->endx;
    Sint32 portendy = view_buf->endy;
    
    
    SDL_Rect r = {Sint16(walkerstartx), Sint16(walkerstarty + w->sizey + 1), Uint16(w->sizex), 1};
    if(r.x < portstartx || r.x > portendx
       || r.y < portstarty || r.y > portendy)
       return;
    
    // Last hit's effect
    float last_points = w->last_hitpoints;
    float last_ratio = float(last_points)/w->stats()->max_hitpoints;
    
    // Current HP
    float points = w->stats()->hitpoints;
    float ratio = float(points)/w->stats()->max_hitpoints;
    
    char whatcolor;
    
    if (float_eq(points, w->stats()->max_hitpoints))
        whatcolor = MAX_HP_COLOR;
    else if ( (points * 3) < w->stats()->max_hitpoints)
        whatcolor = LOW_HP_COLOR;
    else if ( (points * 3 / 2) < w->stats()->max_hitpoints)
        whatcolor = MID_HP_COLOR;
    else if (points < w->stats()->max_hitpoints)
        whatcolor = LIGHT_GREEN;//HIGH_HP_COLOR;
    else 
        whatcolor = ORANGE_START;
    
    if(ratio >= 0.0f)
    {
        if(ratio < 0.95f)
        {
            Uint16 max_w = r.w;
            
            if(w->last_hitpoints > w->stats()->hitpoints && last_ratio <= 1.0f)
                myscreen->draw_box(r.x, r.y, r.x + r.w*last_ratio, r.y + r.h, 53, 1);
                
            myscreen->draw_box(r.x, r.y, r.x + r.w*ratio, r.y + r.h, whatcolor, 1);
            myscreen->draw_box(r.x-1, r.y-1, r.x + max_w+1, r.y + r.h+1, BLACK, 0);
        }
    }
}


walker::DamageNumber::DamageNumber(float x, float y, float value, unsigned char color)
    : x(x), y(y), t(1.0f), value(value), color(color)
{}

void walker::DamageNumber::draw(viewscreen* view_buf)
{
	Sint32 xscreen = static_cast<Sint32>(x - view_buf->topx + view_buf->xloc);
	Sint32 yscreen = static_cast<Sint32>(y - view_buf->topy + view_buf->yloc);
	
	Uint8 alpha = 0;
	if(t >= 255)
        alpha = 255;
    else if(t >= 0)
        alpha = t*255;
    myscreen->text_normal.write_xy_center_alpha(xscreen, yscreen, color, alpha, "%.0f", value);
}

#define ATTACK_LUNGE_SIZE 5
#define HIT_RECOIL_SIZE 3

bool walker::draw(viewscreen  *view_buf)
{
    // Update the drawing coords from the real position
    xpos = worldx_;
    ypos = worldy_;
    
	Sint32 xscreen, yscreen;

	//no need for on screen check, it will be checked at the draw level
	//and the draw level code is cleaner anyway
	//if (!this) return 0;
	if (dead)
	{
		Log("drawing a dead guy!\n");
		return 0;
	}
	//if (!bmp) {Log("No bitmap!\n"); return 0;}
	drawcycle++;

	xscreen = static_cast<Sint32>(xpos - view_buf->topx + view_buf->xloc);
	yscreen = static_cast<Sint32>(ypos - view_buf->topy + view_buf->yloc);
	
	if(attack_lunge > 0.0f)
    {
        xscreen += attack_lunge*ATTACK_LUNGE_SIZE*cos(attack_lunge_angle);
        yscreen += attack_lunge*ATTACK_LUNGE_SIZE*sin(attack_lunge_angle);
    }
    
	if(hit_recoil > 0.0f)
    {
        xscreen += hit_recoil*HIT_RECOIL_SIZE*cos(hit_recoil_angle);
        yscreen += hit_recoil*HIT_RECOIL_SIZE*sin(hit_recoil_angle);
    }

	if (stats_->query_bit_flags( BIT_NAMED ) || invisibility_left || flight_left || invulnerable_left)
	{
		if (outline == OUTLINE_INVULNERABLE)
		{
			if      (flight_left)
				outline = OUTLINE_FLYING;
			else if (view_buf->control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
		}
		else if (outline == OUTLINE_FLYING)
		{
			//if      (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num)) outline = OUTLINE_NAMED;
			//else if (invisibility_left) outline = OUTLINE_INVISIBLE;
			//else if (invulnerable_left) outline = OUTLINE_INVULNERABLE;

			if (view_buf->control)
				if      (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
			{
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
				else if (invulnerable_left)
					outline = OUTLINE_INVULNERABLE;
			}
		}
		else if (outline == OUTLINE_NAMED)
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
		}
		else if (outline == OUTLINE_INVISIBLE)
		{
			if      (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (view_buf->control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;
		}
		else
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (view_buf->control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=view_buf->control->team_num))
					outline = OUTLINE_NAMED;
		}
	}
	else
	{
	    outline = 0;
	}
	
	if(view_buf->control != nullptr)
    {
        if(outline == 0 && user != -1 && this != view_buf->control && this->team_num == view_buf->control->team_num)
            outline = OUTLINE_INVISIBLE;
    }
    
    bool should_draw_hp = true;
    int fill_mode = 0;
    int outline_style = 0;
    int invisibility_amount = 0;
    int phantom_mode = 0;
    
	if (stats_->query_bit_flags(BIT_PHANTOM)) //WE ARE A PHANTOM
    {
        fill_mode = PHANTOM_MODE;
        phantom_mode = SHIFT_RANDOM;
        should_draw_hp = false;
    }
	else if (invisibility_left && view_buf->control != nullptr)  //WE ARE INVISIBLE
	{
		if (this->team_num == view_buf->control->team_num)
        {
            fill_mode = INVISIBLE_MODE;
            invisibility_amount = ( invisibility_left + 10 );
            outline_style = outline;
            should_draw_hp = false;
        }
	}
	else if (stats_->query_bit_flags(BIT_FORESTWALK) && 
	         myscreen->level_data.mysmoother.query_genre_x_y(xpos/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	         && !stats_->query_bit_flags(BIT_FLYING)
	         && (flight_left < 1) )
    {
        fill_mode = INVISIBLE_MODE;
        invisibility_amount = 1000;
        outline_style = 1;
        should_draw_hp = false;
    }
	else if (outline)    // WE HAVE SOME OUTLINE
	{
	    fill_mode = OUTLINE_MODE;
	    outline_style = outline;
	}
	
	// Draw me
	if(hurt_flash)
    {
        hurt_flash = false;
        
        auto bmp_span = std::span<const unsigned char>{bmp, static_cast<size_t>(sizex * sizey)};
        myscreen->walkputbuffer_flash(xscreen, yscreen, sizex, sizey,
                                   view_buf->xloc, view_buf->yloc,
                                   view_buf->endx, view_buf->endy,
                                   bmp_span, query_team_color());
    }
    else
    {
        auto bmp_span = std::span<const unsigned char>{bmp, static_cast<size_t>(sizex * sizey)};
        if(fill_mode == 0 && outline_style == 0)
        {
            myscreen->walkputbuffer(xscreen, yscreen, sizex, sizey,
                                   view_buf->xloc, view_buf->yloc,
                                   view_buf->endx, view_buf->endy,
                                   bmp_span, query_team_color());
        }
        else
        {
            myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
                                    view_buf->xloc, view_buf->yloc,
                                    view_buf->endx, view_buf->endy,
                                    bmp_span, query_team_color(),
                                    fill_mode, //mode
                                    invisibility_amount, //invisibility
                                    outline_style, //outline
                                    phantom_mode); //type of phantom
        }
    }
	
	if(should_draw_hp)
        draw_smallHealthBar(this, view_buf);
	
	for(auto e = damage_numbers.begin(); e != damage_numbers.end();)
    {
        e->t -= 0.05f;
        if(e->t < 0)
        {
            e = damage_numbers.erase(e);
            continue;
        }
        
        e->y -= 1.5f;
        if(view_buf->control == this)
            e->draw(view_buf);
        e++;
    }
	
	if(debug_draw_paths)
        draw_path(view_buf);
	return 1;
}

void walker::compute_outline(const walker* viewer_control)
{
	if (stats_->query_bit_flags( BIT_NAMED ) || invisibility_left || flight_left || invulnerable_left)
	{
		if (outline == OUTLINE_INVULNERABLE)
		{
			if      (flight_left)
				outline = OUTLINE_FLYING;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
		}
		else if (outline == OUTLINE_FLYING)
		{
			if (viewer_control)
				if      (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;

			if (outline != OUTLINE_NAMED)
			{
				if (invisibility_left)
					outline = OUTLINE_INVISIBLE;
				else if (invulnerable_left)
					outline = OUTLINE_INVULNERABLE;
			}
		}
		else if (outline == OUTLINE_NAMED)
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
		}
		else if (outline == OUTLINE_INVISIBLE)
		{
			if      (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;
		}
		else
		{
			if      (invisibility_left)
				outline = OUTLINE_INVISIBLE;
			else if (flight_left)
				outline = OUTLINE_FLYING;
			else if (invulnerable_left)
				outline = OUTLINE_INVULNERABLE;
			else if (viewer_control)
				if (stats_->query_bit_flags (BIT_NAMED) && (team_num!=viewer_control->team_num))
					outline = OUTLINE_NAMED;
		}
	}
	else
	{
	    outline = 0;
	}

    if(outline == 0 && user != -1 && viewer_control && this != viewer_control && this->team_num == viewer_control->team_num)
        outline = OUTLINE_INVISIBLE;
}

bool walker::draw_tile(viewscreen  *view_buf)
{
	Sint32 xscreen, yscreen;

	//no need for on screen check, it will be checked at the draw level
	//and the draw level code is cleaner anyway
	//if (!this) return 0;
	if (dead)
	{
		Log("drawing a dead guy!\n");
		return 0;
	}
	//if (!bmp) {Log("No bitmap!\n"); return 0;}
	drawcycle++;

	xscreen = static_cast<Sint32>(xpos - view_buf->topx + view_buf->xloc);
	yscreen = static_cast<Sint32>(ypos - view_buf->topy + view_buf->yloc);

	compute_outline(view_buf->control);

	auto bmp_span = std::span<const unsigned char>{bmp, static_cast<size_t>(sizex * sizey)};

	if (stats_->query_bit_flags(BIT_PHANTOM)) //WE ARE A PHANTOM
		myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, query_team_color(),
		                        PHANTOM_MODE, //mode
		                        0, //invisibility
		                        0, //outline
		                        SHIFT_RANDOM); //type of phantom

	else if (invisibility_left)  //WE ARE INVISIBLE
	{
		if (this->team_num == view_buf->control->team_num)
			myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
			                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
			                        bmp_span, query_team_color(),
			                        INVISIBLE_MODE,  //mode
			                        ( invisibility_left + 10 ), //invisibility
			                        outline,  //outline
			                        0 ); //type of phantom
	}
	else if (stats_->query_bit_flags(BIT_FORESTWALK) &&
	         myscreen->level_data.mysmoother.query_genre_x_y(xpos/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	         && !stats_->query_bit_flags(BIT_FLYING)
	         && (flight_left < 1) )
		myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, query_team_color(),
		                        INVISIBLE_MODE,  //mode
		                        1000, //invisibility
		                        1,  //outline
		                        0 ); //type of phantom

	else if (outline)    // WE HAVE SOME OUTLINE
	{
		myscreen->walkputbuffer( xscreen, yscreen, sizex, sizey,
		                        view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                        bmp_span, query_team_color(),
		                        OUTLINE_MODE, //mode
		                        0, //invisibility
		                        outline, //outline
		                        0 ); //type of phantom

        draw_smallHealthBar(this, view_buf);
	}
	else
	{
		myscreen->walkputbuffer(xscreen, yscreen, sizex, sizey,
		                       view_buf->xloc, view_buf->yloc,
		                       xscreen+GRID_SIZE, yscreen+GRID_SIZE,
		                       bmp_span, query_team_color());

        draw_smallHealthBar(this, view_buf);
	}

	return 1;
}



bool walker::act()
{
	short temp;

	// Make sure everyone we're pointing to is valid
	if (foe && foe->dead)
		foe = nullptr;
	if (leader && leader->dead)
		leader = nullptr;
	if (owner && owner->dead)
		owner = nullptr;

	collide_ob = nullptr; // always start with no collison..

	// Complete previous animations (like firing)
	if (ani_type != ANI_WALK)
		return animate();

	// Are we frozen?
	if (stats_->frozen_delay)
	{
		stats_->frozen_delay--;
		return 1;
	}

	if (busy > 0)
		busy--; // This allows busy to be our FIRING delay.
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
	
	if(attack_lunge > 0.0f)
    {
        attack_lunge -= 0.4f;
        if(attack_lunge < 0.0f)
            attack_lunge = 0.0f;
    }
	
	if(hit_recoil > 0.0f)
    {
        hit_recoil -= 0.6f;
        if(hit_recoil < 0.0f)
            hit_recoil = 0.0f;
    }
	
	switch (act_type)
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
				this->dead = 1;
				return 1;
			}
			// We are randomly walking toward enemy
		case ACT_RANDOM:
			{
				if (!rng(4) )
				{
					if (!rng(20))   // a 1 in 4 then 1 in 20 chance of rand walk
					{
						if (!special())
							stats_->try_command(COMMAND_WALK, rng(30),
							                   rng(3)-1, rng(3)-1);
						return 1;
					}
					act_random(); //1 in 4 followed by 19 in 20 of doing this
				}
				else    //3 of 4 times
				{
					if (!foe)
					{
						foe = myscreen->find_far_foe(this);
					}
					if (foe)
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
	old_act_type = act_type;
	act_type = static_cast<char>(num);
	return num;
}

short walker::restore_act_type()
{
	act_type = old_act_type;
	return old_act_type;
}

short walker::query_act_type() const
{
	return act_type;
}

short walker::set_old_act_type(short num)
{
	old_act_type = static_cast<char>(num);
	return num;
}

short walker::query_old_act_type() const
{
	return old_act_type;
}

bool walker::collide(walker  *ob)
{
	collide_ob = ob;
	return 1;
}


// Thin adapter: delegates to combat_math pure functions
short exp_from_action(ExpAction action, walker* w, walker* target, short value)
{
    return compute_xp_from_action(action, w->stats()->level, target->stats()->level,
                                  value, *ctx().rng);
}



float get_base_damage(walker* w)
{
    return compute_base_damage(w->damage, *ctx().rng);
}

float get_damage_reduction(walker* w, float damage, walker* target)
{
    (void)w; // kept for minimal churn at call sites
    return compute_damage_reduction(damage, target->stats()->armor);
}


void walker::do_heal_effects(walker* healer, walker* target, short amount)
{
    if(!cfg.is_on("effects", "heal_numbers"))
        return;
    
    if(healer)
        healer->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, amount, 56));
	target->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, amount, 56));
}

void walker::do_hit_effects(walker* attacker, walker* target, short tempdamage)
{
    if(cfg.is_on("effects", "damage_numbers"))
    {
        // Orange numbers for the attacker to see
        if(attacker)
            attacker->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, tempdamage, 235));
        // RED numbers for the target to see
        target->damage_numbers.push_back(DamageNumber(target->xpos + target->sizex/2, target->ypos, tempdamage, RED));
    }
	if (target->stats()->hitpoints < 0)
		tempdamage += target->stats()->hitpoints;
    
    if(cfg.is_on("effects", "hit_anim"))
    {
        // Create hit effect
        if(query_order() != Order::FX || query_family() == FAMILY_KNIFE_BACK)
        {
           walker* newob = myscreen->level_data.add_ob(Order::FX, FAMILY_HIT);
            if (newob)
            {
                newob->owner = target;
                newob->team_num = team_num;
                newob->stats()->level = 1;
                newob->damage = 0;
                newob->ani_type = 1 + rand()%3;
                if(attacker == this)
                {
                    newob->center_on(target);
                }
                else
                {
                    // A projectile
                    newob->center_on(this);  // Make the hit effect start at the projectile position
                    // Then move it a little closer to its target (average)
                    newob->setworldxy((target->worldx_ + target->sizex/2 + newob->worldx_)/2, (target->worldy_ + target->sizey/2 + newob->worldy_)/2);
                }
            }
        }
    }
    
    if(tempdamage > 0)
    {
        if(cfg.is_on("effects", "hit_flash"))
            target->hurt_flash = true;
        
        if(cfg.is_on("effects", "hit_recoil"))
        {
            if(target->query_order() == Order::Living)
            {
                target->hit_recoil = 1.0f;
                target->hit_recoil_angle = atan2(target->ypos + target->sizey/2 - ypos - sizey/2, target->xpos + target->sizex/2 - xpos - sizex/2);
            }
        }
    }
}

void walker::do_combat_damage(walker* attacker, walker* target, short tempdamage)
{
	// Record damage done for records ..
	if (attacker && attacker->myguy && target->query_order() == Order::Living)  // hit a living
    {
		attacker->myguy->total_damage += tempdamage;
		attacker->myguy->scen_damage += tempdamage;
    }
    
    // Deal the damage
    target->last_hitpoints = target->stats()->hitpoints;
	target->stats()->hitpoints -= tempdamage;
	
	do_hit_effects(attacker, target, tempdamage);
	
	if (target->stats()->hitpoints < 0)
		tempdamage += target->stats()->hitpoints;
    
    // Delay HP regeneration
    if(tempdamage > 0)
        target->regen_delay_ = 50;
    
    if(target->myguy != nullptr)
    {
        target->myguy->scen_damage_taken += tempdamage;
        if(target->myguy->scen_min_hp > target->stats()->hitpoints)
            target->myguy->scen_min_hp = target->stats()->hitpoints;
    }
}

bool walker::attack(walker  *target)
{
	walker  *blood; // temporary stain
	walker *headguy; // guy at top of chain..
	short playerteam = -1;
	std::string message;
	float tempdamage = get_base_damage(this);
	short getscore=0;
	Order targetorder = target->query_order();
	char targetfamily= target->query_family();
	walker *attacker; // us or our owner ..
	static short tom = 0;

	if (myguy != nullptr || team_num == 0)
		getscore = 1;

	if (target && target->dead)
		return 0;

	//if ( (targetorder == Order::Living && is_friendly(target) ) ||
	if ( is_friendly(target) || (targetorder == Order::Treasure) )
		return 0;

	if (target->stats()->query_bit_flags(BIT_INVINCIBLE) ||
	        target->invulnerable_left != 0 )
		return 0;

	if (order != Order::Living && owner)
		attacker = owner;
	else
		attacker = this;

	// who's the top on our chain (ie, weapon->summoned->mage)
	headguy = this;
	while (headguy->owner && (headguy->owner != headguy) )
		headguy = headguy->owner;

	if (headguy->myguy && headguy->user == 0 && order == Order::Weapon)
		tom++;

	// Modify attack value based on things like magical attacks, etc.
	switch (targetorder) // generally going to be livings..
	{
		case Order::Living:
			// Hit a living target, so we get credit for a hit
			if (attacker->myguy)
            {
				attacker->myguy->total_hits++;
				attacker->myguy->scen_hits++;
            }

			switch (targetfamily)
			{
				case FAMILY_SLIME:        // Slimes are hurt MORE by
				case FAMILY_SMALL_SLIME:  // magical or fire weapons
				case FAMILY_MEDIUM_SLIME:
					if (stats_->query_bit_flags(BIT_MAGICAL))
						tempdamage *= 2; // twice as susceptible to magic..
					break;  // end of slimes
				case FAMILY_BARBARIAN:              // Barbarians get LESS damaged
					if (stats_->query_bit_flags(BIT_MAGICAL)) // by magical attacks
						tempdamage /= 2;
					break;
				default:
					break; // do nothing in default living case
			} // end of living target case
			break; // end of living
		default:
			// We hit something, but it wasn't living, so don't count
			// as a shot, OR as a hit ..
			if (attacker->myguy)
            {
				attacker->myguy->total_shots--; // since we already counted it
				attacker->myguy->scen_shots--;
            }
			break;
	} // end of checking orders

	tempdamage -= get_damage_reduction(attacker, tempdamage, target);
	if (tempdamage < 0)
		tempdamage = 0;
    
    do_combat_damage(attacker, target, tempdamage);
    TRACE("walker", "attack: %s deals %.0f damage", attacker->stats_->name.c_str(), tempdamage);

    // Base exp from attack damage
	short newexp = exp_from_action(ExpAction::Attack, this, target, tempdamage);

	// Set our target to fighting our owner
	//in the case of our weapon hit something
	if (order != Order::Living && owner)
	{
		owner->foe = target;
		target->stats()->hit_response(owner);
		if (headguy->myguy)
		{
            headguy->myguy->exp += newexp;
		}
	}
	else  //melee combat, set target to hit_response to us
	{
		target->stats()->hit_response(this);
		if (myguy)
		{
            myguy->exp += newexp;
            if (getscore)
            {
                myscreen->save_data.m_score[team_num] += tempdamage + target->stats()->level;
            }
		}
	}

	if (order == Order::Weapon)
	{
		stats_->hitpoints -= tempdamage;
		damage--;
		if (stats_->hitpoints <= 0)
		{
			if (!stats_->query_bit_flags(BIT_IMMORTAL))
				dead = 1;
			death();
		}
		//special effects
		switch (query_family())
		{
			case FAMILY_SPRINKLE:   // Faerie's fire freezes foes :)
				if (targetorder == Order::Living)
				{
					Sint32 con = target->myguy ? target->myguy->constitution : 0;
					target->stats()->frozen_delay =
					    compute_freeze_duration(owner->stats()->level, con, *ctx().rng);
				}
				break;
			default :
				break;
		}

	}

	playerteam = 0;

	// Positive score for hurting enemies, negative for us
	if (owner &&
	        (targetorder != Order::Weapon) ) // are we still alive?
	{
		if (playerteam != target->team_num)
		{
			if (getscore)
			{
				myscreen->save_data.m_score[team_num] += tempdamage + target->stats()->level; // / 2;
			}
			if (headguy->myguy)
				headguy->myguy->exp += newexp;
		}
	}

	if (target->stats()->hitpoints <= 0)
	{
		if (targetorder == Order::Living)
		{
			if (playerteam > -1)
			{
				if (playerteam != target->team_num)
				{
					if (headguy->myguy)  // headguy can == this
					{
						headguy->myguy->exp += newexp + exp_from_action(ExpAction::Kill, this, target, 0);
						headguy->myguy->kills++;
						headguy->myguy->scen_kills++;
						headguy->myguy->level_kills += target->stats()->level;
					}
					//else if (myguy)
					//{
					//  myguy->exp += newexp + (8 * target->stats()->level);
					//  myguy->kills++;
					//  myguy->level_kills += target->stats()->level;
					//}
					if (getscore)
					{
						myscreen->save_data.m_score[team_num] += tempdamage + (10 * target->stats()->level);
					}
					// If named, alert us of the enemy's death
					if (target->stats()->name.size() && !(target->lifetime)
					        && (!target->owner) ) // do we have an NPC name?
					{
						message = std::format("ENEMY DEATH: {} DIED!", target->stats()->name);
						myscreen->viewob[0]->set_display_text(message.c_str(), STANDARD_TEXT_TIME);
					}
					if(remaining_foes(myscreen, this) == 1)  // This is the last foe
					{
						message = "All foes defeated!";
						myscreen->viewob[0]->set_display_text(message.c_str(), STANDARD_TEXT_TIME);
					}
				}
				else
				{
					// Alert us of the death
					if ( (target->owner || target->lifetime) // summoned?
					        && (target->stats()->name.size() ) ) // and have name
						message = std::format("{} Dispelled!", target->stats()->name);
					else if (target->stats()->name.size()) // do we have an NPC name?
						message = std::format("{} DIED!", target->stats()->name);
					else if (target->myguy && target->myguy->name.size() )
						message = std::format("{} Died!", target->myguy->name);
					else
						switch (target->query_family())
						{
							case FAMILY_SOLDIER:
								message = "SOLDIER SLAIN";
								break;
							case FAMILY_ARCHER:
								message = "ARCHER DIED";
								break;
							case FAMILY_THIEF:
								message = "THIEF KILLED";
								break;
							case FAMILY_ELF:
								message = "ELF KILLED";
								break;
							case FAMILY_MAGE:
								message = "MAGE DIED";
								break;
							case FAMILY_SKELETON:
								message = "SKELETON CRUMBLED";
								break;
							case FAMILY_CLERIC:
								message = "CLERIC DIED";
								break;
							case FAMILY_FIREELEMENTAL:
								message = "FIRE ELEMENTAL EXTINGUISHED";
								break;
							case FAMILY_FAERIE:
								message = "FAERIE POPPED";
								break;
							case FAMILY_SMALL_SLIME:
							case FAMILY_MEDIUM_SLIME:
							case FAMILY_SLIME:
								message = "SLIME DESTROYED";
								break;
							case FAMILY_GHOST:
								message = "GHOST VANISHED";
								break;
							case FAMILY_DRUID:
								message = "DRUID VANQUISHED";
								break;
							case FAMILY_ORC:
								message = "ORC DIED";
								break;
							default :
								message = "SOMEONE DIED";
								break;
						}
					myscreen->viewob[0]->set_display_text(message.c_str(), STANDARD_TEXT_TIME);
				}
			}

			/* Blood splats at death */
			// Make temporary stain:
			blood = myscreen->level_data.add_ob(Order::Weapon, FAMILY_BLOOD);
			blood->team_num = target->team_num;
			blood->ani_type = ANI_GROW;
			blood->ignore = 1; // so that we can be walked over .. ?
			blood->setxy(target->xpos,target->ypos);
		}
		if (on_screen() && targetorder == Order::Living)
		{
			if (rng(2))
				myscreen->soundp->play_sound(SOUND_DIE1);
			else
				myscreen->soundp->play_sound(SOUND_DIE2);
		}

		target->dead = 1;
		target->death(); // any special effect upon death ..
	}
	collide_ob = nullptr;

	return 1;
}

bool walker::animate()
{
	walker  * newob;

	set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
	cycle++;
	if (ani[curdir+ani_type*NUM_FACINGS][cycle] == -1)
	{
		//          if (ani_type == ANI_ATTACK &&
		//                        query_order() == Order::Living)
		if (ani_type == ANI_ATTACK)
		{
			fire();
			ani_type = ANI_WALK;
			cycle = 0;
			return 1;
		}
		// finished teleport out sequence
		//          if (ani_type == ANI_SKEL_GROW && family == FAMILY_SKELETON)
		if (ani_type == ANI_SKEL_GROW && query_type(Order::Living, FAMILY_SKELETON))
		{
			ani_type = ANI_WALK;
			cycle = 0;
			return 1;
		}
		if (ani_type == ANI_TELE_OUT && order == Order::Living)
		{
			if (family == FAMILY_MAGE || family==FAMILY_ARCHMAGE)
			{
				ani_type = ANI_TELE_IN;
				cycle = 0;
				teleport();
				return 1;
			}
			else if (family == FAMILY_SKELETON)
			{
				ani_type = ANI_TELE_IN;
				cycle = 0;
				teleport_ranged(stats_->level*18);
				return 1;
			}
			else
			{
				ani_type = ANI_WALK;
				cycle = 0;
				return 0;
			}
		}
		// Were we a slime who just split?
		if (ani_type == ANI_SLIME_SPLIT && order == Order::Living)
		{
			ani_type = ANI_WALK;
			cycle = 0;
			// First, shrink (and move) normal guy ..
			transform_to(Order::Living, FAMILY_SMALL_SLIME);
			setxy(xpos-10, ypos+10); // diagonal 'down left' of normal

			// Create a new small slime ..
			newob = myscreen->level_data.add_ob(Order::Living, FAMILY_SMALL_SLIME);
			newob->setxy(xpos+12, ypos-12);
			// Transfer stats/etc. across to new guy ..
			//stats_->magicpoints -= stats_->special_cost[0];
			transfer_stats(newob);
			if (newob->myguy && newob->myguy->exp < (1000*stats_->level) )
			{
				delete newob->myguy;  // can't be 'sustained' if too low
				newob->myguy = nullptr;
				newob->owns_myguy = false;
				newob->stats()->name = "SLIME"; // generic name
				newob->stats()->level = calculate_level(myguy->exp/2);
			}
			else if (newob->myguy) // split our experience
			{
				Uint32 exp = myguy->exp / 2;
				
				short newlevel = calculate_level(exp);
				// Downgrade us and the copy
				myguy->upgrade_to_level(newlevel);
				myguy->update_derived_stats(this);
				myguy->exp = exp;
				
				newob->myguy->upgrade_to_level(newlevel);
				newob->myguy->update_derived_stats(newob);
				newob->myguy->exp = exp;
			}

			newob->team_num = team_num;
			newob->foe = foe;
			newob->leader = leader;
			return 1;
		}

		ani_type = ANI_WALK;
		cycle = 0;
	}
	return 1;
}

bool walker::set_order_family(Order neworder, char newfamily)
{
	order = neworder;
	family = newfamily;
	return 1;
}

walker  *walker::create_weapon()
{
	walker  *weapon;
	short weapon_type;


	// Special case for generators
	if (query_order() == Order::Generator)
	{
		weapon = myscreen->level_data.add_ob(Order::Living, static_cast<char>(default_weapon));
		weapon->team_num = team_num;
		weapon->owner = this;
		weapon->set_difficulty(stats_->level);
		return weapon;
	}
	// Normally, only livings fire
	weapon_type = current_weapon;

	weapon = myscreen->level_data.add_ob(Order::Weapon, static_cast<char>(weapon_type));
	weapon->team_num = team_num;
	weapon->owner = this;
	weapon->set_difficulty(stats_->level);
	weapon->damage = (weapon->damage * (stats_->level+3))/4;
	if (myguy)
	{
		weapon->lineofsight += (myguy->strength / 23) + (myguy->dexterity / 31);
		weapon->damage += (myguy->strength / 7.0f);
	}
	else
	{
		weapon->damage *= stats_->level;
	}
	weapon->lineofsight += (stats_->level / 3);
	switch ( facing(lastx, lasty) ) // make 'circular' ranges
	{
		case FACE_UP:
		case FACE_RIGHT:
		case FACE_DOWN:
		case FACE_LEFT:
			// this will multiply by 1.207 ..
			weapon->lineofsight *= 309;
			weapon->lineofsight /= 256; // = 1.207 for circular range
			// this will multiply by 1.414
			weapon->stepsize *= 362;
			weapon->stepsize /= 256;
			break;
		default :
			break;
	}

	if (query_family() == FAMILY_CLERIC)
	{
		weapon->ani_type = ANI_GLOWGROW;
		weapon->lifetime += (stats_->level * 110);
	}
	//  if (query_family() == FAMILY_DRUID)
	//       weapon->ani_type = ANI_GROW;
	//duhhhh he's not using this as his normal weapon
	return weapon;
}

bool walker::query_next_to()
{
	short newx, newy;

	newx = xpos;
	newy = ypos;

	if (lastx > 0)
		newx += sizex;
	else if (lastx < 0)
		newx += -sizex;
	if (lasty > 0)
		newy += sizey;
	else //if (lasty < 0)
		newy += -sizey;

	if (!myscreen->query_object_passable(newx, newy, this))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

bool walker::special()
{
	walker  * newob;
	weap * fireob;
	walker  * alive, *tempwalk;
	short tempx, tempy;
	short i, j;
	short targetx, targety;
	Uint32 distance;
	short howmany;
	short didheal;
	short generic, generic2 = 0;
	std::string message, tempstr;
	short person;

	TRACE("walker", "special: family=%d current_special=%d", family, current_special);

	// Are we somehow dead already?
	if (dead)
	{
		Log("Dead guy doing special!\n");
		return 0;
	}

	// Do we have a stats object? If not, freak out and exit :)
	if (!stats_)
	{
		Log("Special with no stats\n");
		return 0;
	}

	// Do we have enough for our special ability?
	if (stats_->magicpoints < stats_->special_cost[static_cast<int>(current_special)])
		return 0;

	if (query_order() != Order::Living)
	{
		return 0;
	}
	switch (query_family())
	{
		case FAMILY_ARCHER:
			switch(current_special)
			{
				case 1: // fire arrows
					tempx = lastx;
					tempy = lasty;
					curdir = -1;
					lastx = 0;
					lasty = 0;
					stats_->magicpoints += (8*stats_->weapon_cost);
					stats_->add_command(COMMAND_SET_WEAPON, 1, FAMILY_FIRE_ARROW, 0);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, 0, -1);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, 1, -1);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, 1, 0);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, 1, 1);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, 0, 1);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, -1, 1);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, -1, 0);
					stats_->add_command(COMMAND_QUICK_FIRE, 1, -1, -1);
					//                  stats_->add_command(COMMAND_WALK, 1, tempx/stepsize, tempy/stepsize);
					stats_->add_command(COMMAND_RESET_WEAPON, 1, 0, 0);
					break;
				case 2:  // flurry of arrows
					if (busy)
						return 0;
					stats_->magicpoints += (3*stats_->weapon_cost);
					fire();
					fire();
					fire();
					busy += (fire_frequency * 2);
					break;
				case 3: // exploding arrows
				case 4:
				default:
					if (busy)
						return 0;
					generic = current_weapon;
					current_weapon = FAMILY_FIRE_ARROW;
					newob = fire();
					current_weapon = generic;
					if (!newob)
						return 0; // failsafe
					newob->skip_exit = 5000; // used as a dummy variable to
					// signify exploding .. :(
					newob->stats()->hitpoints = 500; // buffed arrows
					newob->damage *= 2;
					break;
			}
			break;  // end of archer
		case FAMILY_SOLDIER:
			switch (current_special)
			{
				case 1: // charge enemy
					if (!stats_->forward_blocked())
					{
						stats_->add_command(COMMAND_RUSH, 3, lastx/stepsize, lasty/stepsize);
						if (on_screen())
							myscreen->soundp->play_sound(SOUND_CHARGE);
					}
					else
						return 0;
					break;
				case 2: // boomerang
					newob = myscreen->level_data.add_ob(Order::FX, FAMILY_BOOMERANG);
					newob->owner = this;
					newob->team_num = team_num;
					newob->ani_type = 1; // dummy, non-zero value
					newob->lifetime = 30 + (stats_->level)*12;
					newob->stats()->hitpoints += stats_->level*12;
					newob->stats()->max_hitpoints = newob->stats()->hitpoints;
					newob->damage += stats_->level*4;
					break;
				case 3: // whirlwind attack
					if (busy)
						return 0; // can't do while attacking, etc.
					busy += 8;
					tempx = lastx;
					tempy = lasty;
					curdir = -1;
					lastx = 0;
					lasty = 0;
					//stats_->magicpoints += (8*stats_->weapon_cost);
					stats_->add_command(COMMAND_WALK, 1, 0, -1);
					stats_->add_command(COMMAND_WALK, 1, 1, -1);
					stats_->add_command(COMMAND_WALK, 1, 1, 0);
					stats_->add_command(COMMAND_WALK, 1, 1, 1);
					stats_->add_command(COMMAND_WALK, 1, 0, 1);
					stats_->add_command(COMMAND_WALK, 1, -1, 1);
					stats_->add_command(COMMAND_WALK, 1, -1, 0);
					stats_->add_command(COMMAND_WALK, 1, -1, -1);
					
					{
                        std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                              32+stats_->level*2, &howmany, this);
                        
                        for(auto* w : newlist)
                        {
                            if (w)
                            {
                                tempx = w->xpos - xpos;
                                if (tempx)
                                    tempx = tempx / (abs(tempx));
                                tempy = w->ypos - ypos;
                                if (tempy)
                                    tempy = tempy / (abs(tempy));
                                attack(w);
                                w->stats()->force_command(COMMAND_WALK, 8,
                                                               tempx, tempy);
                            }
                        }
					}
					break; // end of whirlwind attack
				case 4:  // Disarm opponent
					if (busy)
						return 0;
					if (!stats_->forward_blocked())
						return 0; // can't do this if no frontal enemy
                    
                    {
                        std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      28, &howmany, this);
                    
                        generic = 0;
                        
                        for(auto* w : newlist)
                        {
                            if (w)
                            {
                                if (rng(stats_->level) >= rng(w->stats()->level))
                                    w->busy += 6*(stats_->level -
                                                            w->stats()->level + 1);
                                generic = 1; // disarmed at least one guy
                            }
                        }
                        
                        if (generic)
                        {
                            if (on_screen())
                                myscreen->soundp->play_sound(SOUND_CHARGE);
                            if (team_num == 0 || myguy) // player's team
                                myscreen->do_notify("Fighter Disarmed Enemy!", this);
                            busy += 5;
                        }
                        else
                            return 0;
                    }
					break;
				default:
					break;
			}
			break; // end of fighter
		case FAMILY_CLERIC:
			switch (current_special)
			{
				case 1:  // heal / mystic mace
					if (!shifter_down) // then do normal heal
					{
						std::list<walker*> newlist = myscreen->find_friends_in_range(myscreen->level_data.oblist,
						          60, &howmany, this);
                        
						didheal = 0;
						if (howmany > 1) // some friends here ..
						{
						    for(auto* w : newlist)
							{
								newob = w;
								if (newob->stats()->hitpoints < newob->stats()->max_hitpoints &&
								        newob != this )
								{
								    // Get the cost first
									HealResult heal = compute_heal_amount(stats_->magicpoints, stats_->level, *ctx().rng);
									generic = heal.amount;
									int cost = heal.cost;
									if(stats_->magicpoints < cost)
                                    {
                                        generic -= stats_->magicpoints;
                                        cost -= stats_->magicpoints;
                                    }
                                    if(generic <= 0 || cost <= 0)  // Didn't heal any for this guy
                                        break;
                                    
                                    // Do the heal
									newob->stats()->hitpoints += generic;
									stats_->magicpoints -= cost;
									if (myguy)
										myguy->exp += exp_from_action(ExpAction::Heal, this, newob, generic);
									didheal++;
									
                                    do_heal_effects(this, newob, generic);
								}
							}
							if (!didheal)
								return 0; // everyone was healthy; don't charge us
							else
							{
                                if(!cfg.is_on("effects", "heal_numbers"))
                                {
                                    // Inform screen/view to print a message ..
                                    if (didheal == 1)
                                        message = "Cleric healed 1 man!";
                                    else
                                        message = std::format("Cleric healed {} men!", didheal);
                                    if (team_num == 0 || myguy) // home team
                                        myscreen->do_notify(message.c_str(), this);
                                }
                                
								// Play sound ...
								if (on_screen())
									myscreen->soundp->play_sound(SOUND_HEAL);
							}  // end of did heal guys case
						}
						else // no friends, so don't charge us
							return 0;
						break;
					}  // end of normal heal
					else  // else do mystic mace
					{
						// First do legality checks:

						// Can't do more than 1/5 rounds
						if (busy > 0)
							return 0;

						// Do we have the int?
						if (myguy && myguy->intelligence < 50) // need 50+
						{
							if (user != -1) // only players get this
								myscreen->do_notify("50 Int required for Mystic Mace!", this);
							return 0;
						}
						if (myguy)
                        {
							myguy->total_shots++; // record that we fired/attacked
							myguy->scen_shots++;
                        }

						// All okay, let's summon!
						newob = myscreen->level_data.add_ob(Order::FX, FAMILY_MAGIC_SHIELD);
						if (!newob) // safety check
							return 0;
						newob->owner = this;
						newob->team_num = team_num;
						newob->ani_type = 1; // dummy, non-zero value
						// Specify settings based on our mana ..
						generic = stats_->magicpoints - stats_->special_cost[static_cast<int>(current_special)];
						generic /= 2; // get half our excess magic

						newob->lifetime = 100 + generic;
						newob->stats()->hitpoints += generic / 2;
						newob->damage += generic / 4.0f;

						// Remove those excess magic points :>
						stats_->magicpoints -= generic;

						busy += 5;
						break;
					}  // end of mystic mace
				case 2:  // raise skeletons
					if (shifter_down) // turn undead, low level
					{
						if (busy > 0)
							return 0;
						if (myguy && myguy->intelligence < 60) // check for minimum req.
						{
							if ( (team_num == 0 || myguy) && on_screen() )
								myscreen->do_notify("You need 60 Int to Turn Undead", this);
							busy +=5;
							return 0;
						}
						if ( (generic=turn_undead(4*stats_->level, stats_->level)) == -1 )
							return 0; // failed to turn undead
						if (myguy && generic)
						{
							myguy->exp += exp_from_action(ExpAction::TurnUndead, this, nullptr, generic); // (stats_->level/2));
							if (team_num == 0 || myguy)
							{
								message = std::format("{} turned {} undead.",
								        myguy->name, generic);
								myscreen->do_notify(message.c_str(), this);
							} // end of notify visually
						}
						// Play sound ...
						if (on_screen())
							myscreen->soundp->play_sound(SOUND_HEAL);
					} // end of turn undead, low level
					else
					{
						newob = myscreen->find_nearest_blood(this);
						if (newob)
						{
							targetx = newob->xpos;
							targety = newob->ypos;
							distance = static_cast<Uint32>(distance_to_ob(newob)); //(targetx-xpos)*(targetx-xpos) + (targety-ypos)*(targety-ypos);
							if (myscreen->query_passable(targetx, targety, newob) && distance < 60)
							{
								alive = do_summon(FAMILY_SKELETON, 125 + (stats_->level*40) );
								if (!alive)
									return 0;
								alive->team_num = team_num;
								alive->stats()->level = rng(stats_->level) + 1;
								alive->set_difficulty(static_cast<Uint32>(alive->stats()->level));
								alive->setxy(newob->xpos, newob->ypos);
								alive->owner = this;
								//myscreen->remove_fx_ob(newob);
								//myscreen->remove_ob(newob, 0);
								newob->dead = 1;
								if (myguy)
									myguy->exp += exp_from_action(ExpAction::RaiseSkeleton, this, alive, 0);
							} // end passable check
							else
								return 0;
						} // end if-newob check
						else
							return 0; //end of raise skeletons
					} // end of the else-check
					break;
				case 3: // Raise ghosts ..
					if (shifter_down) // turn undead, high level
					{
						if (busy > 0)
							return 0;
						if (myguy && myguy->intelligence < 60) // check for minimum req.
						{
							if ((team_num == 0 || myguy) && on_screen() )
								myscreen->do_notify("You need 60 Int to Turn Undead", this);
							busy +=5;
							return 0;
						}
						if ( (generic=turn_undead(4*stats_->level, stats_->level)) == -1 )
							return 0; // failed to turn undead
						if (myguy && generic)
						{
							myguy->exp += exp_from_action(ExpAction::TurnUndead, this, nullptr, generic); // (stats_->level/2));
							if (team_num == 0 || myguy)
							{
								message = std::format("{} turned {} undead.",
								        myguy->name, generic);
								myscreen->do_notify(message.c_str(), this);
							} // end of notify visually
						}
						// Play sound ...
						if (on_screen())
							myscreen->soundp->play_sound(SOUND_HEAL);
					} // end of turn undead, high level
					else
					{
						newob = myscreen->find_nearest_blood(this);
						if (newob)
						{
							targetx = newob->xpos;
							targety = newob->ypos;
							distance = static_cast<Uint32>(distance_to_ob(newob)); //(targetx-xpos)*(targetx-xpos) + (targety-ypos)*(targety-ypos);
							if (myscreen->query_passable(targetx, targety, newob) && distance < 30)
							{
								//alive = myscreen->level_data.add_ob(Order::Living, FAMILY_SKELETON);
								alive = do_summon(FAMILY_GHOST, 150 + (stats_->level*40) );
								if (!alive)
									return 0;
								alive->stats()->level = rng(stats_->level) + 1;
								alive->set_difficulty(static_cast<Uint32>(alive->stats()->level));
								alive->team_num = team_num;
								alive->setxy(newob->xpos, newob->ypos);
								alive->owner = this;
								//myscreen->remove_fx_ob(newob);
								//myscreen->remove_ob(newob, 0);
								newob->dead = 1;
								if (myguy)
									myguy->exp += exp_from_action(ExpAction::RaiseGhost, this, alive, 0);
							} // end of passable check
							else
								return 0;
						} // end of if-newob check
						else
							return 0; // end of raise ghosts
					} // end of else check
					break;
				case 4:  // Resurrect our guys ..
				default:
					newob = myscreen->find_nearest_blood(this);
					if (newob)
					{
						targetx = newob->xpos;
						targety = newob->ypos;
						distance = distance_to_ob(newob); //(targetx-xpos)*(targetx-xpos) + (targety-ypos)*(targety-ypos);
						if (myscreen->query_passable(targetx, targety, newob) && distance < 30)
						{
							if ( is_friendly(newob) ) // normal resurrection
							{
								alive = myscreen->level_data.add_ob(Order::Living, newob->stats()->old_family);
								if(!alive)
									return 0; // failsafe
								newob->transfer_stats(alive);  // restore our old values ..
								alive->stats()->hitpoints = (alive->stats()->max_hitpoints)/2;
								do_heal_effects(this, alive, (alive->stats()->max_hitpoints)/2);
								alive->team_num = newob->team_num;
								
								if(myguy) // take some EXP away as penalty if we're a player
								{
								    unsigned short exp_loss = exp_from_action(ExpAction::ResurrectPenalty, this, newob, 0);
									if(myguy->exp >= exp_loss)
										myguy->exp -= exp_loss;
									else
										myguy->exp = 0;
								}
							}
							else // raise an opponent as undead
							{
								alive = do_summon(FAMILY_GHOST, 200);
								if (!alive)
									return 0;
								alive->team_num = team_num;
								alive->stats()->level = rng(stats_->level) + 1;
								alive->set_difficulty(static_cast<Uint32>(alive->stats()->level));
								alive->owner = this;
							}
							alive->setxy(newob->xpos, newob->ypos);
							//myscreen->remove_fx_ob(newob);
							//myscreen->remove_ob(newob, 0);
							newob->dead = 1;
							if (myguy)
								myguy->exp += exp_from_action(ExpAction::Resurrect, this, alive, 0);
						} // end of passable
						else
							return 0;
					} // end of if newob
					else
						return 0; // end of ressurection
					break;
			}
			break; // end of cleric
		case FAMILY_MAGE:
			switch (current_special)
			{
				case 1:  // Teleport
					if (ani_type == ANI_TELE_OUT || ani_type == ANI_TELE_IN)
						return 0;
					if (shifter_down) // leave/remove a marker
					{
						if (busy > 0)
							return 0;
						if (myguy && (myguy->intelligence < 75) )
						{
							if (user != -1) // we're a real player ..
								myscreen->do_notify("Need 75 Int for Marker!", this);
							return 0; // so as not to charge player
						}
						// Remove a marker, if present
						generic = 0; // used to check progress
						for(auto& uptr : myscreen->level_data.oblist)
						{
						    walker* ob = uptr.get();
							if (ob &&
							        ob->query_order() == Order::FX &&
							        ob->query_family() == FAMILY_MARKER &&
							        ob->owner == this &&
							        !ob->dead
							   )
							{
								ob->dead = 1;
								ob->death();
								if ((team_num == 0 || myguy) && user!=-1)
									myscreen->do_notify("(Old Marker Removed)", this);
								busy += 8;
								break;
							}
						}
						generic = 0; // force new placement, for now
						if (!generic) // didn't remove a marker, so place one
						{
							newob = myscreen->level_data.add_ob(Order::FX, FAMILY_MARKER);
							if (!newob)
								return 0; // failsafe
							newob->owner = this;
							newob->center_on(this);
							if (myguy)
								newob->lifetime = myguy->intelligence / 33;
							else
								newob->lifetime = (stats_->level / 4) + 1;
							newob->ani_type = ANI_SPIN; // non-walking
							if ((team_num == 0 || myguy) && user != -1)
							{
								myscreen->do_notify("Teleport Marker Placed", this);
								message = std::format("({} Uses)", newob->lifetime);
								myscreen->do_notify(message.c_str(), this);
							}
							busy +=8;
							// Take an extra cost for placing a marker
							generic = stats_->magicpoints - stats_->special_cost[static_cast<int>(current_special)];
							generic /= 2; // reduce our 'extra' by half
							stats_->magicpoints -= generic;
						}
					} // end of put a marker
					else
					{
						if (on_screen())
							myscreen->soundp->play_sound(SOUND_TELEPORT);
						ani_type = ANI_TELE_OUT;
						cycle = 0;
					}
					break;
				case 2:
					tempx = lastx; // store our facing
					tempy = lasty;
					// Do we have extra magic points to spend?
					generic = stats_->magicpoints - stats_->special_cost[static_cast<int>(current_special)];
					if (generic > 0)
					{
						generic = generic / 15;        // take 7% of remaining magic...
						stats_->magicpoints -= generic; // and subtract this cost ...
					}
					else
						generic = 0;
					// Now face each direction and fire ..
					stats_->magicpoints += (8*stats_->weapon_cost);
					for (i=-1;i<2;i++)
						for (j=-1;j<2;j++)
						{
							if (i || j)
							{
								lastx = i;
								lasty = j;
								newob = fire();
								if (newob)
								{
									newob->damage += generic; // bonus for extra mp
									newob->lineofsight += (generic/3);
									if (newob->lastx != 0.0f)
										newob->lastx /= fabs(newob->lastx);
									if (newob->lasty != 0.0f)
										newob->lasty /= fabs(newob->lasty);
								}  // end got a valid weapon
							}  // end checked for not center
						} // end did all 8 directions

					// Restore old facing
					lastx = tempx;
					lasty = tempy;
					break;
				case 3:  // Freeze time
					if (team_num == 0 || myguy) // the player's team
					{
						myscreen->enemy_freeze += 20 + 11*stats_->level;
						set_palette(myscreen->bluepalette);
					}
					else
					{
						generic = 5 + 2*stats_->level;
						if (generic > 50)
							generic = 50;
						message = std::format("TIME IS FROZEN! ({} rounds)", generic);
						myscreen->viewob[0]->set_display_text(message.c_str(), 2);
						myscreen->viewob[0]->redraw();
						myscreen->viewob[0]->refresh();
						//myscreen->buffer_to_screen(0, 0, 320, 200);
						std::list<walker*> newlist = myscreen->find_friends_in_range(
						              myscreen->level_data.oblist, 30000, &howmany, this);
						
						for(auto* w : newlist)
						{
							if (w)
								w->bonus_rounds = w->bonus_rounds + generic;
						}
					}
					break;
				case 4:  // Energy wave
					newob = fire();
					if (!newob)
						return 0; // failed somehow? !?!
					alive = myscreen->level_data.add_ob(Order::Weapon, FAMILY_WAVE);
					alive->center_on(newob);
					alive->owner = this;
					alive->stats()->level = stats_->level;
					alive->lastx = newob->lastx;
					alive->lasty = newob->lasty;
					newob->dead = 1;
					break;
				case 5:
				default: // Burst enemies into flame ..
				{
					std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      80+2*stats_->level, &howmany, this);
					if (!howmany)
						return 0; // didn't find any enemies..
                    
					generic = stats_->magicpoints - stats_->special_cost[5];
					generic /= 2;
					generic /= howmany; // so do half magic, div enemies
					if (myguy)
                    {
						myguy->total_shots += howmany;
						myguy->scen_shots += howmany;
                    }
					busy += 5;
					
					// Create explosions on top of the target objects
					for(auto* ob : newlist)
					{
						newob = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION);
						if (!newob)
							return 0; // failsafe

						newob->owner = this;
						newob->team_num = team_num;
						newob->stats()->level = stats_->level;
						newob->damage = generic;
						newob->center_on(ob);
						if (on_screen())
							myscreen->soundp->play_sound(SOUND_EXPLODE);
						newob->ani_type = ANI_EXPLODE;
						newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
						newob->skip_exit = 100; // don't hurt caster
						stats_->magicpoints -= generic;
					}
					break; // end of burst enemies
				}
			}
			break; // end of mage
		case FAMILY_ARCHMAGE:
			switch (current_special)
			{
				case 1:  // Teleport
					if (ani_type == ANI_TELE_OUT || ani_type == ANI_TELE_IN)
						return 0;
					if (shifter_down) // leave/remove a marker
					{
						if (busy > 0)
							return 0;
						if (myguy && (myguy->intelligence < 75) )
						{
							myscreen->do_notify("Need 75 Int for Marker!", this);
							return 0; // so as not to charge player
						}
						// Remove a marker, if present
						generic = 0; // used to check progress
						for(auto& uptr : myscreen->level_data.oblist)
						{
						    walker* ob = uptr.get();
							if (ob &&
							        ob->query_order() == Order::FX &&
							        ob->query_family() == FAMILY_MARKER &&
							        ob->owner == this &&
							        !ob->dead
							   )
							{
								ob->dead = 1;
								ob->death();
								if (team_num == 0 || myguy)
									myscreen->do_notify("(Old Marker Removed)", this);
								busy += 8;
								generic = 1;
								break;
							}
						}  // end of cycling through object list
						// Now place a marker ..
						newob = myscreen->level_data.add_ob(Order::FX, FAMILY_MARKER);
						if (!newob)
							return 0; // failsafe
						newob->owner = this;
						newob->center_on(this);
						if (myguy)
							newob->lifetime = myguy->intelligence / 33;
						else
							newob->lifetime = (stats_->level / 4) + 1;
						newob->ani_type = 2; // non-walking
						if (team_num == 0 || myguy)
						{
							myscreen->do_notify("Teleport Marker Placed", this);
							message = std::format("({} Uses)", newob->lifetime);
							myscreen->do_notify(message.c_str(), this);
						}
						busy +=8;
						// Take an extra cost for placing a marker
						generic = stats_->magicpoints - stats_->special_cost[static_cast<int>(current_special)];
						generic /= 2; // reduce our 'extra' by half
						stats_->magicpoints -= generic;
					} // end of put a marker (shifter_down)
					else
					{
						if (on_screen())
							myscreen->soundp->play_sound(SOUND_TELEPORT);
						ani_type = ANI_TELE_OUT;
						cycle = 0;
					}
					break;  // end of ArchMage's teleport
				case 2: // Burst enemies into flame, or chain lightning..
					if (busy > 0)
						return 0;
					if (shifter_down)
					{
						if (myguy)
							generic = 200+myguy->intelligence/2;  // range to scan for enemies
						else
							generic = 200+stats_->level*5;
					}
					else
						generic = 80;
                    
                    {
                        std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      generic+2*stats_->level, &howmany, this);
                        if (!howmany)
                            return 0; // didn't find any enemies..
                        
                        if (!shifter_down) // normal usage
                        {
                            generic = stats_->magicpoints - stats_->special_cost[2];
                            generic /= 2;
                            generic /= howmany; // so do half magic, div enemies
                            if (myguy)
                            {
                                myguy->total_shots += howmany;
                                myguy->scen_shots += howmany;
                            }
                            busy += 5;
                            
                            // Create explosions on the target objects
                            for(auto* ob : newlist)
                            {
                                newob = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION);
                                if (!newob)
                                    return 0; // failsafe

                                newob->owner = this;
                                newob->team_num = team_num;
                                newob->stats()->level = stats_->level;
                                newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                                newob->damage = generic;
                                newob->center_on(ob);
                                if (on_screen())
                                    myscreen->soundp->play_sound(SOUND_EXPLODE);
                                newob->ani_type = ANI_EXPLODE;
                                newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                                newob->skip_exit = 100; // don't hurt caster
                                stats_->magicpoints -= generic;
                            }
                        } // end of heartburst, standard case
                        else // do chain-lightning
                        {
                            busy += 5;
                            if (myguy)
                            {
                                myguy->total_shots++; // so can get > 100% :)
                                myguy->scen_shots++;
                            }
                            newob = myscreen->level_data.add_ob(Order::FX, FAMILY_CHAIN);
                            newob->center_on(this);
                            newob->owner = this;
                            newob->stats()->level = stats_->level;
                            newob->team_num = team_num;
                            // Use half our remaining magic ..
                            generic = stats_->magicpoints - stats_->special_cost[2];
                            generic /= 2;
                            stats_->magicpoints -= generic;
                            newob->damage = generic;
                            
                            // find closest of our foes in range
                            generic = 30000;
                            for(auto* w : newlist)
                            {
                                short dist = distance_to_ob_center(w);
                                if (generic > dist)
                                {
                                    generic = dist;
                                    newob->leader = w;
                                }
                            }
                            //newob->ani_type = ANI_ATTACK;
                        } // end of chain-lightning
                    }
					break; // end of burst enemies, chain lightning
				case 3: // Summoning .. real or illusion
					if (busy > 0)
						return 0;
					if (shifter_down) // then we do true summoning ..
					{
						// Do we have the int?
						if (myguy && myguy->intelligence < 150) // need 150+
						{
							if (user != -1) // only players get this
								myscreen->do_notify("150 Int required to Summon!", this);
							return 0;
						}
						// Take an extra 50% mana-cost
						generic = stats_->magicpoints - stats_->special_cost[3];
						generic /= 2;
						stats_->magicpoints -= generic;
						// First make the guy we'd summon, at least physically
						newob = myscreen->level_data.add_ob(Order::Living, FAMILY_FIREELEMENTAL);
						if (!newob)
							return 0; // failsafe
						// We need to check for a space around the archmage...
						generic = 0; // this means we have or haven't found room
						for (i=-1; i <= 1; i++)
							for (j=-1; j <= 1; j++)
							{
								if ( (i==0 && j==0) || (generic) )
									continue;
								if (myscreen->query_passable(xpos+((newob->sizex+1)*i),
								                            ypos+((newob->sizey+1)*j), newob))
								{
									// We've found a legal spot ..
									generic = 1;
									newob->setxy(xpos+((newob->sizex+1)*i),
									             ypos+((newob->sizey+1)*j));
									newob->stats()->level = (stats_->level+1)/2;
									newob->set_difficulty(newob->stats()->level);
									newob->team_num = team_num; // set to our team
									newob->owner = this; // we're owned!
									newob->lifetime = 200 + 60*stats_->level;
								} // end of successfully put summoned creature
							} // end of I and J loops
						if (!generic) // we never found a legal spot
						{
							newob->dead = 1;
							return 0;
						}
						busy += 15; // takes lots of time :)
					}  // end of shifter_down true summoning
					else // standard, illusion-only
					{
						// Determine what type of thing to summon image of
						generic = stats_->magicpoints - stats_->special_cost[3];
						if (generic < 100) // lowest type
							person = FAMILY_ELF;
						else if (generic < 250)
						{
							switch (rng(3))
							{
								case 0:
									person = FAMILY_ELF;
									break;
								case 1:
									person = FAMILY_SOLDIER;
									break;
								case 2:
									person = FAMILY_ARCHER;
									break;
								default:
									person = FAMILY_SOLDIER;
									break;
							}
						}
						else if (generic < 500)
						{
							switch (rng(5))
							{
								case 0:
									person = FAMILY_ELF;
									break;
								case 1:
									person = FAMILY_SOLDIER;
									break;
								case 2:
									person = FAMILY_ARCHER;
									break;
								case 3:
									person = FAMILY_ORC;
									break;
								case 4:
									person = FAMILY_SKELETON;
									break;
								default:
									person = FAMILY_ARCHER;
									break;
							}
						}
						else if (generic < 1000)
						{
							switch (rng(7))
							{
								case 0:
									person = FAMILY_ELF;
									break;
								case 1:
									person = FAMILY_SOLDIER;
									break;
								case 2:
									person = FAMILY_ARCHER;
									break;
								case 3:
									person = FAMILY_ORC;
									break;
								case 4:
									person = FAMILY_SKELETON;
									break;
								case 5:
									person = FAMILY_DRUID;
									break;
								case 6:
									person = FAMILY_CLERIC;
									break;
								default:
									person = FAMILY_ARCHER;
									break;
							}
						}
						else // our maximum possible, insert before if needed
						{
							switch (rng(9))
							{
								case 0:
									person = FAMILY_ELF;
									break;
								case 1:
									person = FAMILY_SOLDIER;
									break;
								case 2:
									person = FAMILY_ARCHER;
									break;
								case 3:
									person = FAMILY_ORC;
									break;
								case 4:
									person = FAMILY_SKELETON;
									break;
								case 5:
									person = FAMILY_DRUID;
									break;
								case 6:
									person = FAMILY_CLERIC;
									break;
								case 7:
									person = FAMILY_FIREELEMENTAL;
									break;
								case 8:
									person = FAMILY_BIG_ORC;
									break;
								default:
									person = FAMILY_ARCHER;
									break;
							}
						}

						// Now make the guy we'd summon, at least physically
						newob = myscreen->level_data.add_ob(Order::Living, person);
						if (!newob)
							return 0; // failsafe
						// We need to check for a space around the archmage...
						generic = 0; // this means we have or haven't found room
						for (i=-1; i <= 1; i++)
							for (j=-1; j <= 1; j++)
							{
								if ( (i==0 && j==0) || (generic) )
									continue;
								if (myscreen->query_passable(xpos+((newob->sizex+1)*i),
								                            ypos+((newob->sizey+1)*j), newob))
								{
									// We've found a legal spot ..
									generic = 1;
									newob->setxy(xpos+((newob->sizex+1)*i),
									             ypos+((newob->sizey+1)*j));
									newob->stats()->level = (stats_->level+2)/3;
									newob->set_difficulty(newob->stats()->level);
									newob->team_num = team_num; // set to our team
									newob->owner = this; // we're owned!
									newob->lifetime = 100 + 20*stats_->level;
									//newob->stats()->armor = -(newob->stats()->max_hitpoints*10);
									newob->stats()->max_hitpoints = 1;
									newob->stats()->hitpoints = 0;
									newob->stats()->armor = 0;
									newob->foe = foe; // just to help out ..
									newob->stats()->set_bit_flags(BIT_MAGICAL, 1); // we're magical
									newob->stats()->name = "Phantom";
								} // end of successfully put summoned creature-image
							} // end of I and J loops
						if (!generic) // we never found a legal spot
						{
							newob->dead = 1;
							return 0;
						}
						busy += 15; // takes lots of time :)
					}  // end of summon illusion
					break;  // end of summoning/illusion cases
				case 4: // Mind-control enemies
					if (busy > 0)
						return 0;
						
                    {
                        std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                              80+4*stats_->level, &howmany, this);
                        if (howmany < 1)
                            return 0; // noone to influence
                        
                        didheal = 0; // howmany actually done yet?
                        generic2 = stats_->magicpoints - stats_->special_cost[static_cast<int>(current_special)] + 10;
                        
                        for(auto* ob : newlist)
                        {
                            if (generic2 < 10) break;
                            if ( (ob->real_team_num == 255) && // never been charmed
                                    (ob->query_order() == Order::Living) && // alive
                                    (ob->charm_left_ <= 10) // not too charmed
                               )
                            {
                                generic2 -= 10; // count cost for additional guy
                                generic = stats_->level - ob->stats()->level;
                                if (generic < 0 || (!rng(20)) ) // trying to control a higher-level
                                {
                                    ob->real_team_num = ob->team_num;
                                    ob->team_num = rng(8);
                                    ob->charm_left_ = 25 + rng(generic*20);
                                }
                                else
                                {
                                    ob->real_team_num = ob->team_num;
                                    ob->team_num = team_num;
                                    ob->foe = nullptr; // allow choice of new foe
                                    ob->charm_left_ = 25 + rng(generic*20);
                                }
                                didheal++;
                            }
                        }
                    }
					if (!didheal) // didn't actually get anyone?
						return 0;
					// Notify screen of our action
					if (stats_->name.size()) // do we have an NPC name?
						message = stats_->name;
					else if (myguy && myguy->name.size() )
						message = myguy->name;
					else
						message = "ArchMage";
					tempstr = std::format("{} has controlled {} men", message, didheal);
					myscreen->do_notify(tempstr.c_str(), this);

					generic2 = stats_->magicpoints - stats_->special_cost[static_cast<int>(current_special)];
					if (generic2 > 0) // sap our extra based on how many guys
					{
						while ( (didheal > 0) && (generic2 >= 10) )
						{
							if (generic2 > 10) // 10 is cost of each additional guy
								generic2 -= 10;
							didheal--;
						}
					}  // end of extra-cost sapping
					busy += 10; // takes a while
					break; // end of Mind control
				default:
					break;
			}
			break; // end of ArchMage
		case FAMILY_FIREELEMENTAL:
			switch (current_special)
			{
				case 1:  // lots o' fireballs
				case 2:
				case 3:
				case 4:
				default:
					tempx = lastx; // store our facing
					tempy = lasty;
					// Now face each direction and fire ..
					stats_->magicpoints += (8*stats_->weapon_cost);
					for (i=-1;i<2;i++)
						for (j=-1;j<2;j++)
						{
							if (i || j)
							{
								lastx = i;
								lasty = j;
								fire();
							}
						}

					// Restore old facing
					lastx = tempx;
					lasty = tempy;
					break;
			}
			break; // end of fire elemental
		case FAMILY_SMALL_SLIME: // grow ..
		case FAMILY_MEDIUM_SLIME:
			if (spaces_clear() > 7) // room to grow?
			{
				if (query_family() == FAMILY_SMALL_SLIME)
					transform_to(Order::Living, FAMILY_MEDIUM_SLIME);
				else
					transform_to(Order::Living, FAMILY_SLIME);
			}
			else
			{
				stats_->set_command(COMMAND_WALK,10,rng(3)-1,rng(3)-1);
				return 0;
			}
			break;
		case FAMILY_SLIME:  // Big slime splits to two small slimes
			ani_type = ANI_SLIME_SPLIT;
			cycle = 0;
			break;
		case FAMILY_GHOST: // do nifty scare thing
			newob = myscreen->level_data.add_ob(Order::FX, FAMILY_GHOST_SCARE); //,1 == underneath
			newob->ani_type = ANI_SCARE;
			newob->setxy(xpos+sizex/2 - newob->sizex/2,
			             ypos+sizey/2 - newob->sizey/2);
			newob->owner = this;
			newob->stats()->level = stats_->level;
			newob->team_num = team_num; // so we scare OTHER teams
			// Actual scare effect done in scare's "death" in effect
			break;
		case FAMILY_THIEF:
			switch (current_special)
			{
				case 1:  // drop a bomb, unregistered
					newob = myscreen->level_data.add_ob(Order::FX, FAMILY_BOMB, 1); // 1 == underneath
					newob->ani_type = ANI_BOMB;
					if (myguy)
                    {
						myguy->total_shots++;
						myguy->scen_shots++;
                    }
					newob->damage = (stats_->level+1)*15;
					newob->setxy(xpos+sizex/2 - newob->sizex/2,
					             ypos+sizey/2 - newob->sizey/2);
					newob->owner = this;
					// Run away if we're AI
					person = 0;
					for (i=0; i < myscreen->numviews; i++)
						if (myscreen->viewob[i]->control == this)
							person = 1;
					if (!person)
					{
						tempx = rng(3)-1;
						tempy = rng(3)-1;
						if ( (tempx==0) && (tempy==0) )
							tempx = 1;
						stats_->force_command(COMMAND_WALK, 20, tempx,tempy);
					}
					break;
				case 2: // thief cloaking ability, Registered
					invisibility_left += 20 + ((rng(20))*stats_->level);
					break;
				case 3: // thief Taunt (draw enemies), Registered
					if (!shifter_down) // normal taunt
					{
						if (busy > 0)
							return 0;
							
                        {
                            std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                                  80+4*stats_->level, &howmany, this);
                            
                            for(auto* ob : newlist)
                            {
                                if (ob && (rng(stats_->level) >=
                                                 rng(ob->stats()->level)) )
                                {
                                    // Set our enemy's foe to us..
                                    ob->foe = this;
                                    ob->leader = this; // a hack, yeah
                                    if (ob->query_act_type() != ACT_CONTROL)
                                        ob->stats()->force_command(COMMAND_FOLLOW, 10+rng(stats_->level), 0, 0);
                                }
                            }
                        }
						if (myguy)
							message = std::format("{}: 'Nyah Nyah!'", myguy->name);
						else if ( stats_->name.size() )
							message = std::format("{}: 'Nyah Nyah!'", stats_->name);
						else
							message = "THIEF: 'Nyah Nyah!'";
						myscreen->do_notify(message.c_str(), this);
						busy += 2;
						break; // end of taunt
					}
					else // charm opponent
					{
						if (busy > 0)
							return 0;
                        
                        {
                            std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                                  16+4*stats_->level, &howmany, this);
                            
                            if (howmany < 1)
                                return 0; // noone to influence
                            
                            didheal = 0; // howmany actually done yet?
                            for(auto* ob : newlist)
                            {
                                if (didheal) break;
                                if ( (ob->real_team_num == 255) && // never been charmed
                                        (ob->query_order() == Order::Living) && // alive
                                        1 // (ob->charm_left_ <= 10) // not too charmed
                                   )
                                {
                                    generic = stats_->level - ob->stats()->level;
                                    if (generic < 0 || (!rng(20)) ) // trying to control a higher-level
                                    {
                                        // Enemy gets free attack ..
                                        ob->foe = this;
                                        ob->attack(this);
                                        generic2 = 1;
                                    }
                                    else
                                    {
                                        ob->real_team_num = ob->team_num;
                                        ob->team_num = team_num;
                                        if (foe == ob)
                                            ob->foe = nullptr;
                                        else
                                            ob->foe = foe;
                                        ob->charm_left_ = 75 + generic*25;
                                        generic2 = 0;
                                    }
                                    didheal++;
                                } // end of if-valid-target
                            } // end of until-got-target loop
                        }
						if (!didheal)
							return 0;
						// Notify screen of our action
						if (stats_->name.size()) // do we have an NPC name?
							message = stats_->name;
						else if (myguy && myguy->name.size() )
							message = myguy->name;
						else
							message = "Thief";
						if (generic2) // then we actually failed to charm
							tempstr = std::format("{} failed to charm!", message);
						else
							tempstr = std::format("{} charmed an opponent!", message);
						myscreen->do_notify(tempstr.c_str(), this);
						busy += 10; // takes a while
						break; // end of Charm Opponent
					}
				case 4: // throw poison cloud
				default:
					if (busy > 0)
						return 0;
					newob = myscreen->level_data.add_ob(Order::FX, FAMILY_CLOUD);
					if (!newob)
						return 0; // failsafe
					busy += 5;
					newob->ignore = 1;
					newob->lifetime = 40 + 3*stats_->level;
					newob->center_on(this);
					newob->invisibility_left = 10;
					newob->ani_type = ANI_SPIN; // non-walking
					newob->team_num = team_num;
					newob->stats()->level = stats_->level;
					newob->damage = stats_->level;
					newob->owner = this;
					break;
			}
			break;
		case FAMILY_ELF:
			switch(current_special)
			{
				case 1:  // some rocks (normal)
					stats_->magicpoints += (2*stats_->weapon_cost);
					fireob = static_cast<weap*>(fire());
                    if (!fireob) // failsafe
                        return 0;
					fireob->lastx *= 0.8f + 0.4f*(rand()%101)/100.0f;
					fireob->lasty *= 0.8f + 0.4f*(rand()%101)/100.0f;
					fireob = static_cast<weap*>(fire());
                    if (!fireob) // failsafe
                        return 0;
					fireob->lastx *= 0.8f + 0.4f*(rand()%101)/100.0f;
					fireob->lasty *= 0.8f + 0.4f*(rand()%101)/100.0f;
					break;
				case 2:  // more rocks, and bouncing
					stats_->magicpoints += (3*stats_->weapon_cost);
					for (i=0; i < 2; i++)
					{
						fireob = static_cast<weap*>(fire());
						if (!fireob) // failsafe
							return 0;
						fireob->lineofsight *= 3;  // we get 50% longer, too!
						fireob->lineofsight /= 2;
						fireob->do_bounce = 1;
                        fireob->lastx *= 0.8f + 0.4f*(rand()%101)/100.0f;
                        fireob->lasty *= 0.8f + 0.4f*(rand()%101)/100.0f;
					}
					break;
				case 3:
					stats_->magicpoints += (4*stats_->weapon_cost);
					for (i=0; i < 3; i++)
					{
						fireob = static_cast<weap*>(fire());
						if (!fireob) // failsafe
							return 0;
						fireob->lineofsight *= 2;  // get double distance
						fireob->do_bounce = 1;
                        fireob->lastx *= 0.8f + 0.4f*(rand()%101)/100.0f;
                        fireob->lasty *= 0.8f + 0.4f*(rand()%101)/100.0f;
					}
					break;
				case 4:
				default:
					stats_->magicpoints += (5*stats_->weapon_cost);
					for (i=0; i < 4; i++)
					{
						fireob = static_cast<weap*>(fire());
						if (!fireob) // failsafe
							return 0;
						fireob->lineofsight *= 5;  // we get 150% longer, too!
						fireob->lineofsight /= 2;
						fireob->do_bounce = 1;
                        fireob->lastx *= 0.8f + 0.4f*(rand()%101)/100.0f;
                        fireob->lasty *= 0.8f + 0.4f*(rand()%101)/100.0f;
					}
					break;
			}
			break;
		case FAMILY_DRUID:
			switch (current_special)
			{
				case 1: // plant tree
					if (busy > 0)
						return 0;
					stats_->magicpoints += stats_->weapon_cost;
					newob = fire();
					if (!newob)
						return 0;
					busy += (fire_frequency * 2);
					alive = myscreen->level_data.add_ob(Order::Weapon,FAMILY_TREE);
					alive->setxy(newob->xpos,newob->ypos);
					alive->team_num = team_num;
					alive->ani_type = ANI_GROW;
					alive->owner = this;
					newob->dead = 1;
					break;
				case 2:  // summon faerie
					if (busy > 0)
						return 0;
					stats_->magicpoints += stats_->weapon_cost;
					newob = fire();
					if (!newob)
						return 0;
					alive = myscreen->level_data.add_ob(Order::Living, FAMILY_FAERIE);
					alive->setxy(newob->xpos, newob->ypos);
					alive->team_num = team_num;
					alive->owner = this;
					alive->lifetime = 50 + stats_->level*(40);
					newob->dead = 1;
					if (!myscreen->query_passable(alive->xpos, alive->ypos, alive))
					{
						alive->dead = 1;
						return 0;
					}
					busy += (fire_frequency * 3);
					break;
				case 3: // reveal items
					if (busy > 0)
						return 0;
					view_all += stats_->level*10;
					busy += (fire_frequency * 4);
					break;
				case 4:  // circle of protection
				default:
					if (busy > 0)
						return 0;
                    
                    {
                        std::list<walker*> newlist = myscreen->find_friends_in_range(myscreen->level_data.oblist,
                                  60, &howmany, this);
                        didheal = 0;
                        if (howmany > 1) // some friends here ..
                        {
                            //Log("Found %d friends\n", howmany-1);
                            for(auto* w : newlist)
                            {
                                newob = w;
                                if (newob != this) // not for ourselves
                                {
                                    // First see if this person already has protection (slow)
                                    tempwalk = nullptr;
                                    for(auto& uptr : myscreen->level_data.oblist)
                                    {
                                        walker* ob = uptr.get();
                                        if (ob && ob->owner == newob
                                                && ob->query_order() == Order::Weapon
                                                && ob->query_family() == FAMILY_CIRCLE_PROTECTION
                                           ) // found a circle already on newob ...
                                           {
                                            tempwalk = ob;
                                            break;
                                           }
                                    }
                                    if (!tempwalk) // target wasn't protected yet
                                    {
                                        alive = myscreen->level_data.add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
                                        if (!alive) // failed somehow
                                            return 0;
                                        
                                        alive->owner = newob;
                                        alive->center_on(newob);
                                        alive->team_num = newob->team_num;
                                        alive->stats()->level = newob->stats()->level;
                                        didheal++;
                                    } // end of target wasn't protected
                                    else
                                    {
                                        alive = myscreen->level_data.add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
                                        if (!alive) // failed somehow
                                            return 0;
                                        
                                        tempwalk->stats()->hitpoints += alive->stats()->hitpoints;
                                        alive->dead = 1;
                                        didheal++;
                                        // TODO: Should we show healing numbers here?
                                    } // end of target WAS protected
                                    
                                    // Get experience either way
                                    if (myguy)
                                        myguy->exp += exp_from_action(ExpAction::Protection, this, newob, 0);
                                    
                                }  // end of did one guy
                            }  // end of cycling through guys
                            if (!didheal)
                                return 0; // everyone was okay; don't charge us
                            else
                            {
                                // Inform screen/view to print a message ..
                                if (didheal == 1)
                                    message = "Druid protected 1 man!";
                                else
                                    message = std::format("Druid protected {} men!", didheal);
                                if (team_num == 0 || myguy) // home team
                                    myscreen->do_notify(message.c_str(), this);
                                // Play sound ...
                                if (on_screen())
                                    myscreen->soundp->play_sound(SOUND_HEAL);
                            }  // end of did protect guys case
                        } // end of checking for friends
                        else // no friends, so don't charge us
                            return 0;
                    }
					break;
					// end of druid's specials ..
			} // end of switch on druid case
			break;
		case FAMILY_ORC: // registered monster
			switch (current_special)
			{
				case 1:  // yell and 'freeze' foes
					if (busy > 0)
						return 0;
					busy += 2;
					
					{
                        std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      160+(20*stats_->level), &howmany, this);
                        
                        for(auto* ob : newlist)
                        {
                            if (ob)
                            {
                                if (ob->myguy)
                                    tempx = ob->myguy->constitution;
                                else
                                    tempx = ob->stats()->hitpoints / 30;
                                tempy = 10 + rng(stats_->level*10) - rng(tempx*10);
                                if (tempy < 0)
                                    tempy = 0;
                                ob->stats()->frozen_delay += tempy;
                            }
                        }
                        
                        if (on_screen())
                            myscreen->soundp->play_sound(SOUND_ROAR);
					}
					break;
				case 2: // eat corpse for health
				case 3:
				case 4:
				default:
					if (stats_->hitpoints >= stats_->max_hitpoints)
						return 0; // can't eat if we're 'full'
					newob = myscreen->find_nearest_blood(this);
					if (!newob) // no blood, so do nothing
						return 0;
					distance = static_cast<Uint32>(distance_to_ob_center(newob));
					if (distance > 24) // must be close enough
						return 0;
					stats_->hitpoints += newob->stats()->level*5;
					do_heal_effects(nullptr, this, newob->stats()->level*5);
					// Print the eating notice
					if (myguy)
					{
						myguy->exp += exp_from_action(ExpAction::EatCorpse, this, newob, 0);
						message = std::format("{} ate a corpse.", myguy->name);
					}
					else if ( stats_->name.size() )
						message = std::format("{} ate a corpse.", stats_->name);
					else
						message = "Orc ate a corpse.";

                    if(!cfg.is_on("effects", "heal_numbers"))
                        myscreen->do_notify(message.c_str(), this);
					if (stats_->hitpoints > stats_->max_hitpoints)
						stats_->hitpoints = stats_->max_hitpoints;
					newob->dead = 1;
					newob->death();
					break; // end of eat corpse
			} // end of orc case
			break;
		case FAMILY_SKELETON:
			switch (current_special)
			{
				case 1:  // Tunnel
				case 2:
				case 3:
				case 4:
				default:
					if (ani_type == ANI_TELE_OUT || ani_type == ANI_TELE_IN)
						return 0;
					ani_type = ANI_TELE_OUT;
					cycle = 0;
					break;
					break; // end of tunnel case
			} // end of skeleton case
			break; // end of Skeleton
		case FAMILY_BARBARIAN:
			switch (current_special)
			{
				case 1: // Hurl Boulder
				case 2: // Exploding Boulder
				case 3:
				case 4:
					if (busy > 0)
						return 0;
					newob = fire();
					if (!newob)
						return 0; // failed somehow? !?!
					alive = myscreen->level_data.add_ob(Order::Weapon, FAMILY_BOULDER);
					alive->center_on(newob);
					alive->owner = this;
					alive->stats()->level = stats_->level;
					alive->lastx = newob->lastx;
					alive->lasty = newob->lasty;
					// Set our boulder's speed and extra damage ..
					if (myguy)
					{
						alive->stepsize = 1.0f + myguy->strength / 7;
						alive->damage += myguy->strength / 5.0f;
					}
					else
					{
						alive->stepsize = stats_->level * 2;
						alive->damage += stats_->level;
					}
					if (alive->stepsize < 1)
						alive->stepsize = 1;
					if (alive->stepsize > 15)
						alive->stepsize = 15;

					if (alive->lasty > 0)
						alive->lasty = alive->stepsize;
					else if (alive->lasty < 0)
						alive->lasty = -(alive->stepsize);

					if (alive->lastx > 0)
						alive->lastx = alive->stepsize;
					else if (alive->lastx < 0)
						alive->lastx = -(alive->stepsize);

					// If we're on 'exploding boulder,' then
					// make it explode on impact.
					if (current_special == 2)
						alive->skip_exit = 5000; // signify exploding
					else
						alive->skip_exit = 0;
					newob->dead = 1;
					busy += 1 + current_special * 5;
					break; // end of hurl boulder
			} // end of Barbarian
			break;

	} // end of family switch

	stats_->magicpoints -= stats_->special_cost[static_cast<int>(current_special)];
	return 0;
}

bool walker::teleport()
{
	short newx,newy;
	Sint32 distance;

	// First check to see if we have a marker to go to
	// NOTE: it must be a bit away from us ..
	for(auto& uptr : myscreen->level_data.oblist)
	{
	    walker* ob = uptr.get();
		if (ob &&
		        ob->query_order() == Order::FX &&
		        ob->query_family() == FAMILY_MARKER &&
		        ob->owner == this &&
		        !ob->dead
		   )
		{
			// Found our marker!
			if (myscreen->query_passable(ob->xpos, ob->ypos, this)
			        && (distance = distance_to_ob(ob) > 64) )
			{
				center_on(ob);
				ob->lifetime--;
				if (ob->lifetime < 1)
				{
					ob->dead = 1;
					ob->death();
				}
				return 1;
			} // end of successful transport
			else  // blocked somehow?
			{
				if (user != -1 && (distance > 64) ) // only tell players
					myscreen->do_notify("Marker is Blocked!", this);
			}
		}
	} // end of checking for marker (we failed)

	newx = rng(myscreen->level_data.grid.w)*GRID_SIZE;
	newy = rng(myscreen->level_data.grid.h)*GRID_SIZE;

	while(!myscreen->query_passable(newx, newy, this))
	{
		newx = rng(myscreen->level_data.grid.w)*GRID_SIZE;
		newy = rng(myscreen->level_data.grid.h)*GRID_SIZE;
	}
	setxy(newx,newy);
	return 1;
}

bool walker::teleport_ranged(Sint32 range)
{
	short newx,newy;
	short keep_going = 200; // maxtries

	newx = rng(2*range) - range + xpos;
	newy = rng(2*range) - range + ypos;

	while(!myscreen->query_passable(newx, newy, this) && keep_going)
	{
		newx = rng(2*range) - range + xpos;
		newy = rng(2*range) - range + ypos;
		keep_going--;
	}
	if (keep_going)
	{
		setxy(newx,newy);
		return 1;
	}
	return 0; // failed to find safe spot
}

// Turns undead; ie, skeleton or ghost, within range
// Returns the number of dead destroyed
Sint32 walker::turn_undead(Sint32 range, Sint32 power)
{
	Sint32 killed = 0;
	short targets;

	std::list<walker*> deadlist = myscreen->find_foes_in_range(myscreen->level_data.oblist, range,
	                                       &targets, this);
	if (!targets)
		return -1;

    for(auto* w : deadlist)
	{
		if (w
		        && ( (w->query_family() == FAMILY_SKELETON) ||
		             (w->query_family() == FAMILY_GHOST)
		           )
		   ) // end of if-check
		{
			if (rng(range*40) > rng(w->stats()->level*10) )
			{
				w->dead = 1;
				w->stats()->hitpoints = 0;
				//w->death();
				attack(w); // to generate bloodspot, etc.
				killed++;
			}
		}
	}
	
	return killed;
}

// *******************************************
//
//    MONSTER intellIGENCE ROUTINES
//
// *******************************************

// Basically, we check a direction for foes.
// If we find one, we init_fire.  If not,
// we do nothing. init_fire will take care of
// turning us if we need it.
bool walker::fire_check(short xdelta, short ydelta)
{
	walker  *weapon = nullptr;
	//  short newx=0, newy=0;
	short i, loops;
	short xdir = 0;
	short ydir = 0;
	Sint32 distance;
	short targetdir;

	// Allow generators to 'always' succeed
	if (order == Order::Generator)
		return 1;

	weapon = create_weapon();
	if (!weapon)
		return 0;
	set_weapon_heading(weapon); // set lastx, lasty based on our facing...
	weapon->collide_ob = nullptr;
	// Based on facing, we alter the weapon's proposed
	//   size so the collision check is fooled into checking
	//   a Sint32 strip equal to the lineofsight times the size
	//   of the weapon.
	if (!foe)     // nobody to fire at?
	{
		//Log("fire check, no foe.\n");
		//this does happen! but it appears harmless
		return 0;
	}

	if (stats_->query_bit_flags(BIT_NO_RANGED))
	{
		weapon->dead = 1;
		return 0;
	}

	if (stats_->weapon_cost > stats_->magicpoints)
	{
		weapon->dead = 1;
		return 0;
	}

	distance = distance_to_ob(foe);
	if (distance > static_cast<Sint32>( static_cast<Sint32>(weapon->stepsize) * static_cast<Sint32>(weapon->lineofsight)) )
	{
		weapon->dead = 1;
		return 0;
	}

	targetdir = facing(xdelta,ydelta);
	if (targetdir != curdir)
	{
		//         turn(targetdir);
		weapon->dead = 1;
		return 0;
	}

	if (xdelta != 0)
		xdir = xdelta / abs(xdelta);

	if (ydelta != 0)
		ydir = ydelta / abs(ydelta);

	/* // why are we assuming walls don't matter in these two cases?
	  if (!xdelta || !ydelta) // aligned on a major axis
	  {
	         weapon->dead = 1;
	         return 1;
	  }
	 
	  if ( abs( abs(xdelta) - abs(ydelta) ) < 3)
	  {
	         weapon->dead = 1;
	         return 1;
	  }
	  else
	  {
	         weapon->dead = 1;
	//         return 0;
	  }
	*/

	// Run weapon through where it would go if all went well ..
	for (i=0; i < weapon->lineofsight; i++)
	{
		weapon->setxy(weapon->xpos + i*weapon->lastx,
		              weapon->ypos + i*weapon->lasty);
		if ( !myscreen->query_grid_passable(weapon->xpos, weapon->ypos, weapon) )
		{
			// we hit a wall, so fail
			weapon->dead = 1;
			return 0;
		}
		if ( !myscreen->query_object_passable(weapon->xpos, weapon->ypos, weapon) )
		{
			// we hit an enemy, so good!
			weapon->dead = 1;
			return 1;
		}
	}
	// By this point, we should have won or lost .. fail if we went our
	// range and didn't hit anyone ..
	weapon->dead = 1;
	return 0;

	// Determine # of loops to look for guy
	if ( abs(xdelta) > abs(ydelta) )
		loops = abs(xdelta);
	else
		loops = abs(ydelta);

	// * 16 is to match with grid coords
	for (i=0; i <= loops; i+=8)  // half a grid square
		if ( !myscreen->query_grid_passable(xpos+i*xdir, ypos+i*ydir, weapon) )
		{
			weapon->dead = 1;
			//foe = nullptr;  // can't hit this guy
			//stats_->try_command(COMMAND_RANDOM_WALK, rng(8));
			return 0;
		}
	weapon->dead = 1;

	// We have a good chance of hitting, so ..
	return 1;

}

/****************************************************
*
*  Act routines (static)
*
****************************************************/

bool
walker::act_generate()
{
	if ( myscreen->level_data.numobs < MAXOBS &&
	        (rng(stats_->level*3) > (rng(300+(myscreen->level_data.numobs*8)) ) )
	   )
	{
		lastx = 1-rng(3);
		lasty = 1-rng(3);
		if (!lastx && !lasty)
			lastx = 1;
		init_fire(lastx, lasty);
		//    lastx = 0;
		//    lasty = 0;
		stats_->hitpoints++;
		if (stats_->hitpoints > stats_->max_hitpoints)
			stats_->hitpoints--;
	}
	return 1;
}

bool
walker::act_fire()
{
	if (!(lineofsight--)) // this is the range of the weapon
	{
		dead = 1;
		death();
	}
	else if (!walk() || stats_->query_bit_flags(BIT_NO_COLLIDE))
	{
		// Hit the collide_ob;
		if (collide_ob && !collide_ob->dead)
		{
			attack(collide_ob);
		}
		if (!stats_->query_bit_flags(BIT_IMMORTAL))
		{
			dead = 1;
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
	foe = myscreen->find_near_foe(this);
	if (foe)
	{
		curdir = static_cast<char>(facing(foe->xpos - xpos, foe->ypos-ypos));
		stats_->try_command(COMMAND_FIRE,rng(30));
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
	//if (rng(sizex/GRID_SIZE)) return 0;

	// Find our foe
	if (!rng(70) || (!foe))
		foe = myscreen->find_far_foe(this);
	if (!foe)
		return stats_->try_command(COMMAND_RANDOM_WALK,20);

	xdist = foe->xpos - xpos;
	ydist = foe->ypos - ypos;

	// If foe is in firing range, turn and fire
	if (abs(xdist) < lineofsight*GRID_SIZE &&
	        abs(ydist) < lineofsight*GRID_SIZE)
	{
		if (fire_check(xdist, ydist))
		{
			init_fire(xdist, ydist);
			stats_->set_command(COMMAND_FIRE, rng(24), xdist, ydist);
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

	if (foe)
	{
		newx = xdist;    // total horizontal distance..
		if (newx)                      // If it's not 0, then get
			newx = newx / abs(newx);       // the normal of it..

		newy = ydist;
		if (newy)
			newy = newy / abs(newy);
	}  // end of if we had a foe ..
	else
	{
		while ( !newx && !newy)
		{
			newx = (1-rng(3));   // Walk in some random direction
			newy = (1-rng(3));   // other than 0,0 :)
		}
	}

	// If blocked
	collide_ob = nullptr;

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
				if (myscreen->query_passable(xpos+(i*sizex), ypos+(j*sizey), this) )
					count++;

	return count;
}

void walker::transfer_stats(walker  *newob)
{
	guy  *newguy;
	short i;

	// First do the 'stats' stuff ..
	newob->stats()->hitpoints = stats_->hitpoints;
	newob->stats()->max_hitpoints = stats_->max_hitpoints;
	newob->stats()->heal_per_round = stats_->heal_per_round;
	newob->stats()->max_heal_delay = stats_->max_heal_delay;
	// Magic..
	newob->stats()->magicpoints = stats_->magicpoints;
	newob->stats()->max_magicpoints = stats_->max_magicpoints;
	newob->stats()->magic_per_round = stats_->magic_per_round/2;
	newob->stats()->max_magic_delay = stats_->max_magic_delay;

	newob->stats()->level = stats_->level;
	newob->stats()->frozen_delay = stats_->frozen_delay;
	for (i=0; i < 5; i++)
		newob->stats()->special_cost[i] = stats_->special_cost[i];
	newob->stats()->weapon_cost = stats_->weapon_cost;

	newob->stats()->bit_flags = stats_->bit_flags;
	newob->stats()->delete_me = stats_->delete_me;

	// Do we have a 'guy' ?
	if (myguy)
	{
		if (newob->owns_myguy && newob->myguy)
			delete newob->myguy;
		newguy = new guy();
		newguy->name = myguy->name;
		newguy->strength = myguy->strength;
		newguy->constitution = myguy->constitution;
		newguy->dexterity = myguy->dexterity;
		newguy->intelligence = myguy->intelligence;
		newguy->level = myguy->level;
		newguy->armor = myguy->armor;
		newguy->exp = myguy->exp;
		// 'Kill-stats'
		newguy->kills = myguy->kills;
		newguy->level_kills = myguy->level_kills;
		newguy->total_damage = myguy->total_damage;
		newguy->total_hits = myguy->total_hits;
		newguy->total_shots = myguy->total_shots;
		
		newguy->scen_damage = myguy->scen_damage;
		newguy->scen_kills = myguy->scen_kills;
		newguy->scen_damage_taken = myguy->scen_damage_taken;
		newguy->scen_min_hp = myguy->scen_min_hp;
		newguy->scen_shots = myguy->scen_shots;
		newguy->scen_hits = myguy->scen_hits;
		
		newob->myguy = newguy;
		newob->owns_myguy = true;
	}
}

// change picture, etc. but NOT stats (use transfer_stats for that)

void walker::transform_to(Order whatorder, char whatfamily)
{
	short xcenter, ycenter;
	short tempxpos, tempypos;
	short reset = 0;
	short tempact = query_act_type();;

	// First remove us from the collision table..
	myobmap->remove(this);

	if (order == whatorder) // same object type
	{
		reset = 1;
		tempact = query_act_type();
	}

	// Reset bit flags
	stats_->clear_bit_flags();

	// Do this before resetting graphic so illegal
	//  family values don't try to set graphics.
	//  order and family are only set if legal
	myscreen->set_walker(this, whatorder, whatfamily);

	// Reset the graphics
	const PixieData& data = myscreen->level_data.myloader->graphics[PIX(order, family)];
	facings = data.data.get();
	bmp = data.data.get();
	frames = data.frames;
	frame = 0;
	cycle = 0;

	// Deal with resizing and centering ..
	xcenter = xpos + sizex/2;
	ycenter = ypos + sizey/2;

	sizex = data.w;
	sizey = data.h;
	size = sizex*sizey;

	tempxpos = xcenter - sizex/2;
	tempypos = ycenter - sizey/2;


	if (reset)
		set_act_type(tempact);

	setxy(tempxpos, tempypos);  // automatically re-adds us to the list ..
	// set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
	// Don't manually set the frame here -- it can break circles
	// of protection, etc., which are special cases .. instead:
	set_frame(0);
	animate();
}


// death is called when an object dies (or weapon destructed, etc.)
// for special effects ..
bool walker::death()
{
	// Note that the 'dead' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)
	walker  *newob = nullptr;
	Sint32 i;

	if (death_called)
		return 0;

	death_called = 1;

	if (myguy) // were we a real character?  Then make a heart ..
	{
		newob = myscreen->level_data.add_ob(Order::Treasure, FAMILY_LIFE_GEM, 1);
		newob->stats()->hitpoints = myguy->query_heart_value();
		newob->stats()->hitpoints *= 0.75 / 2;  // 75%, divided by 2, since score is doubled at end of level
		newob->team_num = team_num;
		newob->center_on(this);
	}

	switch (order)
	{
		case Order::Living:
			if (   (team_num == 0 || myguy) // our team
			        && (myscreen->level_data.type & SCEN_TYPE_SAVE_ALL)
			        && (stats_->name.size()) // we were named
			   )
				return myscreen->endgame(SCEN_TYPE_SAVE_ALL); // failed
			switch (family)
			{
				case FAMILY_FIREELEMENTAL:  // make us explode
					dead = 0;
					stats_->magicpoints += stats_->special_cost[1];
					special();
					dead = 1;
					break;
				case FAMILY_SLIME: // shrink to medium ..
					dead = 1;
					//transform_to(Order::Living, FAMILY_MEDIUM_SLIME);
					newob = myscreen->level_data.add_ob(Order::Living, FAMILY_MEDIUM_SLIME);
					newob->team_num = team_num;
					newob->stats()->level = stats_->level;
					newob->set_difficulty(stats_->level);
					newob->foe = foe;
					newob->leader = leader;
					if (stats_->name.size())
						stats_->name = newob->stats()->name;
					if (myguy)
					{
						newob->myguy = myguy;
						newob->owns_myguy = owns_myguy;
						myguy = nullptr;
						owns_myguy = false;
					}
					newob->center_on(this);
					stats_->hitpoints = stats_->max_hitpoints;
					break;
				case FAMILY_MEDIUM_SLIME: // shrink to small ..
					dead = 1;
					//transform_to(Order::Living, FAMILY_SMALL_SLIME);
					newob = myscreen->level_data.add_ob(Order::Living, FAMILY_SMALL_SLIME);
					newob->team_num = team_num;
					newob->stats()->level = stats_->level;
					newob->set_difficulty(stats_->level);
					newob->foe = foe;
					newob->leader = leader;
					if (stats_->name.size())
						stats_->name = newob->stats()->name;
					if (myguy)
					{
						newob->myguy = myguy;
						newob->owns_myguy = owns_myguy;
						myguy = nullptr;
						owns_myguy = false;
					}
					newob->center_on(this);
					stats_->hitpoints = stats_->max_hitpoints;
					break;
				case FAMILY_GHOST:     // Undead don't leave bloodspots ..
				case FAMILY_SKELETON:
				case FAMILY_TOWER1:    // neither do towers
					break;
				default:
					generate_bloodspot();
					break;
			}  // end of family switch
			break;  // end of order livings case
		case Order::Generator:  // go up in flames :>
			for (i=0; i < 4; i++)
			{
				newob = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION, 1);
				if (!newob) // failsafe
					break;
				newob->team_num = team_num;
				newob->stats()->level = stats_->level;
				newob->ani_type = ANI_EXPLODE;
				newob->setxy(xpos+rng(sizex-8)+4, ypos+4+rng(sizey-8) );
				newob->damage = stats_->level*2;
				newob->set_frame(rng(3));
				if (on_screen())
					myscreen->soundp->play_sound(SOUND_EXPLODE);
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

	dead = 1; // just in case ..

	bloodstain = myscreen->level_data.add_fx_ob(Order::Treasure, FAMILY_STAIN);
	bloodstain->ignore = 1;
	transfer_stats(bloodstain);

	bloodstain->order  = Order::Treasure;
	bloodstain->family = FAMILY_STAIN;
	bloodstain->stats()->old_order = order;
	bloodstain->stats()->old_family= family;

	bloodstain->team_num = team_num;
	bloodstain->dead = 0;
	bloodstain->setxy(xpos, ypos);
	//data = myscreen->myloader->graphics[PIX(Order::Treasure, FAMILY_STAIN)];
	// We can't select other 'bloodspot' frames, because set_frame
	// appears to check the order and family and reset our picture
	// to a living guy .. we need to find a way around this ..
	bloodstain->set_frame(rng(4));  // has no effect yet ..
	bloodstain->ani_type = ANI_WALK;
	//bloodstain->bmp = (char *) (data+3); // our image

}

bool walker::eat_me(walker  * eater)
{
	if (eater)
		Log("EATING A NON-TREASURE!\n");
	return 0;
}

void walker::set_direct_frame(short whichframe)
{
	frame = whichframe;

	const PixieData& data = myscreen->level_data.myloader->graphics[PIX(order, family)];
	bmp = data.data.get() + frame*size;

}

walker * walker::do_summon(char whatfamily, unsigned short lifetime)
{
	if (whatfamily || lifetime)
		Log("Should not be hitting walker::do_summon!\n");
	return nullptr;
}

bool walker::check_special()
{
	Log("Should not be hitting walker::check_special\n");
	return 0;
}

// Center us on target walker
void walker::center_on(walker  *target)
{
	short newx, newy;

	// First get the center of our target ..
	newx = target->xpos + target->sizex/2;
	newy = target->ypos + target->sizey/2;

	// Now adjust for our position ..
	newx -= sizex/2;
	newy -= sizey/2;

	// Now set our position ..
	setxy(newx, newy);
}

void walker::set_difficulty(Uint32 whatlevel)
{
	Uint32 temp, dif1;

	dif1 = difficulty_level[current_difficulty];

	switch (order)
	{
		case Order::Generator:
			temp = 100*whatlevel;
			temp = (temp * dif1) / 100;
			stats_->hitpoints = temp;
			break;
		default:  // adjust standard settings for the rest ..
			if (team_num != 0)  // do all EXCEPT player characters
			{
				stats_->max_hitpoints = (stats_->max_hitpoints*dif1) / 100.0f;
				stats_->max_magicpoints = (stats_->max_magicpoints*dif1) / 100.0f;
				damage = (damage * dif1) / 100.0f;
			}
			break;
	}

	return;
}

Sint32 walker::distance_to_ob(const walker  * target) const
{
	//Sint32 xdelta,ydelta;

	//xdelta = static_cast<Sint32>(target->xpos - xpos) +
	//         static_cast<Sint32>( (target->sizex - sizex) / 2 );
	//ydelta = static_cast<Sint32>(target->ypos - ypos) +
	//         static_cast<Sint32>( (target->sizey - sizey) / 2 );
	//return static_cast<Sint32>(xdelta*xdelta + ydelta*ydelta);
	return ( abs(target->xpos - xpos) + abs(target->ypos - ypos) );

}

Sint32 walker::distance_to_ob_center(const walker * target) const
{
	Sint32 xdelta,ydelta;

	xdelta = static_cast<Sint32>(target->xpos - xpos) +
	         static_cast<Sint32>( (target->sizex - sizex) / 2 );
	ydelta = static_cast<Sint32>(target->ypos - ypos) +
	         static_cast<Sint32>( (target->sizey - sizey) / 2 );
	return static_cast<Sint32>(xdelta*xdelta + ydelta*ydelta);
}

unsigned char walker::query_team_color() const
{
	// Debugging ..
	//if (foe && !foe->dead)
	return static_cast<unsigned char>(team_num*16+40);
	//else
	//  return static_cast<unsigned char>(7*16 + 40);
}

Sint32 walker::is_friendly(const walker *target) const
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
	// If either of us is dead, we're also unfriendly :)
	if (dead || target->dead)
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	headguy = this;
	while (headguy->owner && (headguy->owner->dead == 0) && (headguy->owner != headguy) )
		headguy = headguy->owner;
	headus = headguy;
	// Now our target ..
	headguy = target;
	while (headguy->owner && (headguy->owner->dead == 0) && (headguy->owner != headguy) )
		headguy = headguy->owner;
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
	if (myscreen->save_data.allied_mode == 0 || has_myguy == 0)
	{
		return (headus->team_num == headtarget->team_num);
	}
	
	// Allied
	if(has_myguy == 2)
    {
        // One person is missing a myguy pointer.
        // The one with a myguy pointer is owned by a player.
        // If the other person belongs to team 0 (red), then they are friendly.
        return (headtarget->myguy == nullptr && headtarget->team_num == 0) || (headus->myguy == nullptr && headus->team_num == 0);
    }

	// If we're in 'friendly' mode, then everyone with
	// a "myguy" pointer (a real, saved character)
	// is friendly to each other ..
	// By now we know that both us and the target have
	// myguy's, so we're friendly
	return 1;
}

Sint32 walker::is_friendly_to_team(unsigned char team) const
{
	// is_friendly_to_team determines if _team_ is "friendly"
	// towards this walker.
	//short allied_mode;
	short has_myguy;
	const walker *headguy;
	const walker *headus;
	
	// If dead, we're also unfriendly :)
	if (dead)
		return 0;

	// who's the top on our chains (ie, weapon->summoned->mage)
	// First us ..
	headguy = this;
	while (headguy->owner && (headguy->owner->dead == 0) && (headguy->owner != headguy) )
		headguy = headguy->owner;
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
	if (myscreen->save_data.allied_mode == 0 || has_myguy == 0)
	{
		return (headus->team_num == team);
	}
	
	// If we're a hired guy in allied mode, then we're friendly with team 0 (red)
	return (has_myguy == 1 && team == 0);
}
