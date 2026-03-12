#include "test_framework.h"
#include <openglad/interface/input.h>
#include <SDL.h>
#include <cstring>

extern void intro_main(Sint32 argc, char** argv);

static void push_any_keypress()
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));

    e.type = SDL_KEYDOWN;
    e.key.state = SDL_PRESSED;
    e.key.keysym.sym = SDLK_SPACE;
    e.key.keysym.scancode = SDL_SCANCODE_SPACE;
    SDL_PushEvent(&e);

    memset(&e, 0, sizeof(e));
    e.type = SDL_KEYUP;
    e.key.state = SDL_RELEASED;
    e.key.keysym.sym = SDLK_SPACE;
    e.key.keysym.scancode = SDL_SCANCODE_SPACE;
    SDL_PushEvent(&e);
}

TEST(IntroSmoke, intro_main_aborts_on_keypress)
{
    // intro_main's show() steps abort when query_key_press_event() is true.
    // Push a key so the intro exits quickly but still executes real code paths.
    push_any_keypress();
    intro_main(0, nullptr);

    // Run the full intro path as well to exercise all show()/cleanup branches.
    clear_events();
    clear_key_press_event();
    clear_keyboard();
    intro_main(0, nullptr);
}

