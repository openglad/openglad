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
	// plumbing: constexpr table fill: every PIX id starts TYPE_UNKNOWN; the mapping itself is
	//   asserted by test_smoother.cpp Smoother.query_genre_maps_known_tiles
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

	// all the carpets ..
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

	// these are various walls ..
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
	table[PIX_WATERGRASS_U] = TYPE_WATER;
	table[PIX_WATERGRASS_L] = TYPE_WATER;
	table[PIX_WATERGRASS_R] = TYPE_WATER;
	table[PIX_WATERGRASS_D] = TYPE_WATER;

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

	// Z-axis / multi-floor tiles. Mapped for query_genre_x_y consumers but kept
	// out of smooth()'s autotiler switch (inert default branch).
	table[PIX_AIR] = TYPE_AIR;
	table[PIX_GLASS] = TYPE_GLASS;
	table[PIX_DROPBLOCK_UP] = TYPE_DROP_BLOCK;
	table[PIX_DROPBLOCK_RIGHT] = TYPE_DROP_BLOCK;
	table[PIX_DROPBLOCK_DOWN] = TYPE_DROP_BLOCK;
	table[PIX_DROPBLOCK_LEFT] = TYPE_DROP_BLOCK;
	table[PIX_ZSTAIR_UP] = TYPE_ZSTAIRS;
	table[PIX_ZSTAIR_DOWN] = TYPE_ZSTAIRS;

	// Westlands terrain. Genre-inert in the autotiler (no smooth() arms);
	// the mapgen paints variants deterministically, pavement-style.
	table[PIX_SNOW1] = TYPE_SNOW;
	table[PIX_SNOW2] = TYPE_SNOW;
	table[PIX_LAVA1] = TYPE_LAVA;
	table[PIX_LAVA2] = TYPE_LAVA;
	table[PIX_MARSH1] = TYPE_MARSH;
	table[PIX_MARSH2] = TYPE_MARSH;
	table[PIX_ASH1] = TYPE_ASH;
	table[PIX_ASH2] = TYPE_ASH;

	return table;
}
static constexpr auto PIX_to_genre = make_pix_to_genre();

// Random variant lookup tables for smooth()
static constexpr std::array<Sint32, 4> grass_variants = {PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4};
static constexpr std::array<Sint32, 4> grass_dark_variants = {PIX_GRASS_DARK_1, PIX_GRASS_DARK_2, PIX_GRASS_DARK_3, PIX_GRASS_DARK_4};
static constexpr std::array<Sint32, 2> grass_dark_right = {PIX_GRASS_DARK_R1, PIX_GRASS_DARK_R2};
static constexpr std::array<Sint32, 2> grass_dark_bottom = {PIX_GRASS_DARK_B1, PIX_GRASS_DARK_B2};
static constexpr std::array<Sint32, 3> water_variants = {PIX_WATER1, PIX_WATER2, PIX_WATER3};
static constexpr std::array<Sint32, 2> watergrass_up = {PIX_WATERGRASS_LL, PIX_WATERGRASS_LR};
static constexpr std::array<Sint32, 2> watergrass_down = {PIX_WATERGRASS_UL, PIX_WATERGRASS_UR};
static constexpr std::array<Sint32, 2> watergrass_left = {PIX_WATERGRASS_UR, PIX_WATERGRASS_LR};
static constexpr std::array<Sint32, 2> watergrass_right = {PIX_WATERGRASS_UL, PIX_WATERGRASS_LL};
static constexpr std::array<Sint32, 4> cobble_variants = {PIX_COBBLE_1, PIX_COBBLE_2, PIX_COBBLE_3, PIX_COBBLE_4};

// Directional lookup tables: surround value (0-15) → PIX tile
static constexpr std::array<Sint32, 16> carpet_by_surround = {
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

static constexpr std::array<Sint32, 16> grass_light_by_surround = {
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

static constexpr std::array<Sint32, 16> dirt_by_surround = {
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

static constexpr std::array<Sint32, 16> dirt_dark_by_surround = {
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
    // plumbing: next_random prefers the gameplay RNG override (test seam / GameContext rng) over
    //   the bound world rng; no game semantic
    if (IRandom* override_rng = gameplay_rng_override())
        return override_rng->next(max_exclusive);
    assert(rng_ != nullptr);
    return rng_->next(max_exclusive);
}

Sint32 smoother::query_x_y(Sint32 x, Sint32 y)
{
	// Are we set up yet?
	// rule: Off the map, or before a map is bound, the terrain reads as plain grass (PIX_GRASS1),
	//   so edge tiles smooth as if grass lay beyond and a forestwalker at the edge is not 'in
	//   trees' off-map.
	// tests: SmoothOps.smooth_query_x_y_no_grid
	// tests: SmoothUnit.smooth_r11_out_of_range_neighbours_read_as_grass_and_writes_are_clamped
	// tests: SmoothOps.smooth_query_x_y_with_grid
	// tests: SmoothMatrix.smooth_query_helpers_and_grass_water_corner_branches
	if (mygrid_span_.empty())
		return PIX_GRASS1;

	// Check boundaries ..
	// rule: Part of the rule above that begins "Off the map, or before a map is bound, ..."
	// tests: SmoothOps.smooth_query_x_y_no_grid
	// tests: SmoothUnit.smooth_r11_out_of_range_neighbours_read_as_grass_and_writes_are_clamped
	// tests: SmoothOps.smooth_query_x_y_with_grid
	// tests: SmoothMatrix.smooth_query_helpers_and_grass_water_corner_branches
	if ( (x < 0) || (y < 0) )
		return PIX_GRASS1;

	// rule: Part of the rule above that begins "Off the map, or before a map is bound, ..."
	// tests: SmoothOps.smooth_query_x_y_no_grid
	// tests: SmoothUnit.smooth_r11_out_of_range_neighbours_read_as_grass_and_writes_are_clamped
	// tests: SmoothOps.smooth_query_x_y_with_grid
	// tests: SmoothMatrix.smooth_query_helpers_and_grass_water_corner_branches
	if ( (x >= maxx) || (y >= maxy) )
		return PIX_GRASS1;

	// Else, return our simple grid data ..
	return static_cast<Sint32>(mygrid_span_[static_cast<std::size_t>(x + y*maxx)]);
}

Sint32 smoother::query_genre_x_y(Sint32 x, Sint32 y)
{
	Sint32 basetype = query_x_y(x, y);
	// plumbing: a stored byte outside 0..PIX_MAX-1 has no genre (TYPE_UNKNOWN) instead of indexing
	//   past the table
	if (basetype < 0 || basetype >= PIX_MAX)
		return TYPE_UNKNOWN;
	return PIX_to_genre[static_cast<std::size_t>(basetype)];
}

Sint32 smoother::surrounds(Sint32 x, Sint32 y, Sint32 whatgenre)
{
	Sint32 howmany = 0;

	// rule: A tile's 'around' mask counts which of its four neighbours share its terrain genre: up
	//   1, right 2, down 4, left 8.
	// tests: Smoother.surrounds_bitmask_counts_neighbors
	// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
	if (query_genre_x_y(x, y-1) == whatgenre) // above
		howmany += 1;

	// rule: Part of the rule above that begins "A tile's 'around' mask counts which of its four
	//   ..."
	// tests: Smoother.surrounds_bitmask_counts_neighbors
	// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
	if (query_genre_x_y(x+1, y) == whatgenre) // right
		howmany += 2;

	// rule: Part of the rule above that begins "A tile's 'around' mask counts which of its four
	//   ..."
	// tests: Smoother.surrounds_bitmask_counts_neighbors
	// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
	if (query_genre_x_y(x, y+1) == whatgenre) // below
		howmany += 4;

	// rule: Part of the rule above that begins "A tile's 'around' mask counts which of its four
	//   ..."
	// tests: Smoother.surrounds_bitmask_counts_neighbors
	// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
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


	// rule: Grass with water wrapped around one corner (the corner plus its two sides and the two
	//   adjacent diagonals) becomes the matching grass/water shoreline corner; otherwise it becomes
	//   one of four plain grass variants at random. The autotiler runs in the level editor and the
	//   mapgen tools (builders.cpp:138, level_editor.cpp), never in a sim tick; only
	//   query_x_y/query_genre_x_y feed the sim (forestwalk, effects).
	// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
	// tests: SmoothMatrix.smooth_query_helpers_and_grass_water_corner_branches
	// tests: CoverageMisc.final_r16_smooth_targeted_grass_and_dark_variants
	switch (here) // switch on genre
	{
		case TYPE_GRASS:
			// rule: Part of the rule above that begins "Grass with water wrapped around one corner
			//   (the corner ..."
			// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
			// tests: SmoothMatrix.smooth_query_helpers_and_grass_water_corner_branches
			// tests: CoverageMisc.final_r16_smooth_targeted_grass_and_dark_variants
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
		// rule: Without a shadow caster in play, dark grass picks its piece from which neighbours
		//   are also dark grass (mask), plain grass beside an open edge giving the LL/UR fade
		//   pieces; anything else is PIX_GRASS_DARK_1.
		// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
		// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
		// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
		// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
		// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
		// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
		// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
		case TYPE_GRASS_DARK:  // Shadowed grass
			// rule: Part of the rule above that begins "Without a shadow caster in play, dark grass
			//   picks ..."
			// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
			// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
			// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
			// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
			// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
			// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
			// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
			if (around == TO_AROUND ) // all around
			{
				newvalue = grass_dark_variants[next_random(4)];
			}
			else if ( (left == TYPE_TREES || left == TYPE_WALL)
			          && (down == TYPE_TREES || down == TYPE_WALL)
			          && (upright != TYPE_TREES && upright != TYPE_WALL) ) // act as right edge // rule: Dark grass is the shadow trees
			              //   and walls cast: with a shadow caster to its left and below it is a
			              //   right-edge piece; under a caster (upleft and up) it is a bottom piece
			              //   if lighter ground (grass, water, trees, dirt, cobble) lies below,
			              //   with a 1-in-20 rubble tile, else plain dark; with casters up and
			              //   right it is the UR corner.
			              // tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
			              // tests: SmoothMoreBranches.dark_grass_under_trees_over_more_shadow_stays_plain_dark
			              // tests: CoverageMisc.coverage_r17_smooth_grass_water_and_dark_variants
			{
				newvalue = grass_dark_right[next_random(2)];
				break;
			}
			else if (   (upleft == TYPE_TREES || upleft == TYPE_WALL)
			            && (up == TYPE_TREES || up == TYPE_WALL) ) // act as bottom middle // rule: Part of the rule above that
			                //   begins "Dark grass is the shadow trees and walls cast: ..."
			                // tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
			                // tests: SmoothMoreBranches.dark_grass_under_trees_over_more_shadow_stays_plain_dark
			                // tests: CoverageMisc.coverage_r17_smooth_grass_water_and_dark_variants
			{
				// rule: Part of the rule above that begins "Dark grass is the shadow trees and
				//   walls cast: ..."
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SmoothMoreBranches.dark_grass_under_trees_over_more_shadow_stays_plain_dark
				// tests: CoverageMisc.coverage_r17_smooth_grass_water_and_dark_variants
				switch (down) // are we a bottom middle, or center? depends..
				{
					case TYPE_GRASS:
					case TYPE_WATER:
					case TYPE_TREES:
					case TYPE_DIRT:
					case TYPE_COBBLE:
						newvalue = grass_dark_bottom[next_random(2)];
						// rule: Part of the rule above that begins "Dark grass is the shadow trees
						//   and walls cast: ..."
						// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
						// tests: SmoothMoreBranches.dark_grass_under_trees_over_more_shadow_stays_plain_dark
						// tests: CoverageMisc.coverage_r17_smooth_grass_water_and_dark_variants
						if (!next_random(20)) // then place a bit o' rubble
							newvalue = PIX_GRASS_RUBBLE;
						break;
					default:
						newvalue = grass_dark_variants[next_random(4)];
						break;
				} // end case-check of what's below us
			}  // end of first bottom-middle case
			else if ( (up == TYPE_TREES || up == TYPE_WALL) &&
			          (right==TYPE_TREES|| right==TYPE_WALL) )  // rule: Part of the rule above that
			              //   begins "Dark grass is the shadow trees and walls cast: ..."
			              // tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
			              // tests: SmoothMoreBranches.dark_grass_under_trees_over_more_shadow_stays_plain_dark
			              // tests: CoverageMisc.coverage_r17_smooth_grass_water_and_dark_variants
				newvalue = PIX_GRASS_DARK_UR;
			else if (around == (TO_LEFT | TO_RIGHT | TO_DOWN)) // == top middle
			{
				newvalue = grass_dark_variants[next_random(4)];
			}
			else if (around == (TO_UP | TO_DOWN | TO_LEFT)) // right middle
			{
				newvalue = grass_dark_right[next_random(2)];
			}
			else if (around == (TO_LEFT | TO_DOWN)) // top right
			{
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
				if (right == TYPE_GRASS)
					newvalue = PIX_GRASS_DARK_LL;
				else
					newvalue = PIX_GRASS_DARK_B2;
			}
			else if (around == (TO_LEFT | TO_RIGHT | TO_UP)) // bottom middle
			{
				newvalue = grass_dark_bottom[next_random(2)];
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
				if (!next_random(20)) // then place a bit o' rubble
					newvalue = PIX_GRASS_RUBBLE;
			}
			else if (around == (TO_LEFT | TO_RIGHT)) // middle, thin
			{
				newvalue = grass_dark_bottom[next_random(2)];
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
				if (!next_random(20)) // then place a bit o' rubble
					newvalue = PIX_GRASS_RUBBLE;
			}
			else if (around == (TO_LEFT | TO_UP)) // bottom right
				newvalue = PIX_GRASS_DARK_BR;
			else if (around == TO_LEFT) // right, thin
			{
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
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
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
				if ( (right == TYPE_GRASS) || (up == TYPE_GRASS) )
					newvalue = PIX_GRASS_DARK_LL;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else if (around == (TO_RIGHT | TO_UP)) // bottom left
			{
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
				if ( (left==TYPE_GRASS) || (down==TYPE_GRASS) )
					newvalue = PIX_GRASS_DARK_UR;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else if (around == TO_RIGHT) // left, alone
			{
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
				if (left==TYPE_GRASS)
					newvalue = PIX_GRASS_DARK_UR;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else if (around == TO_UP) // bottom, alone
			{
				// rule: Part of the rule above that begins "Without a shadow caster in play, dark
				//   grass picks ..."
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_round9_dark_grass_single_neighbor_branch_pairs
				// tests: SmoothUnit.dark_grass_top_middle_draws_a_dark_variant
				// tests: SmoothOps.smooth_dark_grass_round7_branch_matrix_338_448
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.dark_grass_open_to_the_right_or_top_fades_to_ll_only_beside_plain_grass
				// tests: SemSmooth.dark_grass_open_to_the_left_or_bottom_fades_to_ur_only_beside_plain_grass
				if (down==TYPE_GRASS)
					newvalue = PIX_GRASS_DARK_UR;
				else
					newvalue = PIX_GRASS_DARK_B1;
			}
			else
				newvalue = PIX_GRASS_DARK_1; // default case
			break;
		// rule: Carpet pieces follow a fixed table by the carpet-neighbour mask.
		// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
		case TYPE_CARPET:
			newvalue = carpet_by_surround[static_cast<std::size_t>(around)];
			break; // end of carpet case
		// rule: Light grass pieces follow a fixed table by mask.
		// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
		case TYPE_GRASS_LIGHT:
			newvalue = grass_light_by_surround[static_cast<std::size_t>(around)];
			break; // end of light grass case
		// rule: An arrow-slit wall re-skins its sill to the ground above it: grass, dark grass,
		//   stone pavement or wood floor; with no matching sill it keeps its own tile.
		// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
		// tests: SmoothUnit.arrow_slit_with_no_matching_sill_keeps_its_own_tile
		// tests: SmoothCoverage.smooth_wall_arrow_slit_branches
		case TYPE_WALL:
			// rule: Part of the rule above that begins "An arrow-slit wall re-skins its sill to the
			//   ground ..."
			// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
			// tests: SmoothUnit.arrow_slit_with_no_matching_sill_keeps_its_own_tile
			// tests: SmoothCoverage.smooth_wall_arrow_slit_branches
			if (herepix == PIX_WALL_ARROW_GRASS ||
			        herepix == PIX_WALL_ARROW_FLOOR ||
			        herepix == PIX_WALL4            ||
			        herepix == PIX_WALL_ARROW_GRASS_DARK) // arrow slit?
			{
				// rule: Part of the rule above that begins "An arrow-slit wall re-skins its sill to
				//   the ground ..."
				// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
				// tests: SmoothUnit.arrow_slit_with_no_matching_sill_keeps_its_own_tile
				// tests: SmoothCoverage.smooth_wall_arrow_slit_branches
				if (up == TYPE_GRASS)
					newvalue = PIX_WALL_ARROW_GRASS;
				else if (up == TYPE_GRASS_DARK)
					newvalue = PIX_WALL_ARROW_GRASS_DARK;
				else
				{
					uppix = query_x_y(x, y-1);
					// rule: Part of the rule above that begins "An arrow-slit wall re-skins its
					//   sill to the ground ..."
					// tests: Smoother.smooth_writes_the_exact_tile_for_every_genre_and_around_mask
					// tests: SmoothUnit.arrow_slit_with_no_matching_sill_keeps_its_own_tile
					// tests: SmoothCoverage.smooth_wall_arrow_slit_branches
					if (uppix == PIX_PAVEMENT1) // stone
						newvalue = PIX_WALL4;
					else if (uppix == PIX_FLOOR1) // wood
						newvalue = PIX_WALL_ARROW_FLOOR;
					else // no sill to match: keep the slit we have
						newvalue = herepix;
				}
				break;
			}
			else // we're not an arrow slit ... // rule: A plain wall picks its piece from its
				//   wall-neighbour mask and whether the wall continues two tiles down (a face vs a
				//   top) and below-left: side end, lower-left/right bases, vertical runs, middle
				//   base (1-in-10 cracked), top-right corners and horizontal pipes, and the
				//   surrounded cases; any other mask (0, 2, 8, 10) keeps the tile it has.
				// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
				// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
				// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
			{
				// rule: Part of the rule above that begins "A plain wall picks its piece from its
				//   wall-neighbour ..."
				// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
				// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
				// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
				// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
				// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
				// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
				// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
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
						// rule: Part of the rule above that begins "A plain wall picks its piece
						//   from its wall-neighbour ..."
						// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
						// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
						// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
						// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
						// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
						// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
						// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
							newvalue = PIX_WALL2;
						else
							newvalue = PIX_WALL_LL;
						break;
					case 6: // lower left "top" of wall
					case 7:
						// rule: Part of the rule above that begins "A plain wall picks its piece
						//   from its wall-neighbour ..."
						// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
						// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
						// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
						// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
						// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
						// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
						// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
							newvalue = PIX_WALL2;
						else
							newvalue = PIX_WALL_LL;
						break;
					case 9: // we're the lower-right base of a wall
						newvalue = PIX_WALLSIDE_R;
						break;
					case 11: // we're the middle base of a wall
						// rule: Part of the rule above that begins "A plain wall picks its piece
						//   from its wall-neighbour ..."
						// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
						// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
						// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
						// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
						// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
						// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
						// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
						if (next_random(10) == 0)
							newvalue = PIX_WALLSIDE_CRACK_C1;
						else
							newvalue = PIX_WALLSIDE1;
						break;
					case 12: // we're a top-right corner or horizontal pipe
					case 14:
						// rule: Part of the rule above that begins "A plain wall picks its piece
						//   from its wall-neighbour ..."
						// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
						// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
						// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
						// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
						// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
						// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
						// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
							newvalue = PIX_WALL3;
						else
							newvalue = PIX_H_WALL1;
						break;
					case 13:  // (13 same case as 15)
					case 15: // we're surrounded! hack for now ..
						// rule: Part of the rule above that begins "A plain wall picks its piece
						//   from its wall-neighbour ..."
						// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
						// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
						// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
						// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
						// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
						// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
						// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
						if (query_genre_x_y(x, y+2) == TYPE_WALL)
						{
							// rule: Part of the rule above that begins "A plain wall picks its
							//   piece from its wall-neighbour ..."
							// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
							// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
							// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
							// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
							// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
							// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
							// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
							if (query_genre_x_y(x-1, y+1) == TYPE_WALL)
								newvalue = PIX_WALL3;
							else
								newvalue = PIX_WALL2;
						}
						else
						{
							// rule: Part of the rule above that begins "A plain wall picks its
							//   piece from its wall-neighbour ..."
							// tests: SmoothMoreBranches.smooth_wall_vertical_and_base_cases
							// tests: SmoothCoverage.smooth_round14_wall_case11_and_case15_branch_matrix
							// tests: SmoothMatrix.grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile
							// tests: SmoothUnit.smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection
							// tests: SmoothMoreBranches.dark_grass_and_wall_clusters_select_their_exact_piece
							// tests: SemSmooth.a_wall_middle_base_is_plain_wallside_unless_the_one_in_ten_roll_cracks_it
							// tests: SemSmooth.classic_a_wall_with_only_side_neighbours_keeps_its_painted_tile
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
		// rule: Water with water on both sides along a line or on three-four sides is open water (3
		//   variants); water at a corner of a lake is the matching water/grass corner; water with a
		//   single water neighbour is a shoreline piece (2 variants); lone water keeps its tile.
		// tests: SmoothMatrix.water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile
		// tests: SmoothMoreBranches.water_shoreline_pairs_take_their_second_entry_under_rng_1
		case TYPE_WATER:
			// rule: Part of the rule above that begins "Water with water on both sides along a line
			//   ..."
			// tests: SmoothMatrix.water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile
			// tests: SmoothMoreBranches.water_shoreline_pairs_take_their_second_entry_under_rng_1
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

		// rule: Trees pick canopy pieces from the tree mask: fully surrounded trees are a right or
		//   left edge if a diagonal on that side is open, else the middle; top rows are tops,
		//   bottoms (and every thin/odd case) trunks, vertical pipes the middle-top, side columns
		//   the left/right middles.
		// tests: SmoothMatrix.water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile
		// tests: SmoothMoreBranches.smooth_tree_dirt_dark_dirt_mask_ladders
		// tests: SmoothCoverage.smooth_round11_water_diagonals_and_tree_to_around_edges
		// tests: CoverageMisc.coverage_r18_smooth_targeted_mask_branches
		case TYPE_TREES:
			// rule: Part of the rule above that begins "Trees pick canopy pieces from the tree
			//   mask: fully ..."
			// tests: SmoothMatrix.water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile
			// tests: SmoothMoreBranches.smooth_tree_dirt_dark_dirt_mask_ladders
			// tests: SmoothCoverage.smooth_round11_water_diagonals_and_tree_to_around_edges
			// tests: CoverageMisc.coverage_r18_smooth_targeted_mask_branches
			if (around == TO_AROUND ) // all around
			{
				// rule: Part of the rule above that begins "Trees pick canopy pieces from the tree
				//   mask: fully ..."
				// tests: SmoothMatrix.water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile
				// tests: SmoothMoreBranches.smooth_tree_dirt_dark_dirt_mask_ladders
				// tests: SmoothCoverage.smooth_round11_water_diagonals_and_tree_to_around_edges
				// tests: CoverageMisc.coverage_r18_smooth_targeted_mask_branches
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
		// rule: Dirt takes a dirt/grass corner piece where two adjacent sides are dirt, else plain
		//   dirt (table by mask).
		// tests: SmoothMatrix.targets_tree_dirt_dark_dirt_large_mask_blocks
		// tests: SmoothMatrix.water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile
		case TYPE_DIRT:
			newvalue = dirt_by_surround[static_cast<std::size_t>(around)];
			break; // end of dirt cases
		// rule: Dark dirt does the same with its own table.
		// tests: SmoothMatrix.targets_tree_dirt_dark_dirt_large_mask_blocks
		// tests: SmoothMatrix.water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile
		case TYPE_DIRT_DARK:
			newvalue = dirt_dark_by_surround[static_cast<std::size_t>(around)];
			break; // end of dark dirt cases
		// rule: Cobblestone becomes one of four variants at random.
		// tests: Smoother.smooth_cobble_walks_every_variant_the_rng_indexes
		case TYPE_COBBLE: // cobblestone
			newvalue = cobble_variants[next_random(4)];
			break;
		// rule: Terrain with no smoothing rule (unknown, pavement, floors, Z-axis tiles, snow,
		//   lava, marsh, ash) is left exactly as painted.
		// tests: SmoothMoreBranches.smooth_wall_water_tree_unknown_and_setxy_guard_paths
		// tests: SemSmooth.terrain_with_no_smoothing_rule_is_left_exactly_as_painted
		case TYPE_UNKNOWN:  // don't change these ..
		// rule: Part of the rule above that begins "Terrain with no smoothing rule (unknown,
		//   pavement, floors, Z-axis ..."
		// tests: SmoothMoreBranches.smooth_wall_water_tree_unknown_and_setxy_guard_paths
		// tests: SemSmooth.terrain_with_no_smoothing_rule_is_left_exactly_as_painted
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

	// plumbing: smooth() with no bound grid returns 0 (asserted
	//   SmoothUnit.smooth_without_a_target_returns_zero_and_reset_clears_it)
	if (mygrid_span_.empty())
		return 0;


	// plumbing: whole-map pass of smooth(x,y) column by column; rules at smooth.cpp:349..646
	for (x=0; x < maxx; x++)
		// plumbing: whole-map pass of smooth(x,y) column by column; rules at smooth.cpp:349..646
		for (y=0; y < maxy; y++)
			smooth(x, y);

	return 1;
}

void smoother::set_x_y(Sint32 x, Sint32 y, Sint32 whatvalue)
{
	// plumbing: set_x_y ignores writes with no grid or outside the map (OOB guard, issue #12);
	//   asserted SmoothCoverage.set_x_y_rejects_out_of_bounds_writes
	if (mygrid_span_.empty())
		return;

	// Mirror query_x_y()'s bounds check: the read path already rejects
	// out-of-range coordinates, so the symmetric write must too. Without this
	// guard a future caller could store past the end of the grid span (an OOB
	// write into the level buffer); the index math below assumes 0<=x<maxx and
	// 0<=y<maxy.
	// plumbing: set_x_y ignores writes with no grid or outside the map (OOB guard, issue #12);
	//   asserted SmoothCoverage.set_x_y_rejects_out_of_bounds_writes
	if ( (x < 0) || (y < 0) )
		return;

	// plumbing: set_x_y ignores writes with no grid or outside the map (OOB guard, issue #12);
	//   asserted SmoothCoverage.set_x_y_rejects_out_of_bounds_writes
	if ( (x >= maxx) || (y >= maxy) )
		return;

	mygrid_span_[static_cast<std::size_t>(x+y*maxx)] = static_cast<unsigned char>(whatvalue);
}

// Grid/smoother sync invariant (issue #12). mygrid_span_ is a NON-OWNING view
// of a PixieData's buffer: whoever frees or replaces that buffer must call
// set_target() (or reset()) or the smoother is left reading -- and, through
// set_x_y(), WRITING -- memory the grid no longer owns. That was the 2013
// level-editor crash: "make the level 30x30" replaced the grid, the terrain
// brush kept smoothing through the old pointer, and the bytes landed in the
// walkers allocated afterwards.
bool smoother::has_target() const noexcept
{
	return !mygrid_span_.empty();
}

bool smoother::targets(const PixieData& data) const noexcept
{
	return data.valid()
	    && mygrid_span_.data() == data.data.get()
	    && mygrid_span_.size() == static_cast<std::size_t>(data.w) * data.h
	    && maxx == data.w
	    && maxy == data.h;
}

// The second half of that invariant: rng_ is borrowed from the GameWorld that
// owns the smoother (GameWorld's constructor for floor 0, set_floor_count for
// the stacked floors). Moving a floor between worlds therefore has to rebind
// it -- LevelRuntimeData::load builds the level in a stack-local GameWorld and
// moves the floors out of it, and a floor smoother left pointing at that dead
// world's rng_ crashes on the first grass/carpet stroke, exactly the way the
// stale grid pointer did in 2013.
bool smoother::uses_rng(const IRandom* rng) const noexcept
{
	return rng_ == rng;
}
