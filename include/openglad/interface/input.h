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
// input.h
//
// input code
//

#pragma once

#include <openglad/interface/base.h>
#include <cctype>
#include <string>


// SDL2 scancode values copied into interface constants so this header stays
// platform-agnostic. Values are stable in SDL2.
inline constexpr int KEYSTATE_UNKNOWN = 0;
inline constexpr int KEYSTATE_a = 4;
inline constexpr int KEYSTATE_b = 5;
inline constexpr int KEYSTATE_c = 6;
inline constexpr int KEYSTATE_d = 7;
inline constexpr int KEYSTATE_e = 8;
inline constexpr int KEYSTATE_f = 9;
inline constexpr int KEYSTATE_g = 10;
inline constexpr int KEYSTATE_h = 11;
inline constexpr int KEYSTATE_i = 12;
inline constexpr int KEYSTATE_j = 13;
inline constexpr int KEYSTATE_k = 14;
inline constexpr int KEYSTATE_l = 15;
inline constexpr int KEYSTATE_m = 16;
inline constexpr int KEYSTATE_n = 17;
inline constexpr int KEYSTATE_o = 18;
inline constexpr int KEYSTATE_p = 19;
inline constexpr int KEYSTATE_q = 20;
inline constexpr int KEYSTATE_r = 21;
inline constexpr int KEYSTATE_s = 22;
inline constexpr int KEYSTATE_t = 23;
inline constexpr int KEYSTATE_u = 24;
inline constexpr int KEYSTATE_v = 25;
inline constexpr int KEYSTATE_w = 26;
inline constexpr int KEYSTATE_x = 27;
inline constexpr int KEYSTATE_y = 28;
inline constexpr int KEYSTATE_z = 29;
inline constexpr int KEYSTATE_1 = 30;
inline constexpr int KEYSTATE_2 = 31;
inline constexpr int KEYSTATE_3 = 32;
inline constexpr int KEYSTATE_4 = 33;
inline constexpr int KEYSTATE_5 = 34;
inline constexpr int KEYSTATE_6 = 35;
inline constexpr int KEYSTATE_7 = 36;
inline constexpr int KEYSTATE_8 = 37;
inline constexpr int KEYSTATE_9 = 38;
inline constexpr int KEYSTATE_0 = 39;
inline constexpr int KEYSTATE_RETURN = 40;
inline constexpr int KEYSTATE_ESCAPE = 41;
inline constexpr int KEYSTATE_SPACE = 44;
inline constexpr int KEYSTATE_LEFTBRACKET = 47;
inline constexpr int KEYSTATE_RIGHTBRACKET = 48;
inline constexpr int KEYSTATE_COMMA = 54;
inline constexpr int KEYSTATE_PERIOD = 55;
inline constexpr int KEYSTATE_SLASH = 56;
inline constexpr int KEYSTATE_F1 = 58;
inline constexpr int KEYSTATE_F5 = 62;
inline constexpr int KEYSTATE_F9 = 66;
inline constexpr int KEYSTATE_F10 = 67;
inline constexpr int KEYSTATE_PAGEUP = 75;
inline constexpr int KEYSTATE_DELETE = 76;
inline constexpr int KEYSTATE_PAGEDOWN = 78;
inline constexpr int KEYSTATE_RIGHT = 79;
inline constexpr int KEYSTATE_LEFT = 80;
inline constexpr int KEYSTATE_DOWN = 81;
inline constexpr int KEYSTATE_UP = 82;
inline constexpr int KEYSTATE_KP_MULTIPLY = 85;
inline constexpr int KEYSTATE_KP_MINUS = 86;
inline constexpr int KEYSTATE_KP_PLUS = 87;
inline constexpr int KEYSTATE_KP_1 = 89;
inline constexpr int KEYSTATE_KP_2 = 90;
inline constexpr int KEYSTATE_KP_3 = 91;
inline constexpr int KEYSTATE_KP_4 = 92;
inline constexpr int KEYSTATE_KP_5 = 93;
inline constexpr int KEYSTATE_KP_6 = 94;
inline constexpr int KEYSTATE_KP_7 = 95;
inline constexpr int KEYSTATE_KP_8 = 96;
inline constexpr int KEYSTATE_KP_9 = 97;
inline constexpr int KEYSTATE_KP_0 = 98;
inline constexpr int KEYSTATE_LCTRL = 224;
inline constexpr int KEYSTATE_LSHIFT = 225;
inline constexpr int KEYSTATE_RCTRL = 228;
inline constexpr int KEYSTATE_RSHIFT = 229;

// SDL2 keycode values copied into interface constants so interface code does
// not include SDL headers.
inline constexpr int KEYCODE_UNKNOWN = 0;
inline constexpr int KEYCODE_RETURN = 13;
inline constexpr int KEYCODE_ESCAPE = 27;
inline constexpr int KEYCODE_BACKSPACE = 8;
inline constexpr int KEYCODE_TAB = 9;
inline constexpr int KEYCODE_SPACE = 32;
inline constexpr int KEYCODE_QUOTE = 39;
inline constexpr int KEYCODE_COMMA = 44;
inline constexpr int KEYCODE_MINUS = 45;
inline constexpr int KEYCODE_PERIOD = 46;
inline constexpr int KEYCODE_SLASH = 47;
inline constexpr int KEYCODE_0 = 48;
inline constexpr int KEYCODE_1 = 49;
inline constexpr int KEYCODE_2 = 50;
inline constexpr int KEYCODE_3 = 51;
inline constexpr int KEYCODE_4 = 52;
inline constexpr int KEYCODE_5 = 53;
inline constexpr int KEYCODE_6 = 54;
inline constexpr int KEYCODE_7 = 55;
inline constexpr int KEYCODE_8 = 56;
inline constexpr int KEYCODE_9 = 57;
inline constexpr int KEYCODE_SEMICOLON = 59;
inline constexpr int KEYCODE_EQUALS = 61;
inline constexpr int KEYCODE_LEFTBRACKET = 91;
inline constexpr int KEYCODE_BACKSLASH = 92;
inline constexpr int KEYCODE_RIGHTBRACKET = 93;
inline constexpr int KEYCODE_BACKQUOTE = 96;
inline constexpr int KEYCODE_a = 97;
inline constexpr int KEYCODE_b = 98;
inline constexpr int KEYCODE_c = 99;
inline constexpr int KEYCODE_d = 100;
inline constexpr int KEYCODE_e = 101;
inline constexpr int KEYCODE_f = 102;
inline constexpr int KEYCODE_g = 103;
inline constexpr int KEYCODE_h = 104;
inline constexpr int KEYCODE_i = 105;
inline constexpr int KEYCODE_j = 106;
inline constexpr int KEYCODE_k = 107;
inline constexpr int KEYCODE_l = 108;
inline constexpr int KEYCODE_m = 109;
inline constexpr int KEYCODE_n = 110;
inline constexpr int KEYCODE_o = 111;
inline constexpr int KEYCODE_p = 112;
inline constexpr int KEYCODE_q = 113;
inline constexpr int KEYCODE_r = 114;
inline constexpr int KEYCODE_s = 115;
inline constexpr int KEYCODE_t = 116;
inline constexpr int KEYCODE_u = 117;
inline constexpr int KEYCODE_v = 118;
inline constexpr int KEYCODE_w = 119;
inline constexpr int KEYCODE_x = 120;
inline constexpr int KEYCODE_y = 121;
inline constexpr int KEYCODE_z = 122;
inline constexpr int KEYCODE_DELETE = 127;
inline constexpr int KEYCODE_HOME = 1073741898;
inline constexpr int KEYCODE_END = 1073741901;
inline constexpr int KEYCODE_UP = 1073741906;
inline constexpr int KEYCODE_DOWN = 1073741905;
inline constexpr int KEYCODE_LEFT = 1073741904;
inline constexpr int KEYCODE_RIGHT = 1073741903;
inline constexpr int KEYCODE_KP_MULTIPLY = 1073741909;
inline constexpr int KEYCODE_KP_MINUS = 1073741910;
inline constexpr int KEYCODE_KP_PLUS = 1073741911;
inline constexpr int KEYCODE_KP_ENTER = 1073741912;
inline constexpr int KEYCODE_KP_1 = 1073741913;
inline constexpr int KEYCODE_KP_2 = 1073741914;
inline constexpr int KEYCODE_KP_3 = 1073741915;
inline constexpr int KEYCODE_KP_4 = 1073741916;
inline constexpr int KEYCODE_KP_5 = 1073741917;
inline constexpr int KEYCODE_KP_6 = 1073741918;
inline constexpr int KEYCODE_KP_7 = 1073741919;
inline constexpr int KEYCODE_KP_8 = 1073741920;
inline constexpr int KEYCODE_KP_9 = 1073741921;
inline constexpr int KEYCODE_KP_0 = 1073741922;
inline constexpr int KEYCODE_KP_PERIOD = 1073741923;
inline constexpr int KEYCODE_F1 = 1073741882;
inline constexpr int KEYCODE_F2 = 1073741883;
inline constexpr int KEYCODE_F3 = 1073741884;
inline constexpr int KEYCODE_F4 = 1073741885;
inline constexpr int KEYCODE_F5 = 1073741886;
inline constexpr int KEYCODE_F6 = 1073741887;
inline constexpr int KEYCODE_F7 = 1073741888;
inline constexpr int KEYCODE_F8 = 1073741889;
inline constexpr int KEYCODE_F9 = 1073741890;
inline constexpr int KEYCODE_F10 = 1073741891;
inline constexpr int KEYCODE_F12 = 1073741893;
inline constexpr int KEYCODE_LCTRL = 1073742048;
inline constexpr int KEYCODE_LSHIFT = 1073742049;
inline constexpr int KEYCODE_LALT = 1073742050;
inline constexpr int KEYCODE_RSHIFT = 1073742053;
inline constexpr int KEYMOD_CTRL = 0x00C0;

template <typename EventT>
inline const void* to_native_event_ptr(const EventT& event)
{
    return static_cast<const void*>(&event);
}


// Event getting method
inline constexpr int POLL = 0;
inline constexpr int WAIT = 1;

// Keyboard defines
inline constexpr int MAXKEYS = 320;

// Mouse defines
inline constexpr int MOUSE_RESET = 0;
inline constexpr int MOUSE_STATE = 3;
inline constexpr int MSTATE = 4;
inline constexpr int MOUSE_X = 0;
inline constexpr int MOUSE_Y = 1;
inline constexpr int MOUSE_LEFT = 2;
inline constexpr int MOUSE_RIGHT = 3;

// High-level key action indices (used as array indices into key maps).
inline constexpr int KEY_UP                  = 0;
inline constexpr int KEY_UP_RIGHT            = 1;
inline constexpr int KEY_RIGHT               = 2;
inline constexpr int KEY_DOWN_RIGHT          = 3;
inline constexpr int KEY_DOWN                = 4;
inline constexpr int KEY_DOWN_LEFT           = 5;
inline constexpr int KEY_LEFT                = 6;
inline constexpr int KEY_UP_LEFT             = 7;
inline constexpr int KEY_FIRE                = 8;
inline constexpr int KEY_SPECIAL             = 9;
inline constexpr int KEY_SWITCH              = 10;
inline constexpr int KEY_SPECIAL_SWITCH      = 11;
inline constexpr int KEY_YELL                = 12;
inline constexpr int KEY_SHIFTER             = 13;
inline constexpr int KEY_PREFS               = 14;
inline constexpr int KEY_CHEAT               = 15;
inline constexpr int NUM_KEYS                = 16;

enum class ControlDirectionMode : int
{
    FourDirection = 4,
    EightDirection = 8
};

class cfg_store;

void reset_default_player_controls();
int get_player_control_mode(int player_index);
void set_player_control_mode(int player_index, int mode);
bool player_allows_diagonal_movement(int player_index);
int get_player_key_binding_for_mode(int player_index, int mode, int key_enum);
void set_player_key_binding(int player_index, int key_enum, int keycode);
void load_player_control_settings_from_cfg(cfg_store& config);
void save_player_control_settings_to_cfg(cfg_store& config);


class JoyData
{
    public:
    int index;
    int numAxes;
    int numButtons;
    int numHats;
    
    static const int NONE = 0;
    static const int BUTTON = 1;
    static const int POS_AXIS = 2;
    static const int NEG_AXIS = 3;
    static const int HAT_UP = 4;
    static const int HAT_UP_RIGHT = 5;
    static const int HAT_RIGHT = 6;
    static const int HAT_DOWN_RIGHT = 7;
    static const int HAT_DOWN = 8;
    static const int HAT_DOWN_LEFT = 9;
    static const int HAT_LEFT = 10;
    static const int HAT_UP_LEFT = 11;
    
    int key_type[NUM_KEYS];
    int key_index[NUM_KEYS];
    
    JoyData();
    JoyData(int index);
    
    void setKeyFromEvent(int key_enum, const void* native_event);
    template <typename EventT>
    void setKeyFromEvent(int key_enum, const EventT& event)
    {
        setKeyFromEvent(key_enum, to_native_event_ptr(event));
    }
    
    bool getState(int key_enum) const;
    bool getPress(int key_enum, const void* native_event) const;
    template <typename EventT>
    bool getPress(int key_enum, const EventT& event) const
    {
        return getPress(key_enum, to_native_event_ptr(event));
    }
    bool getRelease(int key_enum, const void* native_event) const;
    template <typename EventT>
    bool getRelease(int key_enum, const EventT& event) const
    {
        return getRelease(key_enum, to_native_event_ptr(event));
    }
    bool hasButtonSet(int key_enum) const;
};

struct MouseState
{
    float x, y;
    bool left;
    bool right;

    bool in_rect(float rx, float ry, float rw, float rh) const
    {
        return (rx <= x && x < rx + rw && ry <= y && y < ry + rh);
    }

    template <typename RectT>
    bool in(const RectT& r) const
    {
        return in_rect(static_cast<float>(r.x),
                       static_cast<float>(r.y),
                       static_cast<float>(r.w),
                       static_cast<float>(r.h));
    }
};

// Input hardware state now lives in GameSession::input_hw_.
// Macros preserve existing access patterns (mouse_state.x, player_joy[i], etc.).
#include <openglad/interface/input_hardware_state.h>
InputHardwareState& input_hardware_state();
int (&input_player_keys())[4][NUM_KEYS];
int& input_raw_key_ref();
std::string& input_raw_text_input_ref();
bool& input_continue_ref();
short& input_scroll_amount_ref();
short& input_key_press_event_ref();
short& input_text_input_event_ref();

#define mouse_state (input_hardware_state().mouse)
#define player_joy (input_hardware_state().player_joy)

// Inline trivial accessors for joystick/key/input state
inline bool playerHasJoystick(int player_num) { return (player_joy[player_num].index >= 0); }
inline void disablePlayerJoystick(int player_num) { player_joy[player_num].index = -1; }

void resetJoystick(int player_num);
bool isPlayerHoldingKey(int player_index, int key_enum);
bool didPlayerPressKey(int player_index, int key_enum, const void* native_event);
template <typename EventT>
inline bool didPlayerPressKey(int player_index, int key_enum, const EventT& event)
{
    return didPlayerPressKey(player_index, key_enum, to_native_event_ptr(event));
}
bool didPlayerReleaseKey(int player_index, int key_enum, const void* native_event);
template <typename EventT>
inline bool didPlayerReleaseKey(int player_index, int key_enum, const EventT& event)
{
    return didPlayerReleaseKey(player_index, key_enum, to_native_event_ptr(event));
}

//buffers: added prototype
void get_input_events(bool type);
void handle_events(const void* native_event);
template <typename EventT>
inline void handle_events(const EventT& event)
{
    handle_events(to_native_event_ptr(event));
}

// Specific event handling
void handle_window_event(const void* native_event);
template <typename EventT>
inline void handle_window_event(const EventT& event)
{
    handle_window_event(to_native_event_ptr(event));
}
void handle_key_event(const void* native_event);
template <typename EventT>
inline void handle_key_event(const EventT& event)
{
    handle_key_event(to_native_event_ptr(event));
}
void handle_text_event(const void* native_event);
template <typename EventT>
inline void handle_text_event(const EventT& event)
{
    handle_text_event(to_native_event_ptr(event));
}
void handle_mouse_event(const void* native_event);
template <typename EventT>
inline void handle_mouse_event(const EventT& event)
{
    handle_mouse_event(to_native_event_ptr(event));
}
void handle_joy_event(const void* native_event);
template <typename EventT>
inline void handle_joy_event(const EventT& event)
{
    handle_joy_event(to_native_event_ptr(event));
}

// Takes platform keycode values.
void sendFakeKeyDownEvent(int keycode);
void sendFakeKeyUpEvent(int keycode);

inline int query_key() { return input_raw_key_ref(); }
inline const char* query_text_input() {
    if (input_raw_text_input_ref().empty()) return nullptr;
    return input_raw_text_input_ref().c_str();
}
inline bool query_input_continue() { return input_continue_ref(); }
inline short get_and_reset_scroll_amount() {
    short temp = input_scroll_amount_ref();
    input_scroll_amount_ref() = 0;
    return temp;
}

#ifdef USE_TOUCH_INPUT

// Touch input UI layout constants (shared by input and runtime bridge)
inline constexpr int MOVE_AREA_DIM = 60;
inline constexpr int MOVE_DEAD_ZONE = 10;
inline constexpr int FIRE_BUTTON_X = 245;
inline constexpr int FIRE_BUTTON_Y = 165;
inline constexpr int SPECIAL_BUTTON_X = 285;
inline constexpr int SPECIAL_BUTTON_Y = 165;
inline constexpr int YO_BUTTON_X = 160;
inline constexpr int YO_BUTTON_Y = 100;
inline constexpr int SWITCH_CHARACTER_BUTTON_X = 0;
inline constexpr int SWITCH_CHARACTER_BUTTON_Y = 0;
inline constexpr int NEXT_SPECIAL_BUTTON_X = 245;
inline constexpr int NEXT_SPECIAL_BUTTON_Y = 125;
inline constexpr int ALTERNATE_SPECIAL_BUTTON_X = 285;
inline constexpr int ALTERNATE_SPECIAL_BUTTON_Y = 125;
inline constexpr int BUTTON_DIM = 30;

class screen;
void draw_touch_controls(screen* vob);
bool input_touch_has_alternate();
#define CONTINUE_ACTION_STRING "TAP"
#else
#ifdef OUYA
#define CONTINUE_ACTION_STRING "PRESS 'O'"
#else
#define CONTINUE_ACTION_STRING "PRESS 'ESC'"
#endif
#endif

bool query_key_event(int key, const void* native_event);
template <typename EventT>
inline bool query_key_event(int key, const EventT& event)
{
    return query_key_event(key, to_native_event_ptr(event));
}

inline bool isAnyPlayerKey(int key) {
    for (int player_num = 0; player_num < 4; player_num++)
        for (int i = 0; i < NUM_KEYS; i++)
            if (input_player_keys()[player_num][i] == key)
                return true;
    return false;
}

inline bool isPlayerKey(int player_num, int key) {
    for (int i = 0; i < NUM_KEYS; i++)
        if (input_player_keys()[player_num][i] == key)
            return true;
    return false;
}

const void* wait_for_key_event();
void quit_if_quit_event(const void* native_event);
template <typename EventT>
inline void quit_if_quit_event(const EventT& event)
{
    quit_if_quit_event(to_native_event_ptr(event));
}

bool isKeyboardEvent(const void* native_event);
template <typename EventT>
inline bool isKeyboardEvent(const EventT& event)
{
    return isKeyboardEvent(to_native_event_ptr(event));
}
bool isJoystickEvent(const void* native_event);
template <typename EventT>
inline bool isJoystickEvent(const EventT& event)
{
    return isJoystickEvent(to_native_event_ptr(event));
}

void clear_events();

void assignKeyFromWaitEvent(int player_num, int key_enum);

const char* query_key_name(int keycode);

void clear_keyboard();
void wait_for_key(int somekey);
inline short query_key_press_event() { return input_key_press_event_ref(); }
inline void clear_key_press_event() { input_key_press_event_ref() = 0; }
inline short query_text_input_event() { return input_text_input_event_ref(); }
inline void clear_text_input_event() { input_text_input_event_ref() = 0; input_raw_text_input_ref().clear(); }
void init_input();

void grab_mouse();
void release_mouse();

MouseState& query_mouse();
inline MouseState& query_mouse_no_poll() { return mouse_state; }

unsigned char convert_to_ascii(int scancode);

// keystates and viewport globals live in GameSession — access via current_session->member_.

void update_overscan_setting();
