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

#include <cmath>
#include <cstdint>
#include <openglad/core/combat_math.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <cstring>

// RNG now comes from current_game->world->rng_.
namespace
{
std::uint32_t query_difficulty_percent()
{
    if (current_game == nullptr || current_game->world == nullptr)
        return 100u;
    const int difficulty = current_game->world->difficulty;
    return (difficulty > 0) ? static_cast<std::uint32_t>(difficulty) : 100u;
}
} // namespace

living::living(const PixieData& data)
    : walker(data)
{
	set_current_special(1);
	set_lifetime(0);
}

living::living()
    : walker()
{
	set_current_special(1);
	set_lifetime(0);
}

// Zardus: PORT: this tries to delte its parent class (I think), and g++ doesn't
// seem to like chicken and egg problems: walker::~walker();
living::~living()
{}

bool living::act()
{
	// Cap bonus_rounds to prevent stack overflow from unbounded recursion.
	// The original code recursed for each bonus round, which could exhaust
	// the stack with large values (e.g., mage freeze-time on many allies).
	if (bonus_rounds() > 50)
		set_bonus_rounds(50);
	// we get extra rounds to act this cycle
	if (bonus_rounds() > 0 && !dead())
	{
		set_bonus_rounds(static_cast<short>(bonus_rounds() - 1));
		act();
	}
	if (dead())
		return 0;

	// Runtime headless/network sims advance drawcycle here so living periodic
	// effects are not dependent on a renderer-side draw path.
	set_drawcycle(static_cast<unsigned char>(drawcycle() + 1));

	// Z-axis: fall through air / take Z-stairs (multi-floor only; a cheap no-op
	// on single-floor levels). May cause a pit death.
	apply_z_motion();
	if (dead())
		return 0;

	// #160: exit-pad re-trigger latch upkeep — clears once we have fully
	// left the latched pad's rect. Runs before the animate/turn/frozen
	// early-returns so the latch can never wedge shut on a walker that is
	// mid-animation or frozen while stepping off the pad.
	update_exit_latch();

	// Make sure everyone we're pointing to is valid
	if (foe() && (foe()->dead() || (current_game->world->rng_.next(foe()->invisibility_left()/20) > 0) ) )
		set_foe(nullptr);
	if (is_friendly(foe()))
		set_foe(nullptr);
	if (leader() && leader()->dead())
		set_leader(nullptr);
	if (owner() && owner()->dead())
	{
		//owner = nullptr;
		// A living who had an owner who is now dead, dies as well
		set_dead(1);
		death();
		return 0;
	}

	if (lifetime())
	{
		if (!owner() || owner()->dead()) // our owner gone?
		{
			set_dead(1);
			death();
			return 0;
		}
		const auto remaining_lifetime = lifetime() - 1;
		set_lifetime(remaining_lifetime);
		if (remaining_lifetime < 1)
		{
			set_dead(1);
			return death();
		}
	}  // end of summoned monster stuff

	// Thaw-immunity climb (runaway-specials §3.3): the negative frozen_delay
	// phase — written only by the player-side drain in sim_input_handler —
	// recovers +1/tick toward 0 here, so a walker switched away mid-immunity
	// keeps climbing. Sits before the animate/frozen early-returns; the
	// frozen drains below skip the negative phase via the masked getter.
	if (stats_->frozen_delay_raw() < 0)
		stats_->tick_freeze_immunity();


	set_collide_ob(nullptr); // always start with no collison..

	/*
	  if (ignore)
	  {
	         Log("ignoring living\n");
	         return 0;
	  }
	*/

	// Regenerate magic
	{
		bool frozen = current_game->world->enemy_freeze || bonus_rounds();
		RegenTickResult mp = compute_regen_tick(stats_->magicpoints(), stats_->max_magicpoints(),
		                                        stats_->magic_per_round(),
		                                        stats_->current_magic_delay(), stats_->max_magic_delay(),
		                                        frozen);
		stats_->set_magicpoints(mp.new_value);
		stats_->set_current_magic_delay(mp.new_delay);
	}

	// Regenerate hitpoints
	{
		bool frozen = current_game->world->enemy_freeze || bonus_rounds();
		HpRegenResult hp = compute_hp_regen_tick(stats_->hitpoints(), stats_->max_hitpoints(),
		                                          stats_->heal_per_round(),
		                                          stats_->current_heal_delay(), stats_->max_heal_delay(),
		                                          regen_delay(), frozen);
		stats_->set_hitpoints(hp.new_hp);
		stats_->set_current_heal_delay(hp.new_heal_delay);
		set_regen_delay(hp.new_regen_delay);
	}

	// Special-viewing
	if (view_all() > 0)
		set_view_all(static_cast<short>(view_all() - 1));

	// Invulnerability
	if (invulnerable_left() > 0)
		set_invulnerable_left(static_cast<short>(invulnerable_left() - 1));

	// Invisibility
	if (invisibility_left() > 0)
		set_invisibility_left(static_cast<short>(invisibility_left() - 1));
	else
		set_outline(0);

	// Flight
	if (flight_left() > 0)
		set_flight_left(static_cast<short>(flight_left() - 1));
	if (!current_game->world->query_grid_passable(xpos(), ypos(), this) && !flight_left())
	{
		set_flight_left(static_cast<short>(flight_left() + 1));
		stats_->set_hitpoints(	stats_->hitpoints() - 1);
		damage_numbers.push_back(
            DamageNumber(
                xpos() + sizex()/2,
                ypos(),
                1,
                RED,
                current_game->world->tick_count_));
		
		if (stats_->hitpoints() <= 0)
		{
			set_dead(1);
			death();
		}
	}

	// Charmed-ness
	if (charm_left() > 1)
		set_charm_left(static_cast<short>(charm_left() - 1));
	else
	{
		set_charm_left(0);
		if (real_team_num() != 255)
		{
			set_team_num(real_team_num());
			set_real_team_num(255);
		}
	}

	if ( stats_->query_bit_flags(BIT_FORESTWALK) &&
	        (
	            current_game->world->mysmoother.query_genre_x_y(xpos() / GRID_SIZE, ypos() / GRID_SIZE) == TYPE_TREES
	            || current_game->world->mysmoother.query_genre_x_y((xpos() + sizex()) / GRID_SIZE, ypos() / GRID_SIZE) == TYPE_TREES
	            || current_game->world->mysmoother.query_genre_x_y((xpos() + sizex()) / GRID_SIZE, (ypos() + sizey()) / GRID_SIZE) == TYPE_TREES
	            || current_game->world->mysmoother.query_genre_x_y(xpos() / GRID_SIZE, (ypos() + sizey()) / GRID_SIZE) == TYPE_TREES
	            // Concealing decor (SHRUB): same four corner probes, on the
	            // walker's own floor (identical to floor 0 on single-floor
	            // levels). Gated on plane validity — no-decor levels take the
	            // legacy condition byte-identically, and no MIGRATED decor
	            // conceals, so stock behavior is unchanged.
	            || current_game->world->decor_conceals_at(floor(), xpos() / GRID_SIZE, ypos() / GRID_SIZE)
	            || current_game->world->decor_conceals_at(floor(), (xpos() + sizex()) / GRID_SIZE, ypos() / GRID_SIZE)
	            || current_game->world->decor_conceals_at(floor(), (xpos() + sizex()) / GRID_SIZE, (ypos() + sizey()) / GRID_SIZE)
	            || current_game->world->decor_conceals_at(floor(), xpos() / GRID_SIZE, (ypos() + sizey()) / GRID_SIZE)
	        )
	   )
	{
		// charge us a point of magic ..
		if (stats_->magicpoints() > 0.0f && stats_->current_magic_delay() == 0)
        {
            stats_->set_magicpoints( stats_->magicpoints() - 1);
        }

		float temp;
		if (myguy)
			temp = (4 - myguy->dexterity/10.0f);
		else
			temp = (4.0f - static_cast<float>(stats_->level())/2.0f);
		if (temp < 0)
			temp = 0;
		float next_stepsize = stepsize() - temp;
		if (next_stepsize < 1.0f)
			next_stepsize = 1.0f;
		set_stepsize(next_stepsize);
	}  // end of forestwalk check
	else
		set_stepsize(normal_stepsize());

	// Speed bonus
	if (speed_bonus_left() > 1)
	{
		set_speed_bonus_left(speed_bonus_left() - 1);
		set_stepsize(stepsize() + speed_bonus());
	}
	
	
	if(attack_lunge() > 0.0f)
    {
        float next_lunge = attack_lunge() - 0.4f;
        if(next_lunge < 0.0f)
            next_lunge = 0.0f;
        set_attack_lunge(next_lunge);
    }
	
	if(hit_recoil() > 0.0f)
    {
        float next_recoil = hit_recoil() - 0.6f;
        if(next_recoil < 0.0f)
            next_recoil = 0.0f;
        set_hit_recoil(next_recoil);
    }

	// Per-family periodic auto-powers (e.g. archmage view_all bonus)
	{
		const auto* fd = get_family_descriptor(family());
		og::script::hooks::on_act_living(fd, this);
		// Scripted act override: a registered on_act_override hook returning
		// true owns this walker's act for the tick (the effect_on_act
		// "true = handled" contract). Early-outs on the family hook mask, so
		// classic paths are byte-identical.
		if (og::script::hooks::on_act_override(fd, this))
			return 1;
	}

	// Complete previous animations (like firing)
	if (ani_type() != ANI_WALK)
		return animate();

	// Are we frozen?
	if (stats_->frozen_delay())
	{
		stats_->set_frozen_delay(	stats_->frozen_delay() - 1);
		return 1;
	}

	if (busy() > 0.0f)
		set_busy(busy() - 1.0f); // This allows busy to be our FIRING delay.
	// Find new action

	// Turn if you want to (...turn, around the world...)
	if (curdir() != enddir() && query_order() == Order::Living)
		return turn(enddir());


	// Are we performing some action?
	if (stats_->has_commands())
	{
		std::int32_t temp = stats_->do_command();
		if (temp)
			return 1;
	}

	if (skip_exit() > 0)
		set_skip_exit(static_cast<short>(skip_exit() - 1));

	// Do we have a generic action-type set?
	if (action() && (user() == -1) )
	{
		std::int32_t temp = do_action();
		if (temp)
			return temp;
	}

	switch (act_type())
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
				this->set_dead(1);
				return 1;
			}
			// We are randomly walking toward enemy
		case ACT_RANDOM:
			{
				if (!current_game->world->rng_.next(5) ) //1 in 5 to do our special
				{
					// Should we do our special? Are we full of magic?
					if (stats_->magicpoints() >= stats_->special_cost(1))
					{
						set_current_special(static_cast<char>(current_game->world->rng_.next((stats_->level()+2)/3) + 1));
						const FamilyDescriptor* special_fd = get_family_descriptor(family());
						if ( (current_special() > 4) ||
						        (current_special() < 1) ||
						        (current_special() >= FD_NUM_SPECIALS) ||
						        (special_fd == nullptr) ||
						        (strcmp(special_fd->special_names[static_cast<int>(current_special())], "NONE") == 0)
						   )
							set_current_special(1);
						if (check_special() )
							return special();
					}
					else       // do random walking ..
					{
						act_random();
						return 1;
					}
				}
				else if (!current_game->world->rng_.next(5) ) //1 in 5 to do act_random() function
					act_random();
				else // 4 of 5 times
				{
					if (!foe())
					{
						set_foe(current_game->world->find_near_foe(this));
					}
					if (foe()) // && current_game->world->rng_.next(2) )
					{
						const char snapped_dir = static_cast<char>((enddir() / 2) * 2);
						set_curdir(static_cast<signed char>(snapped_dir));
						set_enddir(snapped_dir);
						//stats_->try_command(COMMAND_SEARCH, 40, 0, 0);
						stats_->try_command(COMMAND_SEARCH, 300, 0, 0);
					}
					//else if (foe)
					//  stats_->try_command(COMMAND_RIGHT_WALK,40,0,0);
					else if (!current_game->world->rng_.next(2))
						set_foe(current_game->world->find_far_foe(this));
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

	if (target && !target->dead() && (query_order()==Order::Living) &&  //we are alive
	        (is_friendly(target)) // we are allied
	   )
		// Make sure WE don't get shoved
		if (current_game->world->rng_.next(3) && target->act_type() != ACT_CONTROL)
		{
			// 2026-07-10 shove command-theft livelock fix (see
			// docs/GAMEPLAY_FIXES_FROM_CLASSIC.md). Classic cleared the
			// target's queue and injected COMMAND_WALK(x, y) even when that
			// walk was wall-blocked; a shover re-colliding every tick then
			// refilled the injected command forever, so the target never
			// re-ran its own COMMAND_SEARCH — three interlocked bodies on a
			// one-cell strip froze for 350+ ticks (Westlands L2 bend-1).
			// Probe the injected walk's baby step (walkstep's last resort,
			// walk(x, y)) against the GRID only before stealing the queue:
			// if even that step is terrain-blocked the inject provably
			// cannot move the target, so leave its queue alone and let its
			// own search re-path. Grid-only (no obmap) keeps ally-chain
			// shoving intact and cannot eat treasures or fire collide()
			// side effects. The rng_.next(3) draw above stays unconditional,
			// so the RNG stream is unchanged on every path.
			if (!current_game->world->query_grid_passable(
			        target->xpos() + static_cast<float>(x),
			        target->ypos() + static_cast<float>(y), target))
				return 0;
			// We have to prevent a build-up of shoves which is
			//   caused by a blocked target.  We do so for now by clearing
			//   all commands
			target->stats()->clear_command();
			{
				const auto* fd = get_family_descriptor(target->family());
				og::script::hooks::on_shoved(fd, target);
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

	if (curdir() == dir)  // if continue direction
	{
		// check if off map
		if (x + xpos() < 0 ||
		        x + xpos() >= current_game->world->grid.w * GRID_SIZE ||
		        y + ypos() < 0 ||
		        y + ypos() >= current_game->world->grid.h * GRID_SIZE)
		{
			return 0;
		}

			// Here we check if the move is valid
			// Normally we would check if the object at this grid point
			//    is passable (I cheated for now)
			// FIXME: These additional checks are a hack for the corner clipping bug (you could get into trees, etc.)
			if (current_game->world->query_passable(xpos() + x, ypos() + y, this)
			        && current_game->world->query_passable(xpos() + ceilf(x), ypos() + ceilf(y), this)
			        && current_game->world->query_passable(xpos() + floorf(x), ypos() + floorf(y), this))
		{
			// Control object does complete redraw anyway
			worldmove(x,y);
			set_cycle(static_cast<signed char>(cycle() + 1));
			//if (!ani || (curdir*cycle > sizeof(ani)) )
			//  Log("WALKER::WALK: Bad ani!\n");
			set_frame_from_current_walk_animation();
			return 1;
		}
		else //Invalid move?
		{
			if (collide_ob() && !collide_ob()->dead())
			{
				if (collide_ob()->query_order() == Order::Living && is_friendly(collide_ob()) )
				{
					shove(collide_ob(), x, y);
				}
			}  // end hit some object
			if (stats_->query_bit_flags(BIT_ANIMATE) )  // animate regardless..
			{
				set_cycle(static_cast<signed char>(cycle() + 1));
				set_frame_from_current_walk_animation();
			}

			return 0;
		}
	}
	else // Just changing direction
	{
		set_enddir(static_cast<char>(dir));

		// Technically, control gets and EXTRA call to TURN
		//   because first we call WALK, then ACT, whereas
		//   other walkers call ACT.  This would cause control
		//   to turn TWICE on the first call to walk, which is bad.
		//   So we stop that behavior here.
		if (this->act_type() != ACT_CONTROL || stats_->has_commands())
			turn(enddir());
	}
	return 1;
}

bool walkerIsAutoAttackable(walker* ob)
{
    Order order = ob->query_order();
    if (order == Order::Living || order == Order::Generator)
        return true;
    if (order == Order::Weapon)
    {
        const auto* wfd = get_weapon_family_descriptor(ob->family());
        return wfd && wfd->is_auto_attackable;
    }
    return false;
}

bool living::collide(walker  *ob)
{
	set_collide_ob(ob);
	//return 1; // debug
	if ( ob && walkerIsAutoAttackable(ob) && (is_friendly(ob) == 0)
	        && !ob->dead() && !dead())
		init_fire();
	return 1;
}

walker* living::do_summon(char whatfamily, std::int32_t summon_lifetime)
{
	walker  *newob;

	newob = current_game->world->add_ob(Order::Living, whatfamily);
	if (newob == nullptr) return nullptr;
	newob->set_owner(this);
	newob->set_summoned(true);  // ammunition: never a SAVE_ALL loss
		newob->set_lifetime(summon_lifetime);
	newob->set_floor(floor());  // summons appear on the summoner's floor (A8)
	newob->transform_to(Order::Living, whatfamily);
	//  Log("\n\nSummoned %d, life %d\n", whatfamily, lifetime);

	return newob;
}

// Returns true or false on whether it's good to do
// the special or not ..
bool living::check_special()
{
	// Per-placed-NPC scenario flag: the AI never decides to use a special on
	// a specials-disabled walker (walker::special() is gated too, so direct
	// callers that skip this check still cannot fire one).
	if (specials_disabled())
		return false;

	set_shifter_down(static_cast<short>(current_game->world->rng_.next(2))); // on or off, randomly ..

	// Make sure we have enough ..
	if (stats_->magicpoints() < stats_->special_cost(static_cast<int>(current_special())))
		set_current_special(1); // make us do default ..

	auto* fd = get_family_descriptor(family());
	if (auto hook_result = og::script::hooks::check_special_ai(fd, this))
		return *hook_result;

	// Default: always allow
	return true;
}

void living::set_difficulty(std::uint32_t whatlevel)
{
	//  std::int32_t calcdelay,calcrate;  // apparently not used anymore
	std::uint32_t dif1 = query_difficulty_percent();
	const float levmult = static_cast<float>(whatlevel) * static_cast<float>(whatlevel);
	const float level_f = static_cast<float>(whatlevel);

	auto* fd = get_family_descriptor(family());
	if (og::script::hooks::set_difficulty(fd, this, whatlevel))
	{
		// Scripted or family-specific scaling ran.
	}
	else
	{
		// Default formula
		stats_->set_max_hitpoints(	stats_->max_hitpoints() + 11.0f * levmult);
		stats_->set_max_magicpoints(	stats_->max_magicpoints() + 11.0f * levmult);
		set_damage(damage() + 4.0f * level_f);
		stats_->set_armor(	stats_->armor() + 2.0f * levmult);
	}

	// Adjust for difficulty settings now...
	if (team_num() != 0)  // hostile/neutral teams: legacy path, untouched
	{
		const float dif = static_cast<float>(dif1);
		stats_->set_max_hitpoints((stats_->max_hitpoints() * dif) / 100.0f);
		stats_->set_max_magicpoints((stats_->max_magicpoints() * dif) / 100.0f);
		set_damage((damage() * dif) / 100.0f);
	}
	else if (myguy == nullptr && dif1 != 100)
	{
		// A12a: placed team-0 NPCs (the SAVE_ALL Bearer, allied war-hosts,
		// rearguards...) scale with the difficulty setting exactly like their
		// foes — the legacy team!=0 gate was meant to exempt PLAYER
		// characters, but player crews never pass through set_difficulty
		// (they derive stats via guy::update_derived_stats), so it silently
		// froze allied NPCs at 100% while enemies doubled on Hard. Only a
		// walker actually carrying a player guy (myguy) is exempt.
		// Gated on dif1 != 100: parity goldens and the harness run at 100%,
		// and (x*100.0f)/100.0f is NOT bit-exact for every float, so the
		// skip — not a multiply-by-1 — is what preserves byte-identity.
		const float dif = static_cast<float>(dif1);
		stats_->set_max_hitpoints((stats_->max_hitpoints() * dif) / 100.0f);
		stats_->set_max_magicpoints((stats_->max_magicpoints() * dif) / 100.0f);
		set_damage((damage() * dif) / 100.0f);
	}

	stats_->set_hitpoints(stats_->max_hitpoints());
	stats_->set_magicpoints(stats_->max_magicpoints());

		stats_->set_max_heal_delay(REGEN); //defined in constants.h
		stats_->set_current_heal_delay(static_cast<std::int32_t>(levmult * 4.0f)); //for purposes of calculation only

	while (stats_->current_heal_delay() > REGEN)
	{
		stats_->set_current_heal_delay(	stats_->current_heal_delay() - REGEN);
		stats_->set_heal_per_round(	stats_->heal_per_round() + 1);
	} // this takes care of the integer part, now calculate the fraction

	if (stats_->current_heal_delay() > 1)
	{
		stats_->set_max_heal_delay(
		    stats_->max_heal_delay() /
		    static_cast<std::int32_t>(stats_->current_heal_delay() + 1));
	}
	stats_->set_current_heal_delay(0); //start off without healing

	//make sure we have at least a 2 wait, otherwise we should have
	//calculated our heal_per_round as one higher, and the math must
	//have been screwed up some how
	if (stats_->max_heal_delay() < 2)
		stats_->set_max_heal_delay(2);



	// Set the magic delay ..
	stats_->set_max_magic_delay(REGEN);
	stats_->set_current_magic_delay(static_cast<std::int32_t>(levmult*30));//for calculation only

	while (stats_->current_magic_delay() > REGEN)
	{
		stats_->set_current_magic_delay(	stats_->current_magic_delay() - REGEN);
		stats_->set_magic_per_round(	stats_->magic_per_round() + 1);
	} // this takes care of the integer part, now calculate the fraction

	if (stats_->current_magic_delay() > 1)
	{
		stats_->set_max_magic_delay(
		    stats_->max_magic_delay() /
		    static_cast<std::int32_t>(stats_->current_magic_delay() + 1));
	}
	stats_->set_current_magic_delay(0); //start off without magic regen

	//make sure we have at least a 2 wait, otherwise we should have
	//calculated our magic_per_round as one higher, and the math must
	//have been screwed up some how
	if (stats_->max_magic_delay() < 2)
		stats_->set_max_magic_delay(2);

}

short living::facing(short x, short y)
{
	std::int32_t bigy = static_cast<std::int32_t>(y*1000);
	std::int32_t slope;

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
	if (!current_game->world->rng_.next(80) || (!foe()))
		set_foe(current_game->world->find_near_foe(this));
	if (!foe())
		return stats_->try_command(COMMAND_RANDOM_WALK,40);

	xdist = static_cast<short>(foe()->xpos() - xpos());
	ydist = static_cast<short>(foe()->ypos() - ypos());

	// If foe is in firing range, turn and fire
	if (abs(xdist) < lineofsight() * GRID_SIZE &&
	        abs(ydist) < lineofsight() * GRID_SIZE)
	{
		if (fire_check(xdist, ydist))
		{
			init_fire(xdist, ydist);
			stats_->set_command(COMMAND_FIRE, static_cast<short>(current_game->world->rng_.next(24)), xdist, ydist);
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

	if (!action())
		return 0;

	switch (action())
	{
		case ACTION_FOLLOW: // follow our leader, attack his targets ..
			if (foe())
				return 0;       // continue as normal
			set_leader(current_game->world->find_nearest_player(this));
			if (!leader())
				return 0;       // continue as normal ... shouldn't happen
			if (leader()->foe())
			{
				set_foe(leader()->foe());
				return 0;       // continue from this point ..
			}
			// Else follow our leader
			stats_->force_command(COMMAND_FOLLOW, 5, 0, 0);
			return 1;
		default:
			return 0;
	}
}
