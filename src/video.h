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
#ifndef __VIDEO_H
#define __VIDEO_H

// The definition of the VIDEO class

#include "base.h"

class video
{
	public:
		video();
		~video();
		void clearscreen();
		void clearbuffer();
		void clearfontbuffer();
		void clearfontbuffer(int x, int y, int w, int h);
	
		unsigned char * getbuffer();
		void putblack(int startx, int starty, int xsize, int ysize);
		void fastbox(int startx, int starty, int xsize, int ysize, unsigned char color);
		void fastbox(int startx, int starty, int xsize, int ysize, unsigned char color, unsigned char flag);
		void point(int x, int y, unsigned char color);
		//buffers: PORT: added below prototype
		void pointb(int x, int y, unsigned char color);
		void pointb(int offset, unsigned char color);
		void pointb(int x, int y, int r, int g, int b);
		void hor_line(int x, int y, int length, unsigned char color);
		void ver_line(int x, int y, int length, unsigned char color);
		void hor_line(int x, int y, int length, unsigned char color, int tobuffer);
		void ver_line(int x, int y, int length, unsigned char color, int tobuffer);
		void do_cycle(int curmode, int maxmode);
		void putdata(int startx, int starty, int xsize, int ysize,
		             unsigned char  *sourcedata);
		void putdatatext(int startx, int starty, int xsize, int ysize,
		                             unsigned char  *sourcedata);
		void putdata(int startx, int starty, int xsize, int ysize,
		             unsigned char  *sourcedata, unsigned char color);

		 void putdatatext(int startx, int starty, int xsize, int ysize,
		                              unsigned char  *sourcedata, unsigned char color);

		void putbuffer(int tilestartx, int tilestarty,
		               int tilewidth, int tileheight,
		               int portstartx, int portstarty,
		               int portendx, int portendy,
		               unsigned char * sourceptr);
		void putbuffer(int tilestartx, int tilestarty,
		               int tilewidth, int tileheight,
		               int portstartx, int portstarty,
		               int portendx, int portendy,
		               SDL_Surface *sourceptr);
		void walkputbuffer(int walkerstartx, int walkerstarty,
		                   int walkerwidth, int walkerheight,
		                   int portstartx, int portstarty,
		                   int portendx, int portendy,
		                   unsigned char  *sourceptr, unsigned char teamcolor);
		void walkputbuffertext(int walkerstartx, int walkerstarty,
                                   int walkerwidth, int walkerheight,
                                   int portstartx, int portstarty,
                                   int portendx, int portendy,
                                   unsigned char  *sourceptr, unsigned char teamcolor);


		void walkputbuffer(int walkerstartx, int walkerstarty,
		                   int walkerwidth, int walkerheight,
		                   int portstartx, int portstarty,
		                   int portendx, int portendy,
		                   unsigned char  *sourceptr, unsigned char teamcolor,
		                   unsigned char mode, int invisibility,
		                   unsigned char outline, unsigned char shifttype);
		void buffer_to_screen(int viewstartx,int viewstarty,
		                      int viewwidth, int viewheight);

		void draw_box(int x1, int y1, int x2, int y2, unsigned char color, int filled);
		void draw_box(int x1, int y1, int x2, int y2, unsigned char color, int filled, int tobuffer);
		void draw_button(int x1, int y1, int x2, int y2, int border);
		void draw_button(int x1, int y1, int x2, int y2, int border, int tobuffer);
		int draw_dialog(int x1, int y1, int x2, int y2, const char *header);
		void draw_text_bar(int x1, int y1, int x2, int y2);

		void swap(void);

		void clear_ints();
		void restore_ints();

		void get_pixel(int x, int y, Uint8 *r, Uint8 *g, Uint8 *b);
		int get_pixel(int x, int y, int *index);
		int get_pixel(int offset);

		// Fading code: (thanks Erik!)
		void FadeBetween24(SDL_Surface *, const Uint8 *, const Uint8 *, const int);
		int FadeBetween(SDL_Surface *, SDL_Surface *, SDL_Surface *);
		int fadeblack(bool);

		int fadeDuration;

		unsigned char ourpalette[768]; // our standard glad palette
		unsigned char redpalette[768]; // for 'faded' backgrounds during menus
		unsigned char bluepalette[768]; // for special effects like time-freeze
		unsigned char dospalette[768]; // store the dos palette so we can restore it later

		unsigned char videobuffer[64000]; //our new unified video buffer
		short cyclemode; //color cycling on or off


		//buffers: screen vars
		SDL_Surface *window;
		int screen_width,screen_height,fullscreen;
		int pdouble, mouse_mult,mult,font_mult;
};

#endif
