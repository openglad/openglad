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
//pixie.cpp

/* ChangeLog
	buffers: 7/31/02: 
		*include cleanup
	buffers: 8/8/02:
		*changed the SDL surfaces to 24bit
*/
#include "graph.h"

// ************************************************************
//  Pixie -- Base graphic object. It holds pixel by pixel data
//  of what should appear on screen. When told to, it handles
//  its own placing and movement on the background screen
//  buffer, though it requires info from the screen object.
// ************************************************************
/*
  pixie()                                                       - Does nothing (DON'T USE)
  pixie(char,short,short,screen)    - initializes the pixie data (pix = char)
  short setxy(short, short)                   - set x,y coords (without drawing)
  short move(short,short)                             - move pixie x,y
  short draw(short,short)                             - put pixie x,y
  short draw()
  short on_screen()
*/


// Pixie -- this initializes the graphics data for the pixie,
// as well as its graphics x and y size.  In addition, it informs
// the pixie of the screen object it is linked to.
pixie::pixie(const PixieData& data, screen* myscreen)
{
	screenp = myscreen;
	set_data(data);
	
	accel = 0;
}

//buffers: new constructor that automatically calls init_sdl_surface
pixie::pixie(const PixieData& data, screen *myscreen, int doaccel)
{
	screenp = myscreen;
	set_data(data);
	
	accel = 0;
	
	if(doaccel)
		init_sdl_surface();
}

// Destruct the pixie and its variables
pixie::~pixie()
{
	if(accel)
		SDL_FreeSurface(bmp_surface);
	//  delete oldbmp;
}

void pixie::set_data(const PixieData& data)
{
	bmp = data.data;
	sizex = data.w;
	sizey = data.h;
	size = (unsigned short) (sizex*sizey);
}

// Set the pixie's x and y positon without drawing.
short pixie::setxy(short x, short y)
{
	xpos = x;
	ypos = y;
	return 1;
}

// Allows the pixie to be moved using pixel coord data
short pixie::move(short x, short y)
{
	return setxy((short)(xpos+x),(short)(ypos+y));
}

// Allows the pixie to be placed using pixel coord data
short pixie::draw(short x, short y, viewscreen  * view_buf)
{
	setxy(x, y);
	return draw(view_buf);
}

short pixie::draw(viewscreen * view_buf)
{
	Sint32 xscreen, yscreen;

	//  if (!on_screen(view_buf))
	//         return 0;
	//we actually don't need to waste time on the above since the clipper
	//will handle it

	xscreen = (Sint32) (xpos - view_buf->topx + view_buf->xloc);
	yscreen = (Sint32) (ypos - view_buf->topy + view_buf->yloc);

	if(accel)
	{
		view_buf->screenp->putbuffer(xscreen, yscreen, sizex, sizey,
		                             view_buf->xloc, view_buf->yloc,
		                             view_buf->endx, view_buf->endy,
		                             bmp_surface);
	}
	else
	{
		view_buf->screenp->putbuffer(xscreen, yscreen, sizex, sizey,
		                             view_buf->xloc, view_buf->yloc,
		                             view_buf->endx, view_buf->endy,
		                             bmp);
	}

	return 1;
}

// Allows the pixie to be placed using pixel coord data
short pixie::drawMix(short x, short y, viewscreen  * view_buf)
{
	setxy(x, y);
	return drawMix(view_buf);
}

short pixie::drawMix(viewscreen * view_buf)
{
	Sint32 xscreen, yscreen;

	//  if (!on_screen(view_buf))
	//         return 0;
	//we actually don't need to waste time on the above since the clipper
	//will handle it

	xscreen = (Sint32) (xpos - view_buf->topx + view_buf->xloc);
	yscreen = (Sint32) (ypos - view_buf->topy + view_buf->yloc);

	view_buf->screenp->walkputbuffer(xscreen, yscreen, sizex, sizey,
	                                 view_buf->xloc, view_buf->yloc,
	                                 view_buf->endx, view_buf->endy,
	                                 bmp, RED);

	return 1;
}


short pixie::put_screen(short x, short y)
{
	screenp->putdata(x, y, sizex, sizey, bmp);
	return 1;
}

short pixie::put_screen(short x, short y, unsigned char alpha)
{
	screenp->putdata_alpha(x, y, sizex, sizey, bmp, alpha);
	return 1;
}

short pixie::on_screen()
{
	short i;
	for (i=0; i < screenp->numviews; i++)
	{
		if (on_screen(screenp->viewob[i]))
			return 1;
	}
	return 0;
}

short pixie::on_screen(viewscreen  *viewp)
{
	short topx = viewp->topx;
	short topy = viewp->topy;
	short xview = viewp->xview;
	short yview = viewp->yview;

	// Return 0 if off viewscreen.
	// These measurements are grid coords, not pixels.
	if ( (xpos+sizex) < topx)
		return 0;     // we are to the left of the view
	else if ( xpos > (topx + xview) )
		return 0;  // we are to the right of the view
	else if ( (ypos+sizey) < topy)
		return 0;  // we are above the view
	else if ( ypos > (topy + yview) )
		return 0; //we are below the view
	else
		return 1;
}

//buffers: this func initializes the bmp_surface
void pixie::init_sdl_surface(void)
{
	int r,g,b,c,i,j,num;
	SDL_Rect rect;

	bmp_surface = SDL_CreateRGBSurface(SDL_SWSURFACE,sizex*screenp->mult,sizey*screenp->mult,32,
	                                   0,0,0,0);
	if(!bmp_surface)
	{
		Log("ERROR: pixie::init_sdl_surface(): could not create bmp_surface\n");
	}

	num=0;
	for(i=0;i<sizey;i++)
		for(j=0;j<sizex;j++)
		{
			query_palette_reg(bmp[num],&r,&g,&b);
			c = SDL_MapRGB(bmp_surface->format,r*4,g*4,b*4);
			rect.x = j*screenp->mult;
			rect.y = i*screenp->mult;
			rect.w = rect.h = screenp->mult;
			SDL_FillRect(bmp_surface,&rect,c);
			num++;
		}

	accel = 1;
}

//buffers: turn SDL_Surface accel on and off
void pixie::set_accel(int a)
{
	if(a)
	{
		init_sdl_surface();
	}
	else
	{
		if(accel)
		{
			SDL_FreeSurface(bmp_surface);
			accel = 0;
		}
	}
}
