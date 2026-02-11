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
#include "button.h"
#include "input.h"
#include "util.h"

#include "SDL.h"

#include <cstring>
#include <list>
#include <string>

namespace {
constexpr int YES_VALUE = 5;
constexpr int NO_VALUE = 6;

button yes_or_no_buttons[] =
    {
        button("yes", "YES", KEYSTATE_UNKNOWN,  70, 130, 50, 20, YES_OR_NO, YES_VALUE, MenuNav::Right(1)),
        button("no", "NO", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, YES_OR_NO, NO_VALUE, MenuNav::Left(0))
    };

button no_or_yes_buttons[] =
    {
        button("no", "NO", KEYSTATE_UNKNOWN,  70, 130, 50, 20, YES_OR_NO, NO_VALUE, MenuNav::Right(1)),
        button("yes", "YES", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, YES_OR_NO, YES_VALUE, MenuNav::Left(0))
    };

button popup_dialog_buttons[] =
    {
        button("ok", "OK", KEYSTATE_ESCAPE,  160 - 25, 130, 50, 20, YES_OR_NO, YES_VALUE, MenuNav::None())
    };
} // namespace

extern vbutton * localbuttons;

Sint32 leftmouse(button* buttons);
void draw_highlight_interior(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& localbuttons, button* buttons, int num_buttons, Sint32& retvalue);

void timed_dialog(const char* message, float delay_seconds)
{
    Log("{}\n", message);

    myscreen->darken_screen();

    text& gladtext = myscreen->text_normal;

    int pix_per_char = 6;
    int len = strlen(message);
    int width = len * pix_per_char;
    int leftside  = 160 - width/2 - 12;
    int rightside = 160 + width/2 + 12;

    myscreen->draw_button(leftside, 80, rightside, 110, 1);
    gladtext.write_xy(160 - width/2, 94, message, static_cast<unsigned char>(DARK_BLUE), 1);

    myscreen->buffer_to_screen(0, 0, 320, 200); // refresh screen

    grab_mouse();
    clear_keyboard();

    clear_key_press_event();

    Uint32 start_time = SDL_GetTicks();
    while ((SDL_GetTicks() - start_time)/1000.0f < delay_seconds)
    {
        get_input_events(POLL);

        if(query_mouse().left || query_key_press_event())
            break;

        SDL_Delay(10);
    }
}

bool yes_or_no_prompt(const char* title, const char* message, bool default_value)
{
    Log("{}, {}: \n", title, message);
#ifdef TESTING
    return default_value;
#endif

    myscreen->darken_screen();

    text& gladtext = myscreen->text_normal;

    int pix_per_char = 6;

    // Break message into lines
    std::list<std::string> ls = explode(message, '\n');

    // Get the max dimensions needed to display it
    int w = strlen(title)*9;
    int h = 30 + 10*ls.size();
    for(auto& line : ls)
    {
        if(int(line.size()*pix_per_char) > w)
            w = line.size()*pix_per_char;
    }

    // Centered bounds
    int leftside  = 160 - w/2 - 12;
    int rightside = 160 + w/2 + 12;
    int j = 0;

    int dumbcount;

    if (localbuttons)
        delete (localbuttons);

    button* buttons = yes_or_no_buttons;
    int num_buttons = 2;
    int highlighted_button = (default_value? 0 : 1);
    localbuttons = init_buttons(buttons, num_buttons);

    grab_mouse();
    clear_keyboard();

    clear_key_press_event();

    int retvalue = 0;
    while (retvalue == 0)
    {
        // Input - leftmouse will poll events via query_mouse()
        if(leftmouse(buttons))
            retvalue = localbuttons->leftclick();

        // Check keyboard after leftmouse has polled events
        if(query_key_press_event())
        {
            if(keystates[KEYSTATE_y])
                retvalue = YES_VALUE;
            else if(keystates[KEYSTATE_n])
                retvalue = NO_VALUE;
            else if(keystates[KEYSTATE_ESCAPE])
                break;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);

        // Draw
        dumbcount = myscreen->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
        j = 0;
        for(auto& line : ls)
        {
            gladtext.write_xy(dumbcount + 3*pix_per_char/2, 104 - h/2 + 10*j, line.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
            j++;
        }

        draw_buttons(buttons, num_buttons);

        draw_highlight_interior(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
    }

    if(retvalue == YES_VALUE)
    {
        Log("YES\n");
        return true;
    }
    if(retvalue == NO_VALUE)
    {
        Log("NO\n");
        return false;
    }
    return default_value;
}

bool no_or_yes_prompt(const char* title, const char* message, bool default_value)
{
    Log("{}, {}: \n", title, message);

    myscreen->darken_screen();

    text& gladtext = myscreen->text_normal;

    int pix_per_char = 6;

    // Break message into lines
    std::list<std::string> ls = explode(message, '\n');

    // Get the max dimensions needed to display it
    int w = strlen(title)*9;
    int h = 30 + 10*ls.size();
    for(auto& line : ls)
    {
        if(int(line.size()*pix_per_char) > w)
            w = line.size()*pix_per_char;
    }

    // Centered bounds
    int leftside  = 160 - w/2 - 12;
    int rightside = 160 + w/2 + 12;
    int j = 0;

    int dumbcount;

    if (localbuttons)
        delete (localbuttons);

    button* buttons = no_or_yes_buttons;
    int num_buttons = 2;
    int highlighted_button = (default_value? 1 : 0);
    localbuttons = init_buttons(buttons, num_buttons);

    grab_mouse();
    clear_keyboard();

    clear_key_press_event();

    int retvalue = 0;
    while (retvalue == 0)
    {
        // Input - leftmouse will poll events via query_mouse()
        if(leftmouse(buttons))
            retvalue = localbuttons->leftclick();

        // Check keyboard after leftmouse has polled events
        if(query_key_press_event())
        {
            if(keystates[KEYSTATE_y])
                retvalue = YES_VALUE;
            else if(keystates[KEYSTATE_n])
                retvalue = NO_VALUE;
            else if(keystates[KEYSTATE_ESCAPE])
                break;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);

        // Draw
        dumbcount = myscreen->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
        j = 0;
        for(auto& line : ls)
        {
            gladtext.write_xy(dumbcount + 3*pix_per_char/2, 104 - h/2 + 10*j, line.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
            j++;
        }

        draw_buttons(buttons, num_buttons);

        draw_highlight_interior(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
    }

    if(retvalue == YES_VALUE)
    {
        Log("YES\n");
        return true;
    }
    if(retvalue == NO_VALUE)
    {
        Log("NO\n");
        return false;
    }
    return default_value;
}

void popup_dialog(const char* title, const char* message)
{
    Log("{}, {}\n", title, message);
#ifdef TESTING
    TRACE("popup", "%s: %s", title, message);
    return;
#endif

    myscreen->darken_screen();

    text& gladtext = myscreen->text_normal;

    int pix_per_char = 6;

    // Break message into lines
    std::list<std::string> ls = explode(message, '\n');

    // Get the max dimensions needed to display it
    int w = strlen(title)*9;
    int h = 30 + 10*ls.size();
    for(auto& line : ls)
    {
        if(int(line.size()*pix_per_char) > w)
            w = line.size()*pix_per_char;
    }

    // Centered bounds
    int leftside  = 160 - w/2 - 12;
    int rightside = 160 + w/2 + 12;

    // Draw background
    int dumbcount;

    // Draw message
    int j = 0;

    if (localbuttons)
        delete (localbuttons);

    button* buttons = popup_dialog_buttons;
    int num_buttons = 1;
    int highlighted_button = 0;
    localbuttons = init_buttons(buttons, num_buttons);

    grab_mouse();
    clear_keyboard();

    clear_key_press_event();

    int retvalue = 0;
    while (retvalue == 0)
    {
        // Input - leftmouse will poll events via query_mouse()
        if(leftmouse(buttons))
            retvalue = localbuttons->leftclick();

        // Check keyboard after leftmouse has polled events
        if(query_key_press_event())
        {
            if(keystates[KEYSTATE_RETURN] || keystates[KEYSTATE_SPACE] || keystates[KEYSTATE_ESCAPE])
                break;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);

        // Draw
        dumbcount = myscreen->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
        j = 0;
        for(auto& line : ls)
        {
            gladtext.write_xy(dumbcount + 3*pix_per_char/2 + w/2 - line.size()*pix_per_char/2, 104 - h/2 + 10*j, line.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
            j++;
        }

        draw_buttons(buttons, num_buttons);

        draw_highlight_interior(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
    }
}
