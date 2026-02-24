#include "SDL.h"
#include <openglad/runtime/screen.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

void test_sdl_init() {
    // SDL is already initialized by test_main - just verify it's working
    const char* driver = SDL_GetCurrentVideoDriver();
    TEST_ASSERT(driver != nullptr, "Video driver should be set");
}
REGISTER_TEST(test_sdl_init);

void test_screen_creation() {
    // Screen is already created by test_main - verify key runtime objects exist.
    TEST_ASSERT(myscreen != nullptr, "screen should be created successfully");
    TEST_ASSERT(myscreen->numviews >= 1, "screen should initialize at least one view");
    TEST_ASSERT(myscreen->viewob[0] != nullptr, "primary view should be initialized");
    TEST_ASSERT(myscreen->soundp != nullptr, "sound object should be initialized");
}
REGISTER_TEST(test_screen_creation);
