#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

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
    JoyDeviceAdded,
    JoyDeviceRemoved,
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

    // Hotplug (JoyDeviceAdded/Removed): the 0-based device index when the
    // instance id is still enumerable (Added fires after the device joins the
    // list), or -1 when it is not (Removed fires after the device left). The
    // raw SDL instance id rides alongside for logging/diagnostics.
    int joy_device_index = -1;
    int joy_device_instance = 0;

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
// Web builds install the Backspace-as-Escape event remap here (see
// openglad/interface/web_back_key.h); a no-op on native builds.
void install_back_key_remap();
// Web touch overlay BACK button: holds a virtual physical Backspace in the
// web keyboard-state mirror (menu hotkeys and spin-waits are keystate-driven,
// which pushed events cannot reach). No-op on native builds.
void set_virtual_back_key(bool down);
const void* poll_event();
const void* wait_event();
const void* make_test_keydown_event(int keycode, int scancode);
void push_key_event(bool down, int keycode);
// Pushes an SDL text-input event whose text points into a static ring of
// buffers; consumers must copy at poll time (decode_event already does).
void push_text_event(const char* utf8);
void push_touch_event(EventType type, float x, float y, float dx, float dy, std::int64_t finger_id);
void push_mouse_button_event(bool down, int button, int x, int y);
// Like push_mouse_button_event, but the coordinates are CSS pixels relative
// to the displayed canvas of the given CSS size. They are rescaled into SDL
// window-logical space first: the web canvas shows the SDL window at an
// arbitrary CSS scale (e.g. the picker runs a larger logical window on a
// small phone canvas), and pushed events bypass SDL's own DOM-to-logical
// scaling.
void push_mouse_button_event_css(
    bool down, int button, int css_x, int css_y, int css_w, int css_h);
std::uint32_t ticks_ms();

const bool* keyboard_state();
int scancode_from_key(int keycode);
const char* key_name(int keycode);

// Clears SDL's keyboard held-state (SDL synthesizes key-up events for every
// key it believes is down). Used on focus/visibility loss: a release that
// happened while another window/app had the keys — or that the browser
// swallowed outright — never reaches us, and the key would stay latched
// forever. Genuine holds re-assert on the next delivered key events.
void reset_keyboard_state();

using JoystickHandle = void*;
int num_joysticks();
JoystickHandle joystick_open(int index);
int joystick_num_axes(JoystickHandle joystick);
int joystick_num_buttons(JoystickHandle joystick);
int joystick_num_hats(JoystickHandle joystick);
int joystick_get_axis(JoystickHandle joystick, int axis);
int joystick_get_button(JoystickHandle joystick, int button);
int joystick_get_hat(JoystickHandle joystick, int hat);
// Stable device-model identity for persistence (SDL joystick GUID as text).
// "" when device_index is out of range. Note SDL GUIDs identify the device
// model, not the unit: two identical pads share one GUID.
std::string joystick_device_guid(int device_index);
void joystick_set_event_state(bool enabled);
bool joystick_subsystem_initialized();
void joystick_quit_subsystem();
void joystick_init_subsystem();

// The yield every blocking in-game loop owes the browser: natively a plain
// delay, on the web an SDL_Delay that suspends through -sASYNCIFY. A loop
// that spins without calling it never hands control back and hangs the tab.
void sleep_ms(int ms);
#ifdef TESTING
// Monotonic count of sleep_ms() calls — lets a test prove a blocking modal
// actually yields instead of busy-waiting (natively the sleep itself is
// invisible, so "the call returned" proves nothing on its own).
unsigned long yield_count();
#endif
void show_cursor(bool show);
// Start a native text-input session and, on web touch devices, describe the
// active prompt to the DOM keyboard affordance. `max_bytes` is the usable
// payload limit (excluding the C string terminator); multiline prompts keep
// their canvas-native editor instead of showing the single-line DOM field.
void start_text_input(const char* initial_value = nullptr,
                      int max_bytes = 0,
                      const char* prompt = nullptr,
                      bool multiline = false);
void stop_text_input();
// Deliver Escape-cancel semantics to the active text prompt (input_string's
// restore-original-and-return-null path). Needed by the web DOM text-entry
// overlay's CANCEL button: during text input Backspace means delete-character
// and the web back-key filter swallows pushed Escape, so cancel requires this
// dedicated seam (it grants the pushed Escape pair a one-shot pass through
// the filter). No-op unless text input is active.
void push_text_cancel_key();
} // namespace og::input_native
