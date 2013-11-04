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
#ifndef __PIXIE_H
#define __PIXIE_H

// Definition of PIXIE class

#include "base.h"

class pixie
{
	public:
		pixie(const PixieData& data);
		pixie(const PixieData& data, int doaccel);
		virtual ~pixie();
		short setxy(short x, short y);
		virtual short move (short x, short y);
		short draw (viewscreen  *view_buf);
		short draw (short x, short y, viewscreen  *view_buf);
		short drawMix (viewscreen *view_buf);
		short drawMix (short x, short y, viewscreen *view_buf);
		short put_screen(short x, short y);
		short put_screen(short x, short y, unsigned char alpha);
		void init_sdl_surface(void);
		void set_accel(int a);
		void set_data(const PixieData& data);
		short sizex, sizey;
		short xpos, ypos;
		//buffers: is SDL_Surface acceleration on/off, 1/0
		int accel;
		short on_screen();                                                                // on ANY viewscreen?
		short on_screen(viewscreen  *viewp);  // on a specific viewscreen?
		
	protected:
		unsigned short size;
		unsigned char  *bmp,  *oldbmp;
		//buffers: same data as bmp but in a convient SDL_Surface
		SDL_Surface *bmp_surface;
};

#endif
