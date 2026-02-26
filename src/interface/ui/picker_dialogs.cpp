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

#include <openglad/interface/input/button.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/input/input.h>
#include <openglad/core/util.h>
#include <openglad/platform/io.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/interface/screen.h>

#include "SDL.h"

#include <array>
#include <cstring>
#include <list>
#include <string>
#include <vector>

namespace {
constexpr int YES_VALUE = 5;
constexpr int NO_VALUE = 6;
constexpr int PIX_PER_CHAR = 6;

const std::array<button, 2>& yes_or_no_button_template()
{
    static const std::array<button, 2> buttons = {
        button("yes", "YES", KEYSTATE_UNKNOWN,  70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), YES_VALUE, MenuNav{.right=1}),
        button("no", "NO", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), NO_VALUE, MenuNav{.left=0}),
    };
    return buttons;
}

const std::array<button, 2>& no_or_yes_button_template()
{
    static const std::array<button, 2> buttons = {
        button("no", "NO", KEYSTATE_UNKNOWN,  70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), NO_VALUE, MenuNav{.right=1}),
        button("yes", "YES", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), YES_VALUE, MenuNav{.left=0}),
    };
    return buttons;
}

const std::array<button, 1>& popup_dialog_button_template()
{
    static const std::array<button, 1> buttons = {
        button("ok", "OK", KEYSTATE_ESCAPE,  160 - 25, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), YES_VALUE, MenuNav{}),
    };
    return buttons;
}

// Compute centered dialog bounds from a title and message lines.
struct DialogBounds {
    int w, h, leftside, rightside;
};

DialogBounds compute_dialog_bounds(const char* title, const std::list<std::string>& lines)
{
    int w = static_cast<int>(strlen(title)) * 9;
    int h = 30 + 10 * static_cast<int>(lines.size());
    for (auto& line : lines)
    {
        const int line_width = static_cast<int>(line.size()) * PIX_PER_CHAR;
        if (line_width > w)
            w = line_width;
    }
    return { w, h, 160 - w/2 - 12, 160 + w/2 + 12 };
}

} // namespace


Sint32 leftmouse(button* buttons);
void draw_highlight_interior(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons, Sint32& retvalue);

void timed_dialog(const char* message, float delay_seconds)
{
    Log("{}\n", message);

    og::runtime::current_session->myscreen_->darken_screen();

    text& gladtext = og::runtime::current_session->myscreen_->text_normal;

    int len = static_cast<int>(strlen(message));
    int width = len * PIX_PER_CHAR;
    int leftside  = 160 - width/2 - 12;
    int rightside = 160 + width/2 + 12;

    og::runtime::current_session->myscreen_->draw_button(leftside, 80, rightside, 110, 1);
    gladtext.write_xy(160 - width/2, 94, message, static_cast<unsigned char>(DARK_BLUE), 1);

    og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200); // refresh screen

    grab_mouse();
    clear_keyboard();

    clear_key_press_event();

    Uint32 start_time = SDL_GetTicks();
    while (static_cast<float>(SDL_GetTicks() - start_time)/1000.0f < delay_seconds)
    {
        get_input_events(POLL);

        if(query_mouse().left || query_key_press_event())
            break;

        SDL_Delay(10);
    }
}

#ifdef TESTING
namespace
{
// Optional test-only override for yes_or_no_prompt(). When empty, the function
// falls back to returning the provided default_value.
std::vector<bool> s_yes_or_no_overrides;
bool s_force_real_dialogs = false;
}

void picker_testing_yes_or_no_queue_clear()
{
    s_yes_or_no_overrides.clear();
}

void picker_testing_yes_or_no_queue_push(bool value)
{
    s_yes_or_no_overrides.push_back(value);
}

void picker_testing_set_force_real_dialogs(bool enabled)
{
    s_force_real_dialogs = enabled;
}

int picker_testing_yes_or_no_queue_remaining()
{
    return static_cast<int>(s_yes_or_no_overrides.size());
}
#endif

// Shared implementation for yes/no prompts.
// When yes_first is true, YES is on the left (default highlight maps directly).
// When yes_first is false, NO is on the left (default highlight is inverted).
static bool yes_no_prompt_impl(const char* title, const char* message, bool default_value, bool yes_first)
{
    Log("{}, {}: \n", title, message);
#ifdef TESTING
    if (!s_force_real_dialogs)
    {
        if (yes_first && !s_yes_or_no_overrides.empty())
        {
            bool v = s_yes_or_no_overrides.front();
            s_yes_or_no_overrides.erase(s_yes_or_no_overrides.begin());
            return v;
        }
        return default_value;
    }
#endif

    og::runtime::current_session->myscreen_->darken_screen();

    text& gladtext = og::runtime::current_session->myscreen_->text_normal;

    std::list<std::string> ls = explode(message, '\n');
    auto [w, h, leftside, rightside] = compute_dialog_bounds(title, ls);

    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    std::array<button, 2> button_storage =
        yes_first ? yes_or_no_button_template() : no_or_yes_button_template();
    button* buttons = button_storage.data();
    int num_buttons = 2;
    int highlighted_button = yes_first ? (default_value ? 0 : 1)
                                       : (default_value ? 1 : 0);
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

    grab_mouse();
    clear_keyboard();
    clear_key_press_event();

    int retvalue = 0;
    while (retvalue == 0)
    {
        if(leftmouse(buttons))
            retvalue = og::runtime::current_session->localbuttons_->leftclick();

        if(query_key_press_event())
        {
            if(og::runtime::current_session->keystates_[KEYSTATE_y])
                retvalue = YES_VALUE;
            else if(og::runtime::current_session->keystates_[KEYSTATE_n])
                retvalue = NO_VALUE;
            else if(og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
                break;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);
        reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue);

        int dumbcount = og::runtime::current_session->myscreen_->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
        int j = 0;
        for(auto& line : ls)
        {
            gladtext.write_xy(dumbcount + 3*PIX_PER_CHAR/2, 104 - h/2 + 10*j, line.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
            j++;
        }

        draw_buttons(buttons, num_buttons);
        draw_highlight_interior(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
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

bool yes_or_no_prompt(const char* title, const char* message, bool default_value)
{
    return yes_no_prompt_impl(title, message, default_value, true);
}

bool no_or_yes_prompt(const char* title, const char* message, bool default_value)
{
    return yes_no_prompt_impl(title, message, default_value, false);
}

void popup_dialog(const char* title, const char* message)
{
    Log("{}, {}\n", title, message);
#ifdef TESTING
    if (!s_force_real_dialogs)
    {
        TRACE("popup", "%s: %s", title, message);
        return;
    }
#endif

    og::runtime::current_session->myscreen_->darken_screen();

    text& gladtext = og::runtime::current_session->myscreen_->text_normal;

    std::list<std::string> ls = explode(message, '\n');
    auto [w, h, leftside, rightside] = compute_dialog_bounds(title, ls);

    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    std::array<button, 1> button_storage = popup_dialog_button_template();
    button* buttons = button_storage.data();
    int num_buttons = 1;
    int highlighted_button = 0;
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

    grab_mouse();
    clear_keyboard();
    clear_key_press_event();

    int retvalue = 0;
    while (retvalue == 0)
    {
        if(leftmouse(buttons))
            retvalue = og::runtime::current_session->localbuttons_->leftclick();

        if(query_key_press_event())
        {
            if(og::runtime::current_session->keystates_[KEYSTATE_RETURN] || og::runtime::current_session->keystates_[KEYSTATE_SPACE] || og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
                break;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);
        reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue);

        int dumbcount = og::runtime::current_session->myscreen_->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
        int j = 0;
        for(auto& line : ls)
        {
            gladtext.write_xy(dumbcount + 3*PIX_PER_CHAR/2 + w/2 - static_cast<Sint32>(line.size())*PIX_PER_CHAR/2, 104 - h/2 + 10*j, line.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
            j++;
        }

        draw_buttons(buttons, num_buttons);
        draw_highlight_interior(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
    }
}
