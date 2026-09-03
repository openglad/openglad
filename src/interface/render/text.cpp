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
#include <openglad/interface/render/text.h>
#include <algorithm>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/base.h>
#include <array>
#include <cstdarg>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

static PixieData letters1;
static PixieData letters_big;

static void erase_last_utf8_codepoint(char* value, std::size_t length)
{
    if (value == nullptr || length == 0)
        return;

    std::size_t erase_at = length - 1;
    while (erase_at > 0 &&
           (static_cast<unsigned char>(value[erase_at]) & 0xc0u) == 0x80u)
    {
        --erase_at;
    }
    value[erase_at] = '\0';
}

static std::span<const unsigned char> safe_glyph_span(const PixieData* font, unsigned char letter)
{
    if (font == nullptr || !font->valid() || font->frames == 0 || font->w == 0 || font->h == 0 || !font->data)
        return {};

    const std::size_t stride = static_cast<std::size_t>(font->w) * static_cast<std::size_t>(font->h);
    const std::size_t total = stride * static_cast<std::size_t>(font->frames);
    if (stride == 0 || total == 0)
        return {};

    auto idx = static_cast<std::size_t>(letter);
    if ((idx + 1) * stride > total)
    {
        // Font pixies don't necessarily contain all 256 ASCII glyphs.
        // Fall back to '?' if present; otherwise fall back to glyph 0.
        const unsigned char fallback = '?';
        idx = (fallback < font->frames) ? static_cast<std::size_t>(fallback) : 0u;
    }

    const std::size_t off = idx * stride;
    if (off + stride > total)
        return {};

    return {font->data.get() + off, stride};
}


text::text(const char * filename)
    : letters(nullptr), sizex(0), sizey(0)
{
    if(!letters1.valid())
        letters1 = read_pixie_file(TEXT_1);
    if(!letters_big.valid())
        letters_big = read_pixie_file(TEXT_BIG);

    std::string temp_filename = (filename && strlen(filename) >= 2) ? filename : "text.png";

    if(temp_filename == TEXT_BIG)
        letters = &letters_big;
    else
        letters = &letters1;
    sizex = letters->w;
    sizey = letters->h;
}

text::~text()
{
    // Shared font pixies are freed via text_shutdown().
}

// Re-read the glyph box from the font pixie in hand. The pixies are shared
// lazily-loaded statics: a text constructed before its file could be read
// cached a 0x0 box, and a later text's constructor loads the pixie for real
// without touching that stale copy. A 0-wide glyph blit clips to nothing, so
// a dialog header drawn through such a text came out blank while the body
// text -- a different text object, built after the load -- painted fine
// (issue #259). Called from every measuring and drawing entry point.
void text::sync_geometry()
{
    if (letters == nullptr)
        return;
    sizex = static_cast<short>(letters->w);
    sizey = static_cast<short>(letters->h);
}

void text_shutdown()
{
    letters1.free();
    letters_big.free();
}
Sint32 text::query_width(std::string_view string) // returns width, in pixels
{
	sync_geometry();
	std::size_t i = 0;
	Sint32 over = 0;

	if (sizex < 9) // small, monospaced font
		return static_cast<Sint32>(sizex + 1) * static_cast<Sint32>(string.size());

	while (i < string.size())
	{
		if (string[i] >= 65 && string[i] <= 93) // uppercase
			over += sizex;
		else
			over += sizex-1;
		i++;
	}

	return over;
}

Sint32 text::write_xy(Sint32 x, Sint32 y, std::string_view string, unsigned char color)
{
	sync_geometry();
	for (std::size_t i = 0; i < string.size(); i++)
	{
		const Sint32 xi = x + static_cast<Sint32>(i) * static_cast<Sint32>(sizex + 1);
		write_char_xy(xi, y, string[i], color);
	}
	return 1;
}

Sint32 text::write_formatted(Sint32 x, Sint32 y, const char* str,
                              unsigned char color, bool center, bool shadow,
                              bool use_alpha, Uint8 alpha)
{
	sync_geometry();
    Sint32 base_x = x;
    const std::size_t len = strlen(str);
    if (center)
        base_x -= (static_cast<Sint32>(len) * static_cast<Sint32>(sizex + 1)) / 2;
    for (std::size_t i = 0; str[i]; i++)
    {
        const Sint32 xi = base_x + static_cast<Sint32>(i) * static_cast<Sint32>(sizex + 1);
        if (shadow)
            write_char_xy(xi - 1, y + 1, str[i], static_cast<unsigned char>(PURE_BLACK + 2));
        if (use_alpha)
            write_char_xy_alpha(xi, y, str[i], color, alpha);
        else
            write_char_xy(xi, y, str[i], color);
    }
    return center ? 1 : static_cast<Sint32>(len) * static_cast<Sint32>(sizex + 1);
}

#define TEXT_VFORMAT()                                                          \
    std::array<char, 256> text_buffer;                                          \
    do {                                                                        \
        if (!formatted_string) return 0;                                        \
        va_list lst;                                                            \
        va_start(lst, formatted_string);                                        \
        vsnprintf(text_buffer.data(), text_buffer.size(), formatted_string, lst);\
        va_end(lst);                                                            \
    } while(0)

Sint32 text::write_xy(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...) {
    TEXT_VFORMAT();
    return write_formatted(x, y, text_buffer.data(), color, false, false, false, 0);
}

Sint32 text::write_xy_shadow(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...) {
    TEXT_VFORMAT();
    return write_formatted(x, y, text_buffer.data(), color, false, true, false, 0);
}

Sint32 text::write_xy_center(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...) {
    TEXT_VFORMAT();
    return write_formatted(x, y, text_buffer.data(), color, true, false, false, 0);
}

Sint32 text::write_xy_center_alpha(Sint32 x, Sint32 y, unsigned char color, Uint8 alpha, const char* formatted_string, ...) {
    TEXT_VFORMAT();
    return write_formatted(x, y, text_buffer.data(), color, true, false, true, alpha);
}

Sint32 text::write_xy_center_shadow(Sint32 x, Sint32 y, unsigned char color, const char* formatted_string, ...) {
    TEXT_VFORMAT();
    return write_formatted(x, y, text_buffer.data(), color, true, true, false, 0);
}

#undef TEXT_VFORMAT

Sint32 text::write_xy(Sint32 x, Sint32 y, std::string_view string)
{
	return write_xy(x, y, string, static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
}

Sint32 text::write_xy(Sint32 x, Sint32 y, std::string_view string, unsigned char color,
                     short to_buffer)
{
	sync_geometry();
	std::size_t i = 0;
	Sint32 over = 0;

	if (sizex < 9) // small, monospaced font
		while(i < string.size())
		{
			if (!to_buffer)
				write_char_xy(x + static_cast<Sint32>(i) * static_cast<Sint32>(sizex + 1), y, string[i], color);
			else
				write_char_xy(x + static_cast<Sint32>(i) * static_cast<Sint32>(sizex + 1), y, string[i], color, static_cast<short>(1));
			i++;
			over += (sizex + 1);
		}
	else // larger font, help out the lowercase ..
		while(i < string.size())
		{
			write_char_xy(x + over, y, string[i], color, static_cast<short>(1));
			if (string[i] >=65 && string[i] <= 92) // uppercase
				over += sizex;
			else // lowercase, other things
				over += sizex-1;
			i++;
		}

	return over;
}

Sint32 text::write_xy(Sint32 x, Sint32 y, std::string_view string, short to_buffer)
{
	return write_xy(x, y, string, static_cast<unsigned char>(DEFAULT_TEXT_COLOR), to_buffer);
}

Sint32 text::write_xy(Sint32 x, Sint32 y, std::string_view string, unsigned char color,
                     viewscreen *whereto)
{
	sync_geometry();
	for (std::size_t i = 0; i < string.size(); i++)
	{
		const Sint32 xi = x + static_cast<Sint32>(i) * static_cast<Sint32>(sizex + 1);
		if (!whereto)
			write_char_xy(xi, y, string[i], color);
		else
			write_char_xy(xi, y, string[i], color, whereto);
	}
	return 1;
}

Sint32 text::write_xy(Sint32 x, Sint32 y, std::string_view string, viewscreen *whereto)
{
	return write_xy(x, y, string, static_cast<unsigned char>(DEFAULT_TEXT_COLOR), whereto);
}

Sint32 text::write_y(Sint32 y, std::string_view string, unsigned char color)
{
	const Sint32 len = static_cast<Sint32>(string.size());
	const Sint32 xstart = (320 - len * static_cast<Sint32>(sizex + 1)) / 2;
	return write_xy(xstart, y, string, color);
}

Sint32 text::write_y(Sint32 y, std::string_view string)
{
	return write_y(y, string, static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
}

Sint32 text::write_y(Sint32 y, std::string_view string, unsigned char color,
                    short to_buffer)
{
	const Sint32 len = static_cast<Sint32>(string.size());
	const Sint32 xstart = (320 - len * static_cast<Sint32>(sizex + 1)) / 2;
	return write_xy(xstart, y, string, color, to_buffer);
}

Sint32 text::write_y(Sint32 y, std::string_view string, short to_buffer)
{
	return write_y(y, string, static_cast<unsigned char>(DEFAULT_TEXT_COLOR), to_buffer);
}

Sint32 text::write_y(Sint32 y, std::string_view string, unsigned char color,
                    viewscreen *whereto)
{
	const Sint32 len = static_cast<Sint32>(string.size());
	const Sint32 xstart = (320 - len * static_cast<Sint32>(sizex + 1)) / 2;
	if (!whereto)
		return write_xy(xstart, y, string, color);
	else
		return write_xy(xstart, y, string, color, whereto);
}

Sint32 text::write_y(Sint32 y, std::string_view string, viewscreen *whereto)
{
	if (!whereto)
		return write_y(y, string, static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
	else
		return write_y(y, string, static_cast<unsigned char>(DEFAULT_TEXT_COLOR), whereto);
}

// This version writes to the buffer and then writes the
// buffer to the screen .. (double buffered to eliminate flashing)
Sint32 text::write_char_xy(Sint32 x, Sint32 y, char letter, unsigned char color,
                          short to_buffer)
{
	sync_geometry();
	if (!to_buffer)
		return write_char_xy(x, y, letter, color);

	auto char_span = safe_glyph_span(letters, static_cast<unsigned char>(letter));
	if (char_span.empty())
		return 0;
	screen* const output = og::runtime::current_session->myscreen_;
	output->walkputbuffertext(
		x, y, sizex, sizey, 0, 0,
		output->canvas_w(), output->canvas_h(), char_span, color);
	//myscreen->buffer_to_screen(x, y, sizex + 4 - (sizex%4), sizey + 4 - (sizey%4) );
	return 1;
}

Sint32 text::write_xy_flat(Sint32 x, Sint32 y, std::string_view string,
                           unsigned char color, short to_buffer)
{
	sync_geometry();
	if (sizex <= 0 || sizey <= 0)
		return 0;

	screen* const output = og::runtime::current_session->myscreen_;
	std::vector<unsigned char> flat_glyph(
		static_cast<std::size_t>(sizex) * static_cast<std::size_t>(sizey));
	for (std::size_t i = 0; i < string.size(); ++i)
	{
		auto char_span = safe_glyph_span(
			letters, static_cast<unsigned char>(string[i]));
		if (char_span.empty())
			continue;
		std::transform(char_span.begin(), char_span.end(), flat_glyph.begin(),
		               [](unsigned char pixel) {
			               return pixel == 0 ? 0 : 255;
		               });

		const Sint32 xi = x + static_cast<Sint32>(i) *
			static_cast<Sint32>(sizex + 1);
		if (to_buffer)
		{
			output->walkputbuffertext(
				xi, y, sizex, sizey, 0, 0,
				output->canvas_w(), output->canvas_h(), flat_glyph, color);
		}
		else
		{
			output->putdatatext(xi, y, sizex, sizey, flat_glyph, color);
		}
	}
	return 1;
}

Sint32 text::write_char_xy(Sint32 x, Sint32 y, char letter, short to_buffer)
{
	sync_geometry();
	if (!to_buffer)
		return write_char_xy(x, y, letter, static_cast<unsigned char>(DEFAULT_TEXT_COLOR));

	auto char_span = safe_glyph_span(letters, static_cast<unsigned char>(letter));
	if (char_span.empty())
		return 0;
	screen* const output = og::runtime::current_session->myscreen_;
	output->walkputbuffertext(
		x, y, sizex, sizey, 0, 0,
		output->canvas_w(), output->canvas_h(), char_span,
		static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
	//myscreen->buffer_to_screen(x, y, sizex + 4 - (sizex%4), sizey + 4 - (sizey%4) );
	return 1;
}

Sint32 text::write_char_xy(Sint32 x, Sint32 y, char letter, unsigned char color)
{
	sync_geometry();
	auto char_span = safe_glyph_span(letters, static_cast<unsigned char>(letter));
	if (char_span.empty())
		return 0;
	og::runtime::current_session->myscreen_->putdatatext(x, y, sizex, sizey, char_span, color);
	return 1;
}

Sint32 text::write_char_xy_alpha(Sint32 x, Sint32 y, char letter, unsigned char color, Uint8 alpha)
{
	sync_geometry();
	auto char_span = safe_glyph_span(letters, static_cast<unsigned char>(letter));
	if (char_span.empty())
		return 0;
	screen* const output = og::runtime::current_session->myscreen_;
	output->walkputbuffertext_alpha(
		x, y, sizex, sizey, 0, 0,
		output->canvas_w(), output->canvas_h(),
		char_span, color, alpha);
	return 1;
}

Sint32 text::write_char_xy(Sint32 x, Sint32 y, char letter)
{
	sync_geometry();
	auto char_span = safe_glyph_span(letters, static_cast<unsigned char>(letter));
	if (char_span.empty())
		return 0;
	og::runtime::current_session->myscreen_->putdatatext(x, y, sizex, sizey, char_span);
	return 1;
}

Sint32 text::write_char_xy(Sint32 x, Sint32 y, char letter, unsigned char color,
                          viewscreen *whereto)
{
	sync_geometry();
	auto char_span = safe_glyph_span(letters, static_cast<unsigned char>(letter));
	if (char_span.empty())
		return 0;
	if (!whereto)
		og::runtime::current_session->myscreen_->putdatatext(x, y, sizex, sizey, char_span, color);
	else
				og::runtime::current_session->myscreen_->walkputbuffertext(x+whereto->xloc, y+whereto->yloc, sizex, sizey,
				                       whereto->xloc,whereto->yloc,whereto->endx, whereto->endy,
				                       char_span, color);
	//         myscreen->buffer_to_screen(x+whereto->xloc, y+whereto->yloc,
	//           (sizex + 4 - (sizex%4)), (sizey + 4 - (sizey%4)) );
	return 1;
}

Sint32 text::write_char_xy(Sint32 x, Sint32 y, char letter, viewscreen *whereto)
{
	sync_geometry();
	auto char_span = safe_glyph_span(letters, static_cast<unsigned char>(letter));
	if (char_span.empty())
		return 0;
	if (!whereto)
		og::runtime::current_session->myscreen_->putdatatext(x, y, sizex, sizey, char_span);
	else
				og::runtime::current_session->myscreen_->walkputbuffertext(x+whereto->xloc, y+whereto->yloc, sizex, sizey,
			                       whereto->xloc,whereto->yloc,whereto->endx, whereto->endy,
			                       char_span, static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
	//         myscreen->buffer_to_screen(x+whereto->xloc, y+whereto->yloc,
	//           (sizex + 4 - (sizex%4)), (sizey + 4 - (sizey%4)) );
	return 1;
}

// This version passes DARK_BLUE and a grey color as defaults ..
char * text::input_string(Sint32 x, Sint32 y, short maxlength, const char *begin)
{
	return input_string(x, y, maxlength, begin, DARK_BLUE, 13);
}

// Input_string reads a string from the keyboard, displaying it
// at screen position x,y.  The maximum length of the string
// is maxlength, and any string in 'begin' will automatically be
// entered at the start.  Fore- and backcolor are used for the
// text foreground and background color
char * text::input_string(Sint32 x, Sint32 y, short maxlength, const char *begin,
                          unsigned char forecolor, unsigned char backcolor)
{
	short current_length, i;
	short string_done = 0;
	static char editstring[100], firststring[100];
	constexpr short kMaxBufferLen = static_cast<short>(sizeof(editstring) - 1);
	if (maxlength > kMaxBufferLen)
		maxlength = kMaxBufferLen;
	if (maxlength < 0)
		maxlength = 0;
	
	int tempchar;
	const char* temptext;
	short has_typed = 0; // hasn't typed yet
	bool return_null = false;

	for (i=0; i < static_cast<short>(sizeof(editstring)); i++)
		editstring[i] = 0; // clear the string ...

	if (begin)
	{
		snprintf(editstring, sizeof(editstring), "%s", begin);
	}
	snprintf(firststring, sizeof(firststring), "%s", begin ? begin : ""); // default case
	current_length = static_cast<short>(strlen(editstring));
	og::runtime::current_session->myscreen_->draw_box(x, y, x+maxlength*(sizex+1), y+sizey, backcolor, 1, 1);
	if (begin && begin[0] != '\0')
		og::runtime::current_session->myscreen_->draw_box(x, y, x+query_width(begin), y+sizey-2, forecolor, 1, 1);
	write_xy(x, y, editstring, WHITE, 1);
	og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);

	clear_keyboard();
	clear_key_press_event();
	clear_text_input_event();
	
    og::input_native::start_text_input(
        editstring, std::max(0, static_cast<int>(maxlength) - 1));
    
	while ( !string_done )
	{
	
        tempchar = 0;
        temptext = nullptr;
        
		// Wait for a key to be pressed ..
		while (!query_key_press_event() && !query_text_input_event())
			//dumbcount++;
			get_input_events(WAIT);
        
        if(query_key_press_event())
        {
            tempchar = query_key();
            clear_key_press_event();
            
            if (tempchar == KEYCODE_RETURN)
                string_done = 1;
            else if (tempchar == KEYCODE_ESCAPE)
            {
                snprintf(editstring, sizeof(editstring), "%s", firststring);
                string_done = 1;
                return_null = true;
            }
            else if (tempchar == KEYCODE_BACKSPACE && current_length > 0)
            {
                if (!has_typed) // first char, so replace text
                {
                    current_length = 0;
                    for (i=0; i < static_cast<short>(sizeof(editstring)); i++)
                        editstring[i] = 0; // clear the string ...
                    og::runtime::current_session->myscreen_->draw_button(x, y, x+maxlength*(sizex+1),
                                      y+sizey+1, 1);
                }
                else
                {
                    // SDL_TEXTINPUT appends UTF-8. Remove one complete code
                    // point instead of one byte so Backspace never leaves a
                    // malformed suffix for native or desktop-web prompts.
                    erase_last_utf8_codepoint(
                        editstring,
                        static_cast<std::size_t>(current_length));
                }
                
                has_typed = 1;
            }
            // Other keys which will deselect the whole line
            else if(tempchar == KEYCODE_LEFT || tempchar == KEYCODE_RIGHT || tempchar == KEYCODE_UP || tempchar == KEYCODE_DOWN || tempchar == KEYCODE_HOME || tempchar == KEYCODE_END)
            {
                has_typed = 1;
            }
            
            current_length = static_cast<short>(strlen(editstring));
        }
        
        if(query_text_input_event())
        {
            temptext = query_text_input();
            
            if (!has_typed) // first char, so replace text
            {
                current_length = 0;
                for (i=0; i < static_cast<short>(sizeof(editstring)); i++)
                    editstring[i] = 0; // clear the string ...
                og::runtime::current_session->myscreen_->draw_button(x, y, x+maxlength*(sizex+1),
                                  y+sizey+1, 1);
            }
            
            if (temptext != nullptr)
            {
                const std::size_t len = strlen(temptext);
                const int remaining = maxlength - current_length - 1;
                if (remaining > 0 && len <= static_cast<std::size_t>(remaining))
                {
                    for (std::size_t j = 0; j < len; j++)
                    {
                        const unsigned char c = static_cast<unsigned char>(temptext[j]);
                        if (c != 255)
                        {
                            editstring[current_length] = static_cast<char>(c);
                            current_length++;
                            editstring[current_length] = '\0';
                        }
                    }
                }
            }
            clear_text_input_event();
            
            has_typed = 1;
            current_length = static_cast<short>(strlen(editstring));
        }

		og::runtime::current_session->myscreen_->draw_box(x, y, x+maxlength*(sizex+1), y+sizey+1, backcolor, 1, 1);
        if(!has_typed && strlen(editstring) > 0)
        {
            og::runtime::current_session->myscreen_->draw_box(x, y, x+query_width(editstring), y+sizey-2, forecolor, 1, 1);
            write_xy(x, y, editstring, WHITE, 1);
        }
		else
            write_xy(x, y, editstring, forecolor, 1);
		og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
	}

    og::input_native::stop_text_input();
    
	clear_keyboard();
	if(return_null)
        return nullptr;
	return editstring;

}

std::optional<std::string> text::input_string_value(Sint32 x, Sint32 y, short maxlength, const char* begin)
{
    char* value = input_string(x, y, maxlength, begin);
    if (value == nullptr)
        return std::nullopt;
    return std::string(value);
}

std::optional<std::string> text::input_string_ex_value(Sint32 x, Sint32 y, short maxlength, const char* message, const char* begin)
{
    char* value = input_string_ex(x, y, maxlength, message, begin);
    if (value == nullptr)
        return std::nullopt;
    return std::string(value);
}


// This version passes DARK_BLUE and a grey color as defaults ..
char * text::input_string_ex(Sint32 x, Sint32 y, short maxlength, const char* message, const char *begin)
{
	return input_string_ex(x, y, maxlength, message, begin, DARK_BLUE, 13);
}

char * text::input_string_ex(Sint32 x, Sint32 y, short maxlength, const char* message, const char *begin,
                          unsigned char forecolor, unsigned char backcolor)
{
	sync_geometry();
	short current_length, i;
	short string_done = 0;
	static char editstring[100], firststring[100];
	constexpr short kMaxBufferLen = static_cast<short>(sizeof(editstring) - 1);
	if (maxlength > kMaxBufferLen)
		maxlength = kMaxBufferLen;
	if (maxlength < 0)
		maxlength = 0;
	
	int tempchar;
	const char* temptext;
	short has_typed = 0; // hasn't typed yet
	bool return_null = false;
	const int field_width = maxlength * (sizex + 1);
	// The shared one-line prompt is used by hiring, networking, and the level
	// editor. Keep its touch affordances in the same canvas surface as the
	// field so a phone never has to discover that Return is the only way out.
	// Its equal columns and 12px gutter deliberately repeat new-company naming.
	const og::ui::PromptDialogLayout layout =
	    og::ui::prompt_dialog_layout(x, y, field_width, sizey);
	const og::ui::PromptActionLayout& actions = layout.actions;
	const og::ui::PromptButtonRect& cancel_button = actions.cancel;
	const og::ui::PromptButtonRect& accept_button = actions.accept;

	for (i=0; i < static_cast<short>(sizeof(editstring)); i++)
		editstring[i] = 0; // clear the string ...

	if (begin)
	{
		snprintf(editstring, sizeof(editstring), "%s", begin);
	}
	snprintf(firststring, sizeof(firststring), "%s", begin ? begin : ""); // default case
	current_length = static_cast<short>(strlen(editstring));
	// Own the complete modal geometry here, where the real font height is
	// known. Callers must not size a frame around a guessed text metric: that
	// was how the action row escaped the hiring dialog in the first place.
	og::runtime::current_session->myscreen_->draw_button(
	    layout.frame.x, layout.frame.y,
	    layout.frame.x + layout.frame.w,
	    layout.frame.y + layout.frame.h, 1);
	og::runtime::current_session->myscreen_->draw_box(x, y, x + field_width, y + sizey, backcolor, 1, 1);
	og::runtime::current_session->myscreen_->draw_button(x, y, x + field_width, y + sizey, 1);
	if (begin && begin[0] != '\0')
		og::runtime::current_session->myscreen_->draw_box(x, y, x+query_width(begin), y+sizey-2, forecolor, 1, 1);
	write_xy(x, y - 10, message, DARK_GREEN, 1);
	write_xy(x, y, editstring, WHITE, 1);
	og::runtime::current_session->myscreen_->draw_button(
	    accept_button.x, accept_button.y,
	    accept_button.x + accept_button.w, accept_button.y + accept_button.h, 1);
	write_xy(accept_button.x + (accept_button.w - query_width("ACCEPT")) / 2,
	         accept_button.y + 3, "ACCEPT", DARK_BLUE, 1);
	og::runtime::current_session->myscreen_->draw_button(
	    cancel_button.x, cancel_button.y,
	    cancel_button.x + cancel_button.w, cancel_button.y + cancel_button.h, 1);
	write_xy(cancel_button.x + (cancel_button.w - query_width("CANCEL")) / 2,
	         cancel_button.y + 3, "CANCEL", DARK_BLUE, 1);
	og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);

	clear_keyboard();
	clear_key_press_event();
	clear_text_input_event();
	// A prompt entered from a button can inherit that button's held pointer.
	// Establish a clean baseline before accepting a new prompt click.
	reset_mouse_click_tracking();
	MouseState& prompt_mouse = query_mouse_no_poll();
	prompt_mouse.left = 0;
	
    og::input_native::start_text_input(
        editstring, std::max(0, static_cast<int>(maxlength) - 1), message);
    
	while ( !string_done )
	{
	
        tempchar = 0;
        temptext = nullptr;
        
		// Wait for a key to be pressed ..
		while (!query_key_press_event() && !query_text_input_event() &&
		       !prompt_mouse.left)
			//dumbcount++;
			get_input_events(WAIT);
        
        if(query_key_press_event())
        {
            tempchar = query_key();
            clear_key_press_event();
            
            if (tempchar == KEYCODE_RETURN)
                string_done = 1;
            else if (tempchar == KEYCODE_ESCAPE)
            {
                snprintf(editstring, sizeof(editstring), "%s", firststring);
                string_done = 1;
                return_null = true;
            }
            else if (tempchar == KEYCODE_BACKSPACE && current_length > 0)
            {
                if (!has_typed) // first char, so replace text
                {
                    current_length = 0;
                    for (i=0; i < static_cast<short>(sizeof(editstring)); i++)
                        editstring[i] = 0; // clear the string ...
                    og::runtime::current_session->myscreen_->draw_button(x, y, x+maxlength*(sizex+1),
                                      y+sizey+1, 1);
                }
                else
                {
                    // Keep the extended/message prompt path consistent with
                    // input_string: SDL text events append UTF-8, so one
                    // Backspace removes one complete code point.
                    erase_last_utf8_codepoint(
                        editstring,
                        static_cast<std::size_t>(current_length));
                }
                
                has_typed = 1;
            }
            // Other keys which will deselect the whole line
            else if(tempchar == KEYCODE_LEFT || tempchar == KEYCODE_RIGHT || tempchar == KEYCODE_UP || tempchar == KEYCODE_DOWN || tempchar == KEYCODE_HOME || tempchar == KEYCODE_END)
            {
                has_typed = 1;
            }
            
            current_length = static_cast<short>(strlen(editstring));
        }
        
        if(query_text_input_event())
        {
            temptext = query_text_input();
            
            if (!has_typed) // first char, so replace text
            {
                current_length = 0;
                for (i=0; i < static_cast<short>(sizeof(editstring)); i++)
                    editstring[i] = 0; // clear the string ...
                og::runtime::current_session->myscreen_->draw_button(x, y, x+maxlength*(sizex+1),
                                  y+sizey+1, 1);
            }
            
            if (temptext != nullptr)
            {
                const std::size_t len = strlen(temptext);
                const int remaining = maxlength - current_length - 1;
                if (remaining > 0 && len <= static_cast<std::size_t>(remaining))
                {
                    for (std::size_t j = 0; j < len; j++)
                    {
                        const unsigned char c = static_cast<unsigned char>(temptext[j]);
                        if (c != 255)
                        {
                            editstring[current_length] = static_cast<char>(c);
                            current_length++;
                            editstring[current_length] = '\0';
                        }
                    }
                }
            }
            clear_text_input_event();
            
            has_typed = 1;
            current_length = static_cast<short>(strlen(editstring));
        }

		if (prompt_mouse.left)
		{
			const int click_x = static_cast<int>(prompt_mouse.x);
			const int click_y = static_cast<int>(prompt_mouse.y);
			prompt_mouse.left = 0;
			const auto inside =
			    [click_x, click_y](const og::ui::PromptButtonRect& rect) {
				return click_x >= rect.x && click_x < rect.x + rect.w &&
				       click_y >= rect.y && click_y < rect.y + rect.h;
			};
			if (inside(accept_button))
				string_done = 1;
			else if (inside(cancel_button))
			{
				snprintf(editstring, sizeof(editstring), "%s", firststring);
				string_done = 1;
				return_null = true;
			}
		}
		
		og::runtime::current_session->myscreen_->draw_button(x, y, x + field_width, y + sizey + 1, 1);
        write_xy(x, y - 10, message, DARK_GREEN, 1);
        if(!has_typed && strlen(editstring) > 0)
        {
            og::runtime::current_session->myscreen_->draw_box(x, y, x+query_width(editstring), y+sizey-2, forecolor, 1, 1);
            write_xy(x, y, editstring, WHITE, 1);
        }
		else
            write_xy(x, y, editstring, forecolor, 1);
		og::runtime::current_session->myscreen_->draw_button(
		    accept_button.x, accept_button.y,
		    accept_button.x + accept_button.w, accept_button.y + accept_button.h, 1);
		write_xy(accept_button.x + 7, accept_button.y + 3, "ACCEPT", DARK_BLUE, 1);
		og::runtime::current_session->myscreen_->draw_button(
		    cancel_button.x, cancel_button.y,
		    cancel_button.x + cancel_button.w, cancel_button.y + cancel_button.h, 1);
		write_xy(cancel_button.x + 7, cancel_button.y + 3, "CANCEL", DARK_BLUE, 1);
		og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
	}

    og::input_native::stop_text_input();
	reset_mouse_click_tracking();
    
	clear_keyboard();
	if(return_null)
        return nullptr;
	return editstring;

}
