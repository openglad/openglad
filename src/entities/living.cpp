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
// Living; derived class of walker
//

#include <openglad/runtime/game_context.h>
#include <openglad/core/combat_math.h>
#include <openglad/render/smooth.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/living.h>
#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/screen.h>
#include <cstring>

// From picker
extern Sint32 difficulty_level[DIFFICULTY_SETTINGS];
extern Sint32 current_difficulty;

// Shorthand for the injectable RNG
static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

static inline cfg_store& active_config()
{
    if(ctx().config != nullptr)
        return *ctx().config;
    return cfg;
}

living::living(const PixieData& data)
    : walker(data)
{
	current_special = 1;
	lifetime = 0;
}

living::~living()
{}

bool living::act()
{
	if (bonus_rounds>0 && !dead)  // we get extra rounds to act this cycle
	{
		bonus_rounds--;
		act();
	}
	if (dead)
		return 0;

	// Make sure everyone we're pointing to is valid
	if (foe && (foe->dead || (rng(foe->invisibility_left/20) > 0) ) )
		foe = nullptr;
	if (is_friendly(foe))
		foe = nullptr;
	if (leader && leader->dead)
		leader = nullptr;
	if (owner && owner->dead)
	{
		//owner = nullptr;
		// A living who had an owner who is now dead, dies as well
		dead = 1;
		death();
		return 0;
	}

	if (lifetime)
	{
		if (!owner || owner->dead) // our owner gone?
		{
			dead = 1;
			death();
			return 0;
		}
		if (--lifetime < 1)
		{
			dead = 1;
			return death();
		}
		// Do other things based on our type ..
		switch (family)
		{
			case FAMILY_FIREELEMENTAL: // we take a toll from our mage ..
				if (stats_->hitpoints < stats_->max_hitpoints) // we're hurt
				{
					// Take a 'toll' of one health and 3 mp of mage, if there
					Sint32 temp = 0;
					if (owner->stats()->hitpoints >= (owner->stats()->max_hitpoints/3) )
					{
						temp = 1;
						owner->stats()->hitpoints--;
					}
					if (temp && (owner->stats()->magicpoints >= 3) )
					{
						temp += 1;
						owner->stats()->magicpoints -= 3;
					}
					if (temp == 2) // had both MP and HP, so heal 1 unit
						stats_->hitpoints++;
					else // else go down one more unit of lifetime
						lifetime--;
				} // end of hurt elemental
				break;  // end of elemental drain
			default:
				break;
		} // end of special stuff for summoned guys
	}  // end of summoned monster stuff


	collide_ob = nullptr; // always start with no collison..

	/*
	  if (ignore)
	  {
	         Log("ignoring living\n");
	         return 0;
	  }
	*/

	// Regenerate magic
	{
		bool frozen = myscreen->enemy_freeze || bonus_rounds;
		RegenTickResult mp = compute_regen_tick(stats_->magicpoints, stats_->max_magicpoints,
		                                        stats_->magic_per_round,
		                                        stats_->current_magic_delay, stats_->max_magic_delay,
		                                        frozen);
		stats_->magicpoints = mp.new_value;
		stats_->current_magic_delay = mp.new_delay;
	}

	// Regenerate hitpoints
	{
		bool frozen = myscreen->enemy_freeze || bonus_rounds;
		HpRegenResult hp = compute_hp_regen_tick(stats_->hitpoints, stats_->max_hitpoints,
		                                          stats_->heal_per_round,
		                                          stats_->current_heal_delay, stats_->max_heal_delay,
		                                          regen_delay_, frozen);
		stats_->hitpoints = hp.new_hp;
		stats_->current_heal_delay = hp.new_heal_delay;
		regen_delay_ = hp.new_regen_delay;
	}

	// Special-viewing
	if (view_all > 0)
		view_all--;

	// Invulnerability
	if (invulnerable_left > 0)
		invulnerable_left--;

	// Invisibility
	if (invisibility_left > 0)
		invisibility_left--;
	else
		outline = 0;

	// Flight
	if (flight_left > 0)
		flight_left--;
	if (!myscreen->query_grid_passable(xpos, ypos, this) && !flight_left)
	{
		flight_left++;
		stats_->hitpoints--;
		if(active_config().is_on("effects", "damage_numbers"))
            damage_numbers.push_back(DamageNumber(xpos + sizex/2, ypos, 1, RED));
		
		if (stats_->hitpoints <= 0)
		{
			dead = 1;
			death();
		}
	}

	// Charmed-ness
	if (charm_left_ > 1)
		charm_left_--;
	else
	{
		charm_left_ = 0;
		if (real_team_num != 255)
		{
			team_num = real_team_num;
			real_team_num = 255;
		}
	}

	if ( stats_->query_bit_flags(BIT_FORESTWALK) &&
	        (
	            myscreen->level_data.mysmoother.query_genre_x_y( xpos/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	            || myscreen->level_data.mysmoother.query_genre_x_y( (xpos+sizex)/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	            || myscreen->level_data.mysmoother.query_genre_x_y( (xpos+sizex)/GRID_SIZE, (ypos+sizey)/GRID_SIZE) == TYPE_TREES
	            || myscreen->level_data.mysmoother.query_genre_x_y( xpos/GRID_SIZE, (ypos+sizey)/GRID_SIZE) == TYPE_TREES
	        )
	   )
	{
		// charge us a point of magic ..
		if (stats_->magicpoints > 0.0f && stats_->current_magic_delay == 0)
        {
            stats_->magicpoints--;
        }

		float temp;
		if (myguy)
			temp = (4 - myguy->dexterity/10.0f);
		else
			temp = (4.0f - static_cast<float>(stats_->level)/2.0f);
		if (temp < 0)
			temp = 0;
		stepsize -= temp;
		if (stepsize < 1)
			stepsize = 1;
	}  // end of forestwalk check
	else
		stepsize = normal_stepsize;

	// Speed bonus
	if (speed_bonus_left > 1)
	{
		speed_bonus_left--;
		stepsize += speed_bonus;
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

	// Special things for various different living types
	switch (family)
	{
		case FAMILY_ARCHMAGE:  // gets bonus viewing, at times
		    {
		        Sint32 temp;
                if (stats_->level >= 40)
                    temp = 1;
                else
                    temp = 40 - stats_->level;
                if (!(drawcycle%temp)) // then we get to see..
                    view_all += 1;
		    }
			break;
		default:
			break;
	}  // end of special family auto-powers

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

	// Turn if you want to (...turn, around the world...)
	if (curdir != enddir && query_order() == Order::Living)
		return turn(enddir);


	// Are we performing some action?
	if (stats_->has_commands())
	{
		Sint32 temp = stats_->do_command();
		if (temp)
			return 1;
	}

	if (skip_exit > 0)
		skip_exit--;

	// Do we have a generic action-type set?
	if (action  && (user == -1) )
	{
		Sint32 temp = do_action();
		if (temp)
			return temp;
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
				Log("LIVING Generator?\n");
				//              act_generate();
				break;
			}
			// We are a weapon
		case ACT_FIRE:
			{
				Log("Living think's it's a weapon (act_fire)\n");
				//              act_fire();
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
				if (!rng(5) ) //1 in 5 to do our special
				{
					// Should we do our special? Are we full of magic?
					if (stats_->magicpoints >= stats_->special_cost[1])
					{
						current_special = static_cast<char>(rng((stats_->level+2)/3) + 1);
						if ( (current_special > 4) ||
						        (myscreen->special_name[static_cast<int>(family)][static_cast<int>(current_special)] == "NONE")
						   )
							current_special = 1;
						if (check_special() )
							return special();
					}
					else       // do random walking ..
					{
						act_random();
						return 1;
					}
				}
				else if (!rng(5) ) //1 in 5 to do act_random() function
					act_random();
				else // 4 of 5 times
				{
					if (!foe)
					{
						foe = myscreen->find_near_foe(this);
					}
					if (foe) // && rng(2) )
					{
						curdir = enddir = static_cast<char>((enddir/2) * 2);
						//stats_->try_command(COMMAND_SEARCH, 40, 0, 0);
						stats_->try_command(COMMAND_SEARCH, 300, 0, 0);
					}
					//else if (foe)
					//  stats_->try_command(COMMAND_RIGHT_WALK,40,0,0);
					else if (!rng(2))
						foe = myscreen->find_far_foe(this);
					else
						stats_->try_command(COMMAND_RANDOM_WALK,20);

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

short living::shove(walker  *target, short x, short y)
{
	//return 0; //debug memory

	if (target && !target->dead && (query_order()==Order::Living) &&  //we are alive
	        (is_friendly(target)) // we are allied
	   )
		// Make sure WE don't get shoved
		if (rng(3) && target->query_act_type() != ACT_CONTROL)
		{
			// We have to prevent a build-up of shoves which is
			//   caused by a blocked target.  We do so for now by clearing
			//   all commands
			target->stats()->clear_command();
			if (target->query_family()==FAMILY_CLERIC)
			{
				target->current_special = 1; // healing
				target->special();
			}
			target->stats()->set_command(COMMAND_WALK,4,x ,y );
			return 1;
		}
	return 0;
}

bool living::walk(float x, float y)
{
	short dir;
	//  short newdir, newcurdir;
	//  short distance; // distance between current and desired facings

	// Repeat last walk.
	//  lastx = x;
	//  lasty = y;

	dir = facing(x, y);

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
		// Normally we would check if the object at this grid point
		//    is passable (I cheated for now)
		// FIXME: These additional checks are a hack for the corner clipping bug (you could get into trees, etc.)
		if (myscreen->query_passable(xpos+x, ypos+y,this) && myscreen->query_passable(xpos+ceilf(x), ypos+ceilf(y),this) && myscreen->query_passable(xpos+floorf(x), ypos+floorf(y),this))
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
			if (collide_ob && !collide_ob->dead)
			{
				if (collide_ob->query_order() == Order::Living && is_friendly(collide_ob) )
				{
					shove(collide_ob, x, y);
				}
			}  // end hit some object
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
	else // Just changing direction
	{
		enddir = static_cast<char>(dir);

		// Technically, control gets and EXTRA call to TURN
		//   because first we call WALK, then ACT, whereas
		//   other walkers call ACT.  This would cause control
		//   to turn TWICE on the first call to walk, which is bad.
		//   So we stop that behavior here.
		if (this->query_act_type() != ACT_CONTROL || stats_->has_commands())
			turn(enddir);
	}
	return 1;
}

bool walkerIsAutoAttackable(walker* ob)
{
    return (ob->query_order() == Order::Living
             || ob->query_family() == FAMILY_TENT
             || ob->query_family() == FAMILY_TOWER
             || ob->query_family() == FAMILY_TOWER1
             || ob->query_family() == FAMILY_TREEHOUSE
             || ob->query_family() == FAMILY_BONES
             || ob->query_family() == FAMILY_GLOW
             || ob->query_family() == FAMILY_TREE
             || ob->query_family() == FAMILY_DOOR);
}

bool living::collide(walker  *ob)
{
	collide_ob = ob;
	//return 1; // debug
	if ( ob && walkerIsAutoAttackable(ob) && (is_friendly(ob) == 0)
	        && !ob->dead && !dead)
		init_fire();
	return 1;
}

walker* living::do_summon(char whatfamily, Sint32 summon_lifetime)
{
	walker  *newob;

	newob = myscreen->level_data.add_ob(Order::Living, whatfamily);
	newob->owner = this;
		newob->lifetime = summon_lifetime;
	newob->transform_to(Order::Living, whatfamily);
	//  Log("\n\nSummoned %d, life %d\n", whatfamily, lifetime);

	return newob;
}

// Returns true or false on whether it's good to do
// the special or not ..
bool living::check_special()
{
	shifter_down = static_cast<short>(rng(2)); // on or off, randomly ..

	// Make sure we have enough ..
	if (stats_->magicpoints < stats_->special_cost[static_cast<int>(current_special)])
		current_special = 1; // make us do default ..

	auto* fd = get_family_descriptor(family);
	if (fd && fd->check_special_ai)
		return fd->check_special_ai(this);

	// Default: always allow
	return true;
}

void living::set_difficulty(Uint32 whatlevel)
{
	//  Sint32 calcdelay,calcrate;  // apparently not used anymore
	Uint32 dif1 = difficulty_level[current_difficulty];
	const float levmult = static_cast<float>(whatlevel) * static_cast<float>(whatlevel);
	const float level_f = static_cast<float>(whatlevel);

	auto* fd = get_family_descriptor(family);
	if (fd && fd->set_difficulty)
	{
		fd->set_difficulty(this, whatlevel);
	}
	else
	{
		// Default formula
		stats_->max_hitpoints   += 11.0f * levmult;
		stats_->max_magicpoints += 11.0f * levmult;
		damage += 4.0f * level_f;
		stats_->armor += 2.0f * levmult;
	}

	// Adjust for difficulty settings now...
	if (team_num != 0)  // do all EXCEPT player characters
	{
		const float dif = static_cast<float>(dif1);
		stats_->max_hitpoints = (stats_->max_hitpoints * dif) / 100.0f;
		stats_->max_magicpoints = (stats_->max_magicpoints * dif) / 100.0f;
		damage = (damage * dif) / 100.0f;
	}

	stats_->hitpoints = stats_->max_hitpoints;
	stats_->magicpoints = stats_->max_magicpoints;

		stats_->max_heal_delay = REGEN; //defined in graph.h
		stats_->current_heal_delay =
		    static_cast<Sint32>(levmult * 4.0f); //for purposes of calculation only

	while (stats_->current_heal_delay > REGEN)
	{
		stats_->current_heal_delay -= REGEN;
		stats_->heal_per_round++;
	} // this takes care of the integer part, now calculate the fraction

	if (stats_->current_heal_delay > 1)
	{
		stats_->max_heal_delay /=
		    static_cast<Sint32>(stats_->current_heal_delay + 1);
	}
	stats_->current_heal_delay = 0; //start off without healing

	//make sure we have at least a 2 wait, otherwise we should have
	//calculated our heal_per_round as one higher, and the math must
	//have been screwed up some how
	if (stats_->max_heal_delay < 2)
		stats_->max_heal_delay = 2;



	// Set the magic delay ..
	stats_->max_magic_delay = REGEN;
	stats_->current_magic_delay = static_cast<Sint32>(levmult*30);//for calculation only

	while (stats_->current_magic_delay > REGEN)
	{
		stats_->current_magic_delay -= REGEN;
		stats_->magic_per_round++;
	} // this takes care of the integer part, now calculate the fraction

	if (stats_->current_magic_delay > 1)
	{
		stats_->max_magic_delay /=
		    static_cast<Sint32>(stats_->current_magic_delay + 1);
	}
	stats_->current_magic_delay = 0; //start off without magic regen

	//make sure we have at least a 2 wait, otherwise we should have
	//calculated our magic_per_round as one higher, and the math must
	//have been screwed up some how
	if (stats_->max_magic_delay < 2)
		stats_->max_magic_delay = 2;

}

short living::facing(short x, short y)
{
	Sint32 bigy = static_cast<Sint32>(y*1000);
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

bool living::act_random()
{
	//  short newx, newy; // apparently not used anymore
	short xdist, ydist;

	// Find our foe
	if (!rng(80) || (!foe))
		foe = myscreen->find_near_foe(this);
	if (!foe)
		return stats_->try_command(COMMAND_RANDOM_WALK,40);

	xdist = static_cast<short>(foe->xpos - xpos);
	ydist = static_cast<short>(foe->ypos - ypos);

	// If foe is in firing range, turn and fire
	if (abs(xdist) < lineofsight*GRID_SIZE &&
	        abs(ydist) < lineofsight*GRID_SIZE)
	{
		if (fire_check(xdist, ydist))
		{
			init_fire(xdist, ydist);
			stats_->set_command(COMMAND_FIRE, static_cast<short>(rng(24)), xdist, ydist);
			return 1;
		}
		else
			// Nearest foe is blocked
			turn(facing(xdist,ydist));
	}

	stats_->try_command(COMMAND_SEARCH,200,0,0);
	//stats_->try_command(COMMAND_RIGHT_WALK,50, 0, 0);
	return 1;

}

bool living::do_action()
{

	if (!action)
		return 0;

	switch (action)
	{
		case ACTION_FOLLOW: // follow our leader, attack his targets ..
			if (foe)
				return 0;       // continue as normal
			leader = myscreen->find_nearest_player(this);
			if (!leader)
				return 0;       // continue as normal ... shouldn't happen
			if (leader->foe)
			{
				foe = leader->foe;
				return 0;       // continue from this point ..
			}
			// Else follow our leader
			stats_->force_command(COMMAND_FOLLOW, 5, 0, 0);
			return 1;
		default:
			return 0;
	}
}
