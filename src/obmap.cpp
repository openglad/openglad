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

short ob_pass_check(short x, short y, walker  *ob, const std::list<walker*>& pile);
short collide(short x,  short y,  short xsize,  short ysize,
              short x2, short y2, short xsize2, short ysize2);

//#define XRES GRID_SIZE
//#define YRES GRID_SIZE
#define OBRES 32 //GRID_SIZE

// Zardus: ADD: real quick, lets put in a function for deleting oblists
void delete_list(oblink *list)
{
	oblink *here;
	oblink *prev;

	if (!list) return;

	here = list;
	while (here->next)
	{
		prev = here;
		here = here->next;
		prev->ob = NULL;
		prev->next = NULL;
		delete prev;
	}
	delete here;
}

// These are passed in as PIXEL coordinates now...
obmap::obmap()
{
	obmapres = OBRES;
	
	pos_to_walker.clear();
	walker_to_pos.clear();
}

obmap::~obmap()
{
    // Delete the walkers we're holding
    for(auto e = walker_to_pos.begin(); e != walker_to_pos.end(); e++)
    {
        delete e->first;
    }
}

void obmap::draw()
{
    text t(myscreen);
    short offsetx = myscreen->viewob[0]->topx;
    short offsety = myscreen->viewob[0]->topy;
    // Draw the number of obs in each pile
    for(auto e = pos_to_walker.begin(); e != pos_to_walker.end(); e++)
    {
        short cx = unhash(e->first.first) - offsetx + OBRES/2;
        short cy = unhash(e->first.second) - offsety + OBRES/2;
        myscreen->draw_box(cx - OBRES/2, cy - OBRES/2, cx + OBRES/2, cy + OBRES/2, YELLOW, false);
        t.write_xy_center(cx, cy, YELLOW, "%d", e->second.size());
    }
    
    // Draw a box for each walker
    for(auto e = walker_to_pos.begin(); e != walker_to_pos.end(); e++)
    {
        // Get bounds
        bool unset = true;
        SDL_Rect r = {0, 0, 1, 1};
        for(auto f = e->second.begin(); f != e->second.end(); f++)
        {
            if(unset)
            {
                r.x = f->first;
                r.y = f->second;
                unset = false;
                continue;
            }
            if(f->first < r.x)
            {
                r.w += r.x - f->first;
                r.x = f->first;
            }
            if(f->second < r.y)
            {
                r.h += r.y - f->second;
                r.y = f->second;
            }
            if(f->first > r.x + r.w)
            {
                r.w += f->first - (r.x + r.w);
            }
            if(f->second > r.y + r.h)
            {
                r.h += f->second - (r.y + r.h);
            }
        }
        
        if(!unset)
        {
            // Draw the rect
            short x = unhash(r.x) - offsetx;
            short y = unhash(r.y) - offsety;
            short w = unhash(r.w);
            short h = unhash(r.h);
            myscreen->draw_box(x, y, x + w, y + h, e->first->query_team_color(), false);
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
	endnumx   = hash( (short) (x+ob->sizex) );
	startnumy = hash(y);
	endnumy   = hash( (short) (y+ob->sizey) );

	// For each y grid row we are in...
	for (numx = startnumx; numx <= endnumx; numx++)
	{
		for (numy = startnumy; numy <= endnumy; numy++)
		{
			// We should be finding the same item over and over
			if (!ob_pass_check(x, y, ob, pos_to_walker[std::make_pair(numx, numy)] )) //&& ob->collide_ob??
				return 0;
		}
	}
	return 1;
}


short obmap::remove(walker  *ob)  // This goes in walker's destructor
{
    // Find all of the instances of the object and remove them from the map
    
    // Get the list of positions that the walker occupies
    auto e = walker_to_pos.find(ob);
    if(e != walker_to_pos.end())
    {
        // For each position...
        for(auto f = e->second.begin(); f != e->second.end(); f++)
        {
            // Get the pile
            auto g = pos_to_walker.find(*f);
            
            // Find our guy in this pile and remove him
            auto h = std::find(g->second.begin(), g->second.end(), ob);
            if(h != g->second.end())
                g->second.erase(h);
        }
        
        // Erase the walker from the walker map too
        walker_to_pos.erase(e);
        return true;
    }
    
    return false;
}

short obmap::add(walker  *ob, short x, short y)  // This goes in walker's constructor
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;

	if (x < 0 || y < 0)
		return 0;

	startnumx = hash(x);
	endnumx   = hash( (short) (x + ob->sizex) );
	startnumy = hash(y);
	endnumy   = hash( (short) (y + ob->sizey) );

    // Figure out all of the positions that are occupied
	std::list<std::pair<short,short> > pos;
	for (numx = startnumx; numx <= endnumx; numx++)
	{
		for (numy = startnumy; numy <= endnumy; numy++)
		{
		    // Store this position
		    pos.push_back(std::make_pair(numx, numy));
		    
		    // Put the walker here too
		    pos_to_walker[std::make_pair(numx, numy)].push_back(ob);
		}
	}
	
	// Now record where he is
	walker_to_pos.insert(std::make_pair(ob, pos));
		
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


//       oblink  *list[OB_MAP_SIZE];
short obmap::hash(short y)
{
	//  For now, this assumes no one is smaller than size 8
	//  Also note that the hash table never loops.
	// return (y/8);
	short num = (short) (y/OBRES) ;
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

short ob_pass_check(short x, short y, walker  *ob, const std::list<walker*>& pile)
{
	short oxsize, oysize;
	short x2,y2,xsize2,ysize2;
	short targetorder;//, targetteam, targetfamily;
	short myorder;
	//short myteam;
	static char message[80];

	oxsize = ob->sizex;
	oysize = ob->sizey;

	myorder = ob->query_order();
	//myteam  = ob->team_num;
	
	if(!ob)
        return 1;

	// Check each object to see if sizes collide.
	for(auto e = pile.begin(); e != pile.end(); e++)
	{
	    walker* w = *e;
	    if (w != ob && !w->dead)
        {
            targetorder = w->query_order();
            
            // Let our own team's weapons pass over us
            if((targetorder == ORDER_WEAPON || myorder == ORDER_WEAPON) && ob->is_friendly(w))
                continue;
            // Allow weapons to sometimes 'miss' opposing team's weapons
            else if(targetorder == ORDER_WEAPON && myorder == ORDER_WEAPON && (random(10) > 3))
                continue;
            // Weapons never hit treasure
            else if(targetorder == ORDER_TREASURE && myorder == ORDER_WEAPON)
                continue;
            else
            {
                x2 = w->xpos;
                y2 = w->ypos;
                xsize2 = w->sizex;
                ysize2 = w->sizey;
                if(collide(x,y,oxsize,oysize,x2,y2,xsize2,ysize2))
                {
                    if ( targetorder == ORDER_TREASURE )
                    {
                        w->eat_me(ob);
                    }
                    else if ( (targetorder == ORDER_WEAPON)
                              && (w->query_family() == FAMILY_DOOR) )
                    {
                        // Can we unlock this door?
                        if (ob->keys & (Sint32) (pow((double) 2, w->stats->level)))
                        {
                            // Open the door ..
                            w->dead = 1;
                            w->death();
                            ob->collide(w);
                            if (ob->stats->query_bit_flags(BIT_NO_COLLIDE))
                                return 1;
                            return 0; // block for this round
                        } // end of unlocked door
                        else
                        {
                            // Do we notify?
                            if (!(ob->skip_exit) && (ob->user != -1))
                            {
                                sprintf(message, "Key %d needed!",
                                        w->stats->level);
                                myscreen->do_notify(message, ob);
                                ob->skip_exit = 10;
                            } // end of failed open door notification
                            ob->collide(w);
                            return 0; // failed to open door
                        }
                    } // end of door case
                    else
                    {
                        ob->collide(w);
                        if (ob->stats->query_bit_flags(BIT_NO_COLLIDE))
                            return 1;
                        return 0;
                    }
                }
                else
                    // if (ob->collide_ob) //let's just assume its safe
                    ob->collide_ob = NULL;
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

	x2right = (short) (x2+xsize2);
	if (x > x2right)
		return 0;

	xright = (short) (x+xsize);
	if (xright < x2)
		return 0;

	y2down = (short) (y2+ysize2);
	if (y > y2down)
		return 0;

	ydown = (short) (y+ysize);
	if (ydown < y2)
		return 0;

	return 1;
}

