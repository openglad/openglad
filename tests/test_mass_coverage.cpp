#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/render/obmap_debug_draw.h>
#include <openglad/entities/obmap.h>
#include <openglad/entities/walker.h>
#include <openglad/input/button.h>
#include <openglad/io/yaml_stream.h>
#include <openglad/legacy/colors.h>
#include <openglad/data/gparser.h>

#include "test_framework.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern screen* myscreen;

Sint32 yes_or_no(Sint32 arg);
void toggle_effect(const std::string& category, const std::string& setting);
void toggle_rendering_engine();
walker* find_follow_leader();

namespace {

void reset_level_state()
{
    myscreen->level_data.delete_objects();
    myscreen->level_data.level_done = 0;
}

walker* add_living(unsigned char team = 0, unsigned char family = FAMILY_SOLDIER)
{
    walker* w = myscreen->level_data.add_ob(Order::Living, family);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    w->setxy(100, 100);
    return w;
}

walker* add_weapon(unsigned char family = FAMILY_KNIFE, unsigned char team = 1)
{
    walker* w = myscreen->level_data.add_weap_ob(Order::Weapon, family);
    if (!w)
        return nullptr;
    w->team_num = team;
    w->real_team_num = team;
    w->dead = 0;
    w->setxy(105, 100);
    return w;
}

std::array<unsigned char, 64> sample_pixels(unsigned char base = 32)
{
    std::array<unsigned char, 64> p{};
    for (size_t i = 0; i < p.size(); i++)
        p[i] = static_cast<unsigned char>(base + (i % 8));
    return p;
}

struct ReadBuf {
    const unsigned char* data;
    size_t len;
    size_t pos;
};

int read_handler(void* data, unsigned char* buffer, std::size_t size, std::size_t* size_read)
{
    auto* rb = static_cast<ReadBuf*>(data);
    const size_t rem = (rb->pos < rb->len) ? (rb->len - rb->pos) : 0;
    const size_t n = (size < rem) ? size : rem;
    if (n > 0)
        std::memcpy(buffer, rb->data + rb->pos, n);
    rb->pos += n;
    *size_read = n;
    return 1;
}

int write_handler(void* data, unsigned char* buffer, std::size_t size)
{
    auto* out = static_cast<std::vector<unsigned char>*>(data);
    out->insert(out->end(), buffer, buffer + size);
    return 1;
}

} // namespace

#define MASS_TEST(name, ...) \
    void name() { __VA_ARGS__ } \
    REGISTER_TEST(name)

// MenuNav (button.cpp uncovered)
MASS_TEST(test_mass_menunav_up, { (void)MenuNav::Up(1); });
MASS_TEST(test_mass_menunav_down, { (void)MenuNav::Down(2); });
MASS_TEST(test_mass_menunav_left, { (void)MenuNav::Left(3); });
MASS_TEST(test_mass_menunav_right, { (void)MenuNav::Right(4); });
MASS_TEST(test_mass_menunav_updown, { (void)MenuNav::UpDown(1, 2); });
MASS_TEST(test_mass_menunav_upleft, { (void)MenuNav::UpLeft(1, 2); });
MASS_TEST(test_mass_menunav_upright, { (void)MenuNav::UpRight(1, 2); });
MASS_TEST(test_mass_menunav_updownleft, { (void)MenuNav::UpDownLeft(1, 2, 3); });
MASS_TEST(test_mass_menunav_updownright, { (void)MenuNav::UpDownRight(1, 2, 3); });
MASS_TEST(test_mass_menunav_upleftright, { (void)MenuNav::UpLeftRight(1, 2, 3); });
MASS_TEST(test_mass_menunav_downleft, { (void)MenuNav::DownLeft(1, 2); });
MASS_TEST(test_mass_menunav_downright, { (void)MenuNav::DownRight(1, 2); });
MASS_TEST(test_mass_menunav_downleftright, { (void)MenuNav::DownLeftRight(1, 2, 3); });
MASS_TEST(test_mass_menunav_leftright, { (void)MenuNav::LeftRight(1, 2); });
MASS_TEST(test_mass_menunav_updownleftright, { (void)MenuNav::UpDownLeftRight(1, 2, 3, 4); });
MASS_TEST(test_mass_menunav_none, { (void)MenuNav::None(); });

// vbutton + button helpers (button.cpp uncovered)
MASS_TEST(test_mass_vbutton_ctor_callback, {
    vbutton b(1, 1, 20, 10, [](Sint32 v) { return v + 1; }, 7, "cb", KEYSTATE_UNKNOWN);
    TEST_ASSERT_EQ(7, b.arg, "callback ctor should set arg");
});

MASS_TEST(test_mass_vbutton_ctor_func_code, {
    vbutton b(2, 2, 20, 10, 999, 5, "fn", KEYSTATE_UNKNOWN);
    TEST_ASSERT_EQ(999, b.myfunc, "func ctor should set myfunc");
});

MASS_TEST(test_mass_vbutton_ctor_family, {
    vbutton b(3, 3, 20, 10, 1, 0, "gfx", 0, KEYSTATE_UNKNOWN);
    TEST_ASSERT(b.mypixie != nullptr, "family ctor should allocate pixie");
});

MASS_TEST(test_mass_vbutton_set_graphic, {
    vbutton b(3, 3, 20, 10, 1, 0, "gfx2", KEYSTATE_UNKNOWN);
    b.set_graphic(0);
    TEST_ASSERT(b.mypixie != nullptr, "set_graphic should allocate pixie");
});

MASS_TEST(test_mass_vbutton_rightclick_buttons, {
    vbutton b;
    TEST_ASSERT_EQ(0, b.rightclick(static_cast<button*>(nullptr)), "rightclick(button*) empty should return 0");
});

MASS_TEST(test_mass_vbutton_rightclick_single, {
    vbutton b(10, 10, 20, 10, 0, 0, "r", KEYSTATE_UNKNOWN);
    TEST_ASSERT_EQ(-1, b.rightclick(1), "rightclick(single) no mouse should miss");
});

MASS_TEST(test_mass_vbutton_do_call_right, {
    vbutton b;
    TEST_ASSERT_EQ(4, b.do_call_right(-999, 0), "unknown right call should return OK");
});

MASS_TEST(test_mass_yes_or_no, {
    TEST_ASSERT_EQ(123, yes_or_no(123), "yes_or_no passthrough");
});

MASS_TEST(test_mass_toggle_effect, {
    cfg.apply_setting("effects", "mass_toggle_effect", "off");
    toggle_effect("effects", "mass_toggle_effect");
});

MASS_TEST(test_mass_toggle_rendering_engine, {
    cfg.apply_setting("graphics", "render", "normal");
    toggle_rendering_engine();
});

// screen.cpp uncovered wrappers/branches
MASS_TEST(test_mass_screen_ready_for_battle, { myscreen->ready_for_battle(1); });
MASS_TEST(test_mass_screen_reset, { myscreen->reset(1); });
MASS_TEST(test_mass_screen_query_grid_passable, {
    reset_level_state();
    walker* w = add_living(0);
    (void)myscreen->query_grid_passable(100, 100, w);
    reset_level_state();
});
MASS_TEST(test_mass_screen_query_object_passable, {
    reset_level_state();
    walker* w = add_living(0);
    (void)myscreen->query_object_passable(100, 100, w);
    reset_level_state();
});
MASS_TEST(test_mass_screen_clear, { myscreen->clear(); });
MASS_TEST(test_mass_screen_redraw, { (void)myscreen->redraw(); });
MASS_TEST(test_mass_screen_refresh, { myscreen->refresh(); });

MASS_TEST(test_mass_screen_endgame_one_arg, {
    const char saved_end = myscreen->end;
    myscreen->end = 1;
    (void)myscreen->endgame(0);
    myscreen->end = saved_end;
});

MASS_TEST(test_mass_screen_endgame_two_args, {
    const char saved_end = myscreen->end;
    myscreen->end = 1;
    (void)myscreen->endgame(0, -1);
    myscreen->end = saved_end;
});

MASS_TEST(test_mass_screen_find_near_foe, {
    reset_level_state();
    walker* w = add_living(0);
    (void)myscreen->find_near_foe(w);
    reset_level_state();
});

MASS_TEST(test_mass_screen_find_far_foe, {
    reset_level_state();
    walker* w = add_living(0);
    (void)myscreen->find_far_foe(w);
    reset_level_state();
});

MASS_TEST(test_mass_screen_set_scen_title_with_error, {
    std::string out;
    (void)myscreen->get_scen_title_with_error(nullptr, out);
});

MASS_TEST(test_mass_screen_get_scen_title, {
    (void)myscreen->get_scen_title("missing_mass_scen", myscreen);
});

MASS_TEST(test_mass_screen_first_of, {
    reset_level_state();
    (void)add_living(0);
    (void)myscreen->first_of(Order::Living, FAMILY_SOLDIER, 0);
    reset_level_state();
});

MASS_TEST(test_mass_screen_draw_panels, { myscreen->draw_panels(1); });
MASS_TEST(test_mass_screen_find_nearest_blood, { (void)myscreen->find_nearest_blood(nullptr); });

MASS_TEST(test_mass_screen_find_in_range, {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)myscreen->find_in_range(myscreen->level_data.oblist, 32, &n, w);
    reset_level_state();
});

MASS_TEST(test_mass_screen_find_nearest_player, {
    reset_level_state();
    walker* w = add_living(0);
    (void)myscreen->find_nearest_player(w);
    reset_level_state();
});

MASS_TEST(test_mass_screen_find_foes_in_range, {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)add_living(1);
    (void)myscreen->find_foes_in_range(myscreen->level_data.oblist, 64, &n, w);
    reset_level_state();
});

MASS_TEST(test_mass_screen_find_friends_in_range, {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)add_living(0);
    (void)myscreen->find_friends_in_range(myscreen->level_data.oblist, 64, &n, w);
    reset_level_state();
});

MASS_TEST(test_mass_screen_find_foe_weapons_in_range, {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)add_weapon(FAMILY_KNIFE, 1);
    (void)myscreen->find_foe_weapons_in_range(myscreen->level_data.weaplist, 64, &n, w);
    reset_level_state();
});

MASS_TEST(test_mass_screen_damage_tile, { (void)myscreen->damage_tile(0, 0); });
MASS_TEST(test_mass_screen_do_notify, { myscreen->do_notify("mass-notify", nullptr); });
MASS_TEST(test_mass_screen_report_mem, { myscreen->report_mem(); });
MASS_TEST(test_mass_find_follow_leader, { (void)find_follow_leader(); });

// viewscreen.cpp uncovered
MASS_TEST(test_mass_viewscreen_clear, { myscreen->viewob[0]->clear(); });
MASS_TEST(test_mass_viewscreen_view_team_default, { myscreen->viewob[0]->view_team(); });
MASS_TEST(test_mass_viewscreen_view_team_bounds, { myscreen->viewob[0]->view_team(30, 30, 280, 170); });

// video.cpp uncovered
MASS_TEST(test_mass_video_set_fullscreen, { myscreen->set_fullscreen(false); });
MASS_TEST(test_mass_video_getbuffer, { (void)myscreen->getbuffer(); });
MASS_TEST(test_mass_video_clearbuffer_rect, { myscreen->clearbuffer(1, 1, 20, 20); });
MASS_TEST(test_mass_video_clear_window, { myscreen->clear_window(); });
MASS_TEST(test_mass_video_draw_rect_filled, { myscreen->draw_rect_filled(10, 10, 20, 10, WHITE, 120); });
MASS_TEST(test_mass_video_draw_button_rect, { SDL_Rect r{20, 20, 40, 20}; myscreen->draw_button(r, 1); });
MASS_TEST(test_mass_video_draw_button_inverted_rect, { SDL_Rect r{20, 50, 40, 20}; myscreen->draw_button_inverted(r); });
MASS_TEST(test_mass_video_putblack, { myscreen->putblack(0, 0, 0, 0); });
MASS_TEST(test_mass_video_fastbox_outline, { myscreen->fastbox_outline(2, 2, 8, 8, DARK_GREEN); });
MASS_TEST(test_mass_video_point, { myscreen->point(5, 5, RED); });
MASS_TEST(test_mass_video_pointb_offset, { myscreen->pointb(320 + 2, DARK_BLUE); });
MASS_TEST(test_mass_video_hor_line_tobuffer, { myscreen->hor_line(2, 8, 12, WHITE, 1); });
MASS_TEST(test_mass_video_hor_line_alpha, { myscreen->hor_line_alpha(2, 9, 12, WHITE, 96); });
MASS_TEST(test_mass_video_ver_line_tobuffer, { myscreen->ver_line(2, 8, 12, WHITE, 1); });
MASS_TEST(test_mass_video_do_cycle, { myscreen->do_cycle(0, 1); });

MASS_TEST(test_mass_video_putdata, {
    auto px = sample_pixels(50);
    myscreen->putdata(10, 10, 8, 8, px);
});

MASS_TEST(test_mass_video_putdata_alpha, {
    auto px = sample_pixels(60);
    myscreen->putdata_alpha(10, 10, 8, 8, px, 100);
});

MASS_TEST(test_mass_video_putdatatext, {
    auto px = sample_pixels(70);
    myscreen->putdatatext(10, 10, 8, 8, px);
});

MASS_TEST(test_mass_video_putdata_color, {
    auto px = sample_pixels(248);
    myscreen->putdata(10, 10, 8, 8, px, DARK_GREEN);
});

MASS_TEST(test_mass_video_putdatatext_color, {
    auto px = sample_pixels(248);
    myscreen->putdatatext(10, 10, 8, 8, px, DARK_BLUE);
});

MASS_TEST(test_mass_video_putbuffer_span, {
    auto px = sample_pixels(40);
    myscreen->putbuffer(5, 5, 8, 8, 0, 0, 320, 200, px);
});

MASS_TEST(test_mass_video_putbuffer_alpha, {
    auto px = sample_pixels(45);
    myscreen->putbuffer_alpha(5, 5, 8, 8, 0, 0, 320, 200, px, 90);
});

MASS_TEST(test_mass_video_putbuffer_surface, {
    SDL_Surface* surf = SDL_CreateRGBSurface(SDL_SWSURFACE, 8, 8, 32, 0, 0, 0, 0);
    TEST_ASSERT(surf != nullptr, "surface alloc");
    myscreen->putbuffer(5, 5, 8, 8, 0, 0, 320, 200, surf);
    SDL_FreeSurface(surf);
});

MASS_TEST(test_mass_video_walkputbuffer, {
    auto px = sample_pixels(30);
    myscreen->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
});

MASS_TEST(test_mass_video_walkputbuffer_flash, {
    auto px = sample_pixels(30);
    myscreen->walkputbuffer_flash(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
});

MASS_TEST(test_mass_video_walkputbuffertext, {
    auto px = sample_pixels(30);
    myscreen->walkputbuffertext(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
});

MASS_TEST(test_mass_video_walkputbuffertext_alpha, {
    auto px = sample_pixels(30);
    myscreen->walkputbuffertext_alpha(10, 10, 8, 8, 0, 0, 320, 200, px, 8, 80);
});

MASS_TEST(test_mass_video_walkputbuffer_mode, {
    auto px = sample_pixels(30);
    myscreen->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, px, 8, OUTLINE_MODE, 12, RED, SHIFT_LEFT);
});

MASS_TEST(test_mass_video_swap, { myscreen->swap(); });

MASS_TEST(test_mass_video_get_pixel_rgb, {
    Uint8 r = 0, g = 0, b = 0;
    myscreen->get_pixel(1, 1, &r, &g, &b);
});

MASS_TEST(test_mass_video_get_pixel_index_xy, {
    int idx = 0;
    (void)myscreen->get_pixel(1, 1, &idx);
});

MASS_TEST(test_mass_video_get_pixel_offset, { (void)myscreen->get_pixel(321); });
MASS_TEST(test_mass_video_save_screenshot, { (void)myscreen->save_screenshot(); });

MASS_TEST(test_mass_video_fade_between24, {
    SDL_Surface* s = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    TEST_ASSERT(s != nullptr, "surface alloc");
    std::array<Uint8, 4 * 4 * 4> from{};
    std::array<Uint8, 4 * 4 * 4> to{};
    myscreen->FadeBetween24(s, from.data(), to.data(), 10);
    SDL_FreeSurface(s);
});

MASS_TEST(test_mass_video_fade_between, {
    SDL_Surface* a = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    SDL_Surface* b = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    SDL_Surface* d = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    TEST_ASSERT(a && b && d, "surfaces alloc");
    (void)myscreen->FadeBetween(a, b, d);
    SDL_FreeSurface(a);
    SDL_FreeSurface(b);
    SDL_FreeSurface(d);
});

MASS_TEST(test_mass_video_fadeblack, { (void)myscreen->fadeblack(true); });
MASS_TEST(test_mass_video_darken_screen, { myscreen->darken_screen(); });

// text.cpp uncovered
MASS_TEST(test_mass_text_shutdown, { text_shutdown(); });
MASS_TEST(test_mass_text_write_xy_color, { myscreen->text_normal.write_xy(5, 5, "mass", WHITE); });
MASS_TEST(test_mass_text_write_xy_printf, { myscreen->text_normal.write_xy(5, 12, WHITE, "%s", "fmt"); });
MASS_TEST(test_mass_text_write_xy_shadow, { myscreen->text_normal.write_xy_shadow(5, 20, WHITE, "%s", "shadow"); });
MASS_TEST(test_mass_text_write_xy_center, { myscreen->text_normal.write_xy_center(80, 40, WHITE, "%s", "center"); });
MASS_TEST(test_mass_text_write_xy_center_alpha, { myscreen->text_normal.write_xy_center_alpha(80, 46, WHITE, 100, "%s", "alpha"); });
MASS_TEST(test_mass_text_write_xy_center_shadow, { myscreen->text_normal.write_xy_center_shadow(80, 52, WHITE, "%s", "center-shadow"); });
MASS_TEST(test_mass_text_write_xy_default, { myscreen->text_normal.write_xy(5, 58, "default"); });
MASS_TEST(test_mass_text_write_xy_tobuffer, { myscreen->text_normal.write_xy(5, 64, "buf", static_cast<short>(1)); });
MASS_TEST(test_mass_text_write_xy_view, { myscreen->text_normal.write_xy(5, 70, "view", myscreen->viewob[0].get()); });
MASS_TEST(test_mass_text_write_y_color, { myscreen->text_normal.write_y(76, "yc", WHITE); });
MASS_TEST(test_mass_text_write_y_default, { myscreen->text_normal.write_y(82, "yd"); });
MASS_TEST(test_mass_text_write_y_tobuffer, { myscreen->text_normal.write_y(88, "yb", static_cast<short>(1)); });
MASS_TEST(test_mass_text_write_y_view_color, { myscreen->text_normal.write_y(94, "yvc", WHITE, myscreen->viewob[0].get()); });
MASS_TEST(test_mass_text_write_y_view, { myscreen->text_normal.write_y(100, "yv", myscreen->viewob[0].get()); });
MASS_TEST(test_mass_text_write_char_xy_tobuffer, { myscreen->text_normal.write_char_xy(5, 106, 'Q', static_cast<short>(1)); });
MASS_TEST(test_mass_text_write_char_xy_default, { myscreen->text_normal.write_char_xy(15, 106, 'R'); });
MASS_TEST(test_mass_text_write_char_xy_view, { myscreen->text_normal.write_char_xy(25, 106, 'S', myscreen->viewob[0].get()); });

// yaml_stream.cpp uncovered
MASS_TEST(test_mass_yaml_parser_move_ctor, {
    og::io::YamlParser p;
    og::io::YamlParser p2(std::move(p));
    (void)p2;
});

MASS_TEST(test_mass_yaml_parser_move_assign, {
    og::io::YamlParser a;
    og::io::YamlParser b;
    b = std::move(a);
});

MASS_TEST(test_mass_yaml_emitter_move_ctor, {
    og::io::YamlEmitter e;
    og::io::YamlEmitter e2(std::move(e));
    (void)e2;
});

MASS_TEST(test_mass_yaml_emitter_move_assign, {
    og::io::YamlEmitter a;
    og::io::YamlEmitter b;
    b = std::move(a);
});

MASS_TEST(test_mass_yaml_parser_set_input, {
    og::io::YamlParser p;
    static const unsigned char data[] = "a: b\n";
    ReadBuf rb{data, sizeof(data) - 1, 0};
    p.set_input(read_handler, &rb);
});

MASS_TEST(test_mass_yaml_parser_close_input, {
    og::io::YamlParser p;
    static const unsigned char data[] = "x: y\n";
    ReadBuf rb{data, sizeof(data) - 1, 0};
    p.set_input(read_handler, &rb);
    p.close_input();
});

// obmap_debug_draw.cpp uncovered file target
MASS_TEST(test_mass_obmap_debug_draw, {
    reset_level_state();
    obmap map;
    walker* w = add_living(0);
    if (w)
        map.add(w, 100, 100);
    obmap_debug_draw(map, myscreen);
    reset_level_state();
});
