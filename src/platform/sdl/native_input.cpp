#include <openglad/interface/native_input.h>
#include <openglad/interface/web_back_key.h>

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace og::input_native
{
namespace
{
// The SDL-free translation header duplicates these SDL constants; keep them
// pinned to the real values.
static_assert(og::input::kBackKeycodeEscape == SDLK_ESCAPE);
static_assert(og::input::kBackKeycodeBackspace == SDLK_BACKSPACE);
static_assert(og::input::kBackScancodeEscape == SDL_SCANCODE_ESCAPE);
static_assert(og::input::kBackScancodeBackspace == SDL_SCANCODE_BACKSPACE);

// Bracketed by start_text_input()/stop_text_input(); on web this gates the
// Backspace-as-Escape remap so text fields keep Backspace = delete-character.
bool s_text_input_active = false;
bool s_text_cancel_requested = false;

#ifdef __EMSCRIPTEN__
// keystates_ consumers (button hotkeys, menu loops, gameplay bindings) read a
// scancode-indexed array. SDL's own array reflects physical keys, so web
// builds read this mirror instead: the Escape slot follows physical
// Backspace (outside text input) and never follows physical Escape.
std::array<bool, SDL_SCANCODE_COUNT> s_web_keyboard_state{};

// Touch overlay BACK button (web_touch_bridge.cpp). Pushed key events cannot
// update SDL's keyboard-state array, but menu hotkeys and spin-waits are
// keystate-driven, so the overlay's held BACK is OR-ed into the mirror as a
// virtual physical Backspace (translated to Escape outside text input, exactly
// like the real key).
bool s_virtual_back_key_down = false;

// Budget of pushed Escape key events allowed through the always-swallow rule.
// push_text_cancel_key() grants exactly one down+up pair so the DOM
// text-entry overlay's CANCEL button can reach input_string's Escape path.
// SDL runs the event filter synchronously inside SDL_PushEvent, so the budget
// is consumed before push_text_cancel_key() returns.
int s_allowed_escape_events = 0;

void refresh_web_keyboard_state()
{
    int numkeys = 0;
    const bool* state = SDL_GetKeyboardState(&numkeys);
    if (state == nullptr)
        return;
    const int n = std::min(numkeys, static_cast<int>(SDL_SCANCODE_COUNT));
    std::copy(state, state + n, s_web_keyboard_state.begin());
    const og::input::BackKeyStates back = og::input::translate_back_key_states(
        state[SDL_SCANCODE_ESCAPE],
        state[SDL_SCANCODE_BACKSPACE] || s_virtual_back_key_down,
        /*web_mode=*/true, s_text_input_active);
    s_web_keyboard_state[SDL_SCANCODE_ESCAPE] = back.escape_down;
    s_web_keyboard_state[SDL_SCANCODE_BACKSPACE] = back.backspace_down;
}

// Web-only: Backspace is the universal back/cancel key and physical Escape is
// swallowed (browsers reserve Escape for fullscreen exit; it must not ALSO
// back out of a menu). Applied at the SDL event source so every consumer —
// the decode_event seam, game_loop's direct SDL poll, and raw_key_ — sees one
// consistent remap. Runs at event-post time, which also covers pushed events.
bool SDLCALL web_back_key_event_filter(void* /*userdata*/, SDL_Event* event)
{
    if (event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_KEY_UP)
        return true;
    // SDL updates its keyboard-state array before posting the event, and
    // every state change is accompanied by a key event (including the
    // synthetic releases from a focus-loss keyboard reset). Refreshing here
    // keeps the mirror correct even on paths that pump SDL directly without
    // going through this seam's poll_event/wait_event (e.g. the game loop).
    refresh_web_keyboard_state();
    if ((event->key.key == SDLK_ESCAPE ||
         event->key.scancode == SDL_SCANCODE_ESCAPE) &&
        s_allowed_escape_events > 0)
    {
        // A text-cancel Escape granted by push_text_cancel_key(): deliver it
        // untranslated instead of applying the always-swallow rule.
        --s_allowed_escape_events;
        return true;
    }
    const og::input::BackKeyEventTranslation t = og::input::translate_back_key(
        static_cast<int>(event->key.key),
        static_cast<int>(event->key.scancode),
        /*web_mode=*/true, s_text_input_active);
    if (t.swallow)
        return false;
    event->key.key = static_cast<SDL_Keycode>(t.keycode);
    event->key.scancode = static_cast<SDL_Scancode>(t.scancode);
    return true;
}
#endif

EventType map_event_type(Uint32 type)
{
    // SDL3 flattened window events into first-class event types occupying a
    // contiguous range; map the whole range to Window.
    if (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST)
        return EventType::Window;
    switch (type)
    {
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
    case SDL_EVENT_JOYSTICK_ADDED: return EventType::JoyDeviceAdded;
    case SDL_EVENT_JOYSTICK_REMOVED: return EventType::JoyDeviceRemoved;
    case SDL_EVENT_QUIT: return EventType::Quit;
    default: return EventType::Unknown;
    }
}

WindowEventType map_window_event(Uint32 type)
{
    switch (type)
    {
    case SDL_EVENT_WINDOW_MINIMIZED: return WindowEventType::Minimized;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return WindowEventType::Close;
    case SDL_EVENT_WINDOW_RESTORED: return WindowEventType::Restored;
    case SDL_EVENT_WINDOW_RESIZED: return WindowEventType::Resized;
    // Any other type in the window range (e.g. SDL_EVENT_WINDOW_OCCLUDED)
    // stays Unknown, matching the SDL2 unknown-subtype behavior.
    default: return WindowEventType::Unknown;
    }
}

// SDL3 joystick events carry instance ids (starting at 1); the seam contract
// is "which == 0-based device index" (input.cpp matches it against
// JoyData::index). Translate exactly once, here at decode time. Ids not in
// the current device list (test-constructed events under the dummy driver, or
// a device unplugged before decode) pass through unchanged so injected
// index-style values keep working.
int joystick_index_from_id(SDL_JoystickID which)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (ids != nullptr)
    {
        for (int i = 0; i < count; ++i)
        {
            if (ids[i] == which)
            {
                SDL_free(ids);
                return i;
            }
        }
        SDL_free(ids);
    }
    return static_cast<int>(which);
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

    if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
    {
        out.window_event = map_window_event(e.type);
        out.window_data1 = e.window.data1;
        out.window_data2 = e.window.data2;
        return true;
    }

    switch (e.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        out.key_sym = static_cast<int>(e.key.key);
        // Some synthetic events only initialize keycode; derive scancode from
        // keycode to avoid loading a potentially invalid enum payload.
        out.key_scancode = static_cast<int>(SDL_GetScancodeFromKey(e.key.key, nullptr));
        out.key_mod = e.key.mod;
        out.key_repeat = e.key.repeat;
        break;
    case SDL_EVENT_TEXT_INPUT:
        // SDL3 text is a const char* of arbitrary length — copying
        // out.text.size() bytes from it would read out of bounds.
        SDL_strlcpy(out.text.data(), e.text.text != nullptr ? e.text.text : "", out.text.size());
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        // integer_y accumulates whole scroll "ticks" (Sint32, since 3.2.12) —
        // the exact SDL2 semantics for real wheel events.
        out.wheel_y = e.wheel.integer_y;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        out.motion_x = static_cast<int>(e.motion.x);
        out.motion_y = static_cast<int>(e.motion.y);
        out.motion_dx = static_cast<int>(e.motion.xrel);
        out.motion_dy = static_cast<int>(e.motion.yrel);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        out.button = e.button.button;
        out.button_x = static_cast<int>(e.button.x);
        out.button_y = static_cast<int>(e.button.y);
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
        out.joy_axis_which = joystick_index_from_id(e.jaxis.which);
        out.joy_axis_axis = e.jaxis.axis;
        out.joy_axis_value = e.jaxis.value;
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
        out.joy_button_which = joystick_index_from_id(e.jbutton.which);
        out.joy_button_button = e.jbutton.button;
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        out.joy_hat_which = joystick_index_from_id(e.jhat.which);
        out.joy_hat_hat = e.jhat.hat;
        out.joy_hat_value = e.jhat.value;
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
    {
        // No pass-through here (unlike axis/button/hat): an unresolvable id
        // means the device already left the list, and consumers re-enumerate
        // rather than trusting a stale index.
        out.joy_device_instance = static_cast<int>(e.jdevice.which);
        out.joy_device_index = -1;
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetJoysticks(&count); ids != nullptr)
        {
            for (int i = 0; i < count; ++i)
            {
                if (ids[i] == e.jdevice.which)
                {
                    out.joy_device_index = i;
                    break;
                }
            }
            SDL_free(ids);
        }
        break;
    }
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
    const bool got = SDL_PollEvent(&event);
#ifdef __EMSCRIPTEN__
    // SDL_PollEvent pumps; keep the remapped keyboard mirror in sync even
    // when the queue is empty so held/released keys are observed.
    refresh_web_keyboard_state();
#endif
    if (got)
        return &event;
    return nullptr;
}

const void* wait_event()
{
    static thread_local SDL_Event event;
    const bool got = SDL_WaitEvent(&event);
#ifdef __EMSCRIPTEN__
    refresh_web_keyboard_state();
#endif
    if (got)
        return &event;
    return nullptr;
}

void install_back_key_remap()
{
#ifdef __EMSCRIPTEN__
    SDL_SetEventFilter(web_back_key_event_filter, nullptr);
    refresh_web_keyboard_state();
#endif
}

void set_virtual_back_key(bool down)
{
#ifdef __EMSCRIPTEN__
    s_virtual_back_key_down = down;
    // The flag changes the mirror without any real key event; re-derive now so
    // keystate consumers observe it before the next pump.
    refresh_web_keyboard_state();
#else
    (void)down;
#endif
}

const void* make_test_keydown_event(int keycode, int scancode)
{
    static thread_local SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.repeat = false;
    event.key.key = static_cast<SDL_Keycode>(keycode);
    event.key.mod = 0;
    event.key.scancode = static_cast<SDL_Scancode>(scancode);
    return &event;
}

void push_key_event(bool down, int keycode)
{
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down;
    event.key.repeat = false;
    event.key.key = static_cast<SDL_Keycode>(keycode);
    event.key.mod = 0;
    event.key.scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(keycode), nullptr);
    SDL_PushEvent(&event);
}

void push_text_event(const char* utf8)
{
    // SDL3 text events borrow their string. Route the payload through a
    // static ring so the pointer stays valid until the same-poll copy in
    // decode_event; 8 slots comfortably outlive one queue drain.
    static std::array<std::array<char, 64>, 8> ring{};
    static std::size_t ring_index = 0;
    std::array<char, 64>& slot = ring[ring_index];
    ring_index = (ring_index + 1) % ring.size();
    SDL_strlcpy(slot.data(), utf8 != nullptr ? utf8 : "", slot.size());

    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = slot.data();
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
    event.button.x = static_cast<float>(x);
    event.button.y = static_cast<float>(y);
    event.button.down = down;
    SDL_PushEvent(&event);
}

void push_mouse_button_event_css(
    bool down, int button, int css_x, int css_y, int css_w, int css_h)
{
    // See the header: rescale CSS-relative canvas coordinates into SDL
    // window-logical space (the picker can run a larger logical window than
    // the CSS canvas box; pushed events bypass SDL's DOM scaling).
    float x = static_cast<float>(css_x);
    float y = static_cast<float>(css_y);
    int window_count = 0;
    if (SDL_Window** const windows = SDL_GetWindows(&window_count);
        windows != nullptr)
    {
        if (window_count > 0 && windows[0] != nullptr &&
            css_w > 0 && css_h > 0)
        {
            int window_w = 0;
            int window_h = 0;
            if (SDL_GetWindowSize(windows[0], &window_w, &window_h) &&
                window_w > 0 && window_h > 0)
            {
                x = x * static_cast<float>(window_w) /
                    static_cast<float>(css_w);
                y = y * static_cast<float>(window_h) /
                    static_cast<float>(css_h);
            }
        }
        SDL_free(windows);
    }

    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = static_cast<Uint8>(button);
    event.button.x = x;
    event.button.y = y;
    event.button.down = down;
    SDL_PushEvent(&event);
}

std::uint32_t ticks_ms()
{
    // The seam stays 32-bit: producers truncate SDL3's Uint64 ticks and
    // consumers rely on modulo-2^32 elapsed arithmetic.
    return static_cast<std::uint32_t>(SDL_GetTicks());
}

const bool* keyboard_state()
{
#ifdef __EMSCRIPTEN__
    // Web builds read the remapped mirror (stable address; refreshed on every
    // event pump) so Backspace drives KEYSTATE_ESCAPE consumers and physical
    // Escape never does.
    refresh_web_keyboard_state();
    return s_web_keyboard_state.data();
#else
    return SDL_GetKeyboardState(nullptr);
#endif
}

int scancode_from_key(int keycode)
{
    return static_cast<int>(SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(keycode), nullptr));
}

void reset_keyboard_state()
{
    SDL_ResetKeyboard();
#ifdef __EMSCRIPTEN__
    // The mirror follows SDL's array through key events; a reset must be
    // visible to keystate consumers before the next pump as well.
    refresh_web_keyboard_state();
#endif
}

const char* key_name(int keycode)
{
    return SDL_GetKeyName(static_cast<SDL_Keycode>(keycode));
}

int num_joysticks()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (ids == nullptr)
        return 0;
    SDL_free(ids);
    return count;
}

JoystickHandle joystick_open(int index)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    SDL_Joystick* joystick =
        (ids != nullptr && index >= 0 && index < count) ? SDL_OpenJoystick(ids[index]) : nullptr;
    SDL_free(ids);
    return joystick;
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
    // SDL3 returns bool; the seam keeps the SDL2-style int.
    return SDL_GetJoystickButton(static_cast<SDL_Joystick*>(joystick), button) ? 1 : 0;
}

int joystick_get_hat(JoystickHandle joystick, int hat)
{
    return SDL_GetJoystickHat(static_cast<SDL_Joystick*>(joystick), hat);
}

std::string joystick_device_guid(int device_index)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (ids == nullptr)
        return {};
    std::string guid_text;
    if (device_index >= 0 && device_index < count)
    {
        const SDL_GUID guid = SDL_GetJoystickGUIDForID(ids[device_index]);
        char buf[33];
        SDL_GUIDToString(guid, buf, static_cast<int>(sizeof(buf)));
        guid_text = buf;
        // The all-zero GUID is SDL's failure value, not an identity.
        if (guid_text.find_first_not_of('0') == std::string::npos) guid_text.clear();
    }
    SDL_free(ids);
    return guid_text;
}

void joystick_set_event_state(bool enabled)
{
    SDL_SetJoystickEventsEnabled(enabled);
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
    if (show)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

namespace
{
// SDL3's SDL_StartTextInput/SDL_StopTextInput take an SDL_Window*. Prefer the
// keyboard-focus window (NULL under the dummy driver), falling back to the
// first open window. A null result makes start/stop a silent no-op — pushed
// test events bypass the text-input gate anyway (gating lives only in
// SDL_SendKeyboardText).
SDL_Window* text_input_window()
{
    SDL_Window* w = SDL_GetKeyboardFocus();
    if (w != nullptr)
        return w;
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (windows == nullptr)
        return nullptr;
    w = (count > 0) ? windows[0] : nullptr;
    SDL_free(windows);
    return w;
}
} // namespace

void start_text_input(const char* initial_value, int max_bytes,
                      const char* prompt, bool multiline)
{
#ifndef __EMSCRIPTEN__
    (void)initial_value;
    (void)max_bytes;
    (void)prompt;
    (void)multiline;
#endif
    // Gate the web Backspace-as-Escape remap off for the whole text-entry
    // window, even when no SDL window exists (dummy driver in tests).
    s_text_input_active = true;
    s_text_cancel_requested = false;
#ifdef __EMSCRIPTEN__
    s_allowed_escape_events = 0;
    // The flag changes the mapping without any key event; re-derive now.
    refresh_web_keyboard_state();
    // SDL3's emscripten backend has no screen-keyboard support, so the shell
    // provides a DOM text-entry overlay on touch devices. Signal it here.
// EM_ASM is spelled with $-placeholders and JS '' literals; both are lexed as
// C++ tokens here, so silence the lexer's complaints around this block only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
#pragma clang diagnostic ignored "-Winvalid-pp-token"
    EM_ASM({
        window.dispatchEvent(new CustomEvent('openglad-text-input',
            { detail: {
                active: true,
                initialValue: $0 ? UTF8ToString($0) : '',
                maxBytes: $1,
                prompt: $2 ? UTF8ToString($2) : '',
                multiline: Boolean($3)
            } }));
    }, initial_value, std::max(0, max_bytes), prompt,
       multiline ? 1 : 0);
#pragma clang diagnostic pop
#endif
    if (SDL_Window* w = text_input_window())
        SDL_StartTextInput(w);
}

void stop_text_input()
{
    s_text_input_active = false;
    s_text_cancel_requested = false;
#ifdef __EMSCRIPTEN__
    s_allowed_escape_events = 0;
    refresh_web_keyboard_state();
    EM_ASM({
        window.dispatchEvent(new CustomEvent('openglad-text-input',
                                             { detail: { active: false } }));
    });
#endif
    if (SDL_Window* w = text_input_window())
        SDL_StopTextInput(w);
}

void push_text_cancel_key()
{
    // Only meaningful while a text prompt is polling for keys; outside that
    // window a pushed Escape would back out of whatever menu is active.
    if (!s_text_input_active || s_text_cancel_requested)
        return;
    s_text_cancel_requested = true;
#ifdef __EMSCRIPTEN__
    // Let exactly this down+up pair through the web filter's Escape swallow.
    s_allowed_escape_events = 2;
#endif
    push_key_event(true, SDLK_ESCAPE);
    push_key_event(false, SDLK_ESCAPE);
}
} // namespace og::input_native
