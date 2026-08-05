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
// OBMAP -- an object to handle locations of pixies on a hash table.
#include <cstdint>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/key_mask.h>
#include <openglad/core/util.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <algorithm>
#include <format>
#include <openglad/gameplay/sim_emit.h>


short ob_pass_check(short x, short y, walker* ob, const std::list<walker*>& pile, const obmap* map);
short collide(short x,  short y,  short xsize,  short ysize,
              short x2, short y2, short xsize2, short ysize2);

//#define XRES GRID_SIZE
//#define YRES GRID_SIZE
inline constexpr int OBRES = 32; //GRID_SIZE

// Floor-keyed bucketing for multi-floor levels: each floor occupies a distinct
// numx band [floor*OB_FLOOR_STRIDE, floor*OB_FLOOR_STRIDE+199] so entities on
// different floors never share a collision pile. Floor 0 maps to [0,199],
// byte-identical to pre-Z. Clamped to keep the pos_to_walker short key in range.
inline constexpr int OB_FLOOR_STRIDE = 256;
static short floor_numx_offset(const walker* ob)
{
	int f = ob ? static_cast<int>(ob->floor()) : 0;
	if (f < 0) f = 0;
	if (f > 120) f = 120;
	return static_cast<short>(f * OB_FLOOR_STRIDE);
}

// These are passed in as PIXEL coordinates now...
obmap::obmap()
{
	obmapres = OBRES;
	
	pos_to_walker.clear();
	walker_to_pos.clear();
}

obmap::~obmap()
{}

size_t obmap::size() const
{
    return walker_to_pos.size();
}

short obmap::query_list(walker  *ob, short x, short y)
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;
	if (!ob || ob->dead())
	{
		Log("Bad ob to query_list.\n");
		return 1;
	}
	const short fo = floor_numx_offset(ob);
	startnumx = static_cast<short>(hash(x) + fo);
	endnumx   = static_cast<short>(hash( static_cast<short>(x+ob->sizex()) ) + fo);
	startnumy = hash(y);
	endnumy   = hash( static_cast<short>(y+ob->sizey()) );

	// For each y grid row we are in...
	for (numx = startnumx; numx <= endnumx; numx++)
	{
		for (numy = startnumy; numy <= endnumy; numy++)
		{
			// Avoid default-constructing empty piles during queries.
			auto it = pos_to_walker.find(std::make_pair(numx, numy));
			if (it == pos_to_walker.end())
				continue;
			// Snapshot the pile: collision handlers inside ob_pass_check
			// (e.g. walker::death → obmap::remove) can erase this
			// pos_to_walker entry, which would leave a dangling reference
			// and corrupt iterator traversal — the root cause of the
			// walker_to_pos.find() infinite-loop hang.
			auto pile_snapshot = it->second;
			if (!ob_pass_check(x, y, ob, pile_snapshot, this))
				return 0;
		}
	}
	return 1;
}


short obmap::remove(walker  *ob)  // This goes in walker's destructor
{
    // Find all of the instances of the object and remove them from the map
    // Some of this is redundant, but its worth avoiding segfaults, IMO :-)
    if (!ob)
        return 0;

    // Fast path only: removal is driven by walker_to_pos bookkeeping.
    // Do not scan the entire pos_to_walker map in production; that can turn
    // legitimate gameplay/test loops into O(N^2) behavior.
    // Get the list of positions that the walker occupies
    auto e = walker_to_pos.find(ob);
    if (e == walker_to_pos.end())
    {
        // Bookkeeping mismatch: attempt a bounded removal based on the
        // walker's current bounding box. This avoids global scans while still
        // preventing stale pointers in pos_to_walker (ASan/UAF).
        if (ob->xpos() < 0 || ob->ypos() < 0)
            return 0;

        short numx, startnumx, endnumx;
        short numy, startnumy, endnumy;
        const short fo = floor_numx_offset(ob);
        startnumx = static_cast<short>(hash(ob->xpos()) + fo);
        endnumx = static_cast<short>(hash(static_cast<short>(ob->xpos() + ob->sizex())) + fo);
        startnumy = hash(ob->ypos());
        endnumy = hash(static_cast<short>(ob->ypos() + ob->sizey()));

        bool removed_any = false;
        for (numx = startnumx; numx <= endnumx; numx++)
        {
            for (numy = startnumy; numy <= endnumy; numy++)
            {
                auto g = pos_to_walker.find(std::make_pair(numx, numy));
                if (g == pos_to_walker.end())
                    continue;
                const size_t before = g->second.size();
                g->second.remove(ob);
                if (g->second.size() != before)
                    removed_any = true;
                if (g->second.empty())
                    pos_to_walker.erase(g);
            }
        }
        return removed_any ? 1 : 0;
    }

    // For each occupied cell, remove all occurrences (defensive against
    // accidental duplicates) and drop empty piles.
    for (auto& pos : e->second)
    {
        auto g = pos_to_walker.find(pos);
        if (g == pos_to_walker.end())
            continue;
        g->second.remove(ob);
        if (g->second.empty())
            pos_to_walker.erase(g);
    }

    // Erase the walker from the walker map too
    walker_to_pos.erase(e);
    return 1;
}

short obmap::add(walker  *ob, short x, short y)  // This goes in walker's constructor
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;

	if (!ob)
		return 0;

	if (x < 0 || y < 0)
		return 0;

	// Tighten invariants: a walker may only be present once. If callers try to
	// add it again, remove via O(1) bookkeeping first.
	if (walker_to_pos.find(ob) != walker_to_pos.end())
		(void)remove(ob);

	const short fo = floor_numx_offset(ob);
	startnumx = static_cast<short>(hash(x) + fo);
	endnumx   = static_cast<short>(hash( static_cast<short>(x + ob->sizex()) ) + fo);
	startnumy = hash(y);
	endnumy   = hash( static_cast<short>(y + ob->sizey()) );

    // Figure out all of the positions that are occupied
	std::list<std::pair<short,short> > pos;
	for (numx = startnumx; numx <= endnumx; numx++)
	{
		for (numy = startnumy; numy <= endnumy; numy++)
		{
		    // Store this position
		    pos.push_back(std::make_pair(numx, numy));
		    
			// Put the walker here too (no duplicates).
			auto& pile = pos_to_walker[std::make_pair(numx, numy)];
			if (std::find(pile.begin(), pile.end(), ob) == pile.end())
				pile.push_back(ob);
				}
			}
		
	// Now record where he is
	walker_to_pos[ob] = std::move(pos);
			
	return 1;
}


short obmap::move(walker* ob, short x, short y)  // This goes in walker's setxy
{
    // Do we really need to move?
	if(x == ob->xpos() && y == ob->ypos())
        return 1;

	remove(ob);
	add(ob, x, y);
	return 1;
}


short obmap::hash(short y)
{
	//  For now, this assumes no one is smaller than size 8
	//  Also note that the hash table never loops.
	// return (y/8);
	short num = static_cast<short>(y/OBRES) ;
	if (num > 198)
		num = 199;
	if (num < 0)
		num = 199;
	return num;
}

short obmap::unhash(short y)
{
    return y*OBRES;
}

std::list<walker*>& obmap::obmap_get_list(short x, short y, int floor)
{
	if (floor < 0) floor = 0;
	if (floor > 120) floor = 120;
	const short fo = static_cast<short>(floor * OB_FLOOR_STRIDE);
	return pos_to_walker[std::make_pair(static_cast<short>(hash(x) + fo), hash(y))];
}

/***********************************************
**  All pass checking from here down.
***********************************************/
short ob_pass_check(short x, short y, walker* ob, const std::list<walker*>& pile, const obmap* map)
{
	short oxsize, oysize;
	short x2, y2, xsize2, ysize2;
	Order targetorder;//, targetteam, targetfamily;
	Order myorder;

	if (!ob)
		return 1;

	oxsize = ob->sizex();
	oysize = ob->sizey();

	myorder = ob->query_order();

	// Check each object to see if sizes collide.
	for (auto it = pile.begin(); it != pile.end(); )
	{
		// Advance the iterator up-front: collision handlers may remove elements
		// from the obmap piles (including the current element), and std::list
		// iterator invalidation would otherwise cause a UAF when the loop
		// increments.
		walker* w = *it;
		++it;

	    if (w != ob)
        {
            // If bookkeeping is consistent, any live walker in a pile must also
            // be present in walker_to_pos. If not, treat it as stale and skip
            // without dereferencing (ASan UAF hardening).
            if (map != nullptr && map->walker_to_pos.find(w) == map->walker_to_pos.end())
                continue;

            if (w->dead())
                continue;

            targetorder = w->query_order();
            
            // Let our own team's weapons pass over us
            if((targetorder == Order::Weapon || myorder == Order::Weapon) && ob->is_friendly(w))
                continue;
            // Allow weapons to sometimes 'miss' opposing team's weapons
            else if(targetorder == Order::Weapon && myorder == Order::Weapon && (current_game->world->rng_.next(10) > 3))
                continue;
            // Weapons never hit treasure
            else if(targetorder == Order::Treasure && myorder == Order::Weapon)
                continue;
            else
            {
                // Cylinder z-overlap: skip pairs whose height ranges don't
                // overlap. sizez()==0 is the full-height sentinel (legacy
                // entities), so single-floor collision is unchanged.
                if (ob->sizez() > 0 && w->sizez() > 0)
                {
                    const float oz1 = ob->worldz();
                    const float oz2 = oz1 + static_cast<float>(ob->sizez());
                    const float wz1 = w->worldz();
                    const float wz2 = wz1 + static_cast<float>(w->sizez());
                    if (oz2 <= wz1 || wz2 <= oz1)
                        continue;
                }
                x2 = w->xpos();
                y2 = w->ypos();
                xsize2 = w->sizex();
                ysize2 = w->sizey();
                if(collide(x,y,oxsize,oysize,x2,y2,xsize2,ysize2))
                {
                    if ( targetorder == Order::Treasure )
                    {
                        // #160: exit pads fire once per CONTACT, not once per
                        // skip_exit cooldown. Every movement probe lands here,
                        // so held-direction walking on the pad used to re-run
                        // the exit prompt / "Foes remain!" toast each time the
                        // 10-tick cooldown expired (and in MP every re-prompt
                        // froze the sim for everyone). The prober latches the
                        // pad's rect on its first eaten contact and skips
                        // exit-pad eats until it has fully left the rect
                        // (cleared each tick in living::act via
                        // update_exit_latch()). Non-exit treasures are
                        // unchanged; skip_exit stays as a secondary bound
                        // (it also throttles teleporters/door toasts and
                        // feeds game_server's prompt-routing sniff).
                        if (w->family() == FAMILY_EXIT)
                        {
                            if (ob->exit_latched())
                                continue;
                            ob->latch_exit_contact(w);
                        }
                        w->eat_me(ob);
                    }
                    else if ( (targetorder == Order::Weapon)
                              && (w->family() == FAMILY_DOOR) )
                    {
                        // Can we unlock this door?
                        if (ob->keys() & static_cast<std::uint32_t>(key_level_mask(w->stats()->level())))
                        {
                            // Open the door ..
                            w->set_dead(1);
                            w->death();
                            ob->collide(w);
                            if (ob->stats()->query_bit_flags(BIT_NO_COLLIDE))
                                return 1;
                            return 0; // block for this round
                        } // end of unlocked door
                        else
                        {
                            // Do we notify?
                            if (!(ob->skip_exit()) && (ob->user() != -1))
                            {
                                std::string message = std::format("Key {} needed!",
                                        w->stats()->level());
                                og::sim::emit_notification(current_game->sim_events, message);
                                ob->set_skip_exit(10);
                            } // end of failed open door notification
                            ob->collide(w);
                            return 0; // failed to open door
                        }
                    } // end of door case
                    else
                    {
                        // Interpenetration escape rule (2026-07-07, part of
                        // the guard-standoff wedge fix — see
                        // docs/GAMEPLAY_FIXES_FROM_CLASSIC.md). A transient
                        // pass-through can leave two livings overlapped;
                        // classically that state was ABSORBING: every step
                        // the trapped walker tried still collided with the
                        // body around it, so the pair stood merged forever —
                        // an unkillable shield (weapons died on whichever of
                        // the two they met first). If we ALREADY overlap this
                        // living where we stand, let any strictly separating
                        // move pass (it may still be blocked by someone
                        // else); only non-separating moves collide. Purely
                        // integer arithmetic, no RNG, and inert unless the
                        // boxes already interpenetrate.
                        if (myorder == Order::Living &&
                            targetorder == Order::Living &&
                            collide(ob->xpos(), ob->ypos(), oxsize, oysize,
                                    x2, y2, xsize2, ysize2))
                        {
                            const std::int32_t tx = 2 * x2 + xsize2;
                            const std::int32_t ty = 2 * y2 + ysize2;
                            const std::int32_t nx = 2 * x + oxsize - tx;
                            const std::int32_t ny = 2 * y + oysize - ty;
                            const std::int32_t oxc =
                                2 * ob->xpos() + oxsize - tx;
                            const std::int32_t oyc =
                                2 * ob->ypos() + oysize - ty;
                            const std::int64_t d_new =
                                static_cast<std::int64_t>(nx) * nx +
                                static_cast<std::int64_t>(ny) * ny;
                            const std::int64_t d_old =
                                static_cast<std::int64_t>(oxc) * oxc +
                                static_cast<std::int64_t>(oyc) * oyc;
                            if (d_new > d_old)
                                continue; // separating: not blocked by w
                        }
                        ob->collide(w);
                        if (ob->stats()->query_bit_flags(BIT_NO_COLLIDE))
                            return 1;
                        return 0;
                    }
                }
                else
                    // if (ob->collide_ob()) //let's just assume its safe
                    ob->set_collide_ob(nullptr);
            }
        }
	}
	
	return 1;
}

short collide(short x,  short y,  short xsize,  short ysize,
              short x2, short y2, short xsize2, short ysize2)
{
	short xright, x2right;
	short ydown,  y2down;

	//return 0; // debug

	// Shrink values:
	x += 1;
	x2 += 1;
	y += 1;
	y2 += 1;
	xsize -= 2;
	xsize2 -= 2;
	ysize -= 2;
	ysize2 -= 2;

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
