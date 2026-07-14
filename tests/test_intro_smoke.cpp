#include <gtest/gtest.h>
#include <openglad/interface/input.h>
#include <SDL3/SDL.h>
#include <cstring>

extern void intro_main(Sint32 argc, char** argv);

static void push_any_keypress()
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));

    e.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.key = SDLK_SPACE;
    e.key.scancode = SDL_SCANCODE_SPACE;
    SDL_PushEvent(&e);

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    e.key.key = SDLK_SPACE;
    e.key.scancode = SDL_SCANCODE_SPACE;
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

