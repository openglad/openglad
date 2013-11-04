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
#include "text.h"

class video
{
	public:
		video();
		virtual ~video();
		
        void set_fullscreen(bool fullscreen);
        
		void clearbuffer();
		void clearbuffer(int x, int y, int w, int h);
		
		void clear_window();
	
		unsigned char * getbuffer();
		void putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize);
		void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color);
		void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color, unsigned char flag);
		void fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color);
		void point(Sint32 x, Sint32 y, unsigned char color);
		//buffers: PORT: added below prototype
		void pointb(Sint32 x, Sint32 y, unsigned char color);
		void pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha);
		void pointb(int offset, unsigned char color);
		void pointb(Sint32 x, Sint32 y, int r, int g, int b);
		void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color);
		void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color);
		void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer);
		void hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Uint8 alpha);
		void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer);
		void draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color);
		void do_cycle(Sint32 curmode, Sint32 maxmode);
		void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		             unsigned char  *sourcedata);
        void putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char  *sourcedata, unsigned char alpha);
		void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		                             unsigned char  *sourcedata);
		void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		             unsigned char  *sourcedata, unsigned char color);
           
		 void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		                              unsigned char  *sourcedata, unsigned char color);

		void putbuffer(Sint32 tilestartx, Sint32 tilestarty,
		               Sint32 tilewidth, Sint32 tileheight,
		               Sint32 portstartx, Sint32 portstarty,
		               Sint32 portendx, Sint32 portendy,
		               unsigned char * sourceptr);
		void putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
		               Sint32 tilewidth, Sint32 tileheight,
		               Sint32 portstartx, Sint32 portstarty,
		               Sint32 portendx, Sint32 portendy,
		               unsigned char * sourceptr, unsigned char alpha);
		void putbuffer(Sint32 tilestartx, Sint32 tilestarty,
		               Sint32 tilewidth, Sint32 tileheight,
		               Sint32 portstartx, Sint32 portstarty,
		               Sint32 portendx, Sint32 portendy,
		               SDL_Surface *sourceptr);
		void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   unsigned char  *sourceptr, unsigned char teamcolor);
		void walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   unsigned char  *sourceptr, unsigned char teamcolor);
		void walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
                                   Sint32 walkerwidth, Sint32 walkerheight,
                                   Sint32 portstartx, Sint32 portstarty,
                                   Sint32 portendx, Sint32 portendy,
                                   unsigned char  *sourceptr, unsigned char teamcolor);
		void walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
                                   Sint32 walkerwidth, Sint32 walkerheight,
                                   Sint32 portstartx, Sint32 portstarty,
                                   Sint32 portendx, Sint32 portendy,
                                   unsigned char  *sourceptr, unsigned char teamcolor, Uint8 alpha);


		void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   unsigned char  *sourceptr, unsigned char teamcolor,
		                   unsigned char mode, Sint32 invisibility,
		                   unsigned char outline, unsigned char shifttype);
		void buffer_to_screen(Sint32 viewstartx,Sint32 viewstarty,
		                      Sint32 viewwidth, Sint32 viewheight);

		void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled);
		void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled, Sint32 tobuffer);
		void draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h, unsigned char color, Uint8 alpha);
		void draw_button(const SDL_Rect& rect, Sint32 border);
		void draw_button_inverted(const SDL_Rect& rect);
		void draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h);
		void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border);
		void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border, Sint32 tobuffer);
        void draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, bool use_border, int base_color, int high_color = 15, int shadow_color = 11);
		Sint32 draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, const char *header);
		void draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2);
		
		void darken_screen();

		void swap(void);

		void get_pixel(int x, int y, Uint8 *r, Uint8 *g, Uint8 *b);
		int get_pixel(int x, int y, int *index);
		int get_pixel(int offset);
		
		bool save_screenshot();

		// Fading code: (thanks Erik!)
		void FadeBetween24(SDL_Surface *, const Uint8 *, const Uint8 *, const int);
		int FadeBetween(SDL_Surface *, SDL_Surface *, SDL_Surface *);
		int fadeblack(bool fade_in);

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
		int pdouble;
		
		text text_normal;
		text text_big;
};

#endif
