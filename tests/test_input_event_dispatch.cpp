#include "graph.h"
#include "input/input.h"
#include "test_framework.h"

// input.cpp internal entry point (declared in input.h too).
void handle_events(const SDL_Event& event);

void test_input_handle_events_dispatches_various_event_types()
{
    SDL_Event e{};

    e.type = SDL_FINGERMOTION;
    handle_events(e);
    e.type = SDL_FINGERDOWN;
    handle_events(e);
    e.type = SDL_FINGERUP;
    handle_events(e);

    e.type = SDL_MOUSEMOTION;
    handle_events(e);
    e.type = SDL_MOUSEBUTTONDOWN;
    handle_events(e);
    e.type = SDL_MOUSEBUTTONUP;
    handle_events(e);
    e.type = SDL_MOUSEWHEEL;
    handle_events(e);

    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_a;
    handle_events(e);
    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_a;
    handle_events(e);

    // Joy events (even if no joystick is present, handler should tolerate it).
    e = SDL_Event{};
    e.type = SDL_JOYAXISMOTION;
    e.jaxis.which = 0;
    e.jaxis.axis = 0;
    e.jaxis.value = 1000;
    handle_events(e);

    e = SDL_Event{};
    e.type = SDL_JOYBUTTONDOWN;
    e.jbutton.which = 0;
    e.jbutton.button = 0;
    handle_events(e);

    e = SDL_Event{};
    e.type = SDL_JOYBUTTONUP;
    e.jbutton.which = 0;
    e.jbutton.button = 0;
    handle_events(e);

    // Quit should be safe in TESTING (picker quit path is no-op).
    e = SDL_Event{};
    e.type = SDL_QUIT;
    handle_events(e);
}
REGISTER_TEST(test_input_handle_events_dispatches_various_event_types);

