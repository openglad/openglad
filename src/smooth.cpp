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
// Map smoother, for use in the scenario editor ..

#include "smooth.h"

//smoother * mysmoother; //ignored?????

smoother::smoother()
{
	mygrid = NULL;
}

smoother::~smoother()
{}

void smoother::set_target(screen  *target)
{
	mygrid = target->grid;
	maxx = target->maxx;
	maxy = target->maxy;
	//  buffer = target + 3; // where the data starts
}

Sint32 smoother::query_x_y(Sint32 x, Sint32 y)
{
	// Are we set up yet?
	if (!mygrid)
		return PIX_GRASS1;

	// Check boundaries ..
	if ( (x < 0) || (y < 0) )
		return PIX_GRASS1;

	if ( (x >= maxx) || (y >= maxy) )
		return PIX_GRASS1;

	// Else, return our simple grid data ..
	return (Sint32) mygrid[x + y*maxx];
}

Sint32 smoother::query_genre_x_y(Sint32 x, Sint32 y)
{
	Sint32 basetype;

	basetype = query_x_y(x, y); // get our base type, like PIX_GRASS1

	switch (basetype)
	{
		case PIX_GRASS1:  // all grass
		case PIX_GRASS2:
		case PIX_GRASS3:
		case PIX_GRASS4:
		case PIX_GRASSWATER_LL: // mostly grass
		case PIX_GRASSWATER_LR:
		case PIX_GRASSWATER_UL:
		case PIX_GRASSWATER_UR:
			return TYPE_GRASS;

		case PIX_GRASS_DARK_1: // dark grass
		case PIX_GRASS_DARK_2:
		case PIX_GRASS_DARK_3:
		case PIX_GRASS_DARK_4:
		case PIX_GRASS_DARK_LL: // edges
		case PIX_GRASS_DARK_UR:
		case PIX_GRASS_DARK_B1: // fuzzy bottom edge
		case PIX_GRASS_DARK_B2:
		case PIX_GRASS_DARK_BR:
		case PIX_GRASS_DARK_R1:
		case PIX_GRASS_DARK_R2:
		case PIX_GRASS_RUBBLE: // dark grass with rubble
			return TYPE_GRASS_DARK;

		case PIX_GRASS_LIGHT_1: // light grass
		case PIX_GRASS_LIGHT_TOP:
		case PIX_GRASS_LIGHT_RIGHT_TOP:
		case PIX_GRASS_LIGHT_RIGHT:
		case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
		case PIX_GRASS_LIGHT_BOTTOM:
		case PIX_GRASS_LIGHT_LEFT_BOTTOM:
		case PIX_GRASS_LIGHT_LEFT:
		case PIX_GRASS_LIGHT_LEFT_TOP:
			return TYPE_GRASS_LIGHT;

		case PIX_CARPET_LL:  // all the carpets ..
		case PIX_CARPET_L:
		case PIX_CARPET_B:
		case PIX_CARPET_LR:
		case PIX_CARPET_UR:
		case PIX_CARPET_U:
		case PIX_CARPET_UL:
		case PIX_CARPET_M:
		case PIX_CARPET_M2:
		case PIX_CARPET_R:
		case PIX_CARPET_SMALL_HOR:
		case PIX_CARPET_SMALL_VER:
		case PIX_CARPET_SMALL_CUP:
		case PIX_CARPET_SMALL_CAP:
		case PIX_CARPET_SMALL_LEFT:
		case PIX_CARPET_SMALL_RIGHT:
		case PIX_CARPET_SMALL_TINY:
			return TYPE_CARPET;

		case PIX_H_WALL1:  // these are various walls ..
		case PIX_WALL_LL:
		case PIX_WALL2:
		case PIX_WALL3:
		case PIX_WALL4:
		case PIX_WALL5:
		case PIX_WALLSIDE1:
		case PIX_WALLSIDE_L:
		case PIX_WALLSIDE_R:
		case PIX_WALLSIDE_C:
		case PIX_WALLSIDE_CRACK_C1:
		case PIX_WALL_ARROW_GRASS:
		case PIX_WALL_ARROW_FLOOR:
		case PIX_WALL_ARROW_GRASS_DARK:
			return TYPE_WALL;

		case PIX_WATER1: // all water
		case PIX_WATER2:
		case PIX_WATER3:
		case PIX_WATERGRASS_LL:  // mostly water
		case PIX_WATERGRASS_LR:
		case PIX_WATERGRASS_UL:
		case PIX_WATERGRASS_UR:
			return TYPE_WATER;

		case PIX_TREE_T1:  // Trees ..
		case PIX_TREE_M1:
		case PIX_TREE_ML:
		case PIX_TREE_MR:
		case PIX_TREE_MT:
		case PIX_TREE_B1:
			return TYPE_TREES;

		case PIX_DIRT_1: // Dirt paths ..
		case PIX_DIRTGRASS_UL1:
		case PIX_DIRTGRASS_UR1:
		case PIX_DIRTGRASS_LL1:
		case PIX_DIRTGRASS_LR1:
			return TYPE_DIRT;

		case PIX_DIRT_DARK_1: // Dark dirk
		case PIX_DIRTGRASS_DARK_UL1:
		case PIX_DIRTGRASS_DARK_UR1:
		case PIX_DIRTGRASS_DARK_LL1:
		case PIX_DIRTGRASS_DARK_LR1:
			return TYPE_DIRT_DARK;

		case PIX_COBBLE_1:  // cobblestone
		case PIX_COBBLE_2:
		case PIX_COBBLE_3:
		case PIX_COBBLE_4:
			return TYPE_COBBLE;

		default :
			return TYPE_UNKNOWN;
	}
}

Sint32 smoother::surrounds(Sint32 x, Sint32 y, Sint32 whatgenre)
{
	Sint32 howmany = 0;

	if (query_genre_x_y(x, y-1) == whatgenre) // above
		howmany += 1;

	if (query_genre_x_y(x+1, y) == whatgenre) // right
		howmany += 2;

	if (query_genre_x_y(x, y+1) == whatgenre) // below
		howmany += 4;

	if (query_genre_x_y(x-1, y) == whatgenre) // left
		howmany += 8;

	return howmany;
}

Sint32 smoother::smooth(Sint32 x, Sint32 y)
{
	Sint32 here = query_genre_x_y(x, y);
	Sint32 herepix = query_x_y(x, y), uppix;

	Sint32 up    = query_genre_x_y(x, y-1);
	Sint32 down  = query_genre_x_y(x, y+1);
	Sint32 left  = query_genre_x_y(x-1, y);
	Sint32 right = query_genre_x_y(x+1, y);

	Sint32 upleft = query_genre_x_y(x-1, y-1);
	Sint32 upright = query_genre_x_y(x+1, y-1);
	Sint32 downleft = query_genre_x_y(x-1, y+1);
	Sint32 downright = query_genre_x_y(x+1, y+1);

	Sint32 around = surrounds(x, y, here);
	Sint32 newvalue = PIX_GRASS1;


	switch (here) // switch on genre
	{
		case TYPE_GRASS:
			if (upleft == TYPE_WATER && downright == TYPE_WATER &&
			        downleft == TYPE_WATER && down == TYPE_WATER && left == TYPE_WATER)
				newvalue = PIX_GRASSWATER_LL; // LL is where the water is
			else if (upleft == TYPE_WATER && upright == TYPE_WATER &&
			         downright == TYPE_WATER && up == TYPE_WATER && right == TYPE_WATER)
				newvalue = PIX_GRASSWATER_UR;
			else if (upleft == TYPE_WATER && upright == TYPE_WATER &&
			         downleft == TYPE_WATER && up == TYPE_WATER && left == TYPE_WATER)
				newvalue = PIX_GRASSWATER_UL;
			else if (upright == TYPE_WATER && downright == TYPE_WATER &&
			         downleft == TYPE_WATER && right == TYPE_WATER && down == TYPE_WATER)
				newvalue = PIX_GRASSWATER_LR;
			else
				switch (random(4))
				{
					case 0:
						newvalue = PIX_GRASS1;
						break;
					case 1:
						newvalue = PIX_GRASS2;
						break;
					case 2:
						newvalue = PIX_GRASS3;
						break;
					case 3:
						newvalue = PIX_GRASS4;
						break;
				}
			break;
		case TYPE_GRASS_DARK:  // Shadowed grass
			if (around == TO_AROUND ) // all around
			{
				switch (random(4))
				{
					case 0:
						newvalue = PIX_GRASS_DARK_1;
						break;
					case 1:
						newvalue = PIX_GRASS_DARK_2;
						break;
					case 2:
						newvalue = PIX_GRASS_DARK_3;
						break;
					case 3:
						newvalue = PIX_GRASS_DARK_4;
						break;
				}
			}
			else if ( (left == TYPE_TREES || left == TYPE_WALL)
			          && (down == TYPE_TREES || down == TYPE_WALL)
			          && (upright != TYPE_TREES && upright != TYPE_WALL) ) // act as right edge
			{
				switch (random(2))
				{
					case 0:
						newvalue = PIX_GRASS_DARK_R1;
						break;
					case 1:
						newvalue = PIX_GRASS_DARK_R2;
						break;
				}
				break;
			}
			else if (   (upleft == TYPE_TREES || upleft == TYPE_WALL)
			            && (up == TYPE_TREES || up == TYPE_WALL) ) // act as bottom middle
			{
				switch (down) // are we a bottom middle, or center? depends..
				{
					case TYPE_GRASS:
					case TYPE_WATER:
					case TYPE_TREES:
					case TYPE_DIRT:
					case TYPE_COBBLE:
						switch (random(2))
						{
							case 0:
								newvalue = PIX_GRASS_DARK_B1;
								break;
							case 1:
								newvalue = PIX_GRASS_DARK_B2;
								break;
						}
						if (!random(20)) // then place a bit o' rubble
							newvalue = PIX_GRASS_RUBBLE;
						break;
					default:
						switch (random(4))
						{
							case 0:
								newvalue = PIX_GRASS_DARK_1;
								break;
							case 1:
								newvalue = PIX_GRASS_DARK_2;
								break;
							case 2:
								newvalue = PIX_GRASS_DARK_3;
								break;
							case 3:
								newvalue = PIX_GRASS_DARK_4;
								break;
						}
						break;
				} // end case-check of what's below us
			}  // end of first bottom-middle case
			else if ( (up == TYPE_TREES || up == TYPE_WALL) &&
			          (right==TYPE_TREES|| right==TYPE_WALL) )
				newvalue = PIX_GRASS_DARK_UR;
			else if (around == (TO_LEFT | TO_RIGHT | TO_DOWN)) // == top middle
			{} // do nothing
			else if (around == (TO_UP | TO_DOWN | TO_LEFT)) // right middle
			{
				switch (random(2))
				{
					case 0:
						newvalue = PIX_GRASS_DARK_R1;
						break;
					case 1:
						newvalue = PIX_GRASS_DARK_R2;
						break;
				}
			}
			else if (around == (TO_LEFT | TO_DOWN)) // top right
			{
				if (right == TYPE_GRASS)
					newvalue = PIX_GRASS_DARK_LL;
				else
					newvalue = PIX_GRASS_DARK_B2;
			}
			else if (around == (TO_LEFT | TO_RIGHT | TO_UP)) // bottom middle
			{
				switch (random(2))
				{
					case 0:
						newvalue = PIX_GRASS_DARK_B1;
						break;
					case 1:
						newvalue = PIX_GRASS_DARK_B2;
						break;
				}
				if (!random(20)) // then place a bit o' rubble
					newvalue = PIX_GRASS_RUBBLE;
			}
			else if (around == (TO_LEFT | TO_RIGHT)) // middle, thin
			{
				switch (random(2))
				{
					case 0:
						newvalue = PIX_GRASS_DARK_B1;
						break;
					case 1:
						newvalue = PIX_GRASS_DARK_B2;
						break;
				}
				if (!random(20)) // then place a bit o' rubble
					newvalue = PIX_GRASS_RUBBLE;
			}
			else if (around == (TO_LEFT | TO_UP)) // bottom right
				newvalue = PIX_GRASS_DARK_BR;
			else if (around == TO_LEFT) // right, thin
			{
				if (right == TYPE_GRASS)
					newvalue = PIX_GRASS_DARK_LL;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else if ( (around == (TO_DOWN | TO_RIGHT | TO_UP) ) || // left middle
			          (around == (TO_DOWN | TO_RIGHT) ) ) // top left
			{
				switch (random(4))
				{
					case 0:
						newvalue = PIX_GRASS_DARK_1;
						break;
					case 1:
						newvalue = PIX_GRASS_DARK_2;
						break;
					case 2:
						newvalue = PIX_GRASS_DARK_3;
						break;
					case 3:
						newvalue = PIX_GRASS_DARK_4;
						break;
				}
			}
			else if (around == (TO_DOWN | TO_UP)) // center vertical
			{
				switch (random(2))
				{
					case 0:
						newvalue = PIX_GRASS_DARK_R1;
						break;
					case 1:
						newvalue = PIX_GRASS_DARK_R2;
						break;
				}
			}
			else if (around == TO_DOWN) // top, alone
			{
				if ( (right == TYPE_GRASS) || (up == TYPE_GRASS) )
					newvalue = PIX_GRASS_DARK_LL;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else if (around == (TO_RIGHT | TO_UP)) // bottom left
			{
				if ( (left==TYPE_GRASS) || (down==TYPE_GRASS) )
					newvalue = PIX_GRASS_DARK_UR;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else if (around == TO_RIGHT) // left, alone
			{
				if ( (left==TYPE_GRASS) )
					newvalue = PIX_GRASS_DARK_UR;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else if (around == TO_UP) // bottom, alone
			{
				if ( (down==TYPE_GRASS) )
					newvalue = PIX_GRASS_DARK_UR;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else
				newvalue = PIX_GRASS_DARK_1; // default case
			break;
		case TYPE_CARPET:
			switch (around)
			{
				case 0: // all alone
					newvalue = PIX_CARPET_SMALL_TINY;
					break;
				case 1: // we're a bottom "cup"
					newvalue = PIX_CARPET_SMALL_CUP;
					break;
				case 2: // we're a left edge
					newvalue = PIX_CARPET_SMALL_LEFT;
					break;
				case 3: // we're the bottom left ..
					newvalue = PIX_CARPET_LL;
					break;
				case 4: // we're a top "cap"
					newvalue = PIX_CARPET_SMALL_CAP;
					break;
				case 5: // we're a vertical pipe
					newvalue = PIX_CARPET_SMALL_VER;
					break;
				case 6: // we're a top-left corner
					newvalue = PIX_CARPET_UL;
					break;
				case 7: // we're a left edge
					newvalue = PIX_CARPET_L;
					break;
				case 8: // we're a right edge end
					newvalue = PIX_CARPET_SMALL_RIGHT;
					break;
				case 9: // we're a lower right corner
					newvalue = PIX_CARPET_LR;
					break;
				case 10: // we're a horizontal pipe
					newvalue = PIX_CARPET_SMALL_HOR;
					break;
				case 11: // we're a bottom flat piece
					newvalue = PIX_CARPET_B;
					break;
				case 12: // we're a top-right corner
					newvalue = PIX_CARPET_UR;
					break;
				case 13: // we're a right-edge flat piece
					newvalue = PIX_CARPET_R;
					break;
				case 14: // we're a top-edge flat piece
					newvalue = PIX_CARPET_U;
					break;
				case 15:  // we're surrounded!
					if (here == PIX_CARPET_M || here == PIX_CARPET_M2)
						newvalue = here;
					else
						newvalue = PIX_CARPET_M;
					break;
			}
			break; // end of carpet case
		case TYPE_GRASS_LIGHT:
			switch (around)
			{
				case 0: // all alone
					newvalue = PIX_GRASS_LIGHT_RIGHT; //temp
					break;
				case 1: // we're a bottom "cup"
					newvalue = PIX_GRASS_LIGHT_RIGHT_BOTTOM;
					break;
				case 2: // we're a left edge
					newvalue = PIX_GRASS_LIGHT_LEFT_TOP;
					break;
				case 3: // we're the bottom left ..
					newvalue = PIX_GRASS_LIGHT_LEFT_BOTTOM;
					break;
				case 4: // we're a top "cap"
					newvalue = PIX_GRASS_LIGHT_RIGHT_TOP;
					break;
				case 5: // we're a vertical pipe
					newvalue = PIX_GRASS_LIGHT_RIGHT; //temp
					break;
				case 6: // we're a top-left corner
					newvalue = PIX_GRASS_LIGHT_LEFT_TOP;
					break;
				case 7: // we're a left edge
					newvalue = PIX_GRASS_LIGHT_LEFT;
					break;
				case 8: // we're a right edge end
					newvalue = PIX_GRASS_LIGHT_RIGHT_TOP;
					break;
				case 9: // we're a lower right corner
					newvalue = PIX_GRASS_LIGHT_RIGHT_BOTTOM;
					break;
				case 10: // we're a horizontal pipe
					newvalue = PIX_GRASS_LIGHT_TOP;
					break;
				case 11: // we're a bottom flat piece
					newvalue = PIX_GRASS_LIGHT_BOTTOM;
					break;
				case 12: // we're a top-right corner
					newvalue = PIX_GRASS_LIGHT_RIGHT_TOP;
					break;
				case 13: // we're a right-edge flat piece
					newvalue = PIX_GRASS_LIGHT_RIGHT;
					break;
				case 14: // we're a top-edge flat piece
					newvalue = PIX_GRASS_LIGHT_TOP;
					break;
				case 15:  // we're surrounded!
					newvalue = PIX_GRASS_LIGHT_1;
					break;
			}
			break; // end of light grass case
		case TYPE_WALL:
			if (herepix == PIX_WALL_ARROW_GRASS ||
			        herepix == PIX_WALL_ARROW_FLOOR ||
			        herepix == PIX_WALL4            ||
			        herepix == PIX_WALL_ARROW_GRASS_DARK) // arrow slit?
			{
				if (up == TYPE_GRASS)
					newvalue = PIX_WALL_ARROW_GRASS;
				else if (up == TYPE_GRASS_DARK)
					newvalue = PIX_WALL_ARROW_GRASS_DARK;
				else
				{
					uppix = query_x_y(x, y-1);
					if (uppix == PIX_PAVEMENT1) // stone
						newvalue = PIX_WALL4;
					else if (uppix == PIX_FLOOR1) // wood
						newvalue = PIX_WALL_ARROW_FLOOR;
				}
				break;
			}
			else // we're not an arrow slit ...
			{
				switch (around)
				{
					case 1: // we're the side end of a vertical wall
						newvalue = PIX_WALLSIDE_C;
						break;
					case 3:  // we're the lower-left base of a wall
						newvalue = PIX_WALLSIDE_L;
						break;
					case 4: // (same as case 5)
					case 5: // we're a vertical wall
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
							newvalue = PIX_WALL2;
						else
							newvalue = PIX_WALL_LL;
						break;
					case 6: // lower left "top" of wall
					case 7:
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
							newvalue = PIX_WALL2;
						else
							newvalue = PIX_WALL_LL;
						break;
					case 9: // we're the lower-right base of a wall
						newvalue = PIX_WALLSIDE_R;
						break;
					case 11: // we're the middle base of a wall
						if (random(10) == 0)
							newvalue = PIX_WALLSIDE_CRACK_C1;
						else
							newvalue = PIX_WALLSIDE1;
						break;
					case 12: // we're a top-right corner or horizontal pipe
					case 14:
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
							newvalue = PIX_WALL3;
						else
							newvalue = PIX_H_WALL1;
						break;
					case 13:  // (13 same case as 15)
					case 15: // we're surrounded! hack for now ..
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
						{
							if (query_genre_x_y(x-1, y+1) == TYPE_WALL)
								newvalue = PIX_WALL3;
							else
								newvalue = PIX_WALL2;
						}
						else
						{
							if (query_genre_x_y(x-1, y+1) == TYPE_WALL)
								newvalue = PIX_H_WALL1;
							else
								newvalue = PIX_WALL_LL;
						}
						break;
					default:
						newvalue = herepix;
						break;
				}
			}
			break; // end of walls
		case TYPE_WATER:
			if ( (around == TO_AROUND ) // all around
			        ||(around == (TO_LEFT | TO_RIGHT)) // hor
			        ||(around == (TO_UP | TO_DOWN)) // ver
			        ||(around == (TO_UP | TO_LEFT | TO_RIGHT))
			        ||(around == (TO_DOWN | TO_LEFT | TO_RIGHT))
			        ||(around == (TO_UP | TO_DOWN | TO_LEFT))
			        ||(around == (TO_UP | TO_DOWN | TO_RIGHT))
			   )
				switch (random(3))
				{
					case 0:
						newvalue = PIX_WATER1;
						break;
					case 1:
						newvalue = PIX_WATER2;
						break;
					case 2:
						newvalue = PIX_WATER3;
						break;
				}
			else if (around == (TO_UP | TO_RIGHT) )
				newvalue = PIX_WATERGRASS_LL;
			else if (around == (TO_UP | TO_LEFT) )
				newvalue = PIX_WATERGRASS_LR;
			else if (around == (TO_DOWN | TO_RIGHT) )
				newvalue = PIX_WATERGRASS_UL;
			else if (around == (TO_DOWN | TO_LEFT) )
				newvalue = PIX_WATERGRASS_UR;
			else if (around == (TO_UP) )
				switch (random(2))
				{
					case 0:
						newvalue = PIX_WATERGRASS_LL;
						break;
					case 1:
						newvalue = PIX_WATERGRASS_LR;
						break;
				}
			else if (around == (TO_DOWN) )
				switch (random(2))
				{
					case 0:
						newvalue = PIX_WATERGRASS_UL;
						break;
					case 1:
						newvalue = PIX_WATERGRASS_UR;
						break;
				}
			else if (around == (TO_LEFT) )
				switch (random(2))
				{
					case 0:
						newvalue = PIX_WATERGRASS_UR;
						break;
					case 1:
						newvalue = PIX_WATERGRASS_LR;
						break;
				}
			else if (around == (TO_RIGHT) )
				switch (random(2))
				{
					case 0:
						newvalue = PIX_WATERGRASS_UL;
						break;
					case 1:
						newvalue = PIX_WATERGRASS_LL;
						break;
				}
			else  // Water default:
				newvalue = query_x_y(x, y);
			break;

		case TYPE_TREES:
			if (around == TO_AROUND ) // all around
			{
				if (downright != TYPE_TREES || upright != TYPE_TREES)
					newvalue = PIX_TREE_MR; // right edge..
				else if (downleft != TYPE_TREES || upleft  != TYPE_TREES)
					newvalue = PIX_TREE_ML; // left edge..
				else
					switch (random(3))
					{
						case 0:
							newvalue = PIX_TREE_M1;
							break;
						case 1:
							newvalue = PIX_TREE_M1;
							break;
						case 2:
							newvalue = PIX_TREE_M1;
							break;
					}
			}
			else if (around == (TO_LEFT | TO_RIGHT | TO_DOWN)) // == top middle
				newvalue = PIX_TREE_T1;
			else if (around == (TO_UP | TO_DOWN | TO_LEFT)) // right middle
				newvalue = PIX_TREE_MR;
			else if (around == (TO_LEFT | TO_DOWN)) // top right
				newvalue = PIX_TREE_T1;
			else if (around == (TO_LEFT | TO_RIGHT | TO_UP)) // bottom middle
				newvalue = PIX_TREE_B1;
			else if (around == (TO_LEFT | TO_RIGHT)) // bad case = middle, thin
				newvalue = PIX_TREE_B1;
			else if (around == (TO_LEFT | TO_UP)) // bottom right
				newvalue = PIX_TREE_B1;
			else if (around == TO_LEFT) // bad case = right, thin
				newvalue = PIX_TREE_B1;
			else if (around == (TO_DOWN | TO_RIGHT | TO_UP)) // left middle
				newvalue = PIX_TREE_ML;
			else if (around == (TO_DOWN | TO_RIGHT)) // top left
				newvalue = PIX_TREE_T1;
			else if (around == (TO_DOWN | TO_UP)) // center vertical
				newvalue = PIX_TREE_MT;
			else if (around == TO_DOWN) // top, alone
				newvalue = PIX_TREE_T1;
			else if (around == (TO_RIGHT | TO_UP)) // bottom left
				newvalue = PIX_TREE_B1;
			else if (around == TO_RIGHT) // bad case = left, alone
				newvalue = PIX_TREE_B1;
			else if (around == TO_UP) // bottom, alone
				newvalue = PIX_TREE_B1;
			else
				newvalue = PIX_TREE_B1; // default case
			//if (newvalue == PIX_TREE_T1)
			//  printf("%dx%d = %d\n", x, y, around);
			break;
		case TYPE_DIRT:
			if (around == TO_AROUND ) // all around
				switch (random(3))
				{
					case 0:
						newvalue = PIX_DIRT_1;
						break;
					case 1:
						newvalue = PIX_DIRT_1;
						break;
					case 2:
						newvalue = PIX_DIRT_1;
						break;
				}
			else if (around == (TO_LEFT | TO_RIGHT | TO_DOWN)) // == top middle
				newvalue = PIX_DIRT_1;
			else if (around == (TO_UP | TO_DOWN | TO_LEFT)) // right middle
				newvalue = PIX_DIRT_1;
			else if (around == (TO_LEFT | TO_DOWN)) // top right
				newvalue = PIX_DIRTGRASS_LL1;
			else if (around == (TO_LEFT | TO_RIGHT | TO_UP)) // bottom middle
				newvalue = PIX_DIRT_1;
			else if (around == (TO_LEFT | TO_RIGHT)) // middle, thin
				newvalue = PIX_DIRT_1;
			else if (around == (TO_LEFT | TO_UP)) // bottom right
				newvalue = PIX_DIRTGRASS_UL1;
			else if (around == TO_LEFT) // right, thin
				newvalue = PIX_DIRT_1;
			else if (around == (TO_DOWN | TO_RIGHT | TO_UP)) // left middle
				newvalue = PIX_DIRT_1;
			else if (around == (TO_DOWN | TO_RIGHT)) // top left
				newvalue = PIX_DIRTGRASS_LR1;
			else if (around == (TO_DOWN | TO_UP)) // center vertical
				newvalue = PIX_DIRT_1;
			else if (around == TO_DOWN) // top, alone
				newvalue = PIX_DIRT_1;
			else if (around == (TO_RIGHT | TO_UP)) // bottom left
				newvalue = PIX_DIRTGRASS_UR1;
			else if (around == TO_RIGHT) // left, alone
				newvalue = PIX_DIRT_1;
			else if (around == TO_UP) // bottom, alone
				newvalue = PIX_DIRT_1;
			else
				newvalue = PIX_DIRT_1; // default case
			break; // end of dirt cases
		case TYPE_DIRT_DARK:
			if (around == TO_AROUND ) // all around
				switch (random(3))
				{
					case 0:
						newvalue = PIX_DIRT_DARK_1;
						break;
					case 1:
						newvalue = PIX_DIRT_DARK_1;
						break;
					case 2:
						newvalue = PIX_DIRT_DARK_1;
						break;
				}
			else if (around == (TO_LEFT | TO_RIGHT | TO_DOWN)) // == top middle
				newvalue = PIX_DIRT_DARK_1;
			else if (around == (TO_UP | TO_DOWN | TO_LEFT)) // right middle
				newvalue = PIX_DIRT_DARK_1;
			else if (around == (TO_LEFT | TO_DOWN)) // top right
				newvalue = PIX_DIRTGRASS_DARK_LL1;
			else if (around == (TO_LEFT | TO_RIGHT | TO_UP)) // bottom middle
				newvalue = PIX_DIRT_DARK_1;
			else if (around == (TO_LEFT | TO_RIGHT)) // middle, thin
				newvalue = PIX_DIRT_DARK_1;
			else if (around == (TO_LEFT | TO_UP)) // bottom right
				newvalue = PIX_DIRTGRASS_DARK_UL1;
			else if (around == TO_LEFT) // right, thin
				newvalue = PIX_DIRT_DARK_1;
			else if (around == (TO_DOWN | TO_RIGHT | TO_UP)) // left middle
				newvalue = PIX_DIRT_DARK_1;
			else if (around == (TO_DOWN | TO_RIGHT)) // top left
				newvalue = PIX_DIRTGRASS_DARK_LR1;
			else if (around == (TO_DOWN | TO_UP)) // center vertical
				newvalue = PIX_DIRT_DARK_1;
			else if (around == TO_DOWN) // top, alone
				newvalue = PIX_DIRT_DARK_1;
			else if (around == (TO_RIGHT | TO_UP)) // bottom left
				newvalue = PIX_DIRTGRASS_DARK_UR1;
			else if (around == TO_RIGHT) // left, alone
				newvalue = PIX_DIRT_DARK_1;
			else if (around == TO_UP) // bottom, alone
				newvalue = PIX_DIRT_DARK_1;
			else
				newvalue = PIX_DIRT_DARK_1; // default case
			break; // end of dark dirt cases
		case TYPE_COBBLE: // cobblestone
			switch (random(4))
			{
				case 0:
					newvalue = PIX_COBBLE_1;
					break;
				case 1:
					newvalue = PIX_COBBLE_2;
					break;
				case 2:
					newvalue = PIX_COBBLE_3;
					break;
				case 3:
					newvalue = PIX_COBBLE_4;
					break;
			}
			break;
		case TYPE_UNKNOWN:  // don't change these ..
		default:
			newvalue = query_x_y(x, y);
			break;
	}

	set_x_y(x, y, newvalue);
	return 1;
}

Sint32 smoother::smooth()
{
	Sint32 x, y;

	if (!mygrid)
		return 0;


	for (x=0; x < maxx; x++)
		for (y=0; y < maxy; y++)
			smooth(x, y);

	return 1;
}

void smoother::set_x_y(Sint32 x, Sint32 y, Sint32 whatvalue)
{
	if (!mygrid)
		return;

	mygrid[x+y*maxx] = (char)whatvalue;
}

