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
//radar.cpp

/* ChangeLog
	buffers: 7/31/02: *include cleanup
*/
#include <openglad/core/colors.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <span>
#include <algorithm>
#include <openglad/interface/game_context.h>
static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

namespace
{
template <typename WalkerList>
bool contains_walker_ptr(const WalkerList& list, const walker* candidate)
{
    return std::any_of(list.begin(), list.end(),
                       [candidate](const auto& entry) {
                           return entry.get() == candidate;
                       });
}

bool control_pointer_is_live(LevelRuntimeData& level, const walker* candidate)
{
    if (candidate == nullptr)
        return false;

    return contains_walker_ptr(level.world().oblist, candidate)
        || contains_walker_ptr(level.world().fxlist, candidate)
        || contains_walker_ptr(level.world().weaplist, candidate)
        || contains_walker_ptr(level.world().dead_list, candidate);
}

walker* sanitize_radar_control(viewscreen* view, LevelRuntimeData& level)
{
    if (view == nullptr)
        return nullptr;

    walker* candidate = view->control;
    if (candidate != nullptr && !control_pointer_is_live(level, candidate))
        view->control = nullptr;
    return view->control;
}

// Which floor's terrain (and blips) the radar should show: the level
// editor's floor override when active, else the control walker's floor.
// Single-floor levels always report 0, so the legacy radar path is
// untouched (pixel-identical).
short radar_terrain_floor(viewscreen* view, walker* control,
                          LevelRuntimeData& level)
{
    const int floors = level.world().floor_count();
    if (floors <= 1)
        return 0;
    int f = 0;
    if (view != nullptr && view->editor_floor_override_ >= 0)
        f = static_cast<int>(view->editor_floor_override_);
    else if (control != nullptr)
        f = static_cast<int>(control->floor());
    if (f < 0)
        f = 0;
    if (f >= floors)
        f = floors - 1;
    return static_cast<short>(f);
}
} // namespace

inline constexpr int RADAR_X = 60;  // These are the dimensions of the radar
inline constexpr int RADAR_Y = 44;  // viewport

// ************************************************************
//  RADAR -- It's nothing like pixie, it just looks like it
// ************************************************************
/*
  radar(char,short,short,screen)    - initializes the radar data (pix = char)
  short draw()
  short on_screen()
*/


// Radar -- this initializes the graphics data for the radar,
// as well as its graphics x and y size.  In addition, it informs
// the radar of the screen object it is linked to.
radar::radar(viewscreen * myview, screen * screen_ctx, short whatnum)
{
	screenp = screen_ctx;
	viewscreenp = myview;
	mynum = whatnum; //what number viewscreen we are, to get control's position
    force_lower_position = false;
}

void radar::start()
{
    start(&og::runtime::current_session->myscreen_->level_runtime_data());
}

void radar::start(LevelRuntimeData* data)
{
	sync_to_grid(data);
	update(data);

}

// Derive the radar extents, view clamps, on-screen placement, and bmp
// allocation from the live grid. start() runs this unconditionally; update()
// re-runs it whenever the recorded dimensions disagree with the grid (the
// level editor resizes the grid out from under the radar, and the editor's
// radar can receive update() before any start()), so the bmp writes in
// update() always match the allocation.
void radar::sync_to_grid(LevelRuntimeData* data)
{
	sizex = static_cast<unsigned short>(data->world().grid.w);
	sizey = static_cast<unsigned short>(data->world().grid.h);
	size = static_cast<unsigned short>((static_cast<unsigned short>(sizex))*(static_cast<unsigned short>(sizey)));
	xview = RADAR_X;
	yview = RADAR_Y;
	radarx = 0;
	radary = 0;

	if (xview > sizex)
		xview = sizex;
	if (yview > sizey)
		yview = sizey;

    if(viewscreenp)
    {
        #ifdef USE_TOUCH_INPUT
        if(force_lower_position)  // used by level editor to place minimap
        {
            // At bottom
            #ifdef REDUCE_OVERSCAN
            xloc = static_cast<short>( ((viewscreenp->endx - xview) - 8) );
            yloc = static_cast<short>( ((viewscreenp->endy - yview) - 8) );
            #else
            xloc = static_cast<short>( ((viewscreenp->endx - xview) - 4) );
            yloc = static_cast<short>( ((viewscreenp->endy - yview) - 4) );
            #endif
        }
        else
        {
            // At top
            #ifdef REDUCE_OVERSCAN
            xloc = static_cast<short>( ((viewscreenp->endx - xview) - 8) );
            yloc = static_cast<short>(viewscreenp->yloc + 8);
            #else
            xloc = static_cast<short>( ((viewscreenp->endx - xview) - 4) );
            yloc = static_cast<short>(viewscreenp->yloc + 4);
            #endif
        }
        #else
            // At bottom
            #ifdef REDUCE_OVERSCAN
            xloc = static_cast<short>( ((viewscreenp->endx - xview) - 8) );
            yloc = static_cast<short>( ((viewscreenp->endy - yview) - 8) );
            #else
            xloc = static_cast<short>( ((viewscreenp->endx - xview) - 4) );
            yloc = static_cast<short>( ((viewscreenp->endy - yview) - 4) );
            #endif
        #endif
    }
    bmp.resize(size);
}


// Destruct the radar and its variables
radar::~radar() = default;

short radar::draw()
{
    return draw(&og::runtime::current_session->myscreen_->level_runtime_data());
}

short radar::draw(LevelRuntimeData* data)
{
	Sint32 tempx, tempy, tempz;
	unsigned char tempcolor;
	Order oborder{};
	short obfamily, obteam;
	short can_see = 0, do_show = 0;
	Sint32 listtype = 0;

	radarx = 0;
	radary = 0;

	if (viewscreenp && !viewscreenp->radarstart)
	{
		start(data);
		viewscreenp->radarstart = 1;
	}

    walker* control = sanitize_radar_control(viewscreenp, *data);
	if (control)
	{
		radarx = static_cast<short>(control->xpos()/GRID_SIZE - xview/2);
		radary = static_cast<short>(control->ypos()/GRID_SIZE - yview/2);
		if (control->view_all() > 0)
			can_see = 1;
		obteam = control->team_num();
	}
	else
	{
		radarx = static_cast<short>(data->level_visuals().topx / GRID_SIZE - xview / 2);
		radary = static_cast<short>(data->level_visuals().topy / GRID_SIZE - yview / 2);
		obteam = 0;
	}
	if (radarx > (sizex - xview))
		radarx = static_cast<short>(sizex - xview);
	if (radary > (sizey - yview))
		radary = static_cast<short>(sizey - yview);
	if (radarx < 0)
		radarx = 0;
	if (radary < 0)
		radary = 0;

	// Multi-floor: the minimap tracks the floor being played (or edited).
	// Re-bake the terrain bmp only when that floor changes — update() is
	// slow — and never on single-floor levels (radar_terrain_floor pins 0).
	const short terrain_floor = radar_terrain_floor(viewscreenp, control, *data);
	if (terrain_floor != bmp_floor_)
	{
		bmp_floor_ = terrain_floor;
		update(data);
	}
    
    unsigned char alpha = 255;
    if(og::runtime::current_session->myscreen_->numviews > 2 && !(og::runtime::current_session->myscreen_->numviews == 3 && mynum == 0))
    {
        alpha = 127;
    }
	{
		size_t offset = radarx + (radary * sizex);
		auto radar_span = std::span<const unsigned char>{bmp.data() + offset, bmp.size() - offset};
		og::runtime::current_session->myscreen_->putbuffer_alpha(xloc, yloc,
		                   sizex,sizey,
		                   xloc,yloc,xloc + xview,yloc + yview,
		                   radar_span, alpha);
	}

	// Now determine what objects are visible on the radar ..
	while (listtype <= 1)
	{
			const GameWorld::EntityList* ls;
			if (listtype == 0) // do oblist, standard
			{
				ls = &data->world().oblist;
				listtype++;
			}
			else if (listtype == 1) // do weapons
			{
				ls = &data->world().weaplist;
				listtype++;
			}
		else
			continue;

        for(auto e = ls->begin(); e != ls->end(); e++)
		{
		    walker* ob = e->get();
		    // Delayed spawns: dormant walkers are not in the world yet.
		    if (ob && ob->dormant())
		        continue;
		    // Multi-floor: only blip entities on the floor the radar shows
		    // (the terrain floor baked just above).
		    if (ob && data->world().floor_count() > 1 &&
		        static_cast<int>(ob->floor()) != static_cast<int>(bmp_floor_))
		        continue;
            oborder = ob->query_order();
			do_show = 0; // don't show, by default
			if ((oborder == Order::Living || oborder == Order::Weapon
			            || (oborder == Order::Treasure && (ob->family() == FAMILY_LIFE_GEM))
			            || (oborder == Order::Treasure && (ob->family() == FAMILY_EXIT))
			            || (oborder == Order::Generator && can_see)
			           )
			        && (obteam==ob->team_num() || ob->invisibility_left() < 1 || can_see)
			        && on_screen( static_cast<short>((ob->xpos()+1)/GRID_SIZE), static_cast<short>((ob->ypos()+1)/GRID_SIZE), radarx, radary)
			   )
				do_show = 1;
			if (do_show)
			{
				tempx = xloc + ((ob->xpos()+1)/GRID_SIZE - radarx);
				tempy = yloc + ((ob->ypos()+1)/GRID_SIZE - radary);
				if ( (tempx < xloc) || (tempx > (xloc + xview)) )
				{} //do nothing
				else if ( (tempy < yloc) || (tempy > (yloc + yview)) )
				{} //also do nothing
				else
				{
					tempz = (tempx+(tempy*320)); //this may need fixing
					if (tempz > 64000 || tempz < 0)
					{
						Log("bad radar, bad\n");
						return 1;
					}
					tempcolor = (ob->query_team_color());
					if (viewscreenp && viewscreenp->control == ob)
					{
						tempcolor = static_cast<unsigned char>(rng(256));
						if (tempx >= (xloc + xview - 1) && tempy < (yloc+yview) )
						{
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy+1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy+1,tempcolor, alpha);

						}
						else if (tempx >= (xloc + xview -1) )
						{
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy-1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx-1,tempy-1,tempcolor, alpha);

						}
						else if (tempy >= (yloc + yview -1) && tempx < (xloc+xview) )
						{
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy-1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy-1,tempcolor, alpha);
						}
						else
						{
							og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx,tempy+1,tempcolor, alpha);
							og::runtime::current_session->myscreen_->pointb(tempx+1,tempy+1,tempcolor, alpha);
						}
					}
					else if (oborder == Order::Living)
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,tempcolor, alpha);
					else if (oborder == Order::Generator)
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,static_cast<char>(tempcolor+1), alpha);
					else if (oborder == Order::Treasure) // currently life gems
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,COLOR_FIRE, alpha);
					else
						og::runtime::current_session->myscreen_->pointb(tempx,tempy,COLOR_WHITE, alpha);
				}//draw the blob onto the radar
			}
		}
	} // go back to new screen lists (weapons, etc.)

    for (auto& uptr : data->world().fxlist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead() && !ob->dormant())
		{
			if (data->world().floor_count() > 1 &&
			    static_cast<int>(ob->floor()) != static_cast<int>(bmp_floor_))
				continue;
			oborder  = ob->query_order();
			obfamily = ob->family();

			do_show = 0; // don't show, by default
			if (oborder == Order::Treasure)
			{
				if (can_see)
				{
					switch (obfamily)
					{
						case FAMILY_GOLD_BAR:
							do_show = static_cast<short>(YELLOW + rng(5));
							break;
						case FAMILY_SILVER_BAR:
							do_show = static_cast<short>(GREY + rng(5));
							break;
						case FAMILY_DRUMSTICK:
							do_show = static_cast<short>(COLOR_BROWN + rng(2));
							break;
						case FAMILY_MAGIC_POTION:
						case FAMILY_INVIS_POTION:
						case FAMILY_INVULNERABLE_POTION:
						case FAMILY_FLIGHT_POTION:
							do_show = static_cast<short>(COLOR_BLUE + rng(5));
							break;
						default:
							do_show = 0;
							break;
					}
					}
					if (obfamily == FAMILY_EXIT || obfamily == FAMILY_TELEPORTER)
						do_show = static_cast<short>(static_cast<Uint32>(LIGHT_BLUE) + rng(7));
					// CTF flags and control points always flicker in their
					// team's (owner's) color ramp, like exits.
					if (obfamily == og::FAMILY_FLAG || obfamily == og::FAMILY_CTF_POINT)
						do_show = static_cast<short>(static_cast<Uint32>(ob->query_team_color()) + rng(7));
				}
				if (!on_screen( static_cast<short>((ob->xpos()+1)/GRID_SIZE),
				                static_cast<short>((ob->ypos()+1)/GRID_SIZE),
				                radarx, radary) )
				do_show = 0;
			if (do_show)
			{
				tempx = xloc + ((ob->xpos()+1)/GRID_SIZE - radarx);
				tempy = yloc + ((ob->ypos()+1)/GRID_SIZE - radary);
				if ( (tempx < xloc) || (tempx > (xloc + xview)) )
				{} //do nothing
				else if ( (tempy < yloc) || (tempy > (yloc + yview)) )
				{} //also do nothing
				else
				{
					tempz = (tempx+(tempy*320)); //this may need fixing
					if (tempz > 64000 || tempz < 0)
					{
						Log("bad radar, bad\n");
						return 1;
					}
					og::runtime::current_session->myscreen_->pointb(tempx,tempy,static_cast<char>(do_show), alpha);
				}//draw the blob onto the radar
			} // end of valid do_show
		}  // end of if here->ob
	} // end of while (here)

	return 1;
}

//short radar::refresh()
//{
// The first two values are screwy... I don't know why
// myscreen->buffer_to_screen(xloc, yloc, xview, yview);
// return 1;
//}
// In theory the above function will NOT be required

short radar::on_screen(short whatx, short whaty,
                       short hor, short ver)
{
	// Return 0 if off radar.
	// These measurements are grid coords, not pixels.
	if (whatx < hor || whatx >= (hor+xview) ||
	        whaty < ver || whaty >= (ver+yview) )
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

// This function re-initializes the radar map data.  Do not
// call it often, as it is very slow ..
void radar::update()
{
    update(&og::runtime::current_session->myscreen_->level_runtime_data());
}

// Radar color for a decor id, or `base` (the cell's base-tile color) for ids
// that inherit (DECOR_NONE, BONES, out-of-range bytes). The overrides
// reproduce the legacy combined tiles' radar colors exactly: TORCH*/BRAZIER
// -> COLOR_FIRE, BOULDER_* (and COLUMN_*) -> 24 (wall grey), PEBBLES -> the
// legacy randomized-green rubble arm (the GRASS_DARK_1 base alone would give
// the fixed GREEN+3), SHRUB -> the trees green.
static short decor_radar_color(unsigned char d, short base)
{
	switch (d)
	{
		case DECOR_TORCH1:
		case DECOR_TORCH2:
		case DECOR_TORCH3:
		case DECOR_BRAZIER:
			return COLOR_FIRE;
		case DECOR_BOULDER_1:
		case DECOR_BOULDER_2:
		case DECOR_BOULDER_3:
		case DECOR_BOULDER_4:
		case DECOR_COLUMN_BOTTOM:
		case DECOR_COLUMN_TOP:
			return 24;
		case DECOR_PEBBLES:
			return static_cast<short>(COLOR_GREEN + rng(3) + 3);
		case DECOR_SHRUB:
			return COLOR_TREES;
		default:
			return base;
	}
}

// This function re-initializes the radar map data.  Do not
// call it often, as it is very slow ..
void radar::update(LevelRuntimeData* data)
{
	short temp, i, j;

	// The grid can change size between syncs (editor resize / clear paths
	// call update() directly, and an editor radar may never see start());
	// writing bmp[i+sizex*j] against stale extents is a heap overflow, so
	// re-derive everything whenever the recorded extents disagree.
	if (sizex != static_cast<short>(data->world().grid.w)
	    || sizey != static_cast<short>(data->world().grid.h)
	    || bmp.size() != static_cast<std::size_t>(size))
	{
		sync_to_grid(data);
	}

	// Multi-floor: bake the terrain of the floor the radar shows. Fall back
	// to the base grid when that floor's grid is missing or of a different
	// size (extents above were derived from the base grid). bmp_floor_ never
	// leaves 0 on single-floor levels, so this reads the legacy grid there.
	const PixieData& floor_grid = data->world().grid_for_floor(bmp_floor_);
	const bool use_floor_grid = floor_grid.valid()
	    && static_cast<short>(floor_grid.w) == sizex
	    && static_cast<short>(floor_grid.h) == sizey;
	const PixieData& baked_grid =
	    use_floor_grid ? floor_grid : data->world().grid;

	// Decor plane of the same floor (BASE+DECOR layering): where a cell
	// carries decor with a defined radar color, the decor wins over the base
	// tile color (see decor_radar_color). Gated on validity + matching dims,
	// so no-decor levels bake through exactly the legacy switch below.
	const PixieData& decor_plane = data->world().decor_for_floor(bmp_floor_);
	const bool has_decor = decor_plane.valid()
	    && static_cast<short>(decor_plane.w) == sizex
	    && static_cast<short>(decor_plane.h) == sizey;

	for (i = 0; i < sizex; i++)
		for (j = 0; j < sizey; j++)
		{
			// Check if item in background grid
			switch (static_cast<unsigned char>(baked_grid.data[i+sizex*j]))
			{
				case PIX_GRASS1:  // grass is green
				case PIX_GRASS_DARK_1:
				case PIX_GRASS_DARK_B1:
				case PIX_GRASS_DARK_BR:
					temp = COLOR_GREEN+3;
					break;
				case PIX_GRASS2:
				case PIX_GRASS_DARK_2:
				case PIX_GRASS_DARK_B2:
				case PIX_WALL_ARROW_GRASS:
					temp = COLOR_GREEN+4;
					break;
				case PIX_GRASS3:
				case PIX_GRASS_DARK_3:
				case PIX_GRASS_DARK_R1:
				case PIX_WALL_ARROW_GRASS_DARK:
					temp = COLOR_GREEN+5;
					break;
				case PIX_GRASS4:
				case PIX_GRASS_DARK_4:
				case PIX_GRASS_DARK_R2:
					temp = COLOR_GREEN+5;
					break;
				case PIX_GRASS_DARK_LL:
				case PIX_GRASS_DARK_UR:
				case PIX_GRASS_RUBBLE:
				case PIX_GRASS_LIGHT_1: // lighter grass
				case PIX_GRASS_LIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT:
				case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT:
				case PIX_GRASS_LIGHT_LEFT_TOP:
					temp = static_cast<short>(COLOR_GREEN + rng(3) + 3);
					break;
				case PIX_TREE_M1: // Trees are green
				case PIX_TREE_ML:
				case PIX_TREE_T1:
				case PIX_TREE_MR:
				case PIX_TREE_MT:
					temp = static_cast<short>(COLOR_TREES + rng(3));
					break;
				case PIX_TREE_B1: // Trunks are brown
					temp = COLOR_BROWN + 6;
					break;
				case PIX_PAVEMENT1:   // pavement dark grey
				case PIX_PAVEMENT2:
				case PIX_PAVEMENT3:
				case PIX_PAVESTEPS1:
				case PIX_PAVESTEPS2:
				case PIX_PAVESTEPS2L:
				case PIX_PAVESTEPS2R:
				case PIX_COBBLE_1:
				case PIX_COBBLE_2:
				case PIX_COBBLE_3:
				case PIX_COBBLE_4:
					temp = 17;
					break;
				case PIX_FLOOR_PAVEL: // wood is brown
				case PIX_FLOOR_PAVER:
				case PIX_FLOOR_PAVEU:
				case PIX_FLOOR_PAVED:
				case PIX_FLOOR1:
				case PIX_WALL_ARROW_FLOOR:
					temp = COLOR_BROWN+4;
					break;
				case PIX_DIRT_1: // path is brown
				case PIX_DIRTGRASS_UL1:
				case PIX_DIRTGRASS_UR1:
				case PIX_DIRTGRASS_LL1:
				case PIX_DIRTGRASS_LR1:
				case PIX_DIRT_DARK_1:
				case PIX_DIRTGRASS_DARK_UL1:
				case PIX_DIRTGRASS_DARK_UR1:
				case PIX_DIRTGRASS_DARK_LL1:
				case PIX_DIRTGRASS_DARK_LR1:
					temp = COLOR_BROWN+5;
					break;
				case PIX_JAGGED_GROUND_1:
				case PIX_JAGGED_GROUND_2:
				case PIX_JAGGED_GROUND_3:
				case PIX_JAGGED_GROUND_4:
					temp = COLOR_BROWN+5;
					break;
				case PIX_CLIFF_BOTTOM:  // slightly darker
				case PIX_CLIFF_TOP:
				case PIX_CLIFF_LEFT:
				case PIX_CLIFF_RIGHT:
				case PIX_CLIFF_BACK_1:
				case PIX_CLIFF_BACK_2:
				case PIX_CLIFF_BACK_L:
				case PIX_CLIFF_BACK_R:
				case PIX_CLIFF_TOP_L:
				case PIX_CLIFF_TOP_R:
					temp = COLOR_BROWN+6;
					break;
				case PIX_CARPET_LL:   // carpet is purple
				case PIX_CARPET_B:
				case PIX_CARPET_LR:
				case PIX_CARPET_UR:
				case PIX_CARPET_U:
				case PIX_CARPET_UL:
				case PIX_CARPET_L:
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
					temp = COLOR_PURPLE+4;
					break;
				case PIX_H_WALL1: // walls are light grey
				case PIX_WALL2:
				case PIX_WALL3:
				case PIX_WALL_LL:
				case PIX_WALLTOP_H:
				case PIX_WALL4:
				case PIX_WALL5:
				case PIX_BOULDER_1:
				case PIX_BOULDER_2:
				case PIX_BOULDER_3:
				case PIX_BOULDER_4:
				case PIX_PATH_1:      // sparser cobblestone/grass
				case PIX_PATH_2:
				case PIX_PATH_3:
				case PIX_PATH_4:
					temp = 24;
					break;
				case PIX_WATER1:      // Water is dark blue
				case PIX_WATER2:
				case PIX_WATER3:
				case PIX_WATERGRASS_LL:
				case PIX_WATERGRASS_LR:
				case PIX_WATERGRASS_UL:
				case PIX_WATERGRASS_UR:
				case PIX_GRASSWATER_LL:
				case PIX_GRASSWATER_LR:
				case PIX_GRASSWATER_UL:
				case PIX_GRASSWATER_UR:
					temp = COLOR_BLUE+2;
					break;

				case PIX_WALLSIDE_L:  // White, maybe?
				case PIX_WALLSIDE1:
				case PIX_WALLSIDE_R:
				case PIX_WALLSIDE_C:
				case PIX_WALLSIDE_CRACK_C1:
					temp = COLOR_WHITE-1;
					break;

				case PIX_TORCH1:
				case PIX_TORCH2:
				case PIX_TORCH3:
				case PIX_BRAZIER1:
					temp = COLOR_FIRE;
					break;

				case PIX_SNOW1:       // snow is white
				case PIX_SNOW2:
					temp = COLOR_WHITE;
					break;
				case PIX_LAVA1:       // lava: static ember orange. COLOR_FIRE
				case PIX_LAVA2:       // (224) sits in the cycled ORANGE band
					temp = 233;       // 224-231 and strobes every frame on
					break;            // large lava fields; 233 never cycles.
				case PIX_MARSH1:      // bog is dark green (distinct from the trees ramp)
				case PIX_MARSH2:
					temp = COLOR_GREEN+7;
					break;
				case PIX_ASH1:        // ash is warm dark grey (vs pavement 17 / walls 24)
				case PIX_ASH2:
					temp = 249;
					break;

				default:
					temp =  0;
			}
			if (has_decor)
				temp = decor_radar_color(
				    static_cast<unsigned char>(decor_plane.data[i + sizex * j]),
				    temp);
			bmp[i+sizex*j] = static_cast<unsigned char>(temp);
		}

}
