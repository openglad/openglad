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
#include <openglad/interface/base.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/video.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
        // A known grid for every case in this file: the one-seat camera
        // geometry mirrors the radar block, whose extents clamp to the live
        // grid — without this the rect would depend on what the previous
        // test in the binary left behind (--gtest_shuffle).
        game_->world().create_new_grid();
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
    std::int32_t spawn_target(Sint32 x = 120, Sint32 y = 100)
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
        w->setxy(x, y);
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

    // The seat's radar block, through the SHARED placement rule the radar
    // itself uses (radar_block_for_pane over the seat's UI pane and the live
    // grid's clamp) — so nothing here can drift from where the minimap
    // actually lands, and no rect literal is typed by hand.
    RadarBlock seat_radar_block() const
    {
        const int ui_w = game_->gameplay_ui_canvas_w();
        const int ui_h = game_->gameplay_ui_canvas_h();
        const int mode = (game_->viewob[0] != nullptr)
            ? static_cast<int>(game_->viewob[0]->prefs[PREF_VIEW])
            : og::view_layout::kModeFull;
        const og::view_layout::ViewLayout pane =
            og::view_layout::compute_view_layout(
                game_->layout_pane_count(), 0, mode, ui_w, ui_h);
        EXPECT_TRUE(pane.applies);
        const auto [block_w, block_h] = radar_block_extents(
            game_->world().grid.w, game_->world().grid.h);
        return radar_block_for_pane(pane.y, pane.x + pane.w,
                                    pane.y + pane.h, block_w, block_h,
                                    /*force_lower=*/false);
    }

    // The one-seat SECOND MINIMAP rect (maintainer ruling): the radar block
    // mirrored — same size, same right edge, stacked directly above the
    // radar with the radar's own pane margin between them.
    SeatRect second_minimap_rect() const
    {
        const RadarBlock block = seat_radar_block();
        return SeatRect{block.x, block.y - block.margin - block.h, block.w,
                        block.h};
    }

    // The centered GameplayUI inset the 2- and 4-seat layouts keep.
    SeatRect centered_inset_rect() const
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

    // The non-docked camera geometry for the live seat count: the second
    // minimap at 1 seat, the centered inset at 2/4.
    SeatRect inset_rect() const
    {
        if (game_->numviews == 1)
            return second_minimap_rect();
        return centered_inset_rect();
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
    EXPECT_FALSE(game_->camera_minimap_zoom_)
        << "the docked quadrant is a full-size pane and stays 1:1";
    // Direct geometry: slot == window, so the camera publishes no present
    // slice and the presentation partition never sees it.
    EXPECT_EQ(game_->camera_view_->xloc, game_->camera_view_->slot_x_);
    EXPECT_EQ(game_->camera_view_->yloc, game_->camera_view_->slot_y_);
    EXPECT_EQ(game_->camera_view_->xview, game_->camera_view_->slot_w_);
    EXPECT_EQ(game_->camera_view_->yview, game_->camera_view_->slot_h_);
}

// H4: 1 seat + auto -> inset. At one seat the inset is the SECOND MINIMAP
// (maintainer ruling): the seat camera already centres the hero on the
// canvas, so the pane mirrors the radar block above the radar instead of
// covering him. The seat's geometry is byte-identical to the no-camera run.
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
    // Spelled out against the radar block itself, so the ruling — not just a
    // formula — is what has teeth here.
    {
        const RadarBlock block = seat_radar_block();
        const viewscreen& cam = *game_->camera_view_;
        EXPECT_EQ(block.w, cam.xview) << "the pane must match the radar block";
        EXPECT_EQ(block.h, cam.yview);
        EXPECT_EQ(block.x, cam.xloc) << "same right-edge alignment";
        EXPECT_EQ(block.y, cam.yloc + cam.yview + block.margin)
            << "the pane must sit one radar margin ABOVE the radar block";
        // And decidedly NOT on top of the hero at the canvas centre.
        EXPECT_NE(centered_inset_rect(), capture_rect(cam));
        const int ui_cx = game_->gameplay_ui_canvas_w() / 2;
        const int ui_cy = game_->gameplay_ui_canvas_h() / 2;
        EXPECT_FALSE(ui_cx >= cam.xloc && ui_cx < cam.xloc + cam.xview &&
                     ui_cy >= cam.yloc && ui_cy < cam.yloc + cam.yview)
            << "the one-seat pane covers the canvas centre — the hero";
    }
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

// The split the ruling draws: 2 seats keep the CENTERED inset. The canvas
// centre is a pane boundary at 2 seats (nobody's hero sits there) and the
// bottom-right corner belongs to seat 1's radar, so the second-minimap
// placement is a one-seat rule only. Pins the other side of the branch.
TEST_F(CameraView, two_seats_keep_the_centered_inset)
{
    game_->ready_for_battle(2);
    arm_seats();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    EXPECT_FALSE(game_->camera_docked_);
    EXPECT_EQ(2, game_->layout_pane_count());
    EXPECT_EQ(centered_inset_rect(), capture_rect(*game_->camera_view_));
    EXPECT_NE(second_minimap_rect(), capture_rect(*game_->camera_view_))
        << "2 seats must not take the one-seat second-minimap placement";
    EXPECT_FALSE(game_->camera_minimap_zoom_)
        << "the centered inset is a full-size pane and stays 1:1";
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

// WP4 pixel probes. All three tests read the GameplayUI canvas under the
// production canvas scope; on the classic-pinned fixture that target aliases
// the persistent World surface (begin_gameplay_frame arms the independent
// overlay only at non-classic dims), which is exactly why the stale-pixel
// rule below exists.
bool pixel_is_black(screen* scr, int x, int y)
{
    Uint8 r = 1, g = 1, b = 1;
    scr->get_pixel(x, y, &r, &g, &b);
    return r == 0 && g == 0 && b == 0;
}

bool pixel_matches_index(screen* scr, int x, int y, unsigned char index)
{
    Uint8 r = 0, g = 0, b = 0;
    scr->get_pixel(x, y, &r, &g, &b);
    int pr = 0, pg = 0, pb = 0;
    query_palette_reg(index, &pr, &pg, &pb);
    return r / 4 == pr && g / 4 == pg && b / 4 == pb;
}

// H10 prototype gate (risk #1, the least-proven render path): the inset draw
// — the staged-preview composition aimed at the live level data under the
// GameplayUI canvas scope — must actually land world pixels inside the inset
// rect, frame them with the 1px GREY border, and keep direct geometry
// (slot == window, so no present slice can ever exist for the camera).
TEST_F(CameraView, inset_draw_lands_world_pixels_and_border)
{
    game_->ready_for_battle(1);
    arm_seats();
    // A real grid to draw (the prepare_view_world pattern): grass tiles are
    // non-black, so the probe below measures actual world content.
    game_->world().create_new_grid();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_FALSE(game_->camera_docked_);
    const SeatRect r = inset_rect();
    ASSERT_EQ(r, capture_rect(*game_->camera_view_));

    // Scrub the rect + border ring first, and prove the probe sees black:
    // any pixel found after the draw below was drawn by the inset pass, not
    // left over from the seat's world redraw underneath.
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->clearbuffer(r.xloc - 1, r.yloc - 1, r.xview + 2, r.yview + 2);
        for (int y = r.yloc - 1; y <= r.yloc + r.yview; y += 3)
            for (int x = r.xloc - 1; x <= r.xloc + r.xview; x += 3)
                ASSERT_TRUE(pixel_is_black(game_, x, y))
                    << "scrub failed at " << x << "," << y;
    }

    // The game_loop seam idiom, verbatim.
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->draw_camera_view_ui();
    }

    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        int samples = 0;
        int lit = 0;
        for (int y = r.yloc; y < r.yloc + r.yview; y += 4)
            for (int x = r.xloc; x < r.xloc + r.xview; x += 4)
            {
                ++samples;
                if (!pixel_is_black(game_, x, y))
                    ++lit;
            }
        EXPECT_GT(lit, samples / 4)
            << "the inset world draw landed too few pixels (" << lit << "/"
            << samples << ") — the GameplayUI-canvas world draw is broken";
        // The 1px GREY frame, all four corners (draw_box is inclusive).
        const int x2 = r.xloc + r.xview;
        const int y2 = r.yloc + r.yview;
        EXPECT_TRUE(pixel_matches_index(game_, r.xloc - 1, r.yloc - 1, GREY));
        EXPECT_TRUE(pixel_matches_index(game_, x2, r.yloc - 1, GREY));
        EXPECT_TRUE(pixel_matches_index(game_, r.xloc - 1, y2, GREY));
        EXPECT_TRUE(pixel_matches_index(game_, x2, y2, GREY));
    }

    // Direct geometry: no present slice can exist for the camera.
    EXPECT_EQ(game_->camera_view_->xloc, game_->camera_view_->slot_x_);
    EXPECT_EQ(game_->camera_view_->yloc, game_->camera_view_->slot_y_);
    EXPECT_EQ(game_->camera_view_->xview, game_->camera_view_->slot_w_);
    EXPECT_EQ(game_->camera_view_->yview, game_->camera_view_->slot_h_);
}

// H10: after a nil-clear the old inset rect on the GameplayUI canvas holds
// no stale camera pixels — the destroy branch scrubs rect + border ring and
// forces a repaint (redrawme), because on the classic alias path nothing
// else ever clears that canvas.
TEST_F(CameraView, nil_clear_scrubs_the_inset_rect)
{
    game_->ready_for_battle(1);
    arm_seats();
    game_->world().create_new_grid();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    const SeatRect r = inset_rect();
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->draw_camera_view_ui();
        // Sanity: the pane is visibly there before the nil-clear (the border
        // is the deterministic pixel — world content is asserted above).
        ASSERT_TRUE(pixel_matches_index(
            game_, r.xloc - 1, r.yloc - 1, GREY));
    }

    game_->world().mode.cameras[0] = og::sim::ModeCameraView{};
    game_->redrawme = 0;
    game_->sync_camera_views();
    ASSERT_EQ(nullptr, game_->camera_view_.get());
    EXPECT_EQ(1, game_->redrawme)
        << "the scrub must force a repaint underneath";
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        for (int y = r.yloc - 1; y <= r.yloc + r.yview; ++y)
            for (int x = r.xloc - 1; x <= r.xloc + r.xview; ++x)
                ASSERT_TRUE(pixel_is_black(game_, x, y))
                    << "stale camera pixel at " << x << "," << y
                    << " after the nil-clear";
    }
}

// Forces the per-walker overlay cfg keys ON for a test body (the shipped
// defaults, but earlier tests in the binary may have flipped them through
// their own guards); restores the saved values on exit.
class OverlayCfgOnGuard
{
public:
    OverlayCfgOnGuard()
    {
        for (const char* const key : kKeys)
        {
            saved_.emplace_back(key, cfg.get_setting("effects", key));
            cfg.apply_setting("effects", key, "on");
        }
    }
    ~OverlayCfgOnGuard()
    {
        for (const auto& [key, value] : saved_)
            cfg.apply_setting("effects", key,
                              value.empty() ? "on" : value);
    }
    OverlayCfgOnGuard(const OverlayCfgOnGuard&) = delete;
    OverlayCfgOnGuard& operator=(const OverlayCfgOnGuard&) = delete;

private:
    static constexpr std::array<const char*, 2> kKeys = {
        "mini_hp_bar", "heal_numbers"};
    std::vector<std::pair<std::string, std::string>> saved_;
};

// R1 (code-review finding): a camera pane draws NO per-walker UI overlays —
// no mini HP bars, no damage/heal numbers (the design's §6 suppression list,
// extended by the review ruling). Their GameplayUI projection is keyed on
// layout_pane_count() + mynum, which has no arm for the camera identity: at
// 1 seat the full-canvas layout would scale a bar for a walker in the
// second-minimap pane across the whole 320x200 UI canvas. Teeth: a damaged
// living (below
// the 95% bar threshold, plus a live heal number) inside the camera's world
// view but far OUTSIDE the seat's own view, so any overlay pixel can only
// have come from the camera pane; after a full-canvas scrub the inset seam
// draw must land pixels only inside the inset rect (+1px border), and the
// inset content must be pixel-identical between the damaged and the healed
// walker — total suppression, outside AND inside the pane.
TEST_F(CameraView, camera_pane_draws_no_per_walker_ui_overlays)
{
    OverlayCfgOnGuard overlay_cfg;
    game_->ready_for_battle(1);
    arm_seats();
    game_->world().create_new_grid();
    // (400,700): well outside the 1-seat view of the level origin.
    const std::int32_t target_id = spawn_target(400, 700);
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    // Both seams: screen::redraw (materialize + retarget, settles the seat),
    // then the game_loop inset seam idiom (settles the camera on its target
    // before any probe below).
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_FALSE(game_->camera_docked_);
    const SeatRect r = inset_rect();
    ASSERT_EQ(r, capture_rect(*game_->camera_view_));
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->draw_camera_view_ui();
    }

    walker* const target =
        game_->world().find_by_id(static_cast<std::uint32_t>(target_id));
    ASSERT_NE(nullptr, target);
    ASSERT_EQ(target, game_->camera_view_->control)
        << "the damaged walker must be the one inside the camera view";
    const float max_hp = target->stats()->max_hitpoints();
    ASSERT_GT(max_hp, 0.0f);

    const auto scrub_canvas_and_seam_draw = [&]() {
        {
            ScopedGameplayUiCanvas gameplay_ui(*game_);
            game_->clearbuffer(0, 0, game_->canvas_w(), game_->canvas_h());
        }
        {
            ScopedGameplayUiCanvas gameplay_ui(*game_);
            game_->draw_camera_view_ui();
        }
    };
    const auto capture_inset = [&]() {
        std::vector<Uint32> pixels;
        pixels.reserve(static_cast<std::size_t>(r.xview * r.yview));
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        for (int y = r.yloc; y < r.yloc + r.yview; ++y)
            for (int x = r.xloc; x < r.xloc + r.xview; ++x)
            {
                Uint8 pr = 0, pg = 0, pb = 0;
                game_->get_pixel(x, y, &pr, &pg, &pb);
                pixels.push_back((static_cast<Uint32>(pr) << 16) |
                                 (static_cast<Uint32>(pg) << 8) |
                                 static_cast<Uint32>(pb));
            }
        return pixels;
    };

    // Damaged: below the bar threshold, with a last-hit segment and a live
    // heal number (the camera IS this walker's control, so pre-suppression
    // the number block would fire from the camera pane too).
    target->set_last_hitpoints(max_hp);
    target->stats()->set_hitpoints(max_hp * 0.5f);
    target->damage_numbers.emplace_back(
        static_cast<float>(target->xpos()), static_cast<float>(target->ypos()),
        25.0f, static_cast<unsigned char>(56), game_->world().tick_count_);
    scrub_canvas_and_seam_draw();
    {
        // Outside the inset rect and its 1px border ring the scrubbed canvas
        // must still be black: nothing from the camera pane may leak out.
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        for (int y = 0; y < game_->canvas_h(); y += 2)
            for (int x = 0; x < game_->canvas_w(); x += 2)
            {
                if (x >= r.xloc - 1 && x <= r.xloc + r.xview &&
                    y >= r.yloc - 1 && y <= r.yloc + r.yview)
                    continue;
                ASSERT_TRUE(pixel_is_black(game_, x, y))
                    << "camera-pane overlay leaked outside the inset at "
                    << x << "," << y;
            }
    }
    const std::vector<Uint32> damaged = capture_inset();

    // Healed baseline: at full HP with no numbers, no overlay can
    // legitimately draw anywhere — any inside-inset difference below was a
    // per-walker UI overlay drawn by the camera pane.
    target->stats()->set_hitpoints(max_hp);
    target->set_last_hitpoints(max_hp);
    target->damage_numbers.clear();
    scrub_canvas_and_seam_draw();
    const std::vector<Uint32> healed = capture_inset();
    ASSERT_EQ(damaged.size(), healed.size());
    int diffs = 0;
    for (std::size_t i = 0; i < damaged.size(); ++i)
        if (damaged[i] != healed[i])
            ++diffs;
    EXPECT_EQ(0, diffs)
        << diffs << " inset pixels changed with the walker damaged: a "
        << "per-walker UI overlay was drawn inside the camera pane";
}

// R2 (code-review finding): when the fixed GameplayUI overlay is not active
// for the frame (allocation failure under a split graphics/zoom world
// canvas), the GameplayUI scope deliberately aliases the larger World
// surface, and the inset's fixed classic-density coords would land inside
// seat world pixels. draw_camera_view_ui must skip the inset draw and
// clear_camera_inset_rect must skip the scrub for that frame — the pane is
// simply absent in the degraded mode, like every other adapting HUD path
// (GameplayUiProjector, ScopedGameplayUiViewLayout, touch controls).
TEST_F(CameraView, overlay_allocation_failure_skips_inset_draw_and_scrub)
{
    ASSERT_TRUE(E_Screen);
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(E_Screen->window, &win_w, &win_h);

    game_->ready_for_battle(1);
    arm_seats();
    game_->world().create_new_grid();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_FALSE(game_->camera_docked_);
    const SeatRect r = inset_rect();

    // The maintained degraded mode (test_canvas_scale's allocation-failure
    // pattern): a split 640x400 world canvas whose overlay allocation fails,
    // so GameplayUI aliases World while the fixed UI dims stay 320x200.
    game_->set_world_canvas_pinned_classic(false);
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());
    ASSERT_EQ(320, game_->gameplay_ui_canvas_w());
    ASSERT_EQ(200, game_->gameplay_ui_canvas_h());
    game_->relayout_views();
    ASSERT_EQ(r, capture_rect(*game_->camera_view_))
        << "the inset geometry stays in fixed UI coords";
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->fail_next_gameplay_ui_allocation_for_testing();
    E_Screen->begin_gameplay_frame();
    ASSERT_FALSE(E_Screen->gameplay_ui_overlay_active());

    // Sentinel-fill the classic-coord rect (+ border and scrub ring) on the
    // aliased World surface: any inset draw or scrub would overwrite it.
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        ASSERT_EQ(640, game_->canvas_w())
            << "the GameplayUI scope must alias the split World canvas";
        game_->draw_box(r.xloc - 2, r.yloc - 2,
                        r.xloc + r.xview + 1, r.yloc + r.yview + 1,
                        ORANGE_START, 1, 1);
    }
    game_->redrawme = 0;
    game_->draw_camera_view_ui();
    game_->clear_camera_inset_rect(r.xloc, r.yloc, r.xview, r.yview);
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        for (int y = r.yloc - 2; y <= r.yloc + r.yview + 1; ++y)
            for (int x = r.xloc - 2; x <= r.xloc + r.xview + 1; ++x)
                ASSERT_TRUE(pixel_matches_index(game_, x, y, ORANGE_START))
                    << "pixel written at " << x << "," << y
                    << " on the aliased World canvas in the degraded mode";
    }
    EXPECT_EQ(0, game_->redrawme)
        << "a skipped scrub must not force a repaint";

    // Restore the classic fixture state (the failed-size latch clears on the
    // world-canvas change; the ClassicCanvasRestore recipe minus the window
    // resize this test never made).
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer,
                             win_w, win_h);
    E_Screen->set_active_canvas(CanvasTarget::World);
    game_->set_world_canvas_pinned_classic(true);
    game_->relayout_views();
}

// H10 (flip arm): restyling inset -> docked goes through relayout_views,
// which must scrub the abandoned inset rect the same way.
TEST_F(CameraView, inset_to_docked_flip_scrubs_the_inset_rect)
{
    game_->ready_for_battle(3);
    arm_seats();
    game_->world().create_new_grid();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id, og::sim::kCameraStyleInset);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_FALSE(game_->camera_docked_);
    const SeatRect r = inset_rect();
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->draw_camera_view_ui();
        ASSERT_TRUE(pixel_matches_index(
            game_, r.xloc - 1, r.yloc - 1, GREY));
    }

    // auto at 3 seats docks: the restyle relayout must scrub the old inset.
    game_->world().mode.cameras[0].style = og::sim::kCameraStyleAuto;
    game_->sync_camera_views();
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_TRUE(game_->camera_docked_);
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        for (int y = r.yloc - 1; y <= r.yloc + r.yview; y += 2)
            for (int x = r.xloc - 1; x <= r.xloc + r.xview; x += 2)
                ASSERT_TRUE(pixel_is_black(game_, x, y))
                    << "stale inset pixel at " << x << "," << y
                    << " after the dock flip";
    }
}

// ---------------------------------------------------------------------------
// One-seat 0.25 zoom (maintainer ruling): the second-minimap pane keeps its
// exact radar-matched rect but shows a kCameraMinimapZoomDenominator-times
// wider and taller world window around the target, integer-downsampled onto
// the pane. The docked quadrant and the 2/4-seat centered inset stay 1:1.
// ---------------------------------------------------------------------------

// The RGB content of the camera pane rect, row-major, read under the
// production GameplayUI canvas scope.
std::vector<Uint32> capture_rect_pixels(screen* scr, const SeatRect& r)
{
    std::vector<Uint32> pixels;
    pixels.reserve(static_cast<std::size_t>(r.xview * r.yview));
    ScopedGameplayUiCanvas gameplay_ui(*scr);
    for (int y = r.yloc; y < r.yloc + r.yview; ++y)
        for (int x = r.xloc; x < r.xloc + r.xview; ++x)
        {
            Uint8 pr = 0, pg = 0, pb = 0;
            scr->get_pixel(x, y, &pr, &pg, &pb);
            pixels.push_back((static_cast<Uint32>(pr) << 16) |
                             (static_cast<Uint32>(pg) << 8) |
                             static_cast<Uint32>(pb));
        }
    return pixels;
}

// Zoom evidence: a witness entity fully OUTSIDE the 1:1 world window but
// inside the 4x window appears in the pane at 0.25 zoom — and its pixels land
// in the pane band the 4:1 mapping predicts. Forcing the draw back to 1:1
// makes the witness disappear (diffs == 0): that is this test's red lever.
// The witness sits at the MIDPOINT of the band the zoom adds below the 1:1
// window, so it is out of reach of any smaller denominator too — a draw that
// quietly reverted to the old 2x window would not reach it either.
TEST_F(CameraView, one_seat_minimap_shows_the_wider_world_at_quarter_zoom)
{
    game_->ready_for_battle(1);
    arm_seats();
    game_->world().create_new_grid();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_FALSE(game_->camera_docked_);
    const SeatRect r = inset_rect();
    ASSERT_EQ(r, capture_rect(*game_->camera_view_))
        << "the pane rect itself must not change with the zoom";
    EXPECT_TRUE(game_->camera_minimap_zoom_)
        << "the one-seat second minimap must resolve to the 0.25 zoom";

    walker* const target =
        game_->world().find_by_id(static_cast<std::uint32_t>(target_id));
    ASSERT_NE(nullptr, target);
    // The window edges, from the same centering rule the viewscreen applies
    // (topy = y - (yview - sizey)/2) at 1x and at the zoomed window height.
    const Sint32 zoomed_h = r.yview * kCameraMinimapZoomDenominator;
    const Sint32 topy1 =
        target->ypos() - (r.yview - target->sizey()) / 2;
    const Sint32 bottom1 = topy1 + r.yview;
    const Sint32 topy2 = target->ypos() - (zoomed_h - target->sizey()) / 2;
    const Sint32 bottom2 = topy2 + zoomed_h;
    // The witness: the MIDPOINT of the world band the zoom adds below the
    // 1:1 window — well clear of the 1:1 bottom edge, and past where a
    // half-scale (or any shallower) window would still end.
    const Sint32 witness_y = bottom1 + (bottom2 - bottom1) / 2;
    ASSERT_GT(witness_y, bottom1 + 8)
        << "no world band between the 1x and 4x windows: pane too small";
    ASSERT_GT(bottom2, witness_y + target->sizey())
        << "the witness must fit inside the zoomed window";
    const std::int32_t witness_id = spawn_target(target->xpos(), witness_y);
    ASSERT_NE(0, witness_id);

    const auto seam_draw = [&]() {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->clearbuffer(r.xloc - 1, r.yloc - 1, r.xview + 2, r.yview + 2);
        game_->draw_camera_view_ui();
    };

    seam_draw();
    const std::vector<Uint32> with_witness = capture_rect_pixels(game_, r);
    walker* const witness =
        game_->world().find_by_id(static_cast<std::uint32_t>(witness_id));
    ASSERT_NE(nullptr, witness);
    witness->set_dead(1);
    seam_draw();
    const std::vector<Uint32> without_witness = capture_rect_pixels(game_, r);
    witness->set_dead(0);
    ASSERT_EQ(with_witness.size(), without_witness.size());

    // Where the 4:1 mapping puts the witness: pane y = (world_y - topy2) / 4,
    // so everything at or below witness_y lands in the lower part of the
    // pane (with slack for the interpolation rounding).
    const int expected_min_pane_y =
        static_cast<int>((witness_y - topy2) /
                         kCameraMinimapZoomDenominator) - 2;
    int diffs = 0;
    int stray = 0;
    for (int y = 0; y < r.yview; ++y)
        for (int x = 0; x < r.xview; ++x)
        {
            const std::size_t i =
                static_cast<std::size_t>(y * r.xview + x);
            if (with_witness[i] == without_witness[i])
                continue;
            ++diffs;
            if (y < expected_min_pane_y)
                ++stray;
        }
    EXPECT_GT(diffs, 0)
        << "the witness below the 1:1 window never appeared in the pane: "
        << "the one-seat minimap is not showing the 4x world window";
    EXPECT_EQ(0, stray)
        << stray << " witness pixels above pane row " << expected_min_pane_y
        << ": the 4:1 world-to-pane mapping is off";
    // The seat and the pane geometry never moved for the zoomed draw.
    EXPECT_EQ(r, capture_rect(*game_->camera_view_));
}

// The 4:1 sampling pin: every pane pixel equals the corresponding pixel of
// the 4x world-window render — dst(i,j) = src(i*4, j*4) — including the
// window-edge corners. The comparison source is the SAME viewscreen draw the
// production path renders into the off-screen layer (the widened window at
// port origin), reproduced on the visible canvas where the test can read it.
TEST_F(CameraView, one_seat_minimap_maps_world_pixels_four_to_one)
{
    game_->ready_for_battle(1);
    arm_seats();
    game_->world().create_new_grid();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    ASSERT_FALSE(game_->camera_docked_);
    const SeatRect r = inset_rect();
    ASSERT_EQ(r, capture_rect(*game_->camera_view_));

    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->draw_camera_view_ui();
    }
    const std::vector<Uint32> pane = capture_rect_pixels(game_, r);

    const short zw =
        static_cast<short>(r.xview * kCameraMinimapZoomDenominator);
    const short zh =
        static_cast<short>(r.yview * kCameraMinimapZoomDenominator);
    const SeatRect big_rect{0, 0, zw, zh};
    std::vector<Uint32> big;
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->clearbuffer(0, 0, zw, zh);
        game_->camera_view_->resize(static_cast<short>(0),
                                    static_cast<short>(0), zw, zh);
        ASSERT_TRUE(game_->camera_view_->redraw(&game_->level_runtime_data(),
                                                /*draw_radar=*/false));
        big = capture_rect_pixels(game_, big_rect);
    }
    // Restore the pane geometry through the one placement rule.
    game_->relayout_camera_view();
    ASSERT_EQ(r, capture_rect(*game_->camera_view_));

    const auto expect_sample = [&](int x, int y) {
        const std::size_t pane_i = static_cast<std::size_t>(y * r.xview + x);
        const std::size_t big_i = static_cast<std::size_t>(
            (y * kCameraMinimapZoomDenominator) * zw +
            (x * kCameraMinimapZoomDenominator));
        return pane[pane_i] == big[big_i];
    };
    // The four window-edge corners of the mapping, pinned by name.
    EXPECT_TRUE(expect_sample(0, 0))
        << "pane (0,0) must sample the 4x window's top-left pixel";
    EXPECT_TRUE(expect_sample(r.xview - 1, 0))
        << "pane right edge must sample layer column 4*(w-1)";
    EXPECT_TRUE(expect_sample(0, r.yview - 1))
        << "pane bottom edge must sample layer row 4*(h-1)";
    EXPECT_TRUE(expect_sample(r.xview - 1, r.yview - 1))
        << "pane bottom-right corner must sample layer (4w-4, 4h-4)";
    // And the whole lattice: nearest integer sampling, no filtering.
    int mismatches = 0;
    for (int y = 0; y < r.yview; y += 3)
        for (int x = 0; x < r.xview; x += 3)
            if (!expect_sample(x, y))
                ++mismatches;
    EXPECT_EQ(0, mismatches)
        << mismatches << " pane pixels off the dst(i,j) = src(4i,4j) mapping";
}

// The allocation-fallback yield for the zoom layer: a failed off-screen
// allocation must fall back to the 1:1 pane draw — the pane stays live and
// framed, merely zoomed in for the frame (the same degrade-don't-corrupt
// shape as the R2 overlay yield).
TEST_F(CameraView, one_seat_minimap_allocation_failure_falls_back_to_one_to_one)
{
    game_->ready_for_battle(1);
    arm_seats();
    game_->world().create_new_grid();
    const std::int32_t target_id = spawn_target();
    ASSERT_NE(0, target_id);
    declare_camera(target_id);
    ASSERT_TRUE(game_->redraw());
    ASSERT_NE(nullptr, game_->camera_view_.get());
    const SeatRect r = inset_rect();

    game_->fail_next_camera_scale_allocation_for_testing();
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        game_->clearbuffer(r.xloc - 1, r.yloc - 1, r.xview + 2, r.yview + 2);
        game_->draw_camera_view_ui();
    }
    const std::vector<Uint32> fallback = capture_rect_pixels(game_, r);

    // The fallback content IS the plain 1:1 pane draw: reproduce it directly
    // and expect pixel identity.
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        ASSERT_TRUE(game_->camera_view_->redraw(&game_->level_runtime_data(),
                                                /*draw_radar=*/false));
    }
    const std::vector<Uint32> direct = capture_rect_pixels(game_, r);
    ASSERT_EQ(fallback.size(), direct.size());
    int diffs = 0;
    for (std::size_t i = 0; i < fallback.size(); ++i)
        if (fallback[i] != direct[i])
            ++diffs;
    EXPECT_EQ(0, diffs)
        << diffs << " pixels differ from the plain 1:1 draw: the allocation "
        << "fallback did not yield to the unscaled pane";
    // The border survived the fallback frame.
    {
        ScopedGameplayUiCanvas gameplay_ui(*game_);
        EXPECT_TRUE(pixel_matches_index(game_, r.xloc - 1, r.yloc - 1, GREY));
        EXPECT_TRUE(pixel_matches_index(game_, r.xloc + r.xview,
                                        r.yloc + r.yview, GREY));
    }
}

} // namespace
