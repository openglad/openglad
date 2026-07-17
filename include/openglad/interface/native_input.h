#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace og::input_native
{
enum class EventType : std::uint8_t
{
    Unknown = 0,
    Window,
    TextInput,
    MouseWheel,
    FingerMotion,
    FingerUp,
    FingerDown,
    KeyDown,
    KeyUp,
    MouseMotion,
    MouseButtonUp,
    MouseButtonDown,
    JoyAxisMotion,
    JoyHatMotion,
    JoyButtonDown,
    JoyButtonUp,
    ControllerAxisMotion,
    ControllerButtonDown,
    ControllerButtonUp,
    Quit
};

enum class WindowEventType : std::uint8_t
{
    Unknown = 0,
    Minimized,
    Close,
    Restored,
    Resized
};

struct EventData
{
    EventType type = EventType::Unknown;
    int raw_type = 0;

    int key_sym = 0;
    int key_scancode = 0;
    int key_mod = 0;
    bool key_repeat = false;

    std::array<char, 32> text = {};

    int wheel_y = 0;

    int motion_x = 0;
    int motion_y = 0;
    int motion_dx = 0;
    int motion_dy = 0;

    int button = 0;
    int button_x = 0;
    int button_y = 0;

    float finger_x = 0.0f;
    float finger_y = 0.0f;
    float finger_dx = 0.0f;
    float finger_dy = 0.0f;
    std::int64_t finger_id = 0;

    int joy_axis_which = 0;
    int joy_axis_axis = 0;
    int joy_axis_value = 0;

    int joy_button_which = 0;
    int joy_button_button = 0;

    int joy_hat_which = 0;
    int joy_hat_hat = 0;
    int joy_hat_value = 0;

    // SDL_GameController events. controller_button holds a ControllerButton
    // value; controller_axis a ControllerAxis value (with controller_axis_value
    // the -32768..32767 reading).
    int controller_which = 0;
    int controller_button = 0;
    int controller_axis = 0;
    int controller_axis_value = 0;

    WindowEventType window_event = WindowEventType::Unknown;
    int window_data1 = 0;
    int window_data2 = 0;

    int user_code = 0;
    std::intptr_t user_data1 = 0;
};

constexpr int kMouseButtonLeft = 1;
constexpr int kMouseButtonRight = 3;
constexpr int kHatUp = 0x01;
constexpr int kHatRight = 0x02;
constexpr int kHatDown = 0x04;
constexpr int kHatLeft = 0x08;

bool decode_event(const void* native_event, EventData& out);
const void* poll_event();
const void* wait_event();
const void* make_test_keydown_event(int keycode, int scancode);
void push_key_event(bool down, int keycode);
void push_touch_event(EventType type, float x, float y, float dx, float dy, std::int64_t finger_id);
void push_mouse_button_event(bool down, int button, int x, int y);
std::uint32_t ticks_ms();

const std::uint8_t* keyboard_state();
int scancode_from_key(int keycode);
const char* key_name(int keycode);

using JoystickHandle = void*;
int num_joysticks();
JoystickHandle joystick_open(int index);
int joystick_num_axes(JoystickHandle joystick);
int joystick_num_buttons(JoystickHandle joystick);
int joystick_num_hats(JoystickHandle joystick);
int joystick_get_axis(JoystickHandle joystick, int axis);
int joystick_get_button(JoystickHandle joystick, int button);
int joystick_get_hat(JoystickHandle joystick, int hat);
void joystick_set_event_state(bool enabled);
bool joystick_subsystem_initialized();
void joystick_quit_subsystem();
void joystick_init_subsystem();

// --- SDL_GameController (gamepad) support ---------------------------------
// The GameController API sits on top of the raw joystick API and provides a
// normalized button/axis layout (via the community gamecontrollerdb mappings),
// so any recognized pad reports the same logical buttons. Handles are opaque
// SDL_GameController* pointers.
using GameControllerHandle = void*;

// Logical axis indices; values match SDL_GameControllerAxis so they can be
// passed straight through the wrappers below. Axis range is -32768..32767.
enum class ControllerAxis : int
{
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    TriggerLeft,
    TriggerRight
};

// Logical button indices; values match SDL_GameControllerButton. get_button
// returns 1 while pressed, 0 otherwise.
enum class ControllerButton : int
{
    A = 0,
    B,
    X,
    Y,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight
};

void gamecontroller_init_subsystem();
bool gamecontroller_subsystem_initialized();
// Load additional controller mappings from an asset file (resolved through the
// resource search path). Returns the number of mappings added, or -1 on error.
int gamecontroller_add_mappings_from_file(const char* file);
bool is_game_controller(int joystick_index);
GameControllerHandle game_controller_open(int joystick_index);
void game_controller_close(GameControllerHandle controller);
int game_controller_get_axis(GameControllerHandle controller, int axis);
int game_controller_get_button(GameControllerHandle controller, int button);
const char* game_controller_name(GameControllerHandle controller);

void sleep_ms(int ms);
void show_cursor(bool show);
void start_text_input();
void stop_text_input();
} // namespace og::input_native
