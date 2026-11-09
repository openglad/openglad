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
// Stats.cpp
//

#include <openglad/gameplay/fearless.h>               // og::sim::fearless (New Specials)
#include <openglad/gameplay/statistics.h>      // for bit flags, etc.
#include <openglad/core/util.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/combat_math.h>         // kFreezeThawImmunityTicks
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/dirty_field_bits.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/families/specials_view.h>
// find_follow_leader is defined in the SDL build (screen.cpp) and stubbed
// by the text client. Returns a walker to follow, or nullptr if none.
walker* find_follow_leader();
#include <cmath>
#include <cstdint>
#include <format>
#include <openglad/gameplay/sim_event_log.h>

using Uint32 = std::uint32_t;
using Sint32 = std::int32_t;

static inline Uint32 rng(Uint32 max_exclusive) {
    return current_game->world->rng_.next(max_exclusive);
}

inline constexpr int CHECK_STEP_SIZE = 1; // (controller->stepsize()) // was 1

// COMMAND declarations defined at bottom

statistics::statistics(walker  * someguy)
{
	short i;

	// plumbing: statistics constructor null-controller guard: defaults (Order::Living,
	//   FAMILY_SOLDIER, no controller id) and a Log line; no game rule
	if (someguy)
	{
		controller_ = someguy;
		owner_ = someguy;
		controller_id_ = someguy->entity_id();
	}
	else
	{
		controller_ = nullptr;
		owner_ = nullptr;
		controller_id_ = 0;
		Log("made a stats with no controller!\n");
	}
	hitpoints_ = max_hitpoints_ = 10;
	magicpoints_ = max_magicpoints_ = 50;
	level_ = 1;
	armor_ = 0; // no default armor
	max_heal_delay_ = current_heal_delay_ = 1000;  // default of 50 cycles / hitpoint
	max_magic_delay_ = current_magic_delay_ = 1000;
	magic_per_round_ = 0.0f; // default to not regenerating magic
	heal_per_round_ = 0.0f;  // default to not regenerating hitpoints

	bit_flags_ = 0;           // clear all the bit flags
	frozen_delay_ = 0;     // start out able to move :)
	// plumbing: constructor zeroes special_cost_ for every special slot; initialisation
	for (i=0; i < NUM_SPECIALS; i++)
		special_cost_[i] = 0; // low default special ability cost in magicpoints
	weapon_cost_ = 0;

	delete_me_ = 0;

	// AI finding routine values ..
	last_distance_ = 15000;
	current_distance_ = 15000;

    // plumbing: statistics constructor null-controller guard: defaults (Order::Living,
    //   FAMILY_SOLDIER, no controller id) and a Log line; no game rule
    if(controller_ != nullptr)
    {
        old_order_ = controller_->query_order();
        old_family_ = controller_->family();
    }
    else
    {
        old_order_ = Order::Living;
        old_family_ = FAMILY_SOLDIER;
    }

	name.clear(); // set to null string
}

statistics::~statistics()
{
	controller_ = nullptr;
	controller_id_ = 0;
	owner_ = nullptr;
	delete_me_ = 1;
}

void statistics::mark_dirty(std::uint8_t bit_index)
{
	// plumbing: dirty-bit bookkeeping for snapshot replication (owner falls back to controller;
	//   null guard)
	walker* dirty_owner = owner_ != nullptr ? owner_ : controller_;
	if (dirty_owner != nullptr)
		dirty_owner->mark_dirty(bit_index);
}

void statistics::set_controller(walker* value)
{
	walker* dirty_owner = owner_;

	controller_ = value;
	// plumbing: dirty-bit bookkeeping for snapshot replication (owner falls back to controller;
	//   null guard)
	controller_id_ = (value != nullptr) ? value->entity_id() : 0;

	// plumbing: dirty-bit bookkeeping for snapshot replication (owner falls back to controller;
	//   null guard)
	if (dirty_owner != nullptr)
		dirty_owner->mark_dirty(og::dirty::BIT_CONTROLLER_ID);
}

// Empties the AI command queue and resets the weapon and leader. It does NOT
// touch real_team_num: charm ends on its charm_left timer (living.cpp,
// "Charmed-ness") and nowhere else (#317). Until 2026-10 this routine also
// restored the real team, so any routine AI clear (a hit from a new attacker
// here or in the archer's Lua hit_response hook, a rush, an ally's shove,
// fire_check, walk_to_foe) ended a charm early and left charm_left counting
// down to nothing. Deliberate resets un-charm at their own site: respawn
// (respawn.cpp, revive_player_walker) and every level or lineup rebuild,
// which creates fresh walkers with real_team_num 255 (create_team_walker in
// headless_server_runtime.cpp, lineup.lua).
void statistics::clear_command()
{
	commands.clear();
	// Make sure our weapon type is restored to normal ..
	controller_->set_current_weapon(controller_->default_weapon());
	controller_->set_leader(nullptr);
}

// Character-switch / control-claim variant of clear_command (runaway-specials
// §4). The legacy blanket clear on switch was the laundering exploit: two
// SwitchChar presses wiped any scare/knockback and silently un-charmed.
//  1. The LEADING run of forced COMMAND_WALK entries (fright/knockback/flee)
//     is preserved — a front walk command IS the fleeing state (see
//     score_panel.cpp), so the HUD flee countdown keeps working and now
//     survives switches. Everything queued behind the run is erased.
//  2. The weapon reset and leader clear are KEPT (legacy switch hygiene).
//  3. real_team_num is NOT restored — charm survives control switches; charm
//     expiry belongs solely to charm_left decay (living.cpp), and the
//     switch-cycle filter already refuses to switch INTO a charmed walker.
// Extensionally identical to clear_command whenever the queue has no leading
// forced entries. Since #317 neither clear touches real_team_num: a charm ends
// on its charm_left timer (living.cpp, "Charmed-ness") and nowhere else. The
// parity test driver (scenario_runtime.cpp) and the golden capture tool
// (parity_dump_master.cpp) spawn with real_team_num = team; the walker's own
// first act() runs the decay and resets it to 255 (team unchanged).
void statistics::clear_command_for_control_switch()
{
	int preserved = 0;
	auto keep_end = commands.begin();
	// rule: Switching control onto (or claiming) a character keeps the leading run of externally
	//   forced walks (fright, knockback, yell-flee, shove) and erases everything queued behind it;
	//   the weapon resets to default and the leader clears; charm (real_team_num) is untouched.
	//   With no forced front it is the plain clear_command.
	// tests: StatsFright.selective_clear_preserves_forced_run_keeps_hygiene_and_charm
	// tests: StatsFright.selective_clear_without_forced_front_wipes_queue
	// tests: StatsFright.switch_and_claim_preserve_fright_and_hygiene
	// tests: StatsFright.claim_path_no_longer_uncharms
	// tests: SemAi3.a_control_switch_keeps_only_a_leading_run_of_forced_walks
	while (keep_end != commands.end() && keep_end->forced
	       && keep_end->commandtype == COMMAND_WALK)
	{
		++keep_end;
		++preserved;
	}
	if (preserved > 0)
		TRACE("stats", "selective clear preserved %d forced entries", preserved);
	commands.erase(keep_end, commands.end());
	// Make sure our weapon type is restored to normal ..
	controller_->set_current_weapon(controller_->default_weapon());
	controller_->set_leader(nullptr);
}

// Player-side frozen_delay drain step (sim_input_handler.cpp). Identical
// 1/tick decrement to the AI drains, EXCEPT the 1 -> 0 transition writes
// -kFreezeThawImmunityTicks (runaway-specials §3.3): the negative raw phase
// is a thaw-immunity window — the masked getter reads 0 (the hero can act),
// freeze re-application is refused at the apply sites, and living::act
// climbs the raw value +1/tick back to 0. Only this player-side drain writes
// the negative phase; the AI drains (living.cpp/walker.cpp) thaw to 0 as
// always and their masked-getter guards skip negatives with zero edits.
void statistics::player_thaw_tick()
{
	const short cur = frozen_delay(); // masked; call site guards cur > 0
	// rule: A frozen player-controlled hero thaws one tick at a time; on the last frozen tick it
	//   enters a thaw-immunity window (raw frozen_delay = -kFreezeThawImmunityTicks) during which
	//   it can act but cannot be refrozen; AI walkers thaw straight to 0.
	// tests: StatsFright.player_thaw_tick_writes_immunity_on_last_tick
	// tests: StatsFright.sim_player_thaw_immunity_window
	// tests: StatsFright.living_act_climbs_immunity_and_ai_drain_thaws_to_zero
	if (cur > 1)
		set_frozen_delay(static_cast<short>(cur - 1));
	else if (cur == 1)
	{
		TRACE("stats", "thaw immunity: wrote -%d on the 1->0 transition",
		      og::combat::kFreezeThawImmunityTicks);
		set_frozen_delay(static_cast<short>(-og::combat::kFreezeThawImmunityTicks));
	}
}

void statistics::add_command(Sint32 whatcommand, Sint32 iterations,
                             Sint32 info1, Sint32 info2)
{

	// rule: Queuing a DIE order marks the walker for deletion immediately instead of queueing
	//   anything.
	// tests: StatsCoverage.stats_set_and_try_command_random_walk_paths
	// tests: SemAi3.queuing_a_die_order_marks_the_walker_for_deletion_and_queues_nothing
	if (whatcommand == COMMAND_DIE)
	{
		set_delete_me(1);
		return;
	}

	// plumbing: Log-only branches (FOLLOW 'following', set_command DIE log line); no sim effect
	if (whatcommand == COMMAND_FOLLOW)
	{
		Log("following\n");
	}

	// Add command to end of list
	commands.emplace_back();

	// rule: A queued walk is always a unit step in one of the 8 directions: deltas are clamped to
	//   -1..1 and a (0,0) walk becomes a down-right (1,1) walk. Same rule as below
	//   (force_command) and below (force_fright merge). Consequence: a random walk that
	//   rolls (0,0) walks south-east, so random walks are biased toward SE (see below).
	// tests: StatsMorePaths.stats_add_and_force_command_walk_clamp_and_zero_fallback
	// tests: StatsUnit.stats_r14_lines_122_133_135_155_161_add_force_walk_clamps
	// tests: StatsMorePaths.stats_round12_add_command_walk_clamps_and_follow_shortcuts
	// tests: KitCaptain.grunts_turn_to_the_new_banner
	if (whatcommand == COMMAND_WALK)
	{
		// rule: A queued walk is always a unit step in one of the 8 directions: deltas are clamped
		//   to -1..1 and a (0,0) walk becomes a down-right (1,1) walk.
		// tests: StatsMorePaths.stats_add_and_force_command_walk_clamp_and_zero_fallback
		// tests: StatsUnit.stats_r14_lines_122_133_135_155_161_add_force_walk_clamps
		// tests: StatsMorePaths.stats_round12_add_command_walk_clamps_and_follow_shortcuts
		// tests: KitCaptain.grunts_turn_to_the_new_banner
		if (info1 > 1)
			info1 = 1;
		// rule: A queued walk is always a unit step in one of the 8 directions: deltas are clamped
		//   to -1..1 and a (0,0) walk becomes a down-right (1,1) walk.
		// tests: StatsMorePaths.stats_add_and_force_command_walk_clamp_and_zero_fallback
		// tests: StatsUnit.stats_r14_lines_122_133_135_155_161_add_force_walk_clamps
		// tests: StatsMorePaths.stats_round12_add_command_walk_clamps_and_follow_shortcuts
		// tests: KitCaptain.grunts_turn_to_the_new_banner
		if (info1 < -1)
			info1 = -1;
		// rule: A queued walk is always a unit step in one of the 8 directions: deltas are clamped
		//   to -1..1 and a (0,0) walk becomes a down-right (1,1) walk.
		// tests: StatsMorePaths.stats_add_and_force_command_walk_clamp_and_zero_fallback
		// tests: StatsUnit.stats_r14_lines_122_133_135_155_161_add_force_walk_clamps
		// tests: StatsMorePaths.stats_round12_add_command_walk_clamps_and_follow_shortcuts
		// tests: KitCaptain.grunts_turn_to_the_new_banner
		if (info2 > 1)
			info2 = 1;
		// rule: A queued walk is always a unit step in one of the 8 directions: deltas are clamped
		//   to -1..1 and a (0,0) walk becomes a down-right (1,1) walk.
		// tests: StatsMorePaths.stats_add_and_force_command_walk_clamp_and_zero_fallback
		// tests: StatsUnit.stats_r14_lines_122_133_135_155_161_add_force_walk_clamps
		// tests: StatsMorePaths.stats_round12_add_command_walk_clamps_and_follow_shortcuts
		// tests: KitCaptain.grunts_turn_to_the_new_banner
		if (info2 < -1)
			info2 = -1;
		// rule: A queued walk is always a unit step in one of the 8 directions: deltas are clamped
		//   to -1..1 and a (0,0) walk becomes a down-right (1,1) walk.
		// tests: StatsMorePaths.stats_add_and_force_command_walk_clamp_and_zero_fallback
		// tests: StatsUnit.stats_r14_lines_122_133_135_155_161_add_force_walk_clamps
		// tests: StatsMorePaths.stats_round12_add_command_walk_clamps_and_follow_shortcuts
		// tests: KitCaptain.grunts_turn_to_the_new_banner
		if (!info1 && !info2)
			info1 = info2 = 1;
	}
	commands.back().com1 = info1;
	commands.back().com2 = info2;
	commands.back().commandtype = whatcommand;
	commands.back().commandcount = iterations;
}

void statistics::force_command(Sint32 whatcommand, Sint32 iterations,
                               Sint32 info1, Sint32 info2)
{
	// Add command to start of list
	commands.emplace_front();

	// rule: A forced (prepended) walk obeys the same unit-step rule as add_command (above)
	//   and is the only producer of the 'forced' flag (fright/knockback/flee/shove).
	// tests: StatsCommands.stats_force_command_prepends_a_clamped_forced_walk
	// tests: StatsFright.forced_flag_set_only_by_force_command
	// tests: StatsExtended.statistics_force_command_walk_clamps
	// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
	// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
	// tests: LivingCombat.living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight
	if (whatcommand == COMMAND_WALK)
	{
		// rule: A forced (prepended) walk obeys the same unit-step rule as add_command
		//   (above) and is the only producer of the 'forced' flag
		//   (fright/knockback/flee/shove).
		// tests: StatsCommands.stats_force_command_prepends_a_clamped_forced_walk
		// tests: StatsFright.forced_flag_set_only_by_force_command
		// tests: StatsExtended.statistics_force_command_walk_clamps
		// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
		// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
		// tests: LivingCombat.living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight
		if (info1 > 1)
			info1 = 1;
		// rule: A forced (prepended) walk obeys the same unit-step rule as add_command
		//   (above) and is the only producer of the 'forced' flag
		//   (fright/knockback/flee/shove).
		// tests: StatsCommands.stats_force_command_prepends_a_clamped_forced_walk
		// tests: StatsFright.forced_flag_set_only_by_force_command
		// tests: StatsExtended.statistics_force_command_walk_clamps
		// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
		// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
		// tests: LivingCombat.living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight
		if (info1 < -1)
			info1 = -1;
		// rule: A forced (prepended) walk obeys the same unit-step rule as add_command
		//   (above) and is the only producer of the 'forced' flag
		//   (fright/knockback/flee/shove).
		// tests: StatsCommands.stats_force_command_prepends_a_clamped_forced_walk
		// tests: StatsFright.forced_flag_set_only_by_force_command
		// tests: StatsExtended.statistics_force_command_walk_clamps
		// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
		// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
		// tests: LivingCombat.living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight
		if (info2 > 1)
			info2 = 1;
		// rule: A forced (prepended) walk obeys the same unit-step rule as add_command
		//   (above) and is the only producer of the 'forced' flag
		//   (fright/knockback/flee/shove).
		// tests: StatsCommands.stats_force_command_prepends_a_clamped_forced_walk
		// tests: StatsFright.forced_flag_set_only_by_force_command
		// tests: StatsExtended.statistics_force_command_walk_clamps
		// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
		// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
		// tests: LivingCombat.living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight
		if (info2 < -1)
			info2 = -1;
		// rule: A forced (prepended) walk obeys the same unit-step rule as add_command
		//   (above) and is the only producer of the 'forced' flag
		//   (fright/knockback/flee/shove).
		// tests: StatsCommands.stats_force_command_prepends_a_clamped_forced_walk
		// tests: StatsFright.forced_flag_set_only_by_force_command
		// tests: StatsExtended.statistics_force_command_walk_clamps
		// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
		// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
		// tests: LivingCombat.living_act_guard_wakes_and_fires_only_when_a_foe_is_in_sight
		if (!info1 && !info2)
			info1 = info2 = 1;
	}
	commands.front().com1 = info1;
	commands.front().com2 = info2;
	commands.front().commandtype = whatcommand;
	commands.front().commandcount = iterations;
	// Externally-forced entry (fright/knockback/flee/shove). ONLY this
	// injector sets the flag; add_command leaves it false. The flag is never
	// serialized: snapshots clear queues on apply, and saves/replays never
	// persist commands.
	commands.front().forced = true;
}

// Ghost-scare fright injection (runaway-specials §3.2). If the queue front is
// already an externally-forced walk (a prior scare, a bomb knockback, a
// yell-flee), a fresh scare MERGES into it: the remaining count refreshes to
// max(existing, new) and the flee direction re-points to the new (dx,dy) —
// overlapping scares never sum end-to-end, so the TOTAL queued fright can
// never exceed one cast's value. Otherwise this falls through to the exact
// legacy force_command prepend (including degenerate iteration values after
// the myguy rng(con) subtraction), so single casts on fresh victims stay
// byte-identical to master.
//
// Documented cross-effect collateral: a scare landing while a bomb-knockback
// entry sits at front merges into it — the count becomes the scare value and
// the direction becomes away-from-ghost. Accepted on purpose: replace
// semantics would let an 8-tick knock truncate a 364-tick scare, and a new
// command type has too much blast radius. ONLY ghost scare calls this; every
// other forced-walk user keeps exact legacy prepend behavior.
void statistics::force_fright(Sint32 iterations, Sint32 info1, Sint32 info2)
{
	// rule: Overlapping scares (ghost scare, banner fright) never stack end to end: a scare landing
	//   on a walker already in a forced walk merges into it, keeping the longer remaining count and
	//   re-pointing the flee to the new direction; otherwise the scare is prepended as a fresh
	//   forced walk.
	// tests: StatsFright.force_fright_merges_max_remaining_and_repoints
	// tests: StatsFright.force_fright_falls_through_to_legacy_prepend
	// tests: StatsMorePaths.force_fright_clamps_a_merged_direction_to_the_unit_square
	if (!commands.empty() && commands.front().forced
	    && commands.front().commandtype == COMMAND_WALK)
	{
		// Clamp the deltas exactly like force_command's COMMAND_WALK branch.
		// rule: The merged fright direction obeys the unit-step walk rule (same as above).
		// tests: StatsMorePaths.force_fright_clamps_a_merged_direction_to_the_unit_square
		// tests: StatsFright.force_fright_merges_max_remaining_and_repoints
		if (info1 > 1)
			info1 = 1;
		if (info1 < -1)
			info1 = -1;
		if (info2 > 1)
			info2 = 1;
		if (info2 < -1)
			info2 = -1;
		if (!info1 && !info2)
			info1 = info2 = 1;
		command& front = commands.front();
		// rule: Overlapping scares (ghost scare, banner fright) never stack end to end.
		// tests: StatsFright.force_fright_merges_max_remaining_and_repoints
		// tests: StatsFright.force_fright_falls_through_to_legacy_prepend
		// tests: StatsMorePaths.force_fright_clamps_a_merged_direction_to_the_unit_square
		if (iterations > front.commandcount)
			front.commandcount = iterations;
		front.com1 = info1;
		front.com2 = info2;
		TRACE("stats", "force_fright merged: count=%d dir=(%d,%d)",
		      static_cast<int>(front.commandcount), static_cast<int>(front.com1),
		      static_cast<int>(front.com2));
		return;
	}
	force_command(COMMAND_WALK, iterations, info1, info2);
}

bool statistics::has_commands() const
{
	return !commands.empty();
}


// try_command will only set a command if there is
// none in the queue
// ^ never true: the body has appended unconditionally and returned 0
// since the 2002 initial revision (9990ec7d stats.cpp:184-189). The
// invariant lives in the callers (living::act living.cpp:314,
// walker::act walker.cpp:888 both consume do_command() first; the
// stats.cpp:1317 site clear_command()s), and it is only approximate:
// 560 of the 4509 try_command calls in the 267-scenario parity corpus
// arrive with a non-empty queue. Making the body match this sentence is
// a sim behaviour change that WOULD move goldens, not a cleanup.

short statistics::try_random_walk(Sint32 iterations)
{
	return try_command(COMMAND_WALK, iterations, static_cast<Sint32>(rng(3)) - 1, static_cast<Sint32>(rng(3)) - 1);
}

short statistics::try_command(Sint32 whatcommand, Sint32 iterations,
                              Sint32 info1, Sint32 info2)
{
	add_command(whatcommand, iterations, info1, info2);
	return 0;
}

void statistics::set_command(Sint32 whatcommand, Sint32 iterations)
{
	// rule: A RANDOM_WALK order becomes a forced walk in a random direction (two rng(3)-1 draws; a
	//   (0,0) roll becomes SE per above); any other order is forced with zero arguments.
	// tests: SemAi3.a_random_walk_order_becomes_a_forced_walk_in_a_rolled_direction
	if (whatcommand == COMMAND_RANDOM_WALK)
		set_command(COMMAND_WALK, iterations, static_cast<Sint32>(rng(3)) - 1, static_cast<Sint32>(rng(3)) - 1);
	else
		set_command(whatcommand, iterations, 0, 0);
}

void statistics::set_command(Sint32 whatcommand, Sint32 iterations,
                             Sint32 info1, Sint32 info2)
{
	// plumbing: Log-only branches (FOLLOW 'following', set_command DIE log line); no sim effect
	if (whatcommand == COMMAND_DIE)
		Log("BLLLLLLLLLLLLLAAAAAAAAAAHHHHHHH!\n");

	force_command(whatcommand, iterations, info1, info2);

}

// Defined below (the right-hand-walk section); COMMAND_ATTACK's coincident-
// foe clinch breaker reuses the same facing->unit-step table.
static void facing_step_delta(char dir, short& xdelta, short& ydelta);

// Do the current command
short statistics::do_command()
{
	Sint32 commandtype, com1, com2;
	short i;
	
	walker * target;
	short deltax, deltay;
	Sint32 distance;

	short newx = 0;
	short newy = 0;
#ifdef PROFILING

	profiler docommprofiler("stats::do_command");
#endif

	// plumbing: do_command with no controller returns 0 (null guard)
	if (!controller_) // allow dead controllers for now
	{
		Log("STATS:DO_COM: No controller!\n");
		// The 2013 code waited on a keypress here (wait_for_key); the
		// headless split dropped the call and the function is gone now.
		return 0;
	}

	// Get next command;
	// rule: A walker with no queued orders does nothing in do_command and returns 0, which tells
	//   living::act / walker::act to fall through to its own AI think.
	// tests: StatsCoverage.stats_round6_walk_clamp_extremes_and_empty_queue_paths
	// tests: StatsCommands.stats_do_command_walk_steps_once_and_counts_down
	if (commands.empty())
        return 0;
    
	commandtype = commands.front().commandtype;
	com1 = commands.front().com1;
	com2 = commands.front().com2;
    
    short result = 1;

	// plumbing: command-type dispatch switch; each case has its own rule row (below, 412,
	//   426, 432, 479, 483, 493, 505, 523, 529, 537, 540, 551, 571, 591, 633)
	switch (commandtype)
	{
		// rule: A WALK order takes one step (walkstep, which may turn instead of moving if not
		//   facing that way) in its stored direction per round.
		// tests: StatsCommands.stats_do_command_walk_steps_once_and_counts_down
		case COMMAND_WALK:
			controller_->walkstep(com1, com2);
			break;
			// rule: A FIRE order only fires for a living walker whose fire_check allows the shot in
			//   (com1,com2); a refused shot cancels the order and reports 0 so the AI thinks again
			//   this tick.
			// tests: StatsExtended.do_command_fire_denied_by_fire_check_returns_0_and_drops_the_command
			// tests: StatsExtended.do_command_fire_that_passes_fire_check_starts_the_shot_and_returns_1
			// tests: StatsMorePaths.stats_round13_die_and_non_living_fire_command_branches
			// tests: StatsCommands.stats_do_command_fire_denied_without_foe_and_spends_busy_when_allowed
			case COMMAND_FIRE:
			// rule: A FIRE order only fires for a living walker whose fire_check allows the shot in
			//   (com1,com2).
			// tests: StatsExtended.do_command_fire_denied_by_fire_check_returns_0_and_drops_the_command
			// tests: StatsExtended.do_command_fire_that_passes_fire_check_starts_the_shot_and_returns_1
			// tests: StatsMorePaths.stats_round13_die_and_non_living_fire_command_branches
			// tests: StatsCommands.stats_do_command_fire_denied_without_foe_and_spends_busy_when_allowed
			if (!(controller_->query_order() == Order::Living))
			{
				Log("commanding a non-living to fire?");
				break;
			}
				// rule: A FIRE order only fires for a living walker whose fire_check allows the
				//   shot in (com1,com2).
				// tests: StatsExtended.do_command_fire_denied_by_fire_check_returns_0_and_drops_the_command
				// tests: StatsExtended.do_command_fire_that_passes_fire_check_starts_the_shot_and_returns_1
				// tests: StatsMorePaths.stats_round13_die_and_non_living_fire_command_branches
				// tests: StatsCommands.stats_do_command_fire_denied_without_foe_and_spends_busy_when_allowed
				if (!controller_->fire_check(static_cast<short>(com1), static_cast<short>(com2)))
				{
					commands.front().commandcount = 0;
					result = 0;
					break;
				}
				controller_->init_fire(static_cast<short>(com1), static_cast<short>(com2));
				break;
		// rule: A queued DIE order (debug, unused by the game) deletes the walker on its last
		//   iteration; earlier iterations only count down.
		// tests: StatsCommands.stats_do_command_die_sets_delete_me
		// tests: StatsMorePaths.stats_do_command_die_and_multido_paths
		// tests: StatsCoverage.follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me
		case COMMAND_DIE:  // debugging, not currently used
			// rule: A queued DIE order (debug, unused by the game) deletes the walker on its last
			//   iteration; earlier iterations only count down.
			// tests: StatsCommands.stats_do_command_die_sets_delete_me
			// tests: StatsMorePaths.stats_do_command_die_and_multido_paths
			// tests: StatsCoverage.follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me
			if (!controller_->dead())
				Log("Trying to make a living ob die!\n");
			// rule: A queued DIE order (debug, unused by the game) deletes the walker on its last
			//   iteration; earlier iterations only count down.
			// tests: StatsCommands.stats_do_command_die_sets_delete_me
			// tests: StatsMorePaths.stats_do_command_die_and_multido_paths
			// tests: StatsCoverage.follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me
			if (commands.front().commandcount < 2)  // then delete us ..
				set_delete_me(1);
			break;
		// rule: A follower drops a FOLLOW order the moment it has a foe (fighting beats following)
		//   and when no leader can be found (find_follow_leader: the view's controlled hero);
		//   otherwise it adopts the found leader.
		// tests: StatsCoverage.follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me
		// tests: StatsNavigation.follow_picks_the_view_leader_and_steps_one_normalized_unit
		case COMMAND_FOLLOW:   // follow the leader
			// rule: A follower drops a FOLLOW order the moment it has a foe (fighting beats
			//   following) and when no leader can be found (find_follow_leader.
			// tests: StatsCoverage.follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me
			// tests: StatsNavigation.follow_picks_the_view_leader_and_steps_one_normalized_unit
			if (controller_->foe()) // if we have foe, don't follow this round
			{
				commands.front().commandcount = 0;
				controller_->set_leader(nullptr);
				result = 0;
				break;
			}
			// rule: A follower drops a FOLLOW order the moment it has a foe (fighting beats
			//   following) and when no leader can be found (find_follow_leader.
			// tests: StatsCoverage.follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me
			// tests: StatsNavigation.follow_picks_the_view_leader_and_steps_one_normalized_unit
			if (!controller_->leader())
			{
				controller_->set_leader(find_follow_leader());
				// rule: A follower drops a FOLLOW order the moment it has a foe (fighting beats
				//   following) and when no leader can be found (find_follow_leader.
				// tests: StatsCoverage.follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me
				// tests: StatsNavigation.follow_picks_the_view_leader_and_steps_one_normalized_unit
				if (!controller_->leader())
				{
					commands.front().commandcount = 0;
					result = 0;
					break;
				}
			}
			
			// Do we have a leader now?
			// rule: A follower stops (and lets go of the leader) when within 60 px of it, so
			//   followers never crowd onto the leader.
			// tests: StatsMorePaths.follow_walks_one_unit_step_toward_the_leader_and_drops_it_inside_60px
			if(controller_->leader())
			{
				distance = controller_->distance_to_ob(controller_->leader());
				// rule: A follower stops (and lets go of the leader) when within 60 px of it, so
				//   followers never crowd onto the leader.
				// tests: StatsMorePaths.follow_walks_one_unit_step_toward_the_leader_and_drops_it_inside_60px
				if (distance < 60)
				{
					controller_->set_leader(nullptr);
					result = 1;  // don't get too close
					break;
				}
				newx = static_cast<short>(controller_->leader()->xpos() - controller_->xpos()); // total horizontal distance..
				newy = static_cast<short>(controller_->leader()->ypos() - controller_->ypos());
				// rule: Follow direction snaps to an axis when one delta dominates 3:1, otherwise
				//   diagonal; each axis is a unit step. Same 3:1 snap as COMMAND_ATTACK
				//   (below), direct_walk (below), right_walk (below),
				//   direct_walk_to_point (below).
				// tests: StatsNavigation.follow_picks_the_view_leader_and_steps_one_normalized_unit
				// tests: StatsMorePaths.follow_walks_one_unit_step_toward_the_leader_and_drops_it_inside_60px
				// tests: StatsMorePaths.stats_round11_follow_force_walk_and_right_walk_distance_branches
				if (abs(newx) > abs(3*newy))
					newy = 0;
				if (abs(newy) > abs(3*newx))
					newx = 0;
				if (newx)                      // If it's not 0, then get
					newx = static_cast<short>(newx / abs(newx));     // the normal of it..
				if (newy)
					newy = static_cast<short>(newy / abs(newy));
			}  // end of if we had a foe ..
			
			controller_->walkstep(newx, newy);
			// rule: On the last iteration of a FOLLOW order the follower lets go of its leader.
			// tests: StatsNavigation.follow_picks_the_view_leader_and_steps_one_normalized_unit
			if (commands.front().commandcount < 2)
            {
				controller_->set_leader(nullptr);
            }
			break;
		// rule: A QUICK_FIRE order steps and fires in the same round (no fire_check).
		// tests: StatsCommands.stats_do_command_quick_fire_walks_and_fires_in_one_tick
		// tests: StatsMorePaths.stats_round14_quickfire_multido_rush_and_walk_to_foe_melee_short_circuit
		case COMMAND_QUICK_FIRE:
			controller_->walkstep(com1, com2);
			controller_->fire();
			break;
			// rule: MULTIDO(n) consumes itself and immediately runs the next n queued commands in
			//   the same round (each with its own countdown); n <= 0 runs none; it stops early when
			//   the queue empties.
			// tests: StatsMorePaths.multido_runs_the_next_two_queued_commands_in_one_round
			// tests: StatsMorePaths.stats_round14_quickfire_multido_rush_and_walk_to_foe_melee_short_circuit
			// tests: SemAi3.a_multido_of_zero_or_less_consumes_itself_and_runs_nothing
			case COMMAND_MULTIDO: // Lets you do <com1> commands in one round
				{
					// rule: MULTIDO(n) consumes itself and immediately runs the next n queued
					//   commands in the same round (each with its own countdown).
					// tests: StatsMorePaths.multido_runs_the_next_two_queued_commands_in_one_round
					// tests: StatsMorePaths.stats_round14_quickfire_multido_rush_and_walk_to_foe_melee_short_circuit
					// tests: SemAi3.a_multido_of_zero_or_less_consumes_itself_and_runs_nothing
					const Sint32 repeats = (com1 > 0) ? com1 : 0;
					// Consume MULTIDO first; otherwise do_command() would recurse on
					// the same front command indefinitely.
					commands.pop_front();
					// rule: MULTIDO(n) consumes itself and immediately runs the next n queued
					//   commands in the same round (each with its own countdown).
					// tests: StatsMorePaths.multido_runs_the_next_two_queued_commands_in_one_round
					// tests: StatsMorePaths.stats_round14_quickfire_multido_rush_and_walk_to_foe_melee_short_circuit
					// tests: SemAi3.a_multido_of_zero_or_less_consumes_itself_and_runs_nothing
					for (i = 0; i < repeats && !commands.empty(); i++)
						do_command();
					return 1;
				}
		// rule: A fighter's rush (charge special) moves a living walker three steps per round in
		//   its rush direction; a non-living holder of the order does nothing.
		// tests: StatsCommands.stats_do_command_rush_takes_three_steps_and_shoves_the_collider
		// tests: StatsMorePaths.stats_round14_quickfire_multido_rush_and_walk_to_foe_melee_short_circuit
		// tests: parity:soldier_charge_displacement_scen99
		// tests: SemAi3.a_rush_order_on_a_non_living_body_does_nothing_but_count_down
		case COMMAND_RUSH:    // Fighter's special
			// rule: A fighter's rush (charge special) moves a living walker three steps per round
			//   in its rush direction; a non-living holder of the order does nothing.
			// tests: StatsCommands.stats_do_command_rush_takes_three_steps_and_shoves_the_collider
			// tests: StatsMorePaths.stats_round14_quickfire_multido_rush_and_walk_to_foe_melee_short_circuit
			// tests: parity:soldier_charge_displacement_scen99
			// tests: SemAi3.a_rush_order_on_a_non_living_body_does_nothing_but_count_down
			if (controller_->query_order() == Order::Living)
			{
				controller_->walkstep(com1, com2);
				controller_->walkstep(com1, com2);
				controller_->walkstep(com1, com2);
				// Gameplay Fixes Z: the rush strikes the body its last step ran
				// into: not an ally (walker::attack refuses one, but only after
				// its damage draw, and the queue theft and the forced walk below
				// hit a seat hero and a wall-pressed ally alike), and not a body
				// collide_ob still names from an earlier step this act (a wall
				// stopped the rush; fix X). Off, the classic strike stands.
				// rule: Classic: a rush calls attack on whatever body collide_ob names after the
				//   three steps, wipes its orders and shoves it 4 steps along the rush, allies
				//   included and including a stale body met on an earlier step; an ally takes no
				//   damage (attack refuses a friend after its damage draw) but still loses its
				//   orders to the forced WALK. Gameplay Fixes Z: the rush strikes only a
				//   non-friendly body that its LAST step actually meets; an ally or a body left
				//   over from an earlier step (wall-stopped rush, fix X) keeps its orders, is not
				//   shoved and no damage draw is spent.
				// tests: GameplayFixesWalk.a_rush_into_an_ally_erases_no_orders_with_the_setting_on
				// tests: GameplayFixesWalk.a_rush_into_a_foe_still_strikes_it
				// tests: GameplayFixesWalk.a_wall_stopped_rush_strikes_no_stale_body
				// tests: StatsCommands.stats_do_command_rush_with_the_setting_on_leaves_a_parked_collider_alone
				// tests: parity:soldier_charge_ally_scen99, parity:soldier_charge_front_foe_scen99
				// tests: StatsMorePaths.stats_round14_quickfire_multido_rush_and_walk_to_foe_melee_short_circuit
				if (controller_->collide_ob() // We hit someone
				        && (og::sim::session_gameplay_fixes() == 0
				            || (!controller_->is_friendly(controller_->collide_ob())
				                && og::sim::step_meets(*controller_,
				                                       static_cast<float>(com1) * controller_->stepsize(),
				                                       static_cast<float>(com2) * controller_->stepsize(),
				                                       *controller_->collide_ob()))))
				{
					target = controller_->collide_ob();
					controller_->attack(target);
					// rule: Gameplay Fixes AO (#426): a body that is dead once the rush has struck (a door its own
					//   step opened with a key, or a foe the strike itself killed) is left as it lies: attack still
					//   runs and draws its damage roll, but the body's orders are not erased and no four-step walk
					//   is forced on it. Classic: the dead body loses its orders and is walked all the same.
					// tests: GameplayFixesWalk.a_rush_ending_on_a_dead_body_leaves_its_orders_with_the_setting_on
					// tests: GameplayFixesWalk.a_rush_that_opens_a_door_leaves_the_dead_door_alone_with_the_setting_on
					if (og::sim::session_gameplay_fixes() == 0 || !target->dead())
					{
						target->stats()->clear_command();
						// A violent shove... we can't call shove since we
						//  made shove unable to shove enemies.
						target->stats()->force_command(COMMAND_WALK,
						                             4,com1,com2 );
					}
				}
			}
			break;
			// rule: SET_WEAPON swaps the walker's current weapon type to com1 for as long as it
			//   holds; RESET_WEAPON restores the family default.
			// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
			case COMMAND_SET_WEAPON: // set weapon to specified type
				controller_->set_current_weapon(static_cast<unsigned short>(com1));
				break;
		// rule: SET_WEAPON swaps the walker's current weapon type to com1 for as long as it holds;
		//   RESET_WEAPON restores the family default.
		// tests: StatsMorePaths.stats_do_command_set_reset_weapon_and_search_without_foe
		case COMMAND_RESET_WEAPON: // reset weapon to default type
			controller_->set_current_weapon(controller_->default_weapon());
			break;
		// rule: A SEARCH order hunts the held foe (walk_to_foe) while it lives; with no foe or a
		//   dead foe the order is abandoned that round.
		// tests: StatsCommands.stats_do_command_search_without_a_live_foe_abandons_the_order
		// tests: StatsUnit.stats_r12_command_and_hit_response_branches
		case COMMAND_SEARCH: // use right-hand rule to find foe
			// rule: A SEARCH order hunts the held foe (walk_to_foe) while it lives; with no foe or
			//   a dead foe the order is abandoned that round.
			// tests: StatsCommands.stats_do_command_search_without_a_live_foe_abandons_the_order
			// tests: StatsUnit.stats_r12_command_and_hit_response_branches
			if (controller_->foe() && !controller_->foe()->dead())
				walk_to_foe();
			else // stop trying to walk to this foe
            {
				commands.front().commandcount = 0;
            }
			break;
		// rule: A GOTO order (mode directors: CTF, soccer, onslaught, captain kit) walks toward a
		//   fixed map point and never attacks on the way; see below for arrival.
		// tests: SemAi3.a_goto_order_walks_to_its_point_without_attacking_and_ends_on_arrival
		case COMMAND_GOTO: // walk toward a fixed map position
			walk_to_point(static_cast<short>(com1), static_cast<short>(com2));
			break;
		// rule: RIGHT_WALK wall-follows (right hand) when the foe is at middle range (120..240 px
		//   exclusive); nearer or farther it tries the straight line first and falls back to
		//   wall-following only when blocked; with no foe the order idles.
		// tests: StatsCommands.stats_do_command_right_walk_distance_gate_picks_the_walker
		// tests: StatsUnit.right_walk_command_falls_back_to_wall_following_when_the_direct_step_is_blocked
		// tests: StatsMorePaths.stats_round11_follow_force_walk_and_right_walk_distance_branches
		// tests: SemAi3.a_right_walk_order_without_a_foe_idles
		case COMMAND_RIGHT_WALK: // right-hand-walk ONLY
			// rule: RIGHT_WALK wall-follows (right hand) when the foe is at middle range (120..240
			//   px exclusive).
			// tests: StatsCommands.stats_do_command_right_walk_distance_gate_picks_the_walker
			// tests: StatsUnit.right_walk_command_falls_back_to_wall_following_when_the_direct_step_is_blocked
			// tests: StatsMorePaths.stats_round11_follow_force_walk_and_right_walk_distance_branches
			// tests: SemAi3.a_right_walk_order_without_a_foe_idles
			if (controller_->foe())
			{
				distance = controller_->distance_to_ob(controller_->foe());
				// rule: RIGHT_WALK wall-follows (right hand) when the foe is at middle range
				//   (120..240 px exclusive).
				// tests: StatsCommands.stats_do_command_right_walk_distance_gate_picks_the_walker
				// tests: StatsUnit.right_walk_command_falls_back_to_wall_following_when_the_direct_step_is_blocked
				// tests: StatsMorePaths.stats_round11_follow_force_walk_and_right_walk_distance_branches
				// tests: SemAi3.a_right_walk_order_without_a_foe_idles
				if (distance > 120 && distance < 240)
					right_walk();
				else
					// rule: RIGHT_WALK wall-follows (right hand) when the foe is at middle range
					//   (120..240 px exclusive).
					// tests: StatsCommands.stats_do_command_right_walk_distance_gate_picks_the_walker
					// tests: StatsUnit.right_walk_command_falls_back_to_wall_following_when_the_direct_step_is_blocked
					// tests: StatsMorePaths.stats_round11_follow_force_walk_and_right_walk_distance_branches
					// tests: SemAi3.a_right_walk_order_without_a_foe_idles
					if (!direct_walk())
						right_walk();
			}
			break;
		// rule: An ATTACK order ends as soon as the walker has no foe or its foe is dead.
		// tests: StatsCommands.stats_do_command_attack_without_foe_pops_and_faces_a_foe_it_cannot_shoot
		// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
		// tests: SemAi3.an_attack_order_on_a_dead_foe_ends_at_once
		case COMMAND_ATTACK: // attack a nearby, set foe
			// rule: An ATTACK order ends as soon as the walker has no foe or its foe is dead.
			// tests: StatsCommands.stats_do_command_attack_without_foe_pops_and_faces_a_foe_it_cannot_shoot
			// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
			// tests: SemAi3.an_attack_order_on_a_dead_foe_ends_at_once
			if (!controller_->foe() || controller_->foe()->dead())
			{
				commands.front().commandcount = 0;
				result = 1;
				break;
			}
			// Try to walk toward foe, and/or attack ..
			deltax = static_cast<short>(controller_->foe()->xpos() - controller_->xpos());
			deltay = static_cast<short>(controller_->foe()->ypos() - controller_->ypos());
				// rule: The attacker aims at the foe along an 8-way direction: axis when one delta
				//   dominates 3:1, else diagonal (same snap as above).
				// tests: StatsUnit.stats_attack_and_follow_step_toward_their_target_and_blocked_probes_read_their_own_cells
				// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
				// tests: SemAi3.an_attacker_out_of_range_steps_toward_its_foe_on_the_snapped_direction
				// tests: SemAi3.an_attacker_merged_with_its_foe_steps_out_along_its_facing
				if (abs(deltax) > abs(3*deltay))
					deltay = 0;
				if (abs(deltay) > abs(3*deltax))
					deltax = 0;
				if (deltax)
					deltax = (deltax > 0) ? 1 : -1;
				if (deltay)
					deltay = (deltay > 0) ? 1 : -1;
			{
				walker::FireCheckDenial denial = walker::FireCheckDenial::None;
				// rule: An attacking walker shoots when the shot is clear (queues a FIRE of rng(5)
				//   rounds in front and fires now); when the only reason it cannot is its
				//   orientation (a thrower facing off the foe, or a melee-only family at bump
				//   range) it turns to face the foe without moving (guard-standoff fix, always on);
				//   when the foe is out of range, behind a wall or it lacks mana it walks one step
				//   toward the foe.
				// tests: StatsExtended.do_command_attack_faces_an_in_reach_foe_without_walking
				// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
				// tests: StatsCommands.stats_do_command_attack_without_foe_pops_and_faces_a_foe_it_cannot_shoot
				// tests: MeleeStandoff.facing_denial_snaps_to_face_the_foe_in_one_tick
				// tests: StatsUnit.stats_attack_and_follow_step_toward_their_target_and_blocked_probes_read_their_own_cells
				// tests: MeleeStandoff.out_of_range_denial_still_walks_toward_the_foe
				// tests: SemAi3.classic_an_attack_that_fires_never_counts_down_its_own_rounds
				// tests: SemAi3.a_melee_only_attacker_at_bump_range_turns_to_face_its_foe_without_moving
				if (!controller_->fire_check(deltax, deltay, &denial))
				{
					// Guard-standoff melee deadlock fix (2026-07-07, see
					// docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): when the shot at
					// an IN-REACH foe is denied only by our orientation, turn
					// to face the foe and swing next tick. Two denials mean
					// exactly that:
					//  - Facing: a thrown-weapon family pointed off the foe;
					//  - NoRanged: a melee-only family (orc, ghost, slime) at
					//    bump range — its hit lands through fire()'s blocked
					//    weapon-spawn cell, which also requires facing.
					// The classic fallback walked instead; with the foe's own
					// body blocking that step, walkstep's blocked-NPC branch
					// set curdir PERPENDICULAR (the wall-slide), the orbit
					// re-collapsed the approach delta every tick, and two
					// adjacent fighters could face-dance forever without one
					// landed swing (Westlands L25's eternal last foe / L2's
					// parked companies). OutOfRange/WallBlocked keep the
					// classic walk-toward-the-foe response (they need
					// repositioning), as does NoMagic (regen while closing).
					// rule: An attacker standing exactly on its foe (a pass-through left the bodies
					//   merged) steps out along its own facing to break the clinch,
					//   deterministically and without RNG (always on).
					// tests: SemAi3.an_attacker_merged_with_its_foe_steps_out_along_its_facing
					if (deltax == 0 && deltay == 0)
					{
						// Exactly coincident with the foe (a transient
						// pass-through left us inside its body). The classic
						// walkstep(0,0) is a no-op, freezing the clinch
						// forever; and no swing can land — a weapon always
						// spawns just OUTSIDE the merged box. Step along our
						// facing instead: any direction separates (see the
						// interpenetration escape rule in obmap.cpp), and
						// curdir keeps the choice deterministic. No RNG.
						short stepx = 0, stepy = 0;
						facing_step_delta(static_cast<char>(controller_->curdir()),
						                  stepx, stepy);
						controller_->walkstep(stepx, stepy);
					}
					else if (denial == walker::FireCheckDenial::Facing ||
					         denial == walker::FireCheckDenial::NoRanged)  // rule: An attacking walker shoots
					             //   when the shot is clear (queues a FIRE of rng(5) rounds in
					             //   front and fires now).
					             // tests: StatsExtended.do_command_attack_faces_an_in_reach_foe_without_walking
					             // tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
					             // tests: StatsCommands.stats_do_command_attack_without_foe_pops_and_faces_a_foe_it_cannot_shoot
					             // tests: MeleeStandoff.facing_denial_snaps_to_face_the_foe_in_one_tick
					             // tests: StatsUnit.stats_attack_and_follow_step_toward_their_target_and_blocked_probes_read_their_own_cells
					             // tests: MeleeStandoff.out_of_range_denial_still_walks_toward_the_foe
					             // tests: SemAi3.classic_an_attack_that_fires_never_counts_down_its_own_rounds
					             // tests: SemAi3.a_melee_only_attacker_at_bump_range_turns_to_face_its_foe_without_moving
						// Note for NoRanged (melee-only) families: this only
						// FACES the bump-range foe. Their swings are bumps: a
						// step of their own that runs into a body (living::walk's
						// probe) and, with Gameplay Fixes on, the right-hand
						// walk's probe that finds their own foe
						// (statistics::path_probe; off, every loud probe bumps).
						// walk_to_foe's near-branch init_fire() lands no melee
						// swing with the setting off (walker::turn moves a notch
						// even when the bot already faces the foe, so init_fire
						// only turns back; on, fix AT keeps it facing). Wiring init_fire()
						// here as well was tried and rejected: it roughly
						// doubles melee swing rate over classic and swung
						// campaign balance hard (Westlands L2's fight-through
						// crew started losing members).
						controller_->face_delta(deltax, deltay);
					else  // rule: An attacking walker shoots when the shot is clear (queues a FIRE
						//   of rng(5) rounds in front and fires now).
						// tests: StatsExtended.do_command_attack_faces_an_in_reach_foe_without_walking
						// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
						// tests: StatsCommands.stats_do_command_attack_without_foe_pops_and_faces_a_foe_it_cannot_shoot
						// tests: MeleeStandoff.facing_denial_snaps_to_face_the_foe_in_one_tick
						// tests: StatsUnit.stats_attack_and_follow_step_toward_their_target_and_blocked_probes_read_their_own_cells
						// tests: MeleeStandoff.out_of_range_denial_still_walks_toward_the_foe
						// tests: SemAi3.classic_an_attack_that_fires_never_counts_down_its_own_rounds
						// tests: SemAi3.a_melee_only_attacker_at_bump_range_turns_to_face_its_foe_without_moving
						controller_->walkstep(deltax, deltay);
				}
				else // (controller_->fire_check(deltax, deltay))
				{
					force_command(COMMAND_FIRE,static_cast<short>(rng(5)),deltax,deltay);
					controller_->init_fire(deltax,deltay);
				}
			}
			break;
		// rule: UNCHARM (its body commented out since #317: charm ends only on charm_left) and
		//   unknown command types are no-ops that are simply consumed by the countdown.
		// tests: SemAi3.an_uncharm_order_or_an_unknown_one_is_only_counted_down
		case COMMAND_UNCHARM:
			/*
			  if (commandcount > 1)
			  {
			    add_command(COMMAND_UNCHARM, commandcount-1, 0, 0);
			    commandcount = 1;
			  }
			  else if (controller_->real_team_num() != 255)
			  {
			    controller_->set_team_num(controller_->real_team_num());
			    controller_->set_real_team_num(255);
			  }
			*/
			break;  // end of uncharm case
		// rule: UNCHARM (its body commented out since #317: charm ends only on charm_left) and
		//   unknown command types are no-ops that are simply consumed by the countdown.
		// tests: SemAi3.an_uncharm_order_or_an_unknown_one_is_only_counted_down
		default:
			break;
	}
	
	// NOTE: The first command might be a different command than it was before the switch statement.
	// That would make this code decrement the wrong command.
	// rule: Every round spends one iteration of whatever command is at the FRONT after the switch
	//   ran (which may be a command the switch just forced, e.g. ATTACK's FIRE); a command reaching
	//   0 is removed.
	// tests: StatsCommands.stats_do_command_walk_steps_once_and_counts_down
	// tests: StatsExtended.do_command_unhandled_type_only_runs_the_decrement_tail
	// tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
	if(!commands.empty())
	{
        commands.front().commandcount--;       // reduce # of times left
        
        // rule: Every round spends one iteration of whatever command is at the FRONT after the
        //   switch ran (which may be a command the switch just forced, e.g. ATTACK's FIRE).
        // tests: StatsCommands.stats_do_command_walk_steps_once_and_counts_down
        // tests: StatsExtended.do_command_unhandled_type_only_runs_the_decrement_tail
        // tests: StatsExtended.do_command_attack_that_can_shoot_forces_a_fire_command_and_fires
        if(commands.front().commandcount < 1) // Last iteration!
        {
            commands.front().commandcount = 0;
            
			commands.pop_front();
        }
	}
	
	return result;
}

// Determines what to do when we're hit by 'who'
// 'controller' is our parent walker object
void statistics::hit_response(walker  *who)
{
	short myfamily;
	walker *foe; // who is attacking us?
	float threshold; // for hitpoint 'running away'

	// plumbing: hit_response null attacker / null controller guard
	if (!who || !controller_)
		return;
	// rule: Only a living, AI-driven, alive walker reacts to being hit: a dead attacker or dead
	//   victim, a player-controlled hero (ACT_CONTROL) and non-living targets (weapons, treasure,
	//   effects) never retarget, special or yell.
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsUnit.hit_response_guard_shields_non_living_targets
	// tests: StatsMorePaths.stats_set_command_die_and_hit_response_early_returns
	// tests: SemHit.a_dead_or_player_controlled_victim_and_a_dead_attacker_draw_no_response
	if (who->dead() || controller_->dead())
		return;

	// rule: Only a living, AI-driven, alive walker reacts to being hit.
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsUnit.hit_response_guard_shields_non_living_targets
	// tests: StatsMorePaths.stats_set_command_die_and_hit_response_early_returns
	// tests: SemHit.a_dead_or_player_controlled_victim_and_a_dead_attacker_draw_no_response
	if (controller_->act_type() == ACT_CONTROL)
		return;

	// rule: Only a living, AI-driven, alive walker reacts to being hit.
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsUnit.hit_response_guard_shields_non_living_targets
	// tests: StatsMorePaths.stats_set_command_die_and_hit_response_early_returns
	// tests: SemHit.a_dead_or_player_controlled_victim_and_a_dead_attacker_draw_no_response
	if (controller_->query_order() != Order::Living)
		return;

	// Set quick-reference values ..
	myfamily = controller_->family();
	// rule: A walker hit by a projectile or melee weapon retaliates against the weapon's owner, not
	//   the weapon; an ownerless weapon or a body is itself the foe.
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsMorePaths.stats_set_command_die_and_hit_response_early_returns
	if (who->query_order() == Order::Weapon && who->owner())
		foe = who->owner();
	else
		foe = who;

	auto* fd = get_family_descriptor(myfamily);
	// rule: A family's Lua hit_response hook (archer runs, mage/archmage teleport, cleric, etc. in
	//   packs/core/families/*.lua) replaces the default retaliate-and-flee response entirely when
	//   it reports handled.
	// tests: StatsCommands.stats_hit_response_all_families
	// tests: StatsHitResponseMore.statistics_hit_response_archer_runs_away_and_queues_walk
	// tests: StatsHitResponseTeleport.stats_hit_response_mage_and_archmage_teleport_branches
	// tests: SemHit.a_family_hit_response_hook_replaces_the_default_yell_and_flee
	if (og::script::hooks::hit_response(fd, this, foe))
		return;

	// Default: attack our attacker
	// rule: When struck, an AI walker whose special is available fires it one time in three
	//   (check_special gates before the rng draw).
	// tests: StatsCommands.stats_hit_response_default_arm_special_gate_opens_when_rng3_is_zero
	// tests: SemHit.a_struck_walker_rolls_for_its_special_only_when_the_special_is_available
	if (controller_->check_special() && !rng(3) )
		controller_->special();
	// rule: A player's named character (has a guy record) flees for help below 50 percent hp; an
	//   enemy/bot without a guy is braver and flees below 5/16.
	// tests: SemHit.a_named_hero_flees_below_half_hp_and_a_bot_only_below_five_sixteenths
	if (controller_->myguy) // are we a player's character?
		threshold = (5 * max_hitpoints_)/10; // then flee at 50%
	else                   // we're an enemy, so be braver :>
		threshold = (5 * max_hitpoints_)/16; // flee at 5/16%
	// Retarget before yelling: clearing the old commands afterward would
	// discard the escape walk that yell_for_help just queued.
	// rule: A walker struck by a new attacker drops its orders and targets the attacker, and the
	//   attacker is made to target it back; the chase distance memory resets. Retargeting runs
	//   before the yell so the yell's flee walk survives.
	// tests: StatsExtended.hit_response_from_a_new_attacker_retargets_both_ways_and_clears_commands
	// tests: StatsHitResponseMore.wounded_world_member_recruits_allies_and_keeps_its_escape
	if (controller_->foe() != foe) // we're attacked by a new enemy
	{
		// Clear old commands ..
		clear_command();
		// Attack our attacker
		controller_->set_foe(foe);
		foe->set_foe(controller_);
		set_last_distance(32000);
		set_current_distance(32000);
	}
	// rule: A badly hurt walker yells for help and runs, but at most once per yell cooldown
	//   (yo_delay, +80 per yell).
	// tests: StatsMorePaths.hit_response_yells_once_below_half_hp_and_retargets_both_bodies
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsHitResponseMore.wounded_world_member_recruits_allies_and_keeps_its_escape
	if ( (hitpoints_ < threshold)
	        && !controller_->yo_delay()) // then yell for help & run ..
	{
		yell_for_help(foe);
	} // end of yell for help

}

void statistics::yell_for_help(walker *foe)
{
	Sint32 howmany;
	Sint32 deltax, deltay;

	controller_->set_yo_delay(controller_->yo_delay() + 80);
	
	// Get AI-controlled allies to target my foe
	std::list<walker*> helplist = current_game->world->find_friends_in_range(
	               current_game->world->oblist, 160, &howmany, controller_);
	// plumbing: iterates allies within 160 px from find_friends_in_range; see below
	for(auto* w : helplist)
	{
		w->set_leader(controller_);
		// rule: A yell recruits every living, non-dormant, non-hidden friendly walker within 160 px
		//   of the yeller (find_friends_in_range has no act_type gate, so player-controlled heroes
		//   are recruited too): each gets the yeller as leader and the yeller's attacker as foe;
		//   one newly switched onto that foe resets its chase distance memory to 32000, one already
		//   on it keeps it.
		// tests: StatsCommands.stats_yell_for_help_flees_and_recruits_friends
		// tests: StatsHitResponseMore.wounded_world_member_recruits_allies_and_keeps_its_escape
		// tests: SemHit.a_yell_recruits_friends_in_range_and_resets_only_a_newly_switched_chase
		if (foe != w->foe())
		{
			w->stats()->set_current_distance(32000);
			w->stats()->set_last_distance(32000);
		}
		w->set_foe(foe);
		//if (w->act_type() != ACT_CONTROL)
		//  w->stats()->force_command(COMMAND_FOLLOW, 80, 0, 0);
	}
	
	// Force run in the opposite direction
	deltax = -(foe->xpos() - controller_->xpos());
	// rule: The yeller flees directly away from its attacker on each axis (8-way, no 3:1 snap; same
	//   row/column gives a pure axis flee; coincident gives (0,0) which the walk clamp turns into
	//   SE).
	// tests: StatsCommands.stats_yell_for_help_flees_and_recruits_friends
	// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
	// tests: StatsHitResponseMore.wounded_world_member_recruits_allies_and_keeps_its_escape
	// tests: SemHit.a_yeller_on_top_of_its_attacker_flees_south_east
	if (deltax)
		deltax = (deltax / abs(deltax));
	deltay =  -(foe->ypos() - controller_->ypos());
	if (deltay)
		deltay =  (deltay / abs(deltay));
	// Run away
	// rule: A fearless walker (KIT_FEARLESS mark, or inside a friendly captain banner's rally
	//   radius with New Specials on) still yells and recruits but holds its ground instead of
	//   running.
	// tests: KitSeamsSmall.fearless_bit_and_rally_radius_block_the_flee_walk
	// tests: SemHit.a_fearless_walker_yells_and_recruits_but_does_not_run
	if (!og::sim::fearless(*controller_)) force_command(COMMAND_WALK, 16, deltax, deltay);  // rallied walkers hold
	// Notify friends of need ...
	// rule: Only a named character on team 0 (the first player team) announces its yell as an
	//   on-screen notification; other teams' heroes and bots yell silently.
	// tests: SemHit.classic_only_a_named_team_zero_hero_announces_its_yell
	if (controller_->myguy && (controller_->team_num() == 0) )
	{
		std::string message = std::format("{} yells for help!", controller_->myguy->name);
		// rule: Only a named character on team 0 (the first player team) announces its yell as an
		//   on-screen notification; other teams' heroes and bots yell silently.
		// tests: SemHit.classic_only_a_named_team_zero_hero_announces_its_yell
		if (current_game->sim_events)
			current_game->sim_events->push_notification(message, 10);
	}

}

short statistics::query_bit_flags(Sint32 myvalue) const
{
	return static_cast<short>(myvalue & bit_flags_);
}

void statistics::clear_bit_flags()
{
    set_bit_flags(0);
}

void statistics::set_bit_flags(Sint32 someflag, short newvalue)
{
	// plumbing: set_bit_flags sets or clears the given mask and marks the snapshot dirty bit; flag
	//   semantics live with the flag readers
	if (newvalue)
	{
		bit_flags_ |= someflag;
	}
	else
	{
		bit_flags_ &= ~someflag;
	}
	mark_dirty(og::dirty::BIT_BIT_FLAGS);
}

// Returns whether our right is blocked
bool statistics::right_blocked(bool quiet)
{
	float xdelta, ydelta;
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = controller_->stepsize();

	mystep = CHECK_STEP_SIZE;
	// rule: The right probe asks whether the walker's own box moved one pixel (CHECK_STEP_SIZE) 90
	//   degrees clockwise of the facing (UP->E, UR->SE, R->S, DR->SW, D->W, DL->NW, L->N, UL->NE)
	//   is impassable (grid, bodies, doors via query_passable; quiet when the caller asks). An
	//   out-of-range facing falls through with xdelta=ydelta=0 and probes the walker's own spot:
	//   usually 'not blocked', but the loud probe still runs there and can report a body coincident
	//   with the walker (and set collide_ob).
	// tests: StatsExtended.blocked_probes_pin_every_facing_offset_of_all_four_tables
	// tests: StatsCommands.stats_blocked_helpers_probe_the_exact_cell_for_every_facing
	// tests: StatsRightWalkDirectWalk.blocked_probe_tables_resolve_each_facings_exact_cell
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: SemAi3.the_right_probe_of_an_out_of_range_facing_asks_the_walkers_own_spot
	switch (controller_->curdir())
	{
		case FACE_UP:
			xdelta = mystep;
			ydelta = 0;
			break;
		case FACE_UP_RIGHT:
			xdelta = mystep;
			ydelta = mystep;
			break;
		case FACE_RIGHT:
			xdelta = 0;
			ydelta = mystep;
			break;
		case FACE_DOWN_RIGHT:
			xdelta = -mystep;
			ydelta = mystep;
			break;
		case FACE_DOWN:
			xdelta = -mystep;
			ydelta = 0;
			break;
		case FACE_DOWN_LEFT:
			xdelta = -mystep;
			ydelta = -mystep;
			break;
		case FACE_LEFT:
			xdelta = 0;
			ydelta = -mystep;
			break;
		case FACE_UP_LEFT:
			xdelta = mystep;
			ydelta = -mystep;
			break;
		default:
			xdelta = 0;
			ydelta = 0;
			break;
	}

	const float px = controlx + xdelta, py = controly + ydelta;
	return !current_game->world->query_passable(px, py, controller_, controller_->floor(), quiet);
}

// Returns whether our right-forward is blocked
bool statistics::right_forward_blocked(bool quiet)
{
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = controller_->stepsize();

	mystep = CHECK_STEP_SIZE;
	// rule: The right-front probe asks whether the walker's own box moved one pixel
	//   (CHECK_STEP_SIZE) 45 degrees clockwise of the facing (UP->NE, UR->E, R->SE, DR->S, D->SW,
	//   DL->W, L->NW, UL->N) is impassable (grid, bodies, doors via query_passable; quiet when the
	//   caller asks). An out-of-range facing returns false without any probe (default: break;
	//   return false; query_passable is not called, so a loud probe has no side effect).
	// tests: StatsExtended.blocked_probes_pin_every_facing_offset_of_all_four_tables
	// tests: StatsCommands.stats_blocked_helpers_probe_the_exact_cell_for_every_facing
	// tests: StatsRightWalkDirectWalk.blocked_probe_tables_resolve_each_facings_exact_cell
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsUnit.stats_r11_clear_command_and_blocked_direction_defaults
	// tests: SemAi4.an_out_of_range_facing_leaves_the_right_front_and_right_back_probes_unasked
	switch (controller_->curdir())
	{
		case FACE_UP:
			return !current_game->world->query_passable(controlx+mystep, controly-mystep, controller_, controller_->floor(), quiet);
		case FACE_UP_RIGHT:
			return !current_game->world->query_passable(controlx+mystep, controly, controller_, controller_->floor(), quiet);

		case FACE_RIGHT:
			return !current_game->world->query_passable(controlx+mystep, controly+mystep, controller_, controller_->floor(), quiet);
		case FACE_DOWN_RIGHT:
			return !current_game->world->query_passable(controlx, controly+mystep, controller_, controller_->floor(), quiet);
		case FACE_DOWN:
			return !current_game->world->query_passable(controlx-mystep, controly+mystep, controller_, controller_->floor(), quiet);
		case FACE_DOWN_LEFT:
			return !current_game->world->query_passable(controlx-mystep, controly, controller_, controller_->floor(), quiet);
		case FACE_LEFT:
			return !current_game->world->query_passable(controlx-mystep, controly-mystep, controller_, controller_->floor(), quiet);
		case FACE_UP_LEFT:
			return !current_game->world->query_passable(controlx, controly-mystep, controller_, controller_->floor(), quiet);
		default:
			break;

	}
	return false;
}

// Returns whether our right-back is blocked
bool statistics::right_back_blocked(bool quiet)
{
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = controller_->stepsize();

	mystep = CHECK_STEP_SIZE;
	// rule: The right-back probe asks whether the walker's own box moved one pixel
	//   (CHECK_STEP_SIZE) 135 degrees clockwise of the facing (UP->SE, UR->S, R->SW, DR->W, D->NW,
	//   DL->N, L->NE, UL->E) is impassable (grid, bodies, doors via query_passable; quiet when the
	//   caller asks). An out-of-range facing returns false without any probe (default: break;
	//   return false; query_passable is not called, so a loud probe has no side effect).
	// tests: StatsExtended.blocked_probes_pin_every_facing_offset_of_all_four_tables
	// tests: StatsCommands.stats_blocked_helpers_probe_the_exact_cell_for_every_facing
	// tests: StatsRightWalkDirectWalk.blocked_probe_tables_resolve_each_facings_exact_cell
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsUnit.stats_r11_clear_command_and_blocked_direction_defaults
	// tests: SemAi4.an_out_of_range_facing_leaves_the_right_front_and_right_back_probes_unasked
	switch (controller_->curdir())
	{
		case FACE_UP:
			return !current_game->world->query_passable(controlx+mystep, controly+mystep, controller_, controller_->floor(), quiet);
		case FACE_UP_RIGHT:
			return !current_game->world->query_passable(controlx, controly+mystep, controller_, controller_->floor(), quiet);

		case FACE_RIGHT:
			return !current_game->world->query_passable(controlx-mystep, controly+mystep, controller_, controller_->floor(), quiet);
		case FACE_DOWN_RIGHT:
			return !current_game->world->query_passable(controlx-mystep, controly, controller_, controller_->floor(), quiet);
		case FACE_DOWN:
			return !current_game->world->query_passable(controlx-mystep, controly-mystep, controller_, controller_->floor(), quiet);
		case FACE_DOWN_LEFT:
			return !current_game->world->query_passable(controlx, controly-mystep,
			                                controller_, controller_->floor(), quiet);
		case FACE_LEFT:
			return !current_game->world->query_passable(controlx+mystep, controly-mystep, controller_, controller_->floor(), quiet);
		case FACE_UP_LEFT:
			return !current_game->world->query_passable(controlx+mystep, controly, controller_, controller_->floor(), quiet);
		default:
			break;
	}

	return false;
}

// Returns whether our front is blocked
bool statistics::forward_blocked(bool quiet)
{
	float xdelta, ydelta;
	float controlx = controller_->xpos(), controly = controller_->ypos();
	float mystep = CHECK_STEP_SIZE;

	// rule: The forward probe asks whether the walker's own box moved one pixel (CHECK_STEP_SIZE)
	//   along the facing is impassable (grid, bodies, doors via query_passable; quiet when the
	//   caller asks). An out-of-range facing falls through with xdelta=ydelta=0 and probes the
	//   walker's own spot: usually 'not blocked', but the loud probe still runs there and can
	//   report a body coincident with the walker (and set collide_ob).
	// tests: StatsExtended.blocked_probes_pin_every_facing_offset_of_all_four_tables
	// tests: StatsCommands.stats_blocked_helpers_probe_the_exact_cell_for_every_facing
	// tests: StatsRightWalkDirectWalk.blocked_probe_tables_resolve_each_facings_exact_cell
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing
	// tests: SemAi4.an_out_of_range_facing_makes_the_front_probe_ask_the_walkers_own_spot
	switch (controller_->curdir())
	{
		case FACE_UP:
			xdelta = 0;
			ydelta = -mystep;
			break;
		case FACE_UP_RIGHT:
			xdelta = mystep;
			ydelta = -mystep;
			break;
		case FACE_RIGHT:
			xdelta = mystep;
			ydelta = 0;
			break;
		case FACE_DOWN_RIGHT:
			xdelta = mystep;
			ydelta = mystep;
			break;
		case FACE_DOWN:
			xdelta = 0;
			ydelta = mystep;
			break;
		case FACE_DOWN_LEFT:
			xdelta = -mystep;
			ydelta = mystep;
			break;
		case FACE_LEFT:
			xdelta = -mystep;
			ydelta = 0;
			break;
		case FACE_UP_LEFT:
			xdelta = -mystep;
			ydelta = -mystep;
			break;
		default:
			xdelta = 0;
			ydelta = 0;
			break;
	}

	return !current_game->world->query_passable(controlx + xdelta, controly + ydelta, controller_, controller_->floor(), quiet);
}

// Gameplay Fixes Y: the right-hand walk's four probes ask quietly. The classic
// probes were the walker's own step moved one pixel to a side: they ate a
// potion the bot never stood on, opened a door, and a body a side probe
// grazed took a bump swing (busy, a knife along the heading two acts later)
// or turned the bot, and was left in collide_ob for the stale shove of fix X.
// Off, the loud probes stand. The same flag reaches COMMAND_GOTO's walk below.
// A melee-only bot's own foe in a probe's way is still the classic bump
// (path_probe): that is how such a bot fights. Everything else stays quiet.
bool statistics::right_walk()
{
	float xdelta, ydelta;
	const bool quiet = og::sim::session_gameplay_fixes() != 0;   // Gameplay Fixes Y: a pathing probe changes nothing

	// rule: Right-hand wall following: with a wall on the right (or right-front) the walker keeps
	//   going straight while the front is open and turns left (90 deg) when it is not; with nothing
	//   on the right but a blocked front it turns left; with only the right-back blocked (it just
	//   passed a corner) it turns right and queues one step along the new heading; in open space it
	//   walks straight at its foe (direct_walk) and, failing that, along its facing. Classic: the
	//   four probes are the walker's own step moved one pixel and act like steps (eat a potion,
	//   open a door, bump-swing a body, leave collide_ob for a stale shove). Gameplay Fixes Y: the
	//   probes are quiet and change nothing, except a melee-only bot's own foe in a probe's way is
	//   still bumped (below).
	// tests: GameplayFixesWalk.a_right_walk_side_probe_eats_nothing_and_swings_at_nobody
	// tests: GameplayFixesWalk.a_side_probe_does_not_swing_at_a_body_it_is_not_fighting_with_the_setting_on
	// tests: StatsUnit.stats_r11_right_walk_branch_matrix
	// tests: StatsRightWalkDirectWalk.right_walk_normalizes_the_forward_step_and_turns_left_when_blocked
	// tests: parity:orc_right_walk_potion_scen99, parity:orc_right_walk_potion_fixes_scen99
	// tests: SemAi4.a_right_walker_in_open_space_steps_straight_at_its_foe
	// tests: SemAi4.a_right_walk_side_probe_opens_a_door_only_with_the_setting_off
	if (path_probe(Side::Right, quiet) || path_probe(Side::RightFront, quiet) )
	{
		// rule: Right-hand wall following: with a wall on the right (or right-front) the walker
		//   keeps going straight while the front is open and turns left (90 deg) when it is not.
		// tests: GameplayFixesWalk.a_right_walk_side_probe_eats_nothing_and_swings_at_nobody
		// tests: GameplayFixesWalk.a_side_probe_does_not_swing_at_a_body_it_is_not_fighting_with_the_setting_on
		// tests: StatsUnit.stats_r11_right_walk_branch_matrix
		// tests: StatsRightWalkDirectWalk.right_walk_normalizes_the_forward_step_and_turns_left_when_blocked
		// tests: parity:orc_right_walk_potion_scen99, parity:orc_right_walk_potion_fixes_scen99
		// tests: SemAi4.a_right_walker_in_open_space_steps_straight_at_its_foe
		// tests: SemAi4.a_right_walk_side_probe_opens_a_door_only_with_the_setting_off
		if (!path_probe(Side::Front, quiet))  // walk forward
		{
			xdelta = controller_->lastx();
			ydelta = controller_->lasty();
			// rule: While hugging a wall the walker keeps going in the direction it last moved,
			//   snapped to an axis when one component dominates 3:1 (same snap as above).
			// tests: StatsRightWalkDirectWalk.right_walk_normalizes_the_forward_step_and_turns_left_when_blocked
			// tests: SemAi4.classic_a_wall_hugger_that_never_moved_stands_still_for_a_tick
			if (std::fabs(xdelta) > std::fabs(3 * ydelta))
				ydelta = 0;
			if (std::fabs(ydelta) > std::fabs(3 * xdelta))
				xdelta = 0;
			if (xdelta != 0.0f)
				xdelta /= std::fabs(xdelta);
			if (ydelta != 0.0f)
				ydelta /= std::fabs(ydelta);
			//              return controller_->walk();
			return controller_->walkstep(xdelta, ydelta);
		}
		else  // turn left
		{
			controller_->set_enddir(static_cast<char>((controller_->enddir()+6) %8));  // turn left
			return controller_->turn(controller_->enddir());
		}
	}
	else if (path_probe(Side::Front, quiet))
	{
		controller_->set_enddir(static_cast<char>((controller_->enddir()+6) %8));  // turn left
		return controller_->turn(controller_->enddir());
	}
	else if (path_probe(Side::RightBack, quiet))
	{
		controller_->set_enddir(static_cast<char>((controller_->enddir()+2) %8));  // turn right
		// rule: Turning right around a corner queues one unit walk along the new heading (8-way
		//   table identical to facing_step_delta, below).
		// tests: StatsRightWalkDirectWalk.stats_right_walk_turn_right_adds_walk_command_all_enddirs
		// tests: StatsMorePaths.stats_right_walk_round8_negative_enddir_default_switch_path
		switch (controller_->enddir())
		{
			case FACE_UP:
				xdelta = 0;
				ydelta = -1;
				break;
			case FACE_UP_RIGHT:
				xdelta = 1;
				ydelta = -1;
				break;
			case FACE_RIGHT:
				xdelta = 1;
				ydelta = 0;
				break;
			case FACE_DOWN_RIGHT:
				xdelta = 1;
				ydelta = 1;
				break;
			case FACE_DOWN:
				xdelta = 0;
				ydelta = 1;
				break;
			case FACE_DOWN_LEFT:
				xdelta = -1;
				ydelta = 1;
				break;
			case FACE_LEFT:
				xdelta = -1;
				ydelta = 0;
				break;
			case FACE_UP_LEFT:
				xdelta = -1;
				ydelta = -1;
				break;
			default:
				xdelta = 0;
				ydelta = 0;
				break;
		}
			add_command(COMMAND_WALK, 1, static_cast<Sint32>(xdelta), static_cast<Sint32>(ydelta));
	}
	else
	{
		// rule: Right-hand wall following: with a wall on the right (or right-front) the walker
		//   keeps going straight while the front is open and turns left (90 deg) when it is not.
		// tests: GameplayFixesWalk.a_right_walk_side_probe_eats_nothing_and_swings_at_nobody
		// tests: GameplayFixesWalk.a_side_probe_does_not_swing_at_a_body_it_is_not_fighting_with_the_setting_on
		// tests: StatsUnit.stats_r11_right_walk_branch_matrix
		// tests: StatsRightWalkDirectWalk.right_walk_normalizes_the_forward_step_and_turns_left_when_blocked
		// tests: parity:orc_right_walk_potion_scen99, parity:orc_right_walk_potion_fixes_scen99
		// tests: SemAi4.a_right_walker_in_open_space_steps_straight_at_its_foe
		// tests: SemAi4.a_right_walk_side_probe_opens_a_door_only_with_the_setting_off
		if (!direct_walk()) //we can't even walk straight to our foe
		{
			// rule: In open space with no straight route to the foe, the walker ambles one step
			//   along its current facing.
			// tests: StatsMorePaths.right_walk_with_nothing_blocked_steps_the_unit_vector_of_its_facing
			// tests: StatsCommands.stats_right_walk_steps_along_curdir_when_direct_walk_fails
			// tests: StatsUnit.stats_r11_right_walk_branch_matrix
			// tests: SemAi4.a_right_walker_with_no_facing_ambles_nowhere
			switch (controller_->curdir())
			{
				case FACE_UP:
					xdelta = 0;
					ydelta = -1;
					break;
				case FACE_UP_RIGHT:
					xdelta = 1;
					ydelta = -1;
					break;
				case FACE_RIGHT:
					xdelta = 1;
					ydelta = 0;
					break;
				case FACE_DOWN_RIGHT:
					xdelta = 1;
					ydelta = 1;
					break;
				case FACE_DOWN:
					xdelta = 0;
					ydelta = 1;
					break;
				case FACE_DOWN_LEFT:
					xdelta = -1;
					ydelta = 1;
					break;
				case FACE_LEFT:
					xdelta = -1;
					ydelta = 0;
					break;
				case FACE_UP_LEFT:
					xdelta = -1;
					ydelta = -1;
					break;
				default:
					xdelta = 0;
					ydelta = 0;
					break;
			}
			//if (controller_->spaces_clear() > 6)
			return controller_->walkstep(xdelta, ydelta);
		}
	}
	return true;
}

// Gameplay Fixes Y, the bump. The probe's box is the bot's own moved one
// pixel (CHECK_STEP_SIZE) toward `side`, counted round from its facing: the
// same box facing_step_delta names, and the same collide() rule the classic
// probe met the body with (og::sim::step_meets). A melee-only bot has no aimed
// shot (fire_check refuses it, the ATTACK arm only faces): its blows are the
// bumps, its own step's and, with the setting on, this one at its own foe.
// Measured 2026-11-01: the quiet probes alone took a third of the orc, ghost
// and slime swings and a fifth of their damage, and left an orc stopped
// exactly against a standing foe swinging never. Off, the probe alone.
bool statistics::path_probe(Side side, bool quiet)
{
	bool blocked;
	// plumbing: path_probe dispatches Side to the four probe tables (above, 849, 882, 916)
	switch (side)
	{
		case Side::RightFront: blocked = right_forward_blocked(quiet); break;
		case Side::Right:      blocked = right_blocked(quiet); break;
		case Side::RightBack:  blocked = right_back_blocked(quiet); break;
		default:               blocked = forward_blocked(quiet); break;
	}
	// rule: Gameplay Fixes Y, the bump: with quiet probes, a melee-only bot (BIT_NO_RANGED: orc,
	//   ghost, slime...) whose own live, hostile, same-floor foe stands in a blocked probe's box
	//   still runs into it (collide: a swing along its heading) - that is how such bots fight; any
	//   other body a quiet probe meets is left alone. Off (loud probes), the probe itself does the
	//   bump for every body.
	// tests: GameplayFixesWalk.a_melee_bot_bumps_the_foe_a_quiet_probe_finds
	// tests: GameplayFixesWalk.a_side_probe_does_not_swing_at_a_body_it_is_not_fighting_with_the_setting_on
	// tests: SemAi4.a_quiet_probe_bumps_only_a_live_hostile_foe_on_the_bots_floor
	if (!quiet || !blocked)
		return blocked;
	walker* foe = controller_->foe();
	if (!foe || foe->dead() || foe->floor() != controller_->floor()
	        || !query_bit_flags(BIT_NO_RANGED) || controller_->is_friendly(foe))
		return blocked;
	short dx = 0, dy = 0;
	facing_step_delta(static_cast<char>((controller_->curdir() + static_cast<short>(side)) % 8), dx, dy);
	if (og::sim::step_meets(*controller_, static_cast<float>(dx * CHECK_STEP_SIZE),
	                        static_cast<float>(dy * CHECK_STEP_SIZE), *foe))
		controller_->collide(foe);
	return blocked;
}

bool statistics::direct_walk()
{
	walker * foe = controller_->foe();
	float xdest, ydest;
	float xdelta, ydelta;
	float xdeltastep, ydeltastep;
	float controlx = controller_->xpos(), controly = controller_->ypos();
	//  short xdistance, ydistance;
	//  Uint32 tempdistance;
	//  char olddir = controller_->curdir();
	//  short oldlastx = controller_->lastx();
	//  short oldlasty = controller_->lasty();

	// rule: With no foe there is no straight-line chase: direct_walk reports failure and does not
	//   move (callers then wall-follow).
	// tests: StatsUnit.stats_r11_direct_walk_and_walk_to_foe_tail_branches
	if (!foe)
		return 0;


	xdest = foe->xpos();
	ydest = foe->ypos();

	xdelta = xdest - controller_->xpos();
	ydelta = ydest - controller_->ypos();
	// rule: A chasing walker that can already land a shot on its foe along the (3:1-snapped, raw
	//   pixel) delta stops chasing: it wipes its orders, turns toward the foe and starts an ATTACK
	//   lasting 30+rng(25) rounds.
	// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
	// tests: SemAi4.a_chaser_that_can_land_a_shot_stops_and_attacks
	if (std::fabs(xdelta) > std::fabs(3 * ydelta))
		ydelta = 0;
	if (std::fabs(ydelta) > std::fabs(3 * xdelta))
		xdelta = 0;

	// rule: A chasing walker that can already land a shot on its foe along the (3:1-snapped, raw
	//   pixel) delta stops chasing.
	// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
	// tests: SemAi4.a_chaser_that_can_land_a_shot_stops_and_attacks, SemAi4.a_chaser_that_can_land_a_shot_keeps_facing_its_foe_with_the_setting_on
	if (controller_->fire_check(static_cast<short>(xdelta), static_cast<short>(ydelta)))
	{
		clear_command();
		controller_->turn(controller_->facing(xdelta, ydelta));
		add_command(COMMAND_ATTACK,static_cast<short>(30+rng(25)),0,0);
		return 1;
	}

	// rule: The straight-line chase takes the diagonal unit step toward the foe when the GRID
	//   allows it (bodies are not checked here), else slides along x, else along y; it fails
	//   (returns 0, no move) when every straight move is walled or the foe is exactly on top of it.
	// tests: StatsRightWalkDirectWalk.stats_direct_walk_grid_passability_branches
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
	// tests: SemAi4.a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled
	if (xdelta != 0.0f)
		xdelta = xdelta / std::fabs(xdelta);
	if (ydelta != 0.0f)
		ydelta = ydelta / std::fabs(ydelta);

	xdeltastep = xdelta*controller_->stepsize();
	ydeltastep = ydelta*controller_->stepsize();

	// Tom's note on 8/3/97: I think these would work better if
	// replaced by some sort of single "if forward_blocked()"
	// check, otherwise I'm not sure if this works regardless of
	// current facing ...
	// rule: The straight-line chase takes the diagonal unit step toward the foe when the GRID
	//   allows it (bodies are not checked here), else slides along x, else along y.
	// tests: StatsRightWalkDirectWalk.stats_direct_walk_grid_passability_branches
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
	// tests: SemAi4.a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled
	if (!current_game->world->query_grid_passable(controlx+xdeltastep, controly+ydeltastep, controller_) )
	{
		// rule: The straight-line chase takes the diagonal unit step toward the foe when the GRID
		//   allows it (bodies are not checked here), else slides along x, else along y.
		// tests: StatsRightWalkDirectWalk.stats_direct_walk_grid_passability_branches
		// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
		// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
		// tests: SemAi4.a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled
		if (!current_game->world->query_grid_passable(controlx+xdeltastep,controly+0,controller_) )
		{
			// rule: The straight-line chase takes the diagonal unit step toward the foe when the
			//   GRID allows it (bodies are not checked here), else slides along x, else along y.
			// tests: StatsRightWalkDirectWalk.stats_direct_walk_grid_passability_branches
			// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
			// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
			// tests: SemAi4.a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled
			if (!current_game->world->query_grid_passable(controlx+0,controly+ydeltastep,controller_) )
			{
				return 0;

				//we cannot get there by ANY of the straight line moves which
				//take us directy toward out foe
			}
			else //y ok
			{
				// rule: The straight-line chase takes the diagonal unit step toward the foe when
				//   the GRID allows it (bodies are not checked here), else slides along x, else
				//   along y.
				// tests: StatsRightWalkDirectWalk.stats_direct_walk_grid_passability_branches
				// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
				// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
				// tests: SemAi4.a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled
				if (ydelta == 0.0f)
				{
					return 0;
				}
                                controller_->walkstep(0.0f, ydelta);
				return 1;
				//walk in the y direction
			}
		} //end if(xd,0)
		else //x ok
		{
			// rule: The straight-line chase takes the diagonal unit step toward the foe when the
			//   GRID allows it (bodies are not checked here), else slides along x, else along y.
			// tests: StatsRightWalkDirectWalk.stats_direct_walk_grid_passability_branches
			// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
			// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
			// tests: SemAi4.a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled
			if (xdelta == 0.0f)
			{
				return 0;
			}
                        controller_->walkstep(xdelta, 0.0f);
			return 1;
			//walk in the x direction
		}

	}
	else //x and y ok
	{
		// rule: The straight-line chase takes the diagonal unit step toward the foe when the GRID
		//   allows it (bodies are not checked here), else slides along x, else along y.
		// tests: StatsRightWalkDirectWalk.stats_direct_walk_grid_passability_branches
		// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
		// tests: StatsUnit.stats_direct_walk_steps_toward_the_foe_and_hit_response_retargets_and_yells
		// tests: SemAi4.a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled
		if (xdelta == 0.0f && ydelta == 0.0f)
		{
			return 0;
		}
		controller_->walkstep(xdelta, ydelta);
		return 1;
	} //walk in the x and y direction

}

// When a cross-floor foe has no A*-reachable cell (e.g. it is standing on a
// sparse upper-floor platform that is not ground-connected to the Z-stair
// landing), chasing foe->xpos()/ypos() on OUR floor just mirrors the foe on the
// wrong level. Instead, route to the nearest Z-stair that leads toward the foe's
// floor: scan our floor for the appropriate stair tile (UP if the foe is above,
// DOWN if below) and run the stair-aware A* (find_path_to_point) to it. Walking
// onto the stair triggers apply_z_motion's floor change, after which cross_floor
// flips false and the normal chase resumes on the correct floor. Returns true
// (and leaves a path in path_to_foe) when a stair route exists.
bool statistics::path_toward_stair(int foe_floor)
{
	// plumbing: path_toward_stair: no world / invalid grid for our floor -> false
	if (!current_game || !current_game->world)
		return false;
	GameWorld* world = current_game->world;
	const int myfloor = controller_->floor();
	// rule: A ground walker whose foe is on another floor and unreachable heads for the nearest
	//   Z-stair leading toward the foe (an up-stair if the foe is above, a down-stair if below) and
	//   climbs; with no such stair on its floor it gives up on the stair route.
	// tests: ZAxis.path_toward_stair_answers_only_when_a_stair_leads_that_way
	// tests: SemAi4.a_walker_heads_for_the_nearest_up_stair_when_its_foe_is_above
	// tests: SemAi4.a_walker_heads_for_a_down_stair_when_its_foe_is_below
	// tests: SemAi4.a_stair_with_no_route_to_it_gives_no_stair_route
	if (foe_floor == myfloor)
		return false;

	// rule: A foe on a higher floor sends the walker toward an up stair, a foe on a lower floor
	//   toward a down stair.
	// tests: SemAi4.a_walker_heads_for_the_nearest_up_stair_when_its_foe_is_above
	// tests: SemAi4.a_walker_heads_for_a_down_stair_when_its_foe_is_below
	const unsigned char want = (foe_floor > myfloor)
	    ? static_cast<unsigned char>(PIX_ZSTAIR_UP)
	    : static_cast<unsigned char>(PIX_ZSTAIR_DOWN);

	const PixieData& g = world->grid_for_floor(myfloor);
	// plumbing: path_toward_stair: no world / invalid grid for our floor -> false
	if (!g.valid())
		return false;

	const int mygx = controller_->xpos() / GRID_SIZE;
	const int mygy = controller_->ypos() / GRID_SIZE;
	int best_x = -1, best_y = -1;
	long best_d = -1;
	// plumbing: row-major grid scan for stair tiles; see below
	for (int y = 0; y < g.h; ++y)
		// plumbing: row-major grid scan for stair tiles; see below
		for (int x = 0; x < g.w; ++x)
		{
			// rule: A ground walker whose foe is on another floor and unreachable heads for the
			//   nearest Z-stair leading toward the foe (an up-stair if the foe is above, a
			//   down-stair if below) and climbs.
			// tests: ZAxis.path_toward_stair_answers_only_when_a_stair_leads_that_way
			// tests: SemAi4.a_walker_heads_for_the_nearest_up_stair_when_its_foe_is_above
			// tests: SemAi4.a_walker_heads_for_a_down_stair_when_its_foe_is_below
			// tests: SemAi4.a_stair_with_no_route_to_it_gives_no_stair_route
			if (g.data[static_cast<std::size_t>(x + y * g.w)] != want)
				continue;
			const long d = std::abs(x - mygx) + std::abs(y - mygy);
			// rule: A ground walker whose foe is on another floor and unreachable heads for the
			//   nearest Z-stair leading toward the foe (an up-stair if the foe is above, a
			//   down-stair if below) and climbs.
			// tests: ZAxis.path_toward_stair_answers_only_when_a_stair_leads_that_way
			// tests: SemAi4.a_walker_heads_for_the_nearest_up_stair_when_its_foe_is_above
			// tests: SemAi4.a_walker_heads_for_a_down_stair_when_its_foe_is_below
			// tests: SemAi4.a_stair_with_no_route_to_it_gives_no_stair_route
			if (best_x < 0 || d < best_d)
			{
				best_d = d;
				best_x = x;
				best_y = y;
			}
		}
	// rule: A ground walker whose foe is on another floor and unreachable heads for the nearest
	//   Z-stair leading toward the foe (an up-stair if the foe is above, a down-stair if below) and
	//   climbs.
	// tests: ZAxis.path_toward_stair_answers_only_when_a_stair_leads_that_way
	// tests: SemAi4.a_walker_heads_for_the_nearest_up_stair_when_its_foe_is_above
	// tests: SemAi4.a_walker_heads_for_a_down_stair_when_its_foe_is_below
	// tests: SemAi4.a_stair_with_no_route_to_it_gives_no_stair_route
	if (best_x < 0)
		return false; // no stair toward the foe's floor on our level

	controller_->find_path_to_point(static_cast<short>(best_x * GRID_SIZE),
	                                static_cast<short>(best_y * GRID_SIZE));
	return !controller_->path_to_foe.empty();
}

inline constexpr int PATHING_MIN_DISTANCE = 100;

// Note that obmap::size() counts dead things too, which don't do pathfinding
inline constexpr int PATHING_SHORT_CIRCUIT_OBJECT_LIMIT = 200;

bool statistics::walk_to_foe()
{
    walker* foe = controller_->foe();
	float xdest, ydest;
	float xdelta, ydelta;
	Uint32 tempdistance = 9999999L;
	Sint32 howmany;

	// rule: A chase with no foe resets the 'closest ever' memory; independently, one chase tick in
	//   300 (random) also resets it and skips the move, so a bot stuck behind its best-ever
	//   distance eventually retries the straight line.
	// tests: StatsCommands.stats_walk_to_foe_no_foe_resets_distances
	// tests: StatsUnit.stats_r11_direct_walk_and_walk_to_foe_tail_branches
	// tests: SemAi4.a_chase_with_no_foe_or_the_one_in_300_draw_resets_its_memory_and_stands
	if (!foe || !rng(300) ) //random just to be sure this gets reset sometime
	{
		set_last_distance(15000L);
		set_current_distance(15000L);
		return 0;
	}

	// Cross-floor foe: distance_to_ob is a floor-blind 2D (Manhattan) measure,
	// so a foe directly above/below reads as "close" even though the only route
	// to it is a Z-stair. For such a foe we must always run the full,
	// stair-edge-aware A* (find_path_to_foe) and never short-circuit into the
	// floor-blind melee/direct chase that just runs under the target — nor
	// abandon the chase when the 2D distance gets tiny (we still have to climb).
	// Gated on floor_count()>1, so single-floor behavior is byte-identical.
	//
	// EXCEPT flyers. A flyer never changes floors (apply_z_motion skips it and
	// the pathing graph gives it no Z-stair edges), so a cross-floor foe is
	// unreachable for it BY DESIGN. Routing a flyer through the cross-floor
	// machinery (a) forced a stair-aware A* that had to exhaust every reachable
	// cell before failing — re-run EVERY tick once the empty path forced the
	// per-tick checks below — and (b) path_toward_stair then parked it ON a
	// stair tile the Z-lift will never take it through, where it hovered
	// pinned for the rest of the level (the Westlands L24 caldera-ghost
	// wedge). A flyer keeps the classic floor-blind 2D chase instead: it
	// shadows the foe from its own floor and engages whatever it meets there.
	// Same flying predicate as apply_z_motion (permanent BIT_FLYING or
	// transient flight ticks). Multifloor-gated, so single-floor levels are
	// byte-identical.
	const bool multifloor = current_game->world->floor_count() > 1;
	const bool flyer = multifloor &&
	    (query_bit_flags(BIT_FLYING) || controller_->flight_left() != 0);
	const bool cross_floor = multifloor && !flyer &&
	    foe->floor() != controller_->floor();

	controller_->set_path_check_counter(controller_->path_check_counter() - 1);
	// This makes us only check every few rounds, to save processing time.
	// EXCEPTION: a cross-floor foe with no current path. The path check is
	// throttled to every 5-14 ticks; until the first stair-aware A* runs we have
	// no path and fall through to direct_walk(), which charges the foe's x/y on
	// OUR floor — i.e. the baddie tracks the player on the wrong level for up to a
	// full cadence before "eventually" pathing through the stairs. Force the check
	// the moment we acquire (or exhaust the path to) a cross-floor foe so it heads
	// for the stairs immediately. Once a path exists this is false again, so the
	// normal throttle resumes (no A*-every-tick except for a truly unreachable foe).
	const bool need_cross_floor_path =
	    cross_floor && controller_->path_to_foe.empty();
	// rule: A chaser re-plans only every 5-14 ticks (random cadence); between re-plans it keeps
	//   following its path or the fallback walk. Exception: a ground walker whose foe is on another
	//   floor re-plans every tick until it has a path (so it heads for the stairs at once).
	// tests: StatsUnit.stats_r11_path_check_counter_roll_is_seed_deterministic
	// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
	// tests: StatsMorePaths.stats_walk_to_foe_short_circuit_and_path_branches
	// tests: SemAi4.a_chaser_whose_foe_is_on_another_floor_replans_at_once
	if(controller_->path_check_counter() <= 0 || need_cross_floor_path)
	{
	    // Cosmetic AI-recheck cadence: master draws it from libc rand
	    // (`5 + rand()%10`), NOT the gameplay rng() helper it uses for every
	    // other AI decision here. Production draws from world.rng_ for
	    // snapshot/replay determinism; the parity harness's libc-rand cosmetic
	    // override (when installed) matches master's dual-RNG-stream behavior.
	    // plumbing: the parity harness's cosmetic RNG override draws the recheck cadence when
	    //   installed, else the world RNG (determinism plumbing; the comment above says why).
	    const std::uint32_t cadence = (cosmetic_rng_override() != nullptr)
	        ? cosmetic_rng_override()->next(10)
	        : rng(10);
	    controller_->set_path_check_counter(5 + static_cast<int>(cadence));
	    controller_->path_to_foe.clear();
	    
		xdest = foe->xpos();
		ydest = foe->ypos();

		xdelta = xdest - controller_->xpos();
		ydelta = ydest - controller_->ypos();
        
		tempdistance = static_cast<Uint32>(controller_->distance_to_ob(foe));
		// Do simpler pathing if the distance is short or if there are too many walkers (pathfinding is expensive)
		// rule: On a re-plan tick, a same-floor chaser whose foe is within 100 px (Manhattan), or
		//   any chaser on a crowded map (more than 200 objects, dead included), skips A*: if any
		//   foe is within 100 px it wipes its orders, turns to the foe and starts an ATTACK of
		//   30+rng(25) rounds and fires; if no foe is that close it abandons its current order
		//   (front count zeroed) and falls through to the direct/right-hand walk.
		// tests: StatsUnit.stats_r11_direct_walk_and_walk_to_foe_tail_branches
		// tests: StatsUnit.walk_to_foe_adopts_a_strictly_nearer_foe_and_keeps_the_held_one_otherwise
		// tests: SemAi4.a_chaser_within_100_px_of_a_foe_attacks_it_instead_of_pathing
		// tests: SemAi4.a_crowded_map_skips_pathing_and_ends_the_order_when_no_foe_is_near
		if (!cross_floor &&
		    (tempdistance < PATHING_MIN_DISTANCE || current_game->world->myobmap->size() > PATHING_SHORT_CIRCUIT_OBJECT_LIMIT))
		{
			std::list<walker*> foelist = current_game->world->find_foes_in_range(current_game->world->oblist,
			          PATHING_MIN_DISTANCE, &howmany, controller_);
			// rule: On a re-plan tick, a same-floor chaser whose foe is within 100 px (Manhattan),
			//   or any chaser on a crowded map (more than 200 objects, dead included), skips A*.
			// tests: StatsUnit.stats_r11_direct_walk_and_walk_to_foe_tail_branches
			// tests: StatsUnit.walk_to_foe_adopts_a_strictly_nearer_foe_and_keeps_the_held_one_otherwise
			// tests: SemAi4.a_chaser_within_100_px_of_a_foe_attacks_it_instead_of_pathing, SemAi4.a_chaser_facing_its_foe_within_100_px_swings_only_with_the_setting_on
			// tests: SemAi4.a_crowded_map_skips_pathing_and_ends_the_order_when_no_foe_is_near
			if (howmany > 0)
			{
				clear_command();
				// #320: find_near_foe's result was thrown away since the 2002
				// import, so a walker kept its held foe while a nearer one
				// stood beside it. Adopt the returned foe when it is strictly
				// nearer than the held one (the Manhattan measure tempdistance
				// already holds) and aim this tick's turn, ATTACK and
				// init_fire at it. The find runs ahead of the ATTACK-duration
				// draw, so any cloak draw it makes comes first. A null result
				// means every foe in reach is cloaked: keep the held foe.
				walker* near_foe = current_game->world->find_near_foe(controller_);
				// rule: #320: at the melee short-circuit a chaser switches to a foe strictly nearer
				//   than the one it holds (Manhattan) and aims its turn/attack at it; an equal or
				//   farther result, or null (all in reach cloaked), keeps the held foe.
				// tests: StatsUnit.walk_to_foe_adopts_a_strictly_nearer_foe_and_keeps_the_held_one_otherwise
				// tests: parity:walk_to_foe_adopts_near_foe_scen99
				// tests: SemAi4.a_chaser_switches_to_a_strictly_nearer_foe_and_turns_to_it
				if (near_foe &&
				    static_cast<Uint32>(controller_->distance_to_ob(near_foe)) < tempdistance)
				{
					controller_->set_foe(near_foe);
					xdelta = near_foe->xpos() - controller_->xpos();
					ydelta = near_foe->ypos() - controller_->ypos();
				}
				controller_->turn(controller_->facing(xdelta, ydelta));
				controller_->stats()->try_command(COMMAND_ATTACK,static_cast<short>(30+ rng(25)), 1, 1);
				controller_->init_fire();
				return 1;
			}
			else // our foe has moved; we need a new one
			{
				// Zardus: PORT: needs to check for commandlist
				// rule: On a re-plan tick, a same-floor chaser whose foe is within 100 px
				//   (Manhattan), or any chaser on a crowded map (more than 200 objects, dead
				//   included), skips A*.
				// tests: StatsUnit.stats_r11_direct_walk_and_walk_to_foe_tail_branches
				// tests: StatsUnit.walk_to_foe_adopts_a_strictly_nearer_foe_and_keeps_the_held_one_otherwise
				// tests: SemAi4.a_chaser_within_100_px_of_a_foe_attacks_it_instead_of_pathing
				// tests: SemAi4.a_crowded_map_skips_pathing_and_ends_the_order_when_no_foe_is_near
				if (!commands.empty())
					commands.front().commandcount = 0;
			}
		}
		else if (!flyer || foe->floor() == controller_->floor())  // rule: A far (>= 100 px) foe on an
			//   uncrowded map is reached by A*; a ground walker that cannot path to a foe on
			//   another floor routes to the nearest stair toward it; a flyer never paths to a foe
			//   on another floor (it cannot change floors) and instead shadows the foe's 2D
			//   position from its own floor.
			// tests: ZAxis.flyer_with_cross_floor_foe_shadows_it_and_never_parks_on_stair
			// tests: ZAxis.cross_floor_ai_MOVING_foe_air_unreachable_still_climbs_toward_stair
			// tests: StatsMorePaths.stats_walk_to_foe_short_circuit_and_path_branches
        {
            controller_->find_path_to_foe();
            // Cross-floor foe whose exact cell A* cannot reach (sparse upper
            // floor / disconnected platform). Don't let the empty-path fallback
            // below chase the foe's x/y on OUR floor — head for the nearest
            // Z-stair toward the foe's floor and climb. Gated on cross_floor, so
            // single-floor pathing is untouched (byte-identical for parity).
            // rule: A far (>= 100 px) foe on an uncrowded map is reached by A*.
            // tests: ZAxis.flyer_with_cross_floor_foe_shadows_it_and_never_parks_on_stair
            // tests: ZAxis.cross_floor_ai_MOVING_foe_air_unreachable_still_climbs_toward_stair
            // tests: StatsMorePaths.stats_walk_to_foe_short_circuit_and_path_branches
            if (cross_floor && controller_->path_to_foe.empty())
                path_toward_stair(foe->floor());
        }
        // else: flyer with a cross-floor foe. No stair-aware A* (it can only
        // exhaust the map and fail: flyers get no stair edges) and no
        // path_toward_stair (a stair can never lift a flyer). The path stays
        // empty, so the direct_walk/right_walk fallbacks below shadow the
        // foe's 2D position from our own floor — the classic chase.
	} //end if do_check

    // rule: A chaser with a path follows it; without one it charges straight at the foe only when
    //   it is closer than it has ever been (a re-plan tick that measured a new best distance),
    //   otherwise it wall-follows. Because tempdistance is only measured on re-plan ticks (9999999
    //   otherwise), every throttled tick with no path is a right-hand-walk tick.
    // tests: StatsMorePaths.stats_walk_to_foe_short_circuit_and_path_branches
    // tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
    // tests: SemAi4.a_chaser_follows_its_path_or_charges_when_closer_than_ever
    if(controller_->path_to_foe.size() > 0)
    {
        controller_->follow_path_to_foe();
        set_last_distance(static_cast<Uint32>(controller_->distance_to_ob(foe)));
    }
    else if(tempdistance < last_distance())// are we closer than we've ever been?
	{
		set_last_distance(tempdistance);   // then set our checking distance ..

		// rule: A chaser with a path follows it.
		// tests: StatsMorePaths.stats_walk_to_foe_short_circuit_and_path_branches
		// tests: StatsUnit.stats_r14_hit_response_gates_and_direct_walk_and_blocked_defaults
		// tests: SemAi4.a_chaser_follows_its_path_or_charges_when_closer_than_ever
		if (!direct_walk())               // Can we walk in a direct line to foe?
		{
			right_walk();                   //   If not, use right-hand walking
		}
	}
	else
		right_walk();

	// Are we really really close? Stop searching, then :)  But never abandon a
	// cross-floor chase on 2D proximity — the foe is on another floor and we
	// still need to reach the Z-stair and climb.
	// Zardus: lets check if commandlist exists, too
	// rule: A same-floor chaser within 30 px of its foe (measured on a re-plan tick) ends its
	//   current order; a cross-floor chase never ends on 2D proximity.
	// tests: SemAi4.a_chaser_within_30_px_ends_its_order_only_on_a_replan_tick_on_its_floor
	if (!cross_floor && tempdistance < 30 && !commands.empty())
	{
		commands.front().commandcount = 0;
	}

	return 1;
}

// Distance at which a position goal counts as reached. Kept below a grid
// tile so the final approach overlaps the goal cell: anything standing there
// (a flag, a treasure) collides with the walker before the goal completes.
inline constexpr int GOTO_ARRIVAL_DISTANCE = 12;

// Maps a facing direction to a unit step delta (the same table right_walk
// builds inline with its turn switches).
static void facing_step_delta(char dir, short& xdelta, short& ydelta)
{
	// rule: Each of the 8 facings maps to its unit step (UP (0,-1), UR (1,-1), R (1,0), DR (1,1), D
	//   (0,1), DL (-1,1), L (-1,0), UL (-1,-1)); an invalid facing maps to no step. Used by the
	//   clinch breaker (above), path_probe's box (above) and right_walk_to_point
	//   (below, 1587); same table as right_walk's inline switches (above, 1048).
	// tests: SemAi4.a_point_walker_that_passed_a_corner_turns_right_and_queues_one_step_along_its_new_heading
	// tests: SemAi4.an_invalid_facing_maps_to_no_step
	switch (dir)
	{
		case FACE_UP:
			xdelta = 0;
			ydelta = -1;
			break;
		case FACE_UP_RIGHT:
			xdelta = 1;
			ydelta = -1;
			break;
		case FACE_RIGHT:
			xdelta = 1;
			ydelta = 0;
			break;
		case FACE_DOWN_RIGHT:
			xdelta = 1;
			ydelta = 1;
			break;
		case FACE_DOWN:
			xdelta = 0;
			ydelta = 1;
			break;
		case FACE_DOWN_LEFT:
			xdelta = -1;
			ydelta = 1;
			break;
		case FACE_LEFT:
			xdelta = -1;
			ydelta = 0;
			break;
		case FACE_UP_LEFT:
			xdelta = -1;
			ydelta = -1;
			break;
		default:
			xdelta = 0;
			ydelta = 0;
			break;
	}
}

// direct_walk, retargeted from the foe to a fixed point: take the straight
// line (snapped to an axis when one delta dominates 3:1), falling back to a
// single-axis step when the diagonal is grid-blocked. No foe involvement —
// position goals never auto-attack.
bool statistics::direct_walk_to_point(short x, short y)
{
	float xdelta, ydelta;
	float xdeltastep, ydeltastep;
	float controlx = controller_->xpos(), controly = controller_->ypos();

	xdelta = x - controlx;
	ydelta = y - controly;
	// rule: A walk toward a map point aims 8-way with the same 3:1 axis snap as the foe chase
	//   (above).
	// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
	// tests: SemAi4.a_walk_to_a_point_aims_eight_ways_with_the_three_to_one_snap
	if (std::fabs(xdelta) > std::fabs(3 * ydelta))
		ydelta = 0;
	if (std::fabs(ydelta) > std::fabs(3 * xdelta))
		xdelta = 0;

	// rule: A walk toward a map point aims 8-way with the same 3:1 axis snap as the foe chase
	//   (above).
	// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
	// tests: SemAi4.a_walk_to_a_point_aims_eight_ways_with_the_three_to_one_snap
	if (xdelta != 0.0f)
		xdelta = xdelta / std::fabs(xdelta);
	// rule: A walk toward a map point aims 8-way with the same 3:1 axis snap as the foe chase
	//   (above).
	// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
	// tests: SemAi4.a_walk_to_a_point_aims_eight_ways_with_the_three_to_one_snap
	if (ydelta != 0.0f)
		ydelta = ydelta / std::fabs(ydelta);

	xdeltastep = xdelta * controller_->stepsize();
	ydeltastep = ydelta * controller_->stepsize();

	// rule: A walk toward a point takes the diagonal unit step when the grid allows, else the x
	//   slide, else the y slide, and fails when walled or already there; it never attacks (same
	//   ladder as above).
	// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
	// tests: SemAi4.a_walk_to_a_point_slides_along_an_open_axis_and_fails_when_walled
	if (!current_game->world->query_grid_passable(controlx + xdeltastep, controly + ydeltastep, controller_))
	{
		// rule: A walk toward a point takes the diagonal unit step when the grid allows, else the x
		//   slide, else the y slide, and fails when walled or already there.
		// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
		// tests: SemAi4.a_walk_to_a_point_slides_along_an_open_axis_and_fails_when_walled
		if (!current_game->world->query_grid_passable(controlx + xdeltastep, controly + 0, controller_))
		{
			// rule: A walk toward a point takes the diagonal unit step when the grid allows, else
			//   the x slide, else the y slide, and fails when walled or already there.
			// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
			// tests: SemAi4.a_walk_to_a_point_slides_along_an_open_axis_and_fails_when_walled
			if (!current_game->world->query_grid_passable(controlx + 0, controly + ydeltastep, controller_))
			{
				// No straight-line move brings us closer to the point.
				return 0;
			}
			else // y ok
			{
				// rule: A walk toward a point takes the diagonal unit step when the grid allows,
				//   else the x slide, else the y slide, and fails when walled or already there.
				// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
				// tests: SemAi4.a_walk_to_a_point_slides_along_an_open_axis_and_fails_when_walled
				if (ydelta == 0.0f)
					return 0;
				controller_->walkstep(0.0f, ydelta);
				return 1;
			}
		}
		else // x ok
		{
			// rule: A walk toward a point takes the diagonal unit step when the grid allows, else
			//   the x slide, else the y slide, and fails when walled or already there.
			// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
			// tests: SemAi4.a_walk_to_a_point_slides_along_an_open_axis_and_fails_when_walled
			if (xdelta == 0.0f)
				return 0;
			controller_->walkstep(xdelta, 0.0f);
			return 1;
		}
	}
	else // x and y ok
	{
		// rule: A walk toward a point takes the diagonal unit step when the grid allows, else the x
		//   slide, else the y slide, and fails when walled or already there.
		// tests: StatsMorePaths.direct_walk_to_point_at_the_destination_issues_no_step
		// tests: SemAi4.a_walk_to_a_point_slides_along_an_open_axis_and_fails_when_walled
		if (xdelta == 0.0f && ydelta == 0.0f)
			return 0;
		controller_->walkstep(xdelta, ydelta);
		return 1;
	}
}

// right_walk, retargeted from the foe to a fixed point: identical wall
// following, but the open-space branch steps toward the point instead of
// running direct_walk's foe chase/attack.
bool statistics::right_walk_to_point(short x, short y)
{
	float xdelta, ydelta;
	short xstep, ystep;
	const bool quiet = og::sim::session_gameplay_fixes() != 0;   // Gameplay Fixes Y: a pathing probe changes nothing

	// rule: Wall following toward a map point: identical right-hand rule to right_walk
	//   (above) but the open-space arm steps toward the point and never attacks. Classic:
	//   loud probes (eat/open/bump like steps). Gameplay Fixes Y: quiet probes; unlike right_walk
	//   there is no melee-foe bump here (position goals never attack).
	// tests: GameplayFixesWalk.right_walk_to_point_probes_quietly
	// tests: SemAi4.a_point_walker_follows_the_right_hand_rule_and_steps_at_its_point_in_the_open
	// tests: SemAi4.a_point_walker_that_passed_a_corner_turns_right_and_queues_one_step_along_its_new_heading
	if (right_blocked(quiet) || right_forward_blocked(quiet))
	{
		// rule: Wall following toward a map point: identical right-hand rule to right_walk
		//   (above) but the open-space arm steps toward the point and never attacks.
		// tests: GameplayFixesWalk.right_walk_to_point_probes_quietly
		// tests: SemAi4.a_point_walker_follows_the_right_hand_rule_and_steps_at_its_point_in_the_open
		// tests: SemAi4.a_point_walker_that_passed_a_corner_turns_right_and_queues_one_step_along_its_new_heading
		if (!forward_blocked(quiet))  // walk forward
		{
			xdelta = controller_->lastx();
			ydelta = controller_->lasty();
			// rule: Same as above: hugging a wall keeps the last movement direction.
			// tests: GameplayFixesWalk.right_walk_to_point_probes_quietly
			// tests: SemAi4.a_point_walker_hugging_a_wall_keeps_its_last_direction
			if (std::fabs(xdelta) > std::fabs(3 * ydelta))
				ydelta = 0;
			if (std::fabs(ydelta) > std::fabs(3 * xdelta))
				xdelta = 0;
			if (xdelta != 0.0f)
				xdelta /= std::fabs(xdelta);
			if (ydelta != 0.0f)
				ydelta /= std::fabs(ydelta);
			return controller_->walkstep(xdelta, ydelta);
		}
		else  // turn left
		{
			controller_->set_enddir(static_cast<char>((controller_->enddir() + 6) % 8));
			return controller_->turn(controller_->enddir());
		}
	}
	else if (forward_blocked(quiet))
	{
		controller_->set_enddir(static_cast<char>((controller_->enddir() + 6) % 8));  // turn left
		return controller_->turn(controller_->enddir());
	}
	else if (right_back_blocked(quiet))
	{
		controller_->set_enddir(static_cast<char>((controller_->enddir() + 2) % 8));  // turn right
		facing_step_delta(controller_->enddir(), xstep, ystep);
		add_command(COMMAND_WALK, 1, xstep, ystep);
	}
	else
	{
		// rule: Wall following toward a map point: identical right-hand rule to right_walk
		//   (above) but the open-space arm steps toward the point and never attacks.
		// tests: GameplayFixesWalk.right_walk_to_point_probes_quietly
		// tests: SemAi4.a_point_walker_follows_the_right_hand_rule_and_steps_at_its_point_in_the_open
		// tests: SemAi4.a_point_walker_that_passed_a_corner_turns_right_and_queues_one_step_along_its_new_heading
		if (!direct_walk_to_point(x, y)) // can't walk straight to the point
		{
			facing_step_delta(controller_->curdir(), xstep, ystep);
			return controller_->walkstep(xstep, ystep);
		}
	}
	return true;
}

// walk_to_foe, retargeted from the foe to a fixed point: same path-check
// cadence and crowd/proximity short-circuits, the same follow-the-path /
// direct / right-hand fallback ladder, but the goal is a map position and
// arrival (GOTO_ARRIVAL_DISTANCE) completes the active command instead of
// handing off to combat. Only the CTF director issues position goals, so
// every rng() draw here stays off the classic command paths.
bool statistics::walk_to_point(short x, short y)
{
	Uint32 tempdistance;

	// rule: One GOTO tick in 300 (random) skips the move and resets the closest-ever memory (same
	//   rule as above); the order still counts down.
	// tests: ModesCtf.carrier_runs_home_distance_shrinks_each_cadence_then_captures
	// tests: SemAi4.a_point_walk_skips_one_tick_in_300_and_resets_its_memory
	if (!rng(300)) // random just to be sure this gets reset sometime
	{
		set_last_distance(15000L);
		set_current_distance(15000L);
		return 0;
	}

	tempdistance = static_cast<Uint32>(abs(x - controller_->xpos()) +
	                                   abs(y - controller_->ypos()));

	// Arrived: finish whatever remains of the active command.
	// rule: A position goal is reached within 12 px (Manhattan, less than a tile so the walker
	//   overlaps anything standing on the goal cell, e.g. a flag); arrival clears the path and ends
	//   the active GOTO.
	// tests: ModesCtf.carrier_runs_home_distance_shrinks_each_cadence_then_captures
	// tests: SemAi4.a_point_goal_is_reached_within_12_px
	if (tempdistance < GOTO_ARRIVAL_DISTANCE)
	{
		controller_->path_to_foe.clear();
		// rule: A position goal is reached within 12 px (Manhattan, less than a tile so the walker
		//   overlaps anything standing on the goal cell, e.g. a flag).
		// tests: ModesCtf.carrier_runs_home_distance_shrinks_each_cadence_then_captures
		// tests: SemAi4.a_point_goal_is_reached_within_12_px
		if (!commands.empty())
			commands.front().commandcount = 0;
		return 1;
	}

	controller_->set_path_check_counter(controller_->path_check_counter() - 1);
	// This makes us only check every few rounds, to save
	// processing time
	// rule: A GOTO re-plans every 5-14 ticks, and only a far (>= 100 px) goal on an uncrowded map
	//   (<= 200 objects) gets an A* route; near or crowded goals use the direct/right-hand walk.
	// tests: SemAi4.a_point_walk_replans_on_its_cadence_and_paths_only_far_uncrowded_goals
	if (controller_->path_check_counter() <= 0)
	{
		controller_->set_path_check_counter(5 + static_cast<int>(rng(10)));
		controller_->path_to_foe.clear();

		// Do simpler pathing if the distance is short or if there are too
		// many walkers (pathfinding is expensive)
		// rule: A GOTO re-plans every 5-14 ticks, and only a far (>= 100 px) goal on an uncrowded
		//   map (<= 200 objects) gets an A* route.
		// tests: SemAi4.a_point_walk_replans_on_its_cadence_and_paths_only_far_uncrowded_goals
		if (tempdistance >= PATHING_MIN_DISTANCE &&
		    current_game->world->myobmap != nullptr &&
		    current_game->world->myobmap->size() <= PATHING_SHORT_CIRCUIT_OBJECT_LIMIT)
		{
			controller_->find_path_to_point(x, y);
		}
	} //end if do_check

	// rule: A GOTO follows its A* path when it has one; otherwise it walks straight at the goal
	//   whenever it is closer than ever before (distance measured every tick here, unlike
	//   walk_to_foe), and wall-follows when it is not or when the straight step is blocked.
	// tests: SemAi4.a_point_walk_follows_its_path_or_charges_or_hugs_the_wall
	if (controller_->path_to_foe.size() > 0)
	{
		controller_->follow_path_to_foe();
		set_last_distance(tempdistance);
	}
	else if (tempdistance < last_distance()) // are we closer than we've ever been?
	{
		set_last_distance(tempdistance);   // then set our checking distance ..

		// rule: A GOTO follows its A* path when it has one.
		// tests: SemAi4.a_point_walk_follows_its_path_or_charges_or_hugs_the_wall
		if (!direct_walk_to_point(x, y))  // Can we walk in a direct line to the point?
		{
			right_walk_to_point(x, y);     //   If not, use right-hand walking
		}
	}
	else
		right_walk_to_point(x, y);

	return 1;
}

// Stuff for command?
command::command()
{
	commandtype = 0;
	commandcount = 0;
	com1 = com2 = 0;
}

// Gameplay Fixes AQ (#434): one half of a seat's slide along a wall
// (walker::walkstep, the user arm): the walker's box moved one pixel along
// one axis. Classic: the loud probe, a real step whether the slide takes that
// half or not: on a half a body blocks it eats a treasure it reaches before
// the body, bumps the body (a swing at a foe), opens a door the walker holds
// the key to and posts "Key N needed!" at a locked one. With the setting on
// the half is asked quietly first, as the right-hand walk's probes are (fix
// Y); only a free half takes the loud step, so the pixel the slide moves onto
// still acts. Asked quietly, a living gets the loud answer
// (QuietProbe.every_blocker_answers_like_the_loud_probe_and_changes_nothing).
bool statistics::slide_half_clear(short dx, short dy)
{
	const float x = static_cast<float>(controller_->xpos() + dx);
	const float y = static_cast<float>(controller_->ypos() + dy);
	// rule: Gameplay Fixes AQ (#434): a seat's wall slide asks each half quietly first; a blocked half
	//   eats nothing, swings at nothing, opens no door and posts no key notice, and only a free half
	//   takes the classic loud step (it eats what it moves onto). Classic: every half's probe is the
	//   loud step, taken or not.
	// tests: GameplayFixesWalk.a_seats_wall_slide_eats_nothing_on_the_half_it_does_not_take_with_the_setting_on
	// tests: GameplayFixesWalk.a_seats_wall_slide_swings_at_no_foe_on_the_half_it_does_not_take_with_the_setting_on
	// tests: GameplayFixesWalk.a_seats_wall_slide_still_eats_what_it_slides_onto_on_both_values
	// tests: parity:player_slide_potion_scen99, parity:player_slide_potion_fixes_scen99
	if (og::sim::session_gameplay_fixes() != 0
	        && !current_game->world->query_passable(x, y, controller_, controller_->floor(), /*quiet=*/true))
		return false;
	return current_game->world->query_passable(x, y, controller_);
}
