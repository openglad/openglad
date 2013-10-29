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

#include "graph.h"
#include "smooth.h"

weap::weap(const PixieData& data)
    : walker(data)
{
	do_bounce = 0; // don't normally bounce :)
}

weap::~weap()
{
	//buffers: PORT: can't call destructor w/o obj: walker::~walker();
}

short weap::act()
{
	static char message[80];

	// Make sure everyone we're pointing to is valid
	if (foe && foe->dead)
		foe = NULL;
	if (leader && leader->dead)
		leader = NULL;
	if (owner && owner->dead)
		owner = NULL;

	if (!owner)
		owner = this; //to fix cases where our parent died!

	collide_ob = NULL; // always start with no collision..

	// Complete previous animations (like firing)
	if (ani_type != ANI_WALK)
		return animate();

	//  Log("weap %d is ani %d\n", family, ani_type);

	if (myscreen->level_data.mysmoother.query_genre_x_y(xpos, ypos) == TYPE_TREES)
		if (lineofsight)
			lineofsight--;

	switch (act_type)
	{
			// We are the control character
		case ACT_CONTROL:
			{
				Log("Weapon is act_control?\n");
				return 1;
			}
		case ACT_SIT: // for things like trees
			{
				if (family != FAMILY_TREE && family != FAMILY_BLOOD
				        && family != FAMILY_DOOR)
					//Log("weapon sitting\n");
					myscreen->do_notify("Weapon sitting", this);
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
				this->dead = 1;
				return 1;
			}
			// We are randomly walking toward enemy
		case ACT_RANDOM:
			{
				sprintf(message, "Weapon %d doing act random?", family);
				//Log("Weapon doing act_random?\n");
				myscreen->do_notify(message, this);
				return 1;
			}  // END RANDOM
			//break;

		default:
			{
				//Log("No act type set for weapon.\n");
				myscreen->do_notify("No act type set for weapon", this);
				return 0;
			}
	}  // END SWITCH
	return 0;
}

// death is called when an object dies (weapon destructed, etc.)
// for special effects ..
short weap::death()
{
	// Note that the 'dead' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)

	walker  *newob = NULL;

	if (death_called)  // Make sure we don't get multiple deaths
		return 0;

	death_called = 1;

	switch (family)
	{
		case FAMILY_KNIFE: // for returning knife
			if (owner && owner->query_family() != FAMILY_SOLDIER)
				break;  // only soldiers get returning knives
			newob = myscreen->level_data.add_ob(ORDER_FX, FAMILY_KNIFE_BACK);
			newob->owner = owner;
			newob->center_on(this);
			newob->lastx = lastx;
			newob->lasty = lasty;
			newob->stepsize = stepsize;
			newob->ani_type = ANI_ATTACK;
			newob->damage = damage;
			break;  // end of soldier returning knife
		case FAMILY_ROCK: // used for the elf's bouncing rock, etc.
			if (!do_bounce || !lineofsight || collide_ob) // died of natural causes
				break;
			dead = 0; // first, un-dead us so we can collide ..
			// Did we hit a barrier?
			if (myscreen->query_grid_passable(xpos+lastx, ypos+lasty, this))
			{
				dead = 1;
				break; // if not, die like normal
			}
			if (myscreen->query_grid_passable(xpos-lastx, ypos+lasty, this))
			{
				setxy(xpos-lastx, ypos+lasty);  // bounce 'down-left'
				lastx = -lastx;
				death_called = 0;
				break;
			}
			if (myscreen->query_grid_passable(xpos+lastx, ypos-lasty, this))
			{
				setxy(xpos+lastx, ypos-lasty); // bounce 'up-right'
				lasty = -lasty;
				death_called = 0;
				break;
			}
			if (myscreen->query_grid_passable(xpos-lastx, ypos-lasty, this))
			{
				setxy(xpos-lastx, ypos-lasty);
				lastx = -lastx;
				lasty = -lasty;
				death_called = 0;
				break;
			}
			// Else we're really stuck, so die :)
			dead = 1;
			break;
		case FAMILY_FIRE_ARROW: // only for exploding, really
		case FAMILY_BOULDER:
			if (!skip_exit)
				break;  // skip_exit means we're supposed to explode :)
			if (!owner || owner->dead)
				owner = this;
			newob = myscreen->level_data.add_ob(ORDER_FX, FAMILY_EXPLOSION, 1);
			if (!newob)
				break; // failsafe
			if (on_screen())
				myscreen->soundp->play_sound(SOUND_EXPLODE);
			newob->owner = owner;
			newob->stats->hitpoints = 0;
			newob->stats->level = owner->stats->level;
			newob->ani_type = ANI_EXPLODE;
			newob->center_on(this);
			newob->damage = damage*2;
			break;  // end fire (exploding) arrows
		case FAMILY_WAVE: // grow to wave2
			dead = 0;
			transform_to(ORDER_WEAPON, FAMILY_WAVE2);
			stats->hitpoints = stats->max_hitpoints;
			break;  // end wave -> wave2
		case FAMILY_WAVE2: // grow to wave3
			dead = 0;
			transform_to(ORDER_WEAPON, FAMILY_WAVE3);
			stats->hitpoints = stats->max_hitpoints;
			break;  // end wave2 -> wave3
		case FAMILY_DOOR: // display open picture
			newob = myscreen->level_data.add_weap_ob(ORDER_FX, FAMILY_DOOR_OPEN);
			if (!newob)
				break;
			newob->ani_type = ANI_DOOR_OPEN;
			newob->setxy(xpos, ypos);
			newob->stats->level = stats->level;
			newob->team_num = team_num;
			//      newob->ignore = 1;
			// What way are we 'facing'?
			if (myscreen->level_data.mysmoother.query_genre_x_y((xpos/GRID_SIZE),(ypos/GRID_SIZE)-1)
			        == TYPE_WALL) // a wall above us?
			{
				newob->curdir = FACE_RIGHT;
				//        newob->setxy(xpos, ypos-12); // and move us 'up'
			}
			else
			{
				curdir = FACE_UP;
			}
			break; // end open the door ..
		default:
			break;
	}

	return 1;

}

short weap::animate()
{
	//walker  * newob;

	// We never use ani_type as  as I can tell; always use 0
	//  if (ani_type)
	//  {
	//       Log("weap ani_type = %d\n", ani_type);
	//       ani_type = 0;
	//  }

	switch (family)
	{
		case FAMILY_TREE:
		case FAMILY_BLOOD:
			if (ani_type > 1)
				ani_type = 0;
			set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
			cycle++;
			if (ani[curdir+ani_type*NUM_FACINGS][cycle] == -1)
			{
				ani_type = 0; //ANI_WALK;
				cycle = 0;
			}
			break;
		case FAMILY_CIRCLE_PROTECTION:
			if (!owner || owner->dead || stats->hitpoints <= 0)
			{
				dead = 1;
				return death();
			}
			center_on(owner);
			break;
		case FAMILY_GLOW:
			if (ani_type > 2) // illegal case
				ani_type = 2; // pulse case
			set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
			cycle++;
			if (ani[curdir+ani_type*NUM_FACINGS][cycle] == -1)
			{
				ani_type = 2; // pulse
				cycle = 0;
			}
			if (lifetime-- < 1)
			{
				dead = 1;
				death();
			}
			break;
		default:
			ani_type = 0;
			set_frame(ani[curdir][cycle]);
			cycle++;
			if (ani[curdir][cycle] == -1)
			{
				cycle = 0;
			}
			break;
	} // end of family switch

	return 1;
}

short weap::setxy(short x, short y)
{
	return walker::setxy(x, y);
}

