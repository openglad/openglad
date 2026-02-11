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

#include "graph.h"
#include "combat_math.h"
#include "game_context.h"
#include "test_trace.h"

#include <format>

namespace {
static inline Uint32 rng(Uint32 max_exclusive)
{
    return ctx().rng->next(max_exclusive);
}

static inline cfg_store& active_config()
{
    if(ctx().config != nullptr)
        return *ctx().config;
    return cfg;
}
} // namespace

short exp_from_action(ExpAction action, walker* w, walker* target, short value);

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
                                if(!active_config().is_on("effects", "heal_numbers"))
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

                    if(!active_config().is_on("effects", "heal_numbers"))
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
