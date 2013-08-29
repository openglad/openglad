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
#include "graph.h"
#include "colors.h"

#define RADAR_X 60  // These are the dimensions of the radar
#define RADAR_Y 44  // viewport

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
radar::radar(viewscreen * myview, screen * myscreen, short whatnum)
{
	screenp = myscreen;
	viewscreenp = myview;
	mynum = whatnum; //what number viewscreen we are, to get control's position
	bmp = NULL;
    force_lower_position = false;
}

void radar::start()
{
    start(&screenp->level_data);
}

void radar::start(LevelData* data)
{
	sizex = (unsigned short) data->grid.w;
	sizey = (unsigned short) data->grid.h;
	size = (unsigned short) (((unsigned short) sizex)*((unsigned short) sizey));
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
            xloc = (short) ( ((viewscreenp->endx - xview) - 8) );
            yloc = (short) ( ((viewscreenp->endy - yview) - 8) );
            #else
            xloc = (short) ( ((viewscreenp->endx - xview) - 4) );
            yloc = (short) ( ((viewscreenp->endy - yview) - 4) );
            #endif
        }
        else
        {
            // At top
            #ifdef REDUCE_OVERSCAN
            xloc = (short) ( ((viewscreenp->endx - xview) - 8) );
            yloc = (short) (viewscreenp->yloc + 8);
            #else
            xloc = (short) ( ((viewscreenp->endx - xview) - 4) );
            yloc = (short) (viewscreenp->yloc + 4);
            #endif
        }
        #else
            // At bottom
            #ifdef REDUCE_OVERSCAN
            xloc = (short) ( ((viewscreenp->endx - xview) - 8) );
            yloc = (short) ( ((viewscreenp->endy - yview) - 8) );
            #else
            xloc = (short) ( ((viewscreenp->endx - xview) - 4) );
            yloc = (short) ( ((viewscreenp->endy - yview) - 4) );
            #endif
        #endif
    }
    if(bmp != NULL)
        delete[] bmp;
    bmp = new unsigned char[size];
	update(data);

}


// Destruct the radar and its variables
radar::~radar()
{
	if (bmp)
	{
		delete[] bmp;
		bmp = NULL;
	}
}

short radar::draw()
{
    return draw(&screenp->level_data);
}

short radar::draw(LevelData* data)
{
	oblink  * here;
	Sint32 tempx, tempy, tempz;
	unsigned char tempcolor;
	short oborder, obfamily, obteam;
	short can_see = 0, do_show = 0;
	Sint32 listtype = 0;

	radarx = 0;
	radary = 0;

	if (viewscreenp && !viewscreenp->radarstart)
	{
		start(data);
		viewscreenp->radarstart = 1;
	}

	if (viewscreenp && viewscreenp->control)
	{
		radarx = (short) (viewscreenp->control->xpos/GRID_SIZE - xview/2);
		radary = (short) (viewscreenp->control->ypos/GRID_SIZE - yview/2);
		if (viewscreenp->control->view_all > 0)
			can_see = 1;
		obteam = viewscreenp->control->team_num;
	}
	else
	{
		radarx = (short) (data->topx/GRID_SIZE - xview/2);
		radary = (short) (data->topy/GRID_SIZE - yview/2);
		obteam = 0;
	}
	if (radarx > (sizex - xview))
		radarx = (short) (sizex - xview);
	if (radary > (sizey - yview))
		radary = (short) (sizey - yview);
	if (radarx < 0)
		radarx = 0;
	if (radary < 0)
		radary = 0;
    
    unsigned char alpha = 255;
    if(myscreen->numviews > 2 && !(myscreen->numviews == 3 && mynum == 0))
        alpha = 127;
	screenp->putbuffer_alpha(xloc, yloc,
	                   sizex,sizey,
	                   xloc,yloc,xloc + xview,yloc + yview,
	                   &bmp[radarx + (radary * sizex)], alpha);

	// Now determine what objects are visible on the radar ..
	while (listtype <= 1)
	{
		if (listtype == 0) // do oblist, standard
		{
			here = data->oblist;
			listtype++;
		}
		else if (listtype == 1) // do weapons
		{
			here = data->weaplist;
			listtype++;
		}
		else
			continue;

		while (here)
		{
			if (here->ob)
				oborder = here->ob->query_order();
			do_show = 0; // don't show, by default
			if (here->ob
			        && (oborder == ORDER_LIVING || oborder == ORDER_WEAPON
			            || (oborder == ORDER_TREASURE && (here->ob->query_family() == FAMILY_LIFE_GEM))
			            || (oborder == ORDER_TREASURE && (here->ob->query_family() == FAMILY_EXIT))
			            || (oborder == ORDER_GENERATOR && can_see)
			           )
			        && (obteam==here->ob->team_num || here->ob->invisibility_left < 1 || can_see)
			        && on_screen( (short) ((here->ob->xpos+1)/GRID_SIZE), (short) ((here->ob->ypos+1)/GRID_SIZE), radarx, radary)
			   )
				do_show = 1;
			if (do_show)
			{
				tempx = xloc + ((here->ob->xpos+1)/GRID_SIZE - radarx);
				tempy = yloc + ((here->ob->ypos+1)/GRID_SIZE - radary);
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
					tempcolor = (here->ob->query_team_color());
					if (viewscreenp && viewscreenp->control == here->ob)
					{
						tempcolor = (unsigned char) (random(256));
						if (tempx >= (xloc + xview - 1) && tempy < (yloc+yview) )
						{
							screenp->pointb(tempx-1,tempy,tempcolor, alpha);
							screenp->pointb(tempx,tempy,tempcolor, alpha);
							screenp->pointb(tempx-1,tempy+1,tempcolor, alpha);
							screenp->pointb(tempx,tempy+1,tempcolor, alpha);

						}
						else if (tempx >= (xloc + xview -1) )
						{
							screenp->pointb(tempx,tempy,tempcolor, alpha);
							screenp->pointb(tempx-1,tempy,tempcolor, alpha);
							screenp->pointb(tempx,tempy-1,tempcolor, alpha);
							screenp->pointb(tempx-1,tempy-1,tempcolor, alpha);

						}
						else if (tempy >= (yloc + yview -1) && tempx < (xloc+xview) )
						{
							screenp->pointb(tempx,tempy,tempcolor, alpha);
							screenp->pointb(tempx+1,tempy,tempcolor, alpha);
							screenp->pointb(tempx,tempy-1,tempcolor, alpha);
							screenp->pointb(tempx+1,tempy-1,tempcolor, alpha);
						}
						else
						{
							screenp->pointb(tempx,tempy,tempcolor, alpha);
							screenp->pointb(tempx+1,tempy,tempcolor, alpha);
							screenp->pointb(tempx,tempy+1,tempcolor, alpha);
							screenp->pointb(tempx+1,tempy+1,tempcolor, alpha);
						}
					}
					else if (oborder == ORDER_LIVING)
						screenp->pointb(tempx,tempy,tempcolor, alpha);
					else if (oborder == ORDER_GENERATOR)
						screenp->pointb(tempx,tempy,(char)(tempcolor+1), alpha);
					else if (oborder == ORDER_TREASURE) // currently life gems
						screenp->pointb(tempx,tempy,COLOR_FIRE, alpha);
					else
						screenp->pointb(tempx,tempy,COLOR_WHITE, alpha);
				}//draw the blob onto the radar
			}
			here = here->next;
		}
	} // go back to new screen lists (weapons, etc.)

	here = data->fxlist;
	while (here)
	{
		if (here->ob && !here->ob->dead)
		{
			oborder  = here->ob->query_order();
			obfamily = here->ob->query_family();

			do_show = 0; // don't show, by default
			if (oborder == ORDER_TREASURE)
			{
				if (can_see)
				{
					switch (obfamily)
					{
						case FAMILY_GOLD_BAR:
							do_show = (short) (YELLOW + random(5));
							break;
						case FAMILY_SILVER_BAR:
							do_show = (short) (GREY + random(5));
							break;
						case FAMILY_DRUMSTICK:
							do_show = (short) (COLOR_BROWN + random(2));
							break;
						case FAMILY_MAGIC_POTION:
						case FAMILY_INVIS_POTION:
						case FAMILY_INVULNERABLE_POTION:
						case FAMILY_FLIGHT_POTION:
							do_show = (short) (COLOR_BLUE + random(5));
							break;
						default:
							do_show = 0;
							break;
					}
				}
				if (obfamily == FAMILY_EXIT || obfamily == FAMILY_TELEPORTER)
					do_show = (short) LIGHT_BLUE + random(7);
			}
			if (!on_screen( (short) ((here->ob->xpos+1)/GRID_SIZE),
			                (short) ((here->ob->ypos+1)/GRID_SIZE),
			                radarx, radary) )
				do_show = 0;
			if (do_show)
			{
				tempx = xloc + ((here->ob->xpos+1)/GRID_SIZE - radarx);
				tempy = yloc + ((here->ob->ypos+1)/GRID_SIZE - radary);
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
					screenp->pointb(tempx,tempy,(char)do_show, alpha);
				}//draw the blob onto the radar
			} // end of valid do_show
		}  // end of if here->ob
		here = here->next;
	} // end of while (here)

	return 1;
}

//short radar::refresh()
//{
// The first two values are screwy... I don't know why
// screenp->buffer_to_screen(xloc, yloc, xview, yview);
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
    update(&screenp->level_data);
}

// This function re-initializes the radar map data.  Do not
// call it often, as it is very slow ..
void radar::update(LevelData* data)
{
	short temp, i, j;

	for (i = 0; i < sizex; i++)
		for (j = 0; j < sizey; j++)
		{
			// Check if item in background grid
			switch ((unsigned char)data->grid.data[i+sizex*j])
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
					temp = (short) (COLOR_GREEN + random(3) + 3);
					break;
				case PIX_TREE_M1: // Trees are green
				case PIX_TREE_ML:
				case PIX_TREE_T1:
				case PIX_TREE_MR:
				case PIX_TREE_MT:
					temp = (short) (COLOR_TREES + random(3));
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

				default:
					temp =  0;
			}
			bmp[i+sizex*j] = (unsigned char) temp;
		}

}
