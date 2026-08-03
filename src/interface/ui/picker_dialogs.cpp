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

#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/core/text_wrap.h>
#include <openglad/core/util.h>
#include <openglad/resources/io_common.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>

#include <algorithm>
#include <cstring>
#include <array>
#include <string>
#include <vector>

namespace {
constexpr int YES_VALUE = 5;
constexpr int NO_VALUE = 6;
constexpr int PIX_PER_CHAR = 6;
// Widest dialog message line that keeps the frame on the 320px screen:
// 46*6 = 276px of text + 24px of frame = 300 (issue #152). Longer messages
// word-wrap instead of pushing the dialog off-screen.
constexpr int kMaxDialogChars = 46;

static const button k_yes_or_no_buttons[] =
    {
        button("yes", "YES", KEYSTATE_UNKNOWN,  70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), YES_VALUE, MenuNav{.right=1}),
        button("no", "NO", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), NO_VALUE, MenuNav{.left=0})
    };

static const button k_no_or_yes_buttons[] =
    {
        button("no", "NO", KEYSTATE_UNKNOWN,  70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), NO_VALUE, MenuNav{.right=1}),
        button("yes", "YES", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), YES_VALUE, MenuNav{.left=0})
    };

static const button k_popup_dialog_buttons[] =
    {
        button("ok", "OK", KEYSTATE_ESCAPE,  160 - 25, 130, 50, 20, button_action_id(ButtonAction::YesOrNo), YES_VALUE, MenuNav{})
    };

// Compute centered dialog bounds from a title and message lines.
struct DialogBounds {
    int w, h, leftside, rightside;
};

// Get the max dimensions needed to display it
DialogBounds compute_dialog_bounds(const char* title, const std::vector<std::string>& lines)
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

    video& output = *og::runtime::current_session->myscreen_;
    ScopedUiCanvas canvas_target(output);

    og::runtime::current_session->myscreen_->darken_screen();

    text& gladtext = og::runtime::current_session->myscreen_->text_normal;

    // Wrap over-long messages so the frame stays on-screen (issue #152);
    // a single-line message renders exactly as it always has.
    const std::vector<std::string> lines =
        og::core::wrap_text(message, kMaxDialogChars);
    int width = 0;
    for (const std::string& line : lines)
        width = std::max(width, static_cast<int>(line.size()) * PIX_PER_CHAR);
    int leftside  = 160 - width/2 - 12;
    int rightside = 160 + width/2 + 12;
    const int bottom = 110 + 10 * std::max(0, static_cast<int>(lines.size()) - 1);

    og::runtime::current_session->myscreen_->draw_button(leftside, 80, rightside, bottom, 1);
    int line_y = 94;
    for (const std::string& line : lines)
    {
        gladtext.write_xy(160 - static_cast<int>(line.size()) * PIX_PER_CHAR / 2,
                          line_y, line.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
        line_y += 10;
    }

    og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200); // refresh screen

    grab_mouse();
    clear_keyboard();

    clear_key_press_event();

    Uint32 start_time = og::input_native::ticks_ms();
    while (static_cast<float>(og::input_native::ticks_ms() - start_time)/1000.0f < delay_seconds)
    {
        get_input_events(POLL);

        if(query_mouse().left || query_key_press_event())
            break;

        og::input_native::sleep_ms(10);
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
        // Both prompt shapes consume the override queue: the NO-first
        // no_or_yes confirms (§2.0 U3 — company delete/restore) are driven
        // by tests exactly like the legacy YES-first prompts. The queue is
        // never shared across shapes in any flow (a queued value always
        // targets the next prompt on its path).
        TRACE("confirm", "%s: %s", title, message);
        if (!s_yes_or_no_overrides.empty())
        {
            bool v = s_yes_or_no_overrides.front();
            s_yes_or_no_overrides.erase(s_yes_or_no_overrides.begin());
            return v;
        }
        return default_value;
    }
#endif

    video& output = *og::runtime::current_session->myscreen_;
    ScopedUiCanvas canvas_target(output);

    og::runtime::current_session->myscreen_->darken_screen();

    text& gladtext = og::runtime::current_session->myscreen_->text_normal;

    // Break message into lines, wrapping over-long ones (issue #152).
    std::vector<std::string> ls = og::core::wrap_text(message, kMaxDialogChars);
    auto [w, h, leftside, rightside] = compute_dialog_bounds(title, ls);

    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    std::array<button, 2> dialog_buttons = yes_first
        ? std::array<button, 2>{k_yes_or_no_buttons[0], k_yes_or_no_buttons[1]}
        : std::array<button, 2>{k_no_or_yes_buttons[0], k_no_or_yes_buttons[1]};
    button* buttons = dialog_buttons.data();
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
        // Input - leftmouse will poll events via query_mouse()
        if(leftmouse(buttons))
            retvalue = og::runtime::current_session->localbuttons_->leftclick();

        // Check keyboard after leftmouse has polled events
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
        og::input_native::sleep_ms(10);
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

    video& output = *og::runtime::current_session->myscreen_;
    ScopedUiCanvas canvas_target(output);

    og::runtime::current_session->myscreen_->darken_screen();

    text& gladtext = og::runtime::current_session->myscreen_->text_normal;

    std::vector<std::string> ls = og::core::wrap_text(message, kMaxDialogChars);
    auto [w, h, leftside, rightside] = compute_dialog_bounds(title, ls);

    // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    std::array<button, 1> dialog_buttons = {k_popup_dialog_buttons[0]};
    button* buttons = dialog_buttons.data();
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
        og::input_native::sleep_ms(10);
    }
}
