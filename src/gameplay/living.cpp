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
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <cstring>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/possession.h>
#include "sim_difficulty.h"

// RNG now comes from current_game->world->rng_.
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
	// we get extra rounds to act this cycle
	// rule: A living walker with banked bonus rounds (granted by an enemy-team mage's FREEZE TIME
	//   to its allies) takes one full extra act per banked round, all in the same tick and with no
	//   cap (the old game recursed; this game loops), so every per-tick timer (invulnerability,
	//   invisibility, flight, charm, speed bonus, rests, lunge) runs down once per round. Rounds
	//   banked during those acts wait for the next tick; a dead walker takes none and keeps its bank.
	// tests: LivingUnit.act_spends_every_banked_bonus_round_in_one_call
	// tests: LivingCombat.living_act_bonus_rounds_and_dead_gate
	// tests: SemLiving.classic_banked_rounds_run_every_timer_once_per_round_and_no_rounds_or_dead_act_once
	// tests: SemLiving.banked_rounds_past_fifty_all_run_in_one_tick
	// tests: SemLiving.a_walker_with_rounds_still_banked_does_not_regenerate
	const short extra = (bonus_rounds() > 0 && !dead()) ? bonus_rounds() : short{0};
	if (extra > 0)
		set_bonus_rounds(0);
	for (short i = 0; i < extra; ++i)
		act_round();
	return act_round();
}

bool living::act_round()
{
	// rule: A dead walker does nothing in its act; a walker that dies falling down a pit
	//   (apply_z_motion) stops acting that same tick.
	// tests: LivingCombat.living_act_bonus_rounds_and_dead_gate
	// tests: LivingCombat.living_round8_dead_outline_forestwalk_and_offmap_walk_paths
	// tests: LivingUnit.living_r11_act_owner_dead_and_action_follow
	// tests: SemLiving.a_dead_walker_does_nothing_and_a_pit_death_ends_the_act_at_once
	if (dead())
		return 0;

	// Runtime headless/network sims advance drawcycle here so living periodic
	// effects are not dependent on a renderer-side draw path.
	set_drawcycle(static_cast<unsigned char>(drawcycle() + 1));

	// Z-axis: fall through air / take Z-stairs (multi-floor only; a cheap no-op
	// on single-floor levels). May cause a pit death.
	apply_z_motion();
	// rule: A dead walker does nothing in its act; a walker that dies falling down a pit
	//   (apply_z_motion) stops acting that same tick.
	// tests: LivingCombat.living_act_bonus_rounds_and_dead_gate
	// tests: LivingCombat.living_round8_dead_outline_forestwalk_and_offmap_walk_paths
	// tests: LivingUnit.living_r11_act_owner_dead_and_action_follow
	// tests: SemLiving.a_dead_walker_does_nothing_and_a_pit_death_ends_the_act_at_once
	if (dead())
		return 0;

	// #160: exit-pad re-trigger latch upkeep — clears once we have fully
	// left the latched pad's rect. Runs before the animate/turn/frozen
	// early-returns so the latch can never wedge shut on a walker that is
	// mid-animation or frozen while stepping off the pad.
	update_exit_latch();

	// Make sure everyone we're pointing to is valid. invisibility_left is a raw short written straight from snapshots (world_snapshot.cpp) and from Lua (set_invisibility_left), so it is NOT guaranteed non-negative; a negative divided by 20 and cast to IRandom::next's uint32 wrapped to a ~4e9 bound, which drops the foe on nearly every tick. Clamp the cloak counter to 0 first: for every non-negative value this is the identical expression, and next(0) == 0 keeps the lock exactly as before.
	// rule: A walker forgets a foe that is dead or hidden (dug in / riding a body), and loses track
	//   of a cloaked foe with a chance that grows with the foe's remaining invisibility (a roll of
	//   next(invis/20) above 0); a negative cloak counter is treated as no cloak.
	// tests: SimWorldHeadless.negative_invisibility_does_not_break_the_foe_lock
	// tests: SimWorldHeadless.foe_lock_unchanged_for_non_negative_invisibility
	// tests: CoverageMisc.coverage_r19_living_act_random_acquires_a_foe_and_queues_a_command
	// tests: SemLiving.a_walker_forgets_a_hidden_foe_without_rolling_for_its_cloak
	if (foe() && (foe()->dead() || foe()->hidden() || (current_game->world->rng_.next(static_cast<std::uint32_t>(foe()->invisibility_left() > 0 ? foe()->invisibility_left()/20 : 0)) > 0) ) )
		set_foe(nullptr);
	// rule: A walker never keeps a friend as its foe: a foe that became friendly (charmed onto our
	//   side, or we were charmed onto its side) is dropped.
	// tests: SemLiving.a_walker_drops_a_foe_that_turned_friendly_and_keeps_a_hostile_one
	if (is_friendly(foe()))
		set_foe(nullptr);
	// rule: A follower whose leader has died forgets the leader (so a later Yell can recruit it
	//   again).
	// tests: SemLiving.a_follower_forgets_a_dead_leader_and_keeps_a_live_one
	if (leader() && leader()->dead())
		set_leader(nullptr);
	// rule: A summoned or owned living dies when its owner dies.
	// tests: LivingCombat.living_act_owner_dead_kills_summon
	// tests: LivingUnit.living_r11_act_owner_dead_and_action_follow
	// tests: SemLiving.an_owned_living_goes_on_acting_while_its_owner_lives
	if (owner() && owner()->dead())
	{
		//owner = nullptr;
		// A living who had an owner who is now dead, dies as well
		set_dead(1);
		death();
		return 0;
	}

	// rule: A walker with a lifetime (a summon) dies if it has no owner, otherwise its lifetime
	//   counts down one per act and it dies when it reaches 0. A lifetime of 0 means permanent.
	// tests: LivingCombat.living_act_lifetime_expiry_with_owner
	// tests: LivingCombat.living_act_lifetime_expires_without_owner
	// tests: parity:summon_lifetime_decrement_faerie_scen99, parity:summon_lifetime_faerie_scen99
	// tests: SemLiving.a_lifetime_counts_down_one_per_act_and_zero_means_permanent
	if (lifetime())
	{
		// rule: A walker with a lifetime (a summon) dies if it has no owner, otherwise its lifetime
		//   counts down one per act and it dies when it reaches 0.
		// tests: LivingCombat.living_act_lifetime_expiry_with_owner
		// tests: LivingCombat.living_act_lifetime_expires_without_owner
		// tests: parity:summon_lifetime_decrement_faerie_scen99
		// tests: parity:summon_lifetime_faerie_scen99
		// tests: SemLiving.a_lifetime_counts_down_one_per_act_and_zero_means_permanent
		if (!owner() || owner()->dead()) // our owner gone?
		{
			set_dead(1);
			death();
			return 0;
		}
		const auto remaining_lifetime = lifetime() - 1;
		set_lifetime(remaining_lifetime);
		// rule: A walker with a lifetime (a summon) dies if it has no owner, otherwise its lifetime
		//   counts down one per act and it dies when it reaches 0.
		// tests: LivingCombat.living_act_lifetime_expiry_with_owner
		// tests: LivingCombat.living_act_lifetime_expires_without_owner
		// tests: parity:summon_lifetime_decrement_faerie_scen99
		// tests: parity:summon_lifetime_faerie_scen99
		// tests: SemLiving.a_lifetime_counts_down_one_per_act_and_zero_means_permanent
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
	// rule: After a seated hero thaws it is immune to a new freeze for a short window; that
	//   negative phase climbs back toward 0 by one per act, even if the seat has switched away.
	// tests: StatsFright.living_act_climbs_immunity_and_ai_drain_thaws_to_zero
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
		// rule: HP and MP regeneration stop for every walker while a FREEZE TIME bank
		//   (world.enemy_freeze) runs, and for a walker while it still has banked bonus rounds; HP
		//   regen also waits out regen_delay after damage.
		// tests: SemLiving.classic_regeneration_runs_each_act_and_freeze_time_stops_it_for_every_side
		// tests: SemLiving.a_walker_with_rounds_still_banked_does_not_regenerate
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
	// rule: A view-all (see everything) bonus lasts its count of acts.
	// tests: LivingCombat.living_act_timers_charm_and_recoil_clamps
	// tests: SemLiving.view_all_and_invulnerability_at_zero_stay_zero
	if (view_all() > 0)
		set_view_all(static_cast<short>(view_all() - 1));

	// Invulnerability
	// rule: Invulnerability lasts its count of acts.
	// tests: LivingCombat.living_act_timers_charm_and_recoil_clamps
	// tests: SemLiving.view_all_and_invulnerability_at_zero_stay_zero
	if (invulnerable_left() > 0)
		set_invulnerable_left(static_cast<short>(invulnerable_left() - 1));

	// Invisibility
	// rule: Invisibility lasts its count of acts and keeps its outline while it lasts; once it is
	//   out the outline is cleared.
	// tests: LivingCombat.living_act_invisibility_skip_exit_and_action_command_paths
	// tests: LivingCombat.living_round8_dead_outline_forestwalk_and_offmap_walk_paths
	// tests: LivingCombat.living_act_timers_charm_and_recoil_clamps
	if (invisibility_left() > 0)
		set_invisibility_left(static_cast<short>(invisibility_left() - 1));
	else
		set_outline(0);

	// Flight
	// rule: Flight (potion or spell) lasts its count of acts.
	// tests: SemLiving.flight_lasts_its_count_of_acts_and_then_stays_zero
	if (flight_left() > 0)
		set_flight_left(static_cast<short>(flight_left() - 1));
	// rule: A non-flying walker standing inside impassable terrain (a wall, water, trees without
	//   forestwalk) loses 1 HP per act (a red '1' damage number) and is given one tick of flight so
	//   it can step out; at 0 HP it dies.
	// tests: LivingCombat.living_act_nonpassable_tile_damage_kills
	// tests: SemLiving.a_walker_stuck_in_a_wall_loses_a_point_per_act_and_gets_a_tick_of_flight
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
		
		// rule: A non-flying walker standing inside impassable terrain (a wall, water, trees
		//   without forestwalk) loses 1 HP per act (a red '1' damage number) and is given one tick
		//   of flight so it can step out.
		// tests: LivingCombat.living_act_nonpassable_tile_damage_kills
		// tests: SemLiving.a_walker_stuck_in_a_wall_loses_a_point_per_act_and_gets_a_tick_of_flight
		if (stats_->hitpoints() <= 0)
		{
			set_dead(1);
			death();
		}
	}

	// Charmed-ness
	// rule: Charm lasts its timer; when it runs out the walker goes back to its real team
	//   (real_team_num is cleared to 255). A walker that was never charmed keeps its team.
	// tests: KitPossession.charm_expiry_and_release_in_the_same_act
	// tests: SillinessBattery.thief_charm_ceiling_and_natural_expiry
	// tests: SemLiving.charm_counts_down_without_touching_the_team_and_an_uncharmed_walker_keeps_its_team
	if (charm_left() > 1)
		set_charm_left(static_cast<short>(charm_left() - 1));
	else
	{
		set_charm_left(0);
		// Make sure we're back to our real team
		// rule: Charm lasts its timer; when it runs out the walker goes back to its real team
		//   (real_team_num is cleared to 255).
		// tests: KitPossession.charm_expiry_and_release_in_the_same_act
		// tests: SillinessBattery.thief_charm_ceiling_and_natural_expiry
		// tests: SemLiving.charm_counts_down_without_touching_the_team_and_an_uncharmed_walker_keeps_its_team
		if (real_team_num() != 255)
		{
			set_team_num(real_team_num());
			set_real_team_num(255);
		}
	}
	og::sim::possession_host_tick(*current_game->world, *this); // New Specials: a possessed host carries its rider and its countdown
	// rule: A forestwalker (elf, druid...) that has any corner of its box in trees or in concealing
	//   decor (a SHRUB) moves at a forest pace; out of the trees its step is reset to its normal
	//   stepsize every act.
	// tests: Decor.shrub_conceals_for_living_and_weapon_but_bones_does_not
	// tests: LivingUnit.speed_bonus_counts_down_to_zero_and_rides_every_tick
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
		// rule: Walking in the forest costs a forestwalker one MP each time its magic-regen delay
		//   is at 0.
		// tests: Decor.shrub_conceals_for_living_and_weapon_but_bones_does_not
		// tests: SemLiving.the_forest_charges_magic_only_with_magic_in_hand_and_the_delay_at_zero
		if (stats_->magicpoints() > 0.0f && stats_->current_magic_delay() == 0)
        {
            stats_->set_magicpoints( stats_->magicpoints() - 1);
        }

		float temp;
		// rule: In the forest a forestwalker's step shrinks by 4 - dexterity/10 (a player
		//   character) or 4 - level/2 (an NPC), never by a negative amount and never below 1 px.
		//   Classic shrinks the step it carries: a haste compounds every act, and without one the
		//   step crawls to 1 px. Gameplay Fixes AG shrinks the normal step, every act.
		// tests: SemAi2.a_forestwalker_in_the_trees_steps_shorter_by_its_dexterity_or_level
		// tests: SemAi2.a_forestwalker_in_the_trees_keeps_its_forest_step_with_the_setting_on
		// tests: SemAi2.a_forestwalker_in_the_trees_slows_to_one_pixel_with_the_setting_off
		// tests: SemAi2.a_hastened_forestwalker_in_the_trees_steps_its_forest_step_plus_the_haste_with_the_setting_on
		// tests: SemAi2.a_hastened_forestwalker_in_the_trees_compounds_its_step_with_the_setting_off
		// tests: parity:elf_forest_haste_scen99, parity:elf_forest_haste_fixes_scen99
		if (myguy)
			temp = (4 - myguy->dexterity/10.0f);
		else
			temp = (4.0f - static_cast<float>(stats_->level())/2.0f);
		if (temp < 0)
			temp = 0;
		float next_stepsize = (og::sim::session_gameplay_fixes() != 0 ? normal_stepsize() : stepsize()) - temp;
		if (next_stepsize < 1.0f)
			next_stepsize = 1.0f;
		set_stepsize(next_stepsize);
	}  // end of forestwalk check
	else
		set_stepsize(normal_stepsize());

	// Speed bonus
	// rule: A speed bonus of N ticks gives N ticks of extra step (stepsize + speed_bonus) and then
	//   ends at 0.
	// tests: LivingUnit.speed_bonus_counts_down_to_zero_and_rides_every_tick
	if (speed_bonus_left() > 0) // 2002 said `> 1`: N ticks gave N-1 and the field stuck at 1, so "not hastened" (== 0) was never true again
	{
		set_speed_bonus_left(speed_bonus_left() - 1);
		set_stepsize(stepsize() + speed_bonus());
	}
	// A special's rest between casts (lib/living_common.lua with_rest)
	// rule: A special's rest between casts (FREEZE TIME, HOWL, SCARE, MIND CONTROL...) counts down
	//   one per act.
	// tests: KitRests.freeze_time_rests_thirty_seconds
	// tests: SemAi2.a_specials_rest_counts_down_one_per_act_and_stops_at_zero
	if (special_rest_left() > 0) set_special_rest_left(special_rest_left() - 1);
	// rule: An attack lunge (sprite offset) eases back by 0.4 per act and stops at 0.
	// tests: LivingCombat.living_act_timers_charm_and_recoil_clamps
	// tests: SemAi2.an_attack_lunge_eases_back_by_four_tenths_per_act_and_stops_at_zero
	if(attack_lunge() > 0.0f)
    {
        float next_lunge = attack_lunge() - 0.4f;
        // rule: An attack lunge (sprite offset) eases back by 0.4 per act and stops at 0.
        // tests: LivingCombat.living_act_timers_charm_and_recoil_clamps
        // tests: SemAi2.an_attack_lunge_eases_back_by_four_tenths_per_act_and_stops_at_zero
        if(next_lunge < 0.0f)
            next_lunge = 0.0f;
        set_attack_lunge(next_lunge);
    }
	
	// rule: A hit recoil (sprite offset) eases back by 0.6 per act and stops at 0.
	// tests: LivingCombat.living_act_timers_charm_and_recoil_clamps
	// tests: SemAi2.a_hit_recoil_eases_back_by_six_tenths_per_act_and_stops_at_zero
	if(hit_recoil() > 0.0f)
    {
        float next_recoil = hit_recoil() - 0.6f;
        // rule: A hit recoil (sprite offset) eases back by 0.6 per act and stops at 0.
        // tests: LivingCombat.living_act_timers_charm_and_recoil_clamps
        // tests: SemAi2.a_hit_recoil_eases_back_by_six_tenths_per_act_and_stops_at_zero
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
		// rule: A family (or mode) Lua on_act_override that returns true owns the walker's act for
		//   that tick: none of the animate/frozen/command/AI steps run.
		// tests: ModeTick.act_override_true_owns_the_tick
		// tests: ModeTick.act_override_false_falls_through_to_default_act
		if (og::script::hooks::on_act_override(fd, this))
			return 1;
	}

	// Complete previous animations (like firing)
	// rule: A walker in the middle of an animation (attack, cast, death...) finishes it before it
	//   does anything else: no frozen drain, busy drain, turn, command or AI that tick.
	// tests: SemAi2.a_walker_mid_animation_finishes_it_before_anything_else
	if (ani_type() != ANI_WALK)
		return animate();

	// Are we frozen?
	// rule: A frozen walker spends its act thawing by one and does nothing else.
	// tests: StatsFright.living_act_climbs_immunity_and_ai_drain_thaws_to_zero
	// tests: ModeTick.act_override_false_falls_through_to_default_act
	// tests: SemAi2.a_frozen_walker_spends_its_act_thawing_and_nothing_else
	if (stats_->frozen_delay())
	{
		stats_->set_frozen_delay(	stats_->frozen_delay() - 1);
		return 1;
	}

	// rule: The firing delay (busy) counts down by one per act.
	// tests: GameplayFixesSpecialBusy.whirlwind_after_a_shot
	if (busy() > 0.0f)
		set_busy(busy() - 1.0f); // This allows busy to be our FIRING delay.
	// Find new action

	// Turn if you want to (...turn, around the world...)
	// rule: A walker not yet facing where it wants to go spends its act turning one notch toward
	//   it.
	// tests: SemAi2.a_walker_not_facing_its_goal_spends_its_act_turning_one_notch
	if (curdir() != enddir() && query_order() == Order::Living)
		return turn(enddir());


	// Are we performing some action?
	// rule: A walker with queued commands carries them out before its AI; a command step that does
	//   something ends the act.
	// tests: LivingCombat.living_act_command_execution_and_autoattackable_edges
	// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
	// tests: SemAi2.a_command_step_that_does_nothing_lets_the_act_go_on
	if (stats_->has_commands())
	{
		std::int32_t temp = stats_->do_command();
		// rule: A walker with queued commands carries them out before its AI; a command step that
		//   does something ends the act.
		// tests: LivingCombat.living_act_command_execution_and_autoattackable_edges
		// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
		// tests: SemAi2.a_command_step_that_does_nothing_lets_the_act_go_on
		if (temp)
			return 1;
	}

	// rule: The exit-pad skip counter counts down one per act.
	// tests: LivingCombat.living_act_invisibility_skip_exit_and_action_command_paths
	// tests: SemAi2.the_exit_skip_counter_counts_down_and_stops_at_zero
	if (skip_exit() > 0)
		set_skip_exit(static_cast<short>(skip_exit() - 1));

	// Do we have a generic action-type set?
	// rule: An AI (unseated) walker with an action order (ACTION_FOLLOW from Summon Defense) runs
	//   that order first; if the order acted, the AI does not also run. A seated hero ignores its
	//   action.
	// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
	// tests: SemAi2.a_seated_walker_ignores_its_action_and_a_passing_action_lets_the_act_go_on
	if (action() && (user() == -1) )
	{
		std::int32_t temp = do_action();
		// rule: An AI (unseated) walker with an action order (ACTION_FOLLOW from Summon Defense)
		//   runs that order first; if the order acted, the AI does not also run.
		// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
		// tests: SemAi2.a_seated_walker_ignores_its_action_and_a_passing_action_lets_the_act_go_on
		if (temp)
			return temp;
	}

	// rule: A seated hero (ACT_CONTROL) has no AI: its act ends after the timers and commands; the
	//   seat's input drives it.
	// tests: LivingCombat.living_act_control, LivingUnit.living_r11_collide_and_act_type_switches
	switch (act_type())
	{
			// We are the control character
		case ACT_CONTROL:
			{
				return 1;

				//break;
			}
			// We are a weapon
		// plumbing: ACT_FIRE on a living is an impossible state (weapons act through weap::act);
		//   logs and returns 1.
		case ACT_FIRE:
			{
				Log("Living think's it's a weapon (act_fire)\n");
				//              act_fire();
				return 1;
				//break;
			}
		// rule: A guard runs its guard logic (wake on sight, hold post); see walker act_guard rows.
		// tests: LivingCombat.living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight
		// tests: WalkerSpecials.act_guard_and_low_magic_act_random_pin_what_they_queue
		case ACT_GUARD:
			{
				act_guard();
				break;
			}
		// rule: A walker set to the die act type dies on its next act.
		// tests: LivingUnit.living_r11_collide_and_act_type_switches
		// tests: LivingUnit.living_r14_lines_155_190_196_212_219_226_235_245_259_266_270_303_308
		case ACT_DIE:
			{
				this->set_dead(1);
				return 1;
			}
			// We are randomly walking toward enemy
		// rule: An ordinary bot considers its special one act in five; the other four it takes the
		//   ordinary act.
		// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
		// tests: LivingActFlow.a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on
		case ACT_RANDOM:
			{
				// rule: An ordinary bot considers its special one act in five; the other four it
				//   takes the ordinary act.
				// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
				// tests: LivingActFlow.a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on
				if (!current_game->world->rng_.next(5) ) //1 in 5 to do our special
				{
					// Should we do our special? Are we full of magic?
					// rule: A bot considers a special only when it can pay for its slot 1;
					//   otherwise the special roll is spent on act_random (seek and fire).
					// tests: LivingActFlow.the_act_path_never_reaches_an_unaffordable_slot
					// tests: WalkerSpecials.act_guard_and_low_magic_act_random_pin_what_they_queue
					// tests: LivingActFlow.a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on
					if (stats_->magicpoints() >= og::sim::cast_cost(*this, 1, false))
					{
						const FamilyDescriptor* special_fd = get_family_descriptor(family());
						set_current_special(static_cast<char>(current_game->world->rng_.next(static_cast<std::uint32_t>(og::sim::bot_slot_roll_bound(special_fd, stats_->level(), current_game->world->new_specials, og::sim::session_gameplay_fixes()))) + 1));
						// rule: A bot rolls a slot uniformly from the slots its level opens
						//   (bot_slot_roll_bound); classic folds any roll above slot 4 and any
						//   unnamed slot to slot 1; with Gameplay Fixes on, slots 5+ that are named
						//   and open stay (the bound never passes the last named slot).
						// tests: KitBots.bot_roll_is_identical_with_gameplay_fixes_off
						// tests: KitBots.a_level_sixteen_mage_rolls_heartburst_when_on
						// tests: SemAi2.a_bot_that_rolls_an_unnamed_or_out_of_range_slot_casts_slot_one
						if ( (current_special() > 4 && og::sim::session_gameplay_fixes() == 0) ||
						        (current_special() < 1) ||
						        (current_special() >= FD_NUM_SPECIALS) ||
						        (special_fd == nullptr) ||
						        (strcmp(og::sim::special_name(special_fd, static_cast<int>(current_special()), current_game->world->new_specials), "NONE") == 0)
						   )
							set_current_special(1);
						// rule: A bot that rolled a slot casts it only if its gate (check_special)
						//   says yes; a gate refusal is traced 'special held'; a successful cast is
						//   the bot's act.
						// tests: LivingActFlow.a_cast_that_refuses_after_the_gates_yes_takes_the_ordinary_act_with_the_setting_on
						// tests: LivingActFlow.a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on
						if (check_special() )
						{
							// rule: A bot that rolled a slot casts it only if its gate
							//   (check_special) says yes; a gate refusal is traced 'special held';
							//   a successful cast is the bot's act.
							// tests: LivingActFlow.a_cast_that_refuses_after_the_gates_yes_takes_the_ordinary_act_with_the_setting_on
							// tests: LivingActFlow.a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on
							if (special())
								return 1;
						}
						else
							TRACE("special", "held id=%u family=%d slot=%d shift=%d team=%d",
							      static_cast<unsigned>(entity_id()), family(),
							      static_cast<int>(current_special()), shifter_down(), team_num());
						// rule: Classic: a special roll that the gate or the cast refused is still
						//   the bot's whole act (it stands still). Gameplay Fixes W: the refused
						//   roll is not the act; the bot takes the ordinary act the coin would have
						//   given it (seek, fire, search).
						// tests: LivingActFlow.a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on
						// tests: LivingActFlow.a_cast_that_refuses_after_the_gates_yes_takes_the_ordinary_act_with_the_setting_on
						if (og::sim::session_gameplay_fixes() == 0)
							return 0; // the classic act: the roll was the act, refused or not
						// Gameplay Fixes W: a roll the gate or the cast refused is
						// not the bot's act; it takes the act the coin would have
						// given it.
					}
					else       // do random walking ..
					{
						act_random();
						return 1;
					}
				}
				// rule: On its ordinary act a bot runs act_random (seek and fire) one time in five
				//   and the seek/search routine four times in five.
				// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
				if (!current_game->world->rng_.next(5) ) //1 in 5 to do act_random() function
					act_random();
				else // 4 of 5 times
				{
					// rule: Seeking: a bot without a foe looks for a near one; with a foe it snaps
					//   its facing to the nearest even (orthogonal) direction and searches for it
					//   for 300 steps; with none near it either finds the nearest foe anywhere
					//   (half the time) or wanders 20 steps.
					// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
					// tests: CoverageMisc.coverage_r19_living_act_random_acquires_a_foe_and_queues_a_command
					// tests: SemAi2.a_bot_with_no_foe_near_takes_the_nearest_foe_anywhere_on_the_coins_zero
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
						set_foe(current_game->world->find_nearest_foe(this));
					else
						stats_->try_random_walk(20);

					return 1;
				}
			}  // END RANDOM
			break;
		// rule: A walker with no known act type does nothing.
		// tests: LivingCombat.living_act_invisibility_skip_exit_and_action_command_paths
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

	// rule: A walker only shoves a living ally; two times in three the ally is shoved, never a
	//   seated hero (the rng(3) draw happens even for a seat).
	// tests: LivingCombat.living_shove_injects_walk_only_when_the_baby_step_is_passable
	// tests: LivingFuncs.living_shove_force_queues_walk_on_passable_step_and_refuses_blocked_step
	// tests: SemAi2.a_shove_needs_a_living_ally_that_is_not_a_seat_and_a_nonzero_roll
	if (target && !target->dead() && (query_order()==Order::Living) &&  //we are alive
	        (is_friendly(target)) // we are allied
	   )
		// Make sure WE don't get shoved
		// rule: A walker only shoves a living ally; two times in three the ally is shoved, never a
		//   seated hero (the rng(3) draw happens even for a seat).
		// tests: LivingCombat.living_shove_injects_walk_only_when_the_baby_step_is_passable
		// tests: LivingFuncs.living_shove_force_queues_walk_on_passable_step_and_refuses_blocked_step
		// tests: SemAi2.a_shove_needs_a_living_ally_that_is_not_a_seat_and_a_nonzero_roll
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
			// rule: A shove never steals an ally's orders when the step it would inject is blocked
			//   by terrain (the 2026-07-10 livelock fix): the ally keeps its own search.
			// tests: LivingCombat.living_shove_injects_walk_only_when_the_baby_step_is_passable
			// tests: LivingFuncs.living_shove_force_queues_walk_on_passable_step_and_refuses_blocked_step
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

	// rule: A walker steps only when already facing the step's direction; otherwise the walk just
	//   records the new facing and turns one notch (a seated hero walking by keys does not turn
	//   here, its act turns it, so it never turns twice in one tick).
	// tests: LivingCombat.living_walk_moves_on_matched_facing_and_only_turns_otherwise
	// tests: LivingFuncs.living_walk_moves_on_matching_facing_turns_otherwise_and_refuses_off_map
	// tests: SemAi2.a_walk_off_the_facing_turns_one_notch_but_a_seated_hero_only_records_it
	if (curdir() == dir)  // if continue direction
	{
		// check if off map
		// rule: A walk never leaves the map.
		// tests: LivingFuncs.living_walk_moves_on_matching_facing_turns_otherwise_and_refuses_off_map
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
			// rule: A step moves only if the destination is passable at its exact, rounded-up and
			//   rounded-down offsets (so a fractional step cannot clip a corner into trees or
			//   walls); the walk-cycle frame advances on a move.
			// tests: LivingCombat.living_walk_moves_on_matched_facing_and_only_turns_otherwise
			// tests: LivingFuncs.living_walk_moves_on_matching_facing_turns_otherwise_and_refuses_off_map
			// tests: SemAi2.a_step_moves_only_when_its_exact_and_rounded_offsets_are_passable
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
		else //Invalid move? // rule: A step moves only if the destination is passable at its exact,
			//   rounded-up and rounded-down offsets (so a fractional step cannot clip a corner into
			//   trees or walls).
			// tests: LivingCombat.living_walk_moves_on_matched_facing_and_only_turns_otherwise
			// tests: LivingFuncs.living_walk_moves_on_matching_facing_turns_otherwise_and_refuses_off_map
			// tests: SemAi2.a_step_moves_only_when_its_exact_and_rounded_offsets_are_passable
		{
			// Gameplay Fixes X: the shove below pushes the body THIS step met,
			// from where the walker stands now. collide_ob can be stale: a step
			// the grid stops never reaches the object probe (query_passable is
			// grid && object), so it still names the last body touched, however
			// far; and a probe that eats a teleporter moves the walker before it
			// meets the body it left behind. Off, the classic stale shove stands.
			// rule: A refused step shoves a living ally in the way. Classic: the ally named by
			//   collide_ob, which can be stale (the last body any probe touched, however far, or
			//   one left behind across a teleporter). Gameplay Fixes X: only a body this very step
			//   met is shoved.
			// tests: GameplayFixesWalk.a_walk_that_eats_a_teleporter_does_not_shove_the_ally_it_left
			// tests: GameplayFixesWalk.a_grid_stopped_fallback_does_not_shove_the_ally_the_full_step_met
			// tests: GameplayFixesWalk.a_body_the_step_meets_is_still_shoved
			// tests: SemAi2.a_refused_step_shoves_no_foe_and_no_non_living_body
			if (collide_ob() && !collide_ob()->dead()
			        && (og::sim::session_gameplay_fixes() == 0 || og::sim::step_meets(*this, x, y, *collide_ob())))
			{
				// rule: A refused step shoves a living ally in the way.
				// tests: GameplayFixesWalk.a_walk_that_eats_a_teleporter_does_not_shove_the_ally_it_left
				// tests: GameplayFixesWalk.a_grid_stopped_fallback_does_not_shove_the_ally_the_full_step_met
				// tests: GameplayFixesWalk.a_body_the_step_meets_is_still_shoved
				// tests: SemAi2.a_refused_step_shoves_no_foe_and_no_non_living_body
				if (collide_ob()->query_order() == Order::Living && is_friendly(collide_ob()) )
				{
					TRACE("shove", "walk dist=%d reach=%d", static_cast<int>(distance_to_ob(collide_ob())), static_cast<int>(static_cast<float>(sizex() + sizey() + collide_ob()->sizex() + collide_ob()->sizey()) + std::fabs(x) + std::fabs(y)) + 4); shove(collide_ob(), x, y);
				}
			}  // end hit some object
			// rule: A walker flagged always-animate (e.g. slime) cycles its walk frames even when
			//   its step is blocked.
			// tests: SemAi2.an_always_animate_walker_cycles_its_frames_on_a_refused_step
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
		// rule: A walker steps only when already facing the step's direction.
		// tests: LivingCombat.living_walk_moves_on_matched_facing_and_only_turns_otherwise
		// tests: LivingFuncs.living_walk_moves_on_matching_facing_turns_otherwise_and_refuses_off_map
		// tests: SemAi2.a_walk_off_the_facing_turns_one_notch_but_a_seated_hero_only_records_it
		if (this->act_type() != ACT_CONTROL || stats_->has_commands())
			turn(enddir());
	}
	return 1;
}

bool walkerIsAutoAttackable(walker* ob)
{
    Order order = ob->query_order();
    // rule: Walking into a living or a generator starts an attack; a weapon-order object (a door, a
    //   barrel) only if its family says it is auto-attackable; treasure and effects never.
    // tests: LivingFuncs.walkerIsAutoAttackable_true_for_every_living_family_and_order_discriminators
    // tests: LivingCombat.living_act_command_execution_and_autoattackable_edges
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
	// rule: A walker that bumps a live hostile (living, generator or auto-attackable weapon) swings
	//   at it; bumping an ally only records it as the collision body.
	// tests: LivingUnit.living_r11_collide_and_act_type_switches
	if ( ob && walkerIsAutoAttackable(ob) && (is_friendly(ob) == 0)
	        && !ob->dead() && !dead())
		init_fire();
	return 1;
}

walker* living::do_summon(char whatfamily, std::int32_t summon_lifetime)
{
	walker  *newob;

	newob = current_game->world->add_ob(Order::Living, whatfamily);
	// plumbing: add_ob failure (list full / bad family) returns nullptr from do_summon.
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
	// rule: A placed NPC with specials disabled by the scenario never decides to use a special.
	// tests: LivingFuncs.living_check_special_gates_disabled_flag_then_downgrades_unaffordable
	// tests: LivingCombat.living_check_special_denies_disabled_specials_and_falls_back_to_special_one
	if (specials_disabled())
		return false;

	set_shifter_down(static_cast<short>(current_game->world->rng_.next(2))); // on or off, randomly ..
	// New Specials: a bot cannot pay for a priced alternate -> no shift (never with the setting off).
	// rule: New Specials: a bot that tossed the Shift coin but cannot pay its slot's PRICED
	//   alternate (e.g. MINE) casts the plain slot instead; checked again after any fall-back to
	//   slot 1. Never with New Specials off.
	// tests: KitBots.unaffordable_priced_alternate_drops_the_shift_only_when_priced
	if (shifter_down() != 0 && og::sim::cannot_afford_priced_alternate(*this, static_cast<int>(current_special()))) set_shifter_down(0);
	// Make sure we have enough ..
	// rule: A bot that cannot pay for the slot it rolled falls back to its slot 1.
	// tests: LivingFuncs.living_check_special_gates_disabled_flag_then_downgrades_unaffordable
	// tests: KitBots.unaffordable_priced_alternate_drops_the_shift_only_when_priced
	if (stats_->magicpoints() < og::sim::cast_cost(*this, static_cast<int>(current_special()), shifter_down() != 0))
		set_current_special(1); // make us do default ..
	// ... and again for the default slot the line above may have fallen back to.
	// rule: New Specials: a bot that tossed the Shift coin but cannot pay its slot's PRICED
	//   alternate (e.g. MINE) casts the plain slot instead.
	// tests: KitBots.unaffordable_priced_alternate_drops_the_shift_only_when_priced
	if (shifter_down() != 0 && og::sim::cannot_afford_priced_alternate(*this, static_cast<int>(current_special()))) set_shifter_down(0);

	auto* fd = get_family_descriptor(family());
	const auto hook_result = og::script::hooks::check_special_ai(fd, this);
	// rule: The family's Lua bot gate (check_special_ai) can refuse the cast (no foe in reach,
	//   nothing to heal...); without a gate a bot always may.
	// tests: LivingActFlow.the_hit_response_draws_no_ordinary_act_after_a_gates_no
	// tests: KitBots.unaffordable_priced_alternate_drops_the_shift_only_when_priced
	// tests: LivingActFlow.the_act_path_never_reaches_an_unaffordable_slot
	if (hook_result.has_value() && !*hook_result)
		return false;
	// Gameplay Fixes AB (#409): a yes the bot cannot pay for is a no. The fold
	// above lands on slot 1 without asking slot 1's own price, and the hit
	// path (statistics::hit_response) has no "full of magic?" guard like the
	// act's, so its rng(3) bought a cast refused NOT ENOUGH MP. Asked after
	// the gate, which may have chosen the Shift arm. The act path never
	// reaches an unaffordable slot (its guard), so only the hit path moves.
	// rule: Gameplay Fixes AB: a gate's yes the bot cannot pay for (the slot-1 fall-back's own
	//   price, or the Shift arm the gate chose) is a no; classic said yes and the hit-response cast
	//   was then refused NOT ENOUGH MP.
	// tests: LivingActFlow.a_struck_bot_that_cannot_pay_its_slot_holds_with_the_setting_on
	if (og::sim::session_gameplay_fixes() != 0 &&
	    stats_->magicpoints() < og::sim::cast_cost(*this, static_cast<int>(current_special()), shifter_down() != 0))
		return false;
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
	// rule: A placed walker's level scales its stats: by its family's Lua coefficients when it has
	//   a set_difficulty hook, else by the default +11*L^2 HP and MP, +4*L damage, +2*L^2 armor.
	// tests: LivingFuncs.living_set_difficulty_soldier_applies_lua_coefficients_exactly
	// tests: LivingFuncs.living_set_difficulty_raises_max_hp_for_every_family_and_tops_up
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
	// rule: The difficulty percent scales max HP, max MP and damage of every placed walker, foes
	//   and placed team-0 NPCs alike (A12a); only a walker carrying a player character is exempt.
	//   At 100% team-0 NPCs skip the multiply (bit-exactness).
	// tests: DifficultyScaling.placed_team0_npcs_scale_with_difficulty
	// tests: DifficultyScaling.player_characters_stay_exempt_from_difficulty
	// tests: DifficultyScaling.hostile_team_legacy_scaling_unchanged
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

	// rule: A placed walker's HP regeneration rate comes from its level: whole points per tick from
	//   4*L^2/REGEN, and the remainder as one extra point every REGEN/(rem+1) ticks, never faster
	//   than every 2 ticks; regeneration starts with an empty delay.
	// tests: SemAi2.hp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one
	while (stats_->current_heal_delay() > REGEN)
	{
		stats_->set_current_heal_delay(	stats_->current_heal_delay() - REGEN);
		stats_->set_heal_per_round(	stats_->heal_per_round() + 1);
	} // this takes care of the integer part, now calculate the fraction

	// rule: A placed walker's HP regeneration rate comes from its level.
	// tests: SemAi2.hp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one
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
	// rule: A placed walker's HP regeneration rate comes from its level.
	// tests: SemAi2.hp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one
	if (stats_->max_heal_delay() < 2)
		stats_->set_max_heal_delay(2);



	// Set the magic delay ..
	stats_->set_max_magic_delay(REGEN);
	stats_->set_current_magic_delay(static_cast<std::int32_t>(levmult*30));//for calculation only

	// rule: A placed walker's MP regeneration rate comes from its level the same way, from 30*L^2.
	// tests: SemAi2.mp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one
	while (stats_->current_magic_delay() > REGEN)
	{
		stats_->set_current_magic_delay(	stats_->current_magic_delay() - REGEN);
		stats_->set_magic_per_round(	stats_->magic_per_round() + 1);
	} // this takes care of the integer part, now calculate the fraction

	// rule: A placed walker's MP regeneration rate comes from its level the same way, from 30*L^2.
	// tests: SemAi2.mp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one
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
	// rule: A placed walker's MP regeneration rate comes from its level the same way, from 30*L^2.
	// tests: SemAi2.mp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one
	if (stats_->max_magic_delay() < 2)
		stats_->set_max_magic_delay(2);

}

bool living::act_random()
{
	//  short newx, newy; // apparently not used anymore
	short xdist, ydist;

	// Find our foe
	// rule: act_random retargets to a near foe one time in 80 (or whenever it has none); with no
	//   foe near it wanders 40 steps.
	// tests: WalkerSpecials.act_guard_and_low_magic_act_random_pin_what_they_queue
	// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
	// tests: SemAi2Randomer.act_random_retargets_to_a_near_foe_one_time_in_eighty_or_when_it_has_none
	if (!current_game->world->rng_.next(80) || (!foe()))
		set_foe(current_game->world->find_near_foe(this));
	if (!foe())
		return stats_->try_random_walk(40);

	xdist = static_cast<short>(foe()->xpos() - xpos());
	ydist = static_cast<short>(foe()->ypos() - ypos());

	// If foe is in firing range, turn and fire
	// rule: act_random: a foe inside the line-of-sight box is fired at if a shot can reach it
	//   (COMMAND_FIRE for a random 0-23 count); a blocked shot turns toward the foe. Either way out
	//   of a shot, it searches 200 steps.
	// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
	// tests: SemAi2Randomer.act_random_fires_at_a_foe_in_sight_and_searches_for_one_out_of_it
	if (abs(xdist) < lineofsight() * GRID_SIZE &&
	        abs(ydist) < lineofsight() * GRID_SIZE)
	{
		// rule: act_random: a foe inside the line-of-sight box is fired at if a shot can reach it
		//   (COMMAND_FIRE for a random 0-23 count); a blocked shot turns toward the foe.
		// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
		// tests: SemAi2Randomer.act_random_fires_at_a_foe_in_sight_and_searches_for_one_out_of_it
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

	// plumbing: do_action is only called with action() set; the guard returns 0 for direct callers.
	if (!action())
		return 0;

	// rule: An ally on Follow (Summon Defense) fights its own foe if it has one; otherwise it
	//   follows the nearest player hero and takes up that hero's foe; with no foe anywhere it
	//   queues a 5-step follow. Unknown actions do nothing.
	// tests: LivingCombat.living_walk_and_do_action_edge_branches
	// tests: LivingCombat.living_do_action_follow_leader_null_and_command_paths
	// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
	// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
	// tests: LivingUnit.living_r11_act_owner_dead_and_action_follow
	switch (action())
	{
		case ACTION_FOLLOW: // follow our leader, attack his targets ..
			// rule: An ally on Follow (Summon Defense) fights its own foe if it has one.
			// tests: LivingCombat.living_walk_and_do_action_edge_branches
			// tests: LivingCombat.living_do_action_follow_leader_null_and_command_paths
			// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
			// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
			// tests: LivingUnit.living_r11_act_owner_dead_and_action_follow
			if (foe())
				return 0;       // continue as normal
			set_leader(current_game->world->find_nearest_player(this));
			// rule: An ally on Follow (Summon Defense) fights its own foe if it has one.
			// tests: LivingCombat.living_walk_and_do_action_edge_branches
			// tests: LivingCombat.living_do_action_follow_leader_null_and_command_paths
			// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
			// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
			// tests: LivingUnit.living_r11_act_owner_dead_and_action_follow
			if (!leader())
				return 0;       // continue as normal ... shouldn't happen
			// rule: An ally on Follow (Summon Defense) fights its own foe if it has one.
			// tests: LivingCombat.living_walk_and_do_action_edge_branches
			// tests: LivingCombat.living_do_action_follow_leader_null_and_command_paths
			// tests: LivingUnit.follow_ally_spends_its_act_on_the_follow_command_not_its_ai
			// tests: LivingCombat.living_round7_act_random_and_do_action_targeted_branches
			// tests: LivingUnit.living_r11_act_owner_dead_and_action_follow
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
