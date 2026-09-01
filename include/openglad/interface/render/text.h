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

#include <openglad/interface/base.h>
#include <openglad/gameplay/pixie_data.h>
#include <optional>
#include <string>
#include <string_view>

class screen;
class viewscreen;

namespace og::ui
{
struct PromptButtonRect
{
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

struct PromptActionLayout
{
	PromptButtonRect cancel;
	PromptButtonRect accept;
};

inline constexpr int kPromptActionGap = 12;

// The new-company naming screen establishes the prompt grid: equal-width
// CANCEL/ACCEPT faces, a 12px gutter, and outer edges aligned to the field.
[[nodiscard]] constexpr PromptActionLayout prompt_action_layout(
    int x, int y, int field_width, int field_height)
{
	const int usable_width = field_width > kPromptActionGap
	    ? field_width - kPromptActionGap
	    : 0;
	const int button_width = usable_width / 2;
	const int action_y = y + field_height + 4;
	return PromptActionLayout{
	    .cancel = {x, action_y, button_width, 14},
	    .accept = {x + button_width + kPromptActionGap,
	               action_y, button_width, 14},
	};
}
} // namespace og::ui

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
		Sint32 write_formatted(Sint32 x, Sint32 y, const char* str, unsigned char color, bool center, bool shadow, bool use_alpha, Uint8 alpha);
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string, short to_buffer);
		Sint32 write_xy(Sint32 x, Sint32 y, std::string_view string, unsigned char color, short to_buffer);
		Sint32 write_xy_flat(Sint32 x, Sint32 y, std::string_view string,
		                       unsigned char color, short to_buffer);
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
	    // Cached glyph geometry. The font pixie these mirror is a shared
	    // lazily-loaded static, so a text built before the pixie could be
	    // read keeps zeros here forever while every later text sees the real
	    // 9x12 -- and a zero-sized glyph blit paints nothing at all (issue
	    // #259: blank dialog headers over an intact body). Every drawing and
	    // measuring entry point re-reads the pixie through sync_geometry()
	    // first, so these can never go stale against the font in hand.
	    short sizex, sizey;

	private:
	    void sync_geometry();
};

// Free shared font pixie data loaded by text constructors.
// Safe to call multiple times.
void text_shutdown();
