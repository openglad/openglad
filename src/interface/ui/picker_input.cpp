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

// TODO: Migrate isPlayerHoldingKey() calls in handle_menu_nav() to use
// InputState/InputAction once the menu navigation loop is refactored from
// its current spin-wait (while + sleep + get_input_events) pattern
// to a frame-based design.

#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/interface/native_input.h>

#include <cstdint>

namespace {
constexpr Sint32 OK_VALUE = 4;
constexpr Sint32 REDRAW_VALUE = 2;

#ifdef USE_CONTROLLER_INPUT
constexpr bool MENU_NAV_DEFAULT = true;
#else
constexpr bool MENU_NAV_DEFAULT = false;
#endif
} // namespace

static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

Sint32 leftmouse(button* buttons)
{
    Sint32 i = 0;
    Sint32 somebutton = -1;

    grab_mouse();
    MouseState& mymouse = query_mouse();
    InputHardwareState& input_hw = input_hardware_state();

    while (i < static_cast<Sint32>(og::runtime::current_session->allbuttons_.size())
           && og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)])
    {
        if(buttons != nullptr && !buttons[i].hidden)
        {
            og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->mouse_on();
            if (og::runtime::current_session->keystates_[og::runtime::current_session->allbuttons_[static_cast<std::size_t>(i)]->hotkey])
                somebutton = i;
        }
        i++;
    }

    if (somebutton != -1)
    {
        return 1;  // simulate left-click
    }

    // Detect click transitions (button went from up to down). Collapsed
    // touch taps never show up as a sampled transition, so consume the
    // pending-click queue as well.
    bool left_clicked = (mymouse.left && !input_hw.picker_was_left_down) ||
        take_pending_left_click();
    bool right_clicked = (mymouse.right && !input_hw.picker_was_right_down) ||
        take_pending_right_click();

    // Update state for next frame
    input_hw.picker_was_left_down = mymouse.left;
    input_hw.picker_was_right_down = mymouse.right;

    if (left_clicked)
        return 1;
    if (right_clicked)
        return 2; // for right-mouse
    return 0;
}

void draw_highlight_interior(const button& b)
{
    if(!pks().menu_nav_enabled)
        return;

    const float ticks = static_cast<float>(og::input_native::ticks_ms());
    const float t = (1.0f + sinf(ticks / 300.0f)) * 0.5f;
    const float size = 3.0f;
    const float inset = t * size;
    og::runtime::current_session->myscreen_->draw_box(static_cast<Sint32>(static_cast<float>(b.x) + inset),
                       static_cast<Sint32>(static_cast<float>(b.y) + inset),
                       static_cast<Sint32>(static_cast<float>(b.x + b.sizex) - inset),
                       static_cast<Sint32>(static_cast<float>(b.y + b.sizey) - inset),
                       YELLOW,
                       0);
}

void draw_highlight(const button& b)
{
    if(!pks().menu_nav_enabled)
        return;

    const float ticks = static_cast<float>(og::input_native::ticks_ms());
    const float t = (1.0f + sinf(ticks / 300.0f)) * 0.5f;
    const float size = 3.0f;
    const float inset = t * size;
    og::runtime::current_session->myscreen_->draw_box(static_cast<Sint32>(static_cast<float>(b.x) - inset),
                       static_cast<Sint32>(static_cast<float>(b.y) - inset),
                       static_cast<Sint32>(static_cast<float>(b.x + b.sizex) + inset),
                       static_cast<Sint32>(static_cast<float>(b.y + b.sizey) + inset),
                       YELLOW,
                       0);
}

#ifdef TESTING
// FX-capture hook (scripts/fx_review): injector threads set this to a KEY_*
// direction to drive one keyboard-nav step, so captures show the highlight
// box moving. Real key events can't be used from an injector thread — the
// blocking hold-and-release loops below eat them mid-press.
int g_test_menu_nav_key = -1;
#endif

namespace {
// Menu-nav release waits are BOUNDED. The legacy shape spun until the
// pressed input released, which is fine for keyboards (releases always
// arrive) but hangs on joysticks: a stick drifting past JOY_DEAD_ZONE or a
// trigger-style axis resting at an extreme reads held forever through the
// polled JoyData::getState — no event can ever end the loop (the real-pad
// INPUT-cycler hang). A press that outlives the budget still delivers its
// one step (a deliberate long hold earns its action), then latches as
// "stuck": it produces nothing further until one real release is observed.
// The latch preserves the double-activation protection the spin existed for
// — an input still held when the next screen's loop starts must not fire
// again — across screens, because only an observed release clears it.
constexpr std::uint32_t MENU_NAV_RELEASE_BUDGET_MS = 400;
bool menu_nav_key_stuck[NUM_KEYS] = {};

// True when `key_enum` produced a fresh press for this call.
bool take_menu_nav_press(int key_enum)
{
    if(!isPlayerHoldingKey(0, key_enum))
    {
        menu_nav_key_stuck[key_enum] = false; // real release observed
        return false;
    }
    if(menu_nav_key_stuck[key_enum])
        return false; // the press that ran out the budget, still held
    const std::uint32_t start = og::input_native::ticks_ms();
    while(isPlayerHoldingKey(0, key_enum))
    {
        if(og::input_native::ticks_ms() - start >= MENU_NAV_RELEASE_BUDGET_MS)
        {
            menu_nav_key_stuck[key_enum] = true;
            break;
        }
        og::input_native::sleep_ms(1);
        get_input_events(POLL);
    }
    return true;
}
} // namespace

bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons)
{
    int next_button = -1;
    bool pressed = false;
    bool activated = false;
#ifdef TESTING
    if (g_test_menu_nav_key >= 0)
    {
        const int k = g_test_menu_nav_key;
        g_test_menu_nav_key = -1;
        if (k == KEY_UP) next_button = buttons[highlighted_button].nav.up;
        if (k == KEY_DOWN) next_button = buttons[highlighted_button].nav.down;
        if (k == KEY_LEFT) next_button = buttons[highlighted_button].nav.left;
        if (k == KEY_RIGHT) next_button = buttons[highlighted_button].nav.right;
        pressed = true;
    }
#endif
    if(take_menu_nav_press(KEY_UP))
    {
        next_button = buttons[highlighted_button].nav.up;

        pressed = true;
    }
    if(take_menu_nav_press(KEY_DOWN))
    {
        next_button = buttons[highlighted_button].nav.down;

        pressed = true;
    }
    if(take_menu_nav_press(KEY_LEFT))
    {
        next_button = buttons[highlighted_button].nav.left;

        pressed = true;
    }
    if(take_menu_nav_press(KEY_RIGHT))
    {
        next_button = buttons[highlighted_button].nav.right;

        pressed = true;
    }
    if(take_menu_nav_press(KEY_FIRE))
    {
        if(!pks().menu_nav_enabled)
            pressed = true;
        else
        {
            og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
            if(use_global_vbuttons)
            {
                og::runtime::current_session->allbuttons_[static_cast<std::size_t>(highlighted_button)]->vdisplay(1);
                og::runtime::current_session->allbuttons_[static_cast<std::size_t>(highlighted_button)]->vdisplay();
                if(og::runtime::current_session->allbuttons_[static_cast<std::size_t>(highlighted_button)]->myfunc)
                {
                    // Coordinate-free activation: never leave a stale pointer
                    // for sub-rect affordances (#202).
                    pks().menu_click_x = -1;
                    pks().menu_click_y = -1;
                    retvalue = og::runtime::current_session->allbuttons_[static_cast<std::size_t>(highlighted_button)]->do_call(og::runtime::current_session->allbuttons_[static_cast<std::size_t>(highlighted_button)]->myfunc, og::runtime::current_session->allbuttons_[static_cast<std::size_t>(highlighted_button)]->arg);
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
        pks().menu_nav_enabled = true;
        pks().menu_nav_enabled_time = og::input_native::ticks_ms();
    }
    // Turn it off if it's been a while since something was pressed.
    else if(pks().menu_nav_enabled)
    {
        if(og::input_native::ticks_ms() - pks().menu_nav_enabled_time > 5000)
            pks().menu_nav_enabled = MENU_NAV_DEFAULT;
    }

    return activated;
}

bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons, Sint32& retvalue)
{
    if(local_btns && (retvalue == OK_VALUE || retvalue == REDRAW_VALUE))
    {
        local_btns = init_buttons(buttons, num_buttons);

        retvalue = 0;
        return true;
    }
    return false;
}
