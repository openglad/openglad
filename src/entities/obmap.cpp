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
#include "graph.h"
#include <cmath>
#include <algorithm>
#include <format>
#include "runtime/game_context.h"

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

bool debug_draw_obmap = false;

short ob_pass_check(short x, short y, walker* ob, const std::list<walker*>& pile, const obmap* map);
short collide(short x,  short y,  short xsize,  short ysize,
              short x2, short y2, short xsize2, short ysize2);

//#define XRES GRID_SIZE
//#define YRES GRID_SIZE
#define OBRES 32 //GRID_SIZE

// These are passed in as PIXEL coordinates now...
obmap::obmap()
{
	obmapres = OBRES;
	
	pos_to_walker.clear();
	walker_to_pos.clear();
}

obmap::~obmap()
{}

void obmap::draw()
{
    text& t = myscreen->text_normal;
    const Sint32 offsetx = myscreen->viewob[0]->topx;
    const Sint32 offsety = myscreen->viewob[0]->topy;
    // Draw the number of obs in each pile
    for(auto& [pos, walkers] : pos_to_walker)
    {
        const Sint32 cx = static_cast<Sint32>(unhash(pos.first)) - offsetx + OBRES/2;
        const Sint32 cy = static_cast<Sint32>(unhash(pos.second)) - offsety + OBRES/2;
        myscreen->draw_box(cx - OBRES/2, cy - OBRES/2, cx + OBRES/2, cy + OBRES/2, YELLOW, false);
        t.write_xy_center(cx, cy, YELLOW, "%d", walkers.size());
    }
    
    // Draw a box for each walker
    for(auto& [w, positions] : walker_to_pos)
    {
        // Get bounds
        bool unset = true;
        SDL_Rect r = {0, 0, 1, 1};
        for(auto& [px, py] : positions)
        {
            if(unset)
            {
                r.x = px;
                r.y = py;
                unset = false;
                continue;
            }
            if(px < r.x)
            {
                r.w += r.x - px;
                r.x = px;
            }
            if(py < r.y)
            {
                r.h += r.y - py;
                r.y = py;
            }
            if(px > r.x + r.w)
            {
                r.w += px - (r.x + r.w);
            }
            if(py > r.y + r.h)
            {
                r.h += py - (r.y + r.h);
            }
        }

        if(!unset)
        {
            // Draw the rect
            const Sint32 x = static_cast<Sint32>(unhash(static_cast<short>(r.x))) - offsetx;
            const Sint32 y = static_cast<Sint32>(unhash(static_cast<short>(r.y))) - offsety;
            const Sint32 bw = static_cast<Sint32>(unhash(static_cast<short>(r.w)));
            const Sint32 bh = static_cast<Sint32>(unhash(static_cast<short>(r.h)));
            myscreen->draw_box(x, y, x + bw, y + bh, w->query_team_color(), false);
        }
    }
}

size_t obmap::size() const
{
    return walker_to_pos.size();
}

short obmap::query_list(walker  *ob, short x, short y)
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;
	if (!ob || ob->dead)
	{
		Log("Bad ob to query_list.\n");
		return 1;
	}
	startnumx = hash(x);
	endnumx   = hash( static_cast<short>(x+ob->sizex) );
	startnumy = hash(y);
	endnumy   = hash( static_cast<short>(y+ob->sizey) );

	// For each y grid row we are in...
	for (numx = startnumx; numx <= endnumx; numx++)
	{
		for (numy = startnumy; numy <= endnumy; numy++)
		{
			// We should be finding the same item over and over. Avoid
			// default-constructing empty piles during queries.
			auto it = pos_to_walker.find(std::make_pair(numx, numy));
			if (it == pos_to_walker.end())
				continue;
			if (!ob_pass_check(x, y, ob, it->second, this)) //&& ob->collide_ob??
				return 0;
		}
	}
	return 1;
}


short obmap::remove(walker  *ob)  // This goes in walker's destructor
{
    if (!ob)
        return 0;

    // Fast path only: removal is driven by walker_to_pos bookkeeping.
    // Do not scan the entire pos_to_walker map in production; that can turn
    // legitimate gameplay/test loops into O(N^2) behavior.
    auto e = walker_to_pos.find(ob);
    if (e == walker_to_pos.end())
    {
        // Bookkeeping mismatch: attempt a bounded removal based on the
        // walker's current bounding box. This avoids global scans while still
        // preventing stale pointers in pos_to_walker (ASan/UAF).
        if (ob->xpos < 0 || ob->ypos < 0)
            return 0;

        short numx, startnumx, endnumx;
        short numy, startnumy, endnumy;
        startnumx = hash(ob->xpos);
        endnumx = hash(static_cast<short>(ob->xpos + ob->sizex));
        startnumy = hash(ob->ypos);
        endnumy = hash(static_cast<short>(ob->ypos + ob->sizey));

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

	startnumx = hash(x);
	endnumx   = hash( static_cast<short>(x + ob->sizex) );
	startnumy = hash(y);
	endnumy   = hash( static_cast<short>(y + ob->sizey) );

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
	if(x == ob->xpos && y == ob->ypos)
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

std::list<walker*>& obmap::obmap_get_list(short x, short y)
{
	return pos_to_walker[std::make_pair(hash(x), hash(y))];
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

	oxsize = ob->sizex;
	oysize = ob->sizey;

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

            if (w->dead)
                continue;

            targetorder = w->query_order();
            
            // Let our own team's weapons pass over us
            if((targetorder == Order::Weapon || myorder == Order::Weapon) && ob->is_friendly(w))
                continue;
            // Allow weapons to sometimes 'miss' opposing team's weapons
            else if(targetorder == Order::Weapon && myorder == Order::Weapon && (rng(10) > 3))
                continue;
            // Weapons never hit treasure
            else if(targetorder == Order::Treasure && myorder == Order::Weapon)
                continue;
            else
            {
                x2 = w->xpos;
                y2 = w->ypos;
                xsize2 = w->sizex;
                ysize2 = w->sizey;
                if(collide(x,y,oxsize,oysize,x2,y2,xsize2,ysize2))
                {
                    if ( targetorder == Order::Treasure )
                    {
                        w->eat_me(ob);
                    }
                    else if ( (targetorder == Order::Weapon)
                              && (w->query_family() == FAMILY_DOOR) )
                    {
                        // Can we unlock this door?
                        if (ob->keys & static_cast<Sint32>(pow(static_cast<double>(2), w->stats()->level)))
                        {
                            // Open the door ..
                            w->dead = 1;
                            w->death();
                            ob->collide(w);
                            if (ob->stats()->query_bit_flags(BIT_NO_COLLIDE))
                                return 1;
                            return 0; // block for this round
                        } // end of unlocked door
                        else
                        {
                            // Do we notify?
                            if (!(ob->skip_exit) && (ob->user != -1))
                            {
                                std::string message = std::format("Key {} needed!",
                                        w->stats()->level);
                                myscreen->do_notify(message.c_str(), ob);
                                ob->skip_exit = 10;
                            } // end of failed open door notification
                            ob->collide(w);
                            return 0; // failed to open door
                        }
                    } // end of door case
                    else
                    {
                        ob->collide(w);
                        if (ob->stats()->query_bit_flags(BIT_NO_COLLIDE))
                            return 1;
                        return 0;
                    }
                }
                else
                    // if (ob->collide_ob) //let's just assume its safe
                    ob->collide_ob = nullptr;
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
