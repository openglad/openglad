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

#include "base.h"
#include "button.h"
#include "input.h"

#include "SDL.h"

namespace {
constexpr Sint32 OK_VALUE = 4;
constexpr Sint32 REDRAW_VALUE = 2;

#ifdef USE_CONTROLLER_INPUT
constexpr bool MENU_NAV_DEFAULT = true;
#else
constexpr bool MENU_NAV_DEFAULT = false;
#endif
} // namespace

bool menu_nav_enabled = MENU_NAV_DEFAULT;
Uint32 menu_nav_enabled_time = 0;

Sint32 leftmouse(button* buttons)
{
    // Use static variables for edge detection instead of blocking while loops
    // This prevents ASYNCIFY state corruption in Emscripten
    static bool was_left_down = false;
    static bool was_right_down = false;

    Sint32 i = 0;
    Sint32 somebutton = -1;

    grab_mouse();
    MouseState& mymouse = query_mouse();

    while (allbuttons[i])
    {
        if(buttons != nullptr && !buttons[i].hidden)
        {
            allbuttons[i]->mouse_on();
            if (keystates[allbuttons[i]->hotkey])
                somebutton = i;
        }
        i++;
    }

    if (somebutton != -1)
    {
        return 1;  // simulate left-click
    }

    // Detect click transitions (button went from up to down)
    bool left_clicked = mymouse.left && !was_left_down;
    bool right_clicked = mymouse.right && !was_right_down;

    // Update state for next frame
    was_left_down = mymouse.left;
    was_right_down = mymouse.right;

    if (left_clicked)
        return 1;
    if (right_clicked)
        return 2; // for right-mouse
    return 0;
}

void draw_highlight_interior(const button& b)
{
    if(!menu_nav_enabled)
        return;

    float t = (1.0f + sinf(SDL_GetTicks()/300.0f))/2.0f;
    float size = 3;
    myscreen->draw_box(b.x + t*size, b.y + t*size, b.x + b.sizex - t*size, b.y + b.sizey - t*size, YELLOW, 0);
}

void draw_highlight(const button& b)
{
    if(!menu_nav_enabled)
        return;

    float t = (1.0f + sinf(SDL_GetTicks()/300.0f))/2.0f;
    float size = 3;
    myscreen->draw_box(b.x - t*size, b.y - t*size, b.x + b.sizex + t*size, b.y + b.sizey + t*size, YELLOW, 0);
}

bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons)
{
    int next_button = -1;
    bool pressed = false;
    bool activated = false;
    if(isPlayerHoldingKey(0, KEY_UP))
    {
        while(isPlayerHoldingKey(0, KEY_UP))
        {
            SDL_Delay(1);
            get_input_events(POLL);
        }
        next_button = buttons[highlighted_button].nav.up;

        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_DOWN))
    {
        while(isPlayerHoldingKey(0, KEY_DOWN))
        {
            SDL_Delay(1);
            get_input_events(POLL);
        }
        next_button = buttons[highlighted_button].nav.down;

        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_LEFT))
    {
        while(isPlayerHoldingKey(0, KEY_LEFT))
        {
            SDL_Delay(1);
            get_input_events(POLL);
        }
        next_button = buttons[highlighted_button].nav.left;

        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_RIGHT))
    {
        while(isPlayerHoldingKey(0, KEY_RIGHT))
        {
            SDL_Delay(1);
            get_input_events(POLL);
        }
        next_button = buttons[highlighted_button].nav.right;

        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_FIRE))
    {
        while(isPlayerHoldingKey(0, KEY_FIRE))
        {
            SDL_Delay(1);
            get_input_events(POLL);
        }

        if(!menu_nav_enabled)
            pressed = true;
        else
        {
            myscreen->soundp->play_sound(SOUND_BOW);
            if(use_global_vbuttons)
            {
                allbuttons[highlighted_button]->vdisplay(1);
                allbuttons[highlighted_button]->vdisplay();
                if(allbuttons[highlighted_button]->myfunc)
                {
                    retvalue = allbuttons[highlighted_button]->do_call(allbuttons[highlighted_button]->myfunc, allbuttons[highlighted_button]->arg);
                }
            }
            else
            {
                retvalue = OK_VALUE;
            }

            pressed = true;
            activated = true;
        }
    }

    if(next_button >= 0 && !buttons[next_button].hidden)
        highlighted_button = next_button;

    // Turn menu_nav on if something was pressed.
    if(pressed)
    {
        menu_nav_enabled = true;
        menu_nav_enabled_time = SDL_GetTicks();
    }
    // Turn it off if it's been a while since something was pressed.
    else if(menu_nav_enabled)
    {
        if(SDL_GetTicks() - menu_nav_enabled_time > 5000)
            menu_nav_enabled = MENU_NAV_DEFAULT;
    }

    return activated;
}

bool reset_buttons(vbutton*& localbuttons, button* buttons, int num_buttons, Sint32& retvalue)
{
    if(localbuttons && (retvalue == OK_VALUE || retvalue == REDRAW_VALUE))
    {
        localbuttons = init_buttons(buttons, num_buttons);

        retvalue = 0;
        return true;
    }
    return false;
}
