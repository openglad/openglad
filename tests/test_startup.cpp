#include "SDL.h"
#include <openglad/interface/screen.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

TEST(Startup, sdl_init) {
    // SDL is already initialized by test_main - just verify it's working
    const char* driver = SDL_GetCurrentVideoDriver();
    ASSERT_TRUE(driver != nullptr) << "Video driver should be set";
}


TEST(Startup, screen_creation) {
    // Screen is already created by test_main - verify key runtime objects exist.
    ASSERT_TRUE(og::runtime::current_session->myscreen_ != nullptr) << "screen should be created successfully";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->numviews >= 1) << "screen should initialize at least one view";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->viewob[0] != nullptr) << "primary view should be initialized";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->soundp != nullptr) << "sound object should be initialized";
}

