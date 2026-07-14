#include <cstring>

#include <openglad/interface/input.h>
#include <openglad/platform/game_session.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>


extern unsigned char convert_to_ascii(int scancode);

TEST(Input, handle_key_event_sets_continue_on_escape)
{
    clear_keyboard();

    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_ESCAPE;
    e.key.mod = 0;

    handle_key_event(e);

    ASSERT_EQ((int)SDLK_ESCAPE, (int)query_key()) << "query_key should return SDLK_ESCAPE after keydown";
    ASSERT_TRUE(query_input_continue()) << "Escape should set input_continue";
    ASSERT_EQ(1, (int)query_key_press_event()) << "key_press_event should be set after keydown";

    clear_key_press_event();
    ASSERT_EQ(0, (int)query_key_press_event()) << "clear_key_press_event should reset flag";
}


TEST(Input, handle_text_event_sets_raw_text)
{
    clear_keyboard();

    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    std::strncpy(e.text.text, "abc", sizeof(e.text.text));
    e.text.text[sizeof(e.text.text) - 1] = '\0';

    handle_text_event(e);

    const char* s = query_text_input();
    ASSERT_TRUE(s != nullptr) << "query_text_input should return non-null after text input";
    ASSERT_STREQ("abc", s) << "query_text_input should match injected text";
    ASSERT_EQ(1, (int)query_text_input_event()) << "text_input_event should be set";

    clear_text_input_event();
    ASSERT_TRUE(query_text_input() == nullptr) << "clear_text_input_event should clear raw text";
}


TEST(Input, handle_mouse_motion_scales_to_game_coords)
{
    // Configure a simple 2x scale window (640x400) that maps to 320x200.
    og::runtime::current_session->viewport_offset_x_ = 0.0f;
    og::runtime::current_session->viewport_offset_y_ = 0.0f;
    og::runtime::current_session->viewport_w_ = 640.0f;
    og::runtime::current_session->viewport_h_ = 400.0f;

    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = 320;
    e.motion.y = 200;

    handle_mouse_event(e);

    ASSERT_EQ(160, (int)mouse_state.x) << "mouse x should be scaled to 320-wide game coords";
    ASSERT_EQ(100, (int)mouse_state.y) << "mouse y should be scaled to 200-tall game coords";
}


TEST(Input, overscan_clamps_and_updates_viewport)
{
    // Save original viewport state so we don't poison later tests.
    const float saved_window_w = og::runtime::current_session->window_w_;
    const float saved_window_h = og::runtime::current_session->window_h_;
    const float saved_overscan = og::runtime::current_session->overscan_percentage_;
    const float saved_vp_ox = og::runtime::current_session->viewport_offset_x_;
    const float saved_vp_oy = og::runtime::current_session->viewport_offset_y_;
    const float saved_vp_w = og::runtime::current_session->viewport_w_;
    const float saved_vp_h = og::runtime::current_session->viewport_h_;

    og::runtime::current_session->window_w_ = 1000.0f;
    og::runtime::current_session->window_h_ = 800.0f;

    og::runtime::current_session->overscan_percentage_ = -1.0f;
    // Trigger update via resize event (calls update_overscan_setting internally).
    SDL_Event e{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_EVENT_WINDOW_RESIZED;
    e.window.data1 = (int)og::runtime::current_session->window_w_;
    e.window.data2 = (int)og::runtime::current_session->window_h_;
    handle_window_event(e);

    ASSERT_TRUE(og::runtime::current_session->overscan_percentage_ == 0.0f) << "overscan should clamp at 0.0";
    ASSERT_EQ(0, (int)og::runtime::current_session->viewport_offset_x_) << "offset x should be 0 at 0% overscan";
    ASSERT_EQ(0, (int)og::runtime::current_session->viewport_offset_y_) << "offset y should be 0 at 0% overscan";
    ASSERT_EQ((int)og::runtime::current_session->window_w_, (int)og::runtime::current_session->viewport_w_) << "viewport_w should match window_w at 0% overscan";
    ASSERT_EQ((int)og::runtime::current_session->window_h_, (int)og::runtime::current_session->viewport_h_) << "viewport_h should match window_h at 0% overscan";

    og::runtime::current_session->overscan_percentage_ = 1.0f;
    handle_window_event(e);
    ASSERT_TRUE(og::runtime::current_session->overscan_percentage_ == 0.25f) << "overscan should clamp at 0.25";
    ASSERT_TRUE(og::runtime::current_session->viewport_offset_x_ > 0.0f) << "offset x should be >0 with overscan";
    ASSERT_TRUE(og::runtime::current_session->viewport_offset_y_ > 0.0f) << "offset y should be >0 with overscan";
    ASSERT_TRUE(og::runtime::current_session->viewport_w_ < og::runtime::current_session->window_w_) << "viewport_w should shrink with overscan";
    ASSERT_TRUE(og::runtime::current_session->viewport_h_ < og::runtime::current_session->window_h_) << "viewport_h should shrink with overscan";

    // Restore viewport state.
    og::runtime::current_session->window_w_ = saved_window_w;
    og::runtime::current_session->window_h_ = saved_window_h;
    og::runtime::current_session->overscan_percentage_ = saved_overscan;
    og::runtime::current_session->viewport_offset_x_ = saved_vp_ox;
    og::runtime::current_session->viewport_offset_y_ = saved_vp_oy;
    og::runtime::current_session->viewport_w_ = saved_vp_w;
    og::runtime::current_session->viewport_h_ = saved_vp_h;
}


TEST(Input, key_queries_and_ascii_conversion)
{
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_A;
    ASSERT_TRUE(query_key_event(SDLK_A, e)) << "query_key_event should match keydown sym";
    ASSERT_TRUE(!query_key_event(SDLK_B, e)) << "query_key_event should not match other keys";

    ASSERT_TRUE(isAnyPlayerKey(SDLK_W)) << "isAnyPlayerKey should find player 0 default move key";
    ASSERT_TRUE(isPlayerKey(0, SDLK_W)) << "isPlayerKey should be true for player 0 move key";
    ASSERT_TRUE(!isPlayerKey(1, SDLK_W)) << "isPlayerKey should be false for other players' keys";

    ASSERT_EQ('A', (int)convert_to_ascii(SDLK_A)) << "convert_to_ascii(SDLK_A) should return 'A'";
    ASSERT_EQ('Z', (int)convert_to_ascii(SDLK_Z)) << "convert_to_ascii(SDLK_Z) should return 'Z'";
    ASSERT_EQ('0', (int)convert_to_ascii(SDLK_0)) << "convert_to_ascii(SDLK_0) should return '0'";
    ASSERT_EQ(255, (int)convert_to_ascii(SDLK_UNKNOWN)) << "convert_to_ascii(unknown) should return 255 sentinel";

    for (int i = 0; i < 26; ++i)
    {
        const int key = SDLK_A + i;
        const int expected = 'A' + i;
        ASSERT_EQ(expected, (int)convert_to_ascii(key)) << "alphabet key should map to uppercase ASCII";
    }

    ASSERT_EQ('1', (int)convert_to_ascii(SDLK_1)) << "digit key 1";
    ASSERT_EQ('2', (int)convert_to_ascii(SDLK_2)) << "digit key 2";
    ASSERT_EQ('3', (int)convert_to_ascii(SDLK_3)) << "digit key 3";
    ASSERT_EQ('4', (int)convert_to_ascii(SDLK_4)) << "digit key 4";
    ASSERT_EQ('5', (int)convert_to_ascii(SDLK_5)) << "digit key 5";
    ASSERT_EQ('6', (int)convert_to_ascii(SDLK_6)) << "digit key 6";
    ASSERT_EQ('7', (int)convert_to_ascii(SDLK_7)) << "digit key 7";
    ASSERT_EQ('8', (int)convert_to_ascii(SDLK_8)) << "digit key 8";
    ASSERT_EQ('9', (int)convert_to_ascii(SDLK_9)) << "digit key 9";
    ASSERT_EQ('0', (int)convert_to_ascii(SDLK_0)) << "digit key 0";

    ASSERT_EQ(32, (int)convert_to_ascii(SDLK_SPACE)) << "space";
    ASSERT_EQ(13, (int)convert_to_ascii(SDLK_RETURN)) << "return";
    ASSERT_EQ(27, (int)convert_to_ascii(SDLK_ESCAPE)) << "escape";
    ASSERT_EQ('.', (int)convert_to_ascii(SDLK_PERIOD)) << "period";
    ASSERT_EQ(',', (int)convert_to_ascii(SDLK_COMMA)) << "comma";
    ASSERT_EQ('\'', (int)convert_to_ascii(SDLK_APOSTROPHE)) << "quote";
    ASSERT_EQ('`', (int)convert_to_ascii(SDLK_GRAVE)) << "backquote";
}

