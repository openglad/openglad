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

#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/core/pixdefs.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using Uint32 = std::uint32_t;
using Sint32 = std::int32_t;

// Lookup table: PIX_* value → terrain genre TYPE_*
static constexpr auto make_pix_to_genre() {
	std::array<Sint32, PIX_MAX> table{};
	for (auto& v : table) v = TYPE_UNKNOWN;

	table[PIX_GRASS1] = TYPE_GRASS;
	table[PIX_GRASS2] = TYPE_GRASS;
	table[PIX_GRASS3] = TYPE_GRASS;
	table[PIX_GRASS4] = TYPE_GRASS;
	table[PIX_GRASSWATER_LL] = TYPE_GRASS;
	table[PIX_GRASSWATER_LR] = TYPE_GRASS;
	table[PIX_GRASSWATER_UL] = TYPE_GRASS;
	table[PIX_GRASSWATER_UR] = TYPE_GRASS;

	table[PIX_GRASS_DARK_1] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_2] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_3] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_4] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_LL] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_UR] = TYPE_GRASS_DARK;
	table[PIX_GRASS_RUBBLE] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_B1] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_B2] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_BR] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_R1] = TYPE_GRASS_DARK;
	table[PIX_GRASS_DARK_R2] = TYPE_GRASS_DARK;

	table[PIX_GRASS_LIGHT_1] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_TOP] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_RIGHT_TOP] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_RIGHT] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_RIGHT_BOTTOM] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_BOTTOM] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_LEFT_BOTTOM] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_LEFT] = TYPE_GRASS_LIGHT;
	table[PIX_GRASS_LIGHT_LEFT_TOP] = TYPE_GRASS_LIGHT;

	table[PIX_CARPET_LL] = TYPE_CARPET;
	table[PIX_CARPET_L] = TYPE_CARPET;
	table[PIX_CARPET_B] = TYPE_CARPET;
	table[PIX_CARPET_LR] = TYPE_CARPET;
	table[PIX_CARPET_UR] = TYPE_CARPET;
	table[PIX_CARPET_U] = TYPE_CARPET;
	table[PIX_CARPET_UL] = TYPE_CARPET;
	table[PIX_CARPET_M] = TYPE_CARPET;
	table[PIX_CARPET_M2] = TYPE_CARPET;
	table[PIX_CARPET_R] = TYPE_CARPET;
	table[PIX_CARPET_SMALL_HOR] = TYPE_CARPET;
	table[PIX_CARPET_SMALL_VER] = TYPE_CARPET;
	table[PIX_CARPET_SMALL_CUP] = TYPE_CARPET;
	table[PIX_CARPET_SMALL_CAP] = TYPE_CARPET;
	table[PIX_CARPET_SMALL_LEFT] = TYPE_CARPET;
	table[PIX_CARPET_SMALL_RIGHT] = TYPE_CARPET;
	table[PIX_CARPET_SMALL_TINY] = TYPE_CARPET;

	table[PIX_H_WALL1] = TYPE_WALL;
	table[PIX_WALL_LL] = TYPE_WALL;
	table[PIX_WALL2] = TYPE_WALL;
	table[PIX_WALL3] = TYPE_WALL;
	table[PIX_WALL4] = TYPE_WALL;
	table[PIX_WALL5] = TYPE_WALL;
	table[PIX_WALLSIDE1] = TYPE_WALL;
	table[PIX_WALLSIDE_L] = TYPE_WALL;
	table[PIX_WALLSIDE_R] = TYPE_WALL;
	table[PIX_WALLSIDE_C] = TYPE_WALL;
	table[PIX_WALLSIDE_CRACK_C1] = TYPE_WALL;
	table[PIX_WALL_ARROW_GRASS] = TYPE_WALL;
	table[PIX_WALL_ARROW_FLOOR] = TYPE_WALL;
	table[PIX_WALL_ARROW_GRASS_DARK] = TYPE_WALL;

	table[PIX_WATER1] = TYPE_WATER;
	table[PIX_WATER2] = TYPE_WATER;
	table[PIX_WATER3] = TYPE_WATER;
	table[PIX_WATERGRASS_LL] = TYPE_WATER;
	table[PIX_WATERGRASS_LR] = TYPE_WATER;
	table[PIX_WATERGRASS_UL] = TYPE_WATER;
	table[PIX_WATERGRASS_UR] = TYPE_WATER;

	table[PIX_TREE_T1] = TYPE_TREES;
	table[PIX_TREE_M1] = TYPE_TREES;
	table[PIX_TREE_ML] = TYPE_TREES;
	table[PIX_TREE_MR] = TYPE_TREES;
	table[PIX_TREE_MT] = TYPE_TREES;
	table[PIX_TREE_B1] = TYPE_TREES;

	table[PIX_DIRT_1] = TYPE_DIRT;
	table[PIX_DIRTGRASS_UL1] = TYPE_DIRT;
	table[PIX_DIRTGRASS_UR1] = TYPE_DIRT;
	table[PIX_DIRTGRASS_LL1] = TYPE_DIRT;
	table[PIX_DIRTGRASS_LR1] = TYPE_DIRT;

	table[PIX_DIRT_DARK_1] = TYPE_DIRT_DARK;
	table[PIX_DIRTGRASS_DARK_UL1] = TYPE_DIRT_DARK;
	table[PIX_DIRTGRASS_DARK_UR1] = TYPE_DIRT_DARK;
	table[PIX_DIRTGRASS_DARK_LL1] = TYPE_DIRT_DARK;
	table[PIX_DIRTGRASS_DARK_LR1] = TYPE_DIRT_DARK;

	table[PIX_COBBLE_1] = TYPE_COBBLE;
	table[PIX_COBBLE_2] = TYPE_COBBLE;
	table[PIX_COBBLE_3] = TYPE_COBBLE;
	table[PIX_COBBLE_4] = TYPE_COBBLE;

	return table;
}
static constexpr auto PIX_to_genre = make_pix_to_genre();

// Random variant lookup tables for smooth()
static constexpr Sint32 grass_variants[] = {PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4};
static constexpr Sint32 grass_dark_variants[] = {PIX_GRASS_DARK_1, PIX_GRASS_DARK_2, PIX_GRASS_DARK_3, PIX_GRASS_DARK_4};
static constexpr Sint32 grass_dark_right[] = {PIX_GRASS_DARK_R1, PIX_GRASS_DARK_R2};
static constexpr Sint32 grass_dark_bottom[] = {PIX_GRASS_DARK_B1, PIX_GRASS_DARK_B2};
static constexpr Sint32 water_variants[] = {PIX_WATER1, PIX_WATER2, PIX_WATER3};
static constexpr Sint32 watergrass_up[] = {PIX_WATERGRASS_LL, PIX_WATERGRASS_LR};
static constexpr Sint32 watergrass_down[] = {PIX_WATERGRASS_UL, PIX_WATERGRASS_UR};
static constexpr Sint32 watergrass_left[] = {PIX_WATERGRASS_UR, PIX_WATERGRASS_LR};
static constexpr Sint32 watergrass_right[] = {PIX_WATERGRASS_UL, PIX_WATERGRASS_LL};
static constexpr Sint32 cobble_variants[] = {PIX_COBBLE_1, PIX_COBBLE_2, PIX_COBBLE_3, PIX_COBBLE_4};

// Directional lookup tables: surround value (0-15) → PIX tile
static constexpr Sint32 carpet_by_surround[] = {
	PIX_CARPET_SMALL_TINY,   // 0: all alone
	PIX_CARPET_SMALL_CUP,    // 1: bottom cup
	PIX_CARPET_SMALL_LEFT,   // 2: left edge
	PIX_CARPET_LL,            // 3: bottom left
	PIX_CARPET_SMALL_CAP,    // 4: top cap
	PIX_CARPET_SMALL_VER,    // 5: vertical pipe
	PIX_CARPET_UL,            // 6: top-left corner
	PIX_CARPET_L,             // 7: left edge
	PIX_CARPET_SMALL_RIGHT,  // 8: right edge end
	PIX_CARPET_LR,            // 9: lower right corner
	PIX_CARPET_SMALL_HOR,    // 10: horizontal pipe
	PIX_CARPET_B,             // 11: bottom flat piece
	PIX_CARPET_UR,            // 12: top-right corner
	PIX_CARPET_R,             // 13: right-edge flat piece
	PIX_CARPET_U,             // 14: top-edge flat piece
	PIX_CARPET_M,             // 15: surrounded
};

static constexpr Sint32 grass_light_by_surround[] = {
	PIX_GRASS_LIGHT_RIGHT,          // 0: all alone
	PIX_GRASS_LIGHT_RIGHT_BOTTOM,   // 1: bottom cup
	PIX_GRASS_LIGHT_LEFT_TOP,       // 2: left edge
	PIX_GRASS_LIGHT_LEFT_BOTTOM,    // 3: bottom left
	PIX_GRASS_LIGHT_RIGHT_TOP,      // 4: top cap
	PIX_GRASS_LIGHT_RIGHT,          // 5: vertical pipe
	PIX_GRASS_LIGHT_LEFT_TOP,       // 6: top-left corner
	PIX_GRASS_LIGHT_LEFT,           // 7: left edge
	PIX_GRASS_LIGHT_RIGHT_TOP,      // 8: right edge end
	PIX_GRASS_LIGHT_RIGHT_BOTTOM,   // 9: lower right corner
	PIX_GRASS_LIGHT_TOP,            // 10: horizontal pipe
	PIX_GRASS_LIGHT_BOTTOM,         // 11: bottom flat piece
	PIX_GRASS_LIGHT_RIGHT_TOP,      // 12: top-right corner
	PIX_GRASS_LIGHT_RIGHT,          // 13: right-edge flat piece
	PIX_GRASS_LIGHT_TOP,            // 14: top-edge flat piece
	PIX_GRASS_LIGHT_1,              // 15: surrounded
};

static constexpr Sint32 dirt_by_surround[] = {
	PIX_DIRT_1,          // 0: all alone
	PIX_DIRT_1,          // 1: TO_UP
	PIX_DIRT_1,          // 2: TO_RIGHT
	PIX_DIRTGRASS_UR1,   // 3: TO_UP | TO_RIGHT
	PIX_DIRT_1,          // 4: TO_DOWN
	PIX_DIRT_1,          // 5: TO_UP | TO_DOWN
	PIX_DIRTGRASS_LR1,   // 6: TO_DOWN | TO_RIGHT
	PIX_DIRT_1,          // 7: TO_UP | TO_DOWN | TO_RIGHT
	PIX_DIRT_1,          // 8: TO_LEFT
	PIX_DIRTGRASS_UL1,   // 9: TO_UP | TO_LEFT
	PIX_DIRT_1,          // 10: TO_LEFT | TO_RIGHT
	PIX_DIRT_1,          // 11: TO_UP | TO_LEFT | TO_RIGHT
	PIX_DIRTGRASS_LL1,   // 12: TO_DOWN | TO_LEFT
	PIX_DIRT_1,          // 13: TO_UP | TO_DOWN | TO_LEFT
	PIX_DIRT_1,          // 14: TO_DOWN | TO_LEFT | TO_RIGHT
	PIX_DIRT_1,          // 15: all around
};

static constexpr Sint32 dirt_dark_by_surround[] = {
	PIX_DIRT_DARK_1,          // 0: all alone
	PIX_DIRT_DARK_1,          // 1: TO_UP
	PIX_DIRT_DARK_1,          // 2: TO_RIGHT
	PIX_DIRTGRASS_DARK_UR1,   // 3: TO_UP | TO_RIGHT
	PIX_DIRT_DARK_1,          // 4: TO_DOWN
	PIX_DIRT_DARK_1,          // 5: TO_UP | TO_DOWN
	PIX_DIRTGRASS_DARK_LR1,   // 6: TO_DOWN | TO_RIGHT
	PIX_DIRT_DARK_1,          // 7: TO_UP | TO_DOWN | TO_RIGHT
	PIX_DIRT_DARK_1,          // 8: TO_LEFT
	PIX_DIRTGRASS_DARK_UL1,   // 9: TO_UP | TO_LEFT
	PIX_DIRT_DARK_1,          // 10: TO_LEFT | TO_RIGHT
	PIX_DIRT_DARK_1,          // 11: TO_UP | TO_LEFT | TO_RIGHT
	PIX_DIRTGRASS_DARK_LL1,   // 12: TO_DOWN | TO_LEFT
	PIX_DIRT_DARK_1,          // 13: TO_UP | TO_DOWN | TO_LEFT
	PIX_DIRT_DARK_1,          // 14: TO_DOWN | TO_LEFT | TO_RIGHT
	PIX_DIRT_DARK_1,          // 15: all around
};

smoother::smoother()
    : maxx(0), maxy(0), rng_(nullptr)
{}

void smoother::reset()
{
    mygrid_span_ = {};
    maxx = 0;
    maxy = 0;
}

void smoother::set_rng(IRandom* rng)
{
    rng_ = rng;
}

void smoother::set_target(const PixieData& data)
{
	mygrid_span_ = {data.data.get(), static_cast<std::size_t>(data.w) * data.h};
	maxx = data.w;
	maxy = data.h;
}

Uint32 smoother::next_random(Uint32 max_exclusive) const
{
    if (max_exclusive == 0)
        return 0;
    if (IRandom* override_rng = gameplay_rng_override())
        return override_rng->next(max_exclusive);
    assert(rng_ != nullptr);
    return rng_->next(max_exclusive);
}

Sint32 smoother::query_x_y(Sint32 x, Sint32 y)
{
	// Are we set up yet?
	if (mygrid_span_.empty())
		return PIX_GRASS1;

	// Check boundaries ..
	if ( (x < 0) || (y < 0) )
		return PIX_GRASS1;

	if ( (x >= maxx) || (y >= maxy) )
		return PIX_GRASS1;

	// Else, return our simple grid data ..
	return static_cast<Sint32>(mygrid_span_[static_cast<std::size_t>(x + y*maxx)]);
}

Sint32 smoother::query_genre_x_y(Sint32 x, Sint32 y)
{
	Sint32 basetype = query_x_y(x, y);
	if (basetype < 0 || basetype >= PIX_MAX)
		return TYPE_UNKNOWN;
	return PIX_to_genre[basetype];
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
				newvalue = grass_variants[next_random(4)];
			break;
		case TYPE_GRASS_DARK:  // Shadowed grass
			if (around == TO_AROUND ) // all around
			{
				newvalue = grass_dark_variants[next_random(4)];
			}
			else if ( (left == TYPE_TREES || left == TYPE_WALL)
			          && (down == TYPE_TREES || down == TYPE_WALL)
			          && (upright != TYPE_TREES && upright != TYPE_WALL) ) // act as right edge
			{
				newvalue = grass_dark_right[next_random(2)];
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
						newvalue = grass_dark_bottom[next_random(2)];
						if (!next_random(20)) // then place a bit o' rubble
							newvalue = PIX_GRASS_RUBBLE;
						break;
					default:
						newvalue = grass_dark_variants[next_random(4)];
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
				newvalue = grass_dark_right[next_random(2)];
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
				newvalue = grass_dark_bottom[next_random(2)];
				if (!next_random(20)) // then place a bit o' rubble
					newvalue = PIX_GRASS_RUBBLE;
			}
			else if (around == (TO_LEFT | TO_RIGHT)) // middle, thin
			{
				newvalue = grass_dark_bottom[next_random(2)];
				if (!next_random(20)) // then place a bit o' rubble
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
				newvalue = grass_dark_variants[next_random(4)];
			}
			else if (around == (TO_DOWN | TO_UP)) // center vertical
			{
				newvalue = grass_dark_right[next_random(2)];
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
			newvalue = carpet_by_surround[around];
			break; // end of carpet case
		case TYPE_GRASS_LIGHT:
			newvalue = grass_light_by_surround[around];
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
						if (next_random(10) == 0)
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
				newvalue = water_variants[next_random(3)];
			else if (around == (TO_UP | TO_RIGHT) )
				newvalue = PIX_WATERGRASS_LL;
			else if (around == (TO_UP | TO_LEFT) )
				newvalue = PIX_WATERGRASS_LR;
			else if (around == (TO_DOWN | TO_RIGHT) )
				newvalue = PIX_WATERGRASS_UL;
			else if (around == (TO_DOWN | TO_LEFT) )
				newvalue = PIX_WATERGRASS_UR;
			else if (around == (TO_UP) )
				newvalue = watergrass_up[next_random(2)];
			else if (around == (TO_DOWN) )
				newvalue = watergrass_down[next_random(2)];
			else if (around == (TO_LEFT) )
				newvalue = watergrass_left[next_random(2)];
			else if (around == (TO_RIGHT) )
				newvalue = watergrass_right[next_random(2)];
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
					newvalue = PIX_TREE_M1;
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
			break;
		case TYPE_DIRT:
			newvalue = dirt_by_surround[around];
			break; // end of dirt cases
		case TYPE_DIRT_DARK:
			newvalue = dirt_dark_by_surround[around];
			break; // end of dark dirt cases
		case TYPE_COBBLE: // cobblestone
			newvalue = cobble_variants[next_random(4)];
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

	if (mygrid_span_.empty())
		return 0;


	for (x=0; x < maxx; x++)
		for (y=0; y < maxy; y++)
			smooth(x, y);

	return 1;
}

void smoother::set_x_y(Sint32 x, Sint32 y, Sint32 whatvalue)
{
	if (mygrid_span_.empty())
		return;

	mygrid_span_[static_cast<std::size_t>(x+y*maxx)] = static_cast<unsigned char>(whatvalue);
}
