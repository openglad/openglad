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
//graphlib.cpp

/* ChangeLog
	buffers: 7/31/02: *include cleanup
	buffers: 8/15/02: *rewrote the file finding/loading code in read_pixie_file
*/
#include <openglad/interface/base.h> // PixieData + PIX_* indices + read_pixie_file decls
#include <string>
#include <memory>
#include <openglad/core/util.h>

// Use this for globally setting the graphics dir, etc..
//char pix_directory[80];


// ************************************************************
//  Other graphics routines
// ************************************************************

//have been moved to video


// ************* Editor related functions? ************************
// have also been moved to video


// read_pixie_file is now in src/io/og_file.cpp (SDL-free implementation)


// Synthesize the PIX_GLASS tile in code (no PNG asset): a 16x16 "glass pane" —
// a faint cyan-blue body, a bright cyan frame (the glazing bar / pane edge), and
// a double diagonal near-white sheen (the specular glint). The renderer always
// draws PIX_GLASS through the alpha path at kGlassAlpha (view.cpp), so the whole
// tile is see-through: the body adds a cool tint over the floor below while the
// frame + sheen read as a glazed pane instead of a dim stone floor. PIX_GLASS
// only ever appears on multi-floor maps, so this never affects single-floor
// (parity) rendering. Palette indices (6-bit*4 -> 8-bit RGB). ALL three MUST
// stay OUTSIDE do_cycle's per-frame ranges WATER 208..223 + ORANGE 224..231
// (base.h), or the tile re-tints every cycle tick and flashes like water:
//   BODY  116 -> (96,180,180) muted cyan-blue  FRAME 112 -> (64,228,228) cyan
//   SHEEN  15 -> (228,228,228) near-white
static PixieData make_glass_tile()
{
	constexpr int W = 16, H = 16;
	constexpr unsigned char BODY  = 116; // muted cyan-blue; NOT 208 (==WATER_START, do_cycle rotates -> flashing)
	constexpr unsigned char FRAME = 112;
	constexpr unsigned char SHEEN = 15;
	PixieData glass;
	glass.frames = 1;
	glass.w = static_cast<unsigned char>(W);
	glass.h = static_cast<unsigned char>(H);
	glass.data = std::make_unique<unsigned char[]>(W * H);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			unsigned char c = BODY;
			if (x == 0 || y == 0 || x == W - 1 || y == H - 1)
				c = FRAME;                                   // 1px pane frame
			else if ((x + y) == 5 || (x + y) == 6 || (x + y) == 9)
				c = SHEEN;                                   // double diagonal glint
			glass.data[y * W + x] = c;
		}
	return glass;
}


void load_map_data(PixieData* whereto)
{
	// load the pixie graphics data into memory
	whereto[0] = read_pixie_file("16tile.png");             //done
	whereto[PIX_GRASS1] = read_pixie_file("16grass1.png");  //done
	whereto[PIX_WATER1] = read_pixie_file("16water1.png");  //done
	whereto[3] = read_pixie_file("16space.png");            //done
	whereto[4] = read_pixie_file("16wall2.png");            //done
	whereto[5] = read_pixie_file("16wall3.png");            //done
	whereto[6] = read_pixie_file("16floor.png");            //done
	whereto[7] = read_pixie_file("16walllo.png");           //done
	whereto[8] = read_pixie_file("16w2lo.png");            //done wall2lo
	// return; // works up to here ..

	whereto[9] = read_pixie_file("16carpll.png");          //done thru
	whereto[11] = read_pixie_file("16carpb.png");
	whereto[12] = read_pixie_file("16carplr.png");
	whereto[13] = read_pixie_file("16carpur.png");
	whereto[14] = read_pixie_file("16carpu.png");
	whereto[15] = read_pixie_file("16carpul.png");
	whereto[PIX_CARPET_L] = read_pixie_file("16carpl.png");
	whereto[PIX_CARPET_M] = read_pixie_file("16carpm.png");
	whereto[PIX_CARPET_M2] = read_pixie_file("16carpm2.png");
	whereto[PIX_CARPET_R] = read_pixie_file("16carpr.png");    // here

	whereto[PIX_CARPET_SMALL_HOR] = read_pixie_file("16cshor.png");    // here
	whereto[PIX_CARPET_SMALL_VER] = read_pixie_file("16csver.png");    // here
	whereto[PIX_CARPET_SMALL_CUP] = read_pixie_file("16cscup.png");    // here
	whereto[PIX_CARPET_SMALL_CAP] = read_pixie_file("16cscap.png");    // here
	whereto[PIX_CARPET_SMALL_LEFT] = read_pixie_file("16csleft.png");    // here
	whereto[PIX_CARPET_SMALL_RIGHT] = read_pixie_file("16csrigh.png");    // here
	whereto[PIX_CARPET_SMALL_TINY] = read_pixie_file("16cstiny.png");    // here

	whereto[PIX_GRASS2] = read_pixie_file("16grass2.png");  //done
	whereto[PIX_GRASS3] = read_pixie_file("16grass3.png");  //done
	whereto[PIX_GRASS4] = read_pixie_file("16grass4.png");  //done

	whereto[PIX_GRASS_DARK_1] = read_pixie_file("16grassd.png");  //done
	whereto[PIX_GRASS_DARK_2] = read_pixie_file("16grd2.png");  //done
	whereto[PIX_GRASS_DARK_3] = read_pixie_file("16grd3.png");  //done
	whereto[PIX_GRASS_DARK_4] = read_pixie_file("16grd4.png");  //done
	whereto[PIX_GRASS_DARK_LL] = read_pixie_file("16grassi.png");  //done
	whereto[PIX_GRASS_DARK_UR] = read_pixie_file("16grassh.png");  //done
	whereto[PIX_GRASS_RUBBLE] = read_pixie_file("16grassr.png");  //done

	whereto[PIX_GRASS_DARK_B1] = read_pixie_file("16grdb1.png");  //done
	whereto[PIX_GRASS_DARK_B2] = read_pixie_file("16grdb2.png");  //done
	whereto[PIX_GRASS_DARK_R1] = read_pixie_file("16grdr1.png");  //done
	whereto[PIX_GRASS_DARK_R2] = read_pixie_file("16grdr2.png");  //done
	whereto[PIX_GRASS_DARK_BR] = read_pixie_file("16grdbr.png");  //done

	whereto[PIX_GRASS_LIGHT_1] = read_pixie_file("16grl1.png");  //done
	whereto[PIX_GRASS_LIGHT_TOP] = read_pixie_file("16grlt.png");  //done
	whereto[PIX_GRASS_LIGHT_RIGHT_TOP] = read_pixie_file("16grlrt.png");  //done
	whereto[PIX_GRASS_LIGHT_RIGHT] = read_pixie_file("16grlr.png");  //done
	whereto[PIX_GRASS_LIGHT_RIGHT_BOTTOM] = read_pixie_file("16grlrb.png");  //done
	whereto[PIX_GRASS_LIGHT_BOTTOM] = read_pixie_file("16grlb.png");  //done
	whereto[PIX_GRASS_LIGHT_LEFT_BOTTOM] = read_pixie_file("16grllb.png");  //done
	whereto[PIX_GRASS_LIGHT_LEFT] = read_pixie_file("16grll.png");  //done
	whereto[PIX_GRASS_LIGHT_LEFT_TOP] = read_pixie_file("16grllt.png");  //done

	whereto[PIX_WATER2] = read_pixie_file("16water2.png");  //done
	whereto[PIX_WATER3] = read_pixie_file("16water3.png");  //done

	whereto[PIX_WATERGRASS_LL] = read_pixie_file("16wgll.png"); //done thru
	whereto[PIX_WATERGRASS_LR] = read_pixie_file("16wglr.png");
	whereto[PIX_WATERGRASS_UL] = read_pixie_file("16wgul.png");
	whereto[PIX_WATERGRASS_UR] = read_pixie_file("16wgur.png");
	whereto[PIX_WATERGRASS_U] = read_pixie_file("16wgu.png");
	whereto[PIX_WATERGRASS_D] = read_pixie_file("16wgd.png");
	whereto[PIX_WATERGRASS_L] = read_pixie_file("16wgl.png");
	whereto[PIX_WATERGRASS_R] = read_pixie_file("16wgr.png");
	whereto[PIX_GRASSWATER_LL] = read_pixie_file("16gwll.png");
	whereto[PIX_GRASSWATER_LR] = read_pixie_file("16gwlr.png");
	whereto[PIX_GRASSWATER_UL] = read_pixie_file("16gwul.png");
	whereto[PIX_GRASSWATER_UR] = read_pixie_file("16gwur.png"); // here ..done

	whereto[PIX_PAVEMENT1] = read_pixie_file("16pave1.png"); // pavement done
	whereto[PIX_PAVEMENT2] = read_pixie_file("16pave2.png"); //done
	whereto[PIX_PAVEMENT3] = read_pixie_file("16pave3.png"); //done
	whereto[PIX_PAVESTEPS1] = read_pixie_file("16pstep.png");   // pavestep done
	whereto[PIX_PAVESTEPS2] = read_pixie_file("16ptest.png");   //done
	whereto[PIX_PAVESTEPS2L] = read_pixie_file("16ptestl.png");
	whereto[PIX_PAVESTEPS2R] = read_pixie_file("16ptestr.png"); //done

	whereto[PIX_WALLSIDE1] = read_pixie_file("16brick1.png");  // 'ELL' //done
	whereto[PIX_WALLSIDE_L] = read_pixie_file("16brickl.png"); // 'ONE' //done
	whereto[PIX_WALLSIDE_R] = read_pixie_file("16brickr.png"); //done
	whereto[PIX_WALLSIDE_C] = read_pixie_file("16brickc.png"); //done
	whereto[PIX_WALLSIDE_CRACK_C1] = read_pixie_file("16brick3.png"); //done

	whereto[PIX_WALL_LL] = read_pixie_file("16wallll.png");  //done

	whereto[PIX_BRAZIER1] = read_pixie_file("16braz1.png"); //brazier1

	whereto[PIX_WALLTOP_H] = read_pixie_file("16ttop.png"); //tiletop

	whereto[PIX_TORCH1] = read_pixie_file("16torch1.png");  //done
	whereto[PIX_TORCH2] = read_pixie_file("16torch2.png");  //done
	whereto[PIX_TORCH3] = read_pixie_file("16torch3.png");  //done

	whereto[PIX_FLOOR_PAVEL] = read_pixie_file("16fpl.png"); //done flrpavel
	whereto[PIX_FLOOR_PAVER] = read_pixie_file("16fpr.png"); //done flrpaver
	whereto[PIX_FLOOR_PAVEU] = read_pixie_file("16fpu.png"); //done flrpaveu
	whereto[PIX_FLOOR_PAVED] = read_pixie_file("16fpd.png"); //done flrpaved
	//return; // this is probably buggged if we load more ..

	whereto[PIX_COLUMN1] = read_pixie_file("16colm0.png"); //done column0
	whereto[PIX_COLUMN2] = read_pixie_file("16colm1.png"); //done column1

	// Tree stuff ..
	whereto[PIX_TREE_B1] = read_pixie_file("16treeb1.png"); //done tree_b1

	whereto[PIX_TREE_M1] = read_pixie_file("16treem1.png"); //done tree_m1
	whereto[PIX_TREE_ML] = read_pixie_file("16treeml.png"); //done tree_mL
	whereto[PIX_TREE_MR] = read_pixie_file("16treemr.png"); //done tree_mR
	whereto[PIX_TREE_MT] = read_pixie_file("16treemt.png"); //done tree_mT (thin)

	whereto[PIX_TREE_T1] = read_pixie_file("16treet1.png"); //done tree_t2

	whereto[PIX_DIRT_1] = read_pixie_file("16dirt2.png");
	whereto[PIX_DIRTGRASS_UL1] = read_pixie_file("16dgul1.png");
	whereto[PIX_DIRTGRASS_UR1] = read_pixie_file("16dgur1.png");
	whereto[PIX_DIRTGRASS_LL1] = read_pixie_file("16dgll1.png");
	whereto[PIX_DIRTGRASS_LR1] = read_pixie_file("16dglr1.png");

	whereto[PIX_DIRT_DARK_1] = read_pixie_file("16dirtd1.png");
	whereto[PIX_DIRTGRASS_DARK_UL1] = read_pixie_file("16dguld.png");
	whereto[PIX_DIRTGRASS_DARK_UR1] = read_pixie_file("16dgurd.png");
	whereto[PIX_DIRTGRASS_DARK_LL1] = read_pixie_file("16dglld.png");
	whereto[PIX_DIRTGRASS_DARK_LR1] = read_pixie_file("16dglrd.png");

	whereto[PIX_PATH_1] = read_pixie_file("16path1.png");
	whereto[PIX_PATH_2] = read_pixie_file("16path2.png");
	whereto[PIX_PATH_3] = read_pixie_file("16path3.png");
	whereto[PIX_PATH_4] = read_pixie_file("16path4.png");

	whereto[PIX_BOULDER_1] = read_pixie_file("16stone1.png");
	whereto[PIX_BOULDER_2] = read_pixie_file("16stone2.png");
	whereto[PIX_BOULDER_3] = read_pixie_file("16stone3.png");
	whereto[PIX_BOULDER_4] = read_pixie_file("16stone4.png");

	whereto[PIX_COBBLE_1] = read_pixie_file("16cob1.png");
	whereto[PIX_COBBLE_2] = read_pixie_file("16cob2.png");
	whereto[PIX_COBBLE_3] = read_pixie_file("16cob3.png");
	whereto[PIX_COBBLE_4] = read_pixie_file("16cob4.png");

	whereto[PIX_WALL_ARROW_GRASS] = read_pixie_file("16wallog.png");
	whereto[PIX_WALL_ARROW_FLOOR] = read_pixie_file("16wallof.png");
	whereto[PIX_WALL_ARROW_GRASS_DARK] = read_pixie_file("16wallod.png");

	// Cliff tiles
	whereto[PIX_CLIFF_BOTTOM] = read_pixie_file("16cliff1.png");
	whereto[PIX_CLIFF_TOP] = read_pixie_file("16cliff2.png");
	whereto[PIX_CLIFF_LEFT] = read_pixie_file("16cliff3.png");
	whereto[PIX_CLIFF_RIGHT] = read_pixie_file("16cliff4.png");
	whereto[PIX_CLIFF_BACK_1] = read_pixie_file("16clifup.png");
	whereto[PIX_CLIFF_BACK_2] = read_pixie_file("16clifu2.png");
	whereto[PIX_CLIFF_BACK_L] = read_pixie_file("16cliful.png");
	whereto[PIX_CLIFF_BACK_R] = read_pixie_file("16clifur.png");
	whereto[PIX_CLIFF_TOP_L] = read_pixie_file("16clifdl.png");
	whereto[PIX_CLIFF_TOP_R] = read_pixie_file("16clifdr.png");

	// Damaged tiles ..
	whereto[PIX_GRASS1_DAMAGED] = read_pixie_file("16grasd1.png");

	// Pete's graphics
	whereto[PIX_JAGGED_GROUND_1] = read_pixie_file("16jwg1.png");
	whereto[PIX_JAGGED_GROUND_2] = read_pixie_file("16jwg2.png");
	whereto[PIX_JAGGED_GROUND_3] = read_pixie_file("16jwg3.png");
	whereto[PIX_JAGGED_GROUND_4] = read_pixie_file("16jwg1.png");

	// Z-axis Z-stairs are SOLID, stood-on floor tiles and need visible art; without
	// an entry here pixdata[140/141] stay empty, draw_tile() blits nothing, and the
	// cleared (black) cell shows through. Reuse the existing pavestep art.
	whereto[PIX_ZSTAIR_UP]   = read_pixie_file("16pstep.png");
	whereto[PIX_ZSTAIR_DOWN] = read_pixie_file("16ptest.png");
	// PIX_GLASS is a SYNTHESIZED glass-pane tile (see make_glass_tile) drawn FAINT
	// through the alpha path (kGlassAlpha in view.cpp) so the floor below still shows
	// through while the tile reads as a glazed pane rather than a dim stone floor or
	// an empty air hole. PIX_AIR stays UNLOADED on purpose (empty = fully see-through;
	// the editor's add_floor() fills whole floors with PIX_AIR).
	whereto[PIX_GLASS]       = make_glass_tile();

}
