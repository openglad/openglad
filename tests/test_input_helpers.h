#ifndef _TEST_INPUT_HELPERS_H__
#define _TEST_INPUT_HELPERS_H__

#include "SDL.h"
#include <cstring>

#ifdef OPENGLAD_SDL2_IS_COMPAT
#include <openglad/interface/input.h>  // handle_text_event (compat dispatch path)
#endif

// Push a fake mouse button down event at game coordinates (x, y).
// Game coordinates are 320x200; with the default viewport these map
// 1:1 to SDL window coordinates.
inline void inject_mouse_down(int x, int y)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state = SDL_PRESSED;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
}

// Push a fake mouse button up event at game coordinates (x, y).
inline void inject_mouse_up(int x, int y)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONUP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state = SDL_RELEASED;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
}

// Push a complete mouse click (down + delay + up) at game coords.
// The delay_ms between down and up lets the menu loop pick up the
// transition edge that leftmouse() detects.
inline void inject_click(int x, int y, int delay_ms = 50)
{
    inject_mouse_down(x, y);
    SDL_Delay(delay_ms);
    inject_mouse_up(x, y);
}

// Push a fake key down event (uses SDL_PushEvent, thread-safe).
inline void inject_key_down(int keycode)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.repeat = false;
    event.key.keysym.sym = keycode;
    event.key.keysym.mod = 0;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(keycode);
    SDL_PushEvent(&event);
}

// Push a fake key up event.
inline void inject_key_up(int keycode)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYUP;
    event.key.repeat = false;
    event.key.keysym.sym = keycode;
    event.key.keysym.mod = 0;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(keycode);
    SDL_PushEvent(&event);
}

// Push a complete key press (down + delay + up).
inline void inject_key_press(int keycode, int delay_ms = 50)
{
    inject_key_down(keycode);
    SDL_Delay(delay_ms);
    inject_key_up(keycode);
}

// Inject a synthetic text-input event (e.g. "a", "ab") into the input pipeline.
//
// On real SDL2 this pushes a genuine SDL_TEXTINPUT onto the event queue,
// exercising the full SDL_PushEvent -> get_input_events -> handle_text_event
// path. Under sdl2-compat (an SDL2 API shim over SDL3, detected by CMake which
// defines OPENGLAD_SDL2_IS_COMPAT) pushing an SDL_TEXTINPUT crashes the shim --
// SDL3 changed SDL_TextInputEvent::text from an inline char[32] to a heap
// const char* -- so dispatch straight to handle_text_event, the same call the
// event drain makes. The text lands in the shared (cross-thread) session state,
// so this is safe from injector threads; callers that drive a blocking
// get_input_events(WAIT) loop must still push a subsequent real event (e.g. a
// Return key) to wake it, exactly as they do on the real-SDL2 path.
inline void inject_text_input(const char* utf8)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_TEXTINPUT;
    SDL_strlcpy(event.text.text, utf8, sizeof(event.text.text));
#ifdef OPENGLAD_SDL2_IS_COMPAT
    handle_text_event(event);
#else
    SDL_PushEvent(&event);
#endif
}

#endif // _TEST_INPUT_HELPERS_H__
