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
#pragma once

// Definition of PIXIE class

#include <openglad/interface/base.h>
#include <openglad/resources/pixie_data.h>

class viewscreen;

class pixie
{
	public:
		pixie(const PixieData& data);
		pixie(const PixieData& data, int doaccel);
		virtual ~pixie();
		pixie(const pixie&) = delete;
		pixie& operator=(const pixie&) = delete;
		pixie(pixie&&) = delete;
		pixie& operator=(pixie&&) = delete;
		short setxy(short x, short y);
		// Overloads to avoid implicit narrowing at call sites. These forward to the short-based API.
		short setxy(Sint32 x, Sint32 y) { return setxy(static_cast<short>(x), static_cast<short>(y)); }
		short setxy(Uint32 x, Uint32 y) { return setxy(static_cast<Sint32>(x), static_cast<Sint32>(y)); }
		short setxy(float x, float y) { return setxy(static_cast<Sint32>(x), static_cast<Sint32>(y)); }
		virtual short move (short x, short y);
		short move(Sint32 x, Sint32 y) { return move(static_cast<short>(x), static_cast<short>(y)); }
		short move(float x, float y) { return move(static_cast<Sint32>(x), static_cast<Sint32>(y)); }
		short draw (viewscreen  *view_buf);
		short draw (short x, short y, viewscreen  *view_buf);
		short draw(Sint32 x, Sint32 y, viewscreen* view_buf) { return draw(static_cast<short>(x), static_cast<short>(y), view_buf); }
		short draw(float x, float y, viewscreen* view_buf) { return draw(static_cast<Sint32>(x), static_cast<Sint32>(y), view_buf); }
		short drawMix (viewscreen *view_buf);
		short drawMix (short x, short y, viewscreen *view_buf);
		short drawMix(Sint32 x, Sint32 y, viewscreen* view_buf) { return drawMix(static_cast<short>(x), static_cast<short>(y), view_buf); }
		short drawMix(float x, float y, viewscreen* view_buf) { return drawMix(static_cast<Sint32>(x), static_cast<Sint32>(y), view_buf); }
		short put_screen(short x, short y);
		short put_screen(Sint32 x, Sint32 y) { return put_screen(static_cast<short>(x), static_cast<short>(y)); }
		short put_screen(float x, float y) { return put_screen(static_cast<Sint32>(x), static_cast<Sint32>(y)); }
		void init_sdl_surface(void);
		void set_accel(int a);
		void set_data(const PixieData& data);
		short sizex, sizey;
		short xpos, ypos;
		//buffers: accelerated-surface path on/off, 1/0
		int accel;
		short on_screen();                                                                // on ANY viewscreen?
		short on_screen(viewscreen  *viewp);  // on a specific viewscreen?
		const unsigned char* bmp_data() const { return bmp; }

	protected:
		unsigned short size;
		unsigned char  *bmp,  *oldbmp;
		//buffers: same data as bmp but in a convient SDL_Surface
		// SDL-owned accelerated surface handle (platform type-erased at interface boundary).
		void* bmp_surface = nullptr;
		// Video backend that owns bmp_surface. Needed for safe teardown when
		// current_session is temporarily pointing elsewhere.
		video* accel_video_ = nullptr;
};
