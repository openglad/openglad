// Video object code
#include "graph.h"
#include <fstream.h>
#include <stdlib.h>
#include <math.h>
// Z's script: #include <dos.h>
// Z's script: #include <i86.h>

#define VIDEO_BUFFER_WIDTH 320
#define VIDEO_WIDTH 320
#define VIDEO_SIZE 64000

//buffers: PORT: REGS, bleh bleh: union REGS inregs,outregs;
unsigned char * videoptr = (unsigned char *) VIDEO_LINEAR;

// Zardus: PORT: just a function to return the mult value
int get_mult()
{
	return myscreen->mult;
}

// Define some palettes to use in (video) screen ..

video::video()
{
	long i;
	char temp[5];

	strcpy(temp,get_cfg_item("graphics","pdouble"));
	if(strcmp(temp,"on")==0)
		pdouble = 1;
	else
		pdouble = 0;

	//buffer: set vars according to pdouble
	if(pdouble) {
		mult = 2;
		screen_width = 640;
		screen_height = 400;
	} else {
		mult = 1;
		screen_width = 320;
		screen_height = 200;
	}

	//buffers: PORT: no need: save_palette(dospalette);
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
	for (i=32; i < 256; i++)
	{
		bluepalette[i*3+0] /= 2;
		bluepalette[i*3+1] /= 2;
	}

	//buffers: screen init
	SDL_Init(SDL_INIT_VIDEO);

	strcpy(temp,get_cfg_item("graphics","fullscreen"));
	if(strcmp(temp,"on")==0)
		screen = SDL_SetVideoMode (screen_width, screen_height, 16, SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
	else
		screen = SDL_SetVideoMode (screen_width, screen_height, 16, SDL_HWSURFACE | SDL_DOUBLEBUF);
		
}

video::~video()
{
	SDL_Quit();
}

unsigned char * video::getbuffer()
{
  return &videobuffer[0];
}

void video::clearscreen()
{
	//buffers: PORT: clear the offscreen buffer, not the screen.
	//buffers: we are going to see if we can double buf everything.
	SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 0, 0, 0));
	SDL_UpdateRect(screen,0,0,320*mult,200*mult);
}

void video::clearbuffer()
{
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format,0,0,0));
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
			char *header)
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
  long i;

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
	unsigned long i,j, temp;
	SDL_Rect rect;
	int r,g,b;

	// Zardus: FIX: small check to make sure we're not trying to put in antimatter or something
	if (xsize < 0 || ysize < 0 || startx < 0 || starty < 0) return;

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
	SDL_UpdateRect(screen,x,y,1,1);
}

//buffers: PORT: this draws a point in the offscreen buffer
//buffers: PORT: used for all the funcs that draw stuff in the offscreen buf
void video::pointb(long x, long y, unsigned char color)
{
	int r,g,b;
	int *address = (int *) ((Uint8 *)screen->pixels + y*screen->pitch +
                              x * screen->format->BytesPerPixel);
	int c;
	SDL_Rect rect;

	//buffers: this does bound checking (just to be safe)
	if(x<0 || x>319 || y<0 || y>199)
		return;

	query_palette_reg(color,&r,&g,&b);

	c = SDL_MapRGB(screen->format, r*4, g*4, b*4);

	if(pdouble) {
		rect.x = x*mult;
		rect.y = y*mult;
		rect.w = mult;
		rect.h = mult;
		SDL_FillRect(screen,&rect,c);
	} else {
		SDL_LockSurface(screen);
		*address = c;
		SDL_UnlockSurface(screen);
	}
}											
// Place a horizontal line on the screen.
//buffers: this func originally drew directly to the screen
void video::hor_line(long x, long y, long length, unsigned char color)
{
/* buffers:
	int i;
	for (i = 0; i < length; i++)
	{
		point (x + i, y, color);
	}

	SDL_UpdateRect(screen,x,y,length,0);
*/
	//buffers: we always want to draw to the back buffer now
	hor_line(x,y,length,color,1);
}

void video::hor_line(long x, long y, long length, unsigned char color, long tobuffer)
{
	unsigned long num, i;

	if (!tobuffer)
	{
		hor_line(x,y,length,color);
		return;
	}

	//buffers: PORT: num = x + (VIDEO_WIDTH*y);
	for (i = 0; i < length; i++)
	{
		pointb(x+i,y,color);
	
		//buffers: PORT: if (num>0 && num<VIDEO_SIZE)
		//buffers: PORT: videobuffer[num] = color;
		//buffers: PORT num ++;
	}
}


// Place a vertical line on the screen.
// buffers: this func originally drew directly to the screen
void video::ver_line(long x, long y, long length, unsigned char color)
{
/* buffers:
	int i;

	for (i = 0; i < length; i++)
	{
		point (x, y + i, color);
	}
*/

	//buffers: we always want to draw to the back buffer now
	ver_line(x,y,length,color,1);
}

void video::ver_line(long x, long y, long length, unsigned char color, long tobuffer)
{
	unsigned long num, i;

	if (!tobuffer)  
	{
		ver_line(x,y,length,color);
		return;
	}

	//buffers: PORT: num = x + (VIDEO_WIDTH*y);
	for (i = 0; i < length; i++)
	{
		pointb(x,y+i,color);
	
		//buffers: PORT: if (num>0 && num<VIDEO_SIZE)
		//buffers: PORT:	videobuffer[num] = color;
		//buffers: POT: num += VIDEO_WIDTH;
	}
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
void video::putdata(long startx, long starty, long xsize, long ysize, unsigned char  *sourcedata)
{
  unsigned long curx, cury;
  unsigned char curcolor;
  unsigned long num = 0;
  unsigned long targ = 0;
  
  for(cury = starty;cury < starty +ysize;cury++)
    for (curx = startx; curx < startx +xsize; curx++)
    {
      curcolor = sourcedata[num++];
      if (!curcolor) continue;
      //buffers: PORT: targ = (curx + (cury*VIDEO_WIDTH));
      //buffers: PORT: if (targ>0 && targ<VIDEO_SIZE)
	//buffers: PORT: videoptr[targ] = curcolor;
	point(curx,cury,curcolor);//buffers: PORT: draw the point
    }
}

//video::putdata
//draws objects to screen, respecting transparency
//used by text
void video::putdata(long startx, long starty, long xsize, long ysize, unsigned char  *sourcedata, unsigned char color)
{
	unsigned long curx, cury;
	unsigned char curcolor;
	unsigned long num = 0;
	unsigned long targ = 0;
  
	for(cury = starty;cury < starty +ysize;cury++)
		for (curx = startx; curx < startx +xsize; curx++)
		{
			curcolor = sourcedata[num++];
			if (!curcolor) continue;
			//if (curcolor>=248) curcolor = color+(curcolor-248);
			if (curcolor>247) curcolor = color;
			//buffers: PORT: targ = (curx + (cury*VIDEO_WIDTH));
			//buffers: PORT: if (targ>0 && targ<VIDEO_SIZE)
			//buffers: PORT: videoptr[targ] = curcolor;
			point(curx,cury,curcolor);
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
		      unsigned char * sourceptr)
{
	int i,j,num;
  long xmin=0, xmax=tilewidth, ymin=0, ymax=tileheight;
  unsigned long targetshifter,sourceshifter; //these let you wrap around in the arrays
  long totrows,rowsize; //number of rows and width of each row in the source
  unsigned long offssource,offstarget; //offsets into each array, for clipping and wrap
  unsigned char * videobufptr = &videobuffer[0];
  unsigned char * sourcebufptr = &sourceptr[0];
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

  targetshifter = VIDEO_BUFFER_WIDTH - rowsize; //this will wrap the target around
  sourceshifter = tilewidth - rowsize;  //this will wrap the source around

  offstarget = (tilestarty*VIDEO_BUFFER_WIDTH) + tilestartx; //start at u-l position
  offssource = (ymin * tilewidth) + xmin; //start at u-l position

//buffers: draws graphic. actually uses the above bound checking now (7/18/02)
	num=0;
	for(i=ymin;i<ymax;i++) {
		for(j=xmin;j<xmax;j++) {
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
	int i,j,num;
	long xmin=0, xmax=tilewidth, ymin=0, ymax=tileheight;
	unsigned long targetshifter,sourceshifter; //these let you wrap around in the arrays
	long totrows,rowsize; //number of rows and width of each row in the source
	unsigned long offssource,offstarget; //offsets into each array, for clipping and wrap
	unsigned char * videobufptr = &videobuffer[0];
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

	targetshifter = VIDEO_BUFFER_WIDTH - rowsize; //this will wrap the target around
	sourceshifter = tilewidth - rowsize;  //this will wrap the source around

	offstarget = (tilestarty*VIDEO_BUFFER_WIDTH) + tilestartx; //start at u-l position
	offssource = (ymin * tilewidth) + xmin; //start at u-l position

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
			  unsigned char  *sourceptr, unsigned char teamcolor)
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
      if (curcolor > (unsigned char) 247) curcolor = (unsigned char) (teamcolor+(255-curcolor));
      //buffers: PORT: videobuffer[buffoff++] = curcolor;
      	pointb(walkerstartx+curx,walkerstarty+cury,curcolor);
    }
    walkoff += walkshift;
    buffoff += buffshift;
  }
}

void video::walkputbuffer(long walkerstartx, long walkerstarty,
			  long walkerwidth, long walkerheight,
			  long portstartx, long portstarty,
			  long portendx, long portendy,
			  unsigned char  *sourceptr, unsigned char teamcolor,
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
  // Zardus: they used to do it by buffoff. we'll do it with xval and yval
  //buffoff   = (walkerstarty*VIDEO_BUFFER_WIDTH) + walkerstartx;     
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
	  
	  if (curcolor > (unsigned char) 247) curcolor = (unsigned char) (teamcolor+(255-curcolor));

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
	  
	  if (curcolor > (unsigned char) 247) curcolor = (unsigned char) (teamcolor+(255-curcolor));

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
	  
	  if (shifttype == SHIFT_RANDOM) 
	    videobuffer[buffoff++] = videobuffer[buffoff+random(2)];
	  
	  else if (shifttype == SHIFT_LIGHTER)
	  {
	    bufcolor = videobuffer[buffoff];
	    if ((bufcolor%8)!=0) bufcolor--;
	    videobuffer[buffoff++] = bufcolor;
	  }
	  
	  else if (shifttype == SHIFT_DARKER)
	  {
	    bufcolor = videobuffer[buffoff];
	    if ((bufcolor%7)!=0) bufcolor++;
	    videobuffer[buffoff++] = bufcolor;
	  }
	  
	  else if (shifttype == SHIFT_BLOCKY)
	  {
	    if (cury%2) videobuffer[buffoff++] = videobuffer[buffoff-VIDEO_BUFFER_WIDTH];
	    else if (curx%2) videobuffer[buffoff++] = videobuffer[buffoff-1];
	    else buffoff++;
	  }
	  
	  else 
	    videobuffer[buffoff++] = videobuffer[buffoff+shift];
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
	  if (curcolor > (unsigned char) 247) curcolor = (unsigned char) (teamcolor+(255-curcolor));
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
	//buffers: update the screen (swap some buffers :)
	SDL_UpdateRect(screen,viewstartx*mult,viewstarty*mult,viewwidth*mult,viewheight*mult);
}

//buffers: like buffer_to_screen but automaticaly swaps the entire screen
void video::swap(void)
{
	SDL_UpdateRect(screen,0,0,screen_width,screen_height);
}

extern void do_clear_ints();
extern void do_restore_ints();

//buffers: PORT: #pragma aux do_clear_ints = \
//buffers: PORT:   "cli";

#pragma aux do_restore_ints = "sti";

void video::clear_ints()
{
  //buffers: PORT: won't link with this in: do_clear_ints();
}
void video::restore_ints()
{
  //buffers: PORT: won't link with this in: do_restore_ints();
}
