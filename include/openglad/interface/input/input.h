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

#include <openglad/legacy/base.h>
#include <cctype>
#include <string>

struct SDL_Rect;
union SDL_Event;

using SDLKey = int;

#define KEYSTATE_UNKNOWN 0
#define KEYSTATE_a 4
#define KEYSTATE_b 5
#define KEYSTATE_c 6
#define KEYSTATE_d 7
#define KEYSTATE_e 8
#define KEYSTATE_f 9
#define KEYSTATE_g 10
#define KEYSTATE_h 11
#define KEYSTATE_i 12
#define KEYSTATE_j 13
#define KEYSTATE_k 14
#define KEYSTATE_l 15
#define KEYSTATE_m 16
#define KEYSTATE_n 17
#define KEYSTATE_o 18
#define KEYSTATE_p 19
#define KEYSTATE_q 20
#define KEYSTATE_r 21
#define KEYSTATE_s 22
#define KEYSTATE_t 23
#define KEYSTATE_u 24
#define KEYSTATE_v 25
#define KEYSTATE_w 26
#define KEYSTATE_x 27
#define KEYSTATE_y 28
#define KEYSTATE_z 29
#define KEYSTATE_1 30
#define KEYSTATE_2 31
#define KEYSTATE_3 32
#define KEYSTATE_4 33
#define KEYSTATE_5 34
#define KEYSTATE_6 35
#define KEYSTATE_7 36
#define KEYSTATE_8 37
#define KEYSTATE_9 38
#define KEYSTATE_0 39
#define KEYSTATE_RETURN 40
#define KEYSTATE_ESCAPE 41
#define KEYSTATE_SPACE 44
#define KEYSTATE_LEFTBRACKET 47
#define KEYSTATE_RIGHTBRACKET 48
#define KEYSTATE_COMMA 54
#define KEYSTATE_PERIOD 55
#define KEYSTATE_SLASH 56
#define KEYSTATE_F1 58
#define KEYSTATE_F5 62
#define KEYSTATE_F9 66
#define KEYSTATE_F10 67
#define KEYSTATE_PAGEUP 75
#define KEYSTATE_DELETE 76
#define KEYSTATE_PAGEDOWN 78
#define KEYSTATE_RIGHT 79
#define KEYSTATE_LEFT 80
#define KEYSTATE_DOWN 81
#define KEYSTATE_UP 82
#define KEYSTATE_KP_MULTIPLY 85
#define KEYSTATE_KP_MINUS 86
#define KEYSTATE_KP_PLUS 87
#define KEYSTATE_KP_1 89
#define KEYSTATE_KP_2 90
#define KEYSTATE_KP_3 91
#define KEYSTATE_KP_4 92
#define KEYSTATE_KP_5 93
#define KEYSTATE_KP_6 94
#define KEYSTATE_KP_7 95
#define KEYSTATE_KP_8 96
#define KEYSTATE_KP_9 97
#define KEYSTATE_KP_0 98
#define KEYSTATE_LCTRL 224
#define KEYSTATE_LSHIFT 225
#define KEYSTATE_RCTRL 228
#define KEYSTATE_RSHIFT 229


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
    
    void setKeyFromEvent(int key_enum, const SDL_Event& event);
    
    bool getState(int key_enum) const;
    bool getPress(int key_enum, const SDL_Event& event) const;
    bool getRelease(int key_enum, const SDL_Event& event) const;
    bool hasButtonSet(int key_enum) const;
};

struct MouseState
{
    float x, y;
    bool left;
    bool right;

    bool in(const SDL_Rect& r) const;
};

// Input hardware state accessors are implemented in input.cpp.
MouseState& input_mouse_state();
JoyData* input_player_joy();

// Macros preserve existing access patterns (mouse_state.x, player_joy[i], etc.).
#define mouse_state (input_mouse_state())
#define player_joy (input_player_joy())

// Inline trivial accessors for joystick/key/input state
inline bool playerHasJoystick(int player_num) { return (player_joy[player_num].index >= 0); }
inline void disablePlayerJoystick(int player_num) { player_joy[player_num].index = -1; }

void resetJoystick(int player_num);
void reinit_joystick_subsystem();
bool isPlayerHoldingKey(int player_index, int key_enum);
bool didPlayerPressKey(int player_index, int key_enum, const SDL_Event& event);
bool didPlayerReleaseKey(int player_index, int key_enum, const SDL_Event& event);

//buffers: added prototype
void get_input_events(bool type);
void handle_events(const SDL_Event& event);

// Specific event handling
void handle_window_event(const SDL_Event& event);
void handle_key_event(const SDL_Event& event);
void handle_text_event(const SDL_Event& event);
void handle_mouse_event(const SDL_Event& event);
void handle_joy_event(const SDL_Event& event);

// Takes SDLK (SDL_Keycode) values
void sendFakeKeyDownEvent(int keycode);
void sendFakeKeyUpEvent(int keycode);

int query_key();
const char* query_text_input();
bool query_input_continue();
short get_and_reset_scroll_amount();

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

bool query_key_event(int key, const SDL_Event& event);

bool isAnyPlayerKey(SDLKey key);
bool isPlayerKey(int player_num, SDLKey key);

SDL_Event wait_for_key_event();
void quit_if_quit_event(const SDL_Event& event);

bool isKeyboardEvent(const SDL_Event& event);
bool isJoystickEvent(const SDL_Event& event);

void clear_events();

void assignKeyFromWaitEvent(int player_num, int key_enum);

void clear_keyboard();
void wait_for_key(int somekey);
short query_key_press_event();
void clear_key_press_event();
short query_text_input_event();
void clear_text_input_event();
void init_input();

void grab_mouse();
void release_mouse();

MouseState& query_mouse();
MouseState& query_mouse_no_poll();

unsigned char convert_to_ascii(int scancode);

// keystates and viewport globals live in GameSession — access via current_session->member_.

void update_overscan_setting();
