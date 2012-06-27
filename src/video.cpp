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
// Video object code

#include "graph.h"
#include "sai2x.h"

#define VIDEO_BUFFER_WIDTH 320
#define VIDEO_WIDTH 320
#define VIDEO_SIZE 64000
#define CX_SCREEN 320
#define CY_SCREEN 200
#define ASSERT(x) if (!(x)) return 0;

// Zardus: set_mult in input.cpp sets input's mult value
void set_mult(int);

char * videoptr = (char *) VIDEO_LINEAR;

SDL_Surface *screen; //buffers: this is what we draw in
SDL_Surface *fontbuffer;
int fontcolorkey;
Screen *E_Screen;

// Zardus: for the ugly retreat crash hack fix
bool retreat;

video::video()
{
	long i;
	const char *qresult;
	RenderEngine render;

	qresult = cfg.query("graphics","fullscreen");
	if(qresult && strcmp(qresult,"on")==0)
		fullscreen = 1;
	else
		fullscreen = 0;

	qresult = cfg.query("graphics", "render");
	if(qresult && strcmp(qresult, "normal")==0) {
		mouse_mult = 1;
		mult = 1;
		font_mult = 1;
		render = NoZoom;
	} else if(qresult && strcmp(qresult,"sai")==0) {
		mouse_mult = 2;
		mult = 1;
		font_mult = 2;
		render = SAI;
	} else if(qresult && strcmp(qresult,"eagle")==0) {
		mouse_mult = 2;
		mult = 1;
		font_mult = 2;
		render = EAGLE;
	} else if(qresult && strcmp(qresult,"double")==0) {
		mouse_mult = 2;
		mult = 2;
		font_mult = 2;
		render = DOUBLE;
	}

	set_mult(mouse_mult);

	fadeDuration = 2000;

	// Load our palettes ..
	load_and_set_palette("our.pal", ourpalette);
	load_palette("our.pal", redpalette);

	// Create the red-shifted palette
	for (i=32; i < 256; i++)
	{
		redpalette[i*3+1] /= 2;
		redpalette[i*3+2] /= 2;
	}

	load_palette("our.pal", bluepalette);

	// Create the blue-shifted palette
	//for (i=32; i < 256; i++)
	//{
	//	bluepalette[i*3+0] /= 2;
	//	bluepalette[i*3+1] /= 2;
	//}

	//buffers: screen init
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);

	screen = SDL_CreateRGBSurface(SDL_SWSURFACE,320*mult,200*mult,32,0,0,0,0);
	fontbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,320*font_mult,200*font_mult,32,0,0,0,0);
	fontcolorkey = SDL_MapRGB(fontbuffer->format,20,0,0);
	SDL_SetColorKey(fontbuffer,SDL_SRCCOLORKEY,fontcolorkey);
	SDL_FillRect(fontbuffer,NULL,fontcolorkey);
	
	E_Screen = new Screen(render,fullscreen);

/*
#ifndef OPENSCEN
	qresult = cfg.query("graphics","fullscreen");
	if(strcmp(qresult,"on")==0)
		screen = SDL_SetVideoMode (screen_width, screen_height, 24, SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
	else
#endif
		screen = SDL_SetVideoMode (screen_width, screen_height, 24, SDL_HWSURFACE | SDL_DOUBLEBUF);
*/
}

video::~video()
{
	E_Screen->Quit();
	delete E_Screen;
	SDL_FreeSurface(screen);
	SDL_FreeSurface(fontbuffer);
	SDL_Quit();
}

char * video::getbuffer()
{
	return &videobuffer[0];
}

void video::clearscreen()
{
	//buffers: PORT: clear the offscreen buffer, not the screen.
	//buffers: we are going to see if we can double buf everything.
	SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 0, 0, 0));
	SDL_FillRect(fontbuffer,NULL,fontcolorkey);
}

void video::clearbuffer()
{
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format,0,0,0));
	SDL_FillRect(fontbuffer,NULL,fontcolorkey);
}

void video::clearfontbuffer()
{
//	SDL_FillRect(fontbuffer,NULL,fontcolorkey);
	clearfontbuffer(0,0,320,200);
}

void video::clearfontbuffer(int x, int y, int w, int h)
{
	SDL_Rect rect;

	rect.x = x*font_mult;
	rect.y = y*font_mult;
	rect.w = w*font_mult;
	rect.h = h*font_mult;

	SDL_FillRect(fontbuffer,&rect,fontcolorkey);
}

void video::draw_box(long x1, long y1, long x2, long y2, unsigned char color, long filled)
{
	long xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	long ylength = y2 - y1 + 1;
	long i;

	if (!filled)          // Hollow box
	{
		hor_line(x1, y1, xlength, color);
		hor_line(x1, y2, xlength, color);
		ver_line(x1, y1, ylength, color);
		ver_line(x2, y1, ylength, color);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, color);
	}
}

void video::draw_box(long x1, long y1, long x2, long y2, unsigned char color, long filled, long tobuffer)
{
	long xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	long ylength = y2 - y1 + 1;
	long i;

	if (!filled)          // Hollow box
	{
		hor_line(x1, y1, xlength, color, tobuffer);
		hor_line(x1, y2, xlength, color, tobuffer);
		ver_line(x1, y1, ylength, color, tobuffer);
		ver_line(x2, y1, ylength, color, tobuffer);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, color, tobuffer);
	}
}


void video::draw_button(long x1, long y1, long x2, long y2, long border)
{
	long xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	long ylength = y2 - y1 + 1;
	long i;

	clearfontbuffer(x1,y1,xlength,ylength);

	if (border)           // Hollow box
	{
		hor_line(x1, y1, xlength, 15); // top, old 14
		hor_line(x1, y2, xlength, 11); // bottom, old 10
		ver_line(x1, y1, ylength, 14); // left, old 13
		ver_line(x2, y1, ylength, 12); // right, old 11
		draw_button(x1+1,y1+1,x2-1,y2-1,border-1);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, 13); // facing, old 12
	}
}

void video::draw_button(long x1, long y1, long x2, long y2, long border, long tobuffer)
{
	long xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	long ylength = y2 - y1 + 1;
	long i;

	clearfontbuffer(x1,y1,xlength,ylength);

	if (border)           // Hollow box
	{
		hor_line(x1, y1, xlength, 15, tobuffer); // top, old 14
		hor_line(x1, y2, xlength, 11, tobuffer); // bottom, old 10
		ver_line(x1, y1, ylength, 14, tobuffer); // left, old 13
		ver_line(x2, y1, ylength, 12, tobuffer); // right, old 11
		draw_button(x1+1,y1+1,x2-1,y2-1,border-1, tobuffer);
	}
	else
	{
		for (i = 0; i < ylength; i++)
			hor_line(x1, y1+i, xlength, 13, tobuffer); // facing, old 12
	}
}

// Draws an empty but headed dialog box, returns the edge at
// which to draw text ... does NOT display to screen.
long video::draw_dialog(long x1, long y1, long x2, long y2,
                        const char *header)
{
	static text dialogtext(myscreen, "textbig.pix"); // large text
	long centerx = x1 + ( (x2-x1) /2 ), left;
	short textwidth;

	draw_button(x1, y1, x2, y2, 1, 1); // single-border width, to buffer
	draw_text_bar(x1+4, y1+4, x2-4, y1+18); // header field
	textwidth = dialogtext.query_width(header);
	left = centerx - (textwidth/2);

	if (header && strlen(header) ) // display a title?
		dialogtext.write_xy(left, y1+6, header,
		                    (unsigned char) RED, 1); // draw header to buffer
	draw_text_bar(x1+4, y1+20, x2-4, y2-4); // draw box for text

	return x1+6;  // where text should begin to display, left-aligned

}

void video::draw_text_bar(long x1, long y1, long x2, long y2)
{
	long xlength = x2 - x1 + 1;    // Assume topleft-bottomright specs
	long ylength = y2 - y1 + 1;

	// First draw the filled, generic grey bar facing
	draw_box(x1, y1, x2, y2, 12, 1, 1); // filled, to buffer

	// Draw the indented border
	hor_line(x1, y1, xlength, 10, 1);  // top
	hor_line(x1, y2, xlength, 15, 1);  // bottom
	ver_line(x1, y1, ylength, 11, 1);  // left
	ver_line(x2, y1, ylength, 14, 1);  // right

}


void video::putblack(long startx, long starty, long xsize, long ysize)
{
	unsigned long curx, cury;
	unsigned long curpoint;

	for(cury = starty;cury < starty +ysize;cury++)
	{
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curpoint = (curx + (cury*VIDEO_WIDTH));
			if (curpoint > 0 && curpoint < VIDEO_SIZE)
				videoptr[curpoint] = 0;
		}
	}
}

// This version of fastbox writes directly to screen memory;
// The following version, with an extra parameter, writes to
// the buffer instead.  Note that it does NOT update (to screen)
// the area which it changes..
void video::fastbox(long startx, long starty, long xsize, long ysize, unsigned char color)
{
	//buffers: we should always draw into the back buffer
	fastbox(startx,starty,xsize,ysize,color,1);
}

// This is the version which writes to the buffer..
void video::fastbox(long startx, long starty, long xsize, long ysize, unsigned char color, unsigned char flag)
{
	SDL_Rect rect;
	int r,g,b;

	// Zardus: FIX: small check to make sure we're not trying to put in antimatter or something
	if (xsize < 0 || ysize < 0 || startx < 0 || starty < 0)
		return;

	if (!flag) // then write to screen directly
	{
		fastbox(startx, starty, xsize, ysize, color);
		return ;
	}

	//buffers: create the rect to fill with SDL_FillRect
	rect.x = startx*mult;
	rect.y = starty*mult;
	rect.w = xsize*mult;
	rect.h = ysize*mult;

	query_palette_reg(color,&r,&g,&b);
	SDL_FillRect(screen,&rect,SDL_MapRGB(screen->format,r*4,g*4,b*4));
}

// Place a point on the screen
//buffers: PORT: this point func is equivalent to drawing directly to screen
void video::point(long x, long y, unsigned char color)
{
	pointb(x,y,color);
	//buffers: PORT: SDL_UpdateRect(screen,x,y,1,1);
}

//buffers: PORT: this draws a point in the offscreen buffer
//buffers: PORT: used for all the funcs that draw stuff in the offscreen buf
void video::pointb(long x, long y, unsigned char color)
{
	int r,g,b;
	int c;
	SDL_Rect rect;

	//buffers: this does bound checking (just to be safe)
	if(x<0 || x>319 || y<0 || y>199)
		return;

	query_palette_reg(color,&r,&g,&b);

	c = SDL_MapRGB(screen->format, r*4, g*4, b*4);

	rect.x = x*mult;
	rect.y = y*mult;
	rect.w = mult;
	rect.h = mult;
	SDL_FillRect(screen,&rect,c);
}

//buffers: this sets the color using raw RGB values. no *4...
void video::pointb(long x, long y, int r, int g, int b)
{
	SDL_Rect  rect;
	int c;
	c = SDL_MapRGB(screen->format,r,g,b);

	rect.x = x*mult;
	rect.y = y*mult;
	rect.w = mult;
	rect.h = mult;
	SDL_FillRect(screen,&rect,c);
}

//buffers: draw color using an offset
void video::pointb(int offset, unsigned char color)
{
	int x, y;

	y = offset/320;
	x = offset - y*320;

	pointb(x,y,color);
}

// Place a horizontal line on the screen.
//buffers: this func originally drew directly to the screen
void video::hor_line(long x, long y, long length, unsigned char color)
{
	hor_line(x,y,length,color,1);
}

void video::hor_line(long x, long y, long length, unsigned char color, long tobuffer)
{
	unsigned long i;

	if (!tobuffer)
	{
		hor_line(x,y,length,color);
		return;
	}

	clearfontbuffer(x,y,length,1);
	
	for (i = 0; i < length; i++)
		pointb(x+i,y,color);
}


// Place a vertical line on the screen.
// buffers: this func originally drew directly to the screen
void video::ver_line(long x, long y, long length, unsigned char color)
{
	//buffers: we always want to draw to the back buffer now
	ver_line(x,y,length,color,1);
}

void video::ver_line(long x, long y, long length, unsigned char color, long tobuffer)
{
	unsigned long i;

	if (!tobuffer)
	{
		ver_line(x,y,length,color);
		return;
	}

	clearfontbuffer(x,y,1,length);
	
	for (i = 0; i < length; i++)
		pointb(x,y+i,color);
}


//
//video::do_cycle
//cycle the palette for flame and water motion
// query and set functions are located in pal32.cpp
//buffers: PORT: added & to the last 3 args of the query_palette_reg funcs
void video::do_cycle(long curmode, long maxmode)
{
	long i;
	//buffers: PORT: changed these two arrays to ints
	int tempcol[3];
	int curcol[3];

	curmode %= maxmode;   // avoid over-runs

	if (!curmode)  // then cycle on 0
	{
		// For orange:
		query_palette_reg(ORANGE_END, &tempcol[0],
		                  &tempcol[1], &tempcol[2]);        // get first color
		for (i=ORANGE_END; i > ORANGE_START; i--)
		{
			query_palette_reg((char) (i-1), &curcol[0], &curcol[1], &curcol[2]);
			set_palette_reg((char) i, (char) curcol[0],(char) curcol[1], (char) curcol[2]);
		}
		set_palette_reg(ORANGE_START, tempcol[0],
		                tempcol[1], tempcol[2]);        // reassign last to first

		// For blue:
		query_palette_reg(WATER_END, &tempcol[0],
		                  &tempcol[1], &tempcol[2]);        // get first color
		for (i=WATER_END; i > WATER_START; i--)
		{
			query_palette_reg((char) (i-1), &curcol[0], &curcol[1], &curcol[2]);
			set_palette_reg((char) i, curcol[0], curcol[1], curcol[2]);
		}
		set_palette_reg(WATER_START, tempcol[0],
		                tempcol[1], tempcol[2]);        // reassign last to first
	}
}

//video::putdata
//draws objects to screen, respecting transparency
//used by text
void video::putdata(long startx, long starty, long xsize, long ysize, char  *sourcedata)
{
	unsigned long curx, cury;
	unsigned char curcolor;
	unsigned long num = 0;

	for(cury = starty;cury < starty +ysize;cury++)
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curcolor = sourcedata[num++];
			if (!curcolor)
				continue;
			//buffers: PORT: targ = (curx + (cury*VIDEO_WIDTH));
			//buffers: PORT: if (targ>0 && targ<VIDEO_SIZE)
			//buffers: PORT: videoptr[targ] = curcolor;
			point(curx,cury,curcolor);//buffers: PORT: draw the point
		}
}


void video::putdatatext(long startx, long starty, long xsize, long ysize, char  *sourcedata)
{
        unsigned long curx, cury;
        unsigned char curcolor;
       	unsigned long num = 0;
	int r,g,b,color;
	SDL_Rect rect;

	for(cury = starty;cury < starty +ysize;cury++)
 	{	
		for (curx = startx; curx < startx +xsize; curx++)
	        {
			curcolor = sourcedata[num++];
			if (!curcolor)
		        	continue;
	        	//point(curx,cury,curcolor);//buffers: PORT: draw the poin
			query_palette_reg(curcolor,&r,&g,&b);
			color = SDL_MapRGB(fontbuffer->format,r*4,g*4,b*4);

			rect.x = curx*font_mult;
			rect.y = cury*font_mult;
			rect.w = font_mult;
			rect.h = font_mult;
			printf("test\n");
			SDL_FillRect(fontbuffer,&rect,color);
		}
    	}
}

//video::putdata
//draws objects to screen, respecting transparency
//used by text
void video::putdata(long startx, long starty, long xsize, long ysize, char  *sourcedata, unsigned char color)
{
	unsigned long curx, cury;
	unsigned char curcolor;
	unsigned long num = 0;

	for(cury = starty;cury < starty +ysize;cury++)
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curcolor = sourcedata[num++];
			if (!curcolor)
				continue;
			//if (curcolor>=248) curcolor = color+(curcolor-248);
			if (curcolor>247)
				curcolor = color;
			//buffers: PORT: targ = (curx + (cury*VIDEO_WIDTH));
			//buffers: PORT: if (targ>0 && targ<VIDEO_SIZE)
			//buffers: PORT: videoptr[targ] = curcolor;
			point(curx,cury,curcolor);
		}
}

void video::putdatatext(long startx, long starty, long xsize, long ysize, char  *sourcedata, unsigned char color)
{
        unsigned long curx, cury;
        unsigned char curcolor;
        unsigned long num = 0;
	int r,g,b,scolor;
	SDL_Rect rect;

       for(cury = starty;cury < starty +ysize;cury++)
	       for (curx = startx; curx < startx +xsize; curx++)
               {
	                curcolor = sourcedata[num++];
                        if (!curcolor)
  	                      	continue;
				//if (curcolor>=248) curcolor = color+(curcolor-248);
			if (curcolor>247)
			        curcolor = color;
			query_palette_reg(curcolor,&r,&g,&b);
			scolor = SDL_MapRGB(fontbuffer->format,r*4,g*4,b*4);

                   	rect.x = curx*font_mult;
	             	rect.y = cury*font_mult;
			rect.w = font_mult;	
			rect.h = font_mult;
			SDL_FillRect(fontbuffer,&rect,scolor);
		}
}

// video::putbuffer
// used to put tiles into the buffer as we compose the screen
// tilestartx,tilestarty are the ul corner of the tiles position on
//    screen, which may be negative since we have tiles offscreen
// tilewidth,tileheight are the tile size, which will usually be GRID_SIZE
//    but this leaves things open
// portstartx portstarty portendx porthendy allow us to clip to
//    a rectangular window on screen, ie a viewscreen
// sourceptr is a pointer to the video data to be copied into the buffer
void video::putbuffer(long tilestartx, long tilestarty,
                      long tilewidth, long tileheight,
                      long portstartx, long portstarty,
                      long portendx, long portendy,
                      char * sourceptr)
{
	int i,j,num;
	long xmin=0, xmax=tilewidth, ymin=0, ymax=tileheight;
	//unsigned long targetshifter,sourceshifter; //these let you wrap around in the arrays
	long totrows,rowsize; //number of rows and width of each row in the source
	//unsigned long offssource,offstarget; //offsets into each array, for clipping and wrap
	char * sourcebufptr = &sourceptr[0];
	if (tilestartx >= portendx || tilestarty >= portendy )
		return; // abort, the tile is drawing outside the clipping region

	if ((tilestartx + tilewidth) > portendx)   //this clips on the right edge
		xmax = portendx - tilestartx; //stop drawing after xmax bytes

	else if (tilestartx < portstartx) //this clips on the left edge
	{
		xmin = portstartx - tilestartx;
		tilestartx = portstartx;
	}

	if ((tilestarty + tileheight) > portendy) //this clips on the bottom edge
		ymax = portendy - tilestarty;

	else if (tilestarty < portstarty) //this clips the top edge
	{
		ymin = portstarty - tilestarty;
		tilestarty = portstarty;
	}

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//targetshifter = VIDEO_BUFFER_WIDTH - rowsize; //this will wrap the target around
	//sourceshifter = tilewidth - rowsize;  //this will wrap the source around

	//offstarget = (tilestarty*VIDEO_BUFFER_WIDTH) + tilestartx; //start at u-l position
	//offssource = (ymin * tilewidth) + xmin; //start at u-l position

	//buffers: draws graphic. actually uses the above bound checking now (7/18/02)
	num=0;
	for(i=ymin;i<ymax;i++)
	{
		for(j=xmin;j<xmax;j++)
		{
			num = i*tilewidth + j;
			pointb(j+tilestartx-xmin,i+tilestarty-ymin,sourcebufptr[num]);
		}
	}
}

//buffers: this is the SDL_Surface accelerated version of putbuffer
void video::putbuffer(long tilestartx, long tilestarty,
                      long tilewidth, long tileheight,
                      long portstartx, long portstarty,
                      long portendx, long portendy,
                      SDL_Surface *sourceptr)
{
	SDL_Rect rect,temp;
	long xmin=0, xmax=tilewidth, ymin=0, ymax=tileheight;
	//unsigned long targetshifter,sourceshifter; //these let you wrap around in the arrays
	long totrows,rowsize; //number of rows and width of each row in the source
	//unsigned long offssource,offstarget; //offsets into each array, for clipping and wrap
	//buffers: unsigned char * sourcebufptr = &sourceptr[0];
	if (tilestartx >= portendx || tilestarty >= portendy )
		return; // abort, the tile is drawing outside the clipping region

	if ((tilestartx + tilewidth) > portendx)   //this clips on the right edge
		xmax = portendx - tilestartx; //stop drawing after xmax bytes
	else if (tilestartx < portstartx) //this clips on the left edge
	{
		xmin = portstartx - tilestartx;
		tilestartx = portstartx;
	}

	if ((tilestarty + tileheight) > portendy) //this clips on the bottom edge
		ymax = portendy - tilestarty;
	else if (tilestarty < portstarty) //this clips the top edge
	{
		ymin = portstarty - tilestarty;
		tilestarty = portstarty;
	}

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//targetshifter = VIDEO_BUFFER_WIDTH - rowsize; //this will wrap the target around
	//sourceshifter = tilewidth - rowsize;  //this will wrap the source around

	//offstarget = (tilestarty*VIDEO_BUFFER_WIDTH) + tilestartx; //start at u-l position
	//offssource = (ymin * tilewidth) + xmin; //start at u-l position

	rect.x = (tilestartx)*mult;
	rect.y = (tilestarty)*mult;
	temp.x = xmin*mult;
	temp.y = ymin*mult;
	temp.w = (xmax-xmin)*mult;
	temp.h = (ymax-ymin)*mult;
	SDL_BlitSurface(sourceptr,&temp,screen,&rect);
}


// walkputbuffer draws active guys to the screen (basically all non-tiles
// c-only since it isn't used that often (despite what you might think)
// walkerstartx,walkerstarty are the screen position we will try to draw to
// walkerwidth,walkerheight define the object's size
// portstartx,portstarty,portendx,portendy define a clipping rectangle
// sourceptr holds the walker data
// teamcolor is used for recoloring the guys to the appropriate team
void video::walkputbuffer(long walkerstartx, long walkerstarty,
                          long walkerwidth, long walkerheight,
                          long portstartx, long portstarty,
                          long portendx, long portendy,
                          char  *sourceptr, unsigned char teamcolor)
{
	long curx, cury;
	unsigned char curcolor;
	long xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
	long walkoff=0,buffoff=0,walkshift=0,buffshift=0;
	long totrows,rowsize;

	if (walkerstartx >= portendx || walkerstarty >= portendy)
		return; //walker is below or to the right of the viewport

	if (walkerstartx < portstartx) //clip the left edge of the view
	{
		xmin = portstartx-walkerstartx;  //start drawing walker at xmin
		walkerstartx = portstartx;
	}

	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
		xmax = portendx - walkerstartx; //stop drawing walker at xmax

	if (walkerstarty < portstarty) // clip the top edge
	{
		ymin = portstarty-walkerstarty; //start drawing walker at ymin
		walkerstarty = portstarty;
	}

	else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
		ymax = portendy - walkerstarty; //stop drawing walker at ymax

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//note!! the clipper makes the assumption that no object is larger than
	// the view it will be clipped to in either dimension!!!

	walkshift = walkerwidth - rowsize;
	buffshift = VIDEO_BUFFER_WIDTH - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;


	for(cury = 0; cury < totrows;cury++)
	{
		for(curx=0;curx<rowsize;curx++)
		{
			curcolor = sourceptr[walkoff++];
			if (!curcolor)
			{
				buffoff++;
				continue;
			}
			if (curcolor > (unsigned char) 247)
				curcolor = (unsigned char) (teamcolor+(255-curcolor));
			//buffers: PORT: videobuffer[buffoff++] = curcolor;
			pointb(walkerstartx+curx,walkerstarty+cury,curcolor);
		}
		walkoff += walkshift;
		buffoff += buffshift;
	}
}

void video::walkputbuffertext(long walkerstartx, long walkerstarty,
                          long walkerwidth, long walkerheight,
                          long portstartx, long portstarty,
                          long portendx, long portendy,
                          char  *sourceptr, unsigned char teamcolor)
{
        long curx, cury;
        unsigned char curcolor;
        long xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
        long walkoff=0,buffoff=0,walkshift=0,buffshift=0;
        long totrows,rowsize;
	int r,g,b,color;
	SDL_Rect rect;

        if (walkerstartx >= portendx || walkerstarty >= portendy)
                return; //walker is below or to the right of the viewport

        if (walkerstartx < portstartx) //clip the left edge of the view
        {
                xmin = portstartx-walkerstartx;  //start drawing walker at xmin
                walkerstartx = portstartx;
        }
	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
                xmax = portendx - walkerstartx; //stop drawing walker at xmax

        if (walkerstarty < portstarty) // clip the top edge
        {
                ymin = portstarty-walkerstarty; //start drawing walker at ymin
                walkerstarty = portstarty;
        }

        else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
                ymax = portendy - walkerstarty; //stop drawing walker at ymax

        totrows = (ymax-ymin); //how many rows to copy
        rowsize = (xmax-xmin); //how many bytes to copy
        if (totrows <= 0 || rowsize <= 0)
                return; //this happens on bad args

        //note!! the clipper makes the assumption that no object is larger than
        // the view it will be clipped to in either dimension!!!

        walkshift = walkerwidth - rowsize;
        buffshift = VIDEO_BUFFER_WIDTH - rowsize;

        walkoff   = (ymin * walkerwidth) + xmin;
        buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;

        for(cury = 0; cury < totrows;cury++)
        {
                for(curx=0;curx<rowsize;curx++)
                {
                        curcolor = sourceptr[walkoff++];
                        if (!curcolor)
                        {
                                buffoff++;
                                continue;
                        }
                        if (curcolor > (unsigned char) 247)
                                curcolor = (unsigned char) (teamcolor+(255-curcolor));
			query_palette_reg(curcolor,&r,&g,&b);
                        color = SDL_MapRGB(fontbuffer->format,r*4,g*4,b*4);

                        rect.x = (curx + walkerstartx)*font_mult;
                        rect.y = (cury + walkerstarty)*font_mult;
                        rect.w = font_mult;
                        rect.h = font_mult;
                        SDL_FillRect(fontbuffer,&rect,color);
                }
                walkoff += walkshift;
                buffoff += buffshift;
        }
}


void video::walkputbuffer(long walkerstartx, long walkerstarty,
                          long walkerwidth, long walkerheight,
                          long portstartx, long portstarty,
                          long portendx, long portendy,
                          char  *sourceptr, unsigned char teamcolor,
                          unsigned char mode, long invisibility,
                          unsigned char outline, unsigned char shifttype)
{
	long curx, cury;
	unsigned char curcolor, bufcolor;
	long xmin = 0, xmax= walkerwidth , ymin= 0 , ymax= walkerheight;
	long walkoff=0,buffoff=0,walkshift=0,buffshift=0;
	long totrows,rowsize;
	signed char shift;
	int yval, xval;
	Uint8 r,g,b;
	int tx,ty,tempbuf;

	if (walkerstartx >= portendx || walkerstarty >= portendy)
		return; //walker is below or to the right of the viewport

	if (walkerstartx < portstartx) //clip the left edge of the view
	{
		xmin = portstartx-walkerstartx;  //start drawing walker at xmin
		walkerstartx = portstartx;
	}

	else if (walkerstartx + walkerwidth > portendx) //clip the right edge
		xmax = portendx - walkerstartx; //stop drawing walker at xmax

	if (walkerstarty < portstarty) // clip the top edge
	{
		ymin = portstarty-walkerstarty; //start drawing walker at ymin
		walkerstarty = portstarty;
	}

	else if (walkerstarty + walkerheight > portendy) //clip the bottom edge
		ymax = portendy - walkerstarty; //stop drawing walker at ymax

	totrows = (ymax-ymin); //how many rows to copy
	rowsize = (xmax-xmin); //how many bytes to copy
	if (totrows <= 0 || rowsize <= 0)
		return; //this happens on bad args

	//note!! the clipper makes the assumption that no object is larger than
	// the view it will be clipped to in either dimension!!!

	walkshift = walkerwidth - rowsize;
	buffshift = VIDEO_BUFFER_WIDTH - rowsize;

	walkoff   = (ymin * walkerwidth) + xmin;
	buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;
	xval = walkerstartx;
	yval = walkerstarty;

	// Zardus: FIX: and now we simply replace all the videobuffer stuff with pointb.
	switch (mode)
	{
		case INVISIBLE_MODE:

			for(cury = 0; cury < totrows;cury++)
			{
				for(curx=0;curx<rowsize;curx++)
				{
					curcolor = sourceptr[walkoff++];
					if (!curcolor)
					{
						if (outline)
						{
							if (curx>0)
							{
								if (sourceptr[walkoff-2])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (curx<(rowsize-1))
							{
								if (sourceptr[walkoff])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (cury>0)
							{
								if (sourceptr[walkoff-1-walkerwidth])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}

							if (cury<(totrows-1))
							{
								if (sourceptr[walkoff-1+walkerwidth])
								{
									pointb(xval++, yval, outline);
									continue;
								}
							}
						} // end of outline check

						xval++;
						continue;
					} //end of transparency check

					if (curcolor > (unsigned char) 247)
						curcolor = (unsigned char) (teamcolor+(255-curcolor));

					if (outline)
					{
						if (curx==0 || cury==0 || curx==(walkerwidth-1) || cury==(totrows-1))
						{
							pointb(xval++, yval, outline);
							continue;
						}
					} // end outline

					if (random(invisibility) > 8)
					{
						xval++;
						//videobuffer[buffoff++] = teamcolor+random(7);
						continue;
					}
					pointb(xval++, yval, curcolor);
				} //end of each row

				walkoff += walkshift;
				yval++;
				xval = walkerstartx;
			} // end of all rows

			break; // end INVISIBLE

		case OUTLINE_MODE:

			for(cury = 0; cury < totrows;cury++)
			{
				for(curx=0;curx<rowsize;curx++)
				{
					curcolor = sourceptr[walkoff++];
					if (!curcolor)
					{
						if (curx>0)
						{
							if (sourceptr[walkoff-2])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (curx<(rowsize-1))
						{
							if (sourceptr[walkoff])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (cury>0)
						{
							if (sourceptr[walkoff-1-walkerwidth])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						if (cury<(totrows-1))
						{
							if (sourceptr[walkoff-1+walkerwidth])
							{
								pointb(xval++, yval, outline);
								continue;
							}
						}

						xval++;
						continue;
					} //end of transparency check

					if (curcolor > (unsigned char) 247)
						curcolor = (unsigned char) (teamcolor+(255-curcolor));

					if (curx==0 || cury==0 || curx==(walkerwidth-1) || cury==(totrows-1))
					{
						pointb(xval++, yval, outline);
						continue;
					}

					pointb(xval++, yval, curcolor);
				} //end of each row

				walkoff += walkshift;
				xval = walkerstartx;
				yval++;
			} // end of all rows

			break; // end OUTLINE

			//buffers: PORT: ported the below block of code
		case PHANTOM_MODE:
			switch (shifttype)
			{
				case SHIFT_LEFT:
					shift = -1;
					break;

				case SHIFT_RIGHT:
					shift = 1;
					break;

				case SHIFT_RIGHT_RANDOM:
					shift = (signed char) random(2);
					break;

				default:
					shift = 0;
					break;
			} //end switch (shifttype)

			for(cury = 0; cury < totrows;cury++)
			{
				for(curx=0;curx<rowsize;curx++)
				{
					curcolor = sourceptr[walkoff++];
					if (!curcolor)
					{
						buffoff++;
						continue;
					}

					//buffers: this is a messy optimization. sorry.
					if (shifttype == SHIFT_RANDOM)
					{
						//pointb(buffoff++,get_pixel(buffoff+random(2)));
						tempbuf = buffoff+random(2);
						ty = tempbuf/320;
						tx = tempbuf-ty*320;
						get_pixel(tx,ty,&r,&g,&b);

						ty = buffoff/320;
						tx = buffoff-ty*320;
						;
						pointb(tx,ty,(int)r,(int)g,(int)b);
						buffoff++;
					}

					else if (shifttype == SHIFT_LIGHTER)
					{
						//buffers: bufcolor = videobuffer[buffoff];
						bufcolor = get_pixel(buffoff);
						if ((bufcolor%8)!=0 && bufcolor !=0)
							bufcolor--;
						//buffers: videobuffer[buffoff++] = bufcolor;
						pointb(buffoff,bufcolor);
						buffoff++;
					}

					else if (shifttype == SHIFT_DARKER)
					{
						//buffers: bufcolor = videobuffer[buffoff];
						bufcolor = get_pixel(buffoff);
						if ((bufcolor%7)!=0 && bufcolor<255)
							bufcolor++;
						//videobuffer[buffoff++] = bufcolor;
						pointb(buffoff++,bufcolor);
					}

					else if (shifttype == SHIFT_BLOCKY)
					{
						if (cury%2) //buffers:videobuffer[buffoff++] = videobuffer[buffoff-VIDEO_BUFFER_WIDTH];
							pointb(buffoff++,get_pixel(buffoff-320));
						else if (curx%2) //videobuffer[buffoff++] = videobuffer[buffoff-1];

							pointb(buffoff++,get_pixel(buffoff-2));
						else
							buffoff++;

					}

					else
					{
						//buffers: videobuffer[buffoff++] = videobuffer[buffoff+shift];
						pointb(buffoff,get_pixel(buffoff+shift));
						buffoff++;
					}
				} //end each row

				walkoff += walkshift;
				buffoff += buffshift;
			} //end all rows

			break; //end case PHANTOM

		default: // NORMAL walkputbuffer
			{
				for(cury = 0; cury < totrows;cury++)
				{
					for(curx=0;curx<rowsize;curx++)
					{
						curcolor = sourceptr[walkoff++];
						if (!curcolor)
						{
							buffoff++;
							continue;
						}
						if (curcolor > (unsigned char) 247)
							curcolor = (unsigned char) (teamcolor+(255-curcolor));
						videobuffer[buffoff++] = curcolor;
					} //end each row

					walkoff += walkshift;
					buffoff += buffshift;
				} //end all rows

			} //end default

	} //end switch of mode
}

// video::buffer_to_screen
// copies all or a portion of the video buffer to the screen
// viewstartx,viewstarty,viewwidth,viewheight define a rectangle which
//     can be used to draw only a portion of the buffer to screen,
//     and is used to draw viewscreens when we don't need a full update
// NOTE!! this function requires that you pass it a rectangle which is
// a multiple of four WIDE, or it will NOT draw correctly
// This is designed this way with the assumption that screen draws are
// the slowest thing we can possible do.
void video::buffer_to_screen(long viewstartx,long viewstarty,
                             long viewwidth, long viewheight)
{
	SDL_Surface *render;

        //buffers: update the screen (swap some buffers :)
	//      SDL_BlitSurface(screen,NULL,window,NULL);
	//      SDL_UpdateRect(window,0,0,320,200);
     
      	SDL_BlitSurface(screen,NULL,E_Screen->screen,NULL);
	render = (SDL_Surface *)E_Screen->RenderAndReturn(viewstartx,viewstarty,viewwidth,viewheight);
	SDL_BlitSurface(fontbuffer,NULL,render,NULL);
	E_Screen->Swap(viewstartx,viewstarty,viewwidth,viewheight);
}

//buffers: like buffer_to_screen but automaticaly swaps the entire screen
void video::swap(void)
{
	//SDL_UpdateRect(screen,0,0,screen_width,screen_height);
	buffer_to_screen(0,0,320,200);
}

extern void do_clear_ints();
extern void do_restore_ints();

//buffers: PORT: #pragma aux do_clear_ints = "cli";
// Zardus: PORT: #pragma aux do_restore_ints = "sti";

void video::clear_ints()
{
	//buffers: PORT: won't link with this in: do_clear_ints();
}
void video::restore_ints()
{
	//buffers: PORT: won't link with this in: do_restore_ints();
}

//buffers: get pixel's RGB values if you have XY
void video::get_pixel(int x, int y, Uint8 *r, Uint8 *g, Uint8 *b)
{
	Uint32 col = 0;
	Uint8 q=0,w=0,e=0;

	x*=mult;
	y*=mult;

	char *p = (char *)screen->pixels;
	p += screen->pitch*y;
	p += screen->format->BytesPerPixel*x;

	memcpy(&col,p,screen->format->BytesPerPixel);

	SDL_GetRGB(col,screen->format,&q,&w,&e);
	*r=q;
	*g=w;
	*b=e;
}

//buffers: get pixel index if you have XY.
int video::get_pixel(int x, int y, int *index)
{
	Uint8 r,g,b;
	int tr,tg,tb;
	int i;

	get_pixel(x,y,&r,&g,&b);
	r /= 4;
	g /= 4;
	b /= 4;

	for(i=0;i<256;i++)
	{
		query_palette_reg(i,&tr,&tg,&tb);
		if(r==tr && g==tg && b==tb)
		{
			*index = i;
			return i;
		}
	}

	printf("DEBUG: could not find color: %d %d %d\n",r,g,b);
	return 0;
}

//buffers: get pixel index if you have an buffer offset
int video::get_pixel(int offset)
{
	int x,y,t;

	y = offset/320;
	x = offset-y*320;

	return get_pixel(x,y,&t);
}


// ***************************************************************************
// Fading routines! Thanks, Erik!
// ****************************************************************************
void video::FadeBetween24(
//Show transition between two screens at 'amount' between them.
//
//'pSurface' is the surface you want to apply the fade to,
//'fadeFrom' is a copy of what the old screen looks like, and
//'fadeTo' is a copy of what the normal screen looks like,
// neither faded in or out, but just normal.
//NOTE: fadeFrom, fadeTo, and pSurface must be the same size and dimensions.
//
//Params:
	SDL_Surface* pSurface, const Uint8* fadeFromRGB, const Uint8* fadeToRGB,
	const int amount)	//(in) mixing ratio (in increments of 'fadeDuration')
{
	Uint8 *pw = (Uint8 *)pSurface->pixels;
	Uint32 size = pSurface->pitch * pSurface->h;

	const int nOldAmt = fadeDuration-amount;

	const Uint8 *pFrom = fadeFromRGB;
	const Uint8 *pTo = fadeToRGB;
	
	//Mix pixels in "from" and "to" images by 'amount'
	Uint8 *pStop = pw + size;
	while (pw != pStop)
	{
		*(pw++) = (nOldAmt * *(pFrom++) + amount * *(pTo++)) / fadeDuration;
		*(pw++) = (nOldAmt * *(pFrom++) + amount * *(pTo++)) / fadeDuration;
		*(pw++) = (nOldAmt * *(pFrom++) + amount * *(pTo++)) / fadeDuration;
		pw++; pFrom++; pTo++;
	}

	SDL_UpdateRect (pSurface, 0, 0, 0, 0);
}

//*****************************************************************************
int video::FadeBetween(
//Fade between two screens.
//Time effect to be independent of machine speed.
	SDL_Surface* pOldSurface,	//(in)	Surface that contains starting image.
	SDL_Surface* pNewSurface,	//(in)	Image that destination surface will change to.
	SDL_Surface* DestSurface)	//	surface which is the destination
{
	bool bOldNull = false, bNewNull = false;
	int i = 1;

	//Set NULL pointers to temporary black screens
	//(for simple fade-in/out effects).
	if (!pOldSurface)
	{
		bOldNull = true;
		pOldSurface = SDL_CreateRGBSurface(SDL_SWSURFACE,
			CX_SCREEN, CY_SCREEN, 24, 0, 0, 0, 0);
		SDL_FillRect(pOldSurface,NULL,0);
	}
	if (!pNewSurface)
	{
		bNewNull = true;
		pNewSurface = SDL_CreateRGBSurface(SDL_SWSURFACE,
			CX_SCREEN, CY_SCREEN, 24, 0, 0, 0, 0);
		SDL_FillRect(pNewSurface,NULL,0);
	}
	if (bOldNull && bNewNull) return 0;	//nothing to do

	/* Lock the screen for direct access to the pixels */
	if ( SDL_MUSTLOCK(pOldSurface) ) {
		if ( SDL_LockSurface(pOldSurface) < 0 ) {
			return 0;
		}
	}
	
	//The new surface shouldn't need a lock unless it is somehow a screen surface.
	ASSERT(!SDL_MUSTLOCK(pNewSurface)); 

	//The dimensions and format of the old and new surface must match exactly.
	ASSERT(pOldSurface->pitch == pNewSurface->pitch);
	ASSERT(pOldSurface->w == pNewSurface->w);
	ASSERT(pOldSurface->h == pNewSurface->h);
	ASSERT(pOldSurface->format->Rmask == pNewSurface->format->Rmask);
	ASSERT(pOldSurface->format->Rshift == pNewSurface->format->Rshift);
	ASSERT(pOldSurface->format->Rloss == pNewSurface->format->Rloss);
	ASSERT(pOldSurface->format->Gmask == pNewSurface->format->Gmask);
	ASSERT(pOldSurface->format->Gshift == pNewSurface->format->Gshift);
	ASSERT(pOldSurface->format->Gloss == pNewSurface->format->Gloss);
	ASSERT(pOldSurface->format->Bmask == pNewSurface->format->Bmask);
	ASSERT(pOldSurface->format->Bshift == pNewSurface->format->Bshift);
	ASSERT(pOldSurface->format->Bloss == pNewSurface->format->Bloss);
	ASSERT(pOldSurface->format->Rshift == pNewSurface->format->Rshift);
	ASSERT(pOldSurface->format->BytesPerPixel == pNewSurface->format->BytesPerPixel);

	//Extract RGB pixel values from each image.
	const int bpp = pNewSurface->format->BytesPerPixel;
	ASSERT(bpp==4);	//24-bit color only supported

	Uint32 size = pOldSurface->pitch * pOldSurface->h;
	Uint8 *colorsf, *colorst;
	colorsf = new Uint8[size];
	colorst = new Uint8[size];

	Uint8 *prf = (Uint8 *)pOldSurface->pixels, *prt = (Uint8 *)pNewSurface->pixels;
	memcpy(colorsf, prf, size);
	memcpy(colorst, prt, size);

	//Fade from old to new surface.  Effect takes constant time.
	Uint32
		dwFirstPaint = SDL_GetTicks(),
		dwNow = dwFirstPaint;
	do {
		FadeBetween24(DestSurface,colorsf,colorst,
				dwNow - dwFirstPaint + 50);	//allow first frame to show some change
		E_Screen->Swap(0,0,320,200);
		dwNow = SDL_GetTicks();

		get_input_events(POLL);
		if (query_key_press_event())
		{
			i = -1;
			break;
		}
	} while (dwNow - dwFirstPaint + 50 < fadeDuration);	// constant-time effect

	if ( SDL_MUSTLOCK(pNewSurface) ) {
		SDL_UnlockSurface(pNewSurface);
	}

	//Show new screen entirely.
	SDL_BlitSurface(pNewSurface, NULL, pOldSurface, NULL);
	SDL_UpdateRect(pOldSurface,0,0,CX_SCREEN, CY_SCREEN);
	E_Screen->Swap(0,0,320,200);
	
	//Clean up.
	delete [] colorsf;
	delete [] colorst;

	if (bOldNull)
		SDL_FreeSurface(pOldSurface);
	if (bNewNull)
		SDL_FreeSurface(pNewSurface);

	return i;
}

int video::fadeblack(bool way)
{

	SDL_Surface *black = NULL;
	int c = 0;
	
	if (retreat)
	{
		while (!black)
		{
			black = SDL_CreateRGBSurface(SDL_SWSURFACE, 320 * font_mult, 200 * font_mult, 32, 0, 0, 0, 0);
		}
		retreat = 0;
	}

	black = SDL_CreateRGBSurface(SDL_SWSURFACE, 320 * font_mult, 200 * font_mult, 32, 0, 0, 0, 0);

	c = SDL_MapRGB(black->format, 0, 0, 0);

	SDL_Surface *render;
	int i;

	SDL_BlitSurface(screen,NULL,E_Screen->screen,NULL);
	render = (SDL_Surface *)E_Screen->RenderAndReturn(0,0,320,200);
	SDL_BlitSurface(fontbuffer,NULL,render,NULL);

	SDL_FillRect(black, NULL, c);

	if (way == 1) i = FadeBetween(black, render, render); // fade from black
	if (way == 0) i = FadeBetween(render, black, render); // fade to black

	SDL_FreeSurface(black);
	return i;
}

