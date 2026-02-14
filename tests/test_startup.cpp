#include <openglad/legacy/graph.h>
#include <openglad/legacy/test_trace.h>
#include "test_framework.h"

extern screen* myscreen;

void test_sdl_init() {
    // SDL is already initialized by test_main - just verify it's working
    const char* driver = SDL_GetCurrentVideoDriver();
    TEST_ASSERT(driver != nullptr, "Video driver should be set");
}
REGISTER_TEST(test_sdl_init);

void test_screen_creation() {
    // Screen is already created by test_main - verify traces were logged during init
    TEST_ASSERT(myscreen != nullptr, "screen should be created successfully");
    TEST_ASSERT(trace_contains("init", "screen constructor"), "screen constructor trace should be logged");
    TEST_ASSERT(trace_contains("init", "video initialized"), "video initialized trace should be logged");
}
REGISTER_TEST(test_screen_creation);
