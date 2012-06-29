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
#include <math.h>

short ob_pass_check(short x, short y, walker  *ob, oblink  *row);
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
obmap::obmap(short maxx, short maxy)
{
	short i, j;
	short x,y;
	// Be careful of this memory allocation
	//  printf("New obmap.\n");
	//  list = (oblink **) new double[maxy];
	//  printf("%d\n", maxy);
	if (maxx)
		maxx = 1; // to get rid of warnings; remove maxx, maxy
	if (maxy)
		maxy = 1; // when we're sure they're not used
	obmapres = OBRES;
	totalobs = 0;
	x = 200; //maxx/obmapres;
	y = 200; //maxy/obmapres;
	for(i=0; i< x; i++)
		for(j=0; j < y; j++)
			list[i][j] = NULL;

}

obmap::~obmap()
{
	short i, j;
	oblink *here, *templink;

	for (i=0; i < 200; i++)
		for (j=0; j < 200; j++)
		{
			if (list[i][j])
			{
				here = list[i][j];
				while (here->next)
				{
					templink = here;
					if (here->ob)
					{
						delete here->ob;
						here->ob = NULL;
					}
					here = here->next;
					delete templink;
					templink = NULL;
				}
				if (here->ob)
				{
					delete here->ob;
					here->ob = NULL;
				}
				delete here;
				here = NULL;
			}
			list[i][j] = NULL;
		}

}

short obmap::query_list(walker  *ob, short x, short y)
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;
	if (!ob || ob->dead)
	{
		printf("Bad ob to query_list.\n");
		return 1;
	}
	startnumx = hash(x);
	endnumx   = hash( (short) (x+ob->sizex) );
	startnumy = hash(y);
	endnumy   = hash( (short) (y+ob->sizey) );

	// For each y grid row we are in...
	for (numx = startnumx; numx <= endnumx; numx++)
		for (numy = startnumy; numy <= endnumy; numy++)
		{
			// We should be finding the same item over and over
			if (!ob_pass_check(x, y, ob, list[numx][numy] )) //&& ob->collide_ob??
				return 0;
		}
	return 1;
}

oblink  *remove_link(walker  *ob, oblink **row)
{
	oblink  *here;
	oblink  *old;

	here = *row;
	// Is this an empty list?
	// Some of this is redundant, but its worth avoiding segfaults, IMO :-)
	if (!*row)
		return NULL;
	if (!ob)
		return NULL;
	if (!here)
		return NULL;
	if (!((*row)->ob))
		return NULL;

	// Is our object the first on the list?
	if ((*row)->ob == ob)
	{
		old = *row;
		*row = old->next;
		//old->next = NULL; // doesn't appear to be necessary?
		//         printf("REM_LINK: we were first object\n");
		return old;
	}

	// Search the rest of the list
	while(here->next)
	{
		if (here->next->ob == ob) // BINGO!
		{
			old = here->next;
			here->next = old->next;
			// old->next = NULL; // doesn't appear to be necessary?
			//                printf("REM_LINK: found ob\n");
			return old;
		}
		here = here->next;
	}
	//  printf("REM_LINK: Failed to find ob! push z\n");
	//  wait_for_key(SDLK_z);
	return NULL;
}

short add_link(oblink  *newguy, oblink **row)
{
	oblink  *here;

	here = *row;
	//  printf("  Adding link.\n");
	newguy->next = 0;
	// Is this an empty list?
	if (!here)
	{
		*row = newguy;
		return 1;
	}

	// Search the rest of the list
	while(here->next)
	{
		here = here->next;
	}

	here->next = newguy;
	return 1;
}


short obmap::remove
	(walker  *ob)  // This goes in walker's destructor
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;
	oblink  *old = NULL;
	short found = 0;

	//  printf("Deleting walker.\n");
	//return 1;

	startnumx = hash(ob->xpos);
	endnumx   = hash((short) (ob->xpos + ob->sizex) );
	startnumy = hash(ob->ypos);
	endnumy   = hash((short) (ob->ypos +ob->sizey) );

	// For each y grid row we are in...
	//  printf("removing from %d to %d\n", startnum, endnum);
	//  wait_for_key(SDLK_z);
	for (numx = startnumx; numx <= endnumx; numx++)
		for (numy = startnumy; numy <= endnumy; numy++)
		{
			// We should be finding the same item over and over
			old = remove_link(ob, &list[numx][numy]);
			if (old)
			{
				found = 1;
				delete old;
				old = NULL;
			}
		}
	if (found)
		totalobs --;
	return 1;
}

short obmap::add
	(walker  *ob, short x, short y)  // This goes in walker's constructor
{
	short numx, startnumx, endnumx;
	short numy, startnumy, endnumy;
	oblink  *newguy = NULL;

	if (x < 0 || y < 0)
		return 0;

	startnumx = hash(x);
	endnumx   = hash( (short) (x + ob->sizex) );
	startnumy = hash(y);
	endnumy   = hash( (short) (y + ob->sizey) );

	//  printf("ADDing to %d to %d\n", startnum, endnum);
	//  wait_for_key(SDLK_z);
	// For each y grid row we are in...
	for (numx = startnumx; numx <= endnumx; numx++)
		for (numy = startnumy; numy <= endnumy; numy++)
		{
			newguy = new oblink;
			newguy->ob = ob;
			newguy->next = NULL;

			// We should be finding the same item over and over
			add_link(newguy, &list[numx][numy]);
		}
	totalobs++;
	return 1;
}


short obmap::move(walker  *ob, short x, short y)  // This goes in walker's setxy
{

	if ( y == ob->ypos ) //&&y checked for the special case of 0
		if ( x == ob->xpos )
			return 1;      // do nothing for horizontal

	remove
		(ob);
	add
		(ob, x, y);
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

oblink * obmap::obmap_get_list(short x, short y)
{
	//short hashedx,hashedy;
	//hashedx = hash(x);
	//hashedy = hash(y);
	return list[hash(x)][hash(y)];
}

/***********************************************
**  All pass checking from here down.
***********************************************/

short ob_pass_check(short x, short y, walker  *ob, oblink  *row)
{
	short oxsize, oysize;
	short x2,y2,xsize2,ysize2;
	short targetorder;//, targetteam, targetfamily;
	short myorder;
	//short myteam;
	static char message[80];

	oblink  *here;
	oxsize = ob->sizex;
	oysize = ob->sizey;

	myorder = ob->query_order();
	//myteam  = ob->team_num;

	// Check each object to see if sizes collide.
	here = row;
	while(here)
	{
		if (here)
			if (here->ob)
				if (ob)
					if (here->ob != ob && !here->ob->dead)
					{
						// Let our own team's weapons pass over us ..
						targetorder = here->ob->query_order();
						//targetteam  = here->ob->team_num;
						//targetfamily = here->ob->query_family();
						if ( ( (targetorder == ORDER_WEAPON)||(myorder == ORDER_WEAPON) ) &&
						        (ob->is_friendly(here->ob))
						   )
						{
							// do nothing
						}
						else if ( (targetorder == ORDER_WEAPON) && (myorder == ORDER_WEAPON) &&
						          (random(10)>3) )
						{
							// Allow weapons to sometimes 'miss' opposing team's weapons
						}
						else if ( (targetorder == ORDER_TREASURE) && (myorder == ORDER_WEAPON) )
						{
							// Weapons never hit treasure, so do nothing
						}
						else
						{
							x2 = here->ob->xpos;
							y2 = here->ob->ypos;
							xsize2 = here->ob->sizex;
							ysize2 = here->ob->sizey;
							if (collide(x,y,oxsize,oysize,x2,y2,xsize2,ysize2))
							{
								//                       printf("Ob at %d, %d   \n", x, y);
								if ( targetorder == ORDER_TREASURE )
								{
									here->ob->eat_me(ob);
								}
								else if ( (targetorder == ORDER_WEAPON)
								          && (here->ob->query_family() == FAMILY_DOOR) )
								{
									// Can we unlock this door?
									if (ob->keys & (Sint32) (pow((double) 2, here->ob->stats->level)))
									{
										// Open the door ..
										here->ob->dead = 1;
										here->ob->death();
										ob->collide(here->ob);
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
											        here->ob->stats->level);
											myscreen->do_notify(message, ob);
											ob->skip_exit = 10;
										} // end of failed open door notification
										ob->collide(here->ob);
										return 0; // failed to open door
									}
								} // end of door case
								else
								{
									ob->collide(here->ob);
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

		here = here->next;
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

