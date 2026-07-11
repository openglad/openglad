#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/legacy/base.h>
#include <openglad/core/test_trace.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/video_sdl.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

// From glad.cpp
short remaining_foes(screen* scr, walker* myguy);
short remaining_team(screen* scr, char myteam);
void draw_radar_gems(screen* scr);
void draw_gem(short x, short y, short color, screen* scr);
void draw_value_bar(short left, short top, walker* control, short mode, screen* scr);
void new_draw_value_bar(Sint32 left, Sint32 top, walker* control, short mode, screen* scr);
void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor, short somelength, screen* scr);
short score_panel(screen* scr);
short score_panel(screen* scr, short do_it);
short new_score_panel(screen* scr, short do_it);
// From score_panel.cpp (B5)
void pending_hostile_wave_counts(const GameWorld& world, walker* viewer,
                                 short& pending, std::uint32_t& next_wake_ticks);
// From score_panel.cpp: forced-flee (scared) countdown source.
int hud_scared_flee_ticks(walker* viewer);

static bool control_pointer_is_live(LevelRuntimeData& level_data, const walker* candidate)
{
    if (candidate == nullptr)
        return false;

    const auto in_list = [candidate](const std::list<std::unique_ptr<walker>>& list) {
        for (const auto& entry : list)
        {
            if (entry.get() == candidate)
                return true;
        }
        return false;
    };

    return in_list(level_data.world().oblist)
        || in_list(level_data.world().fxlist)
        || in_list(level_data.world().weaplist);
}

static std::unique_ptr<walker> make_player(unsigned char team)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (!w)
        return nullptr;
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(0);
    w->setxy(100, 100);
    return w;
}

static std::unique_ptr<walker> make_living(unsigned char family, unsigned char team)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(-1);
    w->setxy(120, 100);
    return w;
}

// get_pixel's index read-back returns the FIRST palette index matching the
// pixel's RGB, so any index whose RGB duplicates an earlier entry (the grey
// ramp does) reads back as that earlier alias. Canonicalize expectations the
// same way before comparing against captured frames.
static unsigned char canonical_palette_index(unsigned char color)
{
    int r = 0, g = 0, b = 0;
    query_palette_reg(color, &r, &g, &b);
    for (int i = 0; i < 256; ++i)
    {
        int tr = 0, tg = 0, tb = 0;
        query_palette_reg(static_cast<unsigned char>(i), &tr, &tg, &tb);
        if (r == tr && g == tg && b == tb)
            return static_cast<unsigned char>(i);
    }
    return color;
}

static std::array<unsigned char, 64000> capture_rendered_frame(screen& scr)
{
    std::array<unsigned char, 64000> frame{};
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            int color_index = 0;
            scr.get_pixel(x, y, &color_index);
            frame[static_cast<std::size_t>(y * 320 + x)] =
                static_cast<unsigned char>(color_index);
        }
    }
    return frame;
}

TEST(GladHud, glad_remaining_counts)
{
    // Isolate the oblist so prior game state doesn't affect counts.
    struct ObListSwap {
        std::list<std::unique_ptr<walker>> saved;
        ObListSwap()
        {
            og::runtime::current_session->myscreen_->world().oblist.splice_into(saved);
        }
        ~ObListSwap()
        {
            og::runtime::current_session->myscreen_->world().oblist.splice(og::runtime::current_session->myscreen_->world().oblist.end(), saved);
        }
    } swap;

    auto control = make_player(0);
    auto ally = make_living(FAMILY_ELF, 0);
    auto foe1 = make_living(FAMILY_ORC, 1);
    auto foe2 = make_living(FAMILY_ORC, 2);
    ASSERT_TRUE(control && ally && foe1 && foe2) << "walkers should be created";

    walker* controlp = control.get();
    walker* foe2p = foe2.get();

    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(control));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(ally));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(foe1));
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(foe2));

    ASSERT_EQ(2, (int)remaining_foes(og::runtime::current_session->myscreen_, controlp)) << "should count non-friendly living foes";
    ASSERT_EQ(2, (int)remaining_team(og::runtime::current_session->myscreen_, 0)) << "should count living on team 0 (including control)";

    foe2p->set_dead(1);
    ASSERT_EQ(1, (int)remaining_foes(og::runtime::current_session->myscreen_, controlp)) << "dead foes should not be counted";
    og::runtime::current_session->myscreen_->world().oblist.clear();
}


TEST(GladHud, glad_draw_gems_and_value_bars_smoke)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
    walker* controlp = control.get();

    // Attach control to view so draw_radar_gems can find it.
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    walker* old_control = v->control;
    v->control = controlp;

    // draw_radar_gems caches old team; change team to force multiple draws.
    controlp->set_team_num(0);
    draw_radar_gems(og::runtime::current_session->myscreen_);
    controlp->set_team_num(1);
    draw_radar_gems(og::runtime::current_session->myscreen_);

    // Direct gem draw.
    draw_gem(10, 10, 32, og::runtime::current_session->myscreen_);

    // Exercise value bar thresholds for HP and MP.
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_hitpoints(100);
    draw_value_bar(10, 20, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->set_hitpoints(20);
    draw_value_bar(10, 28, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->set_hitpoints(60);
    draw_value_bar(10, 36, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->set_hitpoints(90);
    draw_value_bar(10, 44, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->set_hitpoints(120);
    draw_value_bar(10, 52, controlp, 0, og::runtime::current_session->myscreen_);

    controlp->stats()->set_max_magicpoints(80);
    controlp->stats()->set_magicpoints(80);
    draw_value_bar(10, 60, controlp, 1, og::runtime::current_session->myscreen_);
    controlp->stats()->set_magicpoints(10);
    draw_value_bar(10, 68, controlp, 1, og::runtime::current_session->myscreen_);
    controlp->stats()->set_magicpoints(100);
    draw_value_bar(10, 76, controlp, 1, og::runtime::current_session->myscreen_);

    // New percentage-bar-based drawing.
    controlp->stats()->set_hitpoints(100);
    new_draw_value_bar(80, 4, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->set_hitpoints(20);
    new_draw_value_bar(80, 12, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->set_hitpoints(80);
    new_draw_value_bar(80, 20, controlp, 0, og::runtime::current_session->myscreen_);
    controlp->stats()->set_magicpoints(80);
    new_draw_value_bar(80, 28, controlp, 1, og::runtime::current_session->myscreen_);
    controlp->stats()->set_magicpoints(60);
    new_draw_value_bar(80, 44, controlp, 1, og::runtime::current_session->myscreen_);
    draw_percentage_bar(80, 36, 12, 30, og::runtime::current_session->myscreen_);

    v->control = control_pointer_is_live(og::runtime::current_session->myscreen_->level_runtime_data(), old_control) ? old_control : nullptr;
}


TEST(GladHud, glad_score_panel_and_new_score_panel_modes)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_level(7);
    controlp->stats()->set_hitpoints(55);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(33);
    controlp->stats()->set_max_magicpoints(80);
    controlp->stats()->set_special_cost(static_cast<unsigned char>(controlp->current_special()), 10);

    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "view should exist";
    walker* old_control = v->control;
    v->control = controlp;

    // Make sure these overlays execute.
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    // Exercise all life display variants in new_score_panel.
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel text mode";
    v->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel bars mode";
    v->prefs[PREF_LIFE] = PREF_LIFE_BOTH;
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel both mode";

    // Toggle shifter special-name branch and low-mp branch.
    controlp->set_shifter_down(1);
    controlp->stats()->set_magicpoints(0);
    (void)new_score_panel(og::runtime::current_session->myscreen_, 1);
    controlp->set_shifter_down(0);

    // Wrapper functions.
    ASSERT_EQ(1, (int)score_panel(og::runtime::current_session->myscreen_)) << "score_panel wrapper";
    ASSERT_EQ(1, (int)score_panel(og::runtime::current_session->myscreen_, 1)) << "score_panel overload";

    v->control = control_pointer_is_live(og::runtime::current_session->myscreen_->level_runtime_data(), old_control) ? old_control : nullptr;
}

// Regression: a network client renders a single local viewport (players == 0),
// but its controlled walker carries the server-global player slot in user()
// (set by GameServer::bind_player and shipped to the client in the snapshot).
// new_score_panel used to gate the HUD on `control->user() == players`, which
// only holds for local split-screen (where game.cpp claims view->control with
// set_user(view_idx)). For any non-host client (slot 1/2/3) that check was
// false, so the HUD silently vanished. The HUD must draw whenever the view's
// control is a live, human-claimed walker, regardless of its global slot.
TEST(GladHud, score_panel_draws_for_network_client_with_nonlocal_user_slot)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    // Simulate this client being server-assigned player slot 2 while it renders
    // its own walker in local viewport index 0 (numviews == 1 on a client).
    controlp->set_user(2);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);

    walker* old_control = v->control;
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;  // draws the TEAM/FOES button box
    v->prefs[PREF_LIFE]    = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE]   = PREF_SCORE_OFF;   // score count-up uses rng(); keep off
    v->prefs[PREF_FOES]    = PREF_FOES_ON;

    // The TEAM/FOES counter box lives in the top-right corner: new_score_panel
    // draws it at x in [endx-57, endx-2], y in [tm+1, tm+16].
    const int rm = v->endx;
    const int kX0 = rm - 57;
    const int kX1 = rm - 2;
    constexpr int kY0 = 1;
    constexpr int kY1 = 16;
    auto box_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = kY0; y < kY1; ++y)
            for (int x = kX0; x < kX1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(box_has_pixels(capture_rendered_frame(*s)))
        << "HUD must render for a client whose control->user() (server slot 2) "
           "differs from the local viewport index 0";

    // Negative control: a genuinely uncontrolled walker (user() == -1, e.g. a
    // spectator camera target) must still leave the HUD off.
    controlp->set_user(-1);
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(box_has_pixels(capture_rendered_frame(*s)))
        << "an uncontrolled (user() == -1) view must not paint the HUD";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

TEST(GladHud, fps_overlay_draws_when_enabled)
{
    struct ShowFpsGuard {
        ~ShowFpsGuard() { og::runtime::current_session->show_fps_ = false; }
    } guard;

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);

    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);
    walker* old_control = v->control;
    v->control = controlp;
    // Keep the base HUD off in the top-right strip the overlay scans.
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE]    = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE]   = PREF_SCORE_OFF;
    v->prefs[PREF_FOES]    = PREF_FOES_OFF;

    // The overlay now renders one row below the TEAM/FOES counter box
    // (which spans y in [1,16]), so scan the band just beneath it.
    constexpr int kY0 = 16;
    constexpr int kY1 = 30;
    constexpr int kX0 = 280;
    constexpr int kX1 = 320;

    auto scan_rect_nonzero = [](const std::array<unsigned char, 64000>& frame) {
        for (int y = kY0; y < kY1; ++y)
            for (int x = kX0; x < kX1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    og::runtime::current_session->show_fps_ = true;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_on = capture_rendered_frame(*s);
    ASSERT_TRUE(scan_rect_nonzero(frame_on))
        << "expected FPS overlay pixels in y[" << kY0 << "," << kY1
        << ") x[" << kX0 << "," << kX1 << ")";

    og::runtime::current_session->show_fps_ = false;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_off = capture_rendered_frame(*s);
    ASSERT_FALSE(scan_rect_nonzero(frame_off))
        << "rectangle must be clean when show_fps_ is false";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

TEST(GladHud, fps_overlay_clears_foes_counter)
{
    // Regression: the FPS overlay used to render at y=2, flush right, directly
    // on top of the TEAM/FOES counter that new_score_panel draws in the
    // top-right corner (box spanning y in [1,16]). It must now render strictly
    // below that box. We diff a FOES-on frame with vs. without the overlay;
    // every pixel the overlay touches must lie below the counter box.
    struct ShowFpsGuard {
        ~ShowFpsGuard() { og::runtime::current_session->show_fps_ = false; }
    } guard;

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr) << "control should be created";
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);

    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);
    walker* old_control = v->control;
    v->control = controlp;
    // Draw the TEAM/FOES counter (and its button background) in the top-right.
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_LIFE]    = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE]   = PREF_SCORE_OFF;  // score countup uses rng(); keep off for determinism
    v->prefs[PREF_FOES]    = PREF_FOES_ON;

    // The counter box bottom edge: new_score_panel draws it at tm+16 with
    // tm == yloc (0 for the top viewport) plus zero overscan in the default build.
    constexpr int kFoesBoxBottom = 16;

    og::runtime::current_session->show_fps_ = false;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_without = capture_rendered_frame(*s);

    og::runtime::current_session->show_fps_ = true;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    auto frame_with = capture_rendered_frame(*s);

    bool overlay_drew = false;
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * 320 + x);
            if (frame_with[i] == frame_without[i])
                continue;
            overlay_drew = true;
            ASSERT_GT(y, kFoesBoxBottom)
                << "FPS overlay pixel at (" << x << "," << y
                << ") overlaps the TEAM/FOES counter box (y in [1,"
                << kFoesBoxBottom << "])";
        }
    }
    ASSERT_TRUE(overlay_drew) << "FPS overlay should have rendered some pixels";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

TEST(GladHud, RedrawmeFlickerNoUnpaintedPresent)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);

    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);
    walker* old_control = v->control;
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_LIFE]    = PREF_LIFE_BARS;
    v->prefs[PREF_SCORE]   = PREF_SCORE_ON;
    v->prefs[PREF_FOES]    = PREF_FOES_ON;

    s->redrawme = 1;
    trace_clear();

    s->draw_panels(s->numviews);
    ASSERT_EQ(0, trace_count("present"))
        << "regression: draw_panels must not present; presenting before "
           "score_panel paints the HUD causes the overlay flicker";

    score_panel(s, 1);
    ASSERT_EQ(0, trace_count("present"))
        << "score_panel paints into the back buffer only; it must not present";

    s->buffer_to_screen(0, 0, 320, 200);
    ASSERT_EQ(1, trace_count("present"))
        << "exactly one full-screen present must follow score_panel in the "
           "redrawme path";

    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
    s->redrawme = 0;
}

TEST(GladHud, render_pending_redraw_presents_hud_overlay_in_single_frame)
{
    class SpyScreen final : public screen
    {
    public:
        SpyScreen(GameWorld& world, std::unique_ptr<video> video_impl)
            : screen(world, std::move(video_impl), 1, false)
        {
        }

        void buffer_to_screen(Sint32 viewstartx,
                              Sint32 viewstarty,
                              Sint32 viewwidth,
                              Sint32 viewheight) override
        {
            ++buffer_to_screen_calls;
            last_viewstartx = viewstartx;
            last_viewstarty = viewstarty;
            last_viewwidth = viewwidth;
            last_viewheight = viewheight;
            presented_frame = capture_rendered_frame(*this);
            presented_frame_captured = true;
        }

        int buffer_to_screen_calls = 0;
        Sint32 last_viewstartx = -1;
        Sint32 last_viewstarty = -1;
        Sint32 last_viewwidth = -1;
        Sint32 last_viewheight = -1;
        bool presented_frame_captured = false;
        std::array<unsigned char, 64000> presented_frame{};
    };

    struct ScreenRestoreGuard
    {
        screen*& current_screen;
        screen* saved_screen;

        ~ScreenRestoreGuard()
        {
            current_screen = saved_screen;
        }
    };

    screen*& session_screen = og::runtime::current_session->myscreen_;
    ScreenRestoreGuard restore_guard{session_screen, session_screen};
    GameWorld world;
    {
        SpyScreen spy_screen(world, std::make_unique<sdl_video>(false));
        viewscreen* const view = spy_screen.viewob[0].get();
        ASSERT_TRUE(view != nullptr);

        view->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
        view->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
        view->prefs[PREF_SCORE] = PREF_SCORE_OFF;
        view->prefs[PREF_FOES] = PREF_FOES_OFF;

        auto control = make_player(0);
        ASSERT_TRUE(control != nullptr);
        walker* const controlp = control.get();
        controlp->set_user(0);
        controlp->set_team_num(0);
        controlp->set_dead(0);
        controlp->stats()->set_level(7);
        controlp->stats()->set_hitpoints(55);
        controlp->stats()->set_max_hitpoints(100);
        controlp->stats()->set_magicpoints(33);
        controlp->stats()->set_max_magicpoints(80);
        view->control = controlp;
        spy_screen.world().oblist.push_back(std::move(control));

        ASSERT_EQ(0, spy_screen.buffer_to_screen_calls);

        spy_screen.draw_panels(1);
        const auto overlayless_frame = capture_rendered_frame(spy_screen);

        spy_screen.redrawme = 1;
        og::runtime::detail::render_pending_redraw(spy_screen, true);

        ASSERT_EQ(1, spy_screen.buffer_to_screen_calls);
        EXPECT_TRUE(spy_screen.presented_frame_captured);
        EXPECT_EQ(0, spy_screen.last_viewstartx);
        EXPECT_EQ(0, spy_screen.last_viewstarty);
        EXPECT_EQ(320, spy_screen.last_viewwidth);
        EXPECT_EQ(200, spy_screen.last_viewheight);
        EXPECT_EQ(0, spy_screen.redrawme);
        EXPECT_NE(overlayless_frame, spy_screen.presented_frame);

        spy_screen.draw_panels(1);
        score_panel(&spy_screen, 1);
        EXPECT_EQ(capture_rendered_frame(spy_screen), spy_screen.presented_frame);
    }
}


// ---------------------------------------------------------------------------
// B5: pending-wave HUD (dormant delayed-spawn hostiles)
// ---------------------------------------------------------------------------

namespace {
// Isolate the oblist so prior game state doesn't affect counts.
struct HudObListSwap {
    std::list<std::unique_ptr<walker>> saved;
    HudObListSwap()
    {
        og::runtime::current_session->myscreen_->world().oblist.splice_into(saved);
    }
    ~HudObListSwap()
    {
        og::runtime::current_session->myscreen_->world().oblist.clear();
        og::runtime::current_session->myscreen_->world().oblist.splice(
            og::runtime::current_session->myscreen_->world().oblist.end(), saved);
    }
};
} // namespace

TEST(GladHud, pending_hostile_wave_counts_splits_dormant_and_counts_down)
{
    HudObListSwap swap;
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const std::uint32_t saved_ltc = world.level_tick_count();

    auto control = make_player(0);
    auto ally = make_living(FAMILY_ELF, 0);
    auto awake_foe = make_living(FAMILY_ORC, 1);
    auto wave1 = make_living(FAMILY_ORC, 1);
    auto wave2 = make_living(FAMILY_SKELETON, 1);
    ASSERT_TRUE(control && ally && awake_foe && wave1 && wave2);

    // A dormant ALLY must not count as a pending hostile.
    ally->set_spawn_delay(200);
    ally->set_dormant(true);
    wave1->set_spawn_delay(100);
    wave1->set_dormant(true);
    wave2->set_spawn_delay(60);
    wave2->set_dormant(true);

    walker* const controlp = control.get();
    walker* const wave2p = wave2.get();
    world.oblist.push_back(std::move(control));
    world.oblist.push_back(std::move(ally));
    world.oblist.push_back(std::move(awake_foe));
    world.oblist.push_back(std::move(wave1));
    world.oblist.push_back(std::move(wave2));

    // Wake rule is level_tick_count > spawn_delay, i.e. wake tick is
    // spawn_delay + 1. At tick 12 the nearest wave (delay 60) is 49 away.
    world.set_level_tick_count(12);
    short pending = 0;
    std::uint32_t ticks = 0;
    pending_hostile_wave_counts(world, controlp, pending, ticks);
    EXPECT_EQ(2, static_cast<int>(pending));
    EXPECT_EQ(49u, ticks);

    // Dead dormant hostiles are not pending; the min moves to delay 100.
    wave2p->set_dead(1);
    pending_hostile_wave_counts(world, controlp, pending, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(89u, ticks);

    // Past the wake tick already: the countdown clamps to zero.
    world.set_level_tick_count(500);
    pending_hostile_wave_counts(world, controlp, pending, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(0u, ticks);

    // Null viewer guard.
    pending_hostile_wave_counts(world, nullptr, pending, ticks);
    EXPECT_EQ(0, static_cast<int>(pending));
    EXPECT_EQ(0u, ticks);

    world.set_level_tick_count(saved_ltc);
}

TEST(GladHud, score_panel_shows_next_wave_countdown_for_dormant_hostiles)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();
    const std::uint32_t saved_ltc = world.level_tick_count();

    auto control = make_player(0);
    auto awake_foe = make_living(FAMILY_ORC, 1);
    auto wave = make_living(FAMILY_ORC, 1);
    ASSERT_TRUE(control && awake_foe && wave);
    wave->set_spawn_delay(60);
    wave->set_dormant(true);

    walker* const controlp = control.get();
    walker* const wavep = wave.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));
    world.oblist.push_back(std::move(awake_foe));
    world.oblist.push_back(std::move(wave));

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    world.set_level_tick_count(12);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "next_wave awake=1 pending=1 secs=5"))
        << "one awake foe, one pending wave, 49 ticks -> 5 s at 12 Hz";

    // Once the wave wakes there is nothing pending: classic FOES readout,
    // no NEXT WAVE line.
    wavep->set_dormant(false);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "next_wave"));

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    world.set_level_tick_count(saved_ltc);
}

// ---------------------------------------------------------------------------
// Disabled-special signifier: specials_disabled greys the SPC line
// ---------------------------------------------------------------------------

TEST(GladHud, score_panel_greys_special_line_when_specials_disabled)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    // MP well above the special cost: without the disabled flag this line
    // would draw in the normal text color, never RED, never GREY.
    controlp->stats()->set_magicpoints(80);
    controlp->stats()->set_max_magicpoints(80);
    controlp->stats()->set_special_cost(
        static_cast<int>(controlp->current_special()), 10);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF; // no button box pixels
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;      // the SPC line lives here
    v->prefs[PREF_FOES] = PREF_FOES_OFF;

    // The SPC line draws at (lm+2, bm-24) = (2, 176) for the full pane.
    const int spc_y = v->endy - 24;
    auto count_color_in_spc_row = [&](const std::array<unsigned char, 64000>& frame,
                                      unsigned char color) {
        int n = 0;
        for (int y = spc_y; y < spc_y + 8; ++y)
            for (int x = 2; x < 100; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] == color)
                    ++n;
        return n;
    };

    const unsigned char grey = canonical_palette_index(GREY);
    const unsigned char yellow = canonical_palette_index(YELLOW);

    // Baseline: specials enabled, plenty of MP -> normal color, no grey.
    controlp->set_specials_disabled(false);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "spc_disabled"));
    const auto enabled_frame = capture_rendered_frame(*s);
    EXPECT_EQ(0, count_color_in_spc_row(enabled_frame, grey))
        << "an enabled special must not draw grey";
    EXPECT_GT(count_color_in_spc_row(enabled_frame, yellow), 0)
        << "an enabled, affordable special draws in the normal text color";

    // Disabled: the same line renders GREY regardless of the full MP pool.
    controlp->set_specials_disabled(true);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "spc_disabled"))
        << "the grey path must be taken for a specials-disabled walker";
    const auto disabled_frame = capture_rendered_frame(*s);
    EXPECT_GT(count_color_in_spc_row(disabled_frame, grey), 0)
        << "the SPC line must render in GREY";
    EXPECT_EQ(0, count_color_in_spc_row(disabled_frame, yellow))
        << "no normal-color SPC pixels may remain when disabled";

    controlp->set_specials_disabled(false);
    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
}

// ---------------------------------------------------------------------------
// SCARED countdown: a forced flee (front COMMAND_WALK) shows its seconds left
// ---------------------------------------------------------------------------

TEST(GladHud, hud_scared_flee_ticks_reads_front_walk_command_only)
{
    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();

    // Calm walker: no commands, no countdown.
    controlp->stats()->clear_command();
    EXPECT_EQ(0, hud_scared_flee_ticks(controlp));

    // A forced walk (the ghost-scare / yell-for-help shape) is the state.
    controlp->stats()->force_command(COMMAND_WALK, 25, 1, 0);
    EXPECT_EQ(25, hud_scared_flee_ticks(controlp));
    controlp->stats()->clear_command();

    // A non-walk front command is not fear.
    controlp->stats()->force_command(COMMAND_FIRE, 10, 0, 0);
    EXPECT_EQ(0, hud_scared_flee_ticks(controlp));
    controlp->stats()->clear_command();

    // Dead and null viewers are calm.
    controlp->stats()->force_command(COMMAND_WALK, 25, 1, 0);
    controlp->set_dead(1);
    EXPECT_EQ(0, hud_scared_flee_ticks(controlp));
    controlp->set_dead(0);
    controlp->stats()->clear_command();
    EXPECT_EQ(0, hud_scared_flee_ticks(nullptr));
}

TEST(GladHud, score_panel_shows_scared_countdown_while_fleeing)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    controlp->set_user(0);
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_OFF;

    // The countdown draws at (lm+2, tm+28), just below the HP/MP rows.
    const unsigned char red = canonical_palette_index(RED);
    auto count_red_in_row = [&](const std::array<unsigned char, 64000>& frame) {
        int n = 0;
        for (int y = 28; y < 36; ++y)
            for (int x = 2; x < 90; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] == red)
                    ++n;
        return n;
    };

    // Scared: a ghost-scare-shaped forced walk with 25 sim ticks left reads
    // as ceil(25 / 12) = 3 seconds at the 12 Hz sim rate.
    controlp->stats()->force_command(COMMAND_WALK, 25, 1, 0);
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "scared ticks=25 secs=3"))
        << "25 forced-walk ticks -> 3 s at 12 Hz";
    EXPECT_GT(count_red_in_row(capture_rendered_frame(*s)), 0)
        << "the SCARED line must paint red pixels below the status rows";

    // Calm again: the line disappears with the drained command queue.
    controlp->stats()->clear_command();
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "scared"))
        << "no countdown once the flee command is gone";
    EXPECT_EQ(0, count_red_in_row(capture_rendered_frame(*s)))
        << "the SCARED row must be clean when calm";

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
}

// ---------------------------------------------------------------------------
// B4: one-shot "the way is clear" notice on level_done 0 -> 1
// ---------------------------------------------------------------------------

TEST(GladHud, way_clear_notice_fires_once_per_level_and_rearms_on_new_level)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    GameWorld& world = s->world();
    const int saved_id = world.id;
    const std::uint32_t saved_tick = world.tick_count_;
    const short saved_done = world.level_done;

    for (short i = 0; i < s->numviews; i++)
        s->viewob[i]->clear_text();
    trace_clear();

    static const char* const kWayClearText = "The way is clear -- you may exit";
    const auto view_has_way_clear_text = [&]() {
        for (int slot = 0; slot < MAX_MESSAGES; ++slot)
            if (v->textlist[slot] == kWayClearText)
                return true;
        return false;
    };

    // Mid-level: hostiles alive (level_done == 0). No announcement.
    world.id = 4242;
    world.tick_count_ = 10;
    world.level_done = 0;
    ASSERT_TRUE(s->redraw());
    EXPECT_FALSE(trace_contains("hud", "way_clear"));
    EXPECT_FALSE(view_has_way_clear_text());

    // Foes cleared with a live exit: the sim flips level_done to 1.
    world.level_done = 1;
    world.tick_count_ = 11;
    ASSERT_TRUE(s->redraw());
    EXPECT_TRUE(trace_contains("hud", "way_clear"));
    EXPECT_TRUE(view_has_way_clear_text())
        << "the one-shot notice must be queued on the view";

    // One-shot: another 0 -> 1 swing on the SAME level must not re-fire.
    trace_clear();
    world.level_done = 0;
    world.tick_count_ = 12;
    ASSERT_TRUE(s->redraw());
    world.level_done = 1;
    world.tick_count_ = 13;
    ASSERT_TRUE(s->redraw());
    EXPECT_FALSE(trace_contains("hud", "way_clear"));

    // A new level id re-arms the latch.
    trace_clear();
    world.id = 4243;
    world.tick_count_ = 5;
    world.level_done = 0;
    ASSERT_TRUE(s->redraw());
    world.level_done = 1;
    world.tick_count_ = 6;
    ASSERT_TRUE(s->redraw());
    EXPECT_TRUE(trace_contains("hud", "way_clear"));

    for (short i = 0; i < s->numviews; i++)
        s->viewob[i]->clear_text();
    world.id = saved_id;
    world.tick_count_ = saved_tick;
    world.level_done = saved_done;
}
