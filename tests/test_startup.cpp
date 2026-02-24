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
    TEST_ASSERT(og::runtime::current_session->myscreen_ != nullptr, "screen should be created successfully");
    TEST_ASSERT(og::runtime::current_session->myscreen_->numviews >= 1, "screen should initialize at least one view");
    TEST_ASSERT(og::runtime::current_session->myscreen_->viewob[0] != nullptr, "primary view should be initialized");
    TEST_ASSERT(og::runtime::current_session->myscreen_->soundp != nullptr, "sound object should be initialized");
}
REGISTER_TEST(test_screen_creation);
