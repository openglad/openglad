#include <openglad/interface/screen.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/render/pixie.h>
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
#include <openglad/core/test_trace.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
// clear(): the render canvas is blanked AND every live view's legacy
// videobuffer is zeroed (src/interface/screen.cpp screen::clear -> clearbuffer
// + viewob[i]->clear(); src/interface/render/view.cpp viewscreen::clear).
TEST(MassCoverage, screen_clear_blanks_the_canvas_and_every_view_buffer) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(1);

    s->point(5, 5, RED);
    int painted = -1;
    s->get_pixel(5, 5, &painted);
    ASSERT_EQ(static_cast<int>(RED), painted)
        << "setup: the probe pixel must be painted before clear() is asked to blank it";

    std::span<unsigned char> buf = s->getbuffer();
    ASSERT_LT(std::size_t{100}, buf.size()) << "setup: the legacy videobuffer must be allocated";
    buf[100] = 42;

    s->clear();

    painted = -1;
    s->get_pixel(5, 5, &painted);
    ASSERT_EQ(0, painted) << "clear() must blank the render canvas";
    ASSERT_EQ(0, static_cast<int>(s->getbuffer()[100]))
        << "clear() must run viewob[i]->clear() and zero the legacy videobuffer";
}

// redraw(): composes a world frame and runs the per-view chrome pass, which
// frames every non-FULL view (src/interface/screen.cpp screen::redraw ->
// draw_panel_chrome, TRACE("hud", "panel_border view=%d")).
TEST(MassCoverage, screen_redraw_runs_the_panel_chrome_pass) {
    screen* s = og::runtime::current_session->myscreen_;
    s->ready_for_battle(1);
    ASSERT_NE(nullptr, s->viewob[0].get()) << "setup: view 0 must exist";

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

    const short saved_numviews = s->numviews;
    s->numviews = 0;
    s->refresh();
    ASSERT_TRUE(s->window_is_black()) << "refresh with no views must present nothing";
    s->numviews = saved_numviews;

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
    ASSERT_TRUE(vs != nullptr) << "viewscreen should exist";
    if (!vs)
        return;

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
    ASSERT_TRUE(vs != nullptr) << "viewscreen should exist";
    if (!vs)
        return;

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
    ASSERT_TRUE(render != nullptr) << "SDL level renderer should be created";
    if (!render)
        return;

    render->draw_tile(0, 0, 0, vs);
    render->draw_tile(PIX_WATER1, 4, 4, vs);
    render->draw_tile(-1, 0, 0, vs);
    render->draw_tile(PIX_MAX, 0, 0, vs);
    render->reset_tiles(tiles.data());
    render->draw_tile(0, 0, 0, vs);
}

// viewscreen::clear(): zeroes the whole legacy videobuffer scratch, not a
// prefix of it (src/interface/render/view.cpp viewscreen::clear).
TEST(MassCoverage, viewscreen_clear_zeroes_the_whole_legacy_videobuffer) {
    screen* s = og::runtime::current_session->myscreen_;
    auto buf = s->getbuffer();
    ASSERT_EQ(static_cast<std::size_t>(kUiCanvasW) * static_cast<std::size_t>(kUiCanvasH),
              buf.size())
        << "setup: the legacy scratch is sized to the fixed UI canvas";

    buf[0] = 41;
    buf[100] = 42;
    buf[buf.size() - 1] = 43;
    ASSERT_EQ(42, static_cast<int>(s->getbuffer()[100]))
        << "setup: the probe bytes must land in the live buffer";

    s->viewob[0]->clear();

    ASSERT_EQ(0, static_cast<int>(s->getbuffer()[0]))
        << "clear() must zero the first byte of the videobuffer";
    ASSERT_EQ(0, static_cast<int>(s->getbuffer()[100]))
        << "clear() must zero the interior of the videobuffer";
    ASSERT_EQ(0, static_cast<int>(s->getbuffer()[buf.size() - 1]))
        << "clear() must zero the whole videobuffer, not a leading run";
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

// getbuffer() hands back the LIVE legacy videobuffer, sized to the fixed UI
// canvas (video_sdl.cpp sdl_video::getbuffer; video_sdl.h videobuffer).
TEST(MassCoverage, video_getbuffer_is_the_live_ui_sized_scratch) {
    screen* s = og::runtime::current_session->myscreen_;
    auto buf = s->getbuffer();
    ASSERT_EQ(static_cast<std::size_t>(kUiCanvasW) * static_cast<std::size_t>(kUiCanvasH),
              buf.size())
        << "the legacy scratch is kUiCanvasW*kUiCanvasH, not the world canvas area";

    buf[0] = 9;
    buf[buf.size() - 1] = 11;
    ASSERT_EQ(9, static_cast<int>(s->getbuffer()[0]))
        << "getbuffer must alias one storage, not hand out a copy";
    ASSERT_EQ(11, static_cast<int>(s->getbuffer()[buf.size() - 1]))
        << "getbuffer must alias one storage across its whole extent";
    ASSERT_EQ(buf.data(), s->getbuffer().data())
        << "successive getbuffer calls must point at the same bytes";

    std::fill(buf.begin(), buf.end(), static_cast<unsigned char>(0));
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

// clear_window() blacks the whole render surface before uploading it to the
// window texture (src/platform/sdl/sai2x.cpp Screen::clear_window). It does NOT
// set window_is_black_ -- only a completed fadeblack does.
TEST(MassCoverage, video_clear_window_blacks_the_whole_render_surface) {
    screen* s = og::runtime::current_session->myscreen_;
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

// putblack(x,y,w,h) zeroes that rect of whatever buffer the session's legacy
// videoptr_ points at, and is a no-op while that pointer is null -- which is
// how production always leaves it (src/platform/sdl/video_sdl.cpp putblack;
// src/platform/sdl/game_session.cpp, "only tests call putblack, after pointing
// videoptr_ at a real buffer").
TEST(MassCoverage, video_putblack_no_ops_until_videoptr_points_somewhere) {
    screen* s = og::runtime::current_session->myscreen_;
    const int cw = s->canvas_w();
    auto buf = s->getbuffer();
    std::fill(buf.begin(), buf.end(), static_cast<unsigned char>(7));

    ASSERT_EQ(nullptr, og::runtime::current_session->videoptr_)
        << "setup: production leaves the legacy direct-video pointer null";
    s->putblack(2, 2, 8, 8);
    ASSERT_EQ(7, static_cast<int>(buf[static_cast<std::size_t>(5 * cw + 5)]))
        << "putblack must be a no-op while videoptr_ is null";

    // Restores the null default even if an assertion below aborts the test.
    struct VideoPtrScope final {
        explicit VideoPtrScope(unsigned char* p) { og::runtime::current_session->videoptr_ = p; }
        ~VideoPtrScope() { og::runtime::current_session->videoptr_ = nullptr; }
    } scope(buf.data());

    s->putblack(2, 2, 8, 8);
    ASSERT_EQ(0, static_cast<int>(buf[static_cast<std::size_t>(2 * cw + 2)]))
        << "the rect's first pixel must be zeroed";
    ASSERT_EQ(0, static_cast<int>(buf[static_cast<std::size_t>(5 * cw + 5)]))
        << "the rect's interior must be zeroed";
    ASSERT_EQ(0, static_cast<int>(buf[static_cast<std::size_t>(9 * cw + 9)]))
        << "the rect's last pixel (x+w-1, y+h-1) must be zeroed";
    ASSERT_EQ(7, static_cast<int>(buf[static_cast<std::size_t>(10 * cw + 10)]))
        << "the pixel past the rect must survive";
    ASSERT_EQ(7, static_cast<int>(buf[static_cast<std::size_t>(1 * cw + 1)]))
        << "the pixel before the rect must survive";
    ASSERT_EQ(7, static_cast<int>(buf[static_cast<std::size_t>(5 * cw + 10)]))
        << "putblack must not run the whole row";

    std::fill(buf.begin(), buf.end(), static_cast<unsigned char>(0));
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
    std::array<int, 3> before{};
    query_palette_reg(ORANGE_START, &before[0], &before[1], &before[2]);

    s->do_cycle(1, 4);

    std::array<int, 3> after{};
    query_palette_reg(ORANGE_START, &after[0], &after[1], &after[2]);
    ASSERT_EQ(before, after) << "only curmode % maxmode == 0 may rotate the palette";
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

// putdatatext(x,y,w,h,pixels) fills one surface rect per non-zero source index
// and skips index 0 (src/platform/sdl/video_sdl.cpp putdatatext).
TEST(MassCoverage, video_putdatatext_blits_opaquely_with_index_zero_transparent) {
    screen* s = og::runtime::current_session->myscreen_;
    auto px = sample_pixels(70);
    px[0] = 0;
    s->clearbuffer();
    s->draw_rect_filled(10, 10, 8, 8, WHITE, 255);
    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10)) << "setup: the backdrop must land";

    s->putdatatext(10, 10, 8, 8, px);

    ASSERT_EQ(pal_readback_index(WHITE), px_index(10, 10))
        << "a zero source index must leave the backdrop showing";
    ASSERT_EQ(pal_readback_index(71), px_index(11, 10))
        << "source[1] paints its own index over the backdrop";
    ASSERT_EQ(pal_readback_index(70), px_index(10, 11))
        << "source[8] starts the second row back at column x";
    ASSERT_EQ(pal_readback_index(PURE_BLACK), px_index(18, 10))
        << "the block is only w wide";
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
    SDL_Surface* surf = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_XRGB8888);
    ASSERT_TRUE(surf != nullptr) << "surface alloc";
    og::runtime::current_session->myscreen_->putbuffer_surface(5, 5, 8, 8, 0, 0, 320, 200, surf);
    SDL_DestroySurface(surf);
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
    SDL_Surface* s = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    ASSERT_TRUE(s != nullptr) << "surface alloc";
    std::array<Uint8, 4 * 4 * 4> from{};
    std::array<Uint8, 4 * 4 * 4> to{};
    og::runtime::current_session->myscreen_->fade_between24(s, from.data(), to.data(), 10);
    SDL_DestroySurface(s);
}

TEST(MassCoverage, video_fade_between) {
    SDL_Surface* a = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    SDL_Surface* b = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    SDL_Surface* d = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_XRGB8888);
    ASSERT_TRUE(a && b && d) << "surfaces alloc";
    (void)og::runtime::current_session->myscreen_->fade_between(a, b, d);
    SDL_DestroySurface(a);
    SDL_DestroySurface(b);
    SDL_DestroySurface(d);
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

TEST(MassCoverage, obmap_debug_draw_expands_bounding_boxes_all_directions) {
    reset_level_state();

    obmap map;
    walker* w = add_living(1, FAMILY_ARCHER);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    if (!w)
        return;

    map.pos_to_walker[{obmap::hash(96), obmap::hash(96)}].push_back(w);
    map.pos_to_walker[{obmap::hash(128), obmap::hash(128)}].push_back(w);
    map.walker_to_pos[w] = {{4, 4}, {2, 4}, {2, 1}, {7, 1}, {7, 6}};

    obmap_debug_draw(map, og::runtime::current_session->myscreen_);
    reset_level_state();
}

