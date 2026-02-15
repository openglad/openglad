#include <cstring>

#include <openglad/input/input.h>
#include "test_framework.h"

extern float overscan_percentage;
extern float window_w;
extern float window_h;
extern float viewport_offset_x;
extern float viewport_offset_y;
extern float viewport_w;
extern float viewport_h;

extern MouseState mouse_state;

extern unsigned char convert_to_ascii(int scancode);

void test_input_handle_key_event_sets_continue_on_escape()
{
    clear_keyboard();

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_ESCAPE;
    e.key.keysym.mod = 0;

    handle_key_event(e);

    TEST_ASSERT_EQ((int)SDLK_ESCAPE, (int)query_key(), "query_key should return SDLK_ESCAPE after keydown");
    TEST_ASSERT(query_input_continue(), "Escape should set input_continue");
    TEST_ASSERT_EQ(1, (int)query_key_press_event(), "key_press_event should be set after keydown");

    clear_key_press_event();
    TEST_ASSERT_EQ(0, (int)query_key_press_event(), "clear_key_press_event should reset flag");
}
REGISTER_TEST(test_input_handle_key_event_sets_continue_on_escape);

void test_input_handle_text_event_sets_raw_text()
{
    clear_keyboard();

    SDL_Event e{};
    e.type = SDL_TEXTINPUT;
    std::strncpy(e.text.text, "abc", sizeof(e.text.text));
    e.text.text[sizeof(e.text.text) - 1] = '\0';

    handle_text_event(e);

    const char* s = query_text_input();
    TEST_ASSERT(s != nullptr, "query_text_input should return non-null after text input");
    TEST_ASSERT_STR_EQ("abc", s, "query_text_input should match injected text");
    TEST_ASSERT_EQ(1, (int)query_text_input_event(), "text_input_event should be set");

    clear_text_input_event();
    TEST_ASSERT(query_text_input() == nullptr, "clear_text_input_event should clear raw text");
}
REGISTER_TEST(test_input_handle_text_event_sets_raw_text);

void test_input_handle_mouse_motion_scales_to_game_coords()
{
    // Configure a simple 2x scale window (640x400) that maps to 320x200.
    viewport_offset_x = 0.0f;
    viewport_offset_y = 0.0f;
    viewport_w = 640.0f;
    viewport_h = 400.0f;

    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.x = 320;
    e.motion.y = 200;

    handle_mouse_event(e);

    TEST_ASSERT_EQ(160, (int)mouse_state.x, "mouse x should be scaled to 320-wide game coords");
    TEST_ASSERT_EQ(100, (int)mouse_state.y, "mouse y should be scaled to 200-tall game coords");
}
REGISTER_TEST(test_input_handle_mouse_motion_scales_to_game_coords);

void test_input_overscan_clamps_and_updates_viewport()
{
    window_w = 1000.0f;
    window_h = 800.0f;

    overscan_percentage = -1.0f;
    // Trigger update via resize event (calls update_overscan_setting internally).
    SDL_Event e{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_RESIZED;
    e.window.data1 = (int)window_w;
    e.window.data2 = (int)window_h;
    handle_window_event(e);

    TEST_ASSERT(overscan_percentage == 0.0f, "overscan should clamp at 0.0");
    TEST_ASSERT_EQ(0, (int)viewport_offset_x, "offset x should be 0 at 0% overscan");
    TEST_ASSERT_EQ(0, (int)viewport_offset_y, "offset y should be 0 at 0% overscan");
    TEST_ASSERT_EQ((int)window_w, (int)viewport_w, "viewport_w should match window_w at 0% overscan");
    TEST_ASSERT_EQ((int)window_h, (int)viewport_h, "viewport_h should match window_h at 0% overscan");

    overscan_percentage = 1.0f;
    handle_window_event(e);
    TEST_ASSERT(overscan_percentage == 0.25f, "overscan should clamp at 0.25");
    TEST_ASSERT(viewport_offset_x > 0.0f, "offset x should be >0 with overscan");
    TEST_ASSERT(viewport_offset_y > 0.0f, "offset y should be >0 with overscan");
    TEST_ASSERT(viewport_w < window_w, "viewport_w should shrink with overscan");
    TEST_ASSERT(viewport_h < window_h, "viewport_h should shrink with overscan");
}
REGISTER_TEST(test_input_overscan_clamps_and_updates_viewport);

void test_input_key_queries_and_ascii_conversion()
{
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_a;
    TEST_ASSERT(query_key_event(SDLK_a, e), "query_key_event should match keydown sym");
    TEST_ASSERT(!query_key_event(SDLK_b, e), "query_key_event should not match other keys");

    TEST_ASSERT(isAnyPlayerKey(SDLK_w), "isAnyPlayerKey should find player 0 default move key");
    TEST_ASSERT(isPlayerKey(0, SDLK_w), "isPlayerKey should be true for player 0 move key");
    TEST_ASSERT(!isPlayerKey(1, SDLK_w), "isPlayerKey should be false for other players' keys");

    TEST_ASSERT_EQ('A', (int)convert_to_ascii(SDLK_a), "convert_to_ascii(SDLK_a) should return 'A'");
    TEST_ASSERT_EQ('Z', (int)convert_to_ascii(SDLK_z), "convert_to_ascii(SDLK_z) should return 'Z'");
    TEST_ASSERT_EQ('0', (int)convert_to_ascii(SDLK_0), "convert_to_ascii(SDLK_0) should return '0'");
    TEST_ASSERT_EQ(255, (int)convert_to_ascii(SDLK_UNKNOWN), "convert_to_ascii(unknown) should return 255 sentinel");

    for (int i = 0; i < 26; ++i)
    {
        const int key = SDLK_a + i;
        const int expected = 'A' + i;
        TEST_ASSERT_EQ(expected, (int)convert_to_ascii(key), "alphabet key should map to uppercase ASCII");
    }

    TEST_ASSERT_EQ('1', (int)convert_to_ascii(SDLK_1), "digit key 1");
    TEST_ASSERT_EQ('2', (int)convert_to_ascii(SDLK_2), "digit key 2");
    TEST_ASSERT_EQ('3', (int)convert_to_ascii(SDLK_3), "digit key 3");
    TEST_ASSERT_EQ('4', (int)convert_to_ascii(SDLK_4), "digit key 4");
    TEST_ASSERT_EQ('5', (int)convert_to_ascii(SDLK_5), "digit key 5");
    TEST_ASSERT_EQ('6', (int)convert_to_ascii(SDLK_6), "digit key 6");
    TEST_ASSERT_EQ('7', (int)convert_to_ascii(SDLK_7), "digit key 7");
    TEST_ASSERT_EQ('8', (int)convert_to_ascii(SDLK_8), "digit key 8");
    TEST_ASSERT_EQ('9', (int)convert_to_ascii(SDLK_9), "digit key 9");
    TEST_ASSERT_EQ('0', (int)convert_to_ascii(SDLK_0), "digit key 0");

    TEST_ASSERT_EQ(32, (int)convert_to_ascii(SDLK_SPACE), "space");
    TEST_ASSERT_EQ(13, (int)convert_to_ascii(SDLK_RETURN), "return");
    TEST_ASSERT_EQ(27, (int)convert_to_ascii(SDLK_ESCAPE), "escape");
    TEST_ASSERT_EQ('.', (int)convert_to_ascii(SDLK_PERIOD), "period");
    TEST_ASSERT_EQ(',', (int)convert_to_ascii(SDLK_COMMA), "comma");
    TEST_ASSERT_EQ('\'', (int)convert_to_ascii(SDLK_QUOTE), "quote");
    TEST_ASSERT_EQ('`', (int)convert_to_ascii(SDLK_BACKQUOTE), "backquote");
}
REGISTER_TEST(test_input_key_queries_and_ascii_conversion);
