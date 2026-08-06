#ifndef _TEST_INPUT_HELPERS_H__
#define _TEST_INPUT_HELPERS_H__

#include <SDL3/SDL.h>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

// Push a fake mouse button down event at game coordinates (x, y).
// Game coordinates are 320x200; with the default viewport these map
// 1:1 to SDL window coordinates.
inline void inject_mouse_down(int x, int y)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 1;
    event.button.x = static_cast<float>(x);
    event.button.y = static_cast<float>(y);
    SDL_PushEvent(&event);
}

// Push a fake mouse button up event at game coordinates (x, y).
inline void inject_mouse_up(int x, int y)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = false;
    event.button.clicks = 1;
    event.button.x = static_cast<float>(x);
    event.button.y = static_cast<float>(y);
    SDL_PushEvent(&event);
}

// Push a complete mouse click (down + delay + up) at game coords.
// The delay_ms between down and up lets the menu loop pick up the
// transition edge that leftmouse() detects.
inline void inject_click(int x, int y, int delay_ms = 50)
{
    inject_mouse_down(x, y);
    SDL_Delay(static_cast<Uint32>(delay_ms));
    inject_mouse_up(x, y);
}

// Push a fake key down event (uses SDL_PushEvent, thread-safe).
inline void inject_key_down(int keycode)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.repeat = false;
    event.key.down = true;
    event.key.key = static_cast<SDL_Keycode>(keycode);
    event.key.mod = 0;
    event.key.scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(keycode), nullptr);
    SDL_PushEvent(&event);
}

// Push a fake key up event.
inline void inject_key_up(int keycode)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_UP;
    event.key.repeat = false;
    event.key.down = false;
    event.key.key = static_cast<SDL_Keycode>(keycode);
    event.key.mod = 0;
    event.key.scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(keycode), nullptr);
    SDL_PushEvent(&event);
}

// Push a complete key press (down + delay + up).
inline void inject_key_press(int keycode, int delay_ms = 50)
{
    inject_key_down(keycode);
    SDL_Delay(static_cast<Uint32>(delay_ms));
    inject_key_up(keycode);
}

// Inject a synthetic text-input event (e.g. "a", "ab") into the input pipeline.
//
// SDL3's SDL_TextInputEvent::text is a const char* and SDL_PushEvent
// shallow-copies the event without copying or freeing an app-owned text
// pointer, so the string must outlive the event queue: intern it in a
// never-freed pool.
inline const char* intern_injected_text(const char* utf8)
{
    static std::mutex mu;
    static std::deque<std::string> pool;   // never invalidates on push_back; reachable at exit => LSan-clean
    std::lock_guard<std::mutex> lock(mu);
    pool.emplace_back(utf8);
    return pool.back().c_str();
}

// Pushed SDL_EVENT_TEXT_INPUT bypasses the SDL_StartTextInput()/focus gate
// (gating lives in SDL_SendKeyboardText, generation only) -- works under
// SDL_VIDEODRIVER=dummy exactly like the real-SDL2 push path.
inline void inject_text_input(const char* utf8)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = 0;
    event.text.text = intern_injected_text(utf8);
    SDL_PushEvent(&event);
}

#endif // _TEST_INPUT_HELPERS_H__
