// Camera viewscreens (docs/camera-views-design.md §9, WP3): the interface
// view model + lifecycle + docked layout. The camera is a parallel member on
// screen — never an element of viewob[], never counted by numviews — so the
// pins here are the by-construction properties the design rules on: the
// constraint-7 HUD-density keys, the display-only materialization belt, the
// one compute_view_layout pipeline for docked geometry, the flip with no
// intermediate 5-pane layout, teardown re-materialization, the free-camera
// target-loss fallback, and the byte-identical OFF state.
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/view_layout.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/core/test_trace.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gloader.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <utility>

// From glad.cpp (the shared HUD entry point the game loop uses).
short score_panel(screen* scr, short do_it);

namespace
{

// The seat-control shape test_glad_hud uses: a real guy-backed soldier so
// score_panel's user()-gated HUD block runs for the seat.
std::unique_ptr<walker> make_seat_hero(unsigned char team, signed char user)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (!w)
        return nullptr;
    w->set_team_num(team);
    w->set_dead(0);
    w->set_user(user);
    w->setxy(100, 100);
    return w;
}

struct SeatRect
{
    Sint32 xloc = 0;
    Sint32 yloc = 0;
    Sint32 xview = 0;
    Sint32 yview = 0;

    bool operator==(const SeatRect&) const = default;
};

SeatRect capture_rect(const viewscreen& v)
{
    return SeatRect{v.xloc, v.yloc, v.xview, v.yview};
}

class CameraView : public testing::Test
{
protected:
    screen* game_ = nullptr;
    short saved_numviews_ = 0;
    char saved_world_type_ = 0;
    bool saved_mode_active_ = false;
    og::sim::ModeCameraView saved_slot_{};

    void SetUp() override
    {
        game_ = og::runtime::current_session->myscreen_;
        ASSERT_NE(nullptr, game_);
        saved_numviews_ = game_->numviews;
        saved_world_type_ = game_->world().type;
        saved_mode_active_ = game_->world().mode.active;
        saved_slot_ = game_->world().mode.cameras[0];
        // The geometry pins below were authored against the classic canvas.
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
    }

    void TearDown() override
    {
        // Clear the declaration and run the destroy branch so no camera pane
        // (or docked pane count) leaks into the next test.
        game_->world().mode.cameras[0] = og::sim::ModeCameraView{};
        game_->sync_camera_views();
        EXPECT_EQ(nullptr, game_->camera_view_.get());
        EXPECT_FALSE(game_->camera_docked_);
        game_->world().type = saved_world_type_;
        game_->world().mode.active = saved_mode_active_;
        game_->world().mode.cameras[0] = saved_slot_;
        game_->world().delete_objects();
        game_->ready_for_battle(saved_numviews_);
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
    }

    // Seats at a known geometry baseline: FULL view mode, no per-view zoom.
    void arm_seats()
    {
        for (short i = 0; i < game_->numviews; ++i)
        {
            ASSERT_NE(nullptr, game_->viewob[i].get());
            game_->viewob[i]->prefs[PREF_VIEW] = PREF_VIEW_FULL;
            game_->viewob[i]->view_zoom_step_ = 0;
        }
        game_->relayout_views();
    }

    // A camera target living in the display world; returns its entity id.
    std::int32_t spawn_target()
    {
        loader* const l = game_->myloader;
        if (l == nullptr)
            return 0;
        auto w = l->create_walker_owned(Order::Living, FAMILY_ELF);
        if (!w)
            return 0;
        w->set_team_num(2);
        w->set_dead(0);
        w->set_user(-1);
        w->setxy(120, 100);
        walker* const raw = w.get();
        game_->world().oblist.push_back(std::move(w));
        return static_cast<std::int32_t>(raw->entity_id());
    }

    void declare_camera(std::int32_t entity_id,
                        std::uint8_t style = og::sim::kCameraStyleAuto)
    {
        game_->world().type =
            static_cast<char>(game_->world().type | GameWorld::TYPE_SCRIPTED);
        game_->world().mode.active = true;
        game_->world().mode.cameras[0].entity_id = entity_id;
        game_->world().mode.cameras[0].style = style;
    }

    // The docked camera quadrant (and each docked seat) comes from the one
    // compute_view_layout pipeline at pane count 4.
    SeatRect four_pane_rect(int pane) const
    {
        const int ui_w = game_->gameplay_ui_canvas_w();
        const int ui_h = game_->gameplay_ui_canvas_h();
        const og::view_layout::ViewLayout baseline =
            og::view_layout::compute_view_layout(
                4, pane, og::view_layout::kModeFull, ui_w, ui_h);
        const og::view_layout::ViewLayout r =
            og::view_layout::project_view_layout(
                baseline, ui_w, ui_h,
                game_->world_canvas_w(), game_->world_canvas_h());
        return SeatRect{r.x, r.y, r.w, r.h};
    }

    SeatRect inset_rect() const
    {
        const int ui_w = game_->gameplay_ui_canvas_w();
        const int ui_h = game_->gameplay_ui_canvas_h();
        int w = ui_w * 3 / 10;
        int h = ui_h * 3 / 10;
        if (w < 96)
            w = 96;
        if (h < 60)
            h = 60;
        return SeatRect{(ui_w - w) / 2, (ui_h - h) / 2, w, h};
    }
};

// H1 (constraint-7 ON-state pin, written FIRST, before the docked path
// landed): a live docked camera must NOT change the numviews-keyed HUD
// density rules for the existing seats — the compact-panel rule
// (score_panel), the radar alpha rule (radar::draw) and the border rule
// (draw_panel_chrome) all stay keyed on numviews == 3 (humans), never on the
// 4-pane layout count. A future refactor that re-keys any of them onto
// layout_pane_count() turns exactly these traces.
TEST_F(CameraView, docked_camera_keeps_three_seat_hud_rules)
{
    game_->ready_for_battle(3);
    arm_seats();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_TRUE(game_->camera_docked_);
    ASSERT_EQ(4, game_->layout_pane_count());
    ASSERT_EQ(3, game_->numviews) << "numviews stays a pure human seat count";

    // Border rule (screen.cpp draw_panel_chrome): at 3 seats a non-FULL view
    // still draws its bevel border — the numviews == 4 suppression must not
    // fire while a docked camera fills the fourth quadrant.
    for (short i = 0; i < 3; ++i)
        game_->viewob[i]->prefs[PREF_VIEW] = PREF_VIEW_PANELS;
    trace_clear();
    game_->draw_panel_chrome(3);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(trace_contains(
            "hud", std::format("panel_border view={}", i).c_str()))
            << "seat " << i
            << " lost its 3-view border: the border rule was re-keyed";
    for (short i = 0; i < 3; ++i)
        game_->viewob[i]->prefs[PREF_VIEW] = PREF_VIEW_FULL;

    // Radar alpha rule (radar.cpp): seat 0 keeps its opaque radar (the
    // numviews==3 exemption); seats 1-2 keep today's translucent 127.
    {
        LevelRuntimeData radar_level(1);
        radar_level.create_new_grid();
        const std::array<int, 3> expected_alpha = {255, 127, 127};
        for (short seat = 0; seat < 3; ++seat)
        {
            radar r(game_->viewob[seat].get(), game_, seat);
            r.force_lower_position = true;
            r.start(&radar_level);
            trace_clear();
            ASSERT_EQ(1, r.draw(&radar_level));
            EXPECT_TRUE(trace_contains(
                "radar",
                std::format("alpha view={} a={}", seat,
                            expected_alpha[static_cast<std::size_t>(seat)])
                    .c_str()))
                << "seat " << seat << " radar alpha left the 3-view rule";
        }
    }

    // Compact-panel rule (score_panel.cpp): seat 0 keeps the full-density
    // panel (non-compact), seats 1-2 keep today's compact tier.
    std::array<std::unique_ptr<walker>, 3> heroes;
    for (short seat = 0; seat < 3; ++seat)
    {
        heroes[static_cast<std::size_t>(seat)] =
            make_seat_hero(0, static_cast<signed char>(seat));
        ASSERT_NE(nullptr, heroes[static_cast<std::size_t>(seat)]);
        game_->viewob[seat]->control =
            heroes[static_cast<std::size_t>(seat)].get();
    }
    trace_clear();
    score_panel(game_, 1);
    EXPECT_TRUE(trace_contains("hud", "panel_density view=0 compact=0"))
        << "seat 0 lost its full (non-compact) panel";
    EXPECT_TRUE(trace_contains("hud", "panel_density view=1 compact=1"));
    EXPECT_TRUE(trace_contains("hud", "panel_density view=2 compact=1"));
    for (short seat = 0; seat < 3; ++seat)
        game_->viewob[seat]->control = nullptr;
}

// H2: the display screen materializes the camera from the replicated
// declaration; a screen that is not the session's display screen — the
// authoritative server's shape — never does, and the 915-919 no-extra-views
// property holds on both.
TEST_F(CameraView, display_materializes_and_server_screen_stays_null)
{
    game_->ready_for_battle(3);
    arm_seats();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);

    // Mirrors materialize only once the declaration lands: absent before.
    ASSERT_TRUE(game_->redraw());
    EXPECT_EQ(nullptr, game_->camera_view_.get())
        << "no declaration yet: nothing may materialize";

    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    EXPECT_TRUE(game_->camera_view_->camera_view_);
    EXPECT_EQ(-1, game_->camera_view_->mynum);
    EXPECT_EQ(-1, game_->camera_view_->global_player_index_);
    EXPECT_FALSE(game_->camera_view_->following_);
    EXPECT_EQ(game_->world().find_by_id(
                  static_cast<std::uint32_t>(target_id)),
              game_->camera_view_->control);
    // The camera lives outside viewob[]: the display's seat slots above the
    // human count stay null (the test_game_loop 915-919 property).
    for (int v = 3; v < MAX_VIEWS; ++v)
        EXPECT_EQ(nullptr, game_->viewob[v].get())
            << "camera leaked into viewob[" << v << "]";

    // A second screen over a camera-declaring world, not registered as the
    // session's display: the identity belt must refuse to materialize.
    {
        screen*& session_screen = og::runtime::current_session->myscreen_;
        screen* const display = session_screen;
        GameWorld server_world;
        server_world.type = GameWorld::TYPE_SCRIPTED;
        server_world.mode.active = true;
        server_world.mode.cameras[0].entity_id = target_id;
        screen server_screen(server_world,
                             std::make_unique<sdl_video>(false), 3, false);
        // The screen constructor stamps itself as myscreen_; restore the
        // display before exercising the belt.
        session_screen = display;
        server_screen.sync_camera_views();
        EXPECT_EQ(nullptr, server_screen.camera_view_.get())
            << "the authority materialized a camera";
        EXPECT_FALSE(server_screen.camera_docked_);
        EXPECT_EQ(3, server_screen.layout_pane_count());
        for (int v = 3; v < MAX_VIEWS; ++v)
            EXPECT_EQ(nullptr, server_screen.viewob[v].get());
        // The display's camera is untouched by the foreign screen's pass.
        EXPECT_NE(nullptr, display->camera_view_.get());
    }
}

// H3: 3 seats + auto -> docked. Every seat's live rect equals the real
// compute_view_layout(4, i, ...) projection and the camera fills quadrant 3
// — the one layout pipeline, no parallel geometry math.
TEST_F(CameraView, docked_geometry_comes_from_the_four_pane_pipeline)
{
    game_->ready_for_battle(3);
    arm_seats();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_TRUE(game_->camera_docked_);

    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(four_pane_rect(i), capture_rect(*game_->viewob[i]))
            << "seat " << i << " is not in its 4-pane quadrant";
    EXPECT_EQ(four_pane_rect(3), capture_rect(*game_->camera_view_))
        << "camera is not in quadrant 3";
    // Direct geometry: slot == window, so the camera publishes no present
    // slice and the presentation partition never sees it.
    EXPECT_EQ(game_->camera_view_->xloc, game_->camera_view_->slot_x_);
    EXPECT_EQ(game_->camera_view_->yloc, game_->camera_view_->slot_y_);
    EXPECT_EQ(game_->camera_view_->xview, game_->camera_view_->slot_w_);
    EXPECT_EQ(game_->camera_view_->yview, game_->camera_view_->slot_h_);
}

// H4: 1 seat + auto -> inset. The camera carries the centered GameplayUI
// inset geometry; the seat's geometry is byte-identical to the no-camera run.
TEST_F(CameraView, single_seat_auto_resolves_to_inset)
{
    game_->ready_for_battle(1);
    arm_seats();
    const SeatRect seat_before = capture_rect(*game_->viewob[0]);
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    EXPECT_FALSE(game_->camera_docked_);
    EXPECT_EQ(1, game_->layout_pane_count());
    EXPECT_EQ(seat_before, capture_rect(*game_->viewob[0]))
        << "an inset camera must not move the seat";
    EXPECT_EQ(inset_rect(), capture_rect(*game_->camera_view_));
    // The WP4 draw stub must not perturb resolution or geometry either.
    game_->draw_camera_view_ui();
    EXPECT_EQ(inset_rect(), capture_rect(*game_->camera_view_));
    EXPECT_EQ(seat_before, capture_rect(*game_->viewob[0]));

    // style = "inset" forces the inset even where auto would dock (H-item 4's
    // style arm, exercised at 3 seats in the flip test below via auto).
    game_->world().mode.cameras[0].style = og::sim::kCameraStyleInset;
    ASSERT_TRUE(game_->redraw());
    EXPECT_FALSE(game_->camera_docked_);
    EXPECT_EQ(inset_rect(), capture_rect(*game_->camera_view_));
}

// H5: seat add 3->4 flips docked->inset with no intermediate 5-pane layout
// observable (the relayout-top recompute ruling); seat remove flips back.
TEST_F(CameraView, seat_add_flips_docked_to_inset_without_five_pane_layout)
{
    game_->ready_for_battle(3);
    arm_seats();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_TRUE(game_->camera_docked_);

    // Seat add, the shape seat surgery uses: construct the view, raise
    // numviews, then relayout (local_transport_shadow ends seat surgery in
    // relayout_views()).
    game_->viewob[3] = std::make_unique<viewscreen>(
        static_cast<short>(0), static_cast<short>(0), static_cast<short>(0),
        static_cast<short>(0), static_cast<short>(3));
    game_->viewob[3]->prefs[PREF_VIEW] = PREF_VIEW_FULL;
    game_->viewob[3]->view_zoom_step_ = 0;
    game_->numviews = 4;
    trace_clear();
    game_->relayout_views();
    EXPECT_FALSE(trace_contains("layout", "panes=5"))
        << "a seat resize consumed a stale docked flag: 5-pane layout";
    EXPECT_FALSE(game_->camera_docked_);
    EXPECT_EQ(4, game_->layout_pane_count());
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(four_pane_rect(i), capture_rect(*game_->viewob[i]));
    EXPECT_EQ(inset_rect(), capture_rect(*game_->camera_view_))
        << "the camera did not flip to the inset geometry";
    ASSERT_TRUE(game_->redraw());
    EXPECT_NE(nullptr, game_->camera_view_.get());

    // Seat remove: back to 3 humans -> the camera re-docks into quadrant 3.
    game_->viewob[3].reset();
    game_->numviews = 3;
    game_->relayout_views();
    EXPECT_TRUE(game_->camera_docked_);
    EXPECT_EQ(4, game_->layout_pane_count());
    EXPECT_EQ(four_pane_rect(3), capture_rect(*game_->camera_view_));
}

// H6: ready_for_battle teardown destroys the camera through the cleanup
// hook; the next redraw re-materializes it from the still-declared slot.
TEST_F(CameraView, teardown_then_redraw_rematerializes)
{
    game_->ready_for_battle(3);
    arm_seats();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());

    game_->ready_for_battle(3);
    EXPECT_EQ(nullptr, game_->camera_view_.get())
        << "cleanup must destroy the camera";
    EXPECT_FALSE(game_->camera_docked_);

    arm_seats();
    ASSERT_TRUE(game_->redraw());
    EXPECT_NE(nullptr, game_->camera_view_.get())
        << "the still-replicated declaration must re-materialize";
    EXPECT_TRUE(game_->camera_docked_);
}

// H7: an unresolvable id keeps the pane alive and drawing with control ==
// nullptr (the LevelVisuals free-camera fallback), with no layout change.
TEST_F(CameraView, unresolvable_id_keeps_pane_on_free_camera)
{
    game_->ready_for_battle(3);
    arm_seats();
    declare_camera(424242);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    EXPECT_EQ(nullptr, game_->camera_view_->control)
        << "an unresolvable id must fall back to the free camera";
    EXPECT_TRUE(game_->camera_docked_);
    const SeatRect cam_rect = capture_rect(*game_->camera_view_);
    EXPECT_EQ(four_pane_rect(3), cam_rect);

    // Still alive and still degraded on the next frame — never destroyed,
    // never a layout change.
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    EXPECT_EQ(nullptr, game_->camera_view_->control);
    EXPECT_EQ(cam_rect, capture_rect(*game_->camera_view_));

    // A target appearing under that id later resolves (retarget every frame).
    const std::int32_t real_id = spawn_target();
    ASSERT_NE(0, real_id);
    game_->world().mode.cameras[0].entity_id = real_id;
    ASSERT_TRUE(game_->redraw());
    EXPECT_EQ(game_->world().find_by_id(static_cast<std::uint32_t>(real_id)),
              game_->camera_view_->control);
}

// H8: OFF-state byte-identity — with no declaration, layout_pane_count() ==
// numviews, no camera exists, seat geometry does not move, and every seat
// window fills its slot (no present slice can exist for it).
TEST_F(CameraView, off_state_changes_nothing)
{
    game_->ready_for_battle(3);
    arm_seats();
    std::array<SeatRect, 3> before;
    for (int i = 0; i < 3; ++i)
        before[static_cast<std::size_t>(i)] = capture_rect(*game_->viewob[i]);

    ASSERT_TRUE(game_->redraw());
    EXPECT_EQ(nullptr, game_->camera_view_.get());
    EXPECT_FALSE(game_->camera_docked_);
    EXPECT_EQ(game_->numviews, game_->layout_pane_count());
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(before[static_cast<std::size_t>(i)],
                  capture_rect(*game_->viewob[i]))
            << "seat " << i << " moved with no camera declared";
        EXPECT_EQ(game_->viewob[i]->xloc, game_->viewob[i]->slot_x_);
        EXPECT_EQ(game_->viewob[i]->yloc, game_->viewob[i]->slot_y_);
        EXPECT_EQ(game_->viewob[i]->xview, game_->viewob[i]->slot_w_);
        EXPECT_EQ(game_->viewob[i]->yview, game_->viewob[i]->slot_h_);
    }
}

// H9: a staged/preview draw of a camera-declaring world goes through
// viewscreen::redraw on a borrowed view — never screen::redraw on the
// gameplay session — and must not materialize a camera or move geometry.
TEST_F(CameraView, staged_preview_never_materializes)
{
    game_->ready_for_battle(1);
    arm_seats();
    const SeatRect seat_before = capture_rect(*game_->viewob[0]);

    // A staged world (the picker VIEW LEVEL shape) whose mode declares a
    // camera slot; the session's own world stays camera-free.
    LevelRuntimeData staged(1);
    staged.create_new_grid();
    staged.world().type = GameWorld::TYPE_SCRIPTED;
    staged.world().mode.active = true;
    staged.world().mode.cameras[0].entity_id = 4242;

    viewscreen* const view = game_->viewob[0].get();
    ASSERT_NE(nullptr, view);
    // The picker's borrowed-view composition: null control (free camera),
    // direct-geometry resize into a band, draw the staged data.
    walker* const saved_control = view->control;
    const SeatRect saved_rect = capture_rect(*view);
    view->control = nullptr;
    view->resize(static_cast<short>(8), static_cast<short>(8),
                 static_cast<short>(160), static_cast<short>(100));
    (void)view->redraw(&staged, /*draw_radar=*/false);
    EXPECT_EQ(nullptr, game_->camera_view_.get())
        << "a staged preview draw materialized a camera";
    EXPECT_EQ(game_->numviews, game_->layout_pane_count());
    view->control = saved_control;
    view->resize(static_cast<short>(saved_rect.xloc),
                 static_cast<short>(saved_rect.yloc),
                 static_cast<short>(saved_rect.xview),
                 static_cast<short>(saved_rect.yview));
    view->resize(view->prefs[PREF_VIEW]);
    EXPECT_EQ(seat_before, capture_rect(*game_->viewob[0]));
    EXPECT_EQ(nullptr, game_->camera_view_.get());
}

} // namespace
