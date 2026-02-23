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

#include "SDL.h"
#include <cctype>
#include <string>


#define SDLKey SDL_Keycode

#define KEYSTATE_UNKNOWN SDL_SCANCODE_UNKNOWN
#define KEYSTATE_a SDL_SCANCODE_A
#define KEYSTATE_b SDL_SCANCODE_B
#define KEYSTATE_c SDL_SCANCODE_C
#define KEYSTATE_d SDL_SCANCODE_D
#define KEYSTATE_e SDL_SCANCODE_E
#define KEYSTATE_f SDL_SCANCODE_F
#define KEYSTATE_g SDL_SCANCODE_G
#define KEYSTATE_h SDL_SCANCODE_H
#define KEYSTATE_i SDL_SCANCODE_I
#define KEYSTATE_j SDL_SCANCODE_J
#define KEYSTATE_k SDL_SCANCODE_K
#define KEYSTATE_l SDL_SCANCODE_L
#define KEYSTATE_m SDL_SCANCODE_M
#define KEYSTATE_n SDL_SCANCODE_N
#define KEYSTATE_o SDL_SCANCODE_O
#define KEYSTATE_p SDL_SCANCODE_P
#define KEYSTATE_q SDL_SCANCODE_Q
#define KEYSTATE_r SDL_SCANCODE_R
#define KEYSTATE_s SDL_SCANCODE_S
#define KEYSTATE_t SDL_SCANCODE_T
#define KEYSTATE_u SDL_SCANCODE_U
#define KEYSTATE_v SDL_SCANCODE_V
#define KEYSTATE_w SDL_SCANCODE_W
#define KEYSTATE_x SDL_SCANCODE_X
#define KEYSTATE_y SDL_SCANCODE_Y
#define KEYSTATE_z SDL_SCANCODE_Z
#define KEYSTATE_0 SDL_SCANCODE_0
#define KEYSTATE_1 SDL_SCANCODE_1
#define KEYSTATE_2 SDL_SCANCODE_2
#define KEYSTATE_3 SDL_SCANCODE_3
#define KEYSTATE_4 SDL_SCANCODE_4
#define KEYSTATE_5 SDL_SCANCODE_5
#define KEYSTATE_6 SDL_SCANCODE_6
#define KEYSTATE_7 SDL_SCANCODE_7
#define KEYSTATE_8 SDL_SCANCODE_8
#define KEYSTATE_9 SDL_SCANCODE_9
#define KEYSTATE_COMMA SDL_SCANCODE_COMMA
#define KEYSTATE_PERIOD SDL_SCANCODE_PERIOD
#define KEYSTATE_DELETE SDL_SCANCODE_DELETE
#define KEYSTATE_UP SDL_SCANCODE_UP
#define KEYSTATE_DOWN SDL_SCANCODE_DOWN
#define KEYSTATE_LEFT SDL_SCANCODE_LEFT
#define KEYSTATE_RIGHT SDL_SCANCODE_RIGHT
#define KEYSTATE_PAGEDOWN SDL_SCANCODE_PAGEDOWN
#define KEYSTATE_PAGEUP SDL_SCANCODE_PAGEUP
#define KEYSTATE_RETURN SDL_SCANCODE_RETURN
#define KEYSTATE_ESCAPE SDL_SCANCODE_ESCAPE
#define KEYSTATE_SPACE SDL_SCANCODE_SPACE
#define KEYSTATE_SLASH SDL_SCANCODE_SLASH
#define KEYSTATE_LCTRL SDL_SCANCODE_LCTRL
#define KEYSTATE_RCTRL SDL_SCANCODE_RCTRL
#define KEYSTATE_LSHIFT SDL_SCANCODE_LSHIFT
#define KEYSTATE_RSHIFT SDL_SCANCODE_RSHIFT
#define KEYSTATE_RIGHTBRACKET SDL_SCANCODE_RIGHTBRACKET
#define KEYSTATE_LEFTBRACKET SDL_SCANCODE_LEFTBRACKET
#define KEYSTATE_KP_PLUS SDL_SCANCODE_KP_PLUS
#define KEYSTATE_KP_MINUS SDL_SCANCODE_KP_MINUS
#define KEYSTATE_KP_MULTIPLY SDL_SCANCODE_KP_MULTIPLY
#define KEYSTATE_KP_0 SDL_SCANCODE_KP_0
#define KEYSTATE_KP_1 SDL_SCANCODE_KP_1
#define KEYSTATE_KP_2 SDL_SCANCODE_KP_2
#define KEYSTATE_KP_3 SDL_SCANCODE_KP_3
#define KEYSTATE_KP_4 SDL_SCANCODE_KP_4
#define KEYSTATE_KP_5 SDL_SCANCODE_KP_5
#define KEYSTATE_KP_6 SDL_SCANCODE_KP_6
#define KEYSTATE_KP_7 SDL_SCANCODE_KP_7
#define KEYSTATE_KP_8 SDL_SCANCODE_KP_8
#define KEYSTATE_KP_9 SDL_SCANCODE_KP_9
#define KEYSTATE_F1 SDL_SCANCODE_F1
#define KEYSTATE_F5 SDL_SCANCODE_F5
#define KEYSTATE_F9 SDL_SCANCODE_F9
#define KEYSTATE_F10 SDL_SCANCODE_F10


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

// Input state globals (defined in input.cpp)
extern JoyData player_joy[4];
extern int player_keys[4][NUM_KEYS];
extern int raw_key;
extern std::string raw_text_input;
extern short key_press_event;
extern short text_input_event;
extern short scroll_amount;
extern bool input_continue;

// Inline trivial accessors for joystick/key/input state
inline bool playerHasJoystick(int player_num) { return (player_joy[player_num].index >= 0); }
inline void disablePlayerJoystick(int player_num) { player_joy[player_num].index = -1; }

void resetJoystick(int player_num);
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

inline int query_key() { return raw_key; }
inline const char* query_text_input() {
    if (raw_text_input.empty()) return nullptr;
    return raw_text_input.c_str();
}
inline bool query_input_continue() { return input_continue; }
inline short get_and_reset_scroll_amount() {
    short temp = scroll_amount;
    scroll_amount = 0;
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

inline bool query_key_event(int key, const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN)
        return (event.key.keysym.sym == key);
    return false;
}

inline bool isAnyPlayerKey(SDLKey key) {
    for (int player_num = 0; player_num < 4; player_num++)
        for (int i = 0; i < NUM_KEYS; i++)
            if (player_keys[player_num][i] == key)
                return true;
    return false;
}

inline bool isPlayerKey(int player_num, SDLKey key) {
    for (int i = 0; i < NUM_KEYS; i++)
        if (player_keys[player_num][i] == key)
            return true;
    return false;
}

SDL_Event wait_for_key_event();
void quit_if_quit_event(const SDL_Event& event);

inline bool isKeyboardEvent(const SDL_Event& event) { return (event.type == SDL_KEYDOWN); }
inline bool isJoystickEvent(const SDL_Event& event) {
    return (event.type == SDL_JOYAXISMOTION || event.type == SDL_JOYHATMOTION || event.type == SDL_JOYBUTTONDOWN);
}

void clear_events();

void assignKeyFromWaitEvent(int player_num, int key_enum);

void clear_keyboard();
void wait_for_key(int somekey);
inline short query_key_press_event() { return key_press_event; }
inline void clear_key_press_event() { key_press_event = 0; }
inline short query_text_input_event() { return text_input_event; }
inline void clear_text_input_event() { text_input_event = 0; raw_text_input.clear(); }
void init_input();

void grab_mouse();
void release_mouse();

struct MouseState
{
    float x, y;
    bool left;
    bool right;
    
	    bool in(const SDL_Rect& r) const
	    {
	        const float rx = static_cast<float>(r.x);
	        const float ry = static_cast<float>(r.y);
	        const float rw = static_cast<float>(r.w);
	        const float rh = static_cast<float>(r.h);
	        return (rx <= x && x < rx + rw && ry <= y && y < ry + rh);
	    }
	};

extern MouseState mouse_state;

MouseState& query_mouse();
inline MouseState& query_mouse_no_poll() { return mouse_state; }

unsigned char convert_to_ascii(int scancode);

extern const Uint8* keystates;

extern float viewport_offset_x;  // In window coords
extern float viewport_offset_y;
extern float window_w;
extern float window_h;
extern float viewport_w;
extern float viewport_h;

extern float overscan_percentage;

void update_overscan_setting();
