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
	ignore_ = 1; // don't collide with other objects
}

effect::~effect()
{
	// Zardus: PORT: that parent object problem again:  walker::~walker();
}

bool effect::act()
{
	short temp;
	float xd, yd;
	Sint32 distance, generic;
	walker *newob;
	short numfoes;
	std::list<walker*> foelist;

	// Make sure everyone we're pointing to is valid
	if (foe_ && foe_->is_dead())
		foe_ = nullptr;
	if (leader_ && leader_->is_dead())
		leader_ = nullptr;
	if (owner_ && owner_->is_dead())
		owner_ = nullptr;

	collide_ob_ = nullptr; // always start with no collision..

	// Any special actions ..
	switch (family) // determine what to do..
	{
		case FAMILY_GHOST_SCARE:
			if (owner_)
				center_on(owner_);
			break;
		case FAMILY_MAGIC_SHIELD: // revolve around owner_
			if (!owner_ || owner_->is_dead())
			{
				dead_ = 1;
				death();
				break;
			}
			switch (drawcycle_ % 16)
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
			center_on(owner_);
			setworldxy(worldx_+xd, worldy_+yd);
			foelist = myscreen->find_foe_weapons_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);
            
			for(auto* w : foelist)  // first weapons
			{
				stats_->hitpoints -= w->damage();
				w->set_dead(1);
				w->death();
			}

			foelist = myscreen->find_foes_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);

			for(auto* w : foelist)  // second enemies
			{
				stats_->hitpoints -= w->damage();
				attack(w);
				dead_ = 0;
			}
			
			if ( (stats_->hitpoints <= 0) || (lifetime_-- < 0) )
			{
				dead_ = 1;
				death();
			}
			break; // end of magic shield case
		case FAMILY_BOOMERANG: // fighter's boomerang
			// Zardus: FIX: if the drawcycle_ is in its >253s, the boomerang dies. This will fix the bug where
			// the boomerang comes back to 0 (owner) after spiraling around all the way if the owner has
			// that good of an ability (to keep its life so high). This caps boomerang ability, though... Another
			// fix could be to make the drawcycle_ var an int or at least something with more capacity than char.
			if (!owner_ || owner_->is_dead() || drawcycle_ > 253)
			{
				dead_ = 1;
				death();
				break;
			}
			switch (drawcycle_ % 16)
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
			xd *= (drawcycle_+4);
			xd /= 48;
			yd *= (drawcycle_+4);
			yd /= 48;
			center_on(owner_);
			setworldxy(worldx_+xd, worldy_+yd);
			foelist = myscreen->find_foe_weapons_in_range(
			              myscreen->level_data.oblist, sizex*2, &temp, this);
			              
			for(auto* w : foelist)  // first weapons
			{
				stats_->hitpoints -= w->damage();
				w->set_dead(1);
				w->death();
			}

			foelist = myscreen->find_foes_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);

			for(auto* w : foelist) // second enemies
			{
				stats_->hitpoints -= w->damage();
				attack(w);
				dead_ = 0;
			}
			
			if ( (stats_->hitpoints <= 0) || (lifetime_-- < 0) )
			{
				dead_ = 1;
				death();
			}
			break; // end of boomerang case
		case FAMILY_KNIFE_BACK: // returning blade
			if (!owner_ || owner_->is_dead())
			{
				dead_ = 1;
				break;
			}
			distance = distance_to_ob(owner_);
			if (distance > 10)
			{
				xd = yd = 0; // zero out distance movements
				if (owner_->xpos > xpos)
				{
					if ( (owner_->xpos - xpos) > stepsize_ )
						xd = stepsize_;
					else
						xd = owner_->xpos - xpos;
				}
				else if (owner_->xpos < xpos)
				{
					if ( (xpos - owner_->xpos) > stepsize_ )
						xd = -stepsize_;
					else
						xd = owner_->xpos - xpos;
				}
				if (owner_->ypos > ypos)
				{
					if ( (owner_->ypos - ypos) > stepsize_ )
						yd = stepsize_;
					else
						yd = owner_->ypos - ypos;
				}
				else if (owner_->ypos < ypos)
				{
					if ( (ypos - owner_->ypos) > stepsize_ )
						yd = -stepsize_;
					else
						yd = owner_->ypos - ypos;
				}
				setworldxy(worldx_+xd, worldy_+yd);
				newob = myscreen->level_data.add_ob(Order::Weapon, FAMILY_KNIFE);
				newob->set_damage(damage_);
				newob->set_owner(owner_);
				newob->set_team_num(team_num_);
				newob->set_death_called(1); // to ensure no spawning of more ..
				newob->setworldxy(worldx_, worldy_);
				if (!myscreen->query_object_passable(xpos+xd, ypos+yd, newob))
				{
					newob->attack(newob->collide_ob());
					damage_ /= 4.0f;
					//setxy(xpos-(2*xd)+random(xd), ypos-(2*yd)+random(yd));
				}
				newob->set_dead(1);
			}
			else
			{
				owner_->set_weapons_left(owner_->weapons_left() + 1);
				//if (owner->user != -1)
				//{
				//  sprintf(message, "Knives now %d", owner->weapons_left_);
				//  myscreen->do_notify(message, owner);
				//}
				ani_type_ = ANI_WALK;
				dead_ = 1;
			}
			break;
		case FAMILY_CLOUD: // poison cloud
			if (lifetime_ > 0)
				lifetime_--;
			else
			{
				dead_ = 1;
				death();
			}
			if (lifetime_ < 8)
				invisibility_left_ +=3;
			if (invisibility_left_ > 0)
				invisibility_left_--;
			// Hit any nearby foes (not friends, for now)
			foelist = myscreen->find_foes_in_range(
			              myscreen->level_data.oblist, sizex, &temp, this);
            
			for(auto* w : foelist) //
			{
				if (hits(xpos, ypos, sizex, sizey, // this is the cloud
				         w->xpos, w->ypos,
				         w->sizex, w->sizey)
				   )
				{
					attack(w);
				} // end of actual hit
			}
			
			// Are we performing some action_?
			if (stats_->has_commands())
				temp = stats_->do_command();
			else
			{
				xd = yd = 0;
				while (xd == 0 && yd == 0)
				{
					xd = random(3)-1;
					yd = random(3)-1;
				}
				stats_->add_command(COMMAND_WALK, static_cast<short>(random(20)), static_cast<short>(xd), static_cast<short>(yd));
			}
			break; // end of cloud
		case FAMILY_CHAIN: // chain lightning ..
			if (!leader_ || lineofsight_<1 || !owner_) // lost our leader_, etc.? kill us ..
			{
				dead_ = 1;
				death();
				return 1;
			}
			// Are we at our leader? If so, attack him :)
			if (hits(xpos, ypos, sizex, sizey,
			         leader_->xpos, leader_->ypos, leader_->sizex, leader_->sizey))
			{
				// Do things ..
				newob = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION);
				if (!newob)
				{
					dead_ = 1;
					death();
					return 1; // failsafe
				}
				newob->set_owner(owner_);
				newob->set_team_num(team_num_);
				newob->stats()->level = stats_->level;
				newob->set_damage(damage_);
				newob->set_ani_type(ANI_EXPLODE);
				newob->center_on(this);
				leader_->set_skip_exit(leader_->skip_exit() + 3); // can't hit us for 3 rounds ..
				if (on_screen())
					myscreen->soundp->play_sound(SOUND_EXPLODE);
				// Now make new objects to seek out foes ..
				// First, are our offspring powerful enough at 1/2 our power?
				generic = (damage_)/2;
				if (owner_->myguy())
					foelist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      240+(owner_->myguy()->intelligence/2), &temp, this);
				else
					foelist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                      240+stats_->level*5, &temp, this);
				if (temp && generic>20) // more foes to find ..
				{
					numfoes = random(owner_->stats()->level)+1;
					for(auto* w : foelist)
					{
					    if (numfoes <= 0) break;
						if (w != leader_ && w->skip_exit()<1) // don't hit current guy, etc.
						{
							newob = myscreen->level_data.add_ob(Order::FX, FAMILY_CHAIN);
							if (!newob)
								return 0; // failsafe

							newob->set_owner(owner_);  // our caster
							newob->set_leader(w); // guy to attack
							newob->stats()->level = stats_->level;
							newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
							newob->set_damage(generic);
							newob->set_team_num(team_num_);
							newob->center_on(this);
						} // end of wasn't current guy case
						numfoes--;
					} // end of loop for nearby foes we found
				} // end of check for nearby foes
                
				dead_ = 1;
				death();
				return 1;
			}
			// Move toward our leader ..
			lineofsight_--;
			distance = distance_to_ob_center(leader_);
			if (distance > stepsize_*2)
			{
				xd = yd = 0; // zero out distance movements
				if (leader_->xpos > xpos)
				{
					if ( (leader_->xpos - xpos) > stepsize_ )
						xd = stepsize_;
					else
						xd = leader_->xpos - xpos;
				}
				else if (leader_->xpos < xpos)
				{
					if ( (xpos - leader_->xpos) > stepsize_ )
						xd = -stepsize_;
					else
						xd = leader_->xpos - xpos;
				}
				if (leader_->ypos > ypos)
				{
					if ( (leader_->ypos - ypos) > stepsize_ )
						yd = stepsize_;
					else
						yd = leader_->ypos - ypos;
				}
				else if (leader_->ypos < ypos)
				{
					if ( (ypos - leader_->ypos) > stepsize_ )
						yd = -stepsize_;
					else
						yd = leader_->ypos - ypos;
				}
				// Set our facing?
				curdir_ = facing(xd, yd);
				set_frame(ani_[curdir_][0]);
			} // end of big step
			else
			{
				//xd = leader->xpos;
				//yd = leader->ypos;
				center_on(leader_);
				return 1;
			}
			setworldxy(worldx_+xd, worldy_+yd);
			return 1;  // so as not to animate, etc.
			//break; // end of FAMILY_CHAIN

		case FAMILY_DOOR_OPEN:

			// Here is how doors work.  They start out as a FAMILY_DOOR
			//  from Order::Weapon under the weaplist.  When the door is
			//  collided with, the obmap marks the door as dead, and spawns
			//  the FAMILY_DOOR_OPEN on the weaplist (this object).  It
			//  animates ANI_DOOR_OPEN, and when it is done, it dies and
			//  spawns a FAMILY_DOOR_OPEN on the fxlist.  The amusing part
			//  is that now that it is on the fxlist, it won't act anymore,
			//  thus preventing it from continuously respawning itself.

			if (ani_type_ != ANI_WALK)
				return animate();
			newob = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_DOOR_OPEN);
			if (!newob)
				break;
			newob->set_ani_type(ANI_WALK);
			newob->setworldxy(worldx_, worldy_);
			newob->stats()->level = stats_->level;
			newob->set_team_num(team_num_);
			newob->set_ignore(1);
			newob->set_curdir(curdir_);
			// set correct frame
			newob->animate();
			dead_ = 1;
			death();
			return 1;
			break;

		default:
			break;
	}

	// Complete previous animations (like firing)
	if (ani_type_ != ANI_WALK)
		return animate();

	switch (family) // determine what to do..
	{
		default:
			dead_ = 1;
			death();
			break;
	}

	return 0;
}

bool effect::animate()
{

	set_frame(ani_[curdir_+ani_type_*NUM_FACINGS][cycle_]);
	cycle_++;

	switch (family)
	{
		case FAMILY_MAGIC_SHIELD:
		case FAMILY_BOOMERANG:
		case FAMILY_KNIFE_BACK:
		case FAMILY_CLOUD:
		case FAMILY_MARKER:
			if (ani_[curdir_+ani_type_*NUM_FACINGS][cycle_] == -1)
				cycle_ = 0;
			break;
		default:
			if (ani_[curdir_+ani_type_*NUM_FACINGS][cycle_] == -1)
				ani_type_ = ANI_WALK;
			break;
	}

	return 1;
}

// death is called when an object dies (or weapon destructed, etc.)
// for special effects ..
bool effect::death()
{
	// Note that the 'dead' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)
	std::list<walker*> foelist;
	short howmany = 0;
	walker  *newob;
	Sint32 xdelta,ydelta;
	Sint32 tempx, tempy, generic;

	if (death_called_)
		return 0;
	death_called_ = 1;

	switch (family)
	{
		case FAMILY_GHOST_SCARE: // the ghost's scare
			if (!owner_ || owner_->is_dead())
				return 0;
			foelist = myscreen->find_foes_in_range(myscreen->level_data.oblist, 50+(10*owner_->stats()->level),
			                                        &howmany, owner_);
			if (howmany < 1)
				return 0;
            
            for(auto* w : foelist)
			{
				if (w && w->query_order() == Order::Living)
				{
					tempx = w->xpos - xpos;
					if (tempx)
						tempx = tempx / (abs(tempx));
					tempy = w->ypos - ypos;
					if (tempy)
						tempy = tempy / (abs(tempy));
					generic = (owner_->stats()->level*25);
					if (w->myguy())
						generic -= random(w->myguy()->constitution);
					if (generic > 0)
						w->stats()->force_command(COMMAND_WALK,
						                               static_cast<short>(generic), static_cast<short>(tempx), static_cast<short>(tempy));
				} // end of valid target
			} // end of cycle_ through scare list
			
			break;  // end of ghost scare
		case FAMILY_BOMB: // Burning bomb
			if (!owner_ || owner_->is_dead())
				owner_ = this;
			if (on_screen())
				myscreen->soundp->play_sound(SOUND_EXPLODE);
			newob = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION, 1);
			newob->set_owner(owner_);
			newob->stats()->hitpoints = 0;
			newob->stats()->level = owner_->stats()->level;
			newob->set_ani_type(ANI_EXPLODE);
			//newob->setxy(xpos, ypos);
			newob->center_on(this);
			newob->set_damage(damage_);
			break;

		case FAMILY_EXPLOSION: // the bomb's explosion
			if (!owner_ || owner_->is_dead())
				owner_ = this;
			// Set the max distance for a bomb ..
			generic = 4*owner_->stats()->level;
			if (generic > 96) // set max range to about 6 tiles
				generic = 96;
			if (skip_exit_) // magical, ie mage, don't go far ..
			{
				generic = 16;
			}
			foelist = myscreen->find_in_range(myscreen->level_data.oblist, 15+generic,
			                                 &howmany, this);
            
			// Damage our tile location ..
			myscreen->damage_tile( static_cast<short>(xpos+(sizex/2)), static_cast<short>(ypos+(sizey/2)) );
			if (howmany < 1)
				return 0;
			// Set our team number to garbage so we can hurt everyone
			//team_num = 50;
			for(auto* w : foelist)
			{
				if (w && !w->is_dead() &&
				        (w->query_order() != Order::Treasure) &&
				        (w->query_order() != Order::FX) &&
				        (!skip_exit_ || w != owner_)
				   ) //&&
					//       w->query_order() == Order::Living
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
					generic = 2+owner_->stats()->level/15;
					if (generic > 8) // max of about 8 steps
						generic = 8;
					w->stats()->force_command(COMMAND_WALK,generic,static_cast<short>(xdelta),static_cast<short>(ydelta));
					// Damage (attack) the object
					if (w == owner_) // do less damage_
					{
						damage_ /= 4.0f;
						attack(w);
						damage_ *= 4.0f;
					}
					else if (!owner_->is_dead() && owner_->is_friendly(w))
					{
						damage_ /= 2.0f;
						attack(w);
						damage_ *= 2.0f;
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
	x2right = static_cast<short>(x2+xsize2);
	if (x > x2right)
		return 0;

	xright = static_cast<short>(x+xsize);
	if (xright < x2)
		return 0;

	y2down = static_cast<short>(y2+ysize2);
	if (y > y2down)
		return 0;

	ydown = static_cast<short>(y+ysize);
	if (ydown < y2)
		return 0;

	return 1;
}
