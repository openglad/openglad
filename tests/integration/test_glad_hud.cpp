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
// From score_panel.cpp (B5). pending = dormant delayed-spawn hostiles
// (alive, so subtractable from remaining_foes); pending_respawn = hostile
// classic-respawn queue entries (corpses, display-only).
void pending_hostile_wave_counts(const GameWorld& world, walker* viewer,
                                 short& pending, short& pending_respawn,
                                 std::uint32_t& next_wake_ticks);
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

// The HUD probes below intentionally assert historical pixel positions and
// capture fixed 320x200 frames. Pin that canvas only for this suite; live
// layouts may expand one axis to match a non-16:10 display.
class GladHud : public testing::Test
{
protected:
    void SetUp() override
    {
        game_ = og::runtime::current_session->myscreen_;
        ASSERT_NE(nullptr, game_);
        saved_target_ = game_->active_canvas();
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
        game_->set_active_canvas(CanvasTarget::World);
    }

    void TearDown() override
    {
        if (game_ == nullptr)
            return;
        game_->set_active_canvas(CanvasTarget::UI);
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
        game_->set_active_canvas(saved_target_);
    }

private:
    screen* game_ = nullptr;
    CanvasTarget saved_target_ = CanvasTarget::UI;
};

TEST_F(GladHud, glad_remaining_counts)
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


TEST_F(GladHud, glad_draw_gems_and_value_bars_smoke)
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

    // Temporary buffs can put either pool above its nominal maximum.  The
    // modern bar deliberately caps its length while switching to the animated
    // orange/water ramps, rather than wrapping or drawing beyond the frame.
    screen* const s = og::runtime::current_session->myscreen_;
    s->clearbuffer();
    controlp->stats()->set_hitpoints(125);
    new_draw_value_bar(180, 20, controlp, 0, s);
    controlp->stats()->set_magicpoints(100);
    new_draw_value_bar(180, 32, controlp, 1, s);
    const auto buffed_frame = capture_rendered_frame(*s);
    auto colored_pixels = [&](int top, unsigned char ramp_start) {
        std::array<bool, 256> ramp_colors{};
        for (int i = 0; i < 16; ++i)
            ramp_colors[canonical_palette_index(
                static_cast<unsigned char>(ramp_start + i))] = true;
        int count = 0;
        for (int y = top; y < top + 7; ++y)
            for (int x = 180; x < 240; ++x)
                if (ramp_colors[buffed_frame[static_cast<std::size_t>(y * 320 + x)]])
                    ++count;
        return count;
    };
    EXPECT_GT(colored_pixels(20, ORANGE_START), 0)
        << "over-max HP should use the orange buff ramp";
    EXPECT_GT(colored_pixels(32, WATER_START), 0)
        << "over-max MP should use the water buff ramp";

    v->control = control_pointer_is_live(og::runtime::current_session->myscreen_->level_runtime_data(), old_control) ? old_control : nullptr;
}


TEST_F(GladHud, glad_score_panel_and_new_score_panel_modes)
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

    // Exercise all life display variants in new_score_panel.  Start at zero
    // before raising the score so this test observes the real animated
    // count-up transition (and its gameplay RNG), rather than merely drawing
    // a score that was already present when the function-static display state
    // initialized.
    Uint32& score = og::runtime::current_session->myscreen_->world_.m_score[0];
    struct ScoreRestore
    {
        Uint32& destination;
        Uint32 value;
        ~ScoreRestore() { destination = value; }
    } restore_score{score, score};
    score = 0;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    og::runtime::current_session->myscreen_->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel text mode";
    const auto zero_score_frame = capture_rendered_frame(
        *og::runtime::current_session->myscreen_);

    score = 1'000'000u;
    v->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    og::runtime::current_session->myscreen_->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(og::runtime::current_session->myscreen_, 1)) << "new_score_panel bars mode";
    const auto counting_score_frame = capture_rendered_frame(
        *og::runtime::current_session->myscreen_);
    bool score_row_changed = false;
    for (int y = v->endy - 8; y < v->endy; ++y)
        for (int x = v->xloc; x < v->xloc + 70; ++x)
            score_row_changed = score_row_changed ||
                zero_score_frame[static_cast<std::size_t>(y * 320 + x)] !=
                    counting_score_frame[static_cast<std::size_t>(y * 320 + x)];
    EXPECT_TRUE(score_row_changed)
        << "raising the team score must advance the visible SC count";
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

TEST_F(GladHud, quarter_screen_special_is_always_visible)
{
    screen* const s = og::runtime::current_session->myscreen_;

    struct ViewSetRestore
    {
        screen* scr;
        short numviews;
        std::array<std::unique_ptr<viewscreen>, MAX_VIEWS> views;

        explicit ViewSetRestore(screen* screen_ptr)
            : scr(screen_ptr), numviews(screen_ptr->numviews)
        {
            for (int i = 0; i < MAX_VIEWS; ++i)
                views[static_cast<std::size_t>(i)] = std::move(scr->viewob[i]);
        }

        ~ViewSetRestore()
        {
            for (int i = 0; i < MAX_VIEWS; ++i)
                scr->viewob[i].reset();
            for (int i = 0; i < MAX_VIEWS; ++i)
                scr->viewob[i] = std::move(views[static_cast<std::size_t>(i)]);
            scr->numviews = numviews;
            scr->relayout_views();
        }
    } restore_views{s};

    s->numviews = 4;
    s->initialize_views();

    std::array<std::unique_ptr<walker>, 4> controls;
    constexpr std::array<unsigned char, 4> kTeams = {0, 16, 17, 1};
    constexpr std::array<char, 4> kScorePrefs = {
        PREF_SCORE_OFF, PREF_SCORE_ON, PREF_SCORE_OFF, PREF_SCORE_ON};
    constexpr std::array<char, 4> kOverlayPrefs = {
        PREF_OVERLAY_ON, PREF_OVERLAY_ON, PREF_OVERLAY_OFF, PREF_OVERLAY_OFF};
    for (int i = 0; i < 4; ++i)
    {
        const std::size_t seat = static_cast<std::size_t>(i);
        controls[seat] = make_player(kTeams[seat]);
        ASSERT_NE(nullptr, controls[seat]);
        walker* const control = controls[seat].get();
        control->set_user(static_cast<signed char>(i));
        control->set_current_special(1); // Soldier CHARGE
        control->stats()->set_magicpoints(1000.0f);

        viewscreen* const view = s->viewob[i].get();
        ASSERT_NE(nullptr, view);
        view->control = control;
        view->prefs[PREF_OVERLAY] = kOverlayPrefs[seat];
        view->prefs[PREF_LIFE] = PREF_LIFE_OFF;
        view->prefs[PREF_SCORE] = kScorePrefs[seat];
        view->prefs[PREF_FOES] = PREF_FOES_OFF;
    }

    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto actual = capture_rendered_frame(*s);

    s->clearbuffer();
    for (int i = 0; i < 4; ++i)
    {
        const viewscreen* const view = s->viewob[i].get();
        if (kOverlayPrefs[static_cast<std::size_t>(i)] == PREF_OVERLAY_ON)
        {
            const int box_top =
                view->endy - (kScorePrefs[static_cast<std::size_t>(i)] == PREF_SCORE_ON
                                  ? 26
                                  : 10);
            s->draw_button(view->xloc + 1, box_top,
                           view->xloc + 98, view->endy - 2, 1, 1);
        }
        const unsigned char text_color =
            kOverlayPrefs[static_cast<std::size_t>(i)] == PREF_OVERLAY_ON
                ? static_cast<unsigned char>(DARK_BLUE)
                : static_cast<unsigned char>(YELLOW);
        s->text_normal.write_xy(view->xloc + 2, view->endy - 8,
                                "SPC: CHARGE",
                                text_color,
                                static_cast<short>(1));
    }
    const auto expected = capture_rendered_frame(*s);

    for (int i = 0; i < 4; ++i)
    {
        const viewscreen* const view = s->viewob[i].get();
        std::vector<unsigned char> actual_row;
        std::vector<unsigned char> expected_row;
        for (int y = view->endy - 28; y < view->endy; ++y)
        {
            for (int x = view->xloc; x < view->xloc + 100; ++x)
            {
                const std::size_t offset = static_cast<std::size_t>(y * 320 + x);
                actual_row.push_back(actual[offset]);
                expected_row.push_back(expected[offset]);
            }
        }
        EXPECT_EQ(expected_row, actual_row)
            << "seat " << i << " must show its exact current-special row"
            << " (team=" << static_cast<int>(kTeams[static_cast<std::size_t>(i)])
            << ", score_pref="
            << static_cast<int>(kScorePrefs[static_cast<std::size_t>(i)])
            << ", overlay_pref="
            << static_cast<int>(kOverlayPrefs[static_cast<std::size_t>(i)])
            << ")";
    }
}

// Regression: a network client renders a single local viewport (players == 0),
// but its controlled walker carries the server-global player slot in user()
// (set by GameServer::bind_player and shipped to the client in the snapshot).
// new_score_panel used to gate the HUD on `control->user() == players`, which
// only holds for local split-screen (where game.cpp claims view->control with
// set_user(view_idx)). For any non-host client (slot 1/2/3) that check was
// false, so the HUD silently vanished. The HUD must draw whenever the view's
// control is a live, human-claimed walker, regardless of its global slot.
TEST_F(GladHud, score_panel_draws_for_network_client_with_nonlocal_user_slot)
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

// §2.8 follow caption + §4.5 [NET-R6]: a follow-engaged view draws the
// bottom-center "FOLLOWING <name>" strip. Watching an AI target (user() ==
// -1) renders the caption ALONE — the HUD box stays dark, because the follow
// camera never stamps user tags and the HUD gates on them; a followed
// foreign hero keeps its owner's snapshot-synced HUD alongside the caption.
// With following_ off nothing new renders (both polarities pinned).
TEST_F(GladHud, follow_caption_draws_and_ai_target_keeps_hud_dark)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* controlp = control.get();
    controlp->set_user(-1); // AI-shaped follow target
    controlp->set_team_num(0);
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);

    walker* old_control = v->control;
    const bool old_following = v->following_;
    const std::string old_company = v->follow_company_;
    v->control = controlp;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    // The TEAM/FOES counter box (the HUD's most robust probe region).
    const int rm = v->endx;
    const int kBoxX0 = rm - 57;
    const int kBoxX1 = rm - 2;
    constexpr int kBoxY0 = 1;
    constexpr int kBoxY1 = 16;
    auto box_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = kBoxY0; y < kBoxY1; ++y)
            for (int x = kBoxX0; x < kBoxX1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };
    // The caption strip band: score_panel draws the yellow FOLLOWING text at
    // cy = bm - 12 (strip cy-2..cy+8), bottom-center of the viewport.
    // bm == endy in this build (score_panel's OVERSCAN_PADDING is 0 without
    // REDUCE_OVERSCAN).
    const int band_y0 = v->endy - 14;
    const int band_y1 = v->endy - 2;
    auto caption_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = band_y0; y < band_y1; ++y)
            for (int x = v->xloc; x < v->endx; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    // Not following: no caption band, no HUD (user() == -1).
    v->following_ = false;
    v->follow_company_.clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    {
        const auto frame = capture_rendered_frame(*s);
        EXPECT_FALSE(caption_has_pixels(frame))
            << "a non-following view must not paint the caption strip";
        EXPECT_FALSE(box_has_pixels(frame));
    }

    // Following an AI target: the caption alone; the HUD stays dark.
    v->following_ = true;
    v->follow_company_ = "Wolfpack";
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    {
        const auto frame = capture_rendered_frame(*s);
        EXPECT_TRUE(caption_has_pixels(frame))
            << "the FOLLOWING strip must render for an engaged view";
        EXPECT_FALSE(box_has_pixels(frame))
            << "[NET-R6] an AI follow target (user() == -1) must not paint "
               "the HUD";
    }

    // Following a foreign hero (its owner's snapshot-synced tag): the
    // caption AND the owner's HUD.
    controlp->set_user(2);
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    {
        const auto frame = capture_rendered_frame(*s);
        EXPECT_TRUE(caption_has_pixels(frame));
        EXPECT_TRUE(box_has_pixels(frame))
            << "a followed foreign hero shows its owner's HUD";
    }

    v->following_ = old_following;
    v->follow_company_ = old_company;
    v->control = control_pointer_is_live(s->level_runtime_data(), old_control)
        ? old_control : nullptr;
}

TEST_F(GladHud, score_panel_sanitizes_mirrored_identity_and_special_metadata)
{
    screen* const s = og::runtime::current_session->myscreen_;
    viewscreen* const v = s->viewob[0].get();
    ASSERT_NE(nullptr, v);
    auto control = make_player(0);
    ASSERT_NE(nullptr, control);
    walker* const controlp = control.get();
    controlp->set_dead(0);
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(80);
    controlp->stats()->set_max_magicpoints(80);

    walker* const old_control = v->control;
    const bool old_following = v->following_;
    const std::string old_company = v->follow_company_;
    const char old_overlay = v->prefs[PREF_OVERLAY];
    const char old_life = v->prefs[PREF_LIFE];
    const char old_score = v->prefs[PREF_SCORE];
    const char old_foes = v->prefs[PREF_FOES];
    struct ViewRestore
    {
        screen* scr;
        viewscreen* view;
        walker* control;
        bool following;
        std::string company;
        char overlay;
        char life;
        char score;
        char foes;
        ~ViewRestore()
        {
            view->following_ = following;
            view->follow_company_ = std::move(company);
            view->prefs[PREF_OVERLAY] = overlay;
            view->prefs[PREF_LIFE] = life;
            view->prefs[PREF_SCORE] = score;
            view->prefs[PREF_FOES] = foes;
            view->control = control_pointer_is_live(
                scr->level_runtime_data(), control) ? control : nullptr;
        }
    } restore{s, v, old_control, old_following, old_company,
              old_overlay, old_life, old_score, old_foes};

    v->control = controlp;
    v->following_ = true;
    v->follow_company_.clear();
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    v->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    v->prefs[PREF_FOES] = PREF_FOES_OFF;

    // Network mirrors can have no guy object.  Prefer their replicated stats
    // name and cap it to the caption's documented 12-character field.
    ASSERT_NE(nullptr, controlp->myguy);
    controlp->myguy->name.clear();
    controlp->stats()->name = "TwelveChars-then-more";
    controlp->set_user(-1);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto named_frame = capture_rendered_frame(*s);
    int caption_pixels = 0;
    int caption_min_x = 320;
    int caption_max_x = -1;
    const unsigned char yellow = canonical_palette_index(YELLOW);
    for (int y = v->endy - 14; y < v->endy - 2; ++y)
    {
        for (int x = v->xloc; x < v->endx; ++x)
        {
            if (named_frame[static_cast<std::size_t>(y * 320 + x)] != yellow)
                continue;
            ++caption_pixels;
            caption_min_x = std::min(caption_min_x, x);
            caption_max_x = std::max(caption_max_x, x);
        }
    }
    ASSERT_GT(caption_pixels, 0);
    EXPECT_LE(caption_max_x - caption_min_x + 1, 22 * 6)
        << "FOLLOWING plus a capped 12-character name must fit its field";

    // With neither guy nor stats name, fall back to the family label.  A raw
    // signed-byte family from a hostile snapshot is clamped before indexing.
    controlp->clear_myguy();
    controlp->stats()->name.clear();
    controlp->set_family(127);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    EXPECT_NE(named_frame, capture_rendered_frame(*s));

    // The live HUD applies the same bounds policy to family and special, and
    // uses replicated stats for both name and level when no guy exists.
    controlp->stats()->name = "Mirror Hero";
    controlp->set_current_special(127);
    controlp->set_user(0);
    controlp->set_team_num(0);
    v->following_ = false;
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto mirrored_frame = capture_rendered_frame(*s);
    bool mirrored_name_visible = false;
    for (int y = v->yloc + 3; y < v->yloc + 12; ++y)
        for (int x = v->xloc + 2; x < v->xloc + 75; ++x)
            mirrored_name_visible = mirrored_name_visible ||
                mirrored_frame[static_cast<std::size_t>(y * 320 + x)] != 0;
    EXPECT_TRUE(mirrored_name_visible);

    // Invalid team bytes never index the fixed four-team score array; the HUD
    // renders the neutral SC: 0 fallback instead.
    controlp->set_team_num(255);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto invalid_team_frame = capture_rendered_frame(*s);
    bool invalid_score_visible = false;
    for (int y = v->endy - 8; y < v->endy; ++y)
        for (int x = v->xloc; x < v->xloc + 45; ++x)
            invalid_score_visible = invalid_score_visible ||
                invalid_team_frame[static_cast<std::size_t>(y * 320 + x)] != 0;
    EXPECT_TRUE(invalid_score_visible);

    // Find a real alternate-special label and verify holding Shifter changes
    // the rendered special row through the normal HUD path.
    int alternate_family = -1;
    int alternate_special = -1;
    for (int family = 0; family < NUM_FAMILIES && alternate_family < 0; ++family)
    {
        for (int special = 0; special < NUM_SPECIALS; ++special)
        {
            if (s->alternate_name[family][special] != "NONE" &&
                s->alternate_name[family][special] !=
                    s->special_name[family][special])
            {
                alternate_family = family;
                alternate_special = special;
                break;
            }
        }
    }
    ASSERT_GE(alternate_family, 0);
    controlp->set_family(static_cast<char>(alternate_family));
    controlp->set_current_special(static_cast<char>(alternate_special));
    controlp->set_team_num(0);
    controlp->set_shifter_down(0);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto normal_special = capture_rendered_frame(*s);
    controlp->set_shifter_down(1);
    s->clearbuffer();
    ASSERT_EQ(1, new_score_panel(s, 1));
    const auto alternate_special_frame = capture_rendered_frame(*s);
    bool special_row_changed = false;
    for (int y = v->endy - 24; y < v->endy - 16; ++y)
        for (int x = v->xloc; x < v->xloc + 100; ++x)
            special_row_changed = special_row_changed ||
                normal_special[static_cast<std::size_t>(y * 320 + x)] !=
                    alternate_special_frame[static_cast<std::size_t>(y * 320 + x)];
    EXPECT_TRUE(special_row_changed);
}

TEST_F(GladHud, fps_overlay_draws_when_enabled)
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

TEST_F(GladHud, fps_overlay_clears_foes_counter)
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

TEST_F(GladHud, RedrawmeFlickerNoUnpaintedPresent)
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

TEST_F(GladHud, render_pending_redraw_presents_hud_overlay_in_single_frame)
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
        spy_screen.set_world_canvas_pinned_classic(true);
        spy_screen.relayout_views();
        spy_screen.set_active_canvas(CanvasTarget::World);
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

TEST_F(GladHud, pending_hostile_wave_counts_splits_dormant_and_counts_down)
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
    short respawn = 0;
    std::uint32_t ticks = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(2, static_cast<int>(pending));
    EXPECT_EQ(0, static_cast<int>(respawn));
    EXPECT_EQ(49u, ticks);

    // Dead dormant hostiles are not pending; the min moves to delay 100.
    wave2p->set_dead(1);
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(89u, ticks);

    // Past the wake tick already: the countdown clamps to zero.
    world.set_level_tick_count(500);
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(0u, ticks);

    // Null viewer guard.
    pending_hostile_wave_counts(world, nullptr, pending, respawn, ticks);
    EXPECT_EQ(0, static_cast<int>(pending));
    EXPECT_EQ(0, static_cast<int>(respawn));
    EXPECT_EQ(0u, ticks);

    world.set_level_tick_count(saved_ltc);
}

// End-of-scenario honesty for the difficulty respawn modes: a hostile foe
// merely awaiting its respawn timer blocks the extermination win
// (classic_respawn_pending_hostile_foe), so it must show up in the (+x) and
// the countdown instead of leaving "FOES: 0" over a map that refuses to end.
TEST_F(GladHud, pending_hostile_wave_counts_includes_classic_respawn_queue)
{
    HudObListSwap swap;
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const std::uint32_t saved_ltc = world.level_tick_count();
    const short saved_mode = world.respawn_mode;
    const short saved_allied = world.allied_mode;
    const auto saved_queue = world.respawn.respawn_queue;

    auto control = make_player(0);
    auto dormant_foe = make_living(FAMILY_ORC, 1);
    ASSERT_TRUE(control && dormant_foe);
    dormant_foe->set_spawn_delay(100);
    dormant_foe->set_dormant(true);
    walker* const controlp = control.get();
    world.oblist.push_back(std::move(control));
    world.oblist.push_back(std::move(dormant_foe));

    world.set_level_tick_count(12);
    world.respawn_mode = 2;
    world.allied_mode = 1;
    world.respawn.respawn_queue.clear();

    og::sim::RespawnEntry ai_foe; // mode-2 AI replacement: hostile
    ai_foe.kind = 1;
    ai_foe.team = 1;
    ai_foe.ticks_left = 30;
    og::sim::RespawnEntry same_team; // the viewer's own team: never hostile
    same_team.kind = 1;
    same_team.team = 0;
    same_team.ticks_left = 5;
    og::sim::RespawnEntry hero; // different-color company hero: hostile
    hero.kind = 0;
    hero.team = 2;
    hero.ticks_left = 3;
    world.respawn.respawn_queue.push_back(ai_foe);
    world.respawn.respawn_queue.push_back(same_team);
    world.respawn.respawn_queue.push_back(hero);

    short pending = 0;
    short respawn = 0;
    std::uint32_t ticks = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending)) << "the dormant foe";
    EXPECT_EQ(2, static_cast<int>(respawn))
        << "both different-color entries are hostile";
    EXPECT_EQ(3u, ticks) << "the hostile hero has the shortest timer";

    // PVP seating mode cannot change the hostility result.
    world.allied_mode = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(2, static_cast<int>(respawn));
    EXPECT_EQ(3u, ticks);

    // Engine off: leftover queue entries are ignored entirely.
    world.respawn_mode = 0;
    pending_hostile_wave_counts(world, controlp, pending, respawn, ticks);
    EXPECT_EQ(1, static_cast<int>(pending));
    EXPECT_EQ(0, static_cast<int>(respawn));
    EXPECT_EQ(89u, ticks);

    world.respawn.respawn_queue = saved_queue;
    world.respawn_mode = saved_mode;
    world.allied_mode = saved_allied;
    world.set_level_tick_count(saved_ltc);
}

TEST_F(GladHud, score_panel_shows_next_wave_countdown_for_dormant_hostiles)
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

// The endgame moment in respawn mode 2: the last live foe is dead, an AI
// replacement sits in the queue. The panel must show it as a pending wave
// (with the countdown) rather than a bare "FOES: 0" that never ends.
TEST_F(GladHud, score_panel_counts_respawn_pending_foe_in_wave)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();
    const short saved_mode = world.respawn_mode;
    const auto saved_queue = world.respawn.respawn_queue;

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    world.respawn_mode = 2;
    world.respawn.respawn_queue.clear();
    og::sim::RespawnEntry entry;
    entry.kind = 1;
    entry.team = 1;
    entry.ticks_left = 55; // (55 + 11) / 12 -> 5 s at 12 Hz
    world.respawn.respawn_queue.push_back(entry);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "next_wave awake=0 pending=1 secs=5"))
        << "a foe awaiting its respawn timer is a pending wave, not FOES: 0";

    // The end-of-level flush clears the queue (every end shape does): the
    // classic FOES readout returns with no NEXT WAVE line.
    world.respawn.respawn_queue.clear();
    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "next_wave"));

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    world.respawn.respawn_queue = saved_queue;
    world.respawn_mode = saved_mode;
}

// ---------------------------------------------------------------------------
// Disabled-special signifier: specials_disabled greys the SPC line
// ---------------------------------------------------------------------------

TEST_F(GladHud, score_panel_greys_special_line_when_specials_disabled)
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
    v->prefs[PREF_SCORE] = PREF_SCORE_ON;      // keep the legacy SC/XP rows on
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

TEST_F(GladHud, hud_scared_flee_ticks_reads_front_walk_command_only)
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

TEST_F(GladHud, score_panel_shows_scared_countdown_while_fleeing)
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

TEST_F(GladHud, way_clear_notice_fires_once_per_level_and_rearms_on_new_level)
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

// ---------------------------------------------------------------------------
// floor_hud_label (feature B): the pure shared formatter (D5). Every HUD
// surface (SDL FOES-box row, curses status line) draws exactly this string,
// so the format pins live here once.

TEST(FloorHudLabel, empty_on_single_floor)
{
    GameWorld w(0);
    ASSERT_EQ(1, w.floor_count());
    EXPECT_EQ("", floor_hud_label(w, 0));
}

TEST(FloorHudLabel, multifloor_is_one_indexed)
{
    GameWorld w(0);
    w.set_floor_count(3);
    EXPECT_EQ("FLR: 1/3", floor_hud_label(w, 0))
        << "floor 0 reads as 1/3 (players count from one)";
    EXPECT_EQ("FLR: 2/3", floor_hud_label(w, 1));
    EXPECT_EQ("FLR: 3/3", floor_hud_label(w, 2));
}

TEST(FloorHudLabel, clamps_out_of_range_walker_floor)
{
    // Mirror int8 safety: walker_floor is attacker-controllable on a network
    // mirror, so the label clamps instead of printing garbage.
    GameWorld w(0);
    w.set_floor_count(3);
    EXPECT_EQ("FLR: 1/3", floor_hud_label(w, -5));
    EXPECT_EQ("FLR: 3/3", floor_hud_label(w, 99));
}

TEST(FloorHudLabel, tower_single_story_shows_absolute_floor)
{
    GameWorld w(0);
    w.type = SCEN_TYPE_TOWER;
    w.id = 723; // Tower floor 23 (id = kTowerGateLevel + N)
    ASSERT_EQ(1, w.floor_count());
    EXPECT_EQ("FLR: 23", floor_hud_label(w, 0));
}

TEST(FloorHudLabel, tower_multi_story_combines_floor_and_story)
{
    GameWorld w(0);
    w.type = SCEN_TYPE_TOWER;
    w.id = 723;
    w.set_floor_count(3);
    EXPECT_EQ("F23: 2/3", floor_hud_label(w, 1))
        << "8 chars for 2-digit floors -- fits the 53px box right-aligned";
}

TEST(FloorHudLabel, tower_gate_is_unlabeled)
{
    // The Gate (id == kTowerGateLevel) is floor 0 of the climb, not a
    // numbered floor: it falls through to the non-tower branch.
    GameWorld w(0);
    w.type = SCEN_TYPE_TOWER;
    w.id = og::kTowerGateLevel;
    ASSERT_EQ(1, w.floor_count());
    EXPECT_EQ("", floor_hud_label(w, 0));
}

// ---------------------------------------------------------------------------
// The FLR row in the PREF_FOES box (feature B, SDL surface).

TEST_F(GladHud, score_panel_shows_floor_indicator_on_multifloor)
{
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    world.set_floor_count(3);
    controlp->set_floor(1);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(trace_contains("hud", "FLR: 2/3"))
        << "the shared label for floor 1 of 3 reaches the panel";

    // The box grows one row (bottom edge 16 -> 24) and the FLR text renders
    // in the new last row: pixels must appear in the extension band.
    const auto frame = capture_rendered_frame(*s);
    // Zero overscan in the default build: tm == yloc == 0, rm == 320.
    const int tm = 0;
    const int rm = 320;
    bool extension_has_pixels = false;
    for (int y = tm + 17; y <= tm + 24 && !extension_has_pixels; ++y)
        for (int x = rm - 57; x <= rm - 2; ++x)
            if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
            {
                extension_has_pixels = true;
                break;
            }
    EXPECT_TRUE(extension_has_pixels)
        << "FLR row (box extension y[" << tm + 17 << "," << tm + 24
        << "]) should have drawn";

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
    controlp->set_floor(0);
    world.set_floor_count(1);
}

TEST_F(GladHud, score_panel_floor_row_absent_single_floor)
{
    // Byte-identity witness: on a single-floor level the label is empty, the
    // box keeps its classic 16px height, and the extension band stays clean.
    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();
    ASSERT_EQ(1, world.floor_count()) << "fixture must be single-floor";

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    trace_clear();
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(trace_contains("hud", "floor "))
        << "no floor row trace on single-floor levels";

    const auto frame = capture_rendered_frame(*s);
    // Zero overscan in the default build: tm == yloc == 0, rm == 320.
    const int tm = 0;
    const int rm = 320;
    for (int y = tm + 17; y <= tm + 24; ++y)
        for (int x = rm - 57; x <= rm - 2; ++x)
            ASSERT_EQ(0, static_cast<int>(
                          frame[static_cast<std::size_t>(y * 320 + x)]))
                << "pixel at (" << x << "," << y
                << ") must stay clean: single-floor frames are byte-identical";

    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
}

TEST_F(GladHud, fps_overlay_clears_extended_foes_counter)
{
    // Clone of fps_overlay_clears_foes_counter on a 2-floor fixture: the FLR
    // row grows the counter box to tm+24, and the dynamically-placed FPS
    // overlay must move strictly below the taller box.
    struct ShowFpsGuard {
        ~ShowFpsGuard() { og::runtime::current_session->show_fps_ = false; }
    } guard;

    screen* s = og::runtime::current_session->myscreen_;
    viewscreen* v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    HudObListSwap swap;
    GameWorld& world = s->world();

    auto control = make_player(0);
    ASSERT_TRUE(control != nullptr);
    walker* const controlp = control.get();
    controlp->stats()->set_hitpoints(50);
    controlp->stats()->set_max_hitpoints(100);
    controlp->stats()->set_magicpoints(30);
    controlp->stats()->set_max_magicpoints(80);
    world.oblist.push_back(std::move(control));

    world.set_floor_count(2);
    controlp->set_floor(0);

    walker* const old_control = v->control;
    const char old_pref_life = v->prefs[PREF_LIFE];
    const char old_pref_score = v->prefs[PREF_SCORE];
    const char old_pref_foes = v->prefs[PREF_FOES];
    const char old_pref_overlay = v->prefs[PREF_OVERLAY];
    v->control = controlp;
    v->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    v->prefs[PREF_SCORE] = PREF_SCORE_OFF; // score count-up uses rng(); keep off
    v->prefs[PREF_FOES] = PREF_FOES_ON;
    v->prefs[PREF_OVERLAY] = PREF_OVERLAY_ON;

    // Extended box bottom: 16 (classic) + 8 (FLR row) with no wave pending.
    const int kExtendedBoxBottom = 24; // zero overscan in the default build

    og::runtime::current_session->show_fps_ = false;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    const auto frame_without = capture_rendered_frame(*s);

    og::runtime::current_session->show_fps_ = true;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    const auto frame_with = capture_rendered_frame(*s);

    bool overlay_drew = false;
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * 320 + x);
            if (frame_with[i] == frame_without[i])
                continue;
            overlay_drew = true;
            ASSERT_GT(y, kExtendedBoxBottom)
                << "FPS overlay pixel at (" << x << "," << y
                << ") overlaps the extended counter box (bottom "
                << kExtendedBoxBottom << ")";
        }
    }
    ASSERT_TRUE(overlay_drew) << "FPS overlay should have rendered some pixels";

    og::runtime::current_session->show_fps_ = false;
    v->control = old_control;
    v->prefs[PREF_LIFE] = old_pref_life;
    v->prefs[PREF_SCORE] = old_pref_score;
    v->prefs[PREF_FOES] = old_pref_foes;
    v->prefs[PREF_OVERLAY] = old_pref_overlay;
    world.set_floor_count(1);
}


// #232: the frozen-time countdown is HUD state read off world.enemy_freeze
// every frame, not a notification pushed into the message feed (where it
// used to evict every other line for the whole freeze). The cell is
// left-anchored at lm+4 on the bm-34 row — the one band the classic HUD's
// top-left column, its bottom-left score box, and the notification banner
// all leave free.
TEST_F(GladHud, freeze_countdown_cell_draws_from_enemy_freeze)
{
    screen* const s = og::runtime::current_session->myscreen_;
    viewscreen* const v = s->viewob[0].get();
    ASSERT_TRUE(v != nullptr);

    GameWorld& world = s->world();
    const std::int32_t saved_freeze = world.enemy_freeze;

    // "TIME LEFT: 30" at 6px per glyph, on the 6px-tall text row at bm-34.
    const int cell_x0 = v->xloc + 4;
    const int cell_x1 = cell_x0 + 13 * 6;
    const int cell_y0 = v->endy - 34;
    const int cell_y1 = cell_y0 + 6;
    const auto cell_has_pixels = [&](const std::array<unsigned char, 64000>& frame) {
        for (int y = cell_y0; y < cell_y1; ++y)
            for (int x = cell_x0; x < cell_x1; ++x)
                if (frame[static_cast<std::size_t>(y * 320 + x)] != 0)
                    return true;
        return false;
    };

    world.enemy_freeze = 0;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_FALSE(cell_has_pixels(capture_rendered_frame(*s)))
        << "no countdown cell without an active freeze";

    world.enemy_freeze = 30;
    s->clearbuffer();
    ASSERT_EQ(1, (int)new_score_panel(s, 1));
    EXPECT_TRUE(cell_has_pixels(capture_rendered_frame(*s)))
        << "an active freeze must draw the countdown cell";

    world.enemy_freeze = saved_freeze;
}
