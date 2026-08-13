// §7.1 per-view ZOOM render seam + per-player HUD/zoom persistence at the
// viewscreen boundary (docs/pause-menu-design.md):
//   - the interface budget constant mirrors the platform compositor budget,
//   - a zoomed frame routes every floor pass through the floor layer (padded
//     window allocated, trace emitted, no fallback),
//   - a declined layer renders unzoomed that frame (never wrong output),
//   - a budget overflow unzooms the WHOLE frame (resolve_frame_zoom),
//   - HUD projection stays consistent mid-pass (settled camera, the pad
//     recipe's widened window feeds project_world_point_to_gameplay_ui),
//   - apply_hud_settings_from_cfg: cfg overlays prefs; absent keys run the
//     one-shot keyprefs seed (prefs -> cfg) and never stomp prefs.

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gparser.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <format>
#include <string>

extern cfg_store cfg;

namespace
{

screen* scr()
{
    return og::runtime::current_session->myscreen_;
}

viewscreen* view0()
{
    return scr()->viewob[0].get();
}

void prepare_world()
{
    GameWorld& world = scr()->world();
    world.create_new_grid();
    world.delete_objects();
    world.mysmoother.set_target(world.grid);
}

bool do_redraw(viewscreen* vs)
{
    return vs->redraw(&scr()->level_runtime_data(), false);
}

// Pin the classic 320x200 world canvas (the RenderEffects fixture shape) and
// restore the view scene afterward.
class PerViewZoom : public testing::Test
{
protected:
    void SetUp() override
    {
        scr()->set_world_canvas_pinned_classic(true);
        scr()->relayout_views();
    }

    void TearDown() override
    {
        viewscreen* const vs = view0();
        vs->control = nullptr;
        vs->view_zoom_step_ = 0;
        scr()->world().delete_objects();
        scr()->world().set_floor_count(1);
        scr()->set_active_canvas(CanvasTarget::UI);
        scr()->set_world_canvas_pinned_classic(false);
        scr()->relayout_views();
    }
};

// Save/restore one player's §7.1 cfg keys + the live view prefs.
struct HudCfgGuard
{
    int player;
    std::array<std::string, 6> saved;
    signed char prefs[4]{};
    Sint32 zoom = 0;
    static constexpr const char* kSuffixes[6] = {
        "hud_radar", "hud_life", "hud_foes", "hud_score", "view_zoom",
        "hud_migrated"};

    explicit HudCfgGuard(int player_index) : player(player_index)
    {
        for (int k = 0; k < 6; ++k)
            saved[static_cast<std::size_t>(k)] = cfg.get_setting(
                "controls",
                std::format("player{}_{}", player + 1, kSuffixes[k]));
        viewscreen* const vs = view0();
        prefs[0] = vs->prefs[PREF_RADAR];
        prefs[1] = vs->prefs[PREF_LIFE];
        prefs[2] = vs->prefs[PREF_FOES];
        prefs[3] = vs->prefs[PREF_SCORE];
        zoom = vs->view_zoom_step_;
    }
    ~HudCfgGuard()
    {
        for (int k = 0; k < 6; ++k)
            cfg.apply_setting(
                "controls",
                std::format("player{}_{}", player + 1, kSuffixes[k]),
                saved[static_cast<std::size_t>(k)]);
        viewscreen* const vs = view0();
        vs->prefs[PREF_RADAR] = prefs[0];
        vs->prefs[PREF_LIFE] = prefs[1];
        vs->prefs[PREF_FOES] = prefs[2];
        vs->prefs[PREF_SCORE] = prefs[3];
        vs->view_zoom_step_ = zoom;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Budget pins.

TEST(PerViewZoomBudget, interface_budget_mirrors_platform_compositor)
{
    // og_interface cannot see the platform header, so view.h mirrors the
    // value; this pin keeps the two from drifting apart.
    EXPECT_EQ(og::kPerViewZoomLayerPixelBudget,
              sdl_video::kFloorLayerSourcePixelBudget);
    // input_state.cpp's SDL-free zoom clamp bound (5) is the top step.
    EXPECT_EQ(5, viewscreen::kViewZoomStepCount - 1);
}

// ---------------------------------------------------------------------------
// Render seam.

TEST_F(PerViewZoom, zoom_frame_routes_through_the_floor_layer)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    // Settle the camera with one unzoomed redraw first (the multifloor test
    // trap), and pin that GAME emits no zoom trace.
    vs->view_zoom_step_ = 0;
    ASSERT_TRUE(do_redraw(vs));
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    EXPECT_FALSE(trace_contains("render", "view_zoom"))
        << "ZOOM: GAME must execute zero zoom code";

    const int fallbacks_before = scr()->floor_layer_fallback_count_for_testing();
    vs->view_zoom_step_ = 5;  // 0.5x: the padded window doubles per axis
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    EXPECT_TRUE(trace_contains("render", "view_zoom step=5"));
    EXPECT_FALSE(trace_contains("render", "view_zoom_budget_fallback"));
    EXPECT_EQ(fallbacks_before,
              scr()->floor_layer_fallback_count_for_testing())
        << "the 0.5x window must fit the compositor budget on 320x200";
    // The layer grew to hold the padded camera-floor window (never shrinks,
    // so >= is the monotonic-safe assertion).
    const std::int64_t expected_w = vs->xloc + 2 * vs->xview;
    const std::int64_t expected_h = vs->yloc + 2 * vs->yview;
    EXPECT_GE(scr()->floor_layer_source_pixels_for_testing(),
              expected_w * expected_h);
    EXPECT_FALSE(scr()->floor_layer_redirect_active_for_testing())
        << "every floor_layer_begin must be balanced by floor_layer_end";
}

TEST_F(PerViewZoom, declined_layer_renders_unzoomed_that_frame)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    vs->view_zoom_step_ = 3;
    ASSERT_TRUE(do_redraw(vs));  // settle

    const int fallbacks_before = scr()->floor_layer_fallback_count_for_testing();
    scr()->fail_next_floor_layer_allocation_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs)) << "the frame must render (unzoomed) anyway";
    EXPECT_EQ(fallbacks_before + 1,
              scr()->floor_layer_fallback_count_for_testing());
    EXPECT_FALSE(scr()->floor_layer_redirect_active_for_testing());

    // The next frame recovers the zoomed path.
    ASSERT_TRUE(do_redraw(vs));
    EXPECT_EQ(fallbacks_before + 1,
              scr()->floor_layer_fallback_count_for_testing());
}

TEST_F(PerViewZoom, budget_overflow_unzooms_the_whole_frame)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();

    vs->view_zoom_step_ = 5;
    // Default pane: the zoom resolves.
    trace_clear();
    EXPECT_FLOAT_EQ(0.5f,
                    vs->resolve_frame_zoom(scr()->world(), false, 0));

    // A pane whose padded window blows the compositor budget: the whole
    // frame unzooms (1.0), with the budget-fallback trace.
    const Sint32 old_xview = vs->xview, old_yview = vs->yview;
    vs->xview = 1600;
    vs->yview = 1000;  // 0.5x: 3200x2000 = 6.4M > 4.096M
    trace_clear();
    EXPECT_FLOAT_EQ(1.0f,
                    vs->resolve_frame_zoom(scr()->world(), false, 0));
    EXPECT_TRUE(trace_contains("render", "view_zoom_budget_fallback"));
    vs->xview = old_xview;
    vs->yview = old_yview;
}

TEST_F(PerViewZoom, hud_projection_stays_consistent_mid_pass)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    vs->view_zoom_step_ = 0;
    ASSERT_TRUE(do_redraw(vs));  // settle the camera BEFORE world_to_screen

    // Classic (unzoomed) projections of the pane-centre world point and an
    // off-centre point a quarter pane to the right.
    const float centre_x = static_cast<float>(vs->xloc) +
        static_cast<float>(vs->xview) / 2.0f;
    const float centre_y = static_cast<float>(vs->yloc) +
        static_cast<float>(vs->yview) / 2.0f;
    const float dx = static_cast<float>(vs->xview) / 4.0f;
    const auto [p0x, p0y] =
        vs->project_world_point_to_gameplay_ui(centre_x, centre_y);
    const auto [p2x, p2y] =
        vs->project_world_point_to_gameplay_ui(centre_x + dx, centre_y);

    // Emulate the camera-floor zoom pass exactly as redraw() does: widen the
    // world window by the 0.5x pad. Mid-pass callers (damage numbers, mini
    // HP bars) compute their inputs from the WIDENED topx/xview, and the
    // existing projection formula must keep them consistent.
    const float z = 0.5f;
    const Sint32 pad_x = static_cast<Sint32>(
        std::ceil(static_cast<float>(vs->xview) * (1.0f / z - 1.0f) * 0.5f));
    const Sint32 pad_y = static_cast<Sint32>(
        std::ceil(static_cast<float>(vs->yview) * (1.0f / z - 1.0f) * 0.5f));
    const Sint32 old_topx = vs->topx, old_topy = vs->topy;
    const Sint32 old_xview = vs->xview, old_yview = vs->yview;
    const Sint32 old_endx = vs->endx, old_endy = vs->endy;
    vs->topx -= pad_x;
    vs->topy -= pad_y;
    vs->xview += 2 * pad_x;
    vs->yview += 2 * pad_y;
    vs->endx += 2 * pad_x;
    vs->endy += 2 * pad_y;

    // Mid-pass, the same WORLD points enter as (world - widened_top + loc):
    // the previous classic coordinates shifted right/down by the pad.
    const auto [p1x, p1y] = vs->project_world_point_to_gameplay_ui(
        centre_x + static_cast<float>(pad_x),
        centre_y + static_cast<float>(pad_y));
    const auto [p3x, p3y] = vs->project_world_point_to_gameplay_ui(
        centre_x + static_cast<float>(pad_x) + dx,
        centre_y + static_cast<float>(pad_y));

    vs->topx = old_topx;
    vs->topy = old_topy;
    vs->xview = old_xview;
    vs->yview = old_yview;
    vs->endx = old_endx;
    vs->endy = old_endy;

    // Centre invariant: the pane-centre world point lands where it always
    // landed. Off-centre points move toward the centre by the zoom factor.
    EXPECT_LE(std::abs(p1x - p0x), 1) << "pane centre must not move";
    EXPECT_LE(std::abs(p1y - p0y), 1);
    const float expected_offset = z * static_cast<float>(p2x - p0x);
    EXPECT_LE(std::abs(static_cast<float>(p3x - p1x) - expected_offset), 2.0f)
        << "off-centre HUD anchors must scale with the zoom";
    EXPECT_LE(std::abs(p3y - p1y), 1);
}

// ---------------------------------------------------------------------------
// §7.1 persistence at the viewscreen boundary.

TEST(PlayerHudCfg, viewscreen_applies_cfg_and_seeds_from_keyprefs_once)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    HudCfgGuard guard(0);

    // Marker absent => the one-shot seed: the keyprefs-loaded prefs are
    // written INTO cfg (prefs untouched), and the marker is stamped.
    vs->prefs[PREF_RADAR] = PREF_RADAR_OFF;
    vs->prefs[PREF_LIFE] = PREF_LIFE_TEXT;  // legacy, displays as ON
    vs->prefs[PREF_FOES] = PREF_FOES_ON;
    vs->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    cfg.apply_setting("controls", "player1_hud_migrated", "");
    vs->apply_hud_settings_from_cfg();
    EXPECT_EQ("0", cfg.get_setting("controls", "player1_hud_radar"));
    EXPECT_EQ("1", cfg.get_setting("controls", "player1_hud_life"))
        << "legacy TEXT seeds as ON";
    EXPECT_EQ("1", cfg.get_setting("controls", "player1_hud_foes"));
    EXPECT_EQ("0", cfg.get_setting("controls", "player1_hud_score"));
    EXPECT_EQ("1", cfg.get_setting("controls", "player1_hud_migrated"));
    EXPECT_EQ(PREF_RADAR_OFF, vs->prefs[PREF_RADAR]) << "seed never stomps";
    EXPECT_EQ(PREF_LIFE_TEXT, vs->prefs[PREF_LIFE]);

    // Marker present => cfg overlays prefs (the boot path).
    cfg.apply_setting("controls", "player1_hud_radar", "1");
    cfg.apply_setting("controls", "player1_hud_life", "0");
    cfg.apply_setting("controls", "player1_hud_foes", "0");
    cfg.apply_setting("controls", "player1_hud_score", "1");
    cfg.apply_setting("controls", "player1_view_zoom", "4");
    vs->apply_hud_settings_from_cfg();
    EXPECT_EQ(PREF_RADAR_ON, vs->prefs[PREF_RADAR]);
    EXPECT_EQ(PREF_LIFE_OFF, vs->prefs[PREF_LIFE]);
    EXPECT_EQ(PREF_FOES_OFF, vs->prefs[PREF_FOES]);
    EXPECT_EQ(PREF_SCORE_ON, vs->prefs[PREF_SCORE]);
    EXPECT_EQ(4, static_cast<int>(vs->view_zoom_step_));

    // life_on=1 keeps a legacy pref value (it displays as ON and normalizes
    // on the first toggle) but revives an OFF pref to BOTH.
    cfg.apply_setting("controls", "player1_hud_life", "1");
    vs->prefs[PREF_LIFE] = PREF_LIFE_BARS;
    vs->apply_hud_settings_from_cfg();
    EXPECT_EQ(PREF_LIFE_BARS, vs->prefs[PREF_LIFE]);
    vs->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    vs->apply_hud_settings_from_cfg();
    EXPECT_EQ(PREF_LIFE_BOTH, vs->prefs[PREF_LIFE]);

    // An out-of-range persisted zoom clamps into the cycle.
    cfg.apply_setting("controls", "player1_view_zoom", "9");
    vs->apply_hud_settings_from_cfg();
    EXPECT_EQ(5, static_cast<int>(vs->view_zoom_step_));
}
