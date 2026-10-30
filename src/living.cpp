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

#include "graph.h"
#include "smooth.h"
#include <cstring>
#include "bot_gates.h"

// From picker
extern Sint32 difficulty_level[DIFFICULTY_SETTINGS];
extern Sint32 current_difficulty;

living::living(const PixieData& data)
    : walker(data)
{
	current_special = 1;
	lifetime = 0;
}

living::~living()
{}

short living::act()
{
	if (bonus_rounds>0 && !dead)  // we get extra rounds to act this cycle
	{
		bonus_rounds--;
		act();
	}
	if (dead)
		return 0;

	// Make sure everyone we're pointing to is valid
	if (foe && (foe->dead || (random(foe->invisibility_left/20) > 0) ) )
		foe = NULL;
	if (is_friendly(foe))
		foe = NULL;
	if (leader && leader->dead)
		leader = NULL;
	if (owner && owner->dead)
	{
		//owner = NULL;
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
				if (stats->hitpoints < stats->max_hitpoints) // we're hurt
				{
					// Take a 'toll' of one health and 3 mp of mage, if there
					Sint32 temp = 0;
					if (owner->stats->hitpoints >= (owner->stats->max_hitpoints/3) )
					{
						temp = 1;
						owner->stats->hitpoints--;
					}
					if (temp && (owner->stats->magicpoints >= 3) )
					{
						temp += 1;
						owner->stats->magicpoints -= 3;
					}
					if (temp == 2) // had both MP and HP, so heal 1 unit
						stats->hitpoints++;
					else // else go down one more unit of lifetime
						lifetime--;
				} // end of hurt elemental
				break;  // end of elemental drain
			default:
				break;
		} // end of special stuff for summoned guys
	}  // end of summoned monster stuff


	collide_ob = NULL; // always start with no collison..

	/*
	  if (ignore)
	  {
	         Log("ignoring living\n");
	         return 0;
	  }
	*/

	// Regenerate magic
	if (stats->magicpoints < stats->max_magicpoints &&
	        !(myscreen->enemy_freeze || bonus_rounds) )
	{
		stats->magicpoints += stats->magic_per_round;
		stats->current_magic_delay++;
		if (stats->current_magic_delay >= stats->max_magic_delay)
		{
			stats->magicpoints++;
			stats->current_magic_delay = 0;
		}
		if (stats->magicpoints > stats->max_magicpoints)
			stats->magicpoints = stats->max_magicpoints;
	} // end magic increment section

	//Regenerate hitpoints
	if(regen_delay > 0)  // Delay after being hit
        regen_delay--;
    else
    {
        if (stats->hitpoints < stats->max_hitpoints &&
                !(myscreen->enemy_freeze || bonus_rounds))
        {
            stats->hitpoints += stats->heal_per_round;
            stats->current_heal_delay++;
            if (stats->current_heal_delay >= stats->max_heal_delay)
            {
                stats->hitpoints++;
                stats->current_heal_delay = 0;
            }
            if (stats->hitpoints > stats->max_hitpoints)
                stats->hitpoints = stats->max_hitpoints;
        } // end hitpoint increment section
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
		stats->hitpoints--;
		if(cfg.is_on("effects", "damage_numbers"))
            damage_numbers.push_back(DamageNumber(xpos + sizex/2, ypos, 1, RED));
		
		if (stats->hitpoints <= 0)
		{
			dead = 1;
			death();
		}
	}

	drawcycle++;

	// Charmed-ness
	if (charm_left > 1)
		charm_left--;
	else
	{
		charm_left = 0;
		// Make sure we're back to our real team
		if (real_team_num != 255)
		{
			team_num = real_team_num;
			real_team_num = 255;
		}
	}

	if ( stats->query_bit_flags(BIT_FORESTWALK) &&
	        (
	            myscreen->level_data.mysmoother.query_genre_x_y( xpos/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	            || myscreen->level_data.mysmoother.query_genre_x_y( (xpos+sizex)/GRID_SIZE, ypos/GRID_SIZE) == TYPE_TREES
	            || myscreen->level_data.mysmoother.query_genre_x_y( (xpos+sizex)/GRID_SIZE, (ypos+sizey)/GRID_SIZE) == TYPE_TREES
	            || myscreen->level_data.mysmoother.query_genre_x_y( xpos/GRID_SIZE, (ypos+sizey)/GRID_SIZE) == TYPE_TREES
	        )
	   )
	{
		// charge us a point of magic ..
		if (stats->magicpoints > 0.0f && stats->current_magic_delay == 0)
        {
            stats->magicpoints--;
        }

        float temp;
		if (myguy)
			temp = (4 - myguy->dexterity/10.0f);
		else
			temp = (4 - stats->level/2.0f);
		if (temp < 0)
			temp = 0;
		stepsize -= temp;
		if (stepsize < 1)
			stepsize = 1;
	}  // end of forestwalk check
	else
		stepsize = normal_stepsize;

	// Speed bonus
	if (speed_bonus_left > 0) // port of the current game's 9b9689d1: 2002 said `> 1`, N ticks gave N-1 and the field stuck at 1
	{
		speed_bonus_left--;
		stepsize += speed_bonus;
	}
	// A special's rest between casts: port of the current game's 54f5890f
	if (special_rest_left > 0) special_rest_left--;
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
                if (stats->level >= 40)
                    temp = 1;
                else
                    temp = 40 - stats->level;
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
	if (stats->frozen_delay)
	{
		stats->frozen_delay--;
		return 1;
	}

	if (busy > 0)
		busy--; // This allows busy to be our FIRING delay.
	// Find new action

	// Turn if you want to (...turn, around the world...)
	if (curdir != enddir && query_order() == ORDER_LIVING)
		return turn(enddir);


	// Are we performing some action?
	if (stats->has_commands())
	{
		Sint32 temp = stats->do_command();
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
				if (!random(5) ) //1 in 5 to do our special
				{
					// Should we do our special? Are we full of magic?
					if (stats->magicpoints >= stats->special_cost[1])
					{
						int bound = (stats->level+2)/3;
						if (myscreen->gameplay_fixes)
						{
							// port of the current game's a6dc8b87: never roll past the last
							// named slot, so a roll is never folded into slot 1 for being
							// out of range (the roll still draws once)
							int top = 0;
							for (int s = 1; s < NUM_SPECIALS; ++s)
								if (strcmp(myscreen->special_name[(int)family][s], "NONE"))
									top = s;
							if (bound > top)
								bound = top;
						}
						current_special = (char) (random(bound) + 1);
						if ( (current_special > 4 && !myscreen->gameplay_fixes) || // port of the current game's a6dc8b87: the fold applies only with the setting off
						        (!strcmp(myscreen->special_name[(int)family][(int)current_special], "NONE"))
						   )
							current_special = 1;
						if (check_special() )
						{
							if (!myscreen->gameplay_fixes)
								return special();
							special();
							if (special_charged)
								return 1;
						}
						if (!myscreen->gameplay_fixes)
							return 0; // the classic act: the roll was the act, refused or not
						// Gameplay Fixes W (port of the current game's a202cb8a): a roll
						// the gate or the cast refused is not the bot's act; it takes
						// the act the coin would have given it.
					}
					else       // do random walking ..
					{
						act_random();
						return 1;
					}
				}
				if (!random(5) ) //1 in 5 to do act_random() function
					act_random();
				else // 4 of 5 times
				{
					if (!foe)
					{
						foe = myscreen->find_near_foe(this);
					}
					if (foe) // && random(2) )
					{
						curdir = enddir = (char) ((enddir/2) * 2);
						//stats->try_command(COMMAND_SEARCH, 40, 0, 0);
						stats->try_command(COMMAND_SEARCH, 300, 0, 0);
					}
					//else if (foe)
					//  stats->try_command(COMMAND_RIGHT_WALK,40,0,0);
					else if (!random(2))
						foe = myscreen->find_far_foe(this);
					else
						stats->try_command(COMMAND_RANDOM_WALK,20);

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

	if (target && !target->dead && (query_order()==ORDER_LIVING) &&  //we are alive
	        (is_friendly(target)) // we are allied
	   )
		// Make sure WE don't get shoved
		if (random(3) && target->query_act_type() != ACT_CONTROL)
		{
			// 2026-07-10 shove command-theft livelock fix, ported from the
			// current game's living::shove (c409e7c8). The old code cleared
			// the target's queue and injected COMMAND_WALK(x, y) even when
			// that walk was wall-blocked; a shover re-colliding every tick
			// then refilled the injected command forever, so the target
			// never re-ran its own COMMAND_SEARCH. Probe the injected walk's
			// baby step against the GRID only before stealing the queue: if
			// it is terrain-blocked, leave the target's queue alone. The
			// random(3) draw above stays unconditional.
			if (!myscreen->query_grid_passable(target->xpos + (float) x,
			                                   target->ypos + (float) y, target))
				return 0;
			// We have to prevent a build-up of shoves which is
			//   caused by a blocked target.  We do so for now by clearing
			//   all commands
			target->stats->clear_command();
			if (target->query_family()==FAMILY_CLERIC)
			{
				target->current_special = 1; // healing
				if (myscreen->gameplay_fixes) // port of the current game's e50189dd: a shove is an ally at the cleric's side, so HEAL, never the mace the last coin left down
					target->shifter_down = 0;
				target->special();
			}
			target->stats->set_command(COMMAND_WALK,4,x ,y );
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
				if (collide_ob->query_order() == ORDER_LIVING && is_friendly(collide_ob) )
				{
					shove(collide_ob, x, y);
				}
			}  // end hit some object
			if (stats->query_bit_flags(BIT_ANIMATE) )  // animate regardless..
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
		enddir = (char) dir;

		// Technically, control gets and EXTRA call to TURN
		//   because first we call WALK, then ACT, whereas
		//   other walkers call ACT.  This would cause control
		//   to turn TWICE on the first call to walk, which is bad.
		//   So we stop that behavior here.
		if (this->query_act_type() != ACT_CONTROL || stats->has_commands())
			turn(enddir);
	}
	return 1;
}

bool walkerIsAutoAttackable(walker* ob)
{
    if (ob->query_order() == ORDER_LIVING || ob->query_order() == ORDER_GENERATOR)
        return true;
    if (ob->query_order() == ORDER_WEAPON)
    {
        return ob->query_family() == FAMILY_GLOW
            || ob->query_family() == FAMILY_TREE
            || ob->query_family() == FAMILY_DOOR;
    }
    return false;
}

short living::collide(walker  *ob)
{
	collide_ob = ob;
	//return 1; // debug
	if ( ob && walkerIsAutoAttackable(ob) && (is_friendly(ob) == 0)
	        && !ob->dead && !dead)
		init_fire();
	return 1;
}

walker * living::do_summon(char whatfamily, unsigned short lifetime)
{
	walker  *newob;

	newob = myscreen->level_data.add_ob(ORDER_LIVING, whatfamily);
	newob->owner = this;
	newob->lifetime = lifetime;
	newob->transform_to(ORDER_LIVING, whatfamily);
	//  Log("\n\nSummoned %d, life %d\n", whatfamily, lifetime);

	return newob;
}

// Returns true or false on whether it's good to do
// the special or not ..
short living::check_special()
{
	Uint32 distance, myrange;
	short howmany;

	shifter_down = random(2); // on or off, randomly ..

	// Make sure we have enough ..
	if (stats->magicpoints < stats->special_cost[(int)current_special])
		current_special = 1; // make us do default ..

	// port of the current game's 54f5890f (lc.gate_with_rest): a bot does not pick a resting special.
	// Only FREEZE TIME (mage, 3), HOWL (orc, 1), SCARE (ghost, 1) and MIND CONTROL (archmage, 4) set the slot.
	if (special_rest_slot == current_special && special_rest_left > 0)
		return 0;

	switch (family)
	{
		case FAMILY_SOLDIER:   // Check for foe in range x
			if (myscreen->gameplay_fixes)
			{
				// port of the current game's 1a632de3: the classic window below is
				// still asked first (its find_near_foe draws), then each slot asks
				// for its own payoff. CHARGE wants the window and a living foe in the
				// path its rush runs along the heading, the window's foe or any other
				// (port of 31752d32; a rush along no heading walks nowhere); BOOMERANG a living foe
				// inside the blade's widest orbit and no blade out; WHIRLWIND two
				// bodies inside its ring (a generator counts); DISARM a living foe
				// blocking the soldier's front, no higher than the soldier's level.
				short classic = 0;
				if (!foe)
					foe = myscreen->find_near_foe(this);
				if (foe)
				{
					distance = (Uint32) distance_to_ob(foe);
					classic = (distance < 75 && distance > 20);
				}
				if (current_special == 1)
					return (classic && bg_rush_strikes(this, 9) != NULL);
				if (current_special == 2)
					return (bg_owned(this, ORDER_FX, FAMILY_BOOMERANG, NULL) == 0
					        && bg_foes_within(this, 1, 17 + 6*stats->level));
				// WHIRLWIND and DISARM refuse a busy soldier: so do their gates
				// (port of the current game's 1ef4a313, Gameplay Fixes W)
				if (current_special == 3)
					return (busy <= 0 && bg_targets_within(this, 2, 32 + 2*stats->level));
				if (busy > 0)
					return 0;
				walker *blocker = bg_ahead_blocked_by_foe(this, 28);
				return (blocker && blocker->stats->level <= stats->level);
			}
			if (foe) // already have a foe ..
			{
				distance = (Uint32) distance_to_ob(foe);//Sint32 (deltax*deltax) + Sint32 (deltay*deltay);
				if (distance < 75 && distance > 20) // about 3 squares max, 1 square min
					return 1;
				return 0;
			}
			else // get a new foe ..
			{
				foe = myscreen->find_near_foe(this);
				if (!foe)
					return 0;
				distance = (Uint32) distance_to_ob(foe); // (deltax*deltax) + Sint32 (deltay*deltay);
				if (distance < 75 && distance > 20) // about 3 squares max, 1 min
					return 1;
				return 0;
			}
			//break; // end of fighter case
		case FAMILY_ARCHER:
			if (myscreen->gameplay_fixes)
			{
				// port of the current game's 58b81a6c: the classic gate below (a
				// foe within 130) was asked of every slot. It is still asked first
				// (it acquires the foe, and that draws); then each slot asks for its
				// own payoff, measured by the fire arrow's flight (step 8, range 12
				// in the weapon table) along the archer's heading. FIRE ARROWS' ring
				// pays for two foes in its flight, or one the archer does not face;
				// BARRAGE's three arrows fly along one line, so they want the
				// archer's foe alive, faced and in flight; EXPLODING BOLT wants the
				// same, and neither the archer nor an ally of its own in the blast.
				if (!foe)
					foe = myscreen->find_near_foe(this);
				if (current_special == 1)
					return bg_ring_pays(this, 8, 12, 0);
				// BARRAGE and EXPLODING BOLT refuse a busy archer (the casts'
				// guard) (port of the current game's 44a685f0, Gameplay Fixes W)
				if (busy > 0)
					return 0;
				if (!foe || foe->dead || foe->query_order() != ORDER_LIVING
				        || !bg_faces(this, foe))
					return 0;
				if (!bg_in_flight(this, foe, 8, 12, 0, 1, 0))
					return 0;
				if (current_special == 2)
					return 1;
				return bg_blast_clear(this, foe, bg_blast_reach(stats->level), true);
			}
			// fall through: with the setting off the archer shares the classic gate
		case FAMILY_FIREELEMENTAL:     // Check for foe in range x
		case FAMILY_GHOST:
		case FAMILY_ORC:
			if (family == FAMILY_GHOST && myscreen->gameplay_fixes)
			{
				// port of the current game's a3916aba: a bot's SCARE pays with a
				// living foe inside the scare radius (the classic gate asked for a
				// foe within 130, which the fright reaches only from level 8). The
				// classic foe search runs first either way, and SCARE is cast
				// unshifted. The radius is the current game's scare radius,
				// capped at 250 from level 21, as the orc's yell below is capped
				// at 420: a ported gate measures what the current game measures.
				if (!foe)
					foe = myscreen->find_near_foe(this);
				shifter_down = 0;
				Sint32 scare = 50 + 10*stats->level;
				if (scare > 250)
					scare = 250;
				return bg_foes_within(this, 1, scare);
			}
			if (family == FAMILY_FIREELEMENTAL && myscreen->gameplay_fixes)
			{
				// port of the current game's ab011fe8: STARBURST, a meteor along
				// each of the eight headings, pays where one meteor would not:
				// two living foes inside the ring's flight, or one the elemental
				// does not face. The classic foe search runs first either way.
				// The meteor is step 7, range 9 in the weapon table.
				if (!foe)
					foe = myscreen->find_near_foe(this);
				return bg_ring_pays(this, 7, 9, 0);
			}
			if (family == FAMILY_ORC && myscreen->gameplay_fixes)
			{
				// port of the current game's 4b39093d: the classic gate below (a
				// foe within 130) was the same for both specials. Its foe search
				// still runs first. HOWL wants a body the yell can stun (a living
				// foe or a generator: a frozen tent makes nothing) inside the
				// yell's own radius; EAT CORPSE wants the orc below 60 percent of
				// its health with a stain under it, whatever the foes are doing.
				short classic = 0;
				if (!foe)
					foe = myscreen->find_near_foe(this);
				if (foe)
					classic = (distance_to_ob(foe) < 130);
				if (current_special == 1)
				{
					Sint32 yell = 160 + 20*stats->level;
					if (yell > 420)
						yell = 420;
					// HOWL refuses a busy orc (port of the current game's 44a685f0, Gameplay Fixes W)
					return (busy <= 0 && bg_targets_within(this, 1, yell));
				}
				if (current_special == 2)
					return (bg_hp_below(this, 3, 5) && bg_corpse_underfoot(this, 24) != NULL);
				return classic;
			}
			if (foe) // already have a foe ..
			{
				distance = (Uint32) distance_to_ob(foe); //Sint32 (deltax*deltax) + Sint32 (deltay*deltay);
				if (distance < 130) // about 6 squares
					return 1;
				return 0;
			}
			else // get a new foe ..
			{
				foe = myscreen->find_near_foe(this);
				if (!foe)
					return 0;
				distance = (Uint32) distance_to_ob(foe); //Sint32 (deltax*deltax) + Sint32 (deltay*deltay);
				if (distance < 130) // about 6 squares
					return 1;
				return 0;
			}
			//break; // end of fighter case
		case FAMILY_THIEF:
			if (current_special == 1) // drop bomb
			{
				if (myscreen->gameplay_fixes)
				{
					// port of the current game's 324f7f74: a bomb pays with a living foe
					// inside its blast and no ally in it; the Shift is 0 whenever the
					// setting is on
					Sint32 reach = bg_blast_reach(stats->level);
					shifter_down = 0;
					return (bg_foes_within(this, 1, reach) && bg_blast_clear(this, this, reach, false));
				}
				if (foe) // already have a foe ..
				{
					distance = (Uint32) distance_to_ob(foe); // (deltax*deltax) + Sint32 (deltay*deltay);
					if (distance < 130 && distance > 35) // about 6 squares max, 2 min
						return 0;
				}
				else // get a new foe ..
				{
					myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                        110, &howmany, this);

					if (howmany < 3)
						return 0;
					return 1;
				}
				break; // end of thief case
			} // end of bomb
			else if (current_special == 3)
			{
				if (myscreen->gameplay_fixes)
				{
					// TAUNT and CHARM refuse a busy thief (the casts' guard)
					// (port of the current game's 44a685f0, Gameplay Fixes W)
					if (busy > 0)
						return 0;
					// port of the current game's 45c6e7ab: CHARM only with a foe the
					// thief can take inside charm reach, else TAUNT; the coin is drawn
					// either way
					std::list<walker*> foes = myscreen->find_foes_in_range(myscreen->level_data.oblist,
					                                        16 + 4*stats->level, &howmany, this);
					if (nearest_charmable(foes, stats->level))
						shifter_down = 1;
					else
						shifter_down = 0;
					// port of the current game's 324f7f74: TAUNT wants a living foe in
					// its reach and an ally there too (foes pulled onto a thief nobody
					// backs is the thief's own danger); a tent cannot follow the thief
					if (!shifter_down)
						return (bg_foes_within(this, 1, 80 + 4*stats->level)
						        && bg_allies_within(this, 1, 80 + 4*stats->level));
				}
				if (!shifter_down) // taunt
					myrange = 80 + 4*stats->level;
				else               // charm
					myrange = 16 + 4*stats->level;

				myscreen->find_foes_in_range(myscreen->level_data.oblist,
				                                        myrange, &howmany, this);
				if (howmany < 1)
					return 0;
				else
					return 1;
			}
			else if (myscreen->gameplay_fixes && current_special == 2)
			{
				// port of the current game's 324f7f74: CLOAK wants a foe hunting the
				// thief and no cloak left
				return (invisibility_left == 0 && bg_hunted_by(this, 130) != NULL);
			}
			else if (myscreen->gameplay_fixes)
			{
				// port of the current game's 324f7f74: POISON CLOUD wants two living
				// foes at blows and no cloud of the thief's alive, and the thief not
				// busy (the cast's guard) (port of the current game's 44a685f0, Gameplay Fixes W)
				return (busy <= 0 && bg_owned(this, ORDER_FX, FAMILY_CLOUD, NULL) == 0
				        && bg_foes_at_blows(this).size() >= 2);
			}
			else
				return 1;  // default is go for it
		case FAMILY_MAGE:  // TP if  away from guys ..
			howmany = 0;
			myscreen->find_foes_in_range(myscreen->level_data.oblist,
			                                        110, &howmany, this);

			if (myscreen->gameplay_fixes)
			{
				// port of the current game's e97c8829: the classic rule below was
				// written for TELEPORT and applied to every slot. Slot 1 keeps it;
				// FREEZE TIME waits for the allies' fight (bg_allies_fight in
				// bot_gates.h, the current game's 95ae7e12); HEARTBURST wants one
				// inside its own reach.
				// port of the current game's 12f25f88: a bot's TELEPORT MARKER is its
				// own special, written over the coin: placed only where the mage will
				// come back (nobody within 110, no marker of its own standing, a hero
				// with 75 INT, and half the pool left after the price still paying
				// for WARP SPACE). WARP SPACE's ring is point-blank (each bolt moves
				// one pixel a tick), so it pays for two foes inside it or one the
				// mage does not face; ENERGY WAVE wants a foe within 110 that the
				// mage faces.
				if (current_special == 1)
				{
					// a busy mage places no marker, and one mid-teleport does not
					// teleport (the casts' guards) (port of the current game's 44a685f0, Gameplay Fixes W)
					if (howmany < 1 && bg_marker_pays(this, 2, 75) && busy <= 0)
						shifter_down = 1;
					else
						shifter_down = 0;
					return ((howmany < 1 || howmany > 3)
					        && !(ani_type == ANI_TELE_OUT || ani_type == ANI_TELE_IN));
				}
				if (current_special == 2)
				{
					// each bolt's range add, as the ring computes it from the spare pool
					Sint32 spare = (Sint32) (stats->magicpoints - stats->special_cost[2]);
					Sint32 add = spare / 15;
					if (add > 40)
						add = 40;
					return bg_ring_pays(this, BG_UNIT_STEP, 7, add / 3);
				}
				if (current_special == 3)
				{
					int engaged;
					bool hurt;
					bg_allies_fight(this, &engaged, &hurt); // port of the current game's 95ae7e12
					return (engaged >= 3 || hurt);
				}
				if (current_special == 4)
				{
					walker *target = bg_nearest_living_foe(this, 110);
					return (target && bg_faces(this, target));
				}
				// HEARTBURST: slot 5 and every other slot
				short in_reach = 0;
				myscreen->find_foes_in_range(myscreen->level_data.oblist,
				                                        80 + 2*stats->level, &in_reach, this);
				return (in_reach >= 1);
			}
			if (howmany < 1) //  away from anybody ..
				return 1;
			if (howmany > 3) // too many enemies!
				return 1;
			return 0;
			//break; // end of fighter case
		case FAMILY_SLIME:
			if (myscreen->level_data.numobs < MAXOBS)
				return 1;
			else
				return 0;
		case FAMILY_CLERIC: // any friends?
			if (current_special == 1) // healing
			{
				if (myscreen->gameplay_fixes)
				{
					// port of the current game's e50189dd: HEAL when an ally within 60
					// is below its full health; else the MYSTIC MACE when it pays: a
					// living foe within 48 (the mace orbits on a 24 px circle), no mace
					// of this cleric out, the pool at least half full (the classic
					// condition) and a hero's 50 INT. The Shift is written after the coin.
					if (bg_hurt_ally(this, 60))
					{
						shifter_down = 0; // we're HEALING
						return 1;
					}
					if (stats->magicpoints < (stats->max_magicpoints/2) )
						return 0;
					if (myguy && myguy->intelligence < 50)
						return 0;
					// and not busy: the cast refuses SPECIAL BUSY (port of the
					// current game's b746503d, Gameplay Fixes W)
					if (busy <= 0 && bg_owned(this, ORDER_FX, FAMILY_MAGIC_SHIELD, NULL) == 0
					        && bg_foes_within(this, 1, 48))
					{
						shifter_down = 1; // do mace
						return 1;
					}
					return 0;
				}
				myscreen->find_friends_in_range(myscreen->level_data.oblist,
				            60, &howmany, this);

				if (howmany > 0) // other than ourselves?
				{
					shifter_down = 0; // we're HEALING
					return 1;
				}
				else if (stats->magicpoints >= (stats->max_magicpoints/2) )
				{
					// Do mace ...
					shifter_down = 1;
					return 1;
				}
				else
					return 0;
			}  // end of healing/mace
			else
			{
				if (myscreen->gameplay_fixes && (current_special == 2 || current_special == 3))
				{
					// port of the current game's 08fad439: TURN UNDEAD only with
					// something to turn in reach, else the raise (which refuses free
					// when there is no corpse)
					// and only with the INT to cast it (port of the current game's 83b7a37d)
					int reach = 4*stats->level;
					if (reach < 60)
						reach = 60;
					// and not busy (port of the current game's b746503d, Gameplay Fixes W)
					if (!(myguy && myguy->intelligence < 60) && busy <= 0 && undead_foes_in_range(reach) > 0)
						shifter_down = 1;
					else
						shifter_down = 0;
					// port of the current game's e50189dd: the raise wants the corpse it
					// would raise (60 for a skeleton, 30 for a ghost) and a living foe
					// within 130 for the risen to fight
					// and (port of the current game's d80a7faa, Gameplay Fixes T) the
					// corpse is one the cast can raise: its body fits near it
					if (!shifter_down)
					{
						walker *b = NULL;
						short bx = 0, by = 0;
						if (current_special == 3)
						{
							if (!raise_spot(30, FAMILY_GHOST, &b, &bx, &by))
								return 0;
						}
						else if (!raise_spot(60, FAMILY_SKELETON, &b, &bx, &by))
							return 0;
						return bg_foes_within(this, 1, 130);
					}
				}
				else if (myscreen->gameplay_fixes && current_special == 4)
				{
					// port of the current game's e50189dd: RESURRECT wants a friendly
					// corpse within 30 (a foe's stain is RAISE GHOST's job); the
					// corpse the cast would take (port of the current game's
					// d80a7faa, Gameplay Fixes T)
					walker *b = NULL;
					short bx = 0, by = 0;
					if (!raise_spot(30, -1, &b, &bx, &by))
						return 0;
					return (b && is_friendly(b));
				}
				return 1;
			}
			//break;
		case FAMILY_SKELETON:  // Tunnel if no foes near ..
			howmany = 0;
			myscreen->find_foes_in_range(myscreen->level_data.oblist,
			                                        5*GRID_SIZE, &howmany, this);

			if (howmany < 1) //  away from anybody ..
			{
				// with the setting on, not mid-teleport: TUNNEL's own guard
				// (port of the current game's 44a685f0, Gameplay Fixes W)
				if (myscreen->gameplay_fixes && (ani_type == ANI_TELE_OUT || ani_type == ANI_TELE_IN))
					return 0;
				return 1;      //  so tunnel
			}
			return 0;
			//break; // end of skeleton case
		case FAMILY_ARCHMAGE:
			// port of the current game's de915ccd: the Gameplay Fixes bot gates
			// for slots 1-3 (slot 4 keeps its rest gate, above). Off, the
			// archmage answers 1, as the default arm always did.
			if (!myscreen->gameplay_fixes)
				return 1;
			if (current_special == 1) // TELEPORT: the mage's rule
			{
				myscreen->find_foes_in_range(myscreen->level_data.oblist,
				                                        110, &howmany, this);
				// port of the current game's a5ee6f18: the TELEPORT MARKER (the
				// Shift arm) is no longer the coin's: with nobody near, mark
				// where the archmage stands when no marker of its own stands, a
				// hero has the cast's 75 INT, and half the pool left after the
				// price still covers HEARTBURST. Written after the coin.
				// A busy archmage places no marker, and one mid-teleport does
				// not teleport (the casts' guards) (port of the current game's 44a685f0, Gameplay Fixes W)
				shifter_down = (howmany < 1 && bg_marker_pays(this, 2, 75) && busy <= 0) ? 1 : 0;
				return ((howmany < 1 || howmany > 3)
				        && !(ani_type == ANI_TELE_OUT || ani_type == ANI_TELE_IN));
			}
			if (current_special == 2)
				return bg_archmage_burst_or_chain(this);
			if (current_special == 3)
			{
				// The cast's own guard: it refuses a busy archmage (port of the current game's 44a685f0, Gameplay Fixes W)
				if (busy > 0)
					return 0;
				// SUMMON ELEMENTAL only when the spare pool is at least the slot's
				// own price, and then a living foe within 200 is wanted; else the
				// image. Port of the current game's a5ee6f18: SUMMON IMAGE, a
				// phantom with one hit point, is a decoy: it pays before the blows
				// land, when at least two living foes within 200 are hunting the
				// archmage and none is within a blow of it yet (at blows HEARTBURST
				// is the cheaper answer), its health is above the flee threshold
				// (below it TELEPORT is the answer), no phantom of its own stands,
				// and the pool after the 500 still covers a HEARTBURST.
				Sint32 spare = (Sint32) (stats->magicpoints - stats->special_cost[3]);
				if (spare >= stats->special_cost[3])
				{
					shifter_down = 1;
					return bg_foes_within(this, 1, 200);
				}
				shifter_down = 0;
				if (bg_hp_below(this, 3, myguy ? 5 : 8))
					return 0;
				if (bg_owned_phantom(this))
					return 0;
				if (!bg_foes_at_blows(this).empty())
					return 0;
				if (spare < stats->special_cost[2])
					return 0;
				return (bg_hunters(this, 200).size() >= 2);
			}
			// port of the current game's a5ee6f18: MIND CONTROL wants a foe the
			// control can take (never charmed, not a hero a player drives, not
			// above the archmage's level) inside its reach; the rest above holds
			// the slot on both values
			{
				// The cast's own guard: it refuses a busy archmage (port of the current game's 44a685f0, Gameplay Fixes W)
				if (busy > 0)
					return 0;
				std::list<walker*> foes = bg_living_foes(this, 80 + 4*stats->level);
				return (nearest_charmable(foes, stats->level) != NULL);
			}
		case FAMILY_ELF:
			{
				// port of the current game's 849df031: the classic elf had no gate
				// and threw at nothing. With the Gameplay Fixes setting on a volley
				// wants the elf's nearest living foe faced (the rocks leave along
				// its heading) and inside the rocks' flight (the rock is step 5,
				// range 8 in the weapon table; the bouncing volleys multiply the
				// range by 3/2, 4/2 and 5/2), and the two bigger volleys want a
				// second living foe in flight anywhere around. Off, the elf answers
				// 1, as the default arm always did.
				if (!myscreen->gameplay_fixes)
					return 1;
				// per slot: the foes needed, the range multiplier in halves; slot 4
				// is also every other slot
				static const int volleys[5][2] = { { 2, 5 }, { 1, 2 }, { 1, 3 }, { 2, 4 }, { 2, 5 } };
				int v = (current_special >= 1 && current_special <= 4) ? current_special : 4;
				// port of the current game's 9953b30b (Gameplay Fixes V): and the
				// rock's pad is clear, or the cast refuses COULD NOT FIRE
				if (!bg_pad_clear(this))
					return 0;
				walker *target = bg_nearest_living_foe(this, 320);
				if (!target || !bg_faces(this, target)
				        || !bg_in_flight(this, target, 5, 8, volleys[v][1], 2, 0))
					return 0;
				if (volleys[v][0] < 2)
					return 1;
				std::list<walker*> foes = bg_living_foes(this, 320);
				int in_flight = 0;
				for (auto f = foes.begin(); f != foes.end(); ++f)
					if (bg_in_flight(this, *f, 5, 8, volleys[v][1], 2, 0))
						in_flight++;
				return (in_flight >= volleys[v][0]);
			}
		case FAMILY_DRUID:
			{
				// port of the current game's b05949b3: the classic druid had no gate
				// and cast every roll. With the Gameplay Fixes setting on GROW TREE
				// wants its nearest living foe within 60 faced and not yet at blows
				// (a tree between the druid and a foe closing on it); SUMMON FAERIE a
				// living foe within 130 and no faerie of its own out; REVEAL changes
				// only a viewscreen's radar, so a bot never casts it; PROTECTION an
				// ally within 60 with a living foe at blows with it and no circle on
				// it or one under 50 hp. Off, the druid answers 1, as the default arm
				// always did.
				if (!myscreen->gameplay_fixes)
					return 1;
				// All four casts refuse a busy druid (their own guard)
				// (port of the current game's 44a685f0, Gameplay Fixes W)
				if (busy > 0)
					return 0;
				if (current_special == 1)
				{
					walker *f = bg_nearest_living_foe(this, 60);
					return (f && bg_faces(this, f) && !bg_at_blows(this, f));
				}
				if (current_special == 2)
					return (bg_owned(this, ORDER_LIVING, FAMILY_FAERIE, NULL) == 0
					        && bg_foes_within(this, 1, 130));
				if (current_special == 3)
					return 0;
				walker *ally = bg_ally_at_blows(this, 60, NULL);
				if (!ally)
					return 0;
				walker *circle;
				bg_owned(ally, ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION, &circle);
				return (!circle || circle->stats->hitpoints < 50);
			}
		case FAMILY_BARBARIAN:
			{
				// port of the current game's b05c15d2: the classic barbarian had no
				// gate and threw at nothing. With the Gameplay Fixes setting on HURL
				// BOULDER wants its nearest living foe faced and inside the
				// boulder's flight (the boulder is made with add_ob, not fired: its
				// step is the thrower's, a bot's 2 a level, a hero's 1 + strength / 7,
				// held to 1..15, over the table's range of 9, and its first walk
				// turns it); EXPLODING BOULDER wants that foe with a second living
				// foe inside the blast around it, and neither the barbarian nor an
				// ally of its own in the blast. Off, the barbarian answers 1, as the
				// default arm always did.
				if (!myscreen->gameplay_fixes)
					return 1;
				// Both throws refuse a busy barbarian (the cast's guard)
				// (port of the current game's 44a685f0, Gameplay Fixes W)
				if (busy > 0)
					return 0;
				int step;
				if (myguy)
					step = 1 + myguy->strength / 7;
				else
					step = 2 * stats->level;
				if (step < 1)
					step = 1;
				if (step > 15)
					step = 15;
				int reach = (9 - 1) * step;
				walker *target = bg_nearest_living_foe(this, 2*reach + 64);
				if (!target || !bg_faces(this, target))
					return 0;
				int gap_x, gap_y;
				bg_gap(this, target, &gap_x, &gap_y);
				if ((gap_x > gap_y ? gap_x : gap_y) > reach)
					return 0;
				if (current_special != 2)
					return 1;
				Sint32 blast = bg_blast_reach(stats->level);
				std::list<walker*> foes = bg_living_foes(this, RING_SCAN);
				int near = 0;
				for (auto f = foes.begin(); f != foes.end(); ++f)
					if (*f == target || target->distance_to_ob(*f) <= blast)
						near++;
				return (near >= 2 && bg_blast_clear(this, target, blast, true));
			}
		default :
			return 1;
	}

	return 1; //unreachable?
}

void living::set_difficulty(Uint32 whatlevel)
{
	//  Sint32 calcdelay,calcrate;  // apparently not used anymore
	Uint32 dif1 = difficulty_level[current_difficulty];
	Uint32 levmult = (Uint32) whatlevel*(Uint32) whatlevel;

	switch (family)
	{
		case FAMILY_ARCHER:
			stats->max_hitpoints   += 11*levmult;
			stats->max_magicpoints += 12*levmult;
			damage += 4*whatlevel;
			stats->armor += levmult;
			break;
		case FAMILY_MAGE:
			stats->max_hitpoints   += 7*levmult;
			stats->max_magicpoints += 14*levmult;
			damage += 3*whatlevel;
			stats->armor += levmult/2.0f;
			break;
		case FAMILY_CLERIC:
		case FAMILY_DRUID:
			stats->max_hitpoints   += 9*levmult;
			stats->max_magicpoints += 12*levmult;
			damage += 4*whatlevel;
			stats->armor += levmult/2.0f;
			break;
		case FAMILY_SOLDIER:  // default as soldier
			stats->max_hitpoints   += 13*levmult;
			stats->max_magicpoints += 8*levmult;
			weapons_left = (short) ((whatlevel+1) / 2);
			damage += 5*whatlevel;
			stats->armor += 2*levmult;
			break;
		case FAMILY_ORC:
			stats->max_hitpoints   += 14*levmult;
			stats->max_magicpoints += 7*levmult;
			damage += 6*whatlevel;
			stats->armor += 3*levmult;
			break;
		case FAMILY_GOLEM:
			stats->max_hitpoints   += 18*levmult;
			stats->max_magicpoints += 5*levmult;
			damage += 7*whatlevel;
			stats->armor += 4*levmult;
			break;
		default:
			stats->max_hitpoints   += 11*levmult;
			stats->max_magicpoints += 11*levmult;
			damage += (short) 4*whatlevel;
			stats->armor += 2*levmult;
			break;
	}

	// Adjust for difficulty settings now...
	if (team_num != 0)  // do all EXCEPT player characters
	{
		stats->max_hitpoints = (stats->max_hitpoints*dif1) / 100.0f;
		stats->max_magicpoints = (stats->max_magicpoints*dif1) / 100.0f;
		damage = (damage * dif1) / 100.0f;
	}

	stats->hitpoints = stats->max_hitpoints;
	stats->magicpoints = stats->max_magicpoints;

	stats->max_heal_delay = REGEN; //defined in graph.h
	stats->current_heal_delay =
	    (levmult * 4); //for purposes of calculation only

	while (stats->current_heal_delay > REGEN)
	{
		stats->current_heal_delay -= REGEN;
		stats->heal_per_round++;
	} // this takes care of the integer part, now calculate the fraction

	if (stats->current_heal_delay > 1)
	{
		stats->max_heal_delay /=
		    (Sint32) (stats->current_heal_delay + 1);
	}
	stats->current_heal_delay = 0; //start off without healing

	//make sure we have at least a 2 wait, otherwise we should have
	//calculated our heal_per_round as one higher, and the math must
	//have been screwed up some how
	if (stats->max_heal_delay < 2)
		stats->max_heal_delay = 2;



	// Set the magic delay ..
	stats->max_magic_delay = REGEN;
	stats->current_magic_delay = (Sint32) (levmult*30);//for calculation only

	while (stats->current_magic_delay > REGEN)
	{
		stats->current_magic_delay -= REGEN;
		stats->magic_per_round++;
	} // this takes care of the integer part, now calculate the fraction

	if (stats->current_magic_delay > 1)
	{
		stats->max_magic_delay /=
		    (Sint32) (stats->current_magic_delay + 1);
	}
	stats->current_magic_delay = 0; //start off without magic regen

	//make sure we have at least a 2 wait, otherwise we should have
	//calculated our magic_per_round as one higher, and the math must
	//have been screwed up some how
	if (stats->max_magic_delay < 2)
		stats->max_magic_delay = 2;

}

short living::facing(short x, short y)
{
	Sint32 bigy = (Sint32) (y*1000);
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

short living::act_random()
{
	//  short newx, newy; // apparently not used anymore
	short xdist, ydist;

	// Find our foe
	if (!random(80) || (!foe))
		foe = myscreen->find_near_foe(this);
	if (!foe)
		return stats->try_command(COMMAND_RANDOM_WALK,40);

	xdist = (short) (foe->xpos - xpos);
	ydist = (short) (foe->ypos - ypos);

	// If foe is in firing range, turn and fire
	if (abs(xdist) < lineofsight*GRID_SIZE &&
	        abs(ydist) < lineofsight*GRID_SIZE)
	{
		if (fire_check(xdist, ydist))
		{
			init_fire(xdist, ydist);
			stats->set_command(COMMAND_FIRE, (short) random(24), xdist, ydist);
			return 1;
		}
		else
			// Nearest foe is blocked
			turn(facing(xdist,ydist));
	}

	stats->try_command(COMMAND_SEARCH,200,0,0);
	//stats->try_command(COMMAND_RIGHT_WALK,50, 0, 0);
	return 1;

}

short living::do_action()
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
			stats->force_command(COMMAND_FOLLOW, 5, 0, 0);
			return 1;
		default:
			return 0;
	}
}
