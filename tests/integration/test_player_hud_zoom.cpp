// §7.1 per-view ZOOM on the single-resample presentation pipeline
// (docs/pause-menu-design.md §7.1) + per-player HUD/zoom persistence at the
// viewscreen boundary.
//
// The invariant under test: GAMEPLAY PIXELS ARE RESAMPLED EXACTLY ONCE, BY
// THE PRESENTATION PATH. A per-view-zoomed frame must be as crisp as the
// equivalent globally-zoomed frame — the original floor-layer implementation
// bilinear-resampled every zoomed frame a second time (the reported smudge)
// and these tests fail against it.

#include <openglad/core/pixdefs.h>
#include <openglad/core/scale_mode.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/view_layout.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gparser.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

extern cfg_store cfg;
short new_score_panel(screen* s, short do_it);

namespace
{

screen* scr()
{
    return og::runtime::current_session->myscreen_;
}

viewscreen* view_at(int i)
{
    return scr()->viewob[static_cast<std::size_t>(i)].get();
}

viewscreen* view0()
{
    return view_at(0);
}

void prepare_world()
{
    GameWorld& world = scr()->world();
    world.create_new_grid();
    world.delete_objects();
    world.mysmoother.set_target(world.grid);
}

// A high-contrast checker + wall scene so resample blur is visible on edges
// (and blank-canvas runs cannot pass any color assertion vacuously).
void paint_checker_world()
{
    GameWorld& world = scr()->world();
    for (int y = 0; y < world.grid.h; ++y)
        for (int x = 0; x < world.grid.w; ++x)
        {
            unsigned char t = ((x / 2 + y / 2) % 2 == 0)
                ? PIX_GRASS1 : PIX_PAVEMENT1;
            if (x >= 10 && x <= 12 && y >= 4 && y <= 12)
                t = PIX_WALL2;
            world.grid.data[static_cast<std::size_t>(
                y * world.grid.w + x)] = t;
        }
    world.mysmoother.set_target(world.grid);
}

bool do_redraw(viewscreen* vs)
{
    // Gameplay renders on the World target (the game loop selects it every
    // frame); on a split canvas a UI-target redraw would paint the wrong
    // surface entirely.
    scr()->set_active_canvas(CanvasTarget::World);
    return vs->redraw(&scr()->level_runtime_data(), false);
}

SDL_Surface* world_surface()
{
    E_Screen->set_active_canvas(CanvasTarget::World);
    return E_Screen->render;
}

// Distinct opaque colors in a rect of an XRGB8888 surface.
std::unordered_set<Uint32> collect_colors(SDL_Surface* surf, int x, int y,
                                          int w, int h)
{
    std::unordered_set<Uint32> colors;
    SDL_LockSurface(surf);
    for (int py = y; py < y + h && py < surf->h; ++py)
    {
        const Uint32* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(surf->pixels) + py * surf->pitch);
        for (int px = x; px < x + w && px < surf->w; ++px)
            colors.insert(row[px] & 0x00FFFFFFu);
    }
    SDL_UnlockSurface(surf);
    return colors;
}

// Restore cfg graphics/zoom + view zoom steps + view count when a test exits.
struct ZoomPipelineGuard
{
    std::string saved_zoom;
    short saved_numviews;

    ZoomPipelineGuard()
        : saved_zoom(cfg.get_setting("graphics", "zoom")),
          saved_numviews(scr()->numviews)
    {
    }

    ~ZoomPipelineGuard()
    {
        cfg.apply_setting("graphics", "zoom",
                          saved_zoom.empty() ? "1.0" : saved_zoom);
        scr()->numviews = saved_numviews;
        scr()->initialize_views();
        for (int i = 0; i < scr()->numviews; ++i)
            if (viewscreen* const vs = view_at(i))
                vs->view_zoom_step_ = 0;
        scr()->reapply_world_scale();
        scr()->relayout_views();
        scr()->world().delete_objects();
        scr()->world().set_floor_count(1);
        scr()->set_active_canvas(CanvasTarget::UI);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The invariant: per-view zoom rides the SAME single-resample pipeline as the
// global zoom. A single view at per-view 0.5x must produce the EXACT pixels
// the global graphics/zoom 0.5 produces for the same world: identical canvas
// dimensions, identical bytes. (The retired floor-layer implementation fails
// here: it kept the small canvas and bilinear-squeezed the frame instead.)

TEST(PerViewZoomCrisp, single_view_matches_global_zoom_exactly)
{
    ASSERT_TRUE(E_Screen);
    ZoomPipelineGuard guard;

    scr()->numviews = 1;
    scr()->initialize_views();
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    vs->view_zoom_step_ = 0;

    prepare_world();
    paint_checker_world();
    // drawcycle (the sprite animation clock) advances on every walker_draw,
    // so each capture must happen at the same animation AGE: rebuild the
    // walker and render the same number of frames per pipeline.
    const auto render_fresh_scene = [&](viewscreen* view) {
        scr()->world().delete_objects();
        walker* const w =
            scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, w);
        w->setxy(160, 120);
        view->control = w;
        ASSERT_TRUE(do_redraw(view));
        ASSERT_TRUE(do_redraw(view));
    };

    // Reference: the proven global-zoom pipeline at 0.5.
    cfg.apply_setting("graphics", "zoom", "0.5");
    scr()->reapply_world_scale();
    scr()->relayout_views();
    vs = view0();
    render_fresh_scene(vs);
    SDL_Surface* const ref = SDL_DuplicateSurface(world_surface());
    ASSERT_NE(nullptr, ref);
    const auto ref_colors =
        collect_colors(ref, 0, 0, ref->w, ref->h);
    ASSERT_GT(ref_colors.size(), 5u)
        << "the reference render must contain real scenery (a blank canvas "
           "would satisfy every comparison vacuously)";

    // Same world, same view, per-view 0.5x with global zoom back at 1.0.
    cfg.apply_setting("graphics", "zoom", "1.0");
    scr()->reapply_world_scale();
    scr()->relayout_views();
    vs = view0();
    vs->view_zoom_step_ = 5; // 0.5x
    scr()->relayout_views();
    render_fresh_scene(vs);
    SDL_Surface* const got = world_surface();

    EXPECT_EQ(ref->w, got->w)
        << "per-view zoom must derive the SAME world canvas dims as the "
           "equivalent global zoom (no smaller canvas + extra resample)";
    EXPECT_EQ(ref->h, got->h);
    if (ref->w == got->w && ref->h == got->h)
    {
        SDL_LockSurface(ref);
        SDL_LockSurface(got);
        bool identical = true;
        for (int py = 0; py < ref->h && identical; ++py)
        {
            const Uint8* const a =
                static_cast<const Uint8*>(ref->pixels) + py * ref->pitch;
            const Uint8* const b =
                static_cast<const Uint8*>(got->pixels) + py * got->pitch;
            identical = std::memcmp(a, b, static_cast<std::size_t>(ref->w) * 4)
                == 0;
        }
        SDL_UnlockSurface(got);
        SDL_UnlockSurface(ref);
        EXPECT_TRUE(identical)
            << "per-view 0.5x and global 0.5 must render identical bytes";

        const auto got_colors =
            collect_colors(got, 0, 0, got->w, got->h);
        EXPECT_EQ(ref_colors.size(), got_colors.size())
            << "distinct-color parity: a resample stage would blend new "
               "intermediate colors into the zoomed frame";
    }
    SDL_DestroySurface(ref);
}

// Split panes: a per-view zoom on ONE pane must not smudge that pane. The
// world canvas pixels of the zoomed pane are rendered 1:1, so they can only
// contain colors the unzoomed render also produces — a bilinear resample
// stage invents intermediate blend colors and fails this subset check.
TEST(PerViewZoomCrisp, split_pane_no_blend_colors)
{
    ASSERT_TRUE(E_Screen);
    ZoomPipelineGuard guard;

    scr()->numviews = 2;
    scr()->initialize_views();
    prepare_world();
    paint_checker_world();
    walker* const w0 = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const w1 = scr()->world().add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, w0);
    ASSERT_NE(nullptr, w1);
    w0->setxy(160, 120);
    w1->setxy(300, 150);
    view_at(0)->control = w0;
    view_at(1)->control = w1;
    view_at(0)->view_zoom_step_ = 0;
    view_at(1)->view_zoom_step_ = 0;
    scr()->relayout_views();
    ASSERT_TRUE(do_redraw(view_at(0)));
    ASSERT_TRUE(do_redraw(view_at(1)));
    SDL_Surface* surf = world_surface();
    auto ref_colors = collect_colors(surf, 0, 0, surf->w, surf->h);
    ASSERT_GT(ref_colors.size(), 5u)
        << "the unzoomed split render must contain real scenery";
    ref_colors.insert(0x000000u); // cleared background is legitimate

    view_at(1)->view_zoom_step_ = 2; // 0.8x on the right pane only
    scr()->relayout_views();
    ASSERT_TRUE(do_redraw(view_at(0)));
    ASSERT_TRUE(do_redraw(view_at(1)));
    surf = world_surface();
    viewscreen* const zoomed = view_at(1);
    const auto pane_colors = collect_colors(
        surf, zoomed->xloc, zoomed->yloc, zoomed->xview, zoomed->yview);

    int invented = 0;
    for (const Uint32 c : pane_colors)
        if (ref_colors.find(c) == ref_colors.end())
            ++invented;
    EXPECT_EQ(0, invented)
        << "the zoomed pane introduced " << invented
        << " colors absent from the crisp render: gameplay pixels were "
           "resampled before presentation";
}

// ---------------------------------------------------------------------------
// Geometry: windows, slots and the presentation partition.

TEST(PerViewZoomGeometry, zoom_off_windows_fill_slots_and_partition_is_empty)
{
    ASSERT_TRUE(E_Screen);
    ZoomPipelineGuard guard;

    cfg.apply_setting("graphics", "zoom", "1.0");
    scr()->reapply_world_scale();
    const int base_w = scr()->world_canvas_w();
    const int base_h = scr()->world_canvas_h();

    scr()->numviews = 2;
    scr()->initialize_views();
    for (int i = 0; i < 2; ++i)
        view_at(i)->view_zoom_step_ = 0;
    scr()->relayout_views();

    EXPECT_EQ(base_w, scr()->world_canvas_w())
        << "all views at GAME must not change the canvas";
    EXPECT_EQ(base_h, scr()->world_canvas_h());
    EXPECT_TRUE(E_Screen->world_present_slices().empty())
        << "all views at GAME must present through the single-blit path";
    for (int i = 0; i < 2; ++i)
    {
        const viewscreen* const v = view_at(i);
        EXPECT_EQ(v->slot_x_, v->xloc) << "view " << i;
        EXPECT_EQ(v->slot_y_, v->yloc) << "view " << i;
        EXPECT_EQ(v->slot_w_, v->xview) << "view " << i;
        EXPECT_EQ(v->slot_h_, v->yview) << "view " << i;
        // The slot IS the pre-per-view-zoom layout projection.
        const og::view_layout::ViewLayout baseline =
            og::view_layout::compute_view_layout(
                2, i, v->prefs[PREF_VIEW],
                scr()->gameplay_ui_canvas_w(), scr()->gameplay_ui_canvas_h());
        const og::view_layout::ViewLayout expected =
            og::view_layout::project_view_layout(
                baseline, scr()->gameplay_ui_canvas_w(),
                scr()->gameplay_ui_canvas_h(), base_w, base_h);
        ASSERT_TRUE(expected.applies);
        EXPECT_EQ(expected.x, v->xloc) << "view " << i;
        EXPECT_EQ(expected.y, v->yloc) << "view " << i;
        EXPECT_EQ(expected.w, v->xview) << "view " << i;
        EXPECT_EQ(expected.h, v->yview) << "view " << i;
    }
}

TEST(PerViewZoomGeometry, split_windows_scale_independently)
{
    ASSERT_TRUE(E_Screen);
    ZoomPipelineGuard guard;

    cfg.apply_setting("graphics", "zoom", "1.0");
    scr()->reapply_world_scale();
    const int base_w = scr()->world_canvas_w();
    const int base_h = scr()->world_canvas_h();

    scr()->numviews = 2;
    scr()->initialize_views();
    view_at(0)->view_zoom_step_ = 0; // GAME
    view_at(1)->view_zoom_step_ = 5; // 0.5x
    scr()->relayout_views();

    // Canvas derives from the MINIMUM effective zoom: doubled (the width can
    // round down to its multiple-of-4 grid after scaling).
    EXPECT_LE(std::abs(scr()->world_canvas_w() - 2 * base_w), 4);
    EXPECT_EQ(2 * base_h, scr()->world_canvas_h());

    const viewscreen* const v0 = view_at(0);
    const viewscreen* const v1 = view_at(1);
    // The 0.5x view sits at the minimum: its window fills its slot.
    EXPECT_EQ(v1->slot_x_, v1->xloc);
    EXPECT_EQ(v1->slot_y_, v1->yloc);
    EXPECT_EQ(v1->slot_w_, v1->xview);
    EXPECT_EQ(v1->slot_h_, v1->yview);
    // The GAME view renders HALF its slot (n_min/n = 5/10), anchored at the
    // slot's top-left — the same world coverage it had before the canvas
    // grew, presented back onto the full slot by its slice.
    EXPECT_EQ(v0->slot_x_, v0->xloc);
    EXPECT_EQ(v0->slot_y_, v0->yloc);
    EXPECT_EQ(v0->slot_w_ / 2, v0->xview);
    EXPECT_EQ(v0->slot_h_ / 2, v0->yview);

    const auto& slices = E_Screen->world_present_slices();
    ASSERT_EQ(1u, slices.size())
        << "exactly the window != slot views get presentation slices";
    EXPECT_EQ(v0->xloc, slices[0].src_x);
    EXPECT_EQ(v0->yloc, slices[0].src_y);
    EXPECT_EQ(v0->xview, slices[0].src_w);
    EXPECT_EQ(v0->yview, slices[0].src_h);
    EXPECT_EQ(v0->slot_x_, slices[0].dst_x);
    EXPECT_EQ(v0->slot_y_, slices[0].dst_y);
    EXPECT_EQ(v0->slot_w_, slices[0].dst_w);
    EXPECT_EQ(v0->slot_h_, slices[0].dst_h);

    // Back to all-GAME: canvas, windows and partition all restore.
    view_at(1)->view_zoom_step_ = 0;
    scr()->relayout_views();
    EXPECT_EQ(base_w, scr()->world_canvas_w());
    EXPECT_EQ(base_h, scr()->world_canvas_h());
    EXPECT_TRUE(E_Screen->world_present_slices().empty());
}

// ---------------------------------------------------------------------------
// HUD projection: the stable zoom-1.0 pane is derived from the WINDOW, so
// chrome anchors survive any per-view zoom.

TEST(PerViewZoomHud, projection_maps_window_onto_the_stable_ui_pane)
{
    ASSERT_TRUE(E_Screen);
    ZoomPipelineGuard guard;

    scr()->numviews = 1;
    scr()->initialize_views();
    viewscreen* vs = view0();
    prepare_world();
    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    vs->view_zoom_step_ = 5;
    scr()->relayout_views();
    vs = view0();
    ASSERT_TRUE(do_redraw(vs)); // settle the camera BEFORE projecting

    const og::view_layout::ViewLayout ui =
        og::view_layout::compute_view_layout(
            1, 0, vs->prefs[PREF_VIEW],
            scr()->gameplay_ui_canvas_w(), scr()->gameplay_ui_canvas_h());
    ASSERT_TRUE(ui.applies);

    // Production projects while the GameplayUI overlay target is active
    // (redraw's chrome pass): prepare the overlay frame and select it, or
    // the projection's alias-fallback guard correctly returns identity.
    scr()->begin_gameplay_frame();
    {
        ScopedGameplayUiCanvas gameplay_ui(*scr());
        ASSERT_EQ(scr()->canvas_w(), scr()->gameplay_ui_canvas_w())
            << "the fixed overlay must be active for HUD projection";

        const auto [cx, cy] = vs->project_world_point_to_gameplay_ui(
            static_cast<float>(vs->xloc) +
                static_cast<float>(vs->xview) / 2.0f,
            static_cast<float>(vs->yloc) +
                static_cast<float>(vs->yview) / 2.0f);
        EXPECT_LE(std::abs(cx - (ui.x + ui.w / 2)), 1)
            << "window centre must project to the ui pane centre";
        EXPECT_LE(std::abs(cy - (ui.y + ui.h / 2)), 1);

        const auto [ox, oy] = vs->project_world_point_to_gameplay_ui(
            static_cast<float>(vs->xloc), static_cast<float>(vs->yloc));
        EXPECT_LE(std::abs(ox - ui.x), 1)
            << "window origin must project to the ui pane origin";
        EXPECT_LE(std::abs(oy - ui.y), 1);
    }
    E_Screen->discard_gameplay_ui_frame(); // no stale overlay for later swaps
}

namespace
{

// RAII scripted-mode stamp: TYPE_SCRIPTED + an active ModeState, restored
// even when an assertion aborts the test body (the beacon test mutates
// shared screen state inside og_test_view).
struct ScriptedModeStamp
{
    GameWorld& world;
    char saved_type;

    explicit ScriptedModeStamp(GameWorld& w) : world(w), saved_type(w.type)
    {
        world.type |= GameWorld::TYPE_SCRIPTED;
        world.mode = og::sim::ModeState{};
        world.mode.active = true;
        world.mode.init_attempted = true;
    }
    ~ScriptedModeStamp()
    {
        world.type = saved_type;
        world.mode = og::sim::ModeState{};
        world.tick_count_ = 0;
    }
    ScriptedModeStamp(const ScriptedModeStamp&) = delete;
    ScriptedModeStamp& operator=(const ScriptedModeStamp&) = delete;
};

// RAII overlay-frame discard: survives assertion aborts, so a failing test
// cannot leak a live overlay frame into later canvas swaps.
struct GameplayUiFrameDiscard
{
    GameplayUiFrameDiscard() = default;
    ~GameplayUiFrameDiscard() { E_Screen->discard_gameplay_ui_frame(); }
    GameplayUiFrameDiscard(const GameplayUiFrameDiscard&) = delete;
    GameplayUiFrameDiscard& operator=(const GameplayUiFrameDiscard&) = delete;
};

} // namespace

// Issue #220: draw_mode_beacons mixed the WORLD-pane camera (topx/topy) with
// the UI-pane origin (xloc, already swapped by the caller's layout scope), so
// a zoomed view drew the marker 1/zoom too far from the pane origin and an
// on-screen ball flipped to an edge arrow. The projection must run through a
// GameplayUiProjector captured BEFORE the layout scope opens.
TEST(PerViewZoomHud, beacon_pulse_tracks_ball_at_zoom_step_5)
{
    ASSERT_TRUE(E_Screen);
    ZoomPipelineGuard guard;

    scr()->numviews = 1;
    scr()->initialize_views();
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    GameWorld& world = scr()->world();
    ScriptedModeStamp mode_stamp(world);

    walker* const hero = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, hero);
    hero->setxy(160, 120);
    vs->control = hero;
    vs->prefs[PREF_OVERLAY] = PREF_OVERLAY_OFF;
    vs->prefs[PREF_LIFE] = PREF_LIFE_OFF;
    vs->prefs[PREF_SCORE] = PREF_SCORE_OFF;
    vs->prefs[PREF_FOES] = PREF_FOES_OFF;

    vs->view_zoom_step_ = 5;
    scr()->relayout_views();
    vs = view0();
    ASSERT_TRUE(do_redraw(vs)); // settle the camera BEFORE projecting

    // Deterministic identity-arm probe: the World canvas is still active and
    // at step 5 it is twice the overlay, so the projector must refuse to
    // project (the overlay-alias fallback) and answer bit-identically.
    {
        const GameplayUiProjector fallback(*vs);
        EXPECT_EQ(123, fallback.project(123.0f, 45.0f).first);
        EXPECT_EQ(45, fallback.project(123.0f, 45.0f).second);
        EXPECT_EQ(10, fallback.scale_w(10, 1));
        EXPECT_EQ(10, fallback.scale_h(10, 1));
    }

    // Place the ball ON SCREEN in world terms (3/4 across the visible pane)
    // but past the UI pane under the broken math (0.75 * xview = 1.5 * ui.w).
    walker* const ball = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ball);
    const Sint32 target_cx_world = vs->topx + (vs->xview * 3) / 4;
    const Sint32 target_cy_world = vs->topy + vs->yview / 2;
    ball->setxy(static_cast<short>(target_cx_world - ball->sizex() / 2),
                static_cast<short>(target_cy_world - ball->sizey() / 2));
    world.mode.beacons[0].entity_id =
        static_cast<std::int32_t>(ball->entity_id());
    world.mode.beacons[0].team = 2;
    world.tick_count_ = 0; // phase 0 -> the narrowest pulse

    const og::view_layout::ViewLayout ui =
        og::view_layout::compute_view_layout(
            1, 0, vs->prefs[PREF_VIEW],
            scr()->gameplay_ui_canvas_w(), scr()->gameplay_ui_canvas_h());
    ASSERT_TRUE(ui.applies);
    ASSERT_EQ(2 * ui.w, vs->xview) << "zoom step 5 must double the world pane";

    scr()->begin_gameplay_frame();
    GameplayUiFrameDiscard discard_overlay_on_exit;
    Sint32 exp_cx = 0;
    Sint32 exp_cy = 0;
    Sint32 exp_bottom = 0;
    {
        ScopedGameplayUiCanvas gameplay_ui(*scr());
        ASSERT_EQ(scr()->canvas_w(), scr()->gameplay_ui_canvas_w())
            << "the fixed overlay must be active for HUD projection";
        // Exactly production's anchor inputs: world-canvas screen coordinates
        // (outside any layout scope vs->xloc IS the world pane origin).
        const float wx = static_cast<float>(
            static_cast<Sint32>(ball->xpos()) + ball->sizex() / 2 -
            vs->topx + vs->xloc);
        const float wy = static_cast<float>(
            static_cast<Sint32>(ball->ypos()) + ball->sizey() / 2 -
            vs->topy + vs->yloc);
        const auto [px, py] = vs->project_world_point_to_gameplay_ui(wx, wy);
        exp_cx = px;
        exp_cy = py;
        exp_bottom = vs->project_world_point_to_gameplay_ui(
            wx, wy + static_cast<float>(ball->sizey() / 2)).second;
        scr()->clearbuffer(); // a clean overlay for the pixel probe
    }

    // Setup self-check: the projected center is interior, so the correct
    // outcome is a pulse marker, not an edge arrow.
    ASSERT_GT(exp_cx, ui.x + 4);
    ASSERT_LT(exp_cx, ui.x + ui.w - 5);

    trace_clear();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(scr(), 1)));

    const Sint32 exp_w = 8 * ui.w / vs->xview;
    const Sint32 exp_h = 2 * ui.h / vs->yview;
    const Sint32 exp_x =
        std::clamp(exp_cx - exp_w / 2, ui.x, ui.x + ui.w - exp_w);
    const Sint32 exp_y = std::min(exp_bottom + 1, ui.y + ui.h - exp_h);
    char expected[96];
    std::snprintf(expected, sizeof expected,
                  "beacon_pulse slot=0 w=%d x=%d y=%d",
                  static_cast<int>(exp_w), static_cast<int>(exp_x),
                  static_cast<int>(exp_y));
    EXPECT_TRUE(trace_contains("mode_hud", expected)) << expected;
    EXPECT_FALSE(trace_contains("mode_hud", "beacon_edge"))
        << "an on-screen ball must not flip to an edge arrow (issue #220)";
    {
        ScopedGameplayUiCanvas gameplay_ui(*scr());
        int index = -1;
        scr()->get_pixel(exp_x, exp_y, &index);
        EXPECT_EQ(72, index)
            << "the pulse must actually paint the team-2 ramp at the "
               "projected spot";
    }

    // Genuinely off the world pane: the fix must not kill real edge arrows.
    ball->setxy(static_cast<short>(vs->topx + vs->xview + 200),
                static_cast<short>(target_cy_world - ball->sizey() / 2));
    trace_clear();
    ASSERT_EQ(1, static_cast<int>(new_score_panel(scr(), 1)));
    std::snprintf(expected, sizeof expected,
                  "beacon_edge slot=0 dx=1 dy=0 x=%d y=%d",
                  static_cast<int>(ui.x + ui.w - 5), static_cast<int>(exp_cy));
    EXPECT_TRUE(trace_contains("mode_hud", expected)) << expected;
    EXPECT_FALSE(trace_contains("mode_hud", "beacon_pulse"))
        << "a genuinely off-pane ball must still get an edge arrow";
}

// ---------------------------------------------------------------------------
// Presented composition: captures and modal backdrops apply the partition
// (windows on slots, nearest), so what tests and screenshots read is what the
// player sees. Setting PAUSE_SHOTS_DIR retains the frame as zoom_split.ppm.

namespace
{

bool write_surface_ppm(SDL_Surface* surf, const std::string& path)
{
    FILE* const f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return false;
    std::fprintf(f, "P6\n%d %d\n255\n", surf->w, surf->h);
    SDL_LockSurface(surf);
    std::vector<Uint8> row_rgb(static_cast<std::size_t>(surf->w) * 3);
    const SDL_PixelFormatDetails* const det =
        SDL_GetPixelFormatDetails(surf->format);
    for (int py = 0; py < surf->h; ++py)
    {
        const Uint32* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(surf->pixels) + py * surf->pitch);
        for (int px = 0; px < surf->w; ++px)
        {
            Uint8 r = 0, g = 0, b = 0;
            SDL_GetRGB(row[px], det, nullptr, &r, &g, &b);
            row_rgb[static_cast<std::size_t>(px) * 3 + 0] = r;
            row_rgb[static_cast<std::size_t>(px) * 3 + 1] = g;
            row_rgb[static_cast<std::size_t>(px) * 3 + 2] = b;
        }
        std::fwrite(row_rgb.data(), 1, row_rgb.size(), f);
    }
    SDL_UnlockSurface(surf);
    std::fclose(f);
    return true;
}

} // namespace

TEST(PerViewZoomCapture, composed_capture_presents_windows_on_slots)
{
    ASSERT_TRUE(E_Screen);
    ZoomPipelineGuard guard;

    cfg.apply_setting("graphics", "zoom", "1.0");
    scr()->reapply_world_scale();

    scr()->numviews = 2;
    scr()->initialize_views();
    prepare_world();
    paint_checker_world();
    walker* const w0 = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const w1 = scr()->world().add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, w0);
    ASSERT_NE(nullptr, w1);
    w0->setxy(160, 120);
    w1->setxy(200, 140);
    view_at(0)->control = w0;
    view_at(1)->control = w1;
    view_at(0)->view_zoom_step_ = 0; // left pane: GAME
    view_at(1)->view_zoom_step_ = 3; // right pane: 0.7x
    scr()->relayout_views();
    ASSERT_TRUE(do_redraw(view_at(0)));
    ASSERT_TRUE(do_redraw(view_at(1)));

    SDL_Surface* const world_surf = world_surface();
    SDL_Surface* const composed =
        E_Screen->compose_gameplay_ui_for_capture(world_surf);
    ASSERT_NE(nullptr, composed)
        << "an active partition must compose even without a HUD overlay";
    EXPECT_EQ(world_surf->w, composed->w);
    EXPECT_EQ(world_surf->h, composed->h);

    // The sliced pane (left, window != slot): the composed slot region can
    // only contain colors from the 1:1 window — nearest invents nothing.
    const viewscreen* const v0 = view_at(0);
    auto window_colors = collect_colors(
        world_surf, v0->xloc, v0->yloc, v0->xview, v0->yview);
    ASSERT_GT(window_colors.size(), 5u)
        << "the rendered window must contain real scenery (a blank canvas "
           "would satisfy the subset check vacuously)";
    window_colors.insert(0x000000u);
    const auto slot_colors = collect_colors(
        composed, v0->slot_x_, v0->slot_y_, v0->slot_w_, v0->slot_h_);
    int invented = 0;
    for (const Uint32 c : slot_colors)
        if (window_colors.find(c) == window_colors.end())
            ++invented;
    EXPECT_EQ(0, invented)
        << invented << " blend colors in the composed slot: the capture "
                       "composition resampled twice";

    const char* const shots_dir = std::getenv("PAUSE_SHOTS_DIR");
    if (shots_dir != nullptr && shots_dir[0] != '\0')
    {
        std::error_code ec;
        std::filesystem::create_directories(shots_dir, ec);
        const std::string path = std::string(shots_dir) + "/zoom_split.ppm";
        EXPECT_TRUE(write_surface_ppm(composed, path));
        std::fprintf(stderr, "[zoomshot] wrote %s (%dx%d)\n", path.c_str(),
                     composed->w, composed->h);
    }
    SDL_DestroySurface(composed);
}

// ---------------------------------------------------------------------------
// §7.1 persistence at the viewscreen boundary (mechanism-independent: the
// cfg/prefs carrier contract is unchanged by the render re-architecture).

namespace
{

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
