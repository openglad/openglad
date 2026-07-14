#include <openglad/interface/native_input.h>

#include <SDL3/SDL.h>

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
    case SDL_EVENT_TEXT_INPUT: return EventType::TextInput;
    case SDL_EVENT_MOUSE_WHEEL: return EventType::MouseWheel;
    case SDL_EVENT_FINGER_MOTION: return EventType::FingerMotion;
    case SDL_EVENT_FINGER_UP: return EventType::FingerUp;
    case SDL_EVENT_FINGER_DOWN: return EventType::FingerDown;
    case SDL_EVENT_KEY_DOWN: return EventType::KeyDown;
    case SDL_EVENT_KEY_UP: return EventType::KeyUp;
    case SDL_EVENT_MOUSE_MOTION: return EventType::MouseMotion;
    case SDL_EVENT_MOUSE_BUTTON_UP: return EventType::MouseButtonUp;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: return EventType::MouseButtonDown;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION: return EventType::JoyAxisMotion;
    case SDL_EVENT_JOYSTICK_HAT_MOTION: return EventType::JoyHatMotion;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: return EventType::JoyButtonDown;
    case SDL_EVENT_JOYSTICK_BUTTON_UP: return EventType::JoyButtonUp;
    case SDL_EVENT_QUIT: return EventType::Quit;
    default: return EventType::Unknown;
    }
}

WindowEventType map_window_event(Uint8 event)
{
    switch (event)
    {
    case SDL_EVENT_WINDOW_MINIMIZED: return WindowEventType::Minimized;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return WindowEventType::Close;
    case SDL_EVENT_WINDOW_RESTORED: return WindowEventType::Restored;
    case SDL_EVENT_WINDOW_RESIZED: return WindowEventType::Resized;
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
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        out.key_sym = e.key.key;
        // Some synthetic events only initialize keycode; derive scancode from
        // keycode to avoid loading a potentially invalid enum payload.
        out.key_scancode = static_cast<int>(SDL_GetScancodeFromKey(out.key_sym));
        out.key_mod = e.key.mod;
        out.key_repeat = e.key.repeat != 0;
        break;
    case SDL_EVENT_TEXT_INPUT:
        std::memcpy(out.text.data(), e.text.text, out.text.size());
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        out.wheel_y = e.wheel.y;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        out.motion_x = e.motion.x;
        out.motion_y = e.motion.y;
        out.motion_dx = e.motion.xrel;
        out.motion_dy = e.motion.yrel;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        out.button = e.button.button;
        out.button_x = e.button.x;
        out.button_y = e.button.y;
        break;
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_DOWN:
        out.finger_x = e.tfinger.x;
        out.finger_y = e.tfinger.y;
        out.finger_dx = e.tfinger.dx;
        out.finger_dy = e.tfinger.dy;
        out.finger_id = static_cast<std::int64_t>(e.tfinger.fingerID);
        break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        out.joy_axis_which = e.jaxis.which;
        out.joy_axis_axis = e.jaxis.axis;
        out.joy_axis_value = e.jaxis.value;
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        out.joy_button_which = e.jbutton.which;
        out.joy_button_button = e.jbutton.button;
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        out.joy_hat_which = e.jhat.which;
        out.joy_hat_hat = e.jhat.hat;
        out.joy_hat_value = e.jhat.value;
        break;
    case SDL_WINDOWEVENT:
        out.window_event = map_window_event(e.window.event);
        out.window_data1 = e.window.data1;
        out.window_data2 = e.window.data2;
        break;
    case SDL_EVENT_USER:
        out.user_code = e.user.code;
        // intptr_t is defined to round-trip a void* losslessly; this is the
        // canonical, portable idiom (std::bit_cast needs a newer libc++ than the
        // ctest Emscripten toolchain ships).
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
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.repeat = 0;
    event.key.key = keycode;
    event.key.mod = 0;
    event.key.scancode = static_cast<SDL_Scancode>(scancode);
    return &event;
}

void push_key_event(bool down, int keycode)
{
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.repeat = 0;
    event.key.key = keycode;
    event.key.mod = 0;
    event.key.scancode = SDL_GetScancodeFromKey(keycode);
    SDL_PushEvent(&event);
}

void push_touch_event(EventType type, float x, float y, float dx, float dy, std::int64_t finger_id)
{
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));

    switch (type)
    {
    case EventType::FingerMotion: event.type = SDL_EVENT_FINGER_MOTION; break;
    case EventType::FingerUp: event.type = SDL_EVENT_FINGER_UP; break;
    case EventType::FingerDown: event.type = SDL_EVENT_FINGER_DOWN; break;
    default: return;
    }

    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.dx = dx;
    event.tfinger.dy = dy;
    event.tfinger.touchID = 1;
    event.tfinger.fingerID = static_cast<SDL_FingerID>(finger_id);
    SDL_PushEvent(&event);
}

void push_mouse_button_event(bool down, int button, int x, int y)
{
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = static_cast<Uint8>(button);
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
}

std::uint32_t ticks_ms()
{
    return SDL_GetTicks();
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
    return SDL_GetNumJoystickAxes(static_cast<SDL_Joystick*>(joystick));
}

int joystick_num_buttons(JoystickHandle joystick)
{
    return SDL_GetNumJoystickButtons(static_cast<SDL_Joystick*>(joystick));
}

int joystick_num_hats(JoystickHandle joystick)
{
    return SDL_GetNumJoystickHats(static_cast<SDL_Joystick*>(joystick));
}

int joystick_get_axis(JoystickHandle joystick, int axis)
{
    return SDL_GetJoystickAxis(static_cast<SDL_Joystick*>(joystick), axis);
}

int joystick_get_button(JoystickHandle joystick, int button)
{
    return SDL_GetJoystickButton(static_cast<SDL_Joystick*>(joystick), button);
}

int joystick_get_hat(JoystickHandle joystick, int hat)
{
    return SDL_GetJoystickHat(static_cast<SDL_Joystick*>(joystick), hat);
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

void start_text_input()
{
    SDL_StartTextInput();
}

void stop_text_input()
{
    SDL_StopTextInput();
}
} // namespace og::input_native
