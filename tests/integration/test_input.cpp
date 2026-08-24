#include <cstring>
#include <utility>

#include <openglad/gameplay/input_state.h>
#include <openglad/interface/device_seats.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/sai2x.h>
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
    e.text.text = "abc";

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

TEST(Input, gameplay_ui_pointer_mapping_tracks_the_active_canvas_contract)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, s);
    const CanvasTarget saved_target = s->active_canvas();
    struct CanvasGuard
    {
        screen* value;
        CanvasTarget target;
        ~CanvasGuard() { value->set_active_canvas(target); }
    } guard{s, saved_target};

    s->set_active_canvas(CanvasTarget::UI);
    const og::CanvasViewport ui_viewport = gameplay_ui_canvas_viewport();
    EXPECT_EQ(active_canvas_viewport().x, ui_viewport.x);
    EXPECT_EQ(active_canvas_viewport().y, ui_viewport.y);
    EXPECT_EQ(active_canvas_viewport().w, ui_viewport.w);
    EXPECT_EQ(active_canvas_viewport().h, ui_viewport.h);

    const auto window_center = ui_canvas_to_window(160.0f, 100.0f);
    const auto ui_center = window_to_gameplay_ui_canvas(
        window_center.first, window_center.second);
    EXPECT_NEAR(160.0f, ui_center.first, 0.6f);
    EXPECT_NEAR(100.0f, ui_center.second, 0.6f);
    EXPECT_TRUE(window_point_in_gameplay_ui_canvas(
        window_center.first, window_center.second));

    // During a gameplay frame the HUD uses its independently fitted overlay
    // canvas even though world rendering is active.
    s->set_active_canvas(CanvasTarget::World);
    const og::CanvasViewport overlay_viewport = gameplay_ui_canvas_viewport();
    EXPECT_GT(overlay_viewport.w, 0);
    EXPECT_GT(overlay_viewport.h, 0);
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
    int saved_physical_w = 0;
    int saved_physical_h = 0;
    SDL_GetWindowSize(E_Screen->window, &saved_physical_w, &saved_physical_h);

    ASSERT_TRUE(SDL_SetWindowSize(E_Screen->window, 1000, 800));
    ASSERT_TRUE(SDL_SyncWindow(E_Screen->window));

    og::runtime::current_session->overscan_percentage_ = -1.0f;
    // Trigger update via resize event (calls update_overscan_setting internally).
    SDL_Event e{};
    e.type = SDL_EVENT_WINDOW_RESIZED;
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
    (void)SDL_SetWindowSize(E_Screen->window,
                            saved_physical_w, saved_physical_h);
    (void)SDL_SyncWindow(E_Screen->window);
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
        const int key = static_cast<int>(SDLK_A + static_cast<Uint32>(i));
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


TEST(Input, touch_control_layout_preserves_classic_geometry)
{
    const TouchControlLayout layout = touch_control_layout(320, 200);

    EXPECT_EQ(320, layout.canvas_w);
    EXPECT_EQ(200, layout.canvas_h);
    EXPECT_EQ(245, layout.fire.x);
    EXPECT_EQ(165, layout.fire.y);
    EXPECT_EQ(30, layout.fire.w);
    EXPECT_EQ(30, layout.fire.h);
    EXPECT_EQ(285, layout.special.x);
    EXPECT_EQ(165, layout.special.y);
    EXPECT_EQ(245, layout.next_special.x);
    EXPECT_EQ(125, layout.next_special.y);
    EXPECT_EQ(285, layout.alternate_special.x);
    EXPECT_EQ(125, layout.alternate_special.y);
    EXPECT_EQ(145, layout.yell.x);
    EXPECT_EQ(85, layout.yell.y);
    EXPECT_EQ(0, layout.switch_character.x);
    EXPECT_EQ(0, layout.switch_character.y);
    EXPECT_EQ(60, layout.switch_character.w);
    EXPECT_EQ(60, layout.switch_character.h);
    EXPECT_EQ(145, layout.movement_region_right);
    EXPECT_EQ(60, layout.movement_region_top);
    EXPECT_EQ(31, layout.movement_center_min_x);
    EXPECT_EQ(31, layout.movement_center_min_y);
    EXPECT_EQ(169, layout.movement_center_max_y);
    EXPECT_EQ(60, layout.movement_area_w);
    EXPECT_EQ(60, layout.movement_area_h);

    EXPECT_TRUE(layout.fire.contains(245, 165));
    EXPECT_TRUE(layout.fire.contains(275, 195));
    EXPECT_FALSE(layout.fire.contains(244, 165));
    EXPECT_FALSE(layout.fire.contains(276, 195));
}


TEST(Input, touch_control_layout_scales_to_deep_zoom_canvas_edges)
{
    const TouchControlLayout layout = touch_control_layout(3200, 2000);

    EXPECT_EQ(2450, layout.fire.x);
    EXPECT_EQ(1650, layout.fire.y);
    EXPECT_EQ(300, layout.fire.w);
    EXPECT_EQ(300, layout.fire.h);
    EXPECT_EQ(2850, layout.special.x);
    EXPECT_EQ(1650, layout.special.y);
    EXPECT_EQ(2450, layout.next_special.x);
    EXPECT_EQ(1250, layout.next_special.y);
    EXPECT_EQ(2850, layout.alternate_special.x);
    EXPECT_EQ(1250, layout.alternate_special.y);
    EXPECT_EQ(1450, layout.yell.x);
    EXPECT_EQ(850, layout.yell.y);
    EXPECT_EQ(600, layout.switch_character.w);
    EXPECT_EQ(600, layout.switch_character.h);
    EXPECT_EQ(1450, layout.movement_region_right);
    EXPECT_EQ(600, layout.movement_region_top);
    EXPECT_EQ(310, layout.movement_center_min_x);
    EXPECT_EQ(310, layout.movement_center_min_y);
    EXPECT_EQ(1690, layout.movement_center_max_y);
    EXPECT_EQ(100, layout.movement_dead_zone_x);
    EXPECT_EQ(100, layout.movement_dead_zone_y);
    EXPECT_EQ(600, layout.movement_area_w);
    EXPECT_EQ(600, layout.movement_area_h);

    // Edge margins and hit target sizes scale with the canvas, keeping the
    // controls at the same visible locations after presentation.
    EXPECT_EQ(50, layout.canvas_w -
                      (layout.special.x + layout.special.w));
    EXPECT_EQ(50, layout.canvas_h -
                      (layout.special.y + layout.special.h));
    EXPECT_TRUE(layout.special.contains(3150, 1950));
    EXPECT_FALSE(layout.special.contains(3151, 1950));
}


TEST(Input, touch_control_layout_stays_bounded_at_fractional_zoom_sizes)
{
    const std::pair<int, int> canvases[] = {
        {356, 222}, {400, 250}, {460, 285}, {536, 333},
        {640, 400}, {800, 500}, {1068, 666}, {1600, 1000},
    };
    for (const auto& [canvas_w, canvas_h] : canvases)
    {
        const TouchControlLayout layout = touch_control_layout(canvas_w, canvas_h);
        const TouchControlRect controls[] = {
            layout.fire, layout.special, layout.yell,
            layout.switch_character, layout.next_special,
            layout.alternate_special,
        };
        for (const TouchControlRect& control : controls)
        {
            EXPECT_GE(control.x, 0) << canvas_w << 'x' << canvas_h;
            EXPECT_GE(control.y, 0) << canvas_w << 'x' << canvas_h;
            EXPECT_LE(control.x + control.w, canvas_w)
                << canvas_w << 'x' << canvas_h;
            EXPECT_LE(control.y + control.h, canvas_h)
                << canvas_w << 'x' << canvas_h;
        }
        EXPECT_GT(layout.movement_center_min_x, 0);
        EXPECT_GT(layout.movement_center_min_y, 0);
        EXPECT_LT(layout.movement_center_max_y, canvas_h);
        // Movement uses x < right while the yell target includes its left
        // edge, so equality is the intended non-overlapping boundary.
        EXPECT_LE(layout.movement_region_right, layout.yell.x);
    }
}

// #249: the seat cap is one rule with one live home. The setter is the web
// shell's seam (it can fire before the session exists, so it latches); every
// reader goes through the session's hardware block.
TEST(Input, single_seat_device_flag_drives_the_local_seat_cap)
{
    const bool saved_device_class =
        input_hardware_state().single_seat_device;

    og::input::set_single_seat_device(false);
    EXPECT_FALSE(og::input::is_single_seat_device());
    EXPECT_EQ(MAX_PLAYERS, og::input::local_seat_cap());

    og::input::set_single_seat_device(true);
    EXPECT_TRUE(og::input::is_single_seat_device());
    // The cap grows with attached devices, so this holds for whatever the
    // test host has plugged in.
    EXPECT_EQ(og::input::max_local_seats(true, joystick_device_count(),
                                         MAX_PLAYERS),
              og::input::local_seat_cap());
    EXPECT_LE(og::input::local_seat_cap(), MAX_PLAYERS);
    EXPECT_GE(og::input::local_seat_cap(), 1);

    // The hardware block is the source of truth the doors read.
    input_hardware_state().single_seat_device = false;
    EXPECT_FALSE(og::input::is_single_seat_device());
    EXPECT_EQ(MAX_PLAYERS, og::input::local_seat_cap());

    og::input::set_single_seat_device(saved_device_class);
    input_hardware_state().single_seat_device = saved_device_class;
}

TEST(Input, single_seat_device_renames_the_input_cycler_label)
{
    const bool saved_device_class =
        input_hardware_state().single_seat_device;
    const int saved_joystick = player_joystick_device(0);

    // The INPUT row and the seat card must agree on the owner token: both
    // compose it through seat_owner_is_screen, so a phone's keyboard-mapped
    // seat reads SCRN in both places, and a pad-driven seat keeps its pad.
    input_hardware_state().single_seat_device = false;
    if (saved_joystick >= 0)
        clear_player_joystick(0);
    const std::string desktop_label = og::ui::input_cycle_button_label(0);
    EXPECT_EQ(0u, desktop_label.find("INPUT: "))
        << "cycler label shape changed: " << desktop_label;
    EXPECT_EQ(std::string::npos, desktop_label.find("SCRN"))
        << "desktop must name the mapping, not the screen";

    input_hardware_state().single_seat_device = true;
    EXPECT_EQ("INPUT: SCRN", og::ui::input_cycle_button_label(0));

    input_hardware_state().single_seat_device = saved_device_class;
    if (saved_joystick >= 0)
        assign_joystick_to_player(0, saved_joystick);
}
