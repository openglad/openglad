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
#include "graph.h"

void get_input_events(bool);

static PixieData letters1;
static PixieData letters_big;


text::text(screen * myscreen)
    : sizex(0), sizey(0)
{
    if(!letters1.valid())
        letters1 = read_pixie_file(TEXT_1);
    if(!letters_big.valid())
        letters_big = read_pixie_file(TEXT_BIG);
    
    letters = letters1;
	if(!letters.valid())
		Log("letters is NULL\n");
    sizex = letters.w;
    sizey = letters.h;
	screenp = myscreen;
}

text::text(screen * myscreen, const char * filename)
    : sizex(0), sizey(0)
{
    if(!letters1.valid())
        letters1 = read_pixie_file(TEXT_1);
    if(!letters_big.valid())
        letters_big = read_pixie_file(TEXT_BIG);
    
    const char* temp_filename = filename;
	if (!temp_filename || strlen(temp_filename) < 2)
		temp_filename = "text.pix";
		
    if(strcmp(temp_filename, TEXT_BIG) == 0)
        letters = letters_big;
    else
        letters = letters1;
    sizex = letters.w;
    sizey = letters.h;
    
	screenp = myscreen;
}

text::~text()
{
    // TODO: Free letters somewhere better (it's a global leak for now, so no hurry)
	//free(letters-3);  // letters is offset by 3 bytes on load
	//letters = NULL;
}
short text::query_width(const char *string) // returns width, in pixels
{
	unsigned short i=0;
	short over = 0;

	if (sizex < 9) // small, monospaced font
		return (sizex+1)*strlen(string);

	while (string[i])
	{
		if (string[i] >= 65 && string[i] <= 93) // uppercase
			over += sizex;
		else
			over += sizex-1;
		i++;
	}

	return over;
}

short text::write_xy(short x, short y, const char *string, unsigned char color)
{
	unsigned short i = 0;
	while(string[i])
	{
		write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color);
		i++;
	}
	return 1;
}

short text::write_xy(short x, short y, const char *string)
{
	unsigned short i = 0;
	while(string[i])
	{
		write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR);
		i++;
	}
	return 1;
}

short text::write_xy(short x, short y, const char *string, unsigned char color,
                     short to_buffer)
{
	unsigned short i = 0;
	unsigned short width;
	short over = 0;

	if (sizex < 9) // small, monospaced font
		while(string[i])
		{
			if (!to_buffer)
				write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color);
			else
				write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color, (short) 1);
			i++;
			over += sizex+1;
		}
	else // larger font, help out the lowercase ..
		while(string[i])
		{
			write_char_xy((short) (x+over), y, string[i], (unsigned char) color, (short) 1);
			if (string[i] >=65 && string[i] <= 92) // uppercase
				over += sizex;
			else // lowercase, other things
				over += sizex-1;
			i++;
		}

	if (to_buffer)
	{
		width = (unsigned short) ((sizex+1)*strlen(string));
		width -= width%4;
		width +=4;
		//screenp->buffer_to_screen(x, y, width, sizey);
	}

	return over;
}

short text::write_xy(short x, short y, const char *string, short to_buffer)
{
	unsigned short i = 0;
	unsigned short width;
	while(string[i])
	{
		if (!to_buffer)
			write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR);
		else
			write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR, (short) 1);
		i++;
	}
	if (to_buffer)
	{
		width = (unsigned short) ((sizex+1)*strlen(string));
		width -= width%4;
		width +=4;
		//screenp->buffer_to_screen(x, y, width, sizey);
	}

	return 1;
}

short text::write_xy(short x, short y, const char *string, unsigned char color,
                     viewscreen *whereto)
{
	unsigned short i = 0;
	while(string[i])
	{
		if (!whereto)
			write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color);
		else
			write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color, whereto);
		i++;
	}
	return 1;
}

short text::write_xy(short x, short y, const char *string, viewscreen *whereto)
{
	unsigned short i = 0;
	while(string[i])
	{
		if (!whereto)
			write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR);
		else
			write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR, whereto);
		i++;
	}
	return 1;
}

short text::write_y(short y, const char *string, unsigned char color)
{
	unsigned short len = 0;
	unsigned short xstart;
	len = (unsigned short) strlen(string);
	xstart = (unsigned short) ((320 - len * (sizex+1))/2);
	return write_xy(xstart, y, string, (unsigned char) color);
}

short text::write_y(short y, const char *string)
{
	unsigned short len = 0;
	unsigned short xstart;
	len = (unsigned short) strlen(string);
	xstart = (unsigned short) ((320 - len * (sizex+1))/2);
	return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR);
}

short text::write_y(short y, const char *string, unsigned char color,
                    short to_buffer)
{
	unsigned short len = 0;
	unsigned short xstart;
	len = (unsigned short) strlen(string);
	xstart = (unsigned short) ((320 - len * (sizex+1))/2);
	if (!to_buffer)
		return write_xy(xstart, y, string, (unsigned char) color);
	else
		return write_xy(xstart, y, string, (unsigned char) color, to_buffer);
}

short text::write_y(short y, const char *string, short to_buffer)
{
	unsigned short len = 0;
	unsigned short xstart;
	len = (unsigned short) strlen(string);
	xstart = (unsigned short) ((320 - len * (sizex+1))/2);
	if (!to_buffer)
		return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR);
	else
		return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR, to_buffer);
}

short text::write_y(short y, const char *string, unsigned char color,
                    viewscreen *whereto)
{
	unsigned short len = 0;
	unsigned short xstart;
	len = (unsigned short) strlen(string);
	xstart = (unsigned short) ((320 - len * (sizex+1))/2);
	if (!whereto)
		return write_xy(xstart, y, string, (unsigned char) color);
	else
		return write_xy(xstart, y, string, (unsigned char) color, whereto);
}

short text::write_y(short y, const char *string, viewscreen *whereto)
{
	unsigned short len = 0;
	unsigned short xstart;
	len = (unsigned short) strlen(string);
	xstart = (unsigned short) ((320 - len * (sizex+1))/2);
	if (!whereto)
		return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR);
	else
		return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR, whereto);
}

// This version writes to the buffer and then writes the
// buffer to the screen .. (double buffered to eliminate flashing)
short text::write_char_xy(short x, short y, char letter, unsigned char color,
                          short to_buffer)
{
	if (!to_buffer)
		return write_char_xy(x, y, letter, (unsigned char) color);

	screenp->walkputbuffertext(x, y, sizex, sizey, 0, 0, 319,199, &letters.data[letter * sizex * sizey], (unsigned char) color);
	//screenp->buffer_to_screen(x, y, sizex + 4 - (sizex%4), sizey + 4 - (sizey%4) );
	return 1;
}

short text::write_char_xy(short x, short y, char letter, short to_buffer)
{
	if (!to_buffer)
		return write_char_xy(x, y, letter, (unsigned char) DEFAULT_TEXT_COLOR);

	screenp->walkputbuffertext(x, y, sizex, sizey, 0, 0, 319,199, &letters.data[letter * sizex * sizey], (unsigned char) DEFAULT_TEXT_COLOR);
	//screenp->buffer_to_screen(x, y, sizex + 4 - (sizex%4), sizey + 4 - (sizey%4) );
	return 1;
}

short text::write_char_xy(short x, short y, char letter, unsigned char color)
{
	screenp->putdatatext(x, y, sizex, sizey, &letters.data[letter *sizex*sizey], (unsigned char) color);
	return 1;
}

short text::write_char_xy(short x, short y, char letter)
{
	screenp->putdatatext(x, y, sizex, sizey, &letters.data[letter *sizex*sizey]);
	return 1;
}

short text::write_char_xy(short x, short y, char letter, unsigned char color,
                          viewscreen *whereto)
{
	if (!whereto)
		screenp->putdatatext(x, y, sizex, sizey, &letters.data[letter *sizex*sizey], (unsigned char) color);
	else
		screenp->walkputbuffertext(x+whereto->xloc, y+whereto->yloc, sizex, sizey,
		                       whereto->xloc,whereto->yloc,whereto->endx, whereto->endy,
		                       &letters.data[letter *sizex*sizey], (unsigned char) color);
	//         screenp->buffer_to_screen(x+whereto->xloc, y+whereto->yloc,
	//           (sizex + 4 - (sizex%4)), (sizey + 4 - (sizey%4)) );
	return 1;
}

short text::write_char_xy(short x, short y, char letter, viewscreen *whereto)
{
	if (!whereto)
		screenp->putdatatext(x, y, sizex, sizey, &letters.data[letter *sizex*sizey]);
	else
		screenp->walkputbuffertext(x+whereto->xloc, y+whereto->yloc, sizex, sizey,
		                       whereto->xloc,whereto->yloc,whereto->endx, whereto->endy,
		                       &letters.data[letter *sizex*sizey], (unsigned char) DEFAULT_TEXT_COLOR);
	//         screenp->buffer_to_screen(x+whereto->xloc, y+whereto->yloc,
	//           (sizex + 4 - (sizex%4)), (sizey + 4 - (sizey%4)) );
	return 1;
}

// This version passes DARK_BLUE and a grey color as defaults ..
char * text::input_string(short x, short y, short maxlength, const char *begin)
{
	return input_string(x, y, maxlength, begin, DARK_BLUE, 13);
}

// Input_string reads a string from the keyboard, displaying it
// at screen position x,y.  The maximum length of the string
// is maxlength, and any string in 'begin' will automatically be
// entered at the start.  Fore- and backcolor are used for the
// text foreground and background color
char * text::input_string(short x, short y, short maxlength, const char *begin,
                          unsigned char forecolor, unsigned char backcolor)
{
	short current_length, i;
	short string_done = 0;
	static char editstring[100], firststring[100];
	//char *somekeyboard = grab_keyboard();
	int tempchar;
	char* temptext;
	short has_typed = 0; // hasn't typed yet
	bool return_null = false;

	for (i=0; i < 100; i++)
		editstring[i] = 0; // clear the string ...

	if (begin)
	{
		strcpy(editstring, begin);
	}
	strcpy(firststring, begin); // default case
	current_length = (short) strlen(editstring);
	screenp->draw_box(x, y, x+maxlength*(sizex+1), y+sizey, backcolor, 1, 1);
	if (strlen(begin))
		screenp->draw_box(x, y, x+query_width(begin), y+sizey-2, forecolor, 1, 1);
	write_xy(x, y, editstring, WHITE, 1);
	screenp->buffer_to_screen(0, 0, 320, 200);

	clear_keyboard();
	clear_key_press_event();
	clear_text_input_event();
	enable_keyrepeat();
    #ifdef USE_SDL2
    SDL_StartTextInput();
    #endif
    
	while ( !string_done )
	{
		
		screenp->clearfontbuffer(x,y,maxlength*sizex,sizey);
	
        tempchar = 0;
        temptext = NULL;
        
		// Wait for a key to be pressed ..
		while (!query_key_press_event() && !query_text_input_event())
			//dumbcount++;
			get_input_events(WAIT);
        
        if(query_key_press_event())
        {
            tempchar = query_key();
            clear_key_press_event();
            
            if (tempchar == SDLK_RETURN)
                string_done = 1;
            else if (tempchar == SDLK_ESCAPE)
            {
                strcpy(editstring, firststring);
                string_done = 1;
                return_null = true;
            }
            else if (tempchar == SDLK_BACKSPACE && current_length > 0)
            {
                if (!has_typed) // first char, so replace text
                {
                    current_length = 0;
                    for (i=0; i < 100; i++)
                        editstring[i] = 0; // clear the string ...
                    screenp->draw_button(x, y, x+maxlength*(sizex+1),
                                      y+sizey+1, 1);
                }
                else
                    editstring[current_length-1] = 0;
                
                has_typed = 1;
            }
            // Other keys which will deselect the whole line
            else if(tempchar == SDLK_LEFT || tempchar == SDLK_RIGHT || tempchar == SDLK_UP || tempchar == SDLK_DOWN || tempchar == SDLK_HOME || tempchar == SDLK_END)
            {
                has_typed = 1;
            }
            
            current_length = (short) strlen(editstring);
        }
        
        if(query_text_input_event())
        {
            temptext = query_text_input();
            
            if (!has_typed) // first char, so replace text
            {
                current_length = 0;
                for (i=0; i < 100; i++)
                    editstring[i] = 0; // clear the string ...
                screenp->draw_button(x, y, x+maxlength*(sizex+1),
                                  y+sizey+1, 1);
            }
            
            if ( temptext != NULL &&
                      (current_length + short(strlen(temptext)) < maxlength) )
            {
                
                int len = strlen(temptext);
                int i;
                for(i = 0; i < len; i++)
                {
                    unsigned char c = temptext[i];
                    if(c != 255)
                    {
                        editstring[current_length] = c;
                        current_length++;
                        editstring[current_length] = '\0';
                    }
                }
            }
            clear_text_input_event();
            
            has_typed = 1;
            current_length = (short) strlen(editstring);
        }

		screenp->draw_box(x, y, x+maxlength*(sizex+1), y+sizey+1, backcolor, 1, 1);
        if(!has_typed && strlen(editstring) > 0)
        {
            screenp->draw_box(x, y, x+query_width(editstring), y+sizey-2, forecolor, 1, 1);
            write_xy(x, y, editstring, WHITE, 1);
        }
		else
            write_xy(x, y, editstring, forecolor, 1);
		screenp->buffer_to_screen(0, 0, 320, 200);
	}

    #ifdef USE_SDL2
    SDL_StopTextInput();
    #endif
	disable_keyrepeat();
	clear_keyboard();
	if(return_null)
        return NULL;
	return editstring;

}


// This version passes DARK_BLUE and a grey color as defaults ..
char * text::input_string_ex(short x, short y, short maxlength, const char* message, const char *begin)
{
	return input_string_ex(x, y, maxlength, message, begin, DARK_BLUE, 13);
}

char * text::input_string_ex(short x, short y, short maxlength, const char* message, const char *begin,
                          unsigned char forecolor, unsigned char backcolor)
{
	short current_length, i;
	short string_done = 0;
	static char editstring[100], firststring[100];
	//char *somekeyboard = grab_keyboard();
	int tempchar;
	char* temptext;
	short has_typed = 0; // hasn't typed yet
	bool return_null = false;

	for (i=0; i < 100; i++)
		editstring[i] = 0; // clear the string ...

	if (begin)
	{
		strcpy(editstring, begin);
	}
	strcpy(firststring, begin); // default case
	current_length = (short) strlen(editstring);
	screenp->draw_button(x, y, x+maxlength*(sizex+1), y+sizey, 1);
	if (strlen(begin))
		screenp->draw_box(x, y, x+query_width(begin), y+sizey-2, forecolor, 1, 1);
	write_xy(x, y - 10, message, DARK_GREEN, 1);
	write_xy(x, y, editstring, WHITE, 1);
	screenp->buffer_to_screen(0, 0, 320, 200);

	clear_keyboard();
	clear_key_press_event();
	clear_text_input_event();
	enable_keyrepeat();
    #ifdef USE_SDL2
    SDL_StartTextInput();
    #endif
    
	while ( !string_done )
	{
		
		screenp->clearfontbuffer(x,y,maxlength*sizex,sizey);
	
        tempchar = 0;
        temptext = NULL;
        
		// Wait for a key to be pressed ..
		while (!query_key_press_event() && !query_text_input_event())
			//dumbcount++;
			get_input_events(WAIT);
        
        if(query_key_press_event())
        {
            tempchar = query_key();
            clear_key_press_event();
            
            if (tempchar == SDLK_RETURN)
                string_done = 1;
            else if (tempchar == SDLK_ESCAPE)
            {
                strcpy(editstring, firststring);
                string_done = 1;
                return_null = true;
            }
            else if (tempchar == SDLK_BACKSPACE && current_length > 0)
            {
                if (!has_typed) // first char, so replace text
                {
                    current_length = 0;
                    for (i=0; i < 100; i++)
                        editstring[i] = 0; // clear the string ...
                    screenp->draw_button(x, y, x+maxlength*(sizex+1),
                                      y+sizey+1, 1);
                }
                else
                    editstring[current_length-1] = 0;
                
                has_typed = 1;
            }
            // Other keys which will deselect the whole line
            else if(tempchar == SDLK_LEFT || tempchar == SDLK_RIGHT || tempchar == SDLK_UP || tempchar == SDLK_DOWN || tempchar == SDLK_HOME || tempchar == SDLK_END)
            {
                has_typed = 1;
            }
            
            current_length = (short) strlen(editstring);
        }
        
        if(query_text_input_event())
        {
            temptext = query_text_input();
            
            if (!has_typed) // first char, so replace text
            {
                current_length = 0;
                for (i=0; i < 100; i++)
                    editstring[i] = 0; // clear the string ...
                screenp->draw_button(x, y, x+maxlength*(sizex+1),
                                  y+sizey+1, 1);
            }
            
            if ( temptext != NULL &&
                      (current_length + short(strlen(temptext)) < maxlength) )
            {
                
                int len = strlen(temptext);
                int i;
                for(i = 0; i < len; i++)
                {
                    unsigned char c = temptext[i];
                    if(c != 255)
                    {
                        editstring[current_length] = c;
                        current_length++;
                        editstring[current_length] = '\0';
                    }
                }
            }
            clear_text_input_event();
            
            has_typed = 1;
            current_length = (short) strlen(editstring);
        }
		
		screenp->draw_button(x, y, x+maxlength*(sizex+1), y+sizey+1, 1);
        write_xy(x, y - 10, message, DARK_GREEN, 1);
        if(!has_typed && strlen(editstring) > 0)
        {
            screenp->draw_box(x, y, x+query_width(editstring), y+sizey-2, forecolor, 1, 1);
            write_xy(x, y, editstring, WHITE, 1);
        }
		else
            write_xy(x, y, editstring, forecolor, 1);
		screenp->buffer_to_screen(0, 0, 320, 200);
	}

    #ifdef USE_SDL2
    SDL_StopTextInput();
    #endif
	disable_keyrepeat();
	clear_keyboard();
	if(return_null)
        return NULL;
	return editstring;

}


