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
#include <string.h>
#include <stdio.h>
#include "graph.h"
#include "util.h"

#define HELPTEXT_LEFT 40
#define HELPTEXT_TOP  40
#define DISPLAY_LINES 15
#define TEXT_DOWN(x) ((x*7)+HELPTEXT_TOP)

short end_of_file;                        // global flag ..
char helptext[HELP_WIDTH][MAX_LINES];


// This function reads one text line from file infile,
// stopping at length (length), or when encountering an
// end-of-line character ..
char* read_one_line(SDL_RWops *infile, short length)
{
	char *newline; // = new char(length);
	char temp;
	short readvalue;
	short i;

	newline = new char[HELP_WIDTH];

	// Make sure this line is blank ..
	for (i=0; i < HELP_WIDTH; i++)
		newline[i] = 0;

	for (i=0; i < length; i++)
	{
		readvalue = (short) SDL_RWread(infile, &temp, 1, 1);
		if (readvalue != 1)
		{
			end_of_file = 1;
			return &newline[0];
		}
		if (temp == '\n' || temp == '\r')
			return &newline[0];
		newline[i] = temp;
	}

	return newline;
}

// Note: this code has been redone to work in 'scanlines,'
//       so that the text scrolls by pixels rather than lines.
short read_scenario(screen *myscreen)
{
	Sint32 screenlines = myscreen->level_data.description.size() * 8;
	Sint32  numlines, j;
	Sint32 linesdown;
	Sint32 changed;
	Sint32 templines;
	Sint32 text_delay = 1; // bigger = slower
	Sint32 key_presses = 0;
	
	text& mytext = myscreen->text_normal;
	Sint32 start_time, now_time;
	Sint32 bottomrow = (screenlines - ((DISPLAY_LINES-1)*8) );

	clear_keyboard();
	linesdown = 0;
	changed = 1;
	start_time = query_timer();
	numlines = screenlines;

	// Do the loop until person hits escape
	while (!query_input_continue())
	{
		get_input_events(POLL);
		
		short scroll_amount = get_and_reset_scroll_amount();
		if (scroll_amount < 0)    // scrolling down
		{
			now_time = query_timer();

			key_presses =  (now_time - start_time) % text_delay;
			if (!key_presses && (linesdown < bottomrow) )
			{
			    while(linesdown < bottomrow && scroll_amount != 0)
                {
                    linesdown++;
                    scroll_amount++;
                }
				changed = 1;
			}
		} // end of KEYSTATE_DOWN

		if (keystates[KEYSTATE_PAGEDOWN])    // scrolling one page down
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && (linesdown < bottomrow) )
			{
				templines = linesdown + (DISPLAY_LINES * 7);
				if (templines > bottomrow)
					templines = bottomrow;
				if (linesdown != templines) // we actually moved down
				{
					linesdown = templines;
					changed = 1;
				}
			}
		} // end of PAGE DOWN

		if (scroll_amount > 0)      // scrolling up
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % text_delay;
			if (!key_presses && linesdown)
			{
			    while(linesdown && scroll_amount != 0)
                {
                    linesdown--;
                    scroll_amount--;
                }
				changed = 1;
			}
		} // end of KEYSTATE_UP

		if (keystates[KEYSTATE_PAGEUP])    // scrolling one page up
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && linesdown)
			{
				linesdown -= (DISPLAY_LINES * 7);
				if (linesdown < 0)
					linesdown = 0;
				changed = 1;
			}
		}  // end of PAGE UP

		if (changed)  // did we scroll, etc.?
		{
			templines = linesdown/8; // which TEXT line are we at?
			myscreen->draw_button(HELPTEXT_LEFT-4, HELPTEXT_TOP-4-8,
			                      HELPTEXT_LEFT+200, HELPTEXT_TOP+107, 3, 1);
			for (j=0; j < DISPLAY_LINES; j++)
            {
                std::string s = myscreen->level_data.get_description_line(j+templines);
				if(s.size() > 0)
					mytext.write_xy(HELPTEXT_LEFT+2, (short) (TEXT_DOWN(j)-linesdown%8),
					                 s.c_str(), (unsigned char) DARK_BLUE, 1 ); // to buffer!
            }


			// Draw a bounding box (top and bottom edges) ..
			myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP-8,
			                        HELPTEXT_LEFT+200-4, HELPTEXT_TOP-2);
			myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP+97,
			                        HELPTEXT_LEFT+200-4, HELPTEXT_TOP+103);
			mytext.write_xy(HELPTEXT_LEFT+40,
			                 HELPTEXT_TOP-7, "SCENARIO INFORMATION", (unsigned char) RED, 1);
			mytext.write_xy(HELPTEXT_LEFT+30,
			                 HELPTEXT_TOP+98, CONTINUE_ACTION_STRING " TO CONTINUE", (unsigned char) RED, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			changed = 0;
		} // end of changed drawing loop

	}  // loop until ESC is pressed

	while (keystates[KEYSTATE_ESCAPE])  // wait for key release
		get_input_events(WAIT);
    
	return (short) numlines;
}

short read_campaign_intro(screen * myscreen)
{
    CampaignData data(myscreen->save_data.current_campaign);
    if(!data.load())
        return 1;
    
	Sint32 screenlines;
	Sint32  numlines, j;
	Sint32 linesdown;
	Sint32 changed;
	Sint32 templines;
	Sint32 text_delay = 1; // bigger = slower
	Sint32 key_presses = 0;
	
	text& mytext = myscreen->text_normal;
	Sint32 start_time, now_time;
	Sint32 bottomrow;

	clear_keyboard();
	end_of_file = 0;
	linesdown = 0;
	changed = 1;
	start_time = query_timer();

	// Fill the helptext array with data ..
	numlines = data.description.size();
	screenlines = numlines*8;
	numlines = screenlines;
	bottomrow = (screenlines - ((DISPLAY_LINES-1)*8) );

	// Do the loop until person hits escape
	while (!query_input_continue())
	{
		get_input_events(POLL);
		
		short scroll_amount = get_and_reset_scroll_amount();
		if (scroll_amount < 0)    // scrolling down
		{
			now_time = query_timer();

			key_presses =  (now_time - start_time) % text_delay;
			if (!key_presses && (linesdown < bottomrow) )
			{
			    while(linesdown < bottomrow && scroll_amount != 0)
                {
                    linesdown++;
                    scroll_amount++;
                }
				changed = 1;
			}
		} // end of KEYSTATE_DOWN

		if (keystates[KEYSTATE_PAGEDOWN])    // scrolling one page down
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && (linesdown < bottomrow) )
			{
				templines = linesdown + (DISPLAY_LINES * 7);
				if (templines > bottomrow)
					templines = bottomrow;
				if (linesdown != templines) // we actually moved down
				{
					linesdown = templines;
					changed = 1;
				}
			}
		} // end of PAGE DOWN

		if (scroll_amount > 0)      // scrolling up
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % text_delay;
			if (!key_presses && linesdown)
			{
			    while(linesdown && scroll_amount != 0)
                {
                    linesdown--;
                    scroll_amount--;
                }
				changed = 1;
			}
		} // end of KEYSTATE_UP

		if (keystates[KEYSTATE_PAGEUP])    // scrolling one page up
		{
			now_time = query_timer();
			key_presses = (now_time - start_time) % (10*text_delay);
			if (!key_presses && linesdown)
			{
				linesdown -= (DISPLAY_LINES * 7);
				if (linesdown < 0)
					linesdown = 0;
				changed = 1;
			}
		}  // end of PAGE UP

		if (changed)  // did we scroll, etc.?
		{
			templines = linesdown/8; // which TEXT line are we at?
			myscreen->draw_button(HELPTEXT_LEFT-4, HELPTEXT_TOP-4-8,
			                      HELPTEXT_LEFT+240, HELPTEXT_TOP+107, 3, 1);
			for (j=0; j < DISPLAY_LINES; j++)
            {
				if(data.getDescriptionLine(j+templines).size() == 0)
                    continue;
                
                mytext.write_xy(HELPTEXT_LEFT+2, (short) (TEXT_DOWN(j)-linesdown%8), data.getDescriptionLine(j+templines).c_str(), (unsigned char) DARK_BLUE, 1 ); // to buffer!
            }

			
			// Draw a bounding box (top and bottom edges) ..
			myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP-8,
			                        HELPTEXT_LEFT+240-4, HELPTEXT_TOP-2);
			myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP+97,
			                        HELPTEXT_LEFT+240-4, HELPTEXT_TOP+103);
			mytext.write_xy(HELPTEXT_LEFT+240/2 - data.title.size()*3,
			                 HELPTEXT_TOP-7, data.title.c_str(), (unsigned char) RED, 1);
			mytext.write_xy(HELPTEXT_LEFT+52,
			                 HELPTEXT_TOP+98, CONTINUE_ACTION_STRING " TO CONTINUE", (unsigned char) RED, 1);
			//myscreen->buffer_to_screen(0, 0, 320, 200);
			myscreen->buffer_to_screen(HELPTEXT_LEFT-4, HELPTEXT_TOP-4-8,244,119);

			
			changed = 0;
		} // end of changed drawing loop

	}  // loop until ESC is pressed



	while (keystates[KEYSTATE_ESCAPE])  // wait for key release
		get_input_events(WAIT);
	//delete mytext;
	return (short) (numlines);
}


// This function fills the array with the help file
// text ..
// It returns the # of lines successfully filled ..
short fill_help_array(char somearray[HELP_WIDTH][MAX_LINES], SDL_RWops *infile)
//short fill_help_array(char somearray[80][80], FILE *infile)
{
	short i;
	char *someline;

	if (!infile)
		return 0;

	for (i=0; i < MAX_LINES; i++)
	{
		//somearray[i] = read_one_line(infile, HELP_WIDTH);
		someline = read_one_line(infile, HELP_WIDTH);
		strcpy(somearray[i], someline);
		delete[] someline;
		if (end_of_file)
			return i;
	}

	return MAX_LINES;
}



