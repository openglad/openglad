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

// Definition of TEXT class

#include <openglad/legacy/base.h>
#include <openglad/data/pixie_data.h>
#include <optional>
#include <string>
#include <string_view>

class screen;
class viewscreen;

class text
{
	public:
		friend class vbutton;
		text(const char * filename);
		Sint32 query_width(std::string_view string); // returns width, in pixels
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string);
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string, unsigned char color);
		Sint32 write_xy(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...);
		Sint32 write_xy_shadow(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...);
		Sint32 write_xy_center(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...);
		Sint32 write_xy_center_alpha(Sint32 x, Sint32 y, unsigned char color, Uint8 alpha, const char* formatted_string, ...);
		Sint32 write_xy_center_shadow(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...);
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string, short to_buffer);
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string, unsigned char color, short to_buffer);
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string, viewscreen *whereto);
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string, unsigned char color, viewscreen *whereto);
		Sint32 write_y(Sint32 y, std::string_view string);
		Sint32 write_y(Sint32 y, std::string_view string, unsigned char color);
		Sint32 write_y(Sint32 y, std::string_view string, short to_buffer);
		Sint32 write_y(Sint32 y, std::string_view string, unsigned char color, short to_buffer);
		Sint32 write_y(Sint32 y, std::string_view string, viewscreen *whereto);
		Sint32 write_y(Sint32 y, std::string_view string, unsigned char color, viewscreen *whereto);
		Sint32 write_char_xy(Sint32 x, Sint32 y, char letter);
		Sint32 write_char_xy(Sint32 x, Sint32 y, char letter, unsigned char color);
		Sint32 write_char_xy_alpha(Sint32 x, Sint32 y, char letter, unsigned char color, Uint8 alpha);
		Sint32 write_char_xy(Sint32 x, Sint32 y, char letter, short to_buffer);
		Sint32 write_char_xy(Sint32 x, Sint32 y, char letter, unsigned char color, short to_buffer);
		Sint32 write_char_xy(Sint32 x, Sint32 y, char letter, viewscreen *whereto);
		Sint32 write_char_xy(Sint32 x, Sint32 y, char letter, unsigned char color, viewscreen *whereto);
		char *input_string(Sint32 x, Sint32 y, short maxlength, const char *begin);
		char *input_string(Sint32 x, Sint32 y, short maxlength, const char *begin,
		                   unsigned char forecolor, unsigned char backcolor);
        char* input_string_ex(Sint32 x, Sint32 y, short maxlength, const char* message, const char *begin);
        char* input_string_ex(Sint32 x, Sint32 y, short maxlength, const char* message, const char *begin,
                          unsigned char forecolor, unsigned char backcolor);
        std::optional<std::string> input_string_value(Sint32 x, Sint32 y, short maxlength, const char* begin);
        std::optional<std::string> input_string_ex_value(Sint32 x, Sint32 y, short maxlength, const char* message, const char* begin);
		~text();

	    const PixieData* letters;
	    short sizex, sizey;
};

// Free shared font pixie data loaded by text constructors.
// Safe to call multiple times.
void text_shutdown();
