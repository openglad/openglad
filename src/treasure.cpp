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
// treasure; a derived class of walker
//

//#include "graph.h"
#include <cmath>
#include <string>
#include "obmap.h"
#include "gloader.h"
#include "screen.h"
#include "treasure.h"
#include "text.h"
#include "stats.h"
#include "guy.h"
#include <algorithm>
#include <format>
#include <cstring>

// Zardus: this is the func to get events
void get_input_events(bool);

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);

treasure::treasure(const PixieData& data)
    : walker(data)
{
	ignore =static_cast<char>(0);
	dead =  static_cast<char>(0);
}

treasure::~treasure()
{
	//bufffers: PORT: cannot call destructor w/o obj: walker::~walker();
}

short treasure::act()
{
	// Abort all later code for now ..
	return 1;
}

short treasure::eat_me(walker  * eater)
{
	short guys_here;
	
	std::string message;
	Sint32 distance;
	walker  *target, *flash;
	static std::string exitname;
	Sint32 leftside, rightside;

	switch (family)
	{
		case FAMILY_DRUMSTICK:
			if (eater->stats->hitpoints >= eater->stats->max_hitpoints)
				return 1;
			else
			{
			    short amount = 10*stats->level + random(10*stats->level);
				eater->stats->hitpoints += amount;
				if (eater->stats->hitpoints > eater->stats->max_hitpoints)
					eater->stats->hitpoints = eater->stats->max_hitpoints;
                
                do_heal_effects(nullptr, eater, amount);
                
				dead = 1;
				if (on_screen())
					myscreen->soundp->play_sound(SOUND_EAT);
				return 1;
			}
		case FAMILY_GOLD_BAR:
			if (eater->team_num == 0 || eater->myguy)
			{
				myscreen->save_data.m_score[eater->team_num] += (200*stats->level);
				dead = 1;
				if (on_screen())
					myscreen->soundp->play_sound(SOUND_MONEY);
			}
			return 1;
		case FAMILY_SILVER_BAR:
			if (eater->team_num == 0 || eater->myguy)
			{
				myscreen->save_data.m_score[eater->team_num] += (50*stats->level);
				dead = 1;
				if (on_screen())
					myscreen->soundp->play_sound(SOUND_MONEY);
			}
			return 1;
		case FAMILY_FLIGHT_POTION:
			if (!eater->stats->query_bit_flags(BIT_FLYING) )
			{
				eater->flight_left += (150*stats->level);
				if (eater->user != -1)
				{
					message = std::format("Potion of Flight({})!", stats->level);
					myscreen->do_notify(message.c_str(), eater);
				}
				dead = 1;
			}
			return 1;
		case FAMILY_MAGIC_POTION:
			if (eater->stats->magicpoints < eater->stats->max_magicpoints)
				eater->stats->magicpoints = eater->stats->max_magicpoints;
			eater->stats->magicpoints += (50*stats->level);
			dead = 1;
			if (eater->user != -1)
			{
				message = std::format("Potion of Mana({})!", stats->level);
				myscreen->do_notify(message.c_str(), eater);
			}
			return 1;
		case FAMILY_INVULNERABLE_POTION:
			if (!eater->stats->query_bit_flags(BIT_INVINCIBLE) )
			{
				eater->invulnerable_left += (150*stats->level);
				dead = 1;
				if (eater->user != -1)
				{
					message = std::format("Potion of Invulnerability({})!", stats->level);
					myscreen->do_notify(message.c_str(), eater);
				}
			}
			return 1;
		case FAMILY_INVIS_POTION:
			eater->invisibility_left += (150*stats->level);
			if (eater->user != -1)
			{
				message = std::format("Potion of Invisibility({})!", stats->level);
				myscreen->do_notify(message.c_str(), eater);
			}
			dead = 1;
			return 1;
		case FAMILY_SPEED_POTION:
			eater->speed_bonus_left += 50*stats->level;
			eater->speed_bonus = stats->level;
			if (eater->user != -1)
			{
				message = std::format("Potion of Speed({})!", stats->level);
				myscreen->do_notify(message.c_str(), eater);
			}
			dead = 1;
			return 1;
		case FAMILY_EXIT: // go to another level, possibly
			if (eater->in_act) return 1;
			if (eater->query_act_type()!= ACT_CONTROL || (eater->skip_exit > 1))
				return 1;
			eater->skip_exit = 10;
			// See if there are any enemies left ...
			if (myscreen->level_done == 0)
				guys_here = 1;
			else
				guys_here = 0;
			// Get the name of our exit..
			message = std::format("scen{}", stats->level);
			exitname = myscreen->get_scen_title(message.c_str(), myscreen);

			//buffers: PORT: using std::string comparison instead of stricmp
			if (exitname == "none")
			{
				exitname = std::format("Level {}", stats->level);
			}

			leftside  = 160 - ( (static_cast<int>(exitname.size()) + 18) * 3);
			rightside = 160 + ( (static_cast<int>(exitname.size()) + 18) * 3);
			// First check to see if we're withdrawing into
			//    somewhere we've been, in which case we abort
			//    this level, and set our current level to
			//    that pointed to by the exit ...
			if ( myscreen->save_data.is_level_completed(stats->level)
			        && !myscreen->save_data.is_level_completed(myscreen->save_data.scen_num)
			        && (guys_here != 0)
			   ) // okay to leave
			{
				leftside -= 12;
				rightside += 12;
                
                std::string buf = std::format("Withdraw to {}?", exitname);
                bool result = yes_or_no_prompt("Exit Field", buf.c_str(), false);
				// Redraw screen ..
				myscreen->redrawme = 1;

				if (result) // accepted level change
				{
					clear_keyboard();
					// Delete all of our current information and abort ..
					for(auto& uptr : myscreen->level_data.oblist)
					{
					    walker* w = uptr.get();
						if (w && w->query_order() == ORDER_LIVING)
						{
							w->dead = 1;
							myscreen->level_data.myobmap->remove(w);
						}
					}
					
					// Now reload the autosave to revert our changes during battle (don't use SaveData::update_guys())
                    myscreen->save_data.load("save0");
                    
                    // Go to the exit's level
					myscreen->save_data.scen_num = stats->level;
					myscreen->end = 1;
					
                    // Autosave because we escaped to a new level
					// Save with the new current level
                    myscreen->save_data.save("save0");

					return myscreen->endgame(1, stats->level); // retreat
				}  // end of accepted withdraw to new level ..
				clear_keyboard();
			} // end of checking for withdrawal to completed level

			//buffers: also, allow exit if scenario_type == can exit
			if (!guys_here || (myscreen->level_data.type == SCEN_TYPE_CAN_EXIT)) // nobody evil left, so okay to exit level ..
			{
                std::string buf = std::format("Exit to {}?", exitname);
                bool result = yes_or_no_prompt("Exit Field", buf.c_str(), false);
				// Redraw screen ..
				myscreen->redrawme = 1;

				if(result) // accepted level change
				{
					clear_keyboard();
					return myscreen->endgame(0, stats->level);
				}
				clear_keyboard();
				return 1;
			}
			return 1;
		case FAMILY_TELEPORTER:
			if (eater->skip_exit > 1)
				return 1;
			distance = distance_to_ob_center(eater); // how  away?
			if (distance > 21)
				return 1;
			if (distance < 4 && eater->skip_exit)
			{
				//eater->skip_exit++;
				eater->skip_exit = 8;
				return 1;
			}
			// If we're close enough, teleport ..
			eater->skip_exit += 20;
			if (!leader)
				target = find_teleport_target();
			else
				target = leader;
			if (!target)
				return 1;
			leader = target;
			eater->center_on(target);
			if (!myscreen->query_passable(eater->xpos, eater->ypos, eater))
			{
				eater->center_on(this);
				return 1;
			}
			// Now do special effects
			flash = myscreen->level_data.add_ob(ORDER_FX, FAMILY_FLASH);
			flash->ani_type = ANI_EXPAND_8;
			flash->center_on(this);
			return 1;
		case FAMILY_LIFE_GEM: // get back some of lost man's xp ..
			if (eater->team_num != team_num) // only our team can get these
				return 1;
			myscreen->save_data.m_score[eater->team_num] += stats->hitpoints;
			flash = myscreen->level_data.add_ob(ORDER_FX, FAMILY_FLASH);
			flash->ani_type = ANI_EXPAND_8;
			flash->center_on(this);
			dead = 1;
			death();
			return 1;
		case FAMILY_KEY: // get the key to this door ..
			if (!(eater->keys & static_cast<Sint32>(pow(static_cast<double>(2), stats->level)) )) // just got it?
			{
				eater->keys |= static_cast<Sint32>(pow(static_cast<double>(2), stats->level)); // ie, 2, 4, 8, 16...
				if (eater->myguy)
					message = std::format("{} picks up key {}", eater->myguy->name,
					        stats->level);
				else
					message = std::format("{} picks up key {}", eater->stats->name, stats->level);
				if (eater->team_num == 0) // only show players picking up keys
				{
					myscreen->do_notify(message.c_str(), eater);
					if (eater->on_screen())
						myscreen->soundp->play_sound(SOUND_MONEY);
				}
			}
			return 1;
		default:
			return 1;
	} // end of treasure-check
}

void treasure::set_direct_frame(short whatframe)
{
	frame = whatframe;

	const PixieData& data = myscreen->level_data.myloader->graphics[PIX(order, family)];
	bmp = data.data.get() + frame*size;

}

// Finds the next connected teleporter in the fxlist for you to warp to.
walker  * treasure::find_teleport_target()
{
	auto& ls = myscreen->level_data.fxlist;
	//Log("Teleporting from #%d ..", number);

	// First find where we are in the list ...
    auto pred = [this](const std::unique_ptr<walker>& p) { return p.get() == this; };
    auto mine = std::find_if(ls.begin(), ls.end(), pred);
    if(mine == ls.end())
        return nullptr;

	// Now search the rest of the list ..
	auto e = mine;
	e++;
	for(; e != ls.end(); e++)
	{
	    walker* w = e->get();
		if (w && !w->dead)
		{
			if (w->query_order() == ORDER_TREASURE &&
			        w->query_family() == FAMILY_TELEPORTER &&
			        w->stats->level == stats->level)
			{
				//Log(" to target %d\n", number);
				return w;
			}
		}
	}

	// Hit the end of the list, look from top down now ..
	for(e = ls.begin(); e != mine; e++)
	{
	    walker* w = e->get();
		if (w && !w->dead)
		{
			if (w->query_order() == ORDER_TREASURE &&
			        w->query_family() == FAMILY_TELEPORTER &&
			        w->stats->level == stats->level)
			{
				//Log(" to looped target %d\n", number);
				return w;
			}
		}
	}

	return nullptr;
}

