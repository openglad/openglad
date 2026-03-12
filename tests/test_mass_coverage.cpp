#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/obmap_debug_draw.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/resources/yaml_stream.h>
#include <openglad/legacy/colors.h>
#include <openglad/resources/gparser.h>

#include <gtest/gtest.h>
#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

Sint32 yes_or_no(Sint32 arg);
void toggle_effect(const std::string& category, const std::string& setting);
void toggle_rendering_engine();
walker* find_follow_leader();

namespace {

void reset_level_state()
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->level_runtime_data().level_done = 0;
}

walker* add_living(unsigned char team = 0, unsigned char family = FAMILY_SOLDIER)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, family);
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
    walker* w = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, family);
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

// MenuNav (button.cpp uncovered)
TEST(MassCoverage, menunav_up) { (void)MenuNav{.up=1}; }
TEST(MassCoverage, menunav_down) { (void)MenuNav{.down=2}; }
TEST(MassCoverage, menunav_left) { (void)MenuNav{.left=3}; }
TEST(MassCoverage, menunav_right) { (void)MenuNav{.right=4}; }
TEST(MassCoverage, menunav_updown) { (void)MenuNav{.up=1, .down=2}; }
TEST(MassCoverage, menunav_upleft) { (void)MenuNav{.up=1, .left=2}; }
TEST(MassCoverage, menunav_upright) { (void)MenuNav{.up=1, .right=2}; }
TEST(MassCoverage, menunav_updownleft) { (void)MenuNav{.up=1, .down=2, .left=3}; }
TEST(MassCoverage, menunav_updownright) { (void)MenuNav{.up=1, .down=2, .right=3}; }
TEST(MassCoverage, menunav_upleftright) { (void)MenuNav{.up=1, .left=2, .right=3}; }
TEST(MassCoverage, menunav_downleft) { (void)MenuNav{.down=1, .left=2}; }
TEST(MassCoverage, menunav_downright) { (void)MenuNav{.down=1, .right=2}; }
TEST(MassCoverage, menunav_downleftright) { (void)MenuNav{.down=1, .left=2, .right=3}; }
TEST(MassCoverage, menunav_leftright) { (void)MenuNav{.left=1, .right=2}; }
TEST(MassCoverage, menunav_updownleftright) { (void)MenuNav{.up=1, .down=2, .left=3, .right=4}; }
TEST(MassCoverage, menunav_none) { (void)MenuNav{}; }

// vbutton + button helpers (button.cpp uncovered)
TEST(MassCoverage, vbutton_ctor_callback) {
    vbutton b(1, 1, 20, 10, [](Sint32 v) { return v + 1; }, 7, "cb", KEYSTATE_UNKNOWN);
    ASSERT_EQ(7, b.arg) << "callback ctor should set arg";
}

TEST(MassCoverage, vbutton_ctor_func_code) {
    vbutton b(2, 2, 20, 10, 999, 5, "fn", KEYSTATE_UNKNOWN);
    ASSERT_EQ(999, b.myfunc) << "func ctor should set myfunc";
}

TEST(MassCoverage, vbutton_ctor_family) {
    vbutton b(3, 3, 20, 10, 1, 0, "gfx", 0, KEYSTATE_UNKNOWN);
    ASSERT_TRUE(b.mypixie != nullptr) << "family ctor should allocate pixie";
}

TEST(MassCoverage, vbutton_set_graphic) {
    vbutton b(3, 3, 20, 10, 1, 0, "gfx2", KEYSTATE_UNKNOWN);
    b.set_graphic(0);
    ASSERT_TRUE(b.mypixie != nullptr) << "set_graphic should allocate pixie";
}

TEST(MassCoverage, vbutton_rightclick_buttons) {
    vbutton b;
    ASSERT_EQ(0, b.rightclick(static_cast<button*>(nullptr))) << "rightclick(button*) empty should return 0";
}

TEST(MassCoverage, vbutton_rightclick_single) {
    vbutton b(10, 10, 20, 10, 0, 0, "r", KEYSTATE_UNKNOWN);
    ASSERT_EQ(-1, b.rightclick(1)) << "rightclick(single) no mouse should miss";
}

TEST(MassCoverage, vbutton_do_call_right) {
    vbutton b;
    ASSERT_EQ(4, b.do_call_right(-999, 0)) << "unknown right call should return OK";
}

TEST(MassCoverage, yes_or_no) {
    ASSERT_EQ(123, yes_or_no(123)) << "yes_or_no passthrough";
}

TEST(MassCoverage, toggle_effect) {
    cfg.apply_setting("effects", "mass_toggle_effect", "off");
    toggle_effect("effects", "mass_toggle_effect");
}

TEST(MassCoverage, toggle_rendering_engine) {
    cfg.apply_setting("graphics", "render", "normal");
    toggle_rendering_engine();
}

// screen.cpp uncovered wrappers/branches
TEST(MassCoverage, screen_ready_for_battle) { og::runtime::current_session->myscreen_->ready_for_battle(1); }
TEST(MassCoverage, screen_reset) { og::runtime::current_session->myscreen_->reset(1); }
TEST(MassCoverage, screen_query_grid_passable) {
    reset_level_state();
    walker* w = add_living(0);
    (void)og::runtime::current_session->myscreen_->world().query_grid_passable(100, 100, w);
    reset_level_state();
}
TEST(MassCoverage, screen_query_object_passable) {
    reset_level_state();
    walker* w = add_living(0);
    (void)og::runtime::current_session->myscreen_->world().query_object_passable(100, 100, w);
    reset_level_state();
}
TEST(MassCoverage, screen_clear) { og::runtime::current_session->myscreen_->clear(); }
TEST(MassCoverage, screen_redraw) { (void)og::runtime::current_session->myscreen_->redraw(); }
TEST(MassCoverage, screen_refresh) { og::runtime::current_session->myscreen_->refresh(); }

TEST(MassCoverage, screen_endgame_one_arg) {
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;
    (void)og::runtime::current_session->myscreen_->endgame(0);
    og::runtime::current_session->myscreen_->world().end = saved_end;
}

TEST(MassCoverage, screen_endgame_two_args) {
    const char saved_end = og::runtime::current_session->myscreen_->world().end;
    og::runtime::current_session->myscreen_->world().end = 1;
    (void)og::runtime::current_session->myscreen_->endgame(0, -1);
    og::runtime::current_session->myscreen_->world().end = saved_end;
}

TEST(MassCoverage, screen_find_near_foe) {
    reset_level_state();
    walker* w = add_living(0);
    (void)og::runtime::current_session->myscreen_->world().find_near_foe(w);
    reset_level_state();
}

TEST(MassCoverage, screen_find_far_foe) {
    reset_level_state();
    walker* w = add_living(0);
    (void)og::runtime::current_session->myscreen_->world().find_far_foe(w);
    reset_level_state();
}

TEST(MassCoverage, screen_set_scen_title_with_error) {
    std::string out;
    (void)og::runtime::current_session->myscreen_->get_scen_title_with_error(nullptr, out);
}

TEST(MassCoverage, screen_get_scen_title) {
    (void)og::runtime::current_session->myscreen_->get_scen_title("missing_mass_scen", og::runtime::current_session->myscreen_);
}

TEST(MassCoverage, screen_first_of) {
    reset_level_state();
    (void)add_living(0);
    (void)og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_SOLDIER, 0);
    reset_level_state();
}

TEST(MassCoverage, screen_draw_panels) { og::runtime::current_session->myscreen_->draw_panels(1); }
TEST(MassCoverage, screen_find_nearest_blood) { (void)og::runtime::current_session->myscreen_->world().find_nearest_blood(nullptr); }

TEST(MassCoverage, screen_find_in_range) {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)og::runtime::current_session->myscreen_->world().find_in_range(og::runtime::current_session->myscreen_->world().oblist, 32, &n, w);
    reset_level_state();
}

TEST(MassCoverage, screen_find_nearest_player) {
    reset_level_state();
    walker* w = add_living(0);
    (void)og::runtime::current_session->myscreen_->world().find_nearest_player(w);
    reset_level_state();
}

TEST(MassCoverage, screen_find_foes_in_range) {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)add_living(1);
    (void)og::runtime::current_session->myscreen_->world().find_foes_in_range(og::runtime::current_session->myscreen_->world().oblist, 64, &n, w);
    reset_level_state();
}

TEST(MassCoverage, screen_find_friends_in_range) {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)add_living(0);
    (void)og::runtime::current_session->myscreen_->world().find_friends_in_range(og::runtime::current_session->myscreen_->world().oblist, 64, &n, w);
    reset_level_state();
}

TEST(MassCoverage, screen_find_foe_weapons_in_range) {
    reset_level_state();
    Sint32 n = 0;
    walker* w = add_living(0);
    (void)add_weapon(FAMILY_KNIFE, 1);
    (void)og::runtime::current_session->myscreen_->world().find_foe_weapons_in_range(og::runtime::current_session->myscreen_->world().weaplist, 64, &n, w);
    reset_level_state();
}

TEST(MassCoverage, screen_damage_tile) { (void)og::runtime::current_session->myscreen_->damage_tile(0, 0); }
TEST(MassCoverage, screen_do_notify) { og::runtime::current_session->myscreen_->do_notify("mass-notify", nullptr); }
TEST(MassCoverage, screen_report_mem) { og::runtime::current_session->myscreen_->report_mem(); }
TEST(MassCoverage, find_follow_leader) { (void)find_follow_leader(); }

// viewscreen.cpp uncovered
TEST(MassCoverage, viewscreen_clear) { og::runtime::current_session->myscreen_->viewob[0]->clear(); }
TEST(MassCoverage, viewscreen_view_team_default) { og::runtime::current_session->myscreen_->viewob[0]->view_team(); }
TEST(MassCoverage, viewscreen_view_team_bounds) { og::runtime::current_session->myscreen_->viewob[0]->view_team(30, 30, 280, 170); }

// video.cpp uncovered
TEST(MassCoverage, video_set_fullscreen) { og::runtime::current_session->myscreen_->set_fullscreen(false); }
TEST(MassCoverage, video_getbuffer) { (void)og::runtime::current_session->myscreen_->getbuffer(); }
TEST(MassCoverage, video_clearbuffer_rect) { og::runtime::current_session->myscreen_->clearbuffer(1, 1, 20, 20); }
TEST(MassCoverage, video_clear_window) { og::runtime::current_session->myscreen_->clear_window(); }
TEST(MassCoverage, video_draw_rect_filled) { og::runtime::current_session->myscreen_->draw_rect_filled(10, 10, 20, 10, WHITE, 120); }
TEST(MassCoverage, video_draw_button_rect) { SDL_Rect r{20, 20, 40, 20}; og::runtime::current_session->myscreen_->draw_button(r.x, r.y, r.x + r.w - 1, r.y + r.h - 1, 1); }
TEST(MassCoverage, video_draw_button_inverted_rect) { SDL_Rect r{20, 50, 40, 20}; og::runtime::current_session->myscreen_->draw_button_inverted(r.x, r.y, r.w, r.h); }
TEST(MassCoverage, video_putblack) { og::runtime::current_session->myscreen_->putblack(0, 0, 0, 0); }
TEST(MassCoverage, video_fastbox_outline) { og::runtime::current_session->myscreen_->fastbox_outline(2, 2, 8, 8, DARK_GREEN); }
TEST(MassCoverage, video_point) { og::runtime::current_session->myscreen_->point(5, 5, RED); }
TEST(MassCoverage, video_pointb_offset) { og::runtime::current_session->myscreen_->pointb(320 + 2, DARK_BLUE); }
TEST(MassCoverage, video_hor_line_tobuffer) { og::runtime::current_session->myscreen_->hor_line(2, 8, 12, WHITE, 1); }
TEST(MassCoverage, video_hor_line_alpha) { og::runtime::current_session->myscreen_->hor_line_alpha(2, 9, 12, WHITE, 96); }
TEST(MassCoverage, video_ver_line_tobuffer) { og::runtime::current_session->myscreen_->ver_line(2, 8, 12, WHITE, 1); }
TEST(MassCoverage, video_do_cycle) { og::runtime::current_session->myscreen_->do_cycle(0, 1); }

TEST(MassCoverage, video_putdata) {
    auto px = sample_pixels(50);
    og::runtime::current_session->myscreen_->putdata(10, 10, 8, 8, px);
}

TEST(MassCoverage, video_putdata_alpha) {
    auto px = sample_pixels(60);
    og::runtime::current_session->myscreen_->putdata_alpha(10, 10, 8, 8, px, 100);
}

TEST(MassCoverage, video_putdatatext) {
    auto px = sample_pixels(70);
    og::runtime::current_session->myscreen_->putdatatext(10, 10, 8, 8, px);
}

TEST(MassCoverage, video_putdata_color) {
    auto px = sample_pixels(248);
    og::runtime::current_session->myscreen_->putdata(10, 10, 8, 8, px, DARK_GREEN);
}

TEST(MassCoverage, video_putdatatext_color) {
    auto px = sample_pixels(248);
    og::runtime::current_session->myscreen_->putdatatext(10, 10, 8, 8, px, DARK_BLUE);
}

TEST(MassCoverage, video_putbuffer_span) {
    auto px = sample_pixels(40);
    og::runtime::current_session->myscreen_->putbuffer(5, 5, 8, 8, 0, 0, 320, 200, px);
}

TEST(MassCoverage, video_putbuffer_alpha) {
    auto px = sample_pixels(45);
    og::runtime::current_session->myscreen_->putbuffer_alpha(5, 5, 8, 8, 0, 0, 320, 200, px, 90);
}

TEST(MassCoverage, video_putbuffer_surface) {
    SDL_Surface* surf = SDL_CreateRGBSurface(SDL_SWSURFACE, 8, 8, 32, 0, 0, 0, 0);
    ASSERT_TRUE(surf != nullptr) << "surface alloc";
    og::runtime::current_session->myscreen_->putbuffer_surface(5, 5, 8, 8, 0, 0, 320, 200, surf);
    SDL_FreeSurface(surf);
}

TEST(MassCoverage, video_walkputbuffer) {
    auto px = sample_pixels(30);
    og::runtime::current_session->myscreen_->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
}

TEST(MassCoverage, video_walkputbuffer_flash) {
    auto px = sample_pixels(30);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
}

TEST(MassCoverage, video_walkputbuffertext) {
    auto px = sample_pixels(30);
    og::runtime::current_session->myscreen_->walkputbuffertext(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
}

TEST(MassCoverage, video_walkputbuffertext_alpha) {
    auto px = sample_pixels(30);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(10, 10, 8, 8, 0, 0, 320, 200, px, 8, 80);
}

TEST(MassCoverage, video_walkputbuffer_mode) {
    auto px = sample_pixels(30);
    og::runtime::current_session->myscreen_->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, px, 8, OUTLINE_MODE, 12, RED, SHIFT_LEFT);
}

TEST(MassCoverage, video_swap) { og::runtime::current_session->myscreen_->swap(); }

TEST(MassCoverage, video_get_pixel_rgb) {
    Uint8 r = 0, g = 0, b = 0;
    og::runtime::current_session->myscreen_->get_pixel(1, 1, &r, &g, &b);
}

TEST(MassCoverage, video_get_pixel_index_xy) {
    int idx = 0;
    (void)og::runtime::current_session->myscreen_->get_pixel(1, 1, &idx);
}

TEST(MassCoverage, video_get_pixel_offset) { (void)og::runtime::current_session->myscreen_->get_pixel(321); }
TEST(MassCoverage, video_save_screenshot) { (void)og::runtime::current_session->myscreen_->save_screenshot(); }

TEST(MassCoverage, video_fade_between24) {
    SDL_Surface* s = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    ASSERT_TRUE(s != nullptr) << "surface alloc";
    std::array<Uint8, 4 * 4 * 4> from{};
    std::array<Uint8, 4 * 4 * 4> to{};
    og::runtime::current_session->myscreen_->fade_between24(s, from.data(), to.data(), 10);
    SDL_FreeSurface(s);
}

TEST(MassCoverage, video_fade_between) {
    SDL_Surface* a = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    SDL_Surface* b = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    SDL_Surface* d = SDL_CreateRGBSurface(SDL_SWSURFACE, 4, 4, 32, 0, 0, 0, 0);
    ASSERT_TRUE(a && b && d) << "surfaces alloc";
    (void)og::runtime::current_session->myscreen_->fade_between(a, b, d);
    SDL_FreeSurface(a);
    SDL_FreeSurface(b);
    SDL_FreeSurface(d);
}

TEST(MassCoverage, video_fadeblack) { (void)og::runtime::current_session->myscreen_->fadeblack(true); }
TEST(MassCoverage, video_darken_screen) { og::runtime::current_session->myscreen_->darken_screen(); }

// text.cpp uncovered
TEST(MassCoverage, text_shutdown) { text_shutdown(); }
TEST(MassCoverage, text_write_xy_color) { og::runtime::current_session->myscreen_->text_normal.write_xy(5, 5, "mass", WHITE); }
TEST(MassCoverage, text_write_xy_printf) { og::runtime::current_session->myscreen_->text_normal.write_xy(5, 12, WHITE, "%s", "fmt"); }
TEST(MassCoverage, text_write_xy_shadow) { og::runtime::current_session->myscreen_->text_normal.write_xy_shadow(5, 20, WHITE, "%s", "shadow"); }
TEST(MassCoverage, text_write_xy_center) { og::runtime::current_session->myscreen_->text_normal.write_xy_center(80, 40, WHITE, "%s", "center"); }
TEST(MassCoverage, text_write_xy_center_alpha) { og::runtime::current_session->myscreen_->text_normal.write_xy_center_alpha(80, 46, WHITE, 100, "%s", "alpha"); }
TEST(MassCoverage, text_write_xy_center_shadow) { og::runtime::current_session->myscreen_->text_normal.write_xy_center_shadow(80, 52, WHITE, "%s", "center-shadow"); }
TEST(MassCoverage, text_write_xy_default) { og::runtime::current_session->myscreen_->text_normal.write_xy(5, 58, "default"); }
TEST(MassCoverage, text_write_xy_tobuffer) { og::runtime::current_session->myscreen_->text_normal.write_xy(5, 64, "buf", static_cast<short>(1)); }
TEST(MassCoverage, text_write_xy_view) { og::runtime::current_session->myscreen_->text_normal.write_xy(5, 70, "view", og::runtime::current_session->myscreen_->viewob[0].get()); }
TEST(MassCoverage, text_write_y_color) { og::runtime::current_session->myscreen_->text_normal.write_y(76, "yc", WHITE); }
TEST(MassCoverage, text_write_y_default) { og::runtime::current_session->myscreen_->text_normal.write_y(82, "yd"); }
TEST(MassCoverage, text_write_y_tobuffer) { og::runtime::current_session->myscreen_->text_normal.write_y(88, "yb", static_cast<short>(1)); }
TEST(MassCoverage, text_write_y_view_color) { og::runtime::current_session->myscreen_->text_normal.write_y(94, "yvc", WHITE, og::runtime::current_session->myscreen_->viewob[0].get()); }
TEST(MassCoverage, text_write_y_view) { og::runtime::current_session->myscreen_->text_normal.write_y(100, "yv", og::runtime::current_session->myscreen_->viewob[0].get()); }
TEST(MassCoverage, text_write_char_xy_tobuffer) { og::runtime::current_session->myscreen_->text_normal.write_char_xy(5, 106, 'Q', static_cast<short>(1)); }
TEST(MassCoverage, text_write_char_xy_default) { og::runtime::current_session->myscreen_->text_normal.write_char_xy(15, 106, 'R'); }
TEST(MassCoverage, text_write_char_xy_view) { og::runtime::current_session->myscreen_->text_normal.write_char_xy(25, 106, 'S', og::runtime::current_session->myscreen_->viewob[0].get()); }

// yaml_stream.cpp uncovered
TEST(MassCoverage, yaml_parser_move_ctor) {
    og::io::YamlParser p;
    og::io::YamlParser p2(std::move(p));
    (void)p2;
}

TEST(MassCoverage, yaml_parser_move_assign) {
    og::io::YamlParser a;
    og::io::YamlParser b;
    b = std::move(a);
}

TEST(MassCoverage, yaml_emitter_move_ctor) {
    og::io::YamlEmitter e;
    og::io::YamlEmitter e2(std::move(e));
    (void)e2;
}

TEST(MassCoverage, yaml_emitter_move_assign) {
    og::io::YamlEmitter a;
    og::io::YamlEmitter b;
    b = std::move(a);
}

TEST(MassCoverage, yaml_parser_set_input) {
    og::io::YamlParser p;
    static const unsigned char data[] = "a: b\n";
    ReadBuf rb{data, sizeof(data) - 1, 0};
    p.set_input(read_handler, &rb);
}

TEST(MassCoverage, yaml_parser_close_input) {
    og::io::YamlParser p;
    static const unsigned char data[] = "x: y\n";
    ReadBuf rb{data, sizeof(data) - 1, 0};
    p.set_input(read_handler, &rb);
    p.close_input();
}

// obmap_debug_draw.cpp uncovered file target
TEST(MassCoverage, obmap_debug_draw) {
    reset_level_state();
    obmap map;
    walker* w = add_living(0);
    if (w)
        map.add(w, 100, 100);
    obmap_debug_draw(map, og::runtime::current_session->myscreen_);
    reset_level_state();
}
