#include <openglad/interface/screen.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/render/pixie.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/obmap_debug_draw.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/weap.h>
#include <openglad/interface/button.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/core/pixdefs.h>
#include <openglad/legacy/colors.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/core/test_trace.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <list>
#include <span>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

Sint32 yes_or_no(Sint32 arg);
void toggle_effect(const std::string& category, const std::string& setting);
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
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    w->setxy(100, 100);
    return w;
}

// A living with a 1x1 collision box parked at an exact spot. Size first, then
// setxy: the obmap pile a walker occupies is derived from its size at
// REGISTRATION time, and obmap::move re-registers only when the coordinates
// actually change -- so re-placing at (100,100) would keep the default-size
// registration and smear the walker across neighbouring cells.
walker* add_sized_living(unsigned char team, short x, short y,
                         unsigned char family = FAMILY_SOLDIER)
{
    walker* w = add_living(team, family);
    if (!w)
        return nullptr;
    w->set_sizex(1);
    w->set_sizey(1);
    w->setxy(x, y);
    return w;
}

// A 1x1 Treasure in fxlist (the list find_nearest_blood scans).
walker* add_fx_treasure(unsigned char family, short x, short y)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_fx_ob(
        Order::Treasure, family);
    if (!w)
        return nullptr;
    w->set_team_num(0);
    w->set_real_team_num(255);
    w->set_dead(0);
    w->set_sizex(1);
    w->set_sizey(1);
    w->setxy(x, y);
    return w;
}

walker* add_weapon(unsigned char family = FAMILY_KNIFE, unsigned char team = 1)
{
    walker* w = og::runtime::current_session->myscreen_->world().add_weap_ob(Order::Weapon, family);
    if (!w)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(team);
    w->set_dead(0);
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

// Read one palette index back off the active render surface.
int px_index(int x, int y)
{
    int idx = -1;
    og::runtime::current_session->myscreen_->get_pixel(x, y, &idx);
    return idx;
}

// get_pixel's index form walks the palette from register 0 and answers with the
// FIRST register whose RGB matches the pixel, so a colour whose RGB is duplicated
// lower down reads back as that lower index (ORANGE_END, for one, reads back as
// 88). Expected values therefore go through here rather than through the colour
// constant itself.
int pal_readback_index(unsigned char color)
{
    int r = 0, g = 0, b = 0;
    query_palette_reg(color, &r, &g, &b);
    for (int i = 0; i < 256; i++)
    {
        int tr = 0, tg = 0, tb = 0;
        query_palette_reg(static_cast<unsigned char>(i), &tr, &tg, &tb);
        if (tr == r && tg == g && tb == b)
            return i;
    }
    return -1;
}

// The 8-bit RGB a palette register paints with: the registers hold 6-bit VGA
// values and the blit path scales them by 4.
void pal_rgb8(unsigned char color, int* r, int* g, int* b)
{
    query_palette_reg(color, r, g, b);
    *r *= 4;
    *g *= 4;
    *b *= 4;
}

// Ink counts of the shipped 5x6 font (data/text.png) for the strings the text
// cases draw: the number of pixels that come out in the requested colour. They
// are goldens -- a blit that paints nothing, paints the wrong colour, or lays
// the glyphs at the wrong pitch all read differently.
inline constexpr int kGlyphInkA = 12;            // 'A'
inline constexpr int kWordInkMass = 45;          // "mass"
inline constexpr int kWordInk42 = 21;            // "42"
inline constexpr int kWordInkShadowGlyph = 67;   // "shadow", glyph pass
inline constexpr int kWordInkShadowShade = 54;   // "shadow", shadow pass not overdrawn
inline constexpr int kWordInkCenter = 70;        // "center"

// The shared font pixies are lazily-loaded statics that text_shutdown() frees,
// so any case that draws text must be able to get them back regardless of the
// order gtest picks. A text ctor reloads both when they are invalid.
void ensure_font_loaded()
{
    text reload(TEXT_1);
    (void)reload.sizex;
}

// How many pixels of a rect read back as `color`. Glyph and outline blits are
// counted rather than probed pixel-by-pixel: the expected counts are the
// shipped font's own ink, so a blit that paints nothing, paints the wrong
// colour, or paints the wrong number of pixels all read differently.
int count_index_in(int x, int y, int w, int h, unsigned char color)
{
    const int want = pal_readback_index(color);
    int n = 0;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (px_index(x + i, y + j) == want)
                n++;
    return n;
}

// The per-channel blend the pointb/blend_pixel alpha path performs:
// dest + ((src - dest) * alpha >> 8), in 8-bit channel values
// (src/platform/sdl/video_sdl.cpp blend_pixel, 32bpp case).
int alpha_blend8(int dest, int src, int alpha)
{
    return dest + (((src - dest) * alpha) >> 8);
}

// Row-major palette indices of a rect of the render surface, for golden-by-
// reconstruction comparisons.
std::vector<int> snapshot_indices(int x, int y, int w, int h)
{
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            out.push_back(px_index(x + i, y + j));
    return out;
}

// The whole 256-entry palette, put back even when an assertion fires: a case
// that rotates or writes palette registers otherwise cascades into every later
// index readback in this binary.
struct ScopedPalette
{
    std::array<std::array<int, 3>, 256> saved{};

    ScopedPalette()
    {
        for (int i = 0; i < 256; i++)
            query_palette_reg(static_cast<unsigned char>(i), &saved[static_cast<std::size_t>(i)][0],
                              &saved[static_cast<std::size_t>(i)][1],
                              &saved[static_cast<std::size_t>(i)][2]);
    }
    ~ScopedPalette()
    {
        for (int i = 0; i < 256; i++)
            set_palette_reg(static_cast<unsigned char>(i), saved[static_cast<std::size_t>(i)][0],
                            saved[static_cast<std::size_t>(i)][1],
                            saved[static_cast<std::size_t>(i)][2]);
    }
    ScopedPalette(const ScopedPalette&) = delete;
    ScopedPalette& operator=(const ScopedPalette&) = delete;
};

// screen::numviews and a view's PREF_VIEW are process-wide layout state that
// later cases in this binary read; restore both on the way out, failure or not.
struct ScopedViewPrefs
{
    screen* scr;
    short saved_numviews;
    std::array<signed char, 4> saved_pref{};
    std::array<bool, 4> had_view{};

    explicit ScopedViewPrefs(screen* s_)
        : scr(s_), saved_numviews(s_->numviews)
    {
        for (int i = 0; i < 4; i++)
        {
            had_view[static_cast<std::size_t>(i)] = (scr->viewob[i] != nullptr);
            if (had_view[static_cast<std::size_t>(i)])
                saved_pref[static_cast<std::size_t>(i)] = scr->viewob[i]->prefs[PREF_VIEW];
        }
    }
    ~ScopedViewPrefs()
    {
        for (int i = 0; i < 4; i++)
            if (had_view[static_cast<std::size_t>(i)] && scr->viewob[i] != nullptr)
                scr->viewob[i]->prefs[PREF_VIEW] = saved_pref[static_cast<std::size_t>(i)];
        scr->numviews = saved_numviews;
    }
    ScopedViewPrefs(const ScopedViewPrefs&) = delete;
    ScopedViewPrefs& operator=(const ScopedViewPrefs&) = delete;
};

PixieData make_test_pixie_data(unsigned char frames = 1,
                               unsigned char w = 2,
                               unsigned char h = 2,
                               unsigned char base = 32)
{
    const std::size_t pixel_count =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    auto* raw = new unsigned char[pixel_count];
    for (std::size_t i = 0; i < pixel_count; i++)
        raw[i] = static_cast<unsigned char>(base + (i % 8));
    return PixieData(frames, w, h, raw);
}

} // namespace

// vbutton + button helpers (button.cpp uncovered)
TEST(MassCoverage, vbutton_ctor_callback) {
    vbutton b(1, 1, 20, 10, [](Sint32 v) { return v + 1; }, 7, "cb", KEYSTATE_UNKNOWN);
    ASSERT_EQ(7, b.arg) << "callback ctor should set arg";
}

TEST(MassCoverage, vbutton_ctor_func_code) {
    vbutton b(2, 2, 20, 10, 999, 5, "fn", KEYSTATE_UNKNOWN);
    ASSERT_EQ(999, b.myfunc) << "func ctor should set myfunc";
}

// The family ctor loads the Order::Button1 graphic for `family` and RESIZES
// the button to it: the wide/high arguments are overwritten by the pixie's own
// dimensions and xend/yend recomputed from them
// (src/interface/ui/button.cpp, the family ctor).
TEST(MassCoverage, vbutton_ctor_family_takes_its_size_from_the_family_graphic) {
    vbutton b(3, 3, 20, 10, 1, 0, "gfx", FAMILY_NORMAL1, KEYSTATE_UNKNOWN);
    ASSERT_NE(nullptr, b.mypixie.get()) << "family ctor should allocate pixie";
    EXPECT_EQ(static_cast<int>(b.mypixie->sizex), static_cast<int>(b.width))
        << "the passed width is discarded for the graphic's";
    EXPECT_EQ(static_cast<int>(b.mypixie->sizey), static_cast<int>(b.height))
        << "the passed height is discarded for the graphic's";
    EXPECT_EQ(3 + static_cast<int>(b.width), static_cast<int>(b.xend))
        << "xend follows the graphic width, not the passed one";
    EXPECT_EQ(3 + static_cast<int>(b.height), static_cast<int>(b.yend))
        << "yend follows the graphic height, not the passed one";

    // Number-free proof that the passed size never survives: a second button
    // asking for a wildly different box comes out the same size.
    vbutton wide(3, 3, 77, 55, 1, 0, "gfx", FAMILY_NORMAL1, KEYSTATE_UNKNOWN);
    EXPECT_EQ(static_cast<int>(b.width), static_cast<int>(wide.width))
        << "two buttons on the same family graphic have the same width";
    EXPECT_EQ(static_cast<int>(b.height), static_cast<int>(wide.height))
        << "two buttons on the same family graphic have the same height";

    // And that the family byte actually selects WHICH graphic: normal1.png is
    // the 140x20 menu plate, butplus.png the 16x12 stepper.
    vbutton plus(3, 3, 20, 10, 1, 0, "gfx", FAMILY_PLUS, KEYSTATE_UNKNOWN);
    ASSERT_NE(nullptr, plus.mypixie.get()) << "family ctor should allocate pixie";
    EXPECT_NE(static_cast<int>(b.width), static_cast<int>(plus.width))
        << "a different family byte must load a different graphic";
}

// set_graphic(family) is the same rule applied after construction: it replaces
// the pixie and re-derives width/height/xend/yend from it
// (src/interface/ui/button.cpp vbutton::set_graphic).
TEST(MassCoverage, vbutton_set_graphic_resizes_the_button_to_the_new_graphic) {
    vbutton b(3, 3, 20, 10, 1, 0, "gfx2", KEYSTATE_UNKNOWN);
    ASSERT_EQ(nullptr, b.mypixie.get()) << "setup: the plain ctor carries no graphic";
    ASSERT_EQ(20, static_cast<int>(b.width)) << "setup: the plain ctor keeps the passed width";

    b.set_graphic(FAMILY_PLUS);
    ASSERT_NE(nullptr, b.mypixie.get()) << "set_graphic should allocate pixie";
    const int plus_w = static_cast<int>(b.mypixie->sizex);
    const int plus_h = static_cast<int>(b.mypixie->sizey);
    EXPECT_EQ(plus_w, static_cast<int>(b.width)) << "set_graphic resizes to the graphic";
    EXPECT_EQ(plus_h, static_cast<int>(b.height)) << "set_graphic resizes to the graphic";
    EXPECT_EQ(3 + plus_w, static_cast<int>(b.xend)) << "xend is re-derived from the new width";
    EXPECT_EQ(3 + plus_h, static_cast<int>(b.yend)) << "yend is re-derived from the new height";

    // A second call swaps the graphic and the size again -- the button is not
    // stuck on whatever it loaded first.
    b.set_graphic(FAMILY_NORMAL1);
    EXPECT_NE(plus_w, static_cast<int>(b.width)) << "set_graphic must reload, not keep the old size";
    EXPECT_EQ(static_cast<int>(b.mypixie->sizex), static_cast<int>(b.width))
        << "the new width is the new graphic's";
    EXPECT_EQ(3 + static_cast<int>(b.width), static_cast<int>(b.xend))
        << "xend follows the new width";
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

// toggle_effect (src/interface/ui/button.cpp): is_on -> "off", else -> "on",
// in the category/setting it was handed and no other.
TEST(MassCoverage, toggle_effect_flips_the_named_setting_both_ways) {
    cfg.apply_setting("effects", "mass_toggle_effect", "off");
    cfg.apply_setting("effects", "mass_toggle_witness", "off");
    ASSERT_FALSE(cfg.is_on("effects", "mass_toggle_effect")) << "setup: the setting starts off";

    toggle_effect("effects", "mass_toggle_effect");
    ASSERT_STREQ("on", cfg.get_setting("effects", "mass_toggle_effect").c_str())
        << "toggle_effect on an off setting must write \"on\"";
    ASSERT_TRUE(cfg.is_on("effects", "mass_toggle_effect")) << "the written value must read back as on";

    toggle_effect("effects", "mass_toggle_effect");
    ASSERT_STREQ("off", cfg.get_setting("effects", "mass_toggle_effect").c_str())
        << "toggle_effect on an on setting must write \"off\"";
    ASSERT_FALSE(cfg.is_on("effects", "mass_toggle_effect")) << "the written value must read back as off";

    ASSERT_STREQ("off", cfg.get_setting("effects", "mass_toggle_witness").c_str())
        << "toggle_effect must only touch the setting it was given";
}

// screen.cpp uncovered wrappers/branches
// ready_for_battle(n): n views built, and the per-battle state (end, retry,
// redrawme, framecount, enemy_freeze, level progress) zeroed
// (src/interface/screen.cpp screen::ready_for_battle).
TEST(MassCoverage, screen_ready_for_battle_builds_views_and_clears_battle_state) {
    screen* s = og::runtime::current_session->myscreen_;
    GameWorld& world = s->world();

    world.end = 7;
    world.retry = true;
    world.enemy_freeze = 5;
    world.completion_events_emitted = true;
    world.set_level_tick_count(9);
    s->redrawme = 0;
    s->framecount = 42;

    s->ready_for_battle(1);

    ASSERT_EQ(1, static_cast<int>(s->numviews)) << "ready_for_battle(1) must set numviews";
    ASSERT_NE(nullptr, s->viewob[0].get()) << "ready_for_battle must build view 0";
    ASSERT_EQ(nullptr, s->viewob[1].get()) << "ready_for_battle(1) must leave no second view";
    ASSERT_EQ(0, static_cast<int>(world.end)) << "ready_for_battle must clear world.end";
    ASSERT_FALSE(world.retry) << "ready_for_battle must clear world.retry";
    ASSERT_EQ(0, static_cast<int>(world.enemy_freeze)) << "ready_for_battle must clear enemy_freeze";
    ASSERT_EQ(1, static_cast<int>(s->redrawme)) << "ready_for_battle must request a redraw";
    ASSERT_EQ(0u, s->framecount) << "ready_for_battle must restart the frame count";
    ASSERT_EQ(0u, world.level_tick_count()) << "ready_for_battle must reset level progress";
    ASSERT_FALSE(world.completion_events_emitted) << "ready_for_battle must re-arm completion events";
}

// reset(n): n views reconstructed, and cleanup() drops the views above n
// (src/interface/screen.cpp screen::reset).
TEST(MassCoverage, screen_reset_reconstructs_exactly_n_views) {
    screen* s = og::runtime::current_session->myscreen_;

    s->reset(2);
    ASSERT_EQ(2, static_cast<int>(s->numviews)) << "reset(2) must set numviews=2";
    ASSERT_NE(nullptr, s->viewob[0].get()) << "reset(2) must construct view 0";
    ASSERT_NE(nullptr, s->viewob[1].get()) << "reset(2) must construct view 1";

    s->reset(1);
    ASSERT_EQ(1, static_cast<int>(s->numviews)) << "reset(1) must set numviews=1";
    ASSERT_NE(nullptr, s->viewob[0].get()) << "reset(1) must construct view 0";
    ASSERT_EQ(nullptr, s->viewob[1].get()) << "reset(1) must drop the second view";
}

// query_grid_passable(x,y,ob): null ob never passes; a walkable floor tile
// passes and a blocking tile does not (src/gameplay/game_world.cpp).
TEST(MassCoverage, screen_query_grid_passable_reads_the_tile_under_the_walker) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    ASSERT_TRUE(world.grid.data != nullptr) << "setup: create_new_grid must allocate the grid";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the probing walker must exist";
    w->set_sizex(1);
    w->set_sizey(1);
    w->setxy(100, 100);

    ASSERT_FALSE(world.query_grid_passable(100, 100, nullptr))
        << "a null walker must never be grid-passable";

    const std::size_t cell =
        static_cast<std::size_t>(100 / GRID_SIZE) +
        static_cast<std::size_t>(world.grid.w) * static_cast<std::size_t>(100 / GRID_SIZE);
    world.grid.data[cell] = PIX_GRASS1;
    ASSERT_TRUE(world.query_grid_passable(100, 100, w)) << "grass under the walker must be passable";

    world.grid.data[cell] = PIX_WATER1;
    ASSERT_FALSE(world.query_grid_passable(100, 100, w))
        << "water under a non-swimming living must block";

    world.grid.data[cell] = PIX_WALLSIDE_L;
    ASSERT_FALSE(world.query_grid_passable(100, 100, w))
        << "a wall under a living must block";

    reset_level_state();
}

// query_object_passable(x,y,ob): null ob never passes, a dead ob always does,
// otherwise the obmap's occupancy decides (src/gameplay/game_world.cpp).
TEST(MassCoverage, screen_query_object_passable_reads_obmap_occupancy) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the probing walker must exist";
    w->setxy(100, 100);

    ASSERT_FALSE(world.query_object_passable(300, 300, nullptr))
        << "a null walker must never be object-passable";

    w->set_dead(1);
    ASSERT_TRUE(world.query_object_passable(300, 300, w))
        << "a dead walker must short-circuit the obmap check";
    w->set_dead(0);

    ASSERT_TRUE(world.query_object_passable(300, 300, w))
        << "an unoccupied spot must be object-passable";

    walker* blocker = add_living(1);
    ASSERT_NE(nullptr, blocker) << "setup: the blocking walker must exist";
    blocker->setxy(300, 300);
    ASSERT_FALSE(world.query_object_passable(300, 300, w))
        << "a living occupying the spot must block";

    reset_level_state();
}
// clear(): the render canvas is blanked (src/interface/screen.cpp
// screen::clear -> clearbuffer).
TEST(MassCoverage, screen_clear_blanks_the_canvas) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(1);

    s->point(5, 5, RED);
    int painted = -1;
    s->get_pixel(5, 5, &painted);
    ASSERT_EQ(static_cast<int>(RED), painted)
        << "setup: the probe pixel must be painted before clear() is asked to blank it";

    s->clear();

    painted = -1;
    s->get_pixel(5, 5, &painted);
    ASSERT_EQ(0, painted) << "clear() must blank the render canvas";
}

// redraw(): composes a world frame and runs the per-view chrome pass, which
// frames every non-FULL view (src/interface/screen.cpp screen::redraw ->
// draw_panel_chrome, TRACE("hud", "panel_border view=%d")).
TEST(MassCoverage, screen_redraw_runs_the_panel_chrome_pass) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(1);
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";
    ScopedViewPrefs restore_views(s);

    s->viewob[0]->prefs[PREF_VIEW] = PREF_VIEW_PANELS;
    trace_clear();
    ASSERT_TRUE(s->redraw()) << "redraw must report a composed frame";
    ASSERT_TRUE(trace_contains("hud", "panel_border view=0"))
        << "redraw must frame the non-FULL view 0";

    s->viewob[0]->prefs[PREF_VIEW] = PREF_VIEW_FULL;
    trace_clear();
    ASSERT_TRUE(s->redraw()) << "redraw must report a composed frame";
    ASSERT_FALSE(trace_contains("hud", "panel_border view=0"))
        << "a FULL view carries no frame";
}

// refresh(): presents the composed canvas exactly once, and presents nothing at
// all when there is no view to compose (src/interface/screen.cpp
// screen::refresh; a present clears window_is_black_ in Screen::swap).
TEST(MassCoverage, screen_refresh_presents_the_canvas_and_skips_when_no_views) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(1);
    ASSERT_TRUE(s->window_is_black())
        << "setup: every integration test starts from a black window";
    ScopedViewPrefs restore_views(s);

    s->numviews = 0;
    s->refresh();
    ASSERT_TRUE(s->window_is_black()) << "refresh with no views must present nothing";
    s->numviews = restore_views.saved_numviews;

    ASSERT_LT(0, static_cast<int>(s->numviews)) << "setup: at least one view must exist";
    s->clearbuffer();
    s->draw_rect_filled(0, 0, 40, 40, WHITE, 255);
    s->refresh();
    ASSERT_FALSE(s->window_is_black()) << "refresh must present the composed canvas";
}

// endgame(ending) forwards to endgame(ending, -1), which returns 1 immediately
// when world.end is already set: no results screen, no win fold
// (src/interface/screen.cpp screen::endgame).
TEST(MassCoverage, screen_endgame_one_arg_short_circuits_on_an_ended_world) {
    screen* s = og::runtime::current_session->myscreen_;
    const char saved_end = s->world().end;
    s->world().end = 1;

    ASSERT_EQ(1, static_cast<int>(s->endgame(0)))
        << "endgame on an already-ended world must report handled";
    ASSERT_EQ(1, static_cast<int>(s->world().end))
        << "the short circuit must leave the world.end guard set";

    s->world().end = saved_end;
}

TEST(MassCoverage, screen_endgame_two_args_short_circuits_without_touching_save_state) {
    screen* s = og::runtime::current_session->myscreen_;
    const char saved_end = s->world().end;
    const short saved_scen = s->save_data.scen_num;
    s->world().end = 1;

    ASSERT_EQ(1, static_cast<int>(s->endgame(0, -1)))
        << "endgame on an already-ended world must report handled";
    ASSERT_EQ(1, static_cast<int>(s->world().end))
        << "the short circuit must leave the world.end guard set";
    ASSERT_EQ(static_cast<int>(saved_scen), static_cast<int>(s->save_data.scen_num))
        << "the short circuit must not advance the campaign cursor";

    s->world().end = saved_end;
}

// find_near_foe(ob): nullptr for a null searcher; otherwise an obmap spiral
// around the searcher's OWN floor that starts one cell out -- a foe sharing the
// searcher's cell is invisible to it -- and only falls back to the full-list
// find_far_foe scan once the spiral leaves the map
// (src/gameplay/game_world.cpp GameWorld::find_near_foe).
TEST(MassCoverage, screen_find_near_foe_returns_the_obmap_spiral_hit) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    ASSERT_NE(nullptr, world.myobmap.get()) << "setup: the collision map must exist";
    // The spiral abandons itself to find_far_foe the moment it steps off the
    // map, so the arena has to be wider than its first probe at x=132.
    world.create_new_grid();
    ASSERT_LT(132, world.pixmaxx) << "setup: the map must outreach the spiral's first probe";

    ASSERT_EQ(nullptr, world.find_near_foe(nullptr)) << "a null searcher has no foe";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    ASSERT_EQ(nullptr, world.find_near_foe(w)) << "an empty level has no foe";

    walker* ally = add_sized_living(0, 140, 100);
    ASSERT_NE(nullptr, ally) << "setup: the ally must exist";
    ASSERT_EQ(nullptr, world.find_near_foe(w)) << "a same-team walker is never a foe";

    // (140,100) is the very first cell the spiral probes; (101,100) shares the
    // searcher's own cell, which the spiral steps over and never probes. So the
    // two functions must disagree here, and that disagreement is the only proof
    // the spiral ran at all (find_near_foe falls back to find_far_foe).
    walker* spiral_foe = add_sized_living(1, 140, 100, FAMILY_ORC);
    ASSERT_NE(nullptr, spiral_foe) << "setup: the spiral foe must exist";
    walker* own_cell_foe = add_sized_living(1, 101, 100, FAMILY_ORC);
    ASSERT_NE(nullptr, own_cell_foe) << "setup: the own-cell foe must exist";

    ASSERT_EQ(spiral_foe, world.find_near_foe(w))
        << "find_near_foe must answer from the obmap spiral, not the full-list scan";
    ASSERT_EQ(own_cell_foe, world.find_far_foe(w))
        << "control: the full-list scan does see the closer own-cell foe";

    reset_level_state();
}

// find_far_foe(ob): despite the name, the full-list scan returns the CLOSEST
// non-friendly, live, non-dormant Living/Generator -- the minimum of
// distance_to_ob -- and nullptr when there is none; it also stamps the 10000
// sentinel on the searcher (src/gameplay/game_world.cpp GameWorld::find_far_foe).
TEST(MassCoverage, screen_find_far_foe_scans_the_list_for_the_closest_live_foe) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    ASSERT_EQ(nullptr, world.find_far_foe(nullptr)) << "a null searcher has no foe";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    ASSERT_EQ(nullptr, world.find_far_foe(w)) << "an empty level has no foe";

    walker* distant_foe = add_sized_living(1, 200, 100, FAMILY_ORC);
    ASSERT_NE(nullptr, distant_foe) << "setup: the distant foe must exist";
    ASSERT_EQ(distant_foe, world.find_far_foe(w)) << "the only foe must be returned";

    walker* close_foe = add_sized_living(1, 110, 100, FAMILY_ORC);
    ASSERT_NE(nullptr, close_foe) << "setup: the close foe must exist";
    ASSERT_EQ(close_foe, world.find_far_foe(w))
        << "the scan keeps the smallest distance_to_ob, not the largest";
    ASSERT_EQ(10000u, w->stats()->last_distance())
        << "the scan stamps the 10000 sentinel on the searcher";

    close_foe->set_dormant(true);
    ASSERT_EQ(distant_foe, world.find_far_foe(w)) << "a dormant foe is skipped";
    close_foe->set_dormant(false);

    close_foe->set_dead(1);
    ASSERT_EQ(distant_foe, world.find_far_foe(w)) << "a dead foe is skipped";

    reset_level_state();
}

// get_scen_title_with_error(filename, out): an unloadable scenario reports a
// typed error and leaves the caller's string at "none" -- it never keeps the
// caller's previous value (src/interface/screen.cpp
// screen::get_scen_title_with_error -> og::data::load_scenario_title_with_error).
TEST(MassCoverage, screen_get_scen_title_with_error_reports_open_failure_and_the_none_fallback) {
    screen* s = og::runtime::current_session->myscreen_;

    std::string out = "left over from the caller";
    ASSERT_EQ(screen::ScenarioTitleError::OpenReadFailed,
              s->get_scen_title_with_error(nullptr, out))
        << "a null filename cannot be opened";
    ASSERT_EQ("none", out) << "the out-parameter must be reset to the none fallback";

    out = "left over from the caller";
    ASSERT_EQ(screen::ScenarioTitleError::OpenReadFailed,
              s->get_scen_title_with_error("missing_mass_scen", out))
        << "a missing scenario file cannot be opened";
    ASSERT_EQ("none", out) << "the out-parameter must be reset to the none fallback";
}

// get_scen_title(filename, master): the char* form hands back "none" whenever
// the title could not be loaded (src/interface/screen.cpp screen::get_scen_title).
TEST(MassCoverage, screen_get_scen_title_returns_the_none_fallback_for_a_missing_scenario) {
    screen* s = og::runtime::current_session->myscreen_;
    ASSERT_STREQ("none", s->get_scen_title("missing_mass_scen", s))
        << "a missing scenario must read back as the none fallback";
    ASSERT_STREQ("none", s->get_scen_title(nullptr, s))
        << "a null filename must read back as the none fallback";
}

// first_of(order, family, team): the first LIVE walker matching order+family,
// with team -1 meaning any team (src/interface/screen.cpp screen::first_of).
TEST(MassCoverage, screen_first_of_matches_order_family_and_team) {
    reset_level_state();
    screen* s = og::runtime::current_session->myscreen_;

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the soldier must exist";

    ASSERT_EQ(w, s->first_of(Order::Living, FAMILY_SOLDIER, 0))
        << "the team-0 soldier must be found on its own team";
    ASSERT_EQ(w, s->first_of(Order::Living, FAMILY_SOLDIER, -1))
        << "team -1 must match any team";
    ASSERT_EQ(nullptr, s->first_of(Order::Living, FAMILY_SOLDIER, 1))
        << "another team must not match";
    ASSERT_EQ(nullptr, s->first_of(Order::Living, FAMILY_ORC, 0))
        << "another family must not match";
    ASSERT_EQ(nullptr, s->first_of(Order::Weapon, FAMILY_SOLDIER, 0))
        << "another order must not match";

    w->set_dead(1);
    ASSERT_EQ(nullptr, s->first_of(Order::Living, FAMILY_SOLDIER, 0))
        << "a dead walker must not be returned";

    reset_level_state();
}

// draw_panels(n): clears the buffer and redraws, so the per-view chrome pass
// runs (src/interface/screen.cpp screen::draw_panels -> redraw ->
// draw_panel_chrome).
TEST(MassCoverage, screen_draw_panels_repaints_through_redraw) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(1);
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";
    ScopedViewPrefs restore_views(s);
    s->viewob[0]->prefs[PREF_VIEW] = PREF_VIEW_PANELS;

    trace_clear();
    s->draw_panels(1);
    ASSERT_TRUE(trace_contains("hud", "panel_border view=0"))
        << "draw_panels must repaint through redraw's chrome pass";
}

// draw_panel_chrome frames every view whose PREF_VIEW is not FULL (and skips
// the whole pass at four views) -- src/interface/screen.cpp
// screen::draw_panel_chrome, TRACE("hud", "panel_border view=%d").
TEST(MassCoverage, screen_draw_panels_frames_every_non_full_view) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(2);
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";
    ASSERT_NE(nullptr, s->viewob[1].get()) << "setup: view 1 must exist";
    ScopedViewPrefs restore_views(s);

    s->viewob[0]->prefs[PREF_VIEW] = PREF_VIEW_PANELS;
    s->viewob[1]->prefs[PREF_VIEW] = PREF_VIEW_1;
    trace_clear();
    s->draw_panels(2);
    ASSERT_TRUE(trace_contains("hud", "panel_border view=0"))
        << "a PANELS view must be framed";
    ASSERT_TRUE(trace_contains("hud", "panel_border view=1"))
        << "a split view must be framed";

    s->viewob[1]->prefs[PREF_VIEW] = PREF_VIEW_FULL;
    trace_clear();
    s->draw_panels(2);
    ASSERT_TRUE(trace_contains("hud", "panel_border view=0"))
        << "the non-FULL view must still be framed";
    ASSERT_FALSE(trace_contains("hud", "panel_border view=1"))
        << "a FULL view must not be framed";

    s->reset(1);
}

// find_nearest_blood(who): nullptr for a null walker, otherwise the closest
// LIVE FAMILY_STAIN Treasure in fxlist whose squared centre distance is under
// 800 (src/gameplay/game_world.cpp GameWorld::find_nearest_blood).
TEST(MassCoverage, screen_find_nearest_blood_picks_the_closest_stain_in_range) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    ASSERT_EQ(nullptr, world.find_nearest_blood(nullptr)) << "a null walker has no blood";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    w->set_sizex(1);
    w->set_sizey(1);
    ASSERT_EQ(nullptr, world.find_nearest_blood(w)) << "an empty level has no blood";

    ASSERT_NE(nullptr, add_fx_treasure(FAMILY_LIFE_GEM, 100, 100))
        << "setup: the decoy treasure must exist";
    ASSERT_EQ(nullptr, world.find_nearest_blood(w))
        << "only FAMILY_STAIN treasures count as blood";

    // 1x1 boxes make distance_to_ob_center the exact squared delta: 20*20=400
    // and 5*5=25 are both inside the 800 search radius.
    walker* far_stain = add_fx_treasure(FAMILY_STAIN, 120, 100);
    ASSERT_NE(nullptr, far_stain) << "setup: the far stain must exist";
    walker* near_stain = add_fx_treasure(FAMILY_STAIN, 105, 100);
    ASSERT_NE(nullptr, near_stain) << "setup: the near stain must exist";
    ASSERT_EQ(near_stain, world.find_nearest_blood(w)) << "the closest stain must win";

    near_stain->set_dead(1);
    ASSERT_EQ(far_stain, world.find_nearest_blood(w)) << "a dead stain is skipped";

    // 29*29 = 841, just outside the radius.
    far_stain->setxy(129, 100);
    ASSERT_EQ(nullptr, world.find_nearest_blood(w))
        << "a stain past the 800 radius must be ignored";

    reset_level_state();
}

// find_in_range(list, range, &n, ob): every live, non-dormant walker within
// range (the searcher included), counted into n; empty with n=0 for a null
// searcher (src/gameplay/game_world.cpp GameWorld::find_in_range).
TEST(MassCoverage, screen_find_in_range_counts_every_live_walker_within_range) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    Sint32 n = 9;
    std::list<walker*> found = world.find_in_range(world.oblist, 32, &n, nullptr);
    ASSERT_EQ(0, n) << "a null searcher must write a zero count";
    ASSERT_TRUE(found.empty()) << "a null searcher must return nothing";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    walker* neighbour = add_sized_living(1, 110, 100, FAMILY_ORC);
    ASSERT_NE(nullptr, neighbour) << "setup: the neighbour must exist";

    n = 0;
    found = world.find_in_range(world.oblist, 32, &n, w);
    ASSERT_EQ(2, n) << "the searcher and the neighbour are both within 32";
    ASSERT_EQ(std::size_t{2}, found.size()) << "the list must match the count";

    neighbour->setxy(300, 100);
    n = 0;
    found = world.find_in_range(world.oblist, 32, &n, w);
    ASSERT_EQ(1, n) << "only the searcher stays within 32";
    ASSERT_EQ(w, found.front()) << "the searcher is always in its own range";

    neighbour->setxy(110, 100);
    neighbour->set_dead(1);
    n = 0;
    found = world.find_in_range(world.oblist, 32, &n, w);
    ASSERT_EQ(1, n) << "a dead walker in range must be excluded";

    reset_level_state();
}

// find_nearest_player(ob): the closest non-dormant walker with user() != -1,
// nullptr when nobody is user-controlled (src/gameplay/game_world.cpp
// GameWorld::find_nearest_player).
TEST(MassCoverage, screen_find_nearest_player_picks_the_closest_user_controlled_walker) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    ASSERT_EQ(nullptr, world.find_nearest_player(nullptr)) << "a null searcher has no player";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    ASSERT_EQ(nullptr, world.find_nearest_player(w))
        << "an uncontrolled level has no nearest player";

    walker* distant_player = add_sized_living(0, 200, 100);
    ASSERT_NE(nullptr, distant_player) << "setup: the distant player must exist";
    distant_player->set_user(1);
    ASSERT_EQ(distant_player, world.find_nearest_player(w))
        << "the only user-controlled walker must be returned";

    walker* close_player = add_sized_living(0, 120, 100);
    ASSERT_NE(nullptr, close_player) << "setup: the close player must exist";
    close_player->set_user(0);
    ASSERT_EQ(close_player, world.find_nearest_player(w))
        << "the closest user-controlled walker must win";

    close_player->set_dormant(true);
    ASSERT_EQ(distant_player, world.find_nearest_player(w))
        << "a dormant player must be skipped";

    reset_level_state();
}

// find_foes_in_range: live, non-dormant Living/Generator walkers the searcher
// is NOT friendly to, within range (src/gameplay/game_world.cpp
// GameWorld::find_foes_in_range).
TEST(MassCoverage, screen_find_foes_in_range_excludes_allies_and_distant_foes) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    Sint32 n = 9;
    std::list<walker*> found = world.find_foes_in_range(world.oblist, 64, &n, nullptr);
    ASSERT_EQ(0, n) << "a null searcher must write a zero count";
    ASSERT_TRUE(found.empty()) << "a null searcher must return nothing";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    walker* foe = add_sized_living(1, 105, 100, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "setup: the foe must exist";

    n = 0;
    found = world.find_foes_in_range(world.oblist, 64, &n, w);
    ASSERT_EQ(1, n) << "exactly the one hostile walker is in range";
    ASSERT_EQ(foe, found.front()) << "and it is the foe, not the searcher";

    ASSERT_NE(nullptr, add_sized_living(0, 110, 100)) << "setup: the ally must exist";
    n = 0;
    found = world.find_foes_in_range(world.oblist, 64, &n, w);
    ASSERT_EQ(1, n) << "an ally in range is not a foe";

    foe->setxy(300, 100);
    n = 0;
    found = world.find_foes_in_range(world.oblist, 64, &n, w);
    ASSERT_EQ(0, n) << "a foe past the range must be excluded";

    reset_level_state();
}

// find_friends_in_range: live, non-dormant Living walkers the searcher IS
// friendly to -- itself included -- within range (src/gameplay/game_world.cpp
// GameWorld::find_friends_in_range).
TEST(MassCoverage, screen_find_friends_in_range_counts_allies_and_the_searcher) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    Sint32 n = 9;
    std::list<walker*> found = world.find_friends_in_range(world.oblist, 64, &n, nullptr);
    ASSERT_EQ(0, n) << "a null searcher must write a zero count";
    ASSERT_TRUE(found.empty()) << "a null searcher must return nothing";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    walker* ally = add_sized_living(0, 110, 100);
    ASSERT_NE(nullptr, ally) << "setup: the ally must exist";

    n = 0;
    found = world.find_friends_in_range(world.oblist, 64, &n, w);
    ASSERT_EQ(2, n) << "the searcher is friendly to itself, so it and the ally both count";
    ASSERT_NE(found.end(), std::find(found.begin(), found.end(), ally))
        << "the ally must be in the returned list";

    ASSERT_NE(nullptr, add_sized_living(1, 105, 100, FAMILY_ORC))
        << "setup: the foe must exist";
    n = 0;
    found = world.find_friends_in_range(world.oblist, 64, &n, w);
    ASSERT_EQ(2, n) << "a hostile walker in range is not a friend";

    ally->setxy(300, 100);
    n = 0;
    found = world.find_friends_in_range(world.oblist, 64, &n, w);
    ASSERT_EQ(1, n) << "an ally past the range must be excluded";

    reset_level_state();
}

// find_foe_weapons_in_range: despite the name, the kept branch is the FRIENDLY
// one -- live Order::Weapon walkers the searcher is friendly to, within range
// (src/gameplay/game_world.cpp GameWorld::find_foe_weapons_in_range).
TEST(MassCoverage, screen_find_foe_weapons_in_range_keeps_friendly_weapons_in_range) {
    reset_level_state();
    GameWorld& world = og::runtime::current_session->myscreen_->world();

    Sint32 n = 9;
    std::list<walker*> found = world.find_foe_weapons_in_range(world.weaplist, 64, &n, nullptr);
    ASSERT_EQ(0, n) << "a null searcher must write a zero count";
    ASSERT_TRUE(found.empty()) << "a null searcher must return nothing";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the searcher must exist";
    walker* knife = add_weapon(FAMILY_KNIFE, 0);
    ASSERT_NE(nullptr, knife) << "setup: the knife must exist";

    n = 0;
    found = world.find_foe_weapons_in_range(world.weaplist, 64, &n, w);
    ASSERT_EQ(1, n) << "a same-team weapon in range is the one this filter keeps";
    ASSERT_EQ(knife, found.front()) << "and it is that knife";

    knife->set_team_num(1);
    n = 0;
    found = world.find_foe_weapons_in_range(world.weaplist, 64, &n, w);
    ASSERT_EQ(0, n) << "a weapon the searcher is not friendly to is excluded";

    knife->set_team_num(0);
    knife->setxy(300, 100);
    n = 0;
    found = world.find_foe_weapons_in_range(world.weaplist, 64, &n, w);
    ASSERT_EQ(0, n) << "a friendly weapon past the range is excluded";

    reset_level_state();
}

// screen::damage_tile forwards to GameWorld::damage_tile: grass chars to
// PIX_GRASS1_DAMAGED in the grid itself, every other tile is returned unchanged
// (src/interface/screen.cpp screen::damage_tile ->
// src/gameplay/game_world.cpp GameWorld::damage_tile).
TEST(MassCoverage, screen_damage_tile_chars_grass_and_leaves_other_tiles_alone) {
    screen* s = og::runtime::current_session->myscreen_;
    auto& world = s->world();
    world.create_new_grid();
    ASSERT_TRUE(world.grid.data != nullptr) << "grid should be allocated";

    world.grid.data[0] = PIX_GRASS1;
    ASSERT_EQ(PIX_GRASS1_DAMAGED, static_cast<unsigned char>(s->damage_tile(0, 0)))
        << "grass tile should convert to damaged grass";
    ASSERT_EQ(PIX_GRASS1_DAMAGED, static_cast<unsigned char>(world.grid.data[0]))
        << "the grid cell itself must be rewritten";

    world.grid.data[0] = PIX_WATER1;
    ASSERT_EQ(PIX_WATER1, static_cast<unsigned char>(s->damage_tile(0, 0)))
        << "a non-grass tile must be handed back unchanged";
    ASSERT_EQ(PIX_WATER1, static_cast<unsigned char>(world.grid.data[0]))
        << "a non-grass tile must not be rewritten";
}

// do_notify(msg, who): the message goes to the view whose control is `who`, and
// to EVERY view when no view controls it (src/interface/screen.cpp
// screen::do_notify -> viewscreen::set_display_text).
TEST(MassCoverage, screen_do_notify_targets_the_controlling_view_else_every_view) {
    reset_level_state();
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(2);
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";
    ASSERT_NE(nullptr, s->viewob[1].get()) << "setup: view 1 must exist";

    walker* w = add_living(0);
    ASSERT_NE(nullptr, w) << "setup: the controlled walker must exist";
    s->viewob[0]->control = nullptr;
    s->viewob[1]->control = w;

    s->viewob[0]->clear_text();
    s->viewob[1]->clear_text();
    s->do_notify("mass-targeted", w);
    ASSERT_EQ("mass-targeted", s->viewob[1]->textlist[0])
        << "the view controlling the walker must receive the notice";
    ASSERT_TRUE(s->viewob[0]->textlist[0].empty())
        << "a targeted notice must not reach the other view";

    s->viewob[0]->clear_text();
    s->viewob[1]->clear_text();
    s->do_notify("mass-broadcast", nullptr);
    ASSERT_EQ("mass-broadcast", s->viewob[0]->textlist[0])
        << "an untargeted notice must reach view 0";
    ASSERT_EQ("mass-broadcast", s->viewob[1]->textlist[0])
        << "an untargeted notice must reach view 1";

    s->viewob[0]->control = nullptr;
    s->viewob[1]->control = nullptr;
    s->reset(1);
    reset_level_state();
}

// report_mem(): posts its one memory line to view 0 for 25 cycles
// (src/interface/screen.cpp screen::report_mem).
TEST(MassCoverage, screen_report_mem_posts_the_memory_line_to_view_zero) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(1);
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";

    s->viewob[0]->clear_text();
    s->report_mem();
    ASSERT_EQ("Free Linear address: 0 pages", s->viewob[0]->textlist[0])
        << "report_mem must post its line to view 0";
    ASSERT_EQ(25, static_cast<int>(s->viewob[0]->textcycles[0]))
        << "report_mem must post it for 25 cycles";

    s->viewob[0]->clear_text();
}
TEST(MassCoverage, find_follow_leader_prefers_active_multiview_control) {
    reset_level_state();

    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(2);
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";
    ASSERT_NE(nullptr, s->viewob[1].get()) << "setup: view 1 must exist";

    walker* left = add_living(0);
    walker* right = add_living(1, FAMILY_ORC);
    ASSERT_NE(nullptr, left) << "setup: the view-0 leader must exist";
    ASSERT_NE(nullptr, right) << "setup: the view-1 leader must exist";

    left->set_yo_delay(0);
    right->set_yo_delay(4);
    s->viewob[0]->control = left;
    s->viewob[1]->control = right;
    ASSERT_EQ(right, find_follow_leader()) << "second active view should be selected";

    left->set_yo_delay(6);
    right->set_yo_delay(0);
    ASSERT_EQ(left, find_follow_leader()) << "first active view should be selected";

    left->set_yo_delay(0);
    right->set_yo_delay(0);
    ASSERT_EQ(nullptr, find_follow_leader()) << "no delayed view should return null";

    s->viewob[0]->control = nullptr;
    s->viewob[1]->control = nullptr;

    s->reset(1);
    reset_level_state();
}

TEST(MassCoverage, pixie_render_paths) {
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen should exist";

    PixieData data = make_test_pixie_data(1, 3, 2, 40);
    pixie p(data);

    ASSERT_EQ(3, static_cast<int>(p.sizex)) << "pixie width should be copied from data";
    ASSERT_EQ(2, static_cast<int>(p.sizey)) << "pixie height should be copied from data";
    ASSERT_TRUE(p.setxy(10, 12)) << "setxy should succeed";
    ASSERT_TRUE(p.move(3, -2)) << "move should succeed";
    ASSERT_EQ(13, static_cast<int>(p.xpos)) << "move should update x";
    ASSERT_EQ(10, static_cast<int>(p.ypos)) << "move should update y";

    ASSERT_TRUE(p.draw(vs)) << "draw(view) should succeed";
    ASSERT_TRUE(p.draw(24, 28, vs)) << "draw(x, y, view) should succeed";
    ASSERT_TRUE(p.drawMix(vs)) << "drawMix(view) should succeed";
    ASSERT_TRUE(p.drawMix(26, 30, vs)) << "drawMix(x, y, view) should succeed";
    ASSERT_TRUE(p.put_screen(0, 0)) << "put_screen should succeed";

    p.setxy(static_cast<short>(vs->topx + 1), static_cast<short>(vs->topy + 1));
    ASSERT_TRUE(p.on_screen(vs)) << "pixie should be visible when inside the view";
    ASSERT_TRUE(p.on_screen()) << "pixie should be visible in at least one active view";

    p.setxy(static_cast<short>(vs->topx - p.sizex - 2), static_cast<short>(vs->topy));
    ASSERT_TRUE(!p.on_screen(vs)) << "pixie left of the view should be hidden";

    p.setxy(static_cast<short>(vs->topx + vs->xview + 2), static_cast<short>(vs->topy));
    ASSERT_TRUE(!p.on_screen(vs)) << "pixie right of the view should be hidden";

    p.setxy(static_cast<short>(vs->topx), static_cast<short>(vs->topy - p.sizey - 2));
    ASSERT_TRUE(!p.on_screen(vs)) << "pixie above the view should be hidden";

    p.setxy(static_cast<short>(vs->topx), static_cast<short>(vs->topy + vs->yview + 2));
    ASSERT_TRUE(!p.on_screen(vs)) << "pixie below the view should be hidden";

    p.set_accel(1);
    if (p.accel) {
        ASSERT_TRUE(p.draw(30, 32, vs)) << "accelerated draw should still succeed";
        p.init_sdl_surface();
    }
    p.set_accel(0);
    ASSERT_EQ(0, p.accel) << "set_accel(0) should disable acceleration";
}

TEST(MassCoverage, pixie_backed_entity_constructors_set_orders_and_sizes) {
    PixieData data = make_test_pixie_data(2, 4, 5, 44);

    effect fx(data);
    EXPECT_EQ(Order::FX, fx.query_order());
    EXPECT_EQ(4, fx.sizex());
    EXPECT_EQ(5, fx.sizey());

    living live(data);
    EXPECT_EQ(Order::Living, live.query_order());
    EXPECT_EQ(4, live.sizex());
    EXPECT_EQ(5, live.sizey());

    treasure loot(data);
    EXPECT_EQ(Order::Treasure, loot.query_order());
    EXPECT_EQ(4, loot.sizex());
    EXPECT_EQ(5, loot.sizey());

    weap weapon(data);
    EXPECT_EQ(Order::Weapon, weapon.query_order());
    EXPECT_EQ(4, weapon.sizex());
    EXPECT_EQ(5, weapon.sizey());
}

TEST(MassCoverage, pixien_and_level_render_paths) {
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen should exist";

    PixieData animated = make_test_pixie_data(3, 2, 1, 60);
    pixieN frames(animated, 1);
    ASSERT_EQ(3, static_cast<int>(frames.frames)) << "frame count should be preserved";
    ASSERT_TRUE(frames.set_frame(1)) << "valid frame selection should succeed";
    ASSERT_EQ(1, static_cast<int>(frames.frame)) << "frame selection should update current frame";
    ASSERT_TRUE(!frames.set_frame(-1)) << "negative frame selection should fail";
    ASSERT_TRUE(!frames.set_frame(3)) << "out-of-range frame selection should fail";
    ASSERT_TRUE(frames.next_frame()) << "next_frame should advance and wrap";

    PixieData replacement = make_test_pixie_data(2, 1, 2, 70);
    frames.set_data(replacement);
    ASSERT_EQ(2, static_cast<int>(frames.frames)) << "set_data should replace frame count";
    ASSERT_EQ(0, static_cast<int>(frames.frame)) << "set_data should reset current frame";
    frames.set_accel(0);

    std::array<PixieData, PIX_MAX> tiles{};
    for (int i = 0; i < PIX_MAX; i++)
        tiles[static_cast<std::size_t>(i)] = make_test_pixie_data(1, 1, 1, static_cast<unsigned char>(i));

    auto render = create_sdl_level_render(tiles.data());
    ASSERT_NE(nullptr, render.get()) << "SDL level renderer should be created";

    render->draw_tile(0, 0, 0, vs);
    render->draw_tile(PIX_WATER1, 4, 4, vs);
    render->draw_tile(-1, 0, 0, vs);
    render->draw_tile(PIX_MAX, 0, 0, vs);
    render->reset_tiles(tiles.data());
    render->draw_tile(0, 0, 0, vs);
}

// viewscreen::view_team(): the no-argument form forwards to the fixed
// VIEW_TEAM_LEFT/TOP/RIGHT/BOTTOM rect (20,2)-(280,198), flags redrawme, paints
// the two-deep button bevel and writes the four BLACK column headers at
// left+5/+80/+140/+190 on row top+3 (src/interface/render/view.cpp
// viewscreen::view_team).
TEST(MassCoverage, viewscreen_view_team_default_draws_the_fixed_panel_and_its_four_headers) {
    reset_level_state();
    screen* s = og::runtime::current_session->myscreen_;
    const int kLeft = 20, kTop = 2, kRight = 280, kBottom = 198;

    s->clearbuffer();
    s->redrawme = 0;
    s->viewob[0]->view_team();

    ASSERT_EQ(1, static_cast<int>(s->redrawme))
        << "view_team must flag the frame for redraw";
    ASSERT_EQ(pal_readback_index(14), px_index(kLeft, 100))
        << "the panel's left bevel must sit on the fixed VIEW_TEAM_LEFT column";
    ASSERT_EQ(pal_readback_index(13), px_index(150, 100))
        << "the panel face must be painted inside the fixed rect";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(kLeft - 1, 100))
        << "nothing may be painted left of VIEW_TEAM_LEFT";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(kRight + 1, 100))
        << "nothing may be painted right of VIEW_TEAM_RIGHT";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(150, kBottom + 1))
        << "nothing may be painted below VIEW_TEAM_BOTTOM";

    // Golden-by-reconstruction for the header row: the same bevel plus exactly
    // these four strings, at exactly these offsets, in BLACK. An omitted or
    // shifted header, or a different colour, makes the bands differ.
    const std::vector<int> got = snapshot_indices(kLeft + 1, kTop + 1, kRight - kLeft - 1, 8);
    s->clearbuffer();
    s->draw_button(kLeft, kTop, kRight, kBottom, 2);
    s->text_normal.write_xy(kLeft + 5, kTop + 3, "  Name  ", static_cast<unsigned char>(BLACK));
    s->text_normal.write_xy(kLeft + 80, kTop + 3, "Health", static_cast<unsigned char>(BLACK));
    s->text_normal.write_xy(kLeft + 140, kTop + 3, "Power", static_cast<unsigned char>(BLACK));
    s->text_normal.write_xy(kLeft + 190, kTop + 3, "Level", static_cast<unsigned char>(BLACK));
    const std::vector<int> expected = snapshot_indices(kLeft + 1, kTop + 1, kRight - kLeft - 1, 8);
    ASSERT_EQ(expected, got)
        << "the header row must be the four BLACK column labels on the panel bevel";
}

// viewscreen::view_team(left,top,right,bottom): the explicit rect is honoured --
// the bevel lands on the given bounds and nothing outside them is touched.
TEST(MassCoverage, viewscreen_view_team_honours_its_explicit_bounds) {
    reset_level_state();
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->viewob[0]->view_team(30, 30, 280, 170);

    ASSERT_EQ(pal_readback_index(14), px_index(31, 31))
        << "the inner bevel's left edge must follow the requested left bound";
    ASSERT_EQ(pal_readback_index(13), px_index(150, 100))
        << "the panel face must be painted inside the requested rect";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(20, 20))
        << "the requested rect must not leak above/left of its top-left corner";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(29, 100))
        << "the column just left of the requested left bound stays clear";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(150, 29))
        << "the row just above the requested top bound stays clear";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(150, 171))
        << "the row just below the requested bottom bound stays clear";
}

// sdl_video::set_fullscreen(bool) is deliberately inert (video_sdl.cpp, the
// FIXME'd commented-out body): it must not resize the window or move the
// session's window_w_/window_h_ or the canvas geometry.
TEST(MassCoverage, video_set_fullscreen_is_inert_in_both_directions) {
    screen* s = og::runtime::current_session->myscreen_;
    const int win_w = static_cast<int>(og::runtime::current_session->window_w_);
    const int win_h = static_cast<int>(og::runtime::current_session->window_h_);
    const int cw = s->canvas_w();
    const int ch = s->canvas_h();
    const int wcw = s->world_canvas_w();
    const int wch = s->world_canvas_h();

    s->set_fullscreen(false);
    ASSERT_EQ(win_w, static_cast<int>(og::runtime::current_session->window_w_))
        << "set_fullscreen(false) must not touch the session window width";
    ASSERT_EQ(win_h, static_cast<int>(og::runtime::current_session->window_h_))
        << "set_fullscreen(false) must not touch the session window height";

    s->set_fullscreen(true);
    ASSERT_EQ(win_w, static_cast<int>(og::runtime::current_session->window_w_))
        << "set_fullscreen(true) must not touch the session window width";
    ASSERT_EQ(win_h, static_cast<int>(og::runtime::current_session->window_h_))
        << "set_fullscreen(true) must not touch the session window height";
    ASSERT_EQ(cw, s->canvas_w()) << "set_fullscreen must not move the active canvas width";
    ASSERT_EQ(ch, s->canvas_h()) << "set_fullscreen must not move the active canvas height";
    ASSERT_EQ(wcw, s->world_canvas_w()) << "set_fullscreen must not move the world canvas width";
    ASSERT_EQ(wch, s->world_canvas_h()) << "set_fullscreen must not move the world canvas height";
}

// clearbuffer(x,y,w,h) blacks EXACTLY that rect of the render surface
// (video_sdl.cpp -> Screen::clear(x,y,w,h) -> SDL_FillSurfaceRect).
TEST(MassCoverage, video_clearbuffer_rect_blacks_only_the_given_rect) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();
    s->draw_rect_filled(0, 0, 40, 40, WHITE, 255);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(5, 5)) << "setup: the fill must land";

    s->clearbuffer(1, 1, 20, 20);

    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(5, 5))
        << "the interior of the rect must be blacked";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(20, 20))
        << "the rect's last pixel (x+w-1, y+h-1) must be blacked";
    ASSERT_EQ(pal_readback_index(WHITE), px_index(0, 0))
        << "the row/column before the rect must survive";
    ASSERT_EQ(pal_readback_index(WHITE), px_index(21, 21))
        << "the pixel just past the rect must survive";
    ASSERT_EQ(pal_readback_index(WHITE), px_index(30, 30))
        << "clearbuffer(rect) must not black the whole surface";
}

// clear_window()'s whole contract (src/platform/sdl/sai2x.cpp
// Screen::clear_window): it blanks the render surface, it never presents, and
// it never touches window_is_black_. All four production callers use it as a
// per-FRAME background wipe under a full panel repaint -- the options family
// (menu_screen_specs.cpp options_panel_draw_background, shared by DIFFICULTY
// and every options subscreen), the pause player screen (pause_menu.cpp), the
// key-remap prompt and the sprite-sheet picker (picker.cpp). Setting the flag
// here would make menu_screen_runner take its fade-in branch on EVERY frame of
// those screens and disarm the #237 entry-violation listener, so the flag's
// owners stay as documented (sai2x.h window_is_black): only a present
// (Screen::swap) clears it, only a completed fadeblack(false) sets it.
TEST(MassCoverage, video_clear_window_blanks_the_render_surface_and_never_presents) {
    screen* s = og::runtime::current_session->myscreen_;

    // Half 1: black window in, black window out -- a wipe presents nothing.
    s->testing_reset_window_state();
    ASSERT_TRUE(s->window_is_black())
        << "setup: testing_reset_window_state must arm the black-window flag";
    s->clearbuffer();
    s->draw_rect_filled(0, 0, 40, 40, WHITE, 255);
    s->draw_rect_filled(300, 190, 20, 10, WHITE, 255);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10)) << "setup: the top-left fill must land";
    ASSERT_EQ(pal_readback_index(WHITE), px_index(310, 195))
        << "setup: the bottom-right fill must land";

    s->clear_window();

    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(10, 10))
        << "clear_window must black the top-left of the surface";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(310, 195))
        << "clear_window must black the far corner too, not a leading rect";
    ASSERT_TRUE(s->window_is_black())
        << "clear_window never presents: the black window stays black";

    // Half 2: a live window stays live -- a wipe under a repaint is not a
    // completed fade-out.
    s->draw_rect_filled(0, 0, 40, 40, WHITE, 255);
    s->swap();
    ASSERT_FALSE(s->window_is_black())
        << "setup: swap() presents the composed canvas and clears the flag";

    s->clear_window();

    ASSERT_FALSE(s->window_is_black())
        << "a background wipe is not a completed fade-out; only fadeblack(false) "
           "may set window_is_black";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(10, 10))
        << "the fill must still run on the live-window path";
}

// draw_rect_filled(x,y,w,h,color,alpha) fills exactly w*h pixels from (x,y);
// alpha 255 takes blend_pixel's opaque shortcut, so the fill reads back as the
// exact palette index (video_sdl.cpp draw_rect_filled -> hor_line_alpha).
TEST(MassCoverage, video_draw_rect_filled_covers_exactly_its_rect) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->draw_rect_filled(10, 10, 20, 10, WHITE, 255);

    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10)) << "the top-left corner is filled";
    ASSERT_EQ(pal_readback_index(WHITE), px_index(15, 15)) << "the interior is filled";
    ASSERT_EQ(pal_readback_index(WHITE), px_index(29, 19))
        << "the last pixel (x+w-1, y+h-1) is filled";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(30, 19))
        << "the column past x+w-1 stays clear";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(15, 20))
        << "the row past y+h-1 stays clear";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(5, 5))
        << "nothing outside the rect is painted";
}

// draw_button(x1,y1,x2,y2,border): top row 15, bottom 11, left 14, right 12,
// and the recursion one pixel in ends on the face colour 13
// (src/platform/sdl/video_sdl.cpp sdl_video::draw_button).
TEST(MassCoverage, video_draw_button_paints_the_raised_bevel_and_face) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->draw_button(20, 20, 59, 39, 1);

    ASSERT_EQ(pal_readback_index(15), px_index(30, 20)) << "top edge is colour 15";
    ASSERT_EQ(pal_readback_index(11), px_index(30, 39)) << "bottom edge is colour 11";
    ASSERT_EQ(pal_readback_index(14), px_index(20, 30)) << "left edge is colour 14";
    ASSERT_EQ(pal_readback_index(12), px_index(59, 30)) << "right edge is colour 12";
    ASSERT_EQ(pal_readback_index(13), px_index(40, 30)) << "the face is colour 13";
    ASSERT_EQ(pal_readback_index(13), px_index(30, 21))
        << "the border-0 recursion fills the row inside the top edge with the face";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(19, 30))
        << "nothing is painted left of x1";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(60, 30))
        << "nothing is painted right of x2";
}

// draw_button_inverted(x,y,w,h) is draw_text_bar, i.e. the SUNKEN bar: face 12,
// top 10, bottom 15, left 11, right 14 -- not the raised bevel with its colours
// swapped (src/platform/sdl/video_sdl.cpp draw_button_inverted -> draw_text_bar).
TEST(MassCoverage, video_draw_button_inverted_paints_the_sunken_text_bar) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->draw_button_inverted(20, 50, 40, 20);

    ASSERT_EQ(pal_readback_index(10), px_index(30, 50)) << "top edge is colour 10";
    ASSERT_EQ(pal_readback_index(15), px_index(30, 69)) << "bottom edge is colour 15";
    ASSERT_EQ(pal_readback_index(11), px_index(20, 60)) << "left edge is colour 11";
    ASSERT_EQ(pal_readback_index(14), px_index(59, 60)) << "right edge is colour 14";
    ASSERT_EQ(pal_readback_index(12), px_index(40, 60)) << "the face is colour 12";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(30, 70))
        << "the bar ends at y+h-1";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(60, 60))
        << "the bar ends at x+w-1";
}

// fastbox_outline(x,y,w,h,color) is draw_box(x,y,x+w,y+h,color,0): the four
// edges of the (x,y)-(x+w,y+h) box and NOTHING inside it
// (src/platform/sdl/video_sdl.cpp sdl_video::fastbox_outline).
TEST(MassCoverage, video_fastbox_outline_draws_edges_and_leaves_the_interior) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->fastbox_outline(2, 2, 8, 8, DARK_GREEN);

    const int green = pal_readback_index(DARK_GREEN);
    ASSERT_EQ(green, px_index(2, 2)) << "top-left corner";
    ASSERT_EQ(green, px_index(10, 10)) << "bottom-right corner sits at (x+w, y+h)";
    ASSERT_EQ(green, px_index(6, 2)) << "top edge";
    ASSERT_EQ(green, px_index(6, 10)) << "bottom edge";
    ASSERT_EQ(green, px_index(2, 6)) << "left edge";
    ASSERT_EQ(green, px_index(10, 6)) << "right edge";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(5, 5))
        << "the outline must not fill the interior";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(11, 11))
        << "nothing is painted past (x+w, y+h)";
}

// point(x,y,color) writes exactly one palette-indexed pixel via pointb
// (src/platform/sdl/video_sdl.cpp sdl_video::point).
TEST(MassCoverage, video_point_writes_exactly_one_pixel) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->point(5, 5, RED);

    ASSERT_EQ(pal_readback_index(RED), px_index(5, 5)) << "the requested pixel takes the colour";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(6, 5)) << "its right neighbour is untouched";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(4, 5)) << "its left neighbour is untouched";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(5, 6)) << "the row below is untouched";
}

// pointb(offset,color) splits a flat buffer offset into (offset % canvas_w,
// offset / canvas_w) (src/platform/sdl/video_sdl.cpp pointb(int, unsigned char)).
TEST(MassCoverage, video_pointb_offset_lands_on_the_row_the_canvas_width_implies) {
    screen* s = og::runtime::current_session->myscreen_;
    const int cw = s->canvas_w();
    s->clearbuffer();

    s->pointb(cw + 2, DARK_BLUE);

    ASSERT_EQ(pal_readback_index(DARK_BLUE), px_index(2, 1))
        << "offset canvas_w+2 is column 2 of row 1";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(2, 0))
        << "row 0 must stay clear -- the offset was not divided by the canvas width";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(3, 1))
        << "only the one pixel is written";
}

// hor_line(x,y,len,color,tobuffer=1) writes len pixels along row y, onto the
// render surface via pointb (src/platform/sdl/video_sdl.cpp hor_line).
TEST(MassCoverage, video_hor_line_tobuffer_spans_exactly_len_pixels_on_one_row) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->hor_line(2, 8, 12, WHITE, 1);

    const int white = pal_readback_index(WHITE);
    const int black = pal_readback_index(PURE_BLACK);
    ASSERT_EQ(white, px_index(2, 8)) << "the span starts at x";
    ASSERT_EQ(white, px_index(13, 8)) << "the span ends at x+len-1";
    ASSERT_EQ(black, px_index(14, 8)) << "the span must not reach x+len";
    ASSERT_EQ(black, px_index(1, 8)) << "the span must not reach x-1";
    ASSERT_EQ(black, px_index(2, 9)) << "the span stays on its own row";
    ASSERT_EQ(black, px_index(2, 7)) << "the row above stays clear";
}

// hor_line_alpha(x,y,len,color,alpha) blends the span into the render surface:
// alpha 255 is the opaque shortcut, alpha 0 leaves the destination alone, and an
// intermediate alpha lands on dst + ((src-dst)*alpha >> 8) per channel
// (src/platform/sdl/video_sdl.cpp hor_line_alpha -> pointb -> blend_pixel).
TEST(MassCoverage, video_hor_line_alpha_blends_the_span_by_its_alpha) {
    screen* s = og::runtime::current_session->myscreen_;
    int wr = 0, wg = 0, wb = 0;
    pal_rgb8(WHITE, &wr, &wg, &wb);

    s->clearbuffer();
    s->hor_line_alpha(2, 8, 12, WHITE, 255);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(2, 8))
        << "alpha 255 writes the source colour unblended";
    ASSERT_EQ(pal_readback_index(WHITE), px_index(13, 8))
        << "the opaque span ends at x+len-1";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(14, 8))
        << "the opaque span must not overrun";

    Uint8 r = 0, g = 0, b = 0;
    s->clearbuffer();
    s->hor_line_alpha(2, 9, 12, WHITE, 96);
    s->get_pixel(5, 9, &r, &g, &b);
    ASSERT_EQ((wr * 96) >> 8, static_cast<int>(r)) << "96/256 of WHITE over black, red channel";
    ASSERT_EQ((wg * 96) >> 8, static_cast<int>(g)) << "96/256 of WHITE over black, green channel";
    ASSERT_EQ((wb * 96) >> 8, static_cast<int>(b)) << "96/256 of WHITE over black, blue channel";
    s->get_pixel(5, 10, &r, &g, &b);
    ASSERT_EQ(0, static_cast<int>(r) + g + b) << "the row below the blended span stays black";

    s->clearbuffer();
    s->draw_rect_filled(0, 11, 20, 1, WHITE, 255);
    s->hor_line_alpha(2, 11, 12, RED, 0);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(5, 11))
        << "alpha 0 must leave the destination pixel exactly as it was";
}

// ver_line(x,y,len,color,tobuffer=1) writes len pixels DOWN column x
// (src/platform/sdl/video_sdl.cpp ver_line).
TEST(MassCoverage, video_ver_line_tobuffer_spans_exactly_len_pixels_down_one_column) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->ver_line(2, 8, 12, WHITE, 1);

    const int white = pal_readback_index(WHITE);
    const int black = pal_readback_index(PURE_BLACK);
    ASSERT_EQ(white, px_index(2, 8)) << "the span starts at y";
    ASSERT_EQ(white, px_index(2, 19)) << "the span ends at y+len-1";
    ASSERT_EQ(black, px_index(2, 20)) << "the span must not reach y+len";
    ASSERT_EQ(black, px_index(2, 7)) << "the span must not reach y-1";
    ASSERT_EQ(black, px_index(3, 8)) << "the span stays in its own column";
}

// do_cycle(0, maxmode) rotates the ORANGE_START..ORANGE_END and
// WATER_START..WATER_END registers up by one, wrapping the old END entry round
// into START (src/platform/sdl/video_sdl.cpp sdl_video::do_cycle).
TEST(MassCoverage, video_do_cycle_rotates_the_orange_and_water_bands_by_one) {
    screen* s = og::runtime::current_session->myscreen_;

    // The rotation mutates process-wide palette state; put it back afterwards
    // so the later pixel-index readbacks in this binary keep their palette.
    std::array<std::array<int, 3>, 256> before{};
    for (int i = 0; i < 256; i++)
        query_palette_reg(static_cast<unsigned char>(i), &before[static_cast<std::size_t>(i)][0],
                          &before[static_cast<std::size_t>(i)][1],
                          &before[static_cast<std::size_t>(i)][2]);

    s->do_cycle(0, 1);

    const auto reg = [](unsigned char i) {
        std::array<int, 3> c{};
        query_palette_reg(i, &c[0], &c[1], &c[2]);
        return c;
    };
    ASSERT_EQ(before[ORANGE_END], reg(ORANGE_START))
        << "the old ORANGE_END entry must wrap round into ORANGE_START";
    ASSERT_EQ(before[ORANGE_END - 1], reg(ORANGE_END))
        << "every orange register must take its predecessor's colour";
    ASSERT_EQ(before[ORANGE_START], reg(ORANGE_START + 1))
        << "the rotation must cover the whole orange band, not just its ends";
    ASSERT_EQ(before[WATER_END], reg(WATER_START))
        << "the old WATER_END entry must wrap round into WATER_START";
    ASSERT_EQ(before[WATER_END - 1], reg(WATER_END))
        << "every water register must take its predecessor's colour";
    ASSERT_EQ(before[100], reg(100))
        << "registers outside the two cycling bands must not move";

    for (int i = 0; i < 256; i++)
        set_palette_reg(static_cast<unsigned char>(i), before[static_cast<std::size_t>(i)][0],
                        before[static_cast<std::size_t>(i)][1],
                        before[static_cast<std::size_t>(i)][2]);
    ASSERT_EQ(before[ORANGE_START], reg(ORANGE_START)) << "teardown: the palette is restored";
}

// do_cycle(curmode != 0, maxmode) does nothing: the rotation is gated on
// curmode % maxmode == 0 (src/platform/sdl/video_sdl.cpp sdl_video::do_cycle).
TEST(MassCoverage, video_do_cycle_off_beat_leaves_the_palette_alone) {
    screen* s = og::runtime::current_session->myscreen_;
    // A do_cycle that rotated off the beat would leave this binary's palette
    // one step out for every later index readback, so the whole palette goes
    // back on the way out whether the assertions below pass or fail.
    ScopedPalette restore_palette;
    std::array<int, 3> before{};
    query_palette_reg(ORANGE_START, &before[0], &before[1], &before[2]);
    std::array<int, 3> water_before{};
    query_palette_reg(WATER_START, &water_before[0], &water_before[1], &water_before[2]);

    s->do_cycle(1, 4);

    std::array<int, 3> after{};
    query_palette_reg(ORANGE_START, &after[0], &after[1], &after[2]);
    ASSERT_EQ(before, after) << "only curmode % maxmode == 0 may rotate the palette";
    std::array<int, 3> water_after{};
    query_palette_reg(WATER_START, &water_after[0], &water_after[1], &water_after[2]);
    ASSERT_EQ(water_before, water_after) << "the water band is gated on the same beat";
}

// putdata(x,y,w,h,pixels) walks the source row-major and draws each NON-ZERO
// index at (x+i, y+j); index 0 is transparent
// (src/platform/sdl/video_sdl.cpp sdl_video::putdata).
TEST(MassCoverage, video_putdata_blits_row_major_with_index_zero_transparent) {
    screen* s = og::runtime::current_session->myscreen_;
    auto px = sample_pixels(50);
    s->clearbuffer();

    s->putdata(10, 10, 8, 8, px);

    ASSERT_EQ(pal_readback_index(50), px_index(10, 10)) << "source[0] lands at (x,y)";
    ASSERT_EQ(pal_readback_index(57), px_index(17, 10))
        << "source[7] lands at the end of the first row";
    ASSERT_EQ(pal_readback_index(50), px_index(10, 11))
        << "source[8] starts the second row back at column x";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(9, 10)) << "nothing lands left of x";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(18, 10)) << "the block is only w wide";

    // Index 0 must be skipped, not painted black over the destination.
    auto holed = sample_pixels(50);
    holed[0] = 0;
    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->putdata(10, 10, 8, 8, holed);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10))
        << "a zero source index must leave the destination pixel alone";
    ASSERT_EQ(pal_readback_index(51), px_index(11, 10))
        << "the neighbouring non-zero index still paints";
}

// putdata_alpha(x,y,w,h,pixels,alpha) blends the block in: alpha 255 is the
// opaque shortcut, alpha 0 leaves the destination untouched
// (src/platform/sdl/video_sdl.cpp putdata_alpha -> pointb -> blend_pixel).
TEST(MassCoverage, video_putdata_alpha_honours_both_ends_of_its_alpha) {
    screen* s = og::runtime::current_session->myscreen_;
    auto px = sample_pixels(60);
    s->clearbuffer();

    s->putdata_alpha(10, 10, 8, 8, px, 255);
    ASSERT_EQ(pal_readback_index(60), px_index(10, 10))
        << "alpha 255 writes source[0] unblended at (x,y)";
    ASSERT_EQ(pal_readback_index(62), px_index(12, 12))
        << "alpha 255 writes source[18] unblended at (x+2,y+2)";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(9, 10))
        << "the block does not spill left of x";

    Uint8 r = 0, g = 0, b = 0;
    s->clearbuffer();
    s->putdata_alpha(10, 10, 8, 8, px, 100);
    s->get_pixel(12, 12, &r, &g, &b);
    int pr = 0, pg = 0, pb = 0;
    pal_rgb8(62, &pr, &pg, &pb);
    ASSERT_EQ((pr * 100) >> 8, static_cast<int>(r)) << "100/256 of the source over black, red";
    ASSERT_EQ((pg * 100) >> 8, static_cast<int>(g)) << "100/256 of the source over black, green";
    ASSERT_EQ((pb * 100) >> 8, static_cast<int>(b)) << "100/256 of the source over black, blue";

    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->putdata_alpha(10, 10, 8, 8, px, 0);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(12, 12))
        << "alpha 0 must leave the destination exactly as it was";
}

// putdata(x,y,w,h,pixels,color): source indices ABOVE 247 are replaced by
// `color`; anything 1..247 keeps its own index
// (src/platform/sdl/video_sdl.cpp putdata with the colour override).
TEST(MassCoverage, video_putdata_color_overrides_only_indices_above_247) {
    screen* s = og::runtime::current_session->myscreen_;

    auto high = sample_pixels(248);  // 248..255, all above the 247 threshold
    s->clearbuffer();
    s->putdata(10, 10, 8, 8, high, DARK_GREEN);
    ASSERT_EQ(pal_readback_index(DARK_GREEN), px_index(10, 10))
        << "a 248 source index takes the override colour";
    ASSERT_EQ(pal_readback_index(DARK_GREEN), px_index(12, 12))
        << "every index above 247 takes the override colour";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(9, 10))
        << "the override blit stays inside its block";

    auto low = sample_pixels(50);  // 50..57, all at or below 247
    s->clearbuffer();
    s->putdata(10, 10, 8, 8, low, DARK_GREEN);
    ASSERT_EQ(pal_readback_index(50), px_index(10, 10))
        << "an index at or below 247 must keep its own colour, not the override";
    ASSERT_EQ(pal_readback_index(57), px_index(17, 10))
        << "the whole low-index row keeps its own colours";
}

// putdatatext(x,y,w,h,pixels,color): index 0 is transparent, source indices
// ABOVE 247 are replaced by `color`, and 1..247 keep their own index
// (src/platform/sdl/video_sdl.cpp putdatatext with the colour override).
TEST(MassCoverage, video_putdatatext_color_overrides_only_indices_above_247) {
    screen* s = og::runtime::current_session->myscreen_;

    auto high = sample_pixels(248);  // 248..255, every index above the threshold
    high[0] = 0;
    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10)) << "setup: the backdrop must land";

    s->putdatatext(10, 10, 8, 8, high, DARK_BLUE);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10))
        << "a zero source index must leave the backdrop showing";
    ASSERT_EQ(pal_readback_index(DARK_BLUE), px_index(11, 10))
        << "a 249 source index takes the override colour";
    ASSERT_EQ(pal_readback_index(DARK_BLUE), px_index(12, 12))
        << "every index above 247 takes the override colour";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(18, 10))
        << "the override blit is only w wide";

    auto low = sample_pixels(70);  // 70..77, all at or below 247
    s->clearbuffer();
    s->putdatatext(10, 10, 8, 8, low, DARK_BLUE);
    ASSERT_EQ(pal_readback_index(70), px_index(10, 10))
        << "an index at or below 247 must keep its own colour, not the override";
    ASSERT_EQ(pal_readback_index(71), px_index(11, 10))
        << "the whole low-index row keeps its own colours";
}

// putbuffer(tile x,y,w,h, port x,y,endx,endy, pixels): an OPAQUE block blit
// clipped to the port window -- a tile starting at or past portendx draws
// nothing at all, and one straddling the right edge is cut at portendx
// (src/platform/sdl/video_sdl.cpp sdl_video::putbuffer).
TEST(MassCoverage, video_putbuffer_blits_the_block_clipped_to_the_port_window) {
    screen* s = og::runtime::current_session->myscreen_;
    auto px = sample_pixels(40);  // 40..47

    s->clearbuffer();
    s->putbuffer(5, 5, 8, 8, 0, 0, 320, 200, px);
    ASSERT_EQ(pal_readback_index(40), px_index(5, 5)) << "source[0] lands at (x,y)";
    ASSERT_EQ(pal_readback_index(44), px_index(9, 5)) << "source[4] lands four columns over";
    ASSERT_EQ(pal_readback_index(40), px_index(5, 6)) << "the second row restarts at column x";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(4, 5)) << "the block does not spill left of x";

    // portendx = 10 cuts the 8-wide tile after column 9.
    s->clearbuffer();
    s->putbuffer(5, 5, 8, 8, 0, 0, 10, 200, px);
    ASSERT_EQ(pal_readback_index(44), px_index(9, 5)) << "the last in-window column is still drawn";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(10, 5))
        << "the right edge must be clipped at portendx";

    // The whole tile is outside the window: nothing is drawn.
    s->clearbuffer();
    s->putbuffer(5, 5, 8, 8, 0, 0, 4, 4, px);
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(5, 5))
        << "a tile at or past portendx must draw nothing";
}

// putbuffer_alpha(..., alpha) blends the clipped block onto the render surface
// through pointb's alpha path; alpha 0 leaves the destination untouched
// (src/platform/sdl/video_sdl.cpp sdl_video::putbuffer_alpha).
TEST(MassCoverage, video_putbuffer_alpha_blends_the_block_at_the_given_alpha) {
    screen* s = og::runtime::current_session->myscreen_;
    auto px = sample_pixels(70);  // 70..77, all three channels non-zero

    int pr = 0, pg = 0, pb = 0;
    pal_rgb8(70, &pr, &pg, &pb);
    int kr = 0, kg = 0, kb = 0;
    pal_rgb8(PURE_BLACK, &kr, &kg, &kb);

    s->clearbuffer();
    s->putbuffer_alpha(5, 5, 8, 8, 0, 0, 320, 200, px, 90);
    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(5, 5, &r, &g, &b);
    ASSERT_EQ(alpha_blend8(kr, pr, 90), static_cast<int>(r)) << "90/256 of the source over black, red";
    ASSERT_EQ(alpha_blend8(kg, pg, 90), static_cast<int>(g)) << "90/256 of the source over black, green";
    ASSERT_EQ(alpha_blend8(kb, pb, 90), static_cast<int>(b)) << "90/256 of the source over black, blue";

    s->clearbuffer();
    s->draw_rect_filled(5, 5, 8, 8, WHITE, 255);
    s->putbuffer_alpha(5, 5, 8, 8, 0, 0, 320, 200, px, 0);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(5, 5))
        << "alpha 0 must leave the destination exactly as it was";

    s->clearbuffer();
    s->putbuffer_alpha(5, 5, 8, 8, 0, 0, 4, 4, px, 90);
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(5, 5))
        << "a tile at or past portendx must blend nothing";
}

// putbuffer_surface(..., surface) blits the SDL surface onto the render
// surface, clipped to the port window the same way the indexed form is
// (src/platform/sdl/video_sdl.cpp sdl_video::putbuffer(SDL_Surface*)).
TEST(MassCoverage, video_putbuffer_surface_blits_the_surface_clipped_to_the_port_window) {
    screen* s = og::runtime::current_session->myscreen_;
    SDL_Surface* surf = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_XRGB8888);
    ASSERT_NE(nullptr, surf) << "setup: the source surface must allocate";
    ASSERT_TRUE(SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE))
        << "setup: the source must copy, not blend: " << SDL_GetError();
    ASSERT_TRUE(SDL_FillSurfaceRect(surf, nullptr, SDL_MapSurfaceRGB(surf, 200, 100, 50)))
        << "setup: the source must be filled: " << SDL_GetError();

    s->clearbuffer();
    s->putbuffer_surface(5, 5, 8, 8, 0, 0, 320, 200, surf);
    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(5, 5, &r, &g, &b);
    ASSERT_EQ(200, static_cast<int>(r)) << "the surface's red lands at (x,y)";
    ASSERT_EQ(100, static_cast<int>(g)) << "the surface's green lands at (x,y)";
    ASSERT_EQ(50, static_cast<int>(b)) << "the surface's blue lands at (x,y)";
    s->get_pixel(12, 12, &r, &g, &b);
    ASSERT_EQ(200, static_cast<int>(r)) << "the whole 8x8 block is blitted";
    s->get_pixel(4, 5, &r, &g, &b);
    ASSERT_EQ(0, static_cast<int>(r)) << "the blit does not spill left of x";

    s->clearbuffer();
    s->putbuffer_surface(5, 5, 8, 8, 0, 0, 4, 4, surf);
    s->get_pixel(5, 5, &r, &g, &b);
    ASSERT_EQ(0, static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b))
        << "a surface at or past portendx must draw nothing";

    SDL_DestroySurface(surf);
}

// walkputbuffer(...,teamcolor): index 0 is transparent, source indices ABOVE
// 247 become teamcolor + (255 - index) -- the team ramp -- and 1..247 paint as
// themselves; the block is clipped to the port window
// (src/platform/sdl/video_sdl.cpp sdl_video::walkputbuffer).
TEST(MassCoverage, video_walkputbuffer_team_recolours_only_indices_above_247) {
    screen* s = og::runtime::current_session->myscreen_;

    auto low = sample_pixels(30);  // 30..37
    low[0] = 0;
    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, low, 8);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10))
        << "a zero source index must leave the backdrop showing";
    ASSERT_EQ(pal_readback_index(31), px_index(11, 10))
        << "an index at or below 247 paints as itself, not through the team ramp";
    ASSERT_EQ(pal_readback_index(30), px_index(10, 11))
        << "the second row restarts at column x";

    auto high = sample_pixels(248);  // 248..255
    s->clearbuffer();
    s->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, high, 8);
    ASSERT_EQ(pal_readback_index(15), px_index(10, 10))
        << "248 must become teamcolor + (255 - 248) = 15";
    ASSERT_EQ(pal_readback_index(14), px_index(11, 10))
        << "249 must become teamcolor + (255 - 249) = 14";

    // portendx = 14 cuts the 8-wide sprite after column 13.
    s->clearbuffer();
    s->walkputbuffer(10, 10, 8, 8, 0, 0, 14, 200, low, 8);
    ASSERT_EQ(pal_readback_index(33), px_index(13, 10)) << "the last in-window column is drawn";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(14, 10))
        << "the right edge must be clipped at portendx";
}

// walkputbuffer_flash paints each pixel's palette RGB BRIGHTENED (+100 per
// channel, or 255 once the channel is already above 155) instead of the raw
// palette colour (src/platform/sdl/video_sdl.cpp sdl_video::walkputbuffer_flash).
TEST(MassCoverage, video_walkputbuffer_flash_brightens_every_channel) {
    screen* s = og::runtime::current_session->myscreen_;
    // 8..15 spans the grey ramp: index 8 is dark enough that +100 is visible,
    // index 15 is already past 155 so it has to clamp instead.
    auto px = sample_pixels(8);
    px[0] = 0;

    const auto brighten = [](int channel) { return channel > 155 ? 255 : channel + 100; };
    int dr = 0, dg = 0, db = 0;
    pal_rgb8(8, &dr, &dg, &db);
    ASSERT_GE(155, dr) << "setup: index 8 must be below the clamp so +100 is observable";
    int br = 0, bg = 0, bb = 0;
    pal_rgb8(15, &br, &bg, &bb);
    ASSERT_LT(155, br) << "setup: index 15 must be above the clamp";

    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->walkputbuffer_flash(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10))
        << "a zero source index must leave the backdrop showing";

    s->clearbuffer();
    s->walkputbuffer_flash(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(10, 11, &r, &g, &b);  // source[8] == 8, the second row's first pixel
    ASSERT_EQ(brighten(dr), static_cast<int>(r)) << "the flash blit brightens red by 100";
    ASSERT_EQ(brighten(dg), static_cast<int>(g)) << "the flash blit brightens green by 100";
    ASSERT_EQ(brighten(db), static_cast<int>(b)) << "the flash blit brightens blue by 100";

    s->get_pixel(17, 10, &r, &g, &b);  // source[7] == 15, already past the clamp
    ASSERT_EQ(255, static_cast<int>(r)) << "a channel already above 155 clamps to 255";

    // Control: the plain blit paints the same source pixel unbrightened.
    s->clearbuffer();
    s->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, px, 8);
    Uint8 r2 = 0, g2 = 0, b2 = 0;
    s->get_pixel(10, 11, &r2, &g2, &b2);
    ASSERT_EQ(dr, static_cast<int>(r2)) << "control: the plain blit paints the raw palette red";
    ASSERT_NE(static_cast<int>(r2), static_cast<int>(r))
        << "the flash blit must not paint the same colour as the plain one";
}

// walkputbuffertext: the per-pixel FillSurfaceRect variant text glyphs go
// through -- index 0 transparent, indices above 247 through the team ramp
// (src/platform/sdl/video_sdl.cpp sdl_video::walkputbuffertext).
TEST(MassCoverage, video_walkputbuffertext_blits_with_index_zero_transparent) {
    screen* s = og::runtime::current_session->myscreen_;

    auto low = sample_pixels(30);
    low[0] = 0;
    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->walkputbuffertext(10, 10, 8, 8, 0, 0, 320, 200, low, 8);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10))
        << "a zero source index must leave the backdrop showing";
    ASSERT_EQ(pal_readback_index(31), px_index(11, 10))
        << "an index at or below 247 paints as itself";

    auto high = sample_pixels(248);
    s->clearbuffer();
    s->walkputbuffertext(10, 10, 8, 8, 0, 0, 320, 200, high, 8);
    ASSERT_EQ(pal_readback_index(15), px_index(10, 10))
        << "248 must become teamcolor + (255 - 248) = 15";
    ASSERT_EQ(pal_readback_index(14), px_index(11, 10))
        << "249 must become teamcolor + (255 - 249) = 14";

    s->clearbuffer();
    s->walkputbuffertext(10, 10, 8, 8, 0, 0, 4, 4, low, 8);
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(10, 10))
        << "a block at or past portendx must draw nothing";
}

// walkputbuffertext_alpha is the alpha form of putdatatext(..., color) and
// applies the same 2002 text ink rule before blending: source byte 0 is
// transparent, a font INK byte (>247) lands as `teamcolor`, and any other
// non-zero byte is a literal palette index that keeps itself -- then whatever
// byte came out of that rule is blended at `alpha`
// (src/platform/sdl/video_sdl.cpp text_ink + sdl_video::walkputbuffertext_alpha;
// this is what text::write_char_xy_alpha draws the damage/heal numbers with).
TEST(MassCoverage, video_walkputbuffertext_alpha_applies_the_text_ink_rule_then_blends) {
    screen* s = og::runtime::current_session->myscreen_;
    auto px = sample_pixels(30);  // 30..37
    px[0] = 0;                    // transparent
    px[1] = 251;                  // font ink: must come out as teamcolor
    // px[2] is 32: a literal palette index that must come out as itself.
    ASSERT_EQ(32, static_cast<int>(px[2])) << "px[2] is the literal-byte probe";
    ASSERT_NE(pal_readback_index(RED), pal_readback_index(32))
        << "control: the literal byte and teamcolor are distinguishable";

    // alpha 255 takes blend_pixel's opaque shortcut, so the painted index is
    // exactly the byte the ink rule produced.
    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->walkputbuffertext_alpha(10, 10, 8, 8, 0, 0, 320, 200, px, RED, 255);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10))
        << "a zero source index must leave the backdrop showing";
    ASSERT_EQ(pal_readback_index(RED), px_index(11, 10))
        << "ink (>247) lands as teamcolor";
    ASSERT_EQ(pal_readback_index(32), px_index(12, 10))
        << "a literal byte keeps itself";
    ASSERT_NE(pal_readback_index(RED), px_index(12, 10))
        << "a literal byte is not repainted in teamcolor";

    // A partial alpha blends the rule's output over whatever was there.
    int wr = 0, wg = 0, wb = 0;
    pal_rgb8(WHITE, &wr, &wg, &wb);
    int rr = 0, rg = 0, rb = 0;
    pal_rgb8(RED, &rr, &rg, &rb);
    int lr = 0, lg = 0, lb = 0;
    pal_rgb8(32, &lr, &lg, &lb);
    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->walkputbuffertext_alpha(10, 10, 8, 8, 0, 0, 320, 200, px, RED, 80);
    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(11, 10, &r, &g, &b);
    ASSERT_EQ(alpha_blend8(wr, rr, 80), static_cast<int>(r)) << "80/256 of teamcolor over white, red";
    ASSERT_EQ(alpha_blend8(wg, rg, 80), static_cast<int>(g)) << "80/256 of teamcolor over white, green";
    ASSERT_EQ(alpha_blend8(wb, rb, 80), static_cast<int>(b)) << "80/256 of teamcolor over white, blue";
    s->get_pixel(12, 10, &r, &g, &b);
    ASSERT_EQ(alpha_blend8(wr, lr, 80), static_cast<int>(r)) << "80/256 of the literal byte over white, red";
    ASSERT_EQ(alpha_blend8(wg, lg, 80), static_cast<int>(g)) << "80/256 of the literal byte over white, green";
    ASSERT_EQ(alpha_blend8(wb, lb, 80), static_cast<int>(b)) << "80/256 of the literal byte over white, blue";

    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    s->walkputbuffertext_alpha(10, 10, 8, 8, 0, 0, 320, 200, px, RED, 0);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(11, 10))
        << "alpha 0 must leave the destination exactly as it was";
}

// walkputbuffer(...,OUTLINE_MODE,invisibility,outline,shifttype) paints the
// silhouette's rim in `outline` and the interior in its own colour: every edge
// pixel of the block, plus every TRANSPARENT pixel that touches a solid one
// (src/platform/sdl/video_sdl.cpp sdl_video::walkputbuffer, case OUTLINE_MODE).
TEST(MassCoverage, video_walkputbuffer_outline_mode_rims_the_silhouette_in_the_outline_colour) {
    screen* s = og::runtime::current_session->myscreen_;
    auto solid = sample_pixels(30);  // 30..37, every pixel opaque

    s->clearbuffer();
    s->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, solid, 8, OUTLINE_MODE, 12, RED, SHIFT_LEFT);
    ASSERT_EQ(pal_readback_index(RED), px_index(10, 10)) << "the left column is rim";
    ASSERT_EQ(pal_readback_index(RED), px_index(17, 10)) << "the right column is rim";
    ASSERT_EQ(pal_readback_index(RED), px_index(10, 17)) << "the bottom row is rim";
    ASSERT_EQ(pal_readback_index(31), px_index(11, 11))
        << "an interior pixel keeps its own colour";

    // Control: the plain blit paints that same rim pixel as its source index.
    s->clearbuffer();
    s->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, solid, 8);
    ASSERT_EQ(pal_readback_index(30), px_index(10, 10))
        << "control: without OUTLINE_MODE the corner is the source colour";

    // A single solid pixel at (2,2): its transparent left neighbour becomes rim,
    // a transparent pixel with no solid neighbour stays untouched.
    std::array<unsigned char, 64> speck{};
    speck[2 * 8 + 2] = 30;
    s->clearbuffer();
    s->walkputbuffer(10, 10, 8, 8, 0, 0, 320, 200, speck, 8, OUTLINE_MODE, 12, RED, SHIFT_LEFT);
    ASSERT_EQ(pal_readback_index(30), px_index(12, 12))
        << "the solid interior pixel keeps its own colour";
    ASSERT_EQ(pal_readback_index(RED), px_index(11, 12))
        << "a transparent pixel touching the silhouette becomes rim";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(15, 15))
        << "a transparent pixel with no solid neighbour is left alone";
}

// swap() presents the whole ACTIVE canvas, and every present clears the
// "window shows black" flag. Note the route: screen::swap() goes through the
// platform bridge's present_frame (src/interface/screen.cpp:954,
// src/platform/sdl/game_session.cpp make_sdl_platform_bridge) to
// Screen::swap, which is the single present site (src/platform/sdl/sai2x.cpp:2485).
TEST(MassCoverage, video_swap_presents_the_whole_canvas) {
    screen* s = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(s->window_is_black())
        << "setup: every integration test starts from a black window";

    s->clearbuffer();
    s->draw_rect_filled(0, 0, 40, 40, WHITE, 255);
    s->swap();
    ASSERT_FALSE(s->window_is_black()) << "swap() must present the composed canvas";
}

// screen::swap() presents through PlatformBridge::present_frame and NOWHERE
// else: there is no second present path. Every platform root installs a bridge
// before a screen exists (src/platform/sdl/game_session.cpp
// make_sdl_platform_bridge; src/platform/platform_headless.cpp), so an empty
// bridge means "no present" -- not "fall back to the video layer". A second
// present site is exactly what the fade-ownership rule forbids
// (include/openglad/platform/sai2x.h window_is_black).
TEST(MassCoverage, screen_swap_presents_only_through_the_platform_bridge) {
    screen* s = og::runtime::current_session->myscreen_;

    // Restores the real bridge even if an assertion below aborts the test.
    struct PlatformBridgeGuard final {
        PlatformBridge saved = platform_bridge();
        ~PlatformBridgeGuard() { set_platform_bridge(std::move(saved)); }
    } bridge_guard;

    // Half 1: the installed bridge is the ONLY present site.
    int presents = 0;
    PlatformBridge counting;
    counting.present_frame = [&presents]() { ++presents; };
    set_platform_bridge(std::move(counting));

    s->testing_reset_window_state();
    ASSERT_TRUE(s->window_is_black())
        << "setup: testing_reset_window_state must re-arm the black-window flag";
    s->clearbuffer();
    s->draw_rect_filled(0, 0, 40, 40, WHITE, 255);

    s->swap();
    ASSERT_EQ(1, presents) << "swap() must call the bridge's present_frame exactly once";
    ASSERT_TRUE(s->window_is_black())
        << "nothing may be presented behind the bridge's back: only the real "
           "SDL present (Screen::swap) clears window_is_black";

    // Half 2: with no present_frame there is no present at all.
    set_platform_bridge(PlatformBridge{});
    s->testing_reset_window_state();
    ASSERT_TRUE(s->window_is_black()) << "setup: the flag is re-armed before the empty-bridge swap";

    s->swap();
    ASSERT_EQ(1, presents) << "the replaced bridge must not still be called";
    ASSERT_TRUE(s->window_is_black())
        << "with no present_frame installed swap() must present nothing (no "
           "sdl_video::swap fallback)";
}

// get_pixel(x,y,&r,&g,&b) answers with the surface RGB (the 6-bit palette
// register scaled by 4) and zeroes the whole trio out of bounds
// (src/platform/sdl/video_sdl.cpp sdl_video::get_pixel).
TEST(MassCoverage, video_get_pixel_rgb_reads_the_palette_rgb_and_zeroes_out_of_bounds) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();
    s->point(1, 1, WHITE);

    int pr = 0, pg = 0, pb = 0;
    pal_rgb8(WHITE, &pr, &pg, &pb);
    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(1, 1, &r, &g, &b);
    ASSERT_EQ(pr, static_cast<int>(r)) << "the painted pixel's red is the palette register times 4";
    ASSERT_EQ(pg, static_cast<int>(g)) << "the painted pixel's green is the palette register times 4";
    ASSERT_EQ(pb, static_cast<int>(b)) << "the painted pixel's blue is the palette register times 4";

    int kr = 0, kg = 0, kb = 0;
    pal_rgb8(PURE_BLACK, &kr, &kg, &kb);
    s->get_pixel(2, 1, &r, &g, &b);
    ASSERT_EQ(kr, static_cast<int>(r)) << "an unpainted neighbour reads back as the cleared colour";

    r = 7;
    g = 7;
    b = 7;
    s->get_pixel(-1, -1, &r, &g, &b);
    ASSERT_EQ(0, static_cast<int>(r)) << "an out-of-bounds read must zero red";
    ASSERT_EQ(0, static_cast<int>(g)) << "an out-of-bounds read must zero green";
    ASSERT_EQ(0, static_cast<int>(b)) << "an out-of-bounds read must zero blue";
}

// get_pixel(x,y,&index) reverse-maps the surface RGB to the first palette
// register with that RGB, returning it AND writing the out-parameter
// (src/platform/sdl/video_sdl.cpp sdl_video::get_pixel(int,int,int*)).
TEST(MassCoverage, video_get_pixel_index_returns_and_writes_the_palette_index) {
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();
    s->point(1, 1, DARK_GREEN);

    int idx = -1;
    ASSERT_EQ(pal_readback_index(DARK_GREEN), s->get_pixel(1, 1, &idx))
        << "the painted pixel must reverse-map to its palette index";
    ASSERT_EQ(pal_readback_index(DARK_GREEN), idx)
        << "the out-parameter must carry the same index the call returned";

    idx = -1;
    ASSERT_EQ(pal_readback_index(PURE_BLACK), s->get_pixel(2, 1, &idx))
        << "an unpainted neighbour reverse-maps to the cleared colour";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), idx) << "...in the out-parameter too";
}

// get_pixel(offset) splits a linear offset by the ACTIVE canvas width and
// rejects offsets outside the surface (src/platform/sdl/video_sdl.cpp
// sdl_video::get_pixel(int)).
TEST(MassCoverage, video_get_pixel_offset_splits_the_offset_by_the_canvas_width) {
    screen* s = og::runtime::current_session->myscreen_;
    const int cw = s->canvas_w();
    s->clearbuffer();
    s->point(1, 1, RED);
    s->point(3, 2, DARK_GREEN);

    ASSERT_EQ(pal_readback_index(RED), s->get_pixel(cw + 1))
        << "offset cw+1 must be row 1, column 1";
    ASSERT_EQ(pal_readback_index(DARK_GREEN), s->get_pixel(2 * cw + 3))
        << "offset 2*cw+3 must be row 2, column 3";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), s->get_pixel(1))
        << "offset 1 is row 0, column 1 -- still unpainted";

    // The row split is the whole rule, so it is pinned twice more, once with a
    // painted pixel at the END of a row and once at the start of the next: an
    // off-by-one in the width would swap these two readings.
    s->point(cw - 1, 4, WHITE);
    s->point(0, 5, YELLOW);
    ASSERT_EQ(pal_readback_index(WHITE), s->get_pixel(5 * cw - 1))
        << "offset 5*cw-1 is the last column of row 4";
    ASSERT_EQ(pal_readback_index(YELLOW), s->get_pixel(5 * cw))
        << "offset 5*cw is the first column of row 5";

    // NOTE (test-teeth audit): the out-of-range arms (offset < 0, offset >=
    // w*h) used to be pinned here as `ASSERT_EQ(0, ...)`. They cannot fail:
    // with the guard removed, get_pixel(x,y) bounds-checks the same read and
    // answers with the index of black, which IS 0. There is no observable that
    // separates the guard from its absence through this API, so the
    // documentation-strength assertions are gone rather than pretending.
}
// save_screenshot() composes the frame, writes it to the user write directory
// and reports whether the write succeeded (src/platform/sdl/video_sdl.cpp
// sdl_video::save_screenshot).
TEST(MassCoverage, video_save_screenshot_writes_one_image_and_reports_success) {
    screen* s = og::runtime::current_session->myscreen_;
    const std::filesystem::path dir(get_user_path());
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::exists(dir, ec))
        << "setup: the per-run write directory must exist: " << dir.string();

    const auto shots = [&dir]() {
        std::vector<std::string> out;
        std::error_code iter_ec;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(dir, iter_ec))
        {
            const std::string name = entry.path().filename().string();
            if (name.rfind("screenshot", 0) == 0)
                out.push_back(name);
        }
        std::sort(out.begin(), out.end());
        return out;
    };

    const std::vector<std::string> before = shots();
    ASSERT_TRUE(s->save_screenshot()) << "save_screenshot must report the write succeeded";

    const std::vector<std::string> after = shots();
    ASSERT_EQ(before.size() + 1, after.size())
        << "exactly one new screenshot must appear in " << dir.string();

    // Never leave proof media behind (AGENTS.md, "PR screenshots").
    for (const std::string& name : after)
    {
        if (std::find(before.begin(), before.end(), name) == before.end())
            std::filesystem::remove(dir / name, ec);
    }
    ASSERT_EQ(before, shots()) << "the case must leave the write directory as it found it";
}

// FadeBetween24(surface,from,to,amount) writes
// ((fadeDuration-amount)*from + amount*to)/fadeDuration per channel into the
// surface, with fadeDuration = 500 (src/platform/sdl/video_sdl.cpp
// sdl_video::FadeBetween24; the constant is set in both sdl_video ctors).
TEST(MassCoverage, video_fade_between24_mixes_the_two_frames_by_amount) {
    screen* scr = og::runtime::current_session->myscreen_;
    constexpr int kFadeDuration = 500;
    SDL_Surface* s = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    ASSERT_NE(nullptr, s) << "setup: the fade target must allocate";
    ASSERT_EQ(4 * 4 * 4, s->pitch * s->h) << "setup: the mix buffers must match the surface";

    std::array<Uint8, 4 * 4 * 4> from{};
    std::array<Uint8, 4 * 4 * 4> to{};
    to.fill(0xFF);

    Uint8 r = 0, g = 0, b = 0, a = 0;
    scr->fade_between24(s, from.data(), to.data(), 10);
    ASSERT_TRUE(SDL_ReadSurfacePixel(s, 0, 0, &r, &g, &b, &a)) << SDL_GetError();
    ASSERT_EQ(255 * 10 / kFadeDuration, static_cast<int>(r))
        << "amount 10 of 500 must give 10/500 of the destination frame";
    ASSERT_EQ(255 * 10 / kFadeDuration, static_cast<int>(g)) << "...on every channel";
    ASSERT_TRUE(SDL_ReadSurfacePixel(s, 3, 3, &r, &g, &b, &a)) << SDL_GetError();
    ASSERT_EQ(255 * 10 / kFadeDuration, static_cast<int>(r)) << "...and for every pixel";

    scr->fade_between24(s, from.data(), to.data(), kFadeDuration);
    ASSERT_TRUE(SDL_ReadSurfacePixel(s, 0, 0, &r, &g, &b, &a)) << SDL_GetError();
    ASSERT_EQ(255, static_cast<int>(r)) << "a full amount must land on the destination frame";

    from.fill(0x80);
    scr->fade_between24(s, from.data(), to.data(), 0);
    ASSERT_TRUE(SDL_ReadSurfacePixel(s, 0, 0, &r, &g, &b, &a)) << SDL_GetError();
    ASSERT_EQ(0x80, static_cast<int>(r)) << "amount 0 must leave the source frame";

    SDL_DestroySurface(s);
}

// fade_between(from,to,dest) under TESTING runs no animation: it blits `to`
// into dest (and into `from`, the historical contract), traces the skip and
// returns 1 (src/platform/sdl/video_sdl.cpp FadeBetween, the #ifdef TESTING
// branch -- the reason menu tests settle on frames instead of waiting on fades).
TEST(MassCoverage, video_fade_between_lands_on_the_new_frame_without_animating) {
    screen* s = og::runtime::current_session->myscreen_;
    SDL_Surface* from = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    SDL_Surface* to = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    SDL_Surface* dest = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    ASSERT_NE(nullptr, from) << "setup: the old frame must allocate";
    ASSERT_NE(nullptr, to) << "setup: the new frame must allocate";
    ASSERT_NE(nullptr, dest) << "setup: the destination must allocate";
    ASSERT_TRUE(SDL_FillSurfaceRect(from, nullptr, SDL_MapSurfaceRGB(from, 0, 0, 0)))
        << SDL_GetError();
    ASSERT_TRUE(SDL_FillSurfaceRect(to, nullptr, SDL_MapSurfaceRGB(to, 255, 0, 255)))
        << SDL_GetError();
    ASSERT_TRUE(SDL_FillSurfaceRect(dest, nullptr, SDL_MapSurfaceRGB(dest, 0, 0, 0)))
        << SDL_GetError();

    trace_clear();
    ASSERT_EQ(1, s->fade_between(from, to, dest)) << "an uninterrupted fade reports 1";
    ASSERT_TRUE(trace_contains("video", "FadeBetween: skipping animation (test mode)"))
        << "under TESTING the fade must be a single blit, not an animation";

    Uint8 r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(dest, 0, 0, &r, &g, &b, &a)) << SDL_GetError();
    ASSERT_EQ(255, static_cast<int>(r)) << "the destination must end on the new frame's red";
    ASSERT_EQ(0, static_cast<int>(g)) << "the destination must end on the new frame's green";
    ASSERT_EQ(255, static_cast<int>(b)) << "the destination must end on the new frame's blue";

    ASSERT_TRUE(SDL_ReadSurfacePixel(from, 0, 0, &r, &g, &b, &a)) << SDL_GetError();
    ASSERT_EQ(255, static_cast<int>(r))
        << "the old surface advances to the new frame too (the historical contract)";

    SDL_DestroySurface(from);
    SDL_DestroySurface(to);
    SDL_DestroySurface(dest);
}

TEST(MassCoverage, video_fadeblack)
{
    // The test boundary leaves the window black: a fade-out there has
    // nothing to fade and is skipped (0). Present a frame so there is
    // something to fade out, then out (1), then in (1).
    screen& scr = *og::runtime::current_session->myscreen_;
    ASSERT_EQ(0, scr.fadeblack(false)) << "idempotent on a black window";
    scr.buffer_to_screen(0, 0, scr.canvas_w(), scr.canvas_h());
    ASSERT_EQ(1, scr.fadeblack(false));
    ASSERT_EQ(1, scr.fadeblack(true));
}
// darken_screen() blends PURE_BLACK at alpha 100 over EVERY pixel of the
// active canvas (src/platform/sdl/video_sdl.cpp sdl_video::darken_screen).
TEST(MassCoverage, video_darken_screen_blends_black_over_the_whole_canvas) {
    screen* s = og::runtime::current_session->myscreen_;
    const int cw = s->canvas_w();
    const int ch = s->canvas_h();
    s->clearbuffer();
    s->draw_rect_filled(0, 0, static_cast<Uint32>(cw), static_cast<Uint32>(ch), WHITE, 255);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(cw - 1, ch - 1))
        << "setup: the backdrop must cover the far corner";

    int wr = 0, wg = 0, wb = 0;
    pal_rgb8(WHITE, &wr, &wg, &wb);
    int kr = 0, kg = 0, kb = 0;
    pal_rgb8(PURE_BLACK, &kr, &kg, &kb);
    const int expect_r = alpha_blend8(wr, kr, 100);
    const int expect_g = alpha_blend8(wg, kg, 100);
    const int expect_b = alpha_blend8(wb, kb, 100);
    ASSERT_LT(expect_r, wr) << "setup: the blend must be a darkening, not a no-op";

    s->darken_screen();

    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(10, 10, &r, &g, &b);
    ASSERT_EQ(expect_r, static_cast<int>(r)) << "100/256 of black over the backdrop, red";
    ASSERT_EQ(expect_g, static_cast<int>(g)) << "100/256 of black over the backdrop, green";
    ASSERT_EQ(expect_b, static_cast<int>(b)) << "100/256 of black over the backdrop, blue";

    s->get_pixel(cw - 1, ch - 1, &r, &g, &b);
    ASSERT_EQ(expect_r, static_cast<int>(r))
        << "the far corner must be darkened too -- the pass covers the whole canvas";
}

// text.cpp
// text_shutdown() frees the shared letters1/letters_big pixies: PixieData
// becomes invalid, safe_glyph_span answers {} and every glyph write is a no-op
// that returns 0 -- until the next text ctor lazily reloads both
// (src/interface/render/text.cpp:48-67, 70-84, 109-113; pixie_data.cpp free()).
TEST(MassCoverage, text_shutdown_frees_the_shared_font_until_a_text_ctor_reloads_it) {
    screen* s = og::runtime::current_session->myscreen_;
    ensure_font_loaded();
    const int sx = s->text_normal.sizex;
    const int sy = s->text_normal.sizey;
    ASSERT_EQ(5, sx) << "setup: the shipped small font is 5 wide";
    ASSERT_EQ(6, sy) << "setup: the shipped small font is 6 tall";

    s->clearbuffer();
    ASSERT_EQ(1, s->text_normal.write_char_xy(5, 5, 'A', WHITE))
        << "setup: a loaded font reports the glyph painted";
    ASSERT_EQ(kGlyphInkA, count_index_in(5, 5, sx + 2, sy + 2, WHITE))
        << "setup: the 'A' glyph's ink";

    text_shutdown();

    s->clearbuffer();
    ASSERT_EQ(0, s->text_normal.write_char_xy(5, 5, 'A', WHITE))
        << "a freed font has no glyph span, so the write reports nothing painted";
    ASSERT_EQ(0, count_index_in(5, 5, sx + 2, sy + 2, WHITE))
        << "...and no pixel is touched";

    // The rest of this binary draws text: the shared pixies must come back, and
    // reloading them here is exactly how the product recovers.
    text reloaded(TEXT_1);
    s->clearbuffer();
    ASSERT_EQ(1, s->text_normal.write_char_xy(5, 5, 'A', WHITE))
        << "a later text ctor must reload the shared font";
    ASSERT_EQ(kGlyphInkA, count_index_in(5, 5, sx + 2, sy + 2, WHITE))
        << "the reloaded font paints the same glyph";
}

// write_xy(x,y,str,color) paints glyph i at x + i*(sizex+1) in `color`
// (src/interface/render/text.cpp write_xy).
TEST(MassCoverage, text_write_xy_lays_each_glyph_one_advance_apart_in_the_given_colour) {
    screen* s = og::runtime::current_session->myscreen_;
    ensure_font_loaded();
    const int sx = s->text_normal.sizex;
    const int sy = s->text_normal.sizey;
    const int box_w = 4 * (sx + 1) + 4;

    s->clearbuffer();
    ASSERT_EQ(1, s->text_normal.write_xy(5, 5, "mass", WHITE)) << "write_xy reports 1";
    const std::vector<int> painted = snapshot_indices(3, 5, box_w, sy);
    ASSERT_EQ(kWordInkMass, count_index_in(3, 5, box_w, sy, WHITE))
        << "the four glyphs' ink must be painted in the requested colour";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(3, 5))
        << "nothing is painted left of x";

    // Golden by reconstruction: the same pixels as four single-glyph writes at
    // x + i*(sizex+1). A wrong advance, a wrong colour or a missing glyph all
    // break this.
    s->clearbuffer();
    const char* word = "mass";
    for (int i = 0; i < 4; i++)
    {
        ASSERT_EQ(1, s->text_normal.write_char_xy(5 + i * (sx + 1), 5, word[i], WHITE))
            << "setup: glyph " << i << " must paint";
    }
    ASSERT_EQ(snapshot_indices(3, 5, box_w, sy), painted)
        << "write_xy must advance by sizex+1 per glyph";

    s->clearbuffer();
    s->text_normal.write_xy(5, 5, "mass", DARK_GREEN);
    ASSERT_EQ(kWordInkMass, count_index_in(3, 5, box_w, sy, DARK_GREEN))
        << "the colour argument reaches the glyph pixels";
    ASSERT_EQ(0, count_index_in(3, 5, box_w, sy, WHITE))
        << "...and nothing is left in the previous colour";
}

// write_xy(x,y,color,fmt,...) formats through vsnprintf, paints via
// write_formatted and returns len*(sizex+1); a null format paints nothing and
// returns 0 (src/interface/render/text.cpp TEXT_VFORMAT, write_formatted).
TEST(MassCoverage, text_write_xy_printf_expands_the_format_and_returns_the_advance) {
    screen* s = og::runtime::current_session->myscreen_;
    ensure_font_loaded();
    const int sx = s->text_normal.sizex;
    const int sy = s->text_normal.sizey;

    s->clearbuffer();
    ASSERT_EQ(2 * (sx + 1), s->text_normal.write_xy(5, 12, WHITE, "%d", 42))
        << "the printf form returns strlen(formatted) * (sizex+1)";
    const std::vector<int> formatted = snapshot_indices(5, 12, 2 * (sx + 1), sy);
    ASSERT_EQ(kWordInk42, count_index_in(5, 12, 2 * (sx + 1), sy, WHITE))
        << "the formatted digits must be painted";

    // "%d" with 42 has to produce exactly the glyphs of the literal "42".
    s->clearbuffer();
    s->text_normal.write_xy(5, 12, "42", WHITE);
    ASSERT_EQ(snapshot_indices(5, 12, 2 * (sx + 1), sy), formatted)
        << "%d must expand to the digits, not paint the format string";

    s->clearbuffer();
    const char* no_format = nullptr;
    ASSERT_EQ(0, s->text_normal.write_xy(5, 12, WHITE, no_format))
        << "a null format string must return 0";
    ASSERT_EQ(0, count_index_in(5, 12, 2 * (sx + 1), sy, WHITE))
        << "...and paint nothing";
}

// write_xy_shadow paints each glyph twice: a (x-1,y+1) copy in PURE_BLACK+2
// first, the glyph in `color` on top (src/interface/render/text.cpp
// write_formatted, shadow branch).
TEST(MassCoverage, text_write_xy_shadow_paints_an_offset_copy_under_each_glyph) {
    screen* s = og::runtime::current_session->myscreen_;
    ensure_font_loaded();
    const int sx = s->text_normal.sizex;
    const int sy = s->text_normal.sizey;
    const int box_w = 6 * (sx + 1) + 2;
    const int box_h = sy + 2;
    constexpr unsigned char kShadowColor = static_cast<unsigned char>(PURE_BLACK + 2);

    s->clearbuffer();
    ASSERT_EQ(6 * (sx + 1), s->text_normal.write_xy_shadow(5, 20, WHITE, "%s", "shadow"))
        << "the shadowed form returns the same advance as the plain one";
    const std::vector<int> shadowed = snapshot_indices(4, 20, box_w, box_h);
    ASSERT_EQ(kWordInkShadowGlyph, count_index_in(4, 20, box_w, box_h, WHITE))
        << "the glyph pass paints in the requested colour";
    ASSERT_EQ(kWordInkShadowShade, count_index_in(4, 20, box_w, box_h, kShadowColor))
        << "the shadow pass paints in PURE_BLACK+2 (what the glyph pass does not cover)";

    // Golden by reconstruction: shadow string at (x-1,y+1), glyphs on top.
    s->clearbuffer();
    s->text_normal.write_xy(4, 21, "shadow", kShadowColor);
    s->text_normal.write_xy(5, 20, "shadow", WHITE);
    ASSERT_EQ(snapshot_indices(4, 20, box_w, box_h), shadowed)
        << "the shadow must sit one left and one down from the glyph, under it";
}

// write_xy_center(cx,y,...) starts the string at cx - len*(sizex+1)/2 and
// returns 1 (src/interface/render/text.cpp write_formatted, center branch).
TEST(MassCoverage, text_write_xy_center_starts_half_the_string_left_of_the_anchor) {
    screen* s = og::runtime::current_session->myscreen_;
    ensure_font_loaded();
    const int sx = s->text_normal.sizex;
    const int sy = s->text_normal.sizey;
    const int base = 80 - 6 * (sx + 1) / 2;

    s->clearbuffer();
    ASSERT_EQ(1, s->text_normal.write_xy_center(80, 40, WHITE, "%s", "center"))
        << "the centered form returns 1";
    const std::vector<int> centered = snapshot_indices(0, 40, 160, sy);
    ASSERT_EQ(kWordInkCenter, count_index_in(0, 40, 160, sy, WHITE))
        << "the six glyphs' ink must be painted";
    ASSERT_EQ(0, count_index_in(0, 40, base, sy, WHITE))
        << "nothing may be painted left of cx - len*(sizex+1)/2";

    s->clearbuffer();
    s->text_normal.write_xy(base, 40, "center", WHITE);
    ASSERT_EQ(snapshot_indices(0, 40, 160, sy), centered)
        << "the centered run must be the plain run started at the computed base";
}

namespace {

// Ink goldens for the remaining text entry points, measured off the shipped
// 5x6 font (data/text.png). Two ramps exist and they differ, so the counts
// differ too:
//   * putdatatext (the direct write_char_xy path) maps EVERY source index
//     above 247 to the requested colour, so the whole glyph reads back as one
//     colour;
//   * walkputbuffertext (the to_buffer / viewscreen path) maps index i>247 to
//     colour + (255 - i), so only the index-255 pixels read back as the
//     requested colour and the rest climb the palette above it.
// A blit that paints nothing, paints the wrong colour, takes the wrong ramp or
// lays the glyphs at the wrong pitch all read differently.
inline constexpr int kWordInkAlpha = 54;              // "alpha", centred
inline constexpr int kWordInkCenterShadowGlyph = 140; // "center-shadow", glyph pass
inline constexpr int kWordInkCenterShadowShade = 116; // "center-shadow", shadow pass
inline constexpr int kWordInkDefault = 74;            // "default", direct ramp
inline constexpr int kWordInkBufRamp = 9;             // "buf", to_buffer ramp
inline constexpr int kWordInkViewRamp = 11;           // "view", viewscreen ramp
inline constexpr int kWordInkYcWhite = 18;            // "yc", direct ramp
inline constexpr int kWordInkYdDefault = 19;          // "yd", direct ramp
inline constexpr int kWordInkYbRamp = 5;              // "yb", to_buffer ramp
inline constexpr int kWordInkYvcRamp = 8;             // "yvc", viewscreen ramp
inline constexpr int kWordInkYvRamp = 4;              // "yv", viewscreen ramp
inline constexpr int kGlyphInkQRamp = 2;              // 'Q', to_buffer ramp
inline constexpr int kGlyphInkRRamp = 3;              // 'R', to_buffer ramp

// The shared font pixies are freed by text_shutdown(), and screen::text_normal
// caches the glyph box it was constructed with. ensure_font_loaded() puts the
// pixies back but does NOT refresh that cache, and write_y() computes its
// centred x from the CACHED sizex before write_xy() gets a chance to
// sync_geometry(). query_width() syncs, so call it first whenever the test
// reads sizex/sizey or relies on write_y's centring.
text& synced_text()
{
    ensure_font_loaded();
    text& t = og::runtime::current_session->myscreen_->text_normal;
    (void)t.query_width("x");  // sync_geometry()
    return t;
}

// viewob[0] is laid out full-canvas, so a view-relative write lands exactly
// where the same absolute write would and proves nothing. These cases push the
// view origin off (0,0) for the duration and put it back even when an
// assertion fires.
struct ScopedViewOffset
{
    viewscreen* view;
    Sint32 saved_x;
    Sint32 saved_y;

    ScopedViewOffset(viewscreen* v, Sint32 x, Sint32 y)
        : view(v), saved_x(v->xloc), saved_y(v->yloc)
    {
        view->xloc = x;
        view->yloc = y;
    }
    ~ScopedViewOffset()
    {
        view->xloc = saved_x;
        view->yloc = saved_y;
    }
};

// How many pixels of the whole canvas are not the cleared background.
int nonblack_on_canvas()
{
    screen* s = og::runtime::current_session->myscreen_;
    const int black = pal_readback_index(PURE_BLACK);
    int n = 0;
    for (int y = 0; y < s->canvas_h(); y++)
        for (int x = 0; x < s->canvas_w(); x++)
            if (px_index(x, y) != black)
                n++;
    return n;
}

// Count of, and bounding box over, the canvas pixels reading back as `index`.
struct IndexExtent
{
    int count = 0;
    int minx = -1;
    int maxx = -1;
    int miny = -1;
    int maxy = -1;
};

IndexExtent index_extent_on_canvas(int index)
{
    screen* s = og::runtime::current_session->myscreen_;
    IndexExtent e;
    for (int y = 0; y < s->canvas_h(); y++)
        for (int x = 0; x < s->canvas_w(); x++)
            if (px_index(x, y) == index)
            {
                if (e.count == 0)
                {
                    e.minx = e.maxx = x;
                    e.miny = e.maxy = y;
                }
                else
                {
                    e.minx = std::min(e.minx, x);
                    e.maxx = std::max(e.maxx, x);
                    e.miny = std::min(e.miny, y);
                    e.maxy = std::max(e.maxy, y);
                }
                e.count++;
            }
    return e;
}

} // namespace

// write_xy_center_alpha(cx,y,color,alpha,...) centres like write_xy_center and
// blends every glyph pixel through write_char_xy_alpha -> pointb, so the ink
// comes out at dest + ((src-dest)*alpha >> 8) rather than in `color`
// (src/interface/render/text.cpp write_formatted, use_alpha branch).
TEST(MassCoverage, text_write_xy_center_alpha_blends_the_centred_glyphs_at_the_given_alpha) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int base = 80 - 5 * (sx + 1) / 2;

    int wr = 0, wg = 0, wb = 0;
    pal_rgb8(WHITE, &wr, &wg, &wb);
    int kr = 0, kg = 0, kb = 0;
    pal_rgb8(PURE_BLACK, &kr, &kg, &kb);
    const int er = alpha_blend8(kr, wr, 100);
    const int eg = alpha_blend8(kg, wg, 100);
    const int eb = alpha_blend8(kb, wb, 100);

    auto count_blended = [&](int x0, int w) {
        int n = 0;
        for (int j = 0; j < sy; j++)
            for (int i = 0; i < w; i++)
            {
                Uint8 r = 0, g = 0, b = 0;
                s->get_pixel(x0 + i, 46 + j, &r, &g, &b);
                if (static_cast<int>(r) == er && static_cast<int>(g) == eg &&
                    static_cast<int>(b) == eb)
                    n++;
            }
        return n;
    };

    s->clearbuffer();
    ASSERT_EQ(1, t.write_xy_center_alpha(80, 46, WHITE, 100, "%s", "alpha"))
        << "the centered form returns 1";
    ASSERT_EQ(kWordInkAlpha, count_blended(0, 160))
        << "every glyph pixel must come out at 100/256 of WHITE over the cleared ground";
    ASSERT_EQ(0, count_index_in(0, 46, 160, sy, WHITE))
        << "the alpha path must blend, never paint the colour flat";
    ASSERT_EQ(0, count_blended(0, base))
        << "nothing may be painted left of cx - len*(sizex+1)/2";

    // alpha 255 is the opaque case: the same pixels write_xy_center paints.
    s->clearbuffer();
    ASSERT_EQ(1, t.write_xy_center_alpha(80, 46, WHITE, 255, "%s", "alpha"))
        << "the centered form returns 1";
    const std::vector<int> opaque = snapshot_indices(0, 46, 160, sy);
    ASSERT_EQ(kWordInkAlpha, count_index_in(0, 46, 160, sy, WHITE))
        << "at alpha 255 the glyphs land flat in the requested colour";
    s->clearbuffer();
    t.write_xy_center(80, 46, WHITE, "%s", "alpha");
    ASSERT_EQ(snapshot_indices(0, 46, 160, sy), opaque)
        << "alpha 255 must be the plain centered run, pixel for pixel";

    s->clearbuffer();
    ASSERT_EQ(1, t.write_xy_center_alpha(80, 46, WHITE, 0, "%s", "alpha"))
        << "the centered form returns 1";
    ASSERT_EQ(0, nonblack_on_canvas())
        << "alpha 0 must leave the destination exactly as it was";
}

// write_xy_center_shadow(cx,y,color,...) centres AND takes write_formatted's
// shadow branch: a (x-1,y+1) copy in PURE_BLACK+2 under every glyph
// (src/interface/render/text.cpp write_formatted).
TEST(MassCoverage, text_write_xy_center_shadow_centres_and_shadows_every_glyph) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int base = 80 - 13 * (sx + 1) / 2;  // "center-shadow" is 13 glyphs
    constexpr unsigned char kShadowColor = static_cast<unsigned char>(PURE_BLACK + 2);

    s->clearbuffer();
    ASSERT_EQ(1, t.write_xy_center_shadow(80, 52, WHITE, "%s", "center-shadow"))
        << "the centered form returns 1";
    const std::vector<int> shadowed = snapshot_indices(0, 52, 160, sy + 2);
    ASSERT_EQ(kWordInkCenterShadowGlyph, count_index_in(0, 52, 160, sy + 2, WHITE))
        << "the glyph pass paints in the requested colour";
    ASSERT_EQ(kWordInkCenterShadowShade,
              count_index_in(0, 52, 160, sy + 2, kShadowColor))
        << "the shadow pass paints in PURE_BLACK+2 where the glyph pass does not cover";
    ASSERT_EQ(0, count_index_in(0, 52, base - 1, sy + 2, WHITE))
        << "nothing may be painted left of cx - len*(sizex+1)/2";

    // Golden by reconstruction: the shadow string one left and one down, the
    // centred glyphs on top.
    s->clearbuffer();
    t.write_xy(base - 1, 53, "center-shadow", kShadowColor);
    t.write_xy(base, 52, "center-shadow", WHITE);
    ASSERT_EQ(snapshot_indices(0, 52, 160, sy + 2), shadowed)
        << "the shadowed centred run must be the plain run at the computed base,"
           " over its offset copy";
}

// write_xy(x,y,str) is the default-colour overload: it forwards to the
// colour form with DEFAULT_TEXT_COLOR (src/interface/render/text.cpp).
TEST(MassCoverage, text_write_xy_default_paints_in_the_default_text_colour) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int box_w = 7 * (sx + 1);  // "default" is 7 glyphs

    s->clearbuffer();
    ASSERT_EQ(1, t.write_xy(5, 58, "default")) << "write_xy returns 1";
    const std::vector<int> painted = snapshot_indices(5, 58, box_w, sy);
    ASSERT_EQ(kWordInkDefault,
              count_index_in(5, 58, box_w, sy, DEFAULT_TEXT_COLOR))
        << "the glyphs must land in DEFAULT_TEXT_COLOR";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(4, 58))
        << "the run must start at x, not left of it";

    s->clearbuffer();
    t.write_xy(5, 58, "default", static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
    ASSERT_EQ(snapshot_indices(5, 58, box_w, sy), painted)
        << "the default overload must be the explicit DEFAULT_TEXT_COLOR run";
}

// write_xy(x,y,str,to_buffer=1) walks the string at a (sizex+1) pitch through
// write_char_xy(...,to_buffer), i.e. walkputbuffertext -- whose ramp turns a
// source index i>247 into colour + (255-i), so only the index-255 pixels read
// back as DEFAULT_TEXT_COLOR. It returns the ADVANCE, not 1
// (src/interface/render/text.cpp, small-font branch).
TEST(MassCoverage, text_write_xy_tobuffer_walks_the_string_through_the_buffer_ramp) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int box_w = 3 * (sx + 1);  // "buf" is 3 glyphs

    s->clearbuffer();
    ASSERT_EQ(3 * (sx + 1), t.write_xy(5, 64, "buf", static_cast<short>(1)))
        << "the to_buffer form returns the advance, len*(sizex+1)";
    const std::vector<int> buffered = snapshot_indices(5, 64, box_w, sy);
    ASSERT_EQ(kWordInkBufRamp,
              count_index_in(5, 64, box_w, sy, DEFAULT_TEXT_COLOR))
        << "the buffer ramp puts only the index-255 pixels in DEFAULT_TEXT_COLOR";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(4, 64))
        << "the run must start at x, not left of it";

    // Golden by reconstruction: the same three glyphs, one per call, at the
    // (sizex+1) pitch. This is the loop's own contract -- right glyphs, right
    // pitch, right start, right ramp.
    s->clearbuffer();
    const char* word = "buf";
    for (int i = 0; i < 3; i++)
        t.write_char_xy(5 + i * (sx + 1), 64, word[i],
                        static_cast<unsigned char>(DEFAULT_TEXT_COLOR),
                        static_cast<short>(1));
    ASSERT_EQ(snapshot_indices(5, 64, box_w, sy), buffered)
        << "the to_buffer run must be the per-glyph buffer blits at the (sizex+1) pitch";

    // The direct path takes the OTHER ramp, so the two must not agree.
    s->clearbuffer();
    t.write_xy(5, 64, "buf", static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
    ASSERT_NE(snapshot_indices(5, 64, box_w, sy), buffered)
        << "to_buffer=1 must take walkputbuffertext's ramp, not putdatatext's";
}

// write_xy(x,y,str,view) paints at the VIEW's origin + (x,y) through
// walkputbuffertext, and falls back to the flat putdatatext path when the view
// is null (src/interface/render/text.cpp write_char_xy(...,viewscreen*)).
TEST(MassCoverage, text_write_xy_view_paints_at_the_view_origin) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int box_w = 4 * (sx + 1);  // "view" is 4 glyphs
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v) << "setup: view 0 must exist";

    {
        ScopedViewOffset offset(v, 20, 30);
        s->clearbuffer();
        ASSERT_EQ(1, t.write_xy(5, 70, "view", v)) << "the view form returns 1";
        ASSERT_EQ(kWordInkViewRamp,
                  count_index_in(25, 100, box_w, sy, DEFAULT_TEXT_COLOR))
            << "the run must land at (view->xloc + x, view->yloc + y)";
        ASSERT_EQ(0, count_index_in(5, 70, box_w, sy, DEFAULT_TEXT_COLOR))
            << "the view offset must not be ignored";
    }

    // A null view is the flat, absolute path.
    s->clearbuffer();
    ASSERT_EQ(1, t.write_xy(5, 70, "view", static_cast<viewscreen*>(nullptr)))
        << "the view form returns 1";
    const std::vector<int> flat = snapshot_indices(5, 70, box_w, sy);
    s->clearbuffer();
    t.write_xy(5, 70, "view", static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
    ASSERT_EQ(snapshot_indices(5, 70, box_w, sy), flat)
        << "a null view must be the plain DEFAULT_TEXT_COLOR run at (x,y)";
}

// write_y(y,str,color) starts the row at (320 - len*(sizex+1))/2
// (src/interface/render/text.cpp).
TEST(MassCoverage, text_write_y_color_centres_the_row_on_the_320_wide_canvas) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int xstart = (320 - 2 * (sx + 1)) / 2;  // "yc" is 2 glyphs
    ASSERT_EQ(154, xstart) << "setup: the 5x6 font centres a 2-glyph row at 154";

    s->clearbuffer();
    ASSERT_EQ(1, t.write_y(76, "yc", WHITE)) << "write_y returns write_xy's 1";
    const std::vector<int> painted = snapshot_indices(xstart, 76, 2 * (sx + 1), sy);
    ASSERT_EQ(kWordInkYcWhite, count_index_in(xstart, 76, 2 * (sx + 1), sy, WHITE))
        << "the glyphs must land in the requested colour at the centred start";
    ASSERT_EQ(0, count_index_in(0, 76, xstart, sy, WHITE))
        << "nothing may be painted left of (320 - len*(sizex+1))/2";

    s->clearbuffer();
    t.write_xy(xstart, 76, "yc", WHITE);
    ASSERT_EQ(snapshot_indices(xstart, 76, 2 * (sx + 1), sy), painted)
        << "the centred row must be the plain run started at the computed x";
}

// write_y(y,str) centres in DEFAULT_TEXT_COLOR (src/interface/render/text.cpp).
TEST(MassCoverage, text_write_y_default_centres_the_row_in_the_default_colour) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int xstart = (320 - 2 * (sx + 1)) / 2;  // "yd" is 2 glyphs

    s->clearbuffer();
    ASSERT_EQ(1, t.write_y(82, "yd")) << "write_y returns write_xy's 1";
    const std::vector<int> painted = snapshot_indices(xstart, 82, 2 * (sx + 1), sy);
    ASSERT_EQ(kWordInkYdDefault,
              count_index_in(xstart, 82, 2 * (sx + 1), sy, DEFAULT_TEXT_COLOR))
        << "the glyphs must land in DEFAULT_TEXT_COLOR at the centred start";
    ASSERT_EQ(0, count_index_in(0, 82, xstart, sy, DEFAULT_TEXT_COLOR))
        << "nothing may be painted left of (320 - len*(sizex+1))/2";

    s->clearbuffer();
    t.write_xy(xstart, 82, "yd", static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
    ASSERT_EQ(snapshot_indices(xstart, 82, 2 * (sx + 1), sy), painted)
        << "the default centred row must be the plain DEFAULT_TEXT_COLOR run at the computed x";
}

// write_y(y,str,to_buffer=1) centres and hands the row to the to_buffer path,
// returning that path's ADVANCE rather than 1 (src/interface/render/text.cpp).
TEST(MassCoverage, text_write_y_tobuffer_centres_the_row_and_returns_the_advance) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int xstart = (320 - 2 * (sx + 1)) / 2;  // "yb" is 2 glyphs

    s->clearbuffer();
    ASSERT_EQ(2 * (sx + 1), t.write_y(88, "yb", static_cast<short>(1)))
        << "the to_buffer row returns the advance, len*(sizex+1)";
    const std::vector<int> painted = snapshot_indices(xstart, 88, 2 * (sx + 1), sy);
    ASSERT_EQ(kWordInkYbRamp,
              count_index_in(xstart, 88, 2 * (sx + 1), sy, DEFAULT_TEXT_COLOR))
        << "the buffer ramp puts only the index-255 pixels in DEFAULT_TEXT_COLOR";
    ASSERT_EQ(0, count_index_in(0, 88, xstart, sy, DEFAULT_TEXT_COLOR))
        << "nothing may be painted left of (320 - len*(sizex+1))/2";

    s->clearbuffer();
    t.write_xy(xstart, 88, "yb", static_cast<short>(1));
    ASSERT_EQ(snapshot_indices(xstart, 88, 2 * (sx + 1), sy), painted)
        << "the centred to_buffer row must be the to_buffer run at the computed x";
}

// write_y(y,str,color,view) centres on 320 and then offsets by the view
// (src/interface/render/text.cpp).
TEST(MassCoverage, text_write_y_view_color_centres_then_offsets_by_the_view) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int xstart = (320 - 3 * (sx + 1)) / 2;  // "yvc" is 3 glyphs
    ASSERT_EQ(151, xstart) << "setup: the 5x6 font centres a 3-glyph row at 151";
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v) << "setup: view 0 must exist";

    {
        ScopedViewOffset offset(v, 20, 30);
        s->clearbuffer();
        ASSERT_EQ(1, t.write_y(94, "yvc", WHITE, v)) << "the view row returns 1";
        ASSERT_EQ(kWordInkYvcRamp,
                  count_index_in(20 + xstart, 30 + 94, 3 * (sx + 1), sy, WHITE))
            << "the centred row must land at the view origin + (xstart, y)";
        ASSERT_EQ(0, count_index_in(xstart, 94, 3 * (sx + 1), sy, WHITE))
            << "the view offset must not be ignored";
    }

    // A null view is the flat, absolute centred row.
    s->clearbuffer();
    ASSERT_EQ(1, t.write_y(94, "yvc", WHITE, static_cast<viewscreen*>(nullptr)))
        << "the view row returns 1";
    const std::vector<int> flat = snapshot_indices(xstart, 94, 3 * (sx + 1), sy);
    s->clearbuffer();
    t.write_xy(xstart, 94, "yvc", WHITE);
    ASSERT_EQ(snapshot_indices(xstart, 94, 3 * (sx + 1), sy), flat)
        << "a null view must be the plain run at the centred x";
}

// write_y(y,str,view) centres in DEFAULT_TEXT_COLOR through the view
// (src/interface/render/text.cpp).
TEST(MassCoverage, text_write_y_view_centres_in_the_default_colour_through_the_view) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;
    const int xstart = (320 - 2 * (sx + 1)) / 2;  // "yv" is 2 glyphs
    viewscreen* v = s->viewob[0].get();
    ASSERT_NE(nullptr, v) << "setup: view 0 must exist";

    {
        ScopedViewOffset offset(v, 20, 30);
        s->clearbuffer();
        ASSERT_EQ(1, t.write_y(100, "yv", v)) << "the view row returns 1";
        ASSERT_EQ(kWordInkYvRamp,
                  count_index_in(20 + xstart, 30 + 100, 2 * (sx + 1), sy,
                                 DEFAULT_TEXT_COLOR))
            << "the centred row must land at the view origin in DEFAULT_TEXT_COLOR";
        ASSERT_EQ(0, count_index_in(xstart, 100, 2 * (sx + 1), sy, DEFAULT_TEXT_COLOR))
            << "the view offset must not be ignored";
    }

    // A null view is the flat, absolute centred row.
    s->clearbuffer();
    ASSERT_EQ(1, t.write_y(100, "yv", static_cast<viewscreen*>(nullptr)))
        << "the view row returns 1";
    const std::vector<int> flat = snapshot_indices(xstart, 100, 2 * (sx + 1), sy);
    s->clearbuffer();
    t.write_xy(xstart, 100, "yv", static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
    ASSERT_EQ(snapshot_indices(xstart, 100, 2 * (sx + 1), sy), flat)
        << "a null view must be the plain DEFAULT_TEXT_COLOR run at the centred x";
}

// write_char_xy(x,y,c,to_buffer=1) blits glyph c through walkputbuffertext in
// DEFAULT_TEXT_COLOR; to_buffer=0 falls through to the flat overload
// (src/interface/render/text.cpp).
TEST(MassCoverage, text_write_char_xy_tobuffer_blits_the_requested_glyph) {
    screen* s = og::runtime::current_session->myscreen_;
    text& t = synced_text();
    const int sx = t.sizex;
    const int sy = t.sizey;

    s->clearbuffer();
    ASSERT_EQ(1, t.write_char_xy(5, 106, 'Q', static_cast<short>(1)))
        << "a blitted glyph reports 1";
    const std::vector<int> q = snapshot_indices(5, 106, sx, sy);
    ASSERT_EQ(kGlyphInkQRamp, count_index_in(5, 106, sx, sy, DEFAULT_TEXT_COLOR))
        << "'Q' must paint its own index-255 ink in DEFAULT_TEXT_COLOR";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(4, 106))
        << "the glyph must start at x, not left of it";

    s->clearbuffer();
    ASSERT_EQ(1, t.write_char_xy(5, 106, 'R', static_cast<short>(1)))
        << "a blitted glyph reports 1";
    const std::vector<int> r = snapshot_indices(5, 106, sx, sy);
    ASSERT_EQ(kGlyphInkRRamp, count_index_in(5, 106, sx, sy, DEFAULT_TEXT_COLOR))
        << "'R' must paint its own index-255 ink in DEFAULT_TEXT_COLOR";
    ASSERT_NE(q, r) << "the blit must follow the letter it was given";

    // to_buffer=0 hands off to the coloured flat overload -- DEFAULT_TEXT_COLOR
    // through putdatatext, which flattens the whole 251..255 ramp onto that one
    // colour.
    s->clearbuffer();
    ASSERT_EQ(1, t.write_char_xy(15, 106, 'R', static_cast<short>(0)))
        << "a blitted glyph reports 1";
    const std::vector<int> flat = snapshot_indices(15, 106, sx, sy);
    s->clearbuffer();
    t.write_char_xy(15, 106, 'R', static_cast<unsigned char>(DEFAULT_TEXT_COLOR));
    ASSERT_EQ(snapshot_indices(15, 106, sx, sy), flat)
        << "to_buffer=0 must be the flat DEFAULT_TEXT_COLOR blit";
}

// obmap_debug_draw(map,scr) runs two passes: a YELLOW hollow OBRES box per
// occupied pos_to_walker cell with the pile size centred in it, and a
// team-colour hollow box per walker_to_pos entry -- both at
// unhash(cell) - viewob[0]->top{x,y} (src/interface/render/obmap_debug_draw.cpp).
TEST(MassCoverage, obmap_debug_draw_boxes_every_pile_and_every_walker) {
    reset_level_state();
    screen* s = og::runtime::current_session->myscreen_;
    synced_text(); // the obmap pass writes its pile counts through text_normal
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";
    ASSERT_EQ(0, s->viewob[0]->topx) << "setup: the camera must sit at the map origin";
    ASSERT_EQ(0, s->viewob[0]->topy) << "setup: the camera must sit at the map origin";

    // An empty map draws nothing at all -- the negative control that a debug
    // pass painting the whole canvas would fail.
    obmap empty_map;
    s->clearbuffer();
    obmap_debug_draw(empty_map, s);
    ASSERT_EQ(0, nonblack_on_canvas()) << "an empty collision map must paint nothing";

    walker* w = add_sized_living(0, 100, 100);
    ASSERT_NE(nullptr, w) << "setup: the walker must exist";
    const int team_index = pal_readback_index(w->query_team_color());
    const int label_index = pal_readback_index(YELLOW);
    ASSERT_NE(team_index, label_index)
        << "setup: the two passes must be distinguishable on the canvas";

    // Pass 1 in isolation: one pile in a cell the walker does not occupy. The
    // YELLOW box spans unhash(hash(200))=192 .. +OBRES and carries the pile
    // count centred inside it.
    obmap pile_only;
    pile_only.pos_to_walker[{obmap::hash(200), obmap::hash(100)}].push_back(w);
    s->clearbuffer();
    obmap_debug_draw(pile_only, s);
    const IndexExtent pile = index_extent_on_canvas(label_index);
    ASSERT_EQ(128 + 8, pile.count)
        << "the pile pass must draw the 33x33 hollow box (128 px) plus the \"1\" label (8 px)";
    ASSERT_EQ(192, pile.minx) << "the pile box starts at unhash(hash(200))";
    ASSERT_EQ(224, pile.maxx) << "the pile box spans OBRES";
    ASSERT_EQ(96, pile.miny) << "the pile box starts at unhash(hash(100))";
    ASSERT_EQ(128, pile.maxy) << "the pile box spans OBRES";

    // EVERY pile, not just the first: a second, far-apart cell doubles both the
    // ink and the bounding box. A loop that painted one entry and stopped reads
    // back as the single box above.
    obmap two_piles;
    two_piles.pos_to_walker[{obmap::hash(200), obmap::hash(100)}].push_back(w);
    two_piles.pos_to_walker[{obmap::hash(40), obmap::hash(40)}].push_back(w);
    s->clearbuffer();
    obmap_debug_draw(two_piles, s);
    const IndexExtent piles = index_extent_on_canvas(label_index);
    ASSERT_EQ(2 * (128 + 8), piles.count)
        << "two occupied cells must draw two boxes and two \"1\" labels";
    ASSERT_EQ(32, piles.minx) << "the second pile box starts at unhash(hash(40))";
    ASSERT_EQ(224, piles.maxx) << "...and the first one still ends at unhash(hash(200))+OBRES";
    ASSERT_EQ(32, piles.miny) << "the second pile box starts at unhash(hash(40))";
    ASSERT_EQ(128, piles.maxy) << "...and the first one still ends at unhash(hash(100))+OBRES";

    // Pass 2 in isolation: obmap::add registers the walker's own cell, and the
    // walker box lands on it in the team colour.
    obmap map;
    map.add(w, 100, 100);
    s->clearbuffer();
    obmap_debug_draw(map, s);
    const IndexExtent team = index_extent_on_canvas(team_index);
    ASSERT_EQ(128, team.count) << "the walker pass must draw a 33x33 hollow box";
    ASSERT_EQ(96, team.minx) << "the walker box starts at unhash(hash(100))";
    ASSERT_EQ(128, team.maxx) << "the walker box spans OBRES";
    ASSERT_EQ(96, team.miny) << "the walker box starts at unhash(hash(100))";
    ASSERT_EQ(128, team.maxy) << "the walker box spans OBRES";

    // EVERY walker, not just the first: a second body on another team gets its
    // own box in its own colour, so a walker_to_pos loop that stopped after one
    // entry leaves one of the two colours off the canvas entirely.
    walker* w2 = add_sized_living(1, 200, 100, FAMILY_ORC);
    ASSERT_NE(nullptr, w2) << "setup: the second walker must exist";
    const int team2_index = pal_readback_index(w2->query_team_color());
    ASSERT_NE(team_index, team2_index)
        << "setup: the two walkers must be distinguishable on the canvas";
    ASSERT_NE(label_index, team2_index)
        << "setup: the second walker must be distinguishable from the pile pass";

    obmap both;
    both.add(w, 100, 100);
    both.add(w2, 200, 100);
    s->clearbuffer();
    obmap_debug_draw(both, s);
    const IndexExtent first = index_extent_on_canvas(team_index);
    const IndexExtent second = index_extent_on_canvas(team2_index);
    ASSERT_EQ(128, first.count) << "the first walker still gets its own 33x33 box";
    ASSERT_EQ(96, first.minx) << "the first walker box sits on its own cell";
    ASSERT_EQ(128, first.maxx) << "the first walker box sits on its own cell";
    ASSERT_EQ(128, second.count) << "the second walker gets a box of its own";
    ASSERT_EQ(192, second.minx) << "the second walker box starts at unhash(hash(200))";
    ASSERT_EQ(224, second.maxx) << "the second walker box spans OBRES";
    ASSERT_EQ(96, second.miny) << "the second walker box starts at unhash(hash(100))";
    ASSERT_EQ(128, second.maxy) << "the second walker box spans OBRES";

    reset_level_state();
}

// The per-walker box grows to the min/max of EVERY cell in walker_to_pos, in
// all four directions, and is then drawn in unhashed (pixel) coordinates
// (src/interface/render/obmap_debug_draw.cpp, the walker_to_pos loop).
TEST(MassCoverage, obmap_debug_draw_expands_bounding_boxes_all_directions) {
    reset_level_state();
    screen* s = og::runtime::current_session->myscreen_;
    synced_text(); // the obmap pass writes its pile counts through text_normal
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";
    ASSERT_EQ(0, s->viewob[0]->topx) << "setup: the camera must sit at the map origin";
    ASSERT_EQ(0, s->viewob[0]->topy) << "setup: the camera must sit at the map origin";

    walker* w = add_living(1, FAMILY_ARCHER);
    ASSERT_NE(nullptr, w) << "setup: the walker must exist";
    const int team_index = pal_readback_index(w->query_team_color());
    ASSERT_NE(pal_readback_index(YELLOW), team_index)
        << "setup: the walker box must be distinguishable from the pile pass";

    // One cell: the box is a single OBRES square at unhash(4) = 128.
    obmap one_cell;
    one_cell.walker_to_pos[w] = {{4, 4}};
    s->clearbuffer();
    obmap_debug_draw(one_cell, s);
    const IndexExtent small = index_extent_on_canvas(team_index);
    ASSERT_EQ(128, small.count) << "one cell must give a 33x33 hollow box";
    ASSERT_EQ(128, small.minx) << "the single-cell box starts at unhash(4)";
    ASSERT_EQ(160, small.maxx) << "the single-cell box spans unhash(1)";
    ASSERT_EQ(128, small.miny) << "the single-cell box starts at unhash(4)";
    ASSERT_EQ(160, small.maxy) << "the single-cell box spans unhash(1)";

    // Five cells reached in every direction from the first: left (x 4->2), up
    // (y 4->1), right (x 2->7) and down (y 1->6). The box must end up at
    // cell (2,1) with cell extents (5,5) -- pixels (64,32)..(224,192).
    obmap map;
    map.pos_to_walker[{obmap::hash(96), obmap::hash(96)}].push_back(w);
    map.pos_to_walker[{obmap::hash(128), obmap::hash(128)}].push_back(w);
    map.walker_to_pos[w] = {{4, 4}, {2, 4}, {2, 1}, {7, 1}, {7, 6}};
    s->clearbuffer();
    obmap_debug_draw(map, s);
    const IndexExtent grown = index_extent_on_canvas(team_index);
    ASSERT_EQ(640, grown.count) << "the grown box is a 161x161 hollow rect";
    ASSERT_EQ(64, grown.minx) << "the leftward expansion must reach unhash(2)";
    ASSERT_EQ(224, grown.maxx) << "the rightward expansion must reach unhash(2+5)";
    ASSERT_EQ(32, grown.miny) << "the upward expansion must reach unhash(1)";
    ASSERT_EQ(192, grown.maxy) << "the downward expansion must reach unhash(1+5)";

    reset_level_state();
}
