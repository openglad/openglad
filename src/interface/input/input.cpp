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
//
// input.cpp
//
// input code
//

#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/core/util.h>
#include <openglad/core/test_trace.h>
#include <algorithm> //buffers: for std::min when clamping joystick counts
#include <cstdio>
#include <ctime>
#include <cstring> //buffers: for strlen
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define YIELD_SLEEP(ms) emscripten_sleep(ms)
#else
#define YIELD_SLEEP(ms) og::input_native::sleep_ms(ms)
#endif

#ifdef OUYA
#include <openglad/interface/OuyaController.h>
#endif

void quit(Sint32 arg1);

// Input scalar globals (raw_key, player_keys, keystates, viewport_*, etc.)
// are now members of GameSession, accessed via macros defined in input.h.

int (&input_player_keys())[4][NUM_KEYS]
{
    return og::runtime::current_session->player_keys_;
}

int& input_raw_key_ref()
{
    return og::runtime::current_session->raw_key_;
}

std::string& input_raw_text_input_ref()
{
    return og::runtime::current_session->raw_text_input_;
}

bool& input_continue_ref()
{
    return og::runtime::current_session->input_continue_;
}

short& input_scroll_amount_ref()
{
    return og::runtime::current_session->scroll_amount_;
}

short& input_key_press_event_ref()
{
    return og::runtime::current_session->key_press_event_;
}

short& input_text_input_event_ref()
{
    return og::runtime::current_session->text_input_event_;
}

// Input hardware state now lives in GameSession::input_hw_ (InputHardwareState).
// Access via hw() helper for fields without macros; mouse_state and player_joy
// are already macros defined in input.h.
static inline auto& hw() { return input_hardware_state(); }

void update_overscan_setting()
{
    if(og::runtime::current_session->overscan_percentage_ < 0.0f)
        og::runtime::current_session->overscan_percentage_ = 0.0f;
    else if(og::runtime::current_session->overscan_percentage_ > 0.25f)
        og::runtime::current_session->overscan_percentage_ = 0.25f;
    
    og::runtime::current_session->viewport_offset_x_ = og::runtime::current_session->window_w_ * og::runtime::current_session->overscan_percentage_/2;
    og::runtime::current_session->viewport_offset_y_ = og::runtime::current_session->window_h_ * og::runtime::current_session->overscan_percentage_/2;
    og::runtime::current_session->viewport_w_ = og::runtime::current_session->window_w_ * (1.0f - og::runtime::current_session->overscan_percentage_);
    og::runtime::current_session->viewport_h_ = og::runtime::current_session->window_h_ * (1.0f - og::runtime::current_session->overscan_percentage_);
}

#define JOY_DEAD_ZONE 8000
#define MAX_NUM_JOYSTICKS 10  // Just in case there are joysticks attached that are not useable (e.g. accelerometer)
og::input_native::JoystickHandle joysticks[MAX_NUM_JOYSTICKS];

namespace
{
bool as_event_data(const void* native_event, og::input_native::EventData& out)
{
    return og::input_native::decode_event(native_event, out);
}

} // namespace


//
// Input routines (for handling all events and then setting the appropriate vars)
//

void init_input()
{
    reset_default_player_controls();
    og::runtime::current_session->keystates_ = og::input_native::keyboard_state();

    // Set up joysticks
    for(int i = 0; i < MAX_NUM_JOYSTICKS; i++)
    {
        joysticks[i] = nullptr;
    }

    const int numjoy = std::min(og::input_native::num_joysticks(), MAX_NUM_JOYSTICKS);

    for(int i = 0; i < numjoy; i++)
    {
        joysticks[i] = og::input_native::joystick_open(i);
        if(joysticks[i] == nullptr)
            continue;
        if(i < 4) // player_joy has only 4 slots
            player_joy[i] = JoyData(i);
    }

    og::input_native::joystick_set_event_state(true);
}

void get_input_events(bool type)
{
    //key_press_event = 0;

    if (type == POLL)
    {
        while (const void* event = og::input_native::poll_event())
            handle_events(event);
    }
    if (type == WAIT)
    {
        if (const void* event = og::input_native::wait_event())
            handle_events(event);
    }
}

#ifdef USE_TOUCH_INPUT

#endif

void sendFakeKeyDownEvent(int keycode)
{
    og::input_native::push_key_event(true, keycode);
}

void sendFakeKeyUpEvent(int keycode)
{
    og::input_native::push_key_event(false, keycode);
}

// handle_window_event and handle_key_event are implemented in
// runtime/input_event_bridge.cpp to avoid runtime/render deps in input module.

void handle_text_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    og::runtime::current_session->raw_text_input_ = event.text;
    og::runtime::current_session->text_input_event_ = 1;
}

void handle_mouse_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    switch(event.type)
    {
    case og::input_native::EventType::MouseWheel:
        og::runtime::current_session->scroll_amount_ = static_cast<short>(5*event.wheel_y);
        og::runtime::current_session->key_press_event_ = 1;
        break;

#ifndef USE_TOUCH_INPUT
        // Mouse event
    case og::input_native::EventType::MouseMotion:
        mouse_state.x = (static_cast<float>(event.motion_x) - og::runtime::current_session->viewport_offset_x_) * (320.0f / og::runtime::current_session->viewport_w_);
        mouse_state.y = (static_cast<float>(event.motion_y) - og::runtime::current_session->viewport_offset_y_) * (200.0f / og::runtime::current_session->viewport_h_);
        break;
    case og::input_native::EventType::MouseButtonUp:
        if (event.button == og::input_native::kMouseButtonLeft)
            mouse_state.left = 0;
        if (event.button == og::input_native::kMouseButtonRight)
            mouse_state.right = 0;

        mouse_state.x = (static_cast<float>(event.button_x) - og::runtime::current_session->viewport_offset_x_) * (320.0f / og::runtime::current_session->viewport_w_);
        mouse_state.y = (static_cast<float>(event.button_y) - og::runtime::current_session->viewport_offset_y_) * (200.0f / og::runtime::current_session->viewport_h_);
        break;
    case og::input_native::EventType::MouseButtonDown:
        if (event.button == og::input_native::kMouseButtonLeft)
            mouse_state.left = 1;
        else if (event.button == og::input_native::kMouseButtonRight)
            mouse_state.right = 1;

        mouse_state.x = (static_cast<float>(event.button_x) - og::runtime::current_session->viewport_offset_x_) * (320.0f / og::runtime::current_session->viewport_w_);
        mouse_state.y = (static_cast<float>(event.button_y) - og::runtime::current_session->viewport_offset_y_) * (200.0f / og::runtime::current_session->viewport_h_);
        break;
#else
#ifdef FAKE_TOUCH_EVENTS
    // Convert SDL mouse events to fake SDL touch events
    case og::input_native::EventType::MouseMotion:
        {
            og::input_native::push_touch_event(
                og::input_native::EventType::FingerMotion,
                event.motion_x / og::runtime::current_session->window_w_,
                event.motion_y / og::runtime::current_session->window_h_,
                event.motion_dx / og::runtime::current_session->window_w_,
                event.motion_dy / og::runtime::current_session->window_h_,
                1);
        }
        break;
    case og::input_native::EventType::MouseButtonUp:
        {
            og::input_native::push_touch_event(
                og::input_native::EventType::FingerUp,
                event.button_x / og::runtime::current_session->window_w_,
                event.button_y / og::runtime::current_session->window_h_,
                0.0f, 0.0f,
                1);
        }
        break;
    case og::input_native::EventType::MouseButtonDown:
        {
            og::input_native::push_touch_event(
                og::input_native::EventType::FingerDown,
                event.button_x / og::runtime::current_session->window_w_,
                event.button_y / og::runtime::current_session->window_h_,
                0.0f, 0.0f,
                1);
        }
        break;
#endif
        // Mouse event
    case og::input_native::EventType::FingerMotion:
        {
        int x = (event.finger_x * og::runtime::current_session->window_w_ - og::runtime::current_session->viewport_offset_x_) * (320 / og::runtime::current_session->viewport_w_);
        int y = (event.finger_y * og::runtime::current_session->window_h_ - og::runtime::current_session->viewport_offset_y_) * (200 / og::runtime::current_session->viewport_h_);
        
        og::runtime::current_session->scroll_amount_ = y - mouse_state.y;
        
        mouse_state.x = x;
        mouse_state.y = y;
        
        if(hw().moving && event.finger_id == hw().movingTouch)
        {
            hw().moving_touch_target_x = x;
            hw().moving_touch_target_y = y;
            
            hw().touch_keystate[0][KEY_UP] = false;
            hw().touch_keystate[0][KEY_UP_RIGHT] = false;
            hw().touch_keystate[0][KEY_RIGHT] = false;
            hw().touch_keystate[0][KEY_DOWN_RIGHT] = false;
            hw().touch_keystate[0][KEY_DOWN] = false;
            hw().touch_keystate[0][KEY_DOWN_LEFT] = false;
            hw().touch_keystate[0][KEY_LEFT] = false;
            hw().touch_keystate[0][KEY_UP_LEFT] = false;
            
            if(abs(x - hw().moving_touch_x) > MOVE_DEAD_ZONE || abs(y - hw().moving_touch_y) > MOVE_DEAD_ZONE)
            {
                float offset = -M_PI + M_PI/8;
                float interval = M_PI/4;
                float angle = atan2(y - hw().moving_touch_y, x - hw().moving_touch_x);
                if(angle < -M_PI + M_PI/8 || angle >= M_PI - M_PI/8)
                    hw().touch_keystate[0][KEY_LEFT] = true;
                else if(angle >= offset && angle < offset + interval)
                    hw().touch_keystate[0][KEY_UP_LEFT] = true;
                else if(angle >= offset + interval && angle < offset + 2*interval)
                    hw().touch_keystate[0][KEY_UP] = true;
                else if(angle >= offset + 2*interval && angle < offset + 3*interval)
                    hw().touch_keystate[0][KEY_UP_RIGHT] = true;
                else if(angle >= offset + 3*interval && angle < offset + 4*interval)
                    hw().touch_keystate[0][KEY_RIGHT] = true;
                else if(angle >= offset + 4*interval && angle < offset + 5*interval)
                    hw().touch_keystate[0][KEY_DOWN_RIGHT] = true;
                else if(angle >= offset + 5*interval && angle < offset + 6*interval)
                    hw().touch_keystate[0][KEY_DOWN] = true;
                else if(angle >= offset + 6*interval && angle < offset + 7*interval)
                    hw().touch_keystate[0][KEY_DOWN_LEFT] = true;
            }
        }
        }
        break;
    case og::input_native::EventType::FingerUp:
        {
            int x = (event.finger_x * og::runtime::current_session->window_w_ - og::runtime::current_session->viewport_offset_x_) * (320 / og::runtime::current_session->viewport_w_);
            int y = (event.finger_y * og::runtime::current_session->window_h_ - og::runtime::current_session->viewport_offset_y_) * (200 / og::runtime::current_session->viewport_h_);
            if(hw().tapping)
            {
                hw().tapping = false;
                if(abs(x - hw().start_tap_x) < 2 && abs(y - hw().start_tap_y) < 2)
                    og::runtime::current_session->input_continue_ = true;
                else
                    og::runtime::current_session->input_continue_ = false;
                hw().start_tap_x = x;
                hw().start_tap_y = y;
            }
            
            if(hw().moving && event.finger_id == hw().movingTouch)
            {
                hw().moving = false;
                
                hw().touch_keystate[0][KEY_UP] = false;
                hw().touch_keystate[0][KEY_UP_RIGHT] = false;
                hw().touch_keystate[0][KEY_RIGHT] = false;
                hw().touch_keystate[0][KEY_DOWN_RIGHT] = false;
                hw().touch_keystate[0][KEY_DOWN] = false;
                hw().touch_keystate[0][KEY_DOWN_LEFT] = false;
                hw().touch_keystate[0][KEY_LEFT] = false;
                hw().touch_keystate[0][KEY_UP_LEFT] = false;
            }
            if(hw().firing && event.finger_id == hw().firingTouch)
            {
                hw().firing = false;
                hw().touch_keystate[0][KEY_FIRE] = false;
            }
            
            mouse_state.left = 0;
        }
        break;
    case og::input_native::EventType::FingerDown:
        {
            hw().tapping = true;
            
            int x = (event.finger_x * og::runtime::current_session->window_w_ - og::runtime::current_session->viewport_offset_x_) * (320 / og::runtime::current_session->viewport_w_);
            int y = (event.finger_y * og::runtime::current_session->window_h_ - og::runtime::current_session->viewport_offset_y_) * (200 / og::runtime::current_session->viewport_h_);
            
            hw().start_tap_x = x;
            hw().start_tap_y = y;
            og::runtime::current_session->input_continue_ = false;
            
            if(!hw().firing && FIRE_BUTTON_X <= x && x <= FIRE_BUTTON_X + BUTTON_DIM
                && FIRE_BUTTON_Y <= y && y <= FIRE_BUTTON_Y + BUTTON_DIM)
            {
                hw().firing = true;
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_FIRE]);
                hw().touch_keystate[0][KEY_FIRE] = true;
                hw().firingTouch = event.finger_id;
            }
            else if(SPECIAL_BUTTON_X <= x && x <= SPECIAL_BUTTON_X + BUTTON_DIM
                && SPECIAL_BUTTON_Y <= y && y <= SPECIAL_BUTTON_Y + BUTTON_DIM)
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SPECIAL]);
            }
            else if(YO_BUTTON_X - BUTTON_DIM/2 <= x && x <= YO_BUTTON_X + BUTTON_DIM/2
                && YO_BUTTON_Y - BUTTON_DIM/2 <= y && y <= YO_BUTTON_Y + BUTTON_DIM/2)
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_YELL]);
            }
            else if(SWITCH_CHARACTER_BUTTON_X <= x && x <= SWITCH_CHARACTER_BUTTON_X + BUTTON_DIM*2
                && SWITCH_CHARACTER_BUTTON_Y <= y && y <= SWITCH_CHARACTER_BUTTON_Y + BUTTON_DIM*2)
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SWITCH]);
            }
            else if(NEXT_SPECIAL_BUTTON_X <= x && x <= NEXT_SPECIAL_BUTTON_X + BUTTON_DIM
                && NEXT_SPECIAL_BUTTON_Y <= y && y <= NEXT_SPECIAL_BUTTON_Y + BUTTON_DIM)
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SPECIAL_SWITCH]);
            }
            else if(ALTERNATE_SPECIAL_BUTTON_X <= x && x <= ALTERNATE_SPECIAL_BUTTON_X + BUTTON_DIM
                && ALTERNATE_SPECIAL_BUTTON_Y <= y && y <= ALTERNATE_SPECIAL_BUTTON_Y + BUTTON_DIM)
            {
                // Treat KEY_SHIFTER as an action instead of a modifier
                if(input_touch_has_alternate())
                    sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SHIFTER]);
            }
            else if(!hw().moving && x < 320/2 - BUTTON_DIM/2 && y > BUTTON_DIM*2)  // Only move with the lower left corner of the screen (and offset for other buttons)
            {
                hw().moving_touch_x = x;
                hw().moving_touch_y = y;
                hw().moving_touch_target_x = x;
                hw().moving_touch_target_y = y;
                if(hw().moving_touch_x < MOVE_AREA_DIM/2 + 1)
                    hw().moving_touch_x = MOVE_AREA_DIM/2 + 1;
                if(hw().moving_touch_y < MOVE_AREA_DIM/2 + 1)
                    hw().moving_touch_y = MOVE_AREA_DIM/2 + 1;
                else if(hw().moving_touch_y > 200 - (MOVE_AREA_DIM/2 + 1))
                    hw().moving_touch_y = 200 - (MOVE_AREA_DIM/2 + 1);
                hw().moving = true;
                hw().movingTouch = event.finger_id;
            }
            
            
            og::runtime::current_session->key_press_event_ = 1;
            mouse_state.left = 1;
            mouse_state.x = event.finger_x * 320;
            mouse_state.y = event.finger_y * 200;
        }
        break;
#endif
    default:
        break;
    }
}

void handle_joy_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    Log("Joystick event!\n");
    switch(event.type)
    {
    case og::input_native::EventType::JoyAxisMotion:
        if (event.joy_axis_value > 8000)
        {
            //key_list[joy_startval[event.jaxis.which] + event.jaxis.axis * 2] = 1;
            //key_list[joy_startval[event.jaxis.which] + event.jaxis.axis * 2 + 1] = 0;
            og::runtime::current_session->key_press_event_ = 1;
            //raw_key = joy_startval[event.jaxis.which] + event.jaxis.axis * 2;
        }
        else if (event.joy_axis_value < -8000)
        {
            //key_list[joy_startval[event.jaxis.which] + event.jaxis.axis * 2] = 0;
            //key_list[joy_startval[event.jaxis.which] + event.jaxis.axis * 2 + 1] = 1;
            og::runtime::current_session->key_press_event_ = 1;
            //raw_key = joy_startval[event.jaxis.which] + event.jaxis.axis * 2 + 1;
        }
        else
        {
            //key_list[joy_startval[event.jaxis.which] + event.jaxis.axis * 2] = 0;
            //key_list[joy_startval[event.jaxis.which] + event.jaxis.axis * 2 + 1] = 0;
        }
        break;
    case og::input_native::EventType::JoyButtonDown:
        //key_list[joy_startval[event.jbutton.which] + joy_numaxes[event.jbutton.which] * 2 + event.jbutton.button] = 1;
        //raw_key = joy_startval[event.jbutton.which] + joy_numaxes[event.jbutton.which] * 2 + event.jbutton.button;
        og::runtime::current_session->key_press_event_ = 1;
        break;
    case og::input_native::EventType::JoyButtonUp:
        //key_list[joy_startval[event.jbutton.which] + joy_numaxes[event.jbutton.which] * 2 + event.jbutton.button] = 0;
        break;
    default:
        break;
    }
}

void handle_events(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    switch (event.type)
    {
    case og::input_native::EventType::Window:
        handle_window_event(native_event);
    break;
    case og::input_native::EventType::TextInput:
        handle_text_event(native_event);
        break;
    case og::input_native::EventType::MouseWheel:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::FingerMotion:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::FingerUp:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::FingerDown:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::KeyDown:
        handle_key_event(native_event);
        break;
    case og::input_native::EventType::KeyUp:
        handle_key_event(native_event);
        break;
    case og::input_native::EventType::MouseMotion:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::MouseButtonUp:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::MouseButtonDown:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::JoyAxisMotion:
        handle_joy_event(native_event);
        break;
    case og::input_native::EventType::JoyButtonDown:
        handle_joy_event(native_event);
        break;
    case og::input_native::EventType::JoyButtonUp:
        handle_joy_event(native_event);
        break;
    case og::input_native::EventType::Quit:
        quit(0);
        break;
    default:
#ifdef OUYA
        if(event.raw_type == OuyaControllerManager::BUTTON_DOWN_EVENT)
        {
            if(static_cast<OuyaController::ButtonEnum>(event.user_data1) == OuyaController::ButtonEnum::O)
                og::runtime::current_session->input_continue_ = true;
            else if(static_cast<OuyaController::ButtonEnum>(event.user_data1) == OuyaController::ButtonEnum::DpadUp)
                og::runtime::current_session->scroll_amount_ = 5;
            else if(static_cast<OuyaController::ButtonEnum>(event.user_data1) == OuyaController::ButtonEnum::DpadDown)
                og::runtime::current_session->scroll_amount_ = -5;
            else if(static_cast<OuyaController::ButtonEnum>(event.user_data1) == OuyaController::ButtonEnum::Menu)
                sendFakeKeyDownEvent(KEYCODE_ESCAPE);
            og::runtime::current_session->key_press_event_ = 1;
        }
        else if(event.raw_type == OuyaControllerManager::AXIS_EVENT)
        {
            const OuyaController& c = OuyaControllerManager::getController(event.user_code);
            
            // This should not be in an event or else it's jerky.
            float v = c.getAxisValue(OuyaController::AxisEnum::LsY) + c.getAxisValue(OuyaController::AxisEnum::RsY);
            if(fabs(v) > OuyaController::DEADZONE)
                og::runtime::current_session->scroll_amount_ = -5*v;
        }
    #endif
        break;
    }
}


//
//Keyboard routines
//

bool query_key_event(int key, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;
    return (event.type == og::input_native::EventType::KeyDown && event.key_sym == key);
}

bool isKeyboardEvent(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;
    return (event.type == og::input_native::EventType::KeyDown);
}

bool isJoystickEvent(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;
    return (event.type == og::input_native::EventType::JoyAxisMotion
            || event.type == og::input_native::EventType::JoyHatMotion
            || event.type == og::input_native::EventType::JoyButtonDown);
}

const void* wait_for_key_event()
{
#ifdef TESTING
    // In test mode, return immediately with a fake ESC keypress
    TRACE("input", "wait_for_key_event: returning fake ESC (test mode)");
    return og::input_native::make_test_keydown_event(KEYCODE_ESCAPE, KEYSTATE_ESCAPE);
#else
    while(1)
    {
        while(const void* event = og::input_native::poll_event())
        {
            og::input_native::EventData event_data;
            if (!as_event_data(event, event_data))
                continue;
            if(event_data.type == og::input_native::EventType::Quit
                    || event_data.type == og::input_native::EventType::KeyDown
                    || (event_data.type == og::input_native::EventType::JoyAxisMotion
                        && (event_data.joy_axis_value > JOY_DEAD_ZONE || event_data.joy_axis_value < -JOY_DEAD_ZONE))
                    || event_data.type == og::input_native::EventType::JoyButtonDown
                    || event_data.type == og::input_native::EventType::JoyHatMotion)
                return event;
        }
        YIELD_SLEEP(10);
    }
    return nullptr;
#endif
}

void quit_if_quit_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    if(event.type == og::input_native::EventType::Quit)
        quit(0);
}

void clear_events()
{
    while (og::input_native::poll_event() != nullptr) {}
}

void assignKeyFromWaitEvent(int player_num, int key_enum)
{
    const void* event = wait_for_key_event();
    og::input_native::EventData event_data;
    if (!as_event_data(event, event_data))
        return;
    quit_if_quit_event(event);
    if(isKeyboardEvent(event))
    {
        if(event_data.key_sym != KEYCODE_ESCAPE)
        {
            set_player_key_binding(player_num, key_enum, event_data.key_sym);
        }
    }
    else if(isJoystickEvent(event))
        player_joy[player_num].setKeyFromEvent(key_enum, event);

#ifndef TESTING
    YIELD_SLEEP(400);
#endif
    clear_events();
}


//
// Set the keyboard array to all zeros, the
// virgin state, nothing depressed
//
void clear_keyboard()
{
    og::runtime::current_session->key_press_event_ = 0;
    og::runtime::current_session->raw_key_ = 0;

    og::runtime::current_session->text_input_event_ = 0;
    og::runtime::current_session->raw_text_input_.clear();
    
    og::runtime::current_session->input_continue_ = false;
    
    #ifdef USE_TOUCH_INPUT
    hw().tapping = false;
    #endif
}

void wait_for_key(int somekey)
{
#ifdef TESTING
    (void)somekey;
    TRACE("input", "wait_for_key: skipping wait (test mode)");
    return;
#else
    // First wait for key press ..
    while (!og::runtime::current_session->keystates_[og::input_native::scancode_from_key(somekey)])
        get_input_events(WAIT);

    // And now for the key to be released ..
    while (!og::runtime::current_session->keystates_[og::input_native::scancode_from_key(somekey)])
        get_input_events(WAIT);
#endif
}

// JoyData::JoyData() (default ctor) is defined in the SDL-free input_state.cpp so
// InputHardwareState (which value-initializes a JoyData[4]) can be constructed by
// the headless builds (curses/server/text) that do not compile this file.

JoyData::JoyData(int joy_index)
    : index(-1), numAxes(0), numButtons(0), numHats(0)
{
    if(joy_index < 0 || joy_index >= MAX_NUM_JOYSTICKS)
        return;
    const og::input_native::JoystickHandle js = joysticks[joy_index];
    if(js == nullptr)
        return;

    this->index = joy_index;
    numAxes = og::input_native::joystick_num_axes(js);
    numButtons = og::input_native::joystick_num_buttons(js);
    numHats = og::input_native::joystick_num_hats(js);

    // Clear all keys for this joystick
    for(int i = 0; i < NUM_KEYS; i++)
    {
        key_type[i] = NONE;
        key_index[i] = 0;
    }

    // Default movement
    if(numAxes > 1) // Prefer two axes
    {
        key_type[KEY_RIGHT] = POS_AXIS;
        key_index[KEY_RIGHT] = 0;
        key_type[KEY_LEFT] = NEG_AXIS;
        key_index[KEY_LEFT] = 0;

        key_type[KEY_UP] = NEG_AXIS;
        key_index[KEY_UP] = 1;
        key_type[KEY_DOWN] = POS_AXIS;
        key_index[KEY_DOWN] = 1;
    }
    else if(numHats > 0) // But a single hat is okay otherwise
    {
        // indices default to hat 0
        key_type[KEY_UP] = HAT_UP;
        key_type[KEY_UP_RIGHT] = HAT_UP_RIGHT;
        key_type[KEY_RIGHT] = HAT_RIGHT;
        key_type[KEY_DOWN_RIGHT] = HAT_DOWN_RIGHT;
        key_type[KEY_DOWN] = HAT_DOWN;
        key_type[KEY_DOWN_LEFT] = HAT_DOWN_LEFT;
        key_type[KEY_LEFT] = HAT_LEFT;
        key_type[KEY_UP_LEFT] = HAT_UP_LEFT;
    }


    // Default actions
    if(numButtons > 0)
    {
        key_type[KEY_FIRE] = BUTTON;
        key_index[KEY_FIRE] = 0;
    }
    if(numButtons > 1)
    {
        key_type[KEY_SPECIAL] = BUTTON;
        key_index[KEY_SPECIAL] = 1;
    }
    if(numButtons > 2)
    {
        key_type[KEY_SPECIAL_SWITCH] = BUTTON;
        key_index[KEY_SPECIAL_SWITCH] = 2;
    }
    if(numButtons > 3)
    {
        key_type[KEY_YELL] = BUTTON;
        key_index[KEY_YELL] = 3;
    }
    if(numButtons > 4)
    {
        key_type[KEY_SHIFTER] = BUTTON;
        key_index[KEY_SHIFTER] = 4;
    }
    if(numButtons > 5)
    {
        key_type[KEY_SWITCH] = BUTTON;
        key_index[KEY_SWITCH] = 5;
    }
}


void JoyData::setKeyFromEvent(int key_enum, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    if (key_enum < 0 || key_enum >= NUM_KEYS) return;  // key_type/key_index are sized NUM_KEYS

    // Diagonals are ignored because they are combinations of the cardinals
    // Things get really messy when diagonals are assigned
    if(key_enum == KEY_UP_RIGHT || key_enum == KEY_UP_LEFT || key_enum == KEY_DOWN_RIGHT || key_enum == KEY_DOWN_LEFT)
    {
        key_type[key_enum] = NONE;
        key_index[key_enum] = 0;
        return;
    }

    bool gotJoy = false;
    if(event.type == og::input_native::EventType::JoyAxisMotion)
    {
        if(event.joy_axis_value >= 0)
            key_type[key_enum] = POS_AXIS;
        else
            key_type[key_enum] = NEG_AXIS;
        key_index[key_enum] = event.joy_axis_axis;
        index = event.joy_axis_which;  // USES THE LAST JOYSTICK PRESSED
        gotJoy = true;
    }
    else if(event.type == og::input_native::EventType::JoyButtonDown)
    {
        key_type[key_enum] = BUTTON;
        key_index[key_enum] = event.joy_button_button;
        index = event.joy_button_which;  // USES THE LAST JOYSTICK PRESSED
        gotJoy = true;
    }
    else if(event.type == og::input_native::EventType::JoyHatMotion)
    {
        bool badHat = false;
        if(event.joy_hat_value == og::input_native::kHatUp)
            key_type[key_enum] = HAT_UP;
        else if(event.joy_hat_value == og::input_native::kHatRight)
            key_type[key_enum] = HAT_RIGHT;
        else if(event.joy_hat_value == og::input_native::kHatDown)
            key_type[key_enum] = HAT_DOWN;
        else if(event.joy_hat_value == og::input_native::kHatLeft)
            key_type[key_enum] = HAT_LEFT;
        else
        {
            badHat = true;
            // Diagonal hat values are ignored because they are combinations
            // of the cardinals and complicate key assignment.
        }
        if(!badHat)
        {
            key_index[key_enum] = event.joy_hat_hat;
            index = event.joy_hat_which;  // USES THE LAST JOYSTICK PRESSED
            gotJoy = true;
        }
    }

    if(gotJoy)
    {
        // Take over this joystick
        for(int i = 0; i < 4; i++)
        {
            if(this != &player_joy[i] && player_joy[i].index == index)
                player_joy[i].index = -1;
        }
    }
}

bool JoyData::getState(int key_enum) const
{
    if(index < 0 || index >= MAX_NUM_JOYSTICKS)
        return false;
    switch(key_type[key_enum])
    {
    case POS_AXIS:
        return og::input_native::joystick_get_axis(joysticks[index], key_index[key_enum]) > JOY_DEAD_ZONE;
    case NEG_AXIS:
        return og::input_native::joystick_get_axis(joysticks[index], key_index[key_enum]) < -JOY_DEAD_ZONE;
    case BUTTON:
        return og::input_native::joystick_get_button(joysticks[index], key_index[key_enum]);
    case HAT_UP:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatUp);
    case HAT_RIGHT:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatRight);
    case HAT_DOWN:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatDown);
    case HAT_LEFT:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatLeft);
        // Diagonals are ignored because they are combinations of the cardinals
    case HAT_UP_RIGHT:
    case HAT_DOWN_RIGHT:
    case HAT_DOWN_LEFT:
    case HAT_UP_LEFT:
    default:
        return false;
    }
}

bool JoyData::getPress(int key_enum, const void* native_event) const
{
    if(index < 0 || index >= MAX_NUM_JOYSTICKS)
        return false;
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    switch(key_type[key_enum])
    {
    case BUTTON:
        if(event.type == og::input_native::EventType::JoyButtonDown)
        {
            return (event.joy_button_which == index && event.joy_button_button == key_index[key_enum]);
        }
        return false;
    case POS_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value > JOY_DEAD_ZONE);
        }
        return false;
    case NEG_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value < -JOY_DEAD_ZONE);
        }
        return false;
    case HAT_UP:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatUp));
    case HAT_RIGHT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatRight));
    case HAT_DOWN:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatDown));
    case HAT_LEFT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatLeft));

        // Diagonals are ignored because they are combinations of the cardinals
    case HAT_UP_RIGHT:
    case HAT_DOWN_RIGHT:
    case HAT_DOWN_LEFT:
    case HAT_UP_LEFT:
    default:
        return false;
    }
}

bool JoyData::getRelease(int key_enum, const void* native_event) const
{
    if(index < 0 || index >= MAX_NUM_JOYSTICKS)
        return false;
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    switch(key_type[key_enum])
    {
    case BUTTON:
        if(event.type == og::input_native::EventType::JoyButtonUp)
        {
            return (event.joy_button_which == index && event.joy_button_button == key_index[key_enum]);
        }
        return false;
    case POS_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value < JOY_DEAD_ZONE);
        }
        return false;
    case NEG_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value > -JOY_DEAD_ZONE);
        }
        return false;
    case HAT_UP:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatUp));
    case HAT_RIGHT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatRight));
    case HAT_DOWN:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatDown));
    case HAT_LEFT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatLeft));

        // Diagonals are ignored because they are combinations of the cardinals
    case HAT_UP_RIGHT:
    case HAT_DOWN_RIGHT:
    case HAT_DOWN_LEFT:
    case HAT_UP_LEFT:
    default:
        return false;
    }
}

bool JoyData::hasButtonSet(int key_enum) const
{
    return (index >= 0 && key_type[key_enum] != NONE);
}

void resetJoystick(int player_num)
{
    // FIXME: SDL2 supports hotplugging, so I don't need to restart the joystick subsystem
    // Reset joystick subsystem
    if(og::input_native::joystick_subsystem_initialized())
        og::input_native::joystick_quit_subsystem();
    og::input_native::joystick_init_subsystem();

    // Set up joysticks
    for(int i = 0; i < MAX_NUM_JOYSTICKS; i++)
    {
        joysticks[i] = nullptr;
    }

    int numjoy = std::min(og::input_native::num_joysticks(), MAX_NUM_JOYSTICKS);
    for(int i = 0; i < numjoy; i++)
    {
        joysticks[i] = og::input_native::joystick_open(i);
        if(joysticks[i] == nullptr)
            continue;
        // The joystick indices might change here.
        // FIXME: There's a chance that players will not have the joysticks they expect and
        // so they might have buttons, etc. that are out of range for the new joystick.
    }

    og::input_native::joystick_set_event_state(true);

    player_joy[player_num] = JoyData(player_num);
}

#ifdef OUYA
bool ouyaJoystickInDirection(int player, int key_enum)
{
    const OuyaController& c = OuyaControllerManager::getController(player);
    if(!c.isStickBeyondDeadzone(OuyaController::AxisEnum::LsX))
        return false;
    
    switch(key_enum)
    {
    case KEY_UP:
        return (c.getAxisValue(OuyaController::AxisEnum::LsY) < -OuyaController::DEADZONE && !c.isStickInNegativeCone(OuyaController::AxisEnum::LsX) && !c.isStickInPositiveCone(OuyaController::AxisEnum::LsX));
    case KEY_UP_RIGHT:
        return (c.getAxisValue(OuyaController::AxisEnum::LsY) < -OuyaController::DEADZONE && c.getAxisValue(OuyaController::AxisEnum::LsX) > OuyaController::DEADZONE);
    case KEY_RIGHT:
        return (c.getAxisValue(OuyaController::AxisEnum::LsX) > OuyaController::DEADZONE && !c.isStickInNegativeCone(OuyaController::AxisEnum::LsY) && !c.isStickInPositiveCone(OuyaController::AxisEnum::LsY));
    case KEY_DOWN_RIGHT:
        return (c.getAxisValue(OuyaController::AxisEnum::LsY) > OuyaController::DEADZONE && c.getAxisValue(OuyaController::AxisEnum::LsX) > OuyaController::DEADZONE);
    case KEY_DOWN:
        return (c.getAxisValue(OuyaController::AxisEnum::LsY) > OuyaController::DEADZONE && !c.isStickInNegativeCone(OuyaController::AxisEnum::LsX) && !c.isStickInPositiveCone(OuyaController::AxisEnum::LsX));
    case KEY_DOWN_LEFT:
        return (c.getAxisValue(OuyaController::AxisEnum::LsY) > OuyaController::DEADZONE && c.getAxisValue(OuyaController::AxisEnum::LsX) < -OuyaController::DEADZONE);
    case KEY_LEFT:
        return (c.getAxisValue(OuyaController::AxisEnum::LsX) < -OuyaController::DEADZONE && !c.isStickInNegativeCone(OuyaController::AxisEnum::LsY) && !c.isStickInPositiveCone(OuyaController::AxisEnum::LsY));
    case KEY_UP_LEFT:
        return (c.getAxisValue(OuyaController::AxisEnum::LsY) < -OuyaController::DEADZONE && c.getAxisValue(OuyaController::AxisEnum::LsX) < -OuyaController::DEADZONE);
    }
    return false;
}
#endif

bool isPlayerHoldingKey(int player_index, int key_enum)
{
    #ifdef OUYA
    const OuyaController& c = OuyaControllerManager::getController(player_index);
    switch(key_enum)
    {
    case KEY_UP:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadUp) && !(c.getButtonValue(OuyaController::ButtonEnum::DpadLeft) || c.getButtonValue(OuyaController::ButtonEnum::DpadRight)))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_UP_RIGHT:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadUp) && c.getButtonValue(OuyaController::ButtonEnum::DpadRight))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_RIGHT:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadRight) && !(c.getButtonValue(OuyaController::ButtonEnum::DpadUp) || c.getButtonValue(OuyaController::ButtonEnum::DpadDown)))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_DOWN_RIGHT:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadDown) && c.getButtonValue(OuyaController::ButtonEnum::DpadRight))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_DOWN:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadDown) && !(c.getButtonValue(OuyaController::ButtonEnum::DpadLeft) || c.getButtonValue(OuyaController::ButtonEnum::DpadRight)))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_DOWN_LEFT:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadDown) && c.getButtonValue(OuyaController::ButtonEnum::DpadLeft))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_LEFT:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadLeft) && !(c.getButtonValue(OuyaController::ButtonEnum::DpadUp) || c.getButtonValue(OuyaController::ButtonEnum::DpadDown)))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_UP_LEFT:
        return (c.getButtonValue(OuyaController::ButtonEnum::DpadUp) && c.getButtonValue(OuyaController::ButtonEnum::DpadLeft))
               || ouyaJoystickInDirection(player_index, key_enum);
    case KEY_FIRE:
        return c.getButtonValue(OuyaController::ButtonEnum::O);
    case KEY_SPECIAL:
        return c.getButtonValue(OuyaController::ButtonEnum::A);
    case KEY_SWITCH:
        return c.getButtonValue(OuyaController::ButtonEnum::L1);
    case KEY_SPECIAL_SWITCH:
        return c.getButtonValue(OuyaController::ButtonEnum::U);
    case KEY_YELL:
        return c.getButtonValue(OuyaController::ButtonEnum::Y);
    case KEY_SHIFTER:
        return c.getButtonValue(OuyaController::ButtonEnum::R1);
    case KEY_PREFS:
        return false;
    case KEY_CHEAT:
        return false;
    }
    #endif
    
    // FIXME: Enable gamepads for Android/iOS, but be careful not to use accelerometer...
    #ifdef USE_TOUCH_INPUT
        if (player_index < 0 || player_index >= 4 || key_enum < 0 || key_enum >= NUM_KEYS)
            return false;
        return hw().touch_keystate[player_index][key_enum];
    #else
    if(player_joy[player_index].hasButtonSet(key_enum))
        return player_joy[player_index].getState(key_enum);
    else
        return og::runtime::current_session->keystates_[og::input_native::scancode_from_key(og::runtime::current_session->player_keys_[player_index][key_enum])];
    #endif
}

bool didPlayerPressKey(int player_index, int key_enum, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    #ifdef OUYA
    const OuyaController& c = OuyaControllerManager::getController(player_index);
    if(event.user_code != player_index)
        return false;
    
    if(event.raw_type == OuyaControllerManager::BUTTON_DOWN_EVENT)
    {
        OuyaController::ButtonEnum button = static_cast<OuyaController::ButtonEnum>(event.user_data1);
        
        switch(key_enum)
        {
        case KEY_UP:
            return (button == OuyaController::ButtonEnum::DpadUp);
        case KEY_RIGHT:
            return (button == OuyaController::ButtonEnum::DpadRight);
        case KEY_DOWN:
            return (button == OuyaController::ButtonEnum::DpadDown);
        case KEY_LEFT:
            return (button == OuyaController::ButtonEnum::DpadLeft);
        case KEY_FIRE:
            return (button == OuyaController::ButtonEnum::O);
        case KEY_SPECIAL:
            return (button == OuyaController::ButtonEnum::A);
        case KEY_SWITCH:
            return (button == OuyaController::ButtonEnum::L1);
        case KEY_SPECIAL_SWITCH:
            return (button == OuyaController::ButtonEnum::U);
        case KEY_YELL:
            return (button == OuyaController::ButtonEnum::Y);
        case KEY_SHIFTER:
            return (button == OuyaController::ButtonEnum::R1);
        default:
            return false;
        }
    }
    else if(event.raw_type == OuyaControllerManager::AXIS_EVENT)
    {
        switch(key_enum)
        {
        case KEY_UP:
        case KEY_RIGHT:
        case KEY_DOWN:
        case KEY_LEFT:
            return ouyaJoystickInDirection(player_index, key_enum);
        default:
            return false;
        }
    }
    #endif
    if(player_joy[player_index].hasButtonSet(key_enum))
    {
        // This key is on the joystick, so check it.
        return player_joy[player_index].getPress(key_enum, native_event);
    }
    else
    {
        // If the player is using KEYBOARD or doesn't have a joystick button set for this key, then check the keyboard.
        if(event.type == og::input_native::EventType::KeyDown)
        {
            if(event.key_repeat) // Repeats don't count!
                return false;
            return (event.key_sym == og::runtime::current_session->player_keys_[player_index][key_enum]);
        }
        return false;
    }
}

bool didPlayerReleaseKey(int player_index, int key_enum, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    #ifdef OUYA
    const OuyaController& c = OuyaControllerManager::getController(player_index);
    if(event.raw_type != OuyaControllerManager::BUTTON_UP_EVENT)
        return false;
    if(event.user_code != player_index)
        return false;
    
    OuyaController::ButtonEnum button = static_cast<OuyaController::ButtonEnum>(event.user_data1);
    
    switch(key_enum)
    {
    case KEY_UP:
        return (button == OuyaController::ButtonEnum::DpadUp);
    case KEY_RIGHT:
        return (button == OuyaController::ButtonEnum::DpadRight);
    case KEY_DOWN:
        return (button == OuyaController::ButtonEnum::DpadDown);
    case KEY_LEFT:
        return (button == OuyaController::ButtonEnum::DpadLeft);
    case KEY_FIRE:
        return (button == OuyaController::ButtonEnum::O);
    case KEY_SPECIAL:
        return (button == OuyaController::ButtonEnum::A);
    case KEY_SWITCH:
        return (button == OuyaController::ButtonEnum::L1);
    case KEY_SPECIAL_SWITCH:
        return (button == OuyaController::ButtonEnum::U);
    case KEY_YELL:
        return (button == OuyaController::ButtonEnum::Y);
    case KEY_SHIFTER:
        return (button == OuyaController::ButtonEnum::R1);
    default:
        return false;
    }
    #endif
    if(player_joy[player_index].hasButtonSet(key_enum))
    {
        // This key is on the joystick, so check it.
        return player_joy[player_index].getRelease(key_enum, native_event);
    }
    else
    {
        // If the player is using KEYBOARD or doesn't have a joystick button set for this key, then check the keyboard.
        if(event.type == og::input_native::EventType::KeyUp)
        {
            return (event.key_sym == og::runtime::current_session->player_keys_[player_index][key_enum]);
        }
        return false;
    }
}

//
// Mouse routines
//

void grab_mouse()
{
    og::input_native::show_cursor(true);
}

void release_mouse()
{
    #ifndef FAKE_TOUCH_EVENTS
    og::input_native::show_cursor(false);
    #endif
}

MouseState& query_mouse()
{
    // The mouse_state thing is set using get_input_events, though
    // it should probably get its own function
    get_input_events(POLL);
    return mouse_state;
}

// Convert from scancode to ascii, ie, KEYCODE_a to 'A'
unsigned char convert_to_ascii(int scancode)
{
    switch (scancode)
    {
    case KEYCODE_a:
        return 'A';
    case KEYCODE_b:
        return 'B';
    case KEYCODE_c:
        return 'C';
    case KEYCODE_d:
        return 'D';
    case KEYCODE_e:
        return 'E';
    case KEYCODE_f:
        return 'F';
    case KEYCODE_g:
        return 'G';
    case KEYCODE_h:
        return 'H';
    case KEYCODE_i:
        return 'I';
    case KEYCODE_j:
        return 'J';
    case KEYCODE_k:
        return 'K';
    case KEYCODE_l:
        return 'L';
    case KEYCODE_m:
        return 'M';
    case KEYCODE_n:
        return 'N';
    case KEYCODE_o:
        return 'O';
    case KEYCODE_p:
        return 'P';
    case KEYCODE_q:
        return 'Q';
    case KEYCODE_r:
        return 'R';
    case KEYCODE_s:
        return 'S';
    case KEYCODE_t:
        return 'T';
    case KEYCODE_u:
        return 'U';
    case KEYCODE_v:
        return 'V';
    case KEYCODE_w:
        return 'W';
    case KEYCODE_x:
        return 'X';
    case KEYCODE_y:
        return 'Y';
    case KEYCODE_z:
        return 'Z';

    case KEYCODE_1:
        return '1';
    case KEYCODE_2:
        return '2';
    case KEYCODE_3:
        return '3';
    case KEYCODE_4:
        return '4';
    case KEYCODE_5:
        return '5';
    case KEYCODE_6:
        return '6';
    case KEYCODE_7:
        return '7';
    case KEYCODE_8:
        return '8';
    case KEYCODE_9:
        return '9';
    case KEYCODE_0:
        return '0';

    case KEYCODE_SPACE:
        return 32;
        //    case KEYCODE_BACKSPACE: return 8;
    case KEYCODE_RETURN:
        return 13;
    case KEYCODE_ESCAPE:
        return 27;
    case KEYCODE_PERIOD:
        return '.';
    case KEYCODE_COMMA:
        return ',';
    case KEYCODE_QUOTE:
        return '\'';
    case KEYCODE_BACKQUOTE:
        return '`';

    default:
        return 255;
    }
}

const char* query_key_name(int keycode)
{
    return og::input_native::key_name(keycode);
}
