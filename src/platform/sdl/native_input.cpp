#include <openglad/interface/native_input.h>

#include "SDL.h"

#include <cstring>

namespace og::input_native
{
namespace
{
EventType map_event_type(Uint32 type)
{
    switch (type)
    {
    case SDL_WINDOWEVENT: return EventType::Window;
    case SDL_TEXTINPUT: return EventType::TextInput;
    case SDL_MOUSEWHEEL: return EventType::MouseWheel;
    case SDL_FINGERMOTION: return EventType::FingerMotion;
    case SDL_FINGERUP: return EventType::FingerUp;
    case SDL_FINGERDOWN: return EventType::FingerDown;
    case SDL_KEYDOWN: return EventType::KeyDown;
    case SDL_KEYUP: return EventType::KeyUp;
    case SDL_MOUSEMOTION: return EventType::MouseMotion;
    case SDL_MOUSEBUTTONUP: return EventType::MouseButtonUp;
    case SDL_MOUSEBUTTONDOWN: return EventType::MouseButtonDown;
    case SDL_JOYAXISMOTION: return EventType::JoyAxisMotion;
    case SDL_JOYHATMOTION: return EventType::JoyHatMotion;
    case SDL_JOYBUTTONDOWN: return EventType::JoyButtonDown;
    case SDL_JOYBUTTONUP: return EventType::JoyButtonUp;
    case SDL_QUIT: return EventType::Quit;
    default: return EventType::Unknown;
    }
}

WindowEventType map_window_event(Uint8 event)
{
    switch (event)
    {
    case SDL_WINDOWEVENT_MINIMIZED: return WindowEventType::Minimized;
    case SDL_WINDOWEVENT_CLOSE: return WindowEventType::Close;
    case SDL_WINDOWEVENT_RESTORED: return WindowEventType::Restored;
    case SDL_WINDOWEVENT_RESIZED: return WindowEventType::Resized;
    default: return WindowEventType::Unknown;
    }
}
} // namespace

bool decode_event(const void* native_event, EventData& out)
{
    if (native_event == nullptr)
        return false;

    const SDL_Event& e = *static_cast<const SDL_Event*>(native_event);
    out = {};
    out.type = map_event_type(e.type);
    out.raw_type = static_cast<int>(e.type);

    switch (e.type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        out.key_sym = e.key.keysym.sym;
        // Some synthetic events only initialize keycode; derive scancode from
        // keycode to avoid loading a potentially invalid enum payload.
        out.key_scancode = static_cast<int>(SDL_GetScancodeFromKey(out.key_sym));
        out.key_mod = e.key.keysym.mod;
        out.key_repeat = e.key.repeat != 0;
        break;
    case SDL_TEXTINPUT:
        std::memcpy(out.text, e.text.text, sizeof(out.text));
        break;
    case SDL_MOUSEWHEEL:
        out.wheel_y = e.wheel.y;
        break;
    case SDL_MOUSEMOTION:
        out.motion_x = e.motion.x;
        out.motion_y = e.motion.y;
        out.motion_dx = e.motion.xrel;
        out.motion_dy = e.motion.yrel;
        break;
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEBUTTONDOWN:
        out.button = e.button.button;
        out.button_x = e.button.x;
        out.button_y = e.button.y;
        break;
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
    case SDL_FINGERDOWN:
        out.finger_x = e.tfinger.x;
        out.finger_y = e.tfinger.y;
        out.finger_dx = e.tfinger.dx;
        out.finger_dy = e.tfinger.dy;
        out.finger_id = static_cast<std::int64_t>(e.tfinger.fingerId);
        break;
    case SDL_JOYAXISMOTION:
        out.joy_axis_which = e.jaxis.which;
        out.joy_axis_axis = e.jaxis.axis;
        out.joy_axis_value = e.jaxis.value;
        break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        out.joy_button_which = e.jbutton.which;
        out.joy_button_button = e.jbutton.button;
        break;
    case SDL_JOYHATMOTION:
        out.joy_hat_which = e.jhat.which;
        out.joy_hat_hat = e.jhat.hat;
        out.joy_hat_value = e.jhat.value;
        break;
    case SDL_WINDOWEVENT:
        out.window_event = map_window_event(e.window.event);
        out.window_data1 = e.window.data1;
        out.window_data2 = e.window.data2;
        break;
    case SDL_USEREVENT:
        out.user_code = e.user.code;
        out.user_data1 = reinterpret_cast<std::intptr_t>(e.user.data1);
        break;
    default:
        break;
    }
    return true;
}

const void* poll_event()
{
    static thread_local SDL_Event event;
    if (SDL_PollEvent(&event))
        return &event;
    return nullptr;
}

const void* wait_event()
{
    static thread_local SDL_Event event;
    if (SDL_WaitEvent(&event))
        return &event;
    return nullptr;
}

const void* make_test_keydown_event(int keycode, int scancode)
{
    static thread_local SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.repeat = 0;
    event.key.keysym.sym = keycode;
    event.key.keysym.mod = 0;
    event.key.keysym.scancode = static_cast<SDL_Scancode>(scancode);
    return &event;
}

void push_key_event(bool down, int keycode)
{
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.repeat = 0;
    event.key.keysym.sym = keycode;
    event.key.keysym.mod = 0;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(keycode);
    SDL_PushEvent(&event);
}

void push_touch_event(EventType type, float x, float y, float dx, float dy, std::int64_t finger_id)
{
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));

    switch (type)
    {
    case EventType::FingerMotion: event.type = SDL_FINGERMOTION; break;
    case EventType::FingerUp: event.type = SDL_FINGERUP; break;
    case EventType::FingerDown: event.type = SDL_FINGERDOWN; break;
    default: return;
    }

    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.dx = dx;
    event.tfinger.dy = dy;
    event.tfinger.touchId = 1;
    event.tfinger.fingerId = static_cast<SDL_FingerID>(finger_id);
    SDL_PushEvent(&event);
}

const std::uint8_t* keyboard_state()
{
    return SDL_GetKeyboardState(nullptr);
}

int scancode_from_key(int keycode)
{
    return SDL_GetScancodeFromKey(keycode);
}

const char* key_name(int keycode)
{
    return SDL_GetKeyName(keycode);
}

int num_joysticks()
{
    return SDL_NumJoysticks();
}

JoystickHandle joystick_open(int index)
{
    return SDL_JoystickOpen(index);
}

int joystick_num_axes(JoystickHandle joystick)
{
    return SDL_JoystickNumAxes(static_cast<SDL_Joystick*>(joystick));
}

int joystick_num_buttons(JoystickHandle joystick)
{
    return SDL_JoystickNumButtons(static_cast<SDL_Joystick*>(joystick));
}

int joystick_num_hats(JoystickHandle joystick)
{
    return SDL_JoystickNumHats(static_cast<SDL_Joystick*>(joystick));
}

int joystick_get_axis(JoystickHandle joystick, int axis)
{
    return SDL_JoystickGetAxis(static_cast<SDL_Joystick*>(joystick), axis);
}

int joystick_get_button(JoystickHandle joystick, int button)
{
    return SDL_JoystickGetButton(static_cast<SDL_Joystick*>(joystick), button);
}

int joystick_get_hat(JoystickHandle joystick, int hat)
{
    return SDL_JoystickGetHat(static_cast<SDL_Joystick*>(joystick), hat);
}

void joystick_set_event_state(bool enabled)
{
    SDL_JoystickEventState(enabled ? SDL_ENABLE : SDL_DISABLE);
}

bool joystick_subsystem_initialized()
{
    return (SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) != 0;
}

void joystick_quit_subsystem()
{
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

void joystick_init_subsystem()
{
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
}

void sleep_ms(int ms)
{
    SDL_Delay(static_cast<Uint32>(ms));
}

void show_cursor(bool show)
{
    SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}
} // namespace og::input_native
