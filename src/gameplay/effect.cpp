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

#include <cstdint>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/effect.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/core/test_trace.h>

effect::effect(const PixieData& data)
    : walker(data)
{
	set_ignore(1); // don't collide with other objects
}

effect::effect()
    : walker()
{
	set_ignore(1);
}

effect::~effect()
{
	// Zardus: PORT: that parent object problem again:  walker::~walker();
}

bool effect::act()
{
	// Make sure everyone we're pointing to is valid
	// rule: An effect forgets a foe, leader or owner that has died at the start of its tick, so a
	//   dead caster no longer steers, owns or credits it.
	// tests: EffectExtended.effect_ctor_defaults_and_owner_pointer_cleanup_in_act
	// tests: SemWalker1.an_effect_keeps_its_living_foe_leader_and_owner
	if (foe() && foe()->dead())
		set_foe(nullptr);
	if (leader() && leader()->dead())
		set_leader(nullptr);
	if (owner() && owner()->dead())
		set_owner(nullptr);

	set_collide_ob(nullptr); // always start with no collision..

	// Runtime headless/network sims advance drawcycle here so effect motion is
	// not dependent on a renderer-side draw path.
	set_drawcycle(static_cast<unsigned char>(drawcycle() + 1));

	// Per-family() action dispatch
	const auto* efd = get_effect_family_descriptor(family());
	// rule: An effect family's Lua on_act owns its tick when it returns true; returning false (or
	//   no hook) hands the tick to the default: finish the playing animation, then die.
	// tests: EffectAct.magic_shield_orbit_step_and_ageing
	// tests: EffectAct.explosion_act_advances_drawcycle_animates_then_dies
	// tests: EffectMorePaths.effect_round8_default_act_death_path
	// tests: SemWalker1.an_effect_whose_on_act_declines_animates_then_dies
	// tests: SemWalker1.an_effect_whose_on_act_claims_the_tick_neither_animates_nor_dies
	if (auto hook_result = og::script::hooks::effect_on_act(efd, this))
	{
		// rule: An effect family's Lua on_act owns its tick when it returns true; returning false
		//   (or no hook) hands the tick to the default: finish the playing animation, then die.
		//   Here: `*hook_result`.
		// tests: EffectAct.magic_shield_orbit_step_and_ageing
		// tests: EffectAct.explosion_act_advances_drawcycle_animates_then_dies
		// tests: EffectMorePaths.effect_round8_default_act_death_path
		// tests: SemWalker1.an_effect_whose_on_act_declines_animates_then_dies
		// tests: SemWalker1.an_effect_whose_on_act_claims_the_tick_neither_animates_nor_dies
		if (*hook_result)
			return 1;
		// on_act returned false: fall through to animate/die below
	}

	// Complete previous animations (like firing)
	// rule: A default effect plays its animation row through to the end and dies on the tick after
	//   it returns to the walk row (its death hook fires then).
	// tests: EffectAct.explosion_act_advances_drawcycle_animates_then_dies
	// tests: EffectMorePaths.effect_round8_default_act_death_path
	// tests: SemWalker1.a_default_effect_on_its_walk_row_dies_and_runs_its_death_hook
	if (ani_type() != ANI_WALK)
		return animate();

	set_dead(1);
	death();

	return 0;
}

bool effect::animate()
{
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	if (!ani)
		return 0;

	// curdir/ani_type/cycle may originate from an untrusted snapshot. Bound the
	// facing, the animation type against this family's actual table length, and
	// the cycle against the sequence sentinel before indexing, to avoid reading
	// past the end of a short animation table (and dereferencing a wild pointer).
	int dir_index = static_cast<int>(static_cast<unsigned char>(curdir()));
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	if (dir_index < 0 || dir_index >= NUM_FACINGS)
		dir_index = 0;
	int type_index = static_cast<int>(ani_type());
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	if (type_index < ANI_WALK)
		type_index = ANI_WALK;

	int ani_index = dir_index + type_index * NUM_FACINGS;
	// For real entities the loader records this family's table length (ani_count);
	// reject an out-of-range row from an untrusted snapshot by falling back to the
	// walk row (always < ani_count for real tables). ani_count == 0 only for
	// test-built effects that assign `ani` directly (legacy direct-index behavior).
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	if (ani_count > 0 && ani_index >= ani_count)
	{
		set_ani_type(ANI_WALK);
		ani_index = dir_index;
	}
	const signed char* seq = ani[ani_index];
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	if (!seq)
		return 0;

	int seq_len = 0;
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	while (seq_len < 128 && seq[seq_len] != -1)
		seq_len++;
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	if (seq_len <= 0 || seq_len >= 128)
		return 0;

	int c = static_cast<int>(cycle());
	// plumbing: Null ani table, facing/type/row/cycle bounds against untrusted snapshot values,
	//   sequence-length scan loop and malformed sequence guards.
	if (c < 0 || c >= seq_len)
		c = 0;
	set_frame(seq[c]);
	c++;                                 // advance into [1, seq_len]; seq[seq_len] is the -1 sentinel

	const auto* efd = get_effect_family_descriptor(family());
	// rule: A looping effect (magic shield, boomerang orbit etc., loops_animation=true) wraps its
	//   animation forever and stays on its row; a non-looping effect drops to the walk row when its
	//   sequence ends, which makes the next act kill it.
	// tests: EffectAct.effect_animate_magic_shield_loops_without_resetting_ani_type
	// tests: EffectAct.effect_animate_explosion_reads_the_row_and_ends_at_the_sentinel
	if (efd && efd->loops_animation)
	{
		// rule: A looping effect (magic shield, boomerang orbit etc., loops_animation=true) wraps
		//   its animation forever and stays on its row.
		// tests: EffectAct.effect_animate_magic_shield_loops_without_resetting_ani_type
		// tests: EffectAct.effect_animate_explosion_reads_the_row_and_ends_at_the_sentinel
		if (seq[c] == -1)
			c = 0;
		set_cycle(static_cast<signed char>(c));
	}
	else
	{
		// rule: A looping effect (magic shield, boomerang orbit etc., loops_animation=true) wraps
		//   its animation forever and stays on its row.
		// tests: EffectAct.effect_animate_magic_shield_loops_without_resetting_ani_type
		// tests: EffectAct.effect_animate_explosion_reads_the_row_and_ends_at_the_sentinel
		if (seq[c] == -1)
			set_ani_type(ANI_WALK);
		set_cycle(static_cast<signed char>(c));
	}

	return 1;
}

// death is called when an object dies (or weapon destructed, etc.)
// for special effects ..
bool effect::death()
{
	// Note that the 'dead()' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)
	// rule: An effect's death consequence (explosion blast, scare) happens exactly once however
	//   many times death() is reached.
	// tests: EffectMorePaths.effect_round9_death_called_guard_returns_false_on_second_call
	// tests: EffectDeathMore.effect_death_bomb_spawns_explosion_with_owner_and_damage
	// tests: SemWalker1.an_effect_dies_only_once_however_often_death_runs
	if (death_called())
		return 0;
	set_death_called(1);

	const auto* efd = get_effect_family_descriptor(family());
	og::script::hooks::effect_on_death(efd, this);

	return 1;
}

short hits(short x,  short y,  short xsize,  short ysize,
           short x2, short y2, short xsize2, short ysize2)
{
	short xright, x2right;
	short ydown,  y2down;

	//return 0; // debug
	x2right = static_cast<short>(x2+xsize2);
	// rule: Two boxes hit when they overlap on both axes, edges inclusive (touching edges count as
	//   a hit).
	// tests: EffectExtended.hits_exact_touching, EffectMorePaths.effect_round11_hits_edge_touches
	// tests: EffectMorePaths.effect_round10_hits_overlap_and_axis_reject_paths
	if (x > x2right)
		return 0;

	xright = static_cast<short>(x+xsize);
	// rule: Two boxes hit when they overlap on both axes, edges inclusive (touching edges count as
	//   a hit). Here: `x+xsize < x2`.
	// tests: EffectExtended.hits_exact_touching, EffectMorePaths.effect_round11_hits_edge_touches
	// tests: EffectMorePaths.effect_round10_hits_overlap_and_axis_reject_paths
	if (xright < x2)
		return 0;

	y2down = static_cast<short>(y2+ysize2);
	// rule: Two boxes hit when they overlap on both axes, edges inclusive (touching edges count as
	//   a hit). Here: `y > y2+ysize2`.
	// tests: EffectExtended.hits_exact_touching, EffectMorePaths.effect_round11_hits_edge_touches
	// tests: EffectMorePaths.effect_round10_hits_overlap_and_axis_reject_paths
	if (y > y2down)
		return 0;

	ydown = static_cast<short>(y+ysize);
	// rule: Two boxes hit when they overlap on both axes, edges inclusive (touching edges count as
	//   a hit). Here: `y+ysize < y2`.
	// tests: EffectExtended.hits_exact_touching, EffectMorePaths.effect_round11_hits_edge_touches
	// tests: EffectMorePaths.effect_round10_hits_overlap_and_axis_reject_paths
	if (ydown < y2)
		return 0;

	return 1;
}
