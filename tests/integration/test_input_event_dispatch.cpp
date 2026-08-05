#include <openglad/interface/input.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

TEST(InputEventDispatch, input_handle_events_dispatches_various_event_types)
{
    SDL_Event e{};

    e.type = SDL_EVENT_FINGER_MOTION;
    handle_events(e);
    e.type = SDL_EVENT_FINGER_DOWN;
    handle_events(e);
    e.type = SDL_EVENT_FINGER_UP;
    handle_events(e);

    e.type = SDL_EVENT_MOUSE_MOTION;
    handle_events(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    handle_events(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    handle_events(e);
    e.type = SDL_EVENT_MOUSE_WHEEL;
    handle_events(e);

    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_A;
    handle_events(e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.key = SDLK_A;
    handle_events(e);

    // Joy events (even if no joystick is present, handler should tolerate it).
    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    e.jaxis.which = 0;
    e.jaxis.axis = 0;
    e.jaxis.value = 1000;
    handle_events(e);

    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 0;
    e.jbutton.button = 0;
    handle_events(e);

    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_UP;
    e.jbutton.which = 0;
    e.jbutton.button = 0;
    handle_events(e);

    // Quit should be safe in TESTING (picker quit path is no-op).
    e = SDL_Event{};
    e.type = SDL_EVENT_QUIT;
    handle_events(e);
}

