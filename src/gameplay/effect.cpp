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
#include <openglad/gameplay/effect.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/core/test_trace.h>

effect::effect(const PixieData& data)
    : walker(data)
{
	ignore = 1; // don't collide with other objects
}

effect::effect()
    : walker()
{
	ignore = 1;
}

effect::~effect()
{
	// Zardus: PORT: that parent object problem again:  walker::~walker();
}

void orbit_offset(int drawcycle, float &xd, float &yd)
{
    static const float orbit_table[16][2] = {
        {  0, -24}, { -9, -22}, {-17, -17}, {-22,  -9},
        {-24,   0}, {-22,   9}, {-17,  17}, { -9,  22},
        {  0,  24}, {  9,  22}, { 17,  17}, { 22,   9},
        { 24,   0}, { 22,  -9}, { 17, -17}, {  9, -22},
    };
    int idx = drawcycle % 16;
    xd = orbit_table[idx][0];
    yd = orbit_table[idx][1];
}

bool effect::act()
{
	// Make sure everyone we're pointing to is valid
	if (foe() && foe()->dead)
		set_foe(nullptr);
	if (leader() && leader()->dead)
		set_leader(nullptr);
	if (owner() && owner()->dead)
		set_owner(nullptr);

	set_collide_ob(nullptr); // always start with no collision..

	// Per-family action dispatch
	const auto* efd = get_effect_family_descriptor(family);
	if (efd && efd->on_act)
	{
		if (efd->on_act(this))
			return 1;
		// on_act returned false: fall through to animate/die below
	}

	// Complete previous animations (like firing)
	if (ani_type != ANI_WALK)
		return animate();

	dead = 1;
	death();

	return 0;
}

bool effect::animate()
{

	set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle]);
	cycle++;

	const auto* efd = get_effect_family_descriptor(family);
	if (efd && efd->loops_animation)
	{
		if (ani[curdir+ani_type*NUM_FACINGS][cycle] == -1)
			cycle = 0;
	}
	else
	{
		if (ani[curdir+ani_type*NUM_FACINGS][cycle] == -1)
			ani_type = ANI_WALK;
	}

	return 1;
}

std::int32_t compute_explosion_range(std::int32_t level, short skip_exit)
{
    std::int32_t range = level * 4;
    if (skip_exit > 0)
        range = 0;
    if (range > 96)
        range = 96;
    if (range < 16)
        range = 16;
    return range;
}

// death is called when an object dies (or weapon destructed, etc.)
// for special effects ..
bool effect::death()
{
	// Note that the 'dead' variable should ALREADY be set by the
	// time this function is called, so that we can easily reverse
	// the decision :)
	if (death_called)
		return 0;
	death_called = 1;

	const auto* efd = get_effect_family_descriptor(family);
	if (efd && efd->on_death)
		efd->on_death(this);

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
