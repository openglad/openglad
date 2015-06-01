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
// effect; a derived class of walker
//
// Generally, an effect will sit on the normal list, have its
//   act called, but will not collide with anything.  At the
//   end of its animation, it will call function x.
//

//#include "graph.h"
#include "effect.h"

short hits(short x,  short y,  short xsize,  short ysize,
           short x2, short y2, short xsize2, short ysize2);

effect::effect(const PixieData& data)
    : walker(data)
{
	ignore = 1; // don't collide with other objects
}

effect::~effect()
{
	// Zardus: PORT: that parent object problem again:  walker::~walker();
}

short effect::act()
{
	short temp;
	float xd, yd;
	Sint32 distance, generic;
	walker *newob;
	short numfoes;
	std::list<walker*> foelist;

	// Make sure everyone we're pointing to is valid
	if (foe && foe->dead)
		foe = NULL;
	if (leader && leader->dead)
		leader = NULL;
	if (owner && owner->dead)
		owner = NULL;

	collide_ob = NULL; // always start with no collision..

	// Any special actions ..
	switch (family) // determine what to do..
	{
		case FAMILY_GHOST_SCARE:
			if (owner)
				center_on(owner);
			break;
		case FAMILY_MAGIC_SHIELD: // revolve around owner
			if (!owner || owner->dead)
			{
				dead = 1;
				death();
				break;
			}
			switch (drawcycle % 16)
			{
				case 0:
					xd = 0;
					yd = -24;
					break;
				case 1:
					xd = -9;
					yd = -22;
					break;
				case 2:
					xd = -17;
					yd = -17;
					break;
				case 3:
					xd = -22;
					yd = -9;
					break;

				case 4:
					xd = -24;
					yd = 0;
					break;
				case 5:
					xd = -22;
					yd = 9;
					break;
				case 6:
					xd = -17;
					yd = 17;
					break;
				case 7:
					xd = -9;
					yd = 22;
					break;

				case 8:
					xd = 0;
					yd = 24;
					break;
				case 9:
					xd = 9;
					yd = 22;
					break;
				case 10:
					xd = 17;
					yd = 17;
					break;
				case 11:
					xd = 22;
					yd = 9;
					break;

				case 12:
					xd = 24;
					yd = 0;
					break;
				case 13:
					xd = 22;
					yd = -9;
					break;
				case 14:
					xd = 17;
					yd = -17;
					break;
				case 15:
					xd = 9;
					yd = -22;
					break;
			}
			center_on(owner);
			setworldxy(worldx+xd, worldy+yd);
			foelist = myscreen->find_foe_weapons_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);
            
			for(auto e = foelist.begin(); e != foelist.end(); e++)  // first weapons
			{
			    walker* w = *e;
				stats->hitpoints -= w->damage;
				w->dead = 1;
				w->death();
			}
			
			foelist = myscreen->find_foes_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);
            
			for(auto e = foelist.begin(); e != foelist.end(); e++)  // second enemies
			{
			    walker* w = *e;
				stats->hitpoints -= w->damage;
				attack(w);
				dead = 0;
			}
			
			if ( (stats->hitpoints <= 0) || (lifetime-- < 0) )
			{
				dead = 1;
				death();
			}
			break; // end of magic shield case
		case FAMILY_BOOMERANG: // fighter's boomerang
			// Zardus: FIX: if the drawcycle is in its >253s, the boomerang dies. This will fix the bug where
			// the boomerang comes back to 0 (owner) after spiraling around all the way if the owner has
			// that good of an ability (to keep its life so high). This caps boomerang ability, though... Another
			// fix could be to make the drawcycle var an int or at least something with more capacity than char.
			if (!owner || owner->dead || drawcycle > 253)
			{
				dead = 1;
				death();
				break;
			}
			switch (drawcycle % 16)
			{
				case 0:
					xd = 0;
					yd = -24;
					break;
				case 1:
					xd = -9;
					yd = -22;
					break;
				case 2:
					xd = -17;
					yd = -17;
					break;
				case 3:
					xd = -22;
					yd = -9;
					break;

				case 4:
					xd = -24;
					yd = 0;
					break;
				case 5:
					xd = -22;
					yd = 9;
					break;
				case 6:
					xd = -17;
					yd = 17;
					break;
				case 7:
					xd = -9;
					yd = 22;
					break;

				case 8:
					xd = 0;
					yd = 24;
					break;
				case 9:
					xd = 9;
					yd = 22;
					break;
				case 10:
					xd = 17;
					yd = 17;
					break;
				case 11:
					xd = 22;
					yd = 9;
					break;

				case 12:
					xd = 24;
					yd = 0;
					break;
				case 13:
					xd = 22;
					yd = -9;
					break;
				case 14:
					xd = 17;
					yd = -17;
					break;
				case 15:
					xd = 9;
					yd = -22;
					break;
			}
			xd *= (drawcycle+4);
			xd /= 48;
			yd *= (drawcycle+4);
			yd /= 48;
			center_on(owner);
			setworldxy(worldx+xd, worldy+yd);
			foelist = myscreen->find_foe_weapons_in_range(
			              myscreen->level_data.oblist, sizex*2, &temp, this);
			              
			for(auto e = foelist.begin(); e != foelist.end(); e++)  // first weapons
			{
			    walker* w = *e;
				stats->hitpoints -= w->damage;
				w->dead = 1;
				w->death();
			}
			
			foelist = myscreen->find_foes_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);
            
			for(auto e = foelist.begin(); e != foelist.end(); e++) // second enemies
			{
			    walker* w = *e;
				stats->hitpoints -= w->damage;
				attack(w);
				dead = 0;
			}
			
			if ( (stats->hitpoints <= 0) || (lifetime-- < 0) )
			{
				dead = 1;
				death();
			}
			break; // end of boomerang case
		case FAMILY_KNIFE_BACK: // returning blade
			if (!owner || owner->dead)
			{
				dead = 1;
				break;
			}
			distance = distance_to_ob(owner);
			if (distance > 10)
			{
				xd = yd = 0; // zero out distance movements
				if (owner->xpos > xpos)
				{
					if ( (owner->xpos - xpos) > stepsize )
						xd = stepsize;
					else
						xd = owner->xpos - xpos;
				}
				else if (owner->xpos < xpos)
				{
					if ( (xpos - owner->xpos) > stepsize )
						xd = -stepsize;
					else
						xd = owner->xpos - xpos;
				}
				if (owner->ypos > ypos)
				{
					if ( (owner->ypos - ypos) > stepsize )
						yd = stepsize;
					else
						yd = owner->ypos - ypos;
				}
				else if (owner->ypos < ypos)
				{
					if ( (ypos - owner->ypos) > stepsize )
						yd = -stepsize;
					else
						yd = owner->ypos - ypos;
				}
				setworldxy(worldx+xd, worldy+yd);
				newob = myscreen->level_data.add_ob(ORDER_WEAPON, FAMILY_KNIFE);
				newob->damage = damage;
				newob->owner = owner;
				newob->team_num = team_num;
				newob->death_called = 1; // to ensure no spawning of more ..
				newob->setworldxy(worldx, worldy);
				if (!myscreen->query_object_passable(xpos+xd, ypos+yd, newob))
				{
					newob->attack(newob->collide_ob);
					damage /= 4.0f;
					//setxy(xpos-(2*xd)+random(xd), ypos-(2*yd)+random(yd));
				}
				newob->dead = 1;
			}
			else
			{
				owner->weapons_left++;
				//if (owner->user != -1)
				//{
				//  sprintf(message, "Knives now %d", owner->weapons_left);
				//  myscreen->do_notify(message, owner);
				//}
				ani_type = ANI_WALK;
				dead = 1;
			}
			break;
		case FAMILY_CLOUD: // poison cloud
			if (lifetime > 0)
				lifetime--;
			else
			{
				dead = 1;
				death();
			}
			if (lifetime < 8)
				invisibility_left +=3;
			if (invisibility_left > 0)
				invisibility_left--;
			// Hit any nearby foes (not friends, for now)
			foelist = myscreen->find_foes_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);
            
			for(auto e = foelist.begin(); e != foelist.end(); e++) //
			{
			    walker* w = *e;
				if (hits(xpos, ypos, sizex, sizey, // this is the cloud
				         w->xpos, w->ypos,
				         w->sizex, w->sizey)
				   )
				{
					attack(w);
				} // end of actual hit
			}
			
			// Are we performing some action?
			if (stats->has_commands())
				temp = stats->do_command();
			else
			{
				xd = yd = 0;
				while (xd == 0 && yd == 0)
				{
					xd = random(3)-1;
					yd = random(3)-1;
				}
				stats->add_command(COMMAND_WALK, (short) random(20), (short) xd, (short) yd);
			}
			break; // end of cloud
		case FAMILY_CHAIN: // chain lightning ..
			if (!leader || lineofsight<1 || !owner) // lost our leader, etc.? kill us ..
			{
				dead = 1;
				death();
				return 1;
			}
			// Are we at our leader? If so, attack him :)
			if (hits(xpos, ypos, sizex, sizey,
			         leader->xpos, leader->ypos, leader->sizex, leader->sizey))
			{
				// Do things ..
				newob = myscreen->level_data.add_ob(ORDER_FX, FAMILY_EXPLOSION);
				if (!newob)
				{
					dead = 1;
					death();
					return 1; // failsafe
				}
				newob->owner = owner;
				newob->team_num = team_num;
				newob->stats->level = stats->level;
				newob->damage = damage;
				newob->ani_type = ANI_EXPLODE;
				newob->center_on(this);
				leader->skip_exit += 3; // can't hit us for 3 rounds ..
				if (on_screen())
					myscreen->soundp->play_sound(SOUND_EXPLODE);
				// Now make new objects to seek out foes ..
				// First, are our offspring powerful enough at 1/2 our power?
				generic = (damage)/2;
				if (owner->myguy)
					foelist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      240+(owner->myguy->intelligence/2), &temp, this);
				else
					foelist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      240+stats->level*5, &temp, this);
				if (temp && generic>20) // more foes to find ..
				{
					numfoes = random(owner->stats->level)+1;
					for(auto e = foelist.begin(); e != foelist.end() && numfoes > 0; e++, numfoes--)
					{
					    walker* w = *e;
						if (w != leader && w->skip_exit<1) // don't hit current guy, etc.
						{
							newob = myscreen->level_data.add_ob(ORDER_FX, FAMILY_CHAIN);
							if (!newob)
								return 0; // failsafe
                            
							newob->owner = owner;  // our caster
							newob->leader = w; // guy to attack
							newob->stats->level = stats->level;
							newob->stats->set_bit_flags(BIT_MAGICAL, 1);
							newob->damage = generic;
							newob->team_num = team_num;
							newob->center_on(this);
						} // end of wasn't current guy case
					} // end of loop for nearby foes we found
				} // end of check for nearby foes
                
				dead = 1;
				death();
				return 1;
			}
			// Move toward our leader ..
			lineofsight--;
			distance = distance_to_ob_center(leader);
			if (distance > stepsize*2)
			{
				xd = yd = 0; // zero out distance movements
				if (leader->xpos > xpos)
				{
					if ( (leader->xpos - xpos) > stepsize )
						xd = stepsize;
					else
						xd = leader->xpos - xpos;
				}
				else if (leader->xpos < xpos)
				{
					if ( (xpos - leader->xpos) > stepsize )
						xd = -stepsize;
					else
						xd = leader->xpos - xpos;
				}
				if (leader->ypos > ypos)
				{
					if ( (leader->ypos - ypos) > stepsize )
						yd = stepsize;
					else
						yd = leader->ypos - ypos;
				}
				else if (leader->ypos < ypos)
				{
					if ( (ypos - leader->ypos) > stepsize )
						yd = -stepsize;
					else
						yd = leader->ypos - ypos;
				}
				// Set our facing?
				curdir = facing(xd, yd);
				set_frame(ani[curdir][0]);
			} // end of big step
			else
			{
				//xd = leader->xpos;
				//yd = leader->ypos;
				center_on(leader);
				return 1;
			}
			setworldxy(worldx+xd, worldy+yd);
			return 1;  // so as not to animate, etc.
			//break; // end of FAMILY_CHAIN

		case FAMILY_DOOR_OPEN:

			// Here is how doors work.  They start out as a FAMILY_DOOR
			//  from ORDER_WEAPON under the weaplist.  When the door is
			//  collided with, the obmap marks the door as dead, and spawns
			//  the FAMILY_DOOR_OPEN on the weaplist (this object).  It
			//  animates ANI_DOOR_OPEN, and when it is done, it dies and
			//  spawns a FAMILY_DOOR_OPEN on the fxlist.  The amusing part
			//  is that now that it is on the fxlist, it won't act anymore,
			//  thus preventing it from continuously respawning itself.

			if (ani_type != ANI_WALK)
				return animate();
			newob = myscreen->level_data.add_fx_ob(ORDER_FX, FAMILY_DOOR_OPEN);
			if (!newob)
				break;
			newob->ani_type = ANI_WALK;
			newob->setworldxy(worldx, worldy);
			newob->stats->level = stats->level;
			newob->team_num = team_num;
			newob->ignore = 1;
			newob->curdir = curdir;
			// set correct frame
			newob->animate();
			dead = 1;
			death();
			return 1;
			break;

		default:
			break;
	}

	// Complete previous animations (like firing)
	if (ani_type != ANI_WALK)
		return animate();

	switch (family) // determine what to do..
	{
		default:
			dead = 1;
			death();
			break;
	}

	return 0;
}

short effect::animate()
{

	set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
	cycle++;

	switch (family)
	{
		case FAMILY_MAGIC_SHIELD:
		case FAMILY_BOOMERANG:
		case FAMILY_KNIFE_BACK:
		case FAMILY_CLOUD:
		case FAMILY_MARKER:
			if (ani[curdir+ani_type*NUM_FACINGS][cycle] == -1)
				cycle = 0;
			break;
		default:
			if (ani[curdir+ani_type*NUM_FACINGS][cycle] == -1)
				ani_type = ANI_WALK;
			break;
	}

	return 1;
}

// death is called when an object dies (or weapon destructed, etc.)
// for special effects ..
short effect::death()
{
	// Note that the 'dead' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)
	std::list<walker*> foelist;
	short howmany = 0;
	walker  *newob;
	Sint32 xdelta,ydelta;
	Sint32 tempx, tempy, generic;

	if (death_called)
		return 0;
	death_called = 1;

	switch (family)
	{
		case FAMILY_GHOST_SCARE: // the ghost's scare
			if (!owner || owner->dead)
				return 0;
			foelist = myscreen->find_foes_in_range(myscreen->level_data.oblist, 50+(10*owner->stats->level),
			                                        &howmany, owner);
			if (howmany < 1)
				return 0;
            
            for(auto e = foelist.begin(); e != foelist.end(); e++)
			{
			    walker* w = *e;
				if (w && w->query_order() == ORDER_LIVING)
				{
					tempx = w->xpos - xpos;
					if (tempx)
						tempx = tempx / (abs(tempx));
					tempy = w->ypos - ypos;
					if (tempy)
						tempy = tempy / (abs(tempy));
					generic = (owner->stats->level*25);
					if (w->myguy)
						generic -= random(w->myguy->constitution);
					if (generic > 0)
						w->stats->force_command(COMMAND_WALK,
						                               (short) generic, (short) tempx, (short) tempy);
				} // end of valid target
			} // end of cycle through scare list
			
			break;  // end of ghost scare
		case FAMILY_BOMB: // Burning bomb
			if (!owner || owner->dead)
				owner = this;
			if (on_screen())
				myscreen->soundp->play_sound(SOUND_EXPLODE);
			newob = myscreen->level_data.add_ob(ORDER_FX, FAMILY_EXPLOSION, 1);
			newob->owner = owner;
			newob->stats->hitpoints = 0;
			newob->stats->level = owner->stats->level;
			newob->ani_type = ANI_EXPLODE;
			//newob->setxy(xpos, ypos);
			newob->center_on(this);
			newob->damage = damage;
			break;

		case FAMILY_EXPLOSION: // the bomb's explosion
			if (!owner || owner->dead)
				owner = this;
			// Set the max distance for a bomb ..
			generic = 4*owner->stats->level;
			if (generic > 96) // set max range to about 6 tiles
				generic = 96;
			if (skip_exit) // magical, ie mage, don't go far ..
			{
				generic = 16;
			}
			foelist = myscreen->find_in_range(myscreen->level_data.oblist, 15+generic,
			                                 &howmany, this);
            
			// Damage our tile location ..
			myscreen->damage_tile( (short) (xpos+(sizex/2)), (short) (ypos+(sizey/2)) );
			if (howmany < 1)
				return 0;
			// Set our team number to garbage so we can hurt everyone
			//team_num = 50;
			for(auto e = foelist.begin(); e != foelist.end(); e++)
			{
			    walker* w = *e;
				if (w && !w->dead &&
				        (w->query_order() != ORDER_TREASURE) &&
				        (w->query_order() != ORDER_FX) &&
				        (!skip_exit || w != owner)
				   ) //&&
					//       w->query_order() == ORDER_LIVING
					//     && w->team_num != owner->team_num
					//      )
				{
					//shove the target
					xdelta = w->xpos - xpos;
					if (xdelta)
						xdelta = xdelta/abs(xdelta);
					ydelta = w->ypos - ypos;
					if (ydelta)
						ydelta = ydelta/abs(ydelta);
					// Set the distance to 'shove' by explosion
					generic = 2+owner->stats->level/15;
					if (generic > 8) // max of about 8 steps
						generic = 8;
					w->stats->force_command(COMMAND_WALK,generic,(short)xdelta,(short)ydelta);
					// Damage (attack) the object
					if (w == owner) // do less damage
					{
						damage /= 4.0f;
						attack(w);
						damage *= 4.0f;
					}
					else if (!owner->dead && owner->is_friendly(w))
					{
						damage /= 2.0f;
						attack(w);
						damage *= 2.0f;
					}
					else
						attack(w);
				}
			}
			break;  // end explosion case
		default:
			break;
	}                    // end of switch family for effect objects

	return 1;
}

short hits(short x,  short y,  short xsize,  short ysize,
           short x2, short y2, short xsize2, short ysize2)
{
	short xright, x2right;
	short ydown,  y2down;

	//return 0; // debug
	x2right = (short) (x2+xsize2);
	if (x > x2right)
		return 0;

	xright = (short) (x+xsize);
	if (xright < x2)
		return 0;

	y2down = (short) (y2+ysize2);
	if (y > y2down)
		return 0;

	ydown = (short) (y+ysize);
	if (ydown < y2)
		return 0;

	return 1;
}
