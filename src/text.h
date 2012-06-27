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
#ifndef __TEXT_H
#define __TEXT_H

// Definition of TEXT class

#include "base.h"

class text
{
	public:
		friend class vbutton;
		text(screen * myscreen);
		text(screen * myscreen, const char * filename);
		short query_width(const char *string); // returns width, in pixels
		short write_xy(short x, short y, const char  *string);
		short write_xy(short x, short y, const char  *string, unsigned char color);
		short write_xy(short x, short y, const char  *string, short to_buffer);
		short write_xy(short x, short y, const char  *string, unsigned char color, short to_buffer);
		short write_xy(short x, short y, const char  *string, viewscreen *whereto);
		short write_xy(short x, short y, const char  *string, unsigned char color, viewscreen *whereto);
		short write_y(short y, const char  *string);
		short write_y(short y, const char  *string, unsigned char color);
		short write_y(short y, const char  *string, short to_buffer);
		short write_y(short y, const char  *string, unsigned char color, short to_buffer);
		short write_y(short y, const char  *string, viewscreen *whereto);
		short write_y(short y, const char  *string, unsigned char color, viewscreen *whereto);
		short write_char_xy(short x, short y, char letter);
		short write_char_xy(short x, short y, char letter, unsigned char color);
		short write_char_xy(short x, short y, char letter, short to_buffer);
		short write_char_xy(short x, short y, char letter, unsigned char color, short to_buffer);
		short write_char_xy(short x, short y, char letter, viewscreen *whereto);
		short write_char_xy(short x, short y, char letter, unsigned char color, viewscreen *whereto);
		char *input_string(short x, short y, short maxlength, char *begin);
		char *input_string(short x, short y, short maxlength, char *begin,
		                   unsigned char forecolor, unsigned char backcolor);
		unsigned char convert_to_ascii(int scancode);
		~text();
		screen * screenp;

	protected:
		char  *letters;
		short sizex;
		short sizey;
};

#endif

