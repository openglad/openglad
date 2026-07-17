// Integration coverage for the cfg graphics/zoom + graphics/smoothing
// world-canvas machinery:
//  * Screen::set_world_zoom sizing the world surface+texture from the
//    classic-density baseline expanded to the logical window's aspect
//    (lower zoom = baseline / zoom),
//  * the per-canvas present engine in Screen::swap (off = GPU nearest of
//    the variable World canvas; sai/eagle = software 2x into a canvas*2
//    render2; fixed gameplay UI is always composited nearest afterwards),
//  * the gameplay UI canvas staying at the zoom-1.0 aspect and classic pixel
//    density while World and its projected viewscreen layouts change zoom,
//  * window resizes re-deriving the selected zoom canvas and view layout,
//  * aspect-fitted fixed UI presentation/input on non-16:10 windows,
//  * the level editor's classic-canvas pin.
//
// Every test restores the 1.0 classic-density renderer state and UI target so
// the rest of the binary keeps running on the fixture's normal setup.

#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/view_layout.h>
#include <openglad/interface/input.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

// Implemented in src/interface/ui/level_editor.cpp
Sint32 level_editor();

namespace
{

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

// RAII restore of the classic-density default renderer state around a test body.
struct ClassicCanvasRestore
{
    int win_w = 0;
    int win_h = 0;
    float old_window_w;
    float old_window_h;
    ClassicCanvasRestore()
        : old_window_w(og::runtime::current_session->window_w_),
          old_window_h(og::runtime::current_session->window_h_)
    {
        SDL_GetWindowSize(E_Screen->window, &win_w, &win_h);
    }
    ~ClassicCanvasRestore()
    {
        E_Screen->set_world_canvas_pinned_classic(false);
        (void)SDL_SetWindowSize(E_Screen->window, win_w, win_h);
        (void)SDL_SyncWindow(E_Screen->window);
        E_Screen->set_world_zoom(og::kZoomStepsMax,
                                 og::WorldScaleMode::Integer, win_w, win_h);
        E_Screen->set_active_canvas(CanvasTarget::UI);
        og::runtime::current_session->window_w_ = old_window_w;
        og::runtime::current_session->window_h_ = old_window_h;
        update_overscan_setting();
        if (screen* s = test_screen())
            s->relayout_views();
    }
};

void query_texture_dims(SDL_Texture* tex, int* w, int* h)
{
    // SDL3 removed SDL_QueryTexture; SDL_Texture is a public struct with
    // read-only format/w/h fields.
    ASSERT_NE(nullptr, tex);
    *w = tex->w;
    *h = tex->h;
}

int stop_editor_after_render(void*)
{
    og::runtime::ensure_thread_session();
    // The editor paints immediately on entry. Leave enough time for at least
    // one complete map + chrome transaction before ending its private loop.
    SDL_Delay(300);
    og::runtime::current_session->myscreen_->world().end = 1;
    return 0;
}

} // namespace

TEST(CanvasScale, zoom_one_matches_masters_classic_density_default)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 640, 400);
    ASSERT_EQ(og::WorldScaleMode::Legacy, E_Screen->world_scale().mode);
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(ui, E_Screen->render)
        << "a 16:10 window keeps master's shared classic world/UI pair";
    EXPECT_EQ(320, E_Screen->render->w);
    EXPECT_EQ(200, E_Screen->render->h);
    EXPECT_EQ(320, test_screen()->world_canvas_w());
    EXPECT_EQ(200, test_screen()->world_canvas_h());
    E_Screen->set_active_canvas(CanvasTarget::UI);
}

TEST(CanvasScale, zoom_half_doubles_the_classic_density_world_canvas_and_presents)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // A 640x400 window has a 320x200 classic-density baseline. Zoom 0.5
    // doubles that baseline into a real grown canvas.
    const og::WorldCanvasDims want = og::compute_zoom_canvas_dims(640, 400, 5);
    ASSERT_EQ(640, want.w);
    ASSERT_EQ(400, want.h);
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 640, 400);
    ASSERT_EQ(want.w, E_Screen->world_w());
    ASSERT_EQ(want.h, E_Screen->world_h());

    // The world pair split off the UI pair; the UI canvas stays 320x200.
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    EXPECT_EQ(320, ui->w);
    EXPECT_EQ(200, ui->h);
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_NE(ui, E_Screen->render);
    EXPECT_EQ(want.w, E_Screen->render->w);
    EXPECT_EQ(want.h, E_Screen->render->h);
    int tw = 0, th = 0;
    query_texture_dims(E_Screen->render_tex, &tw, &th);
    EXPECT_EQ(want.w, tw);
    EXPECT_EQ(want.h, th);

    // Present-path smoke on the big canvas: plot into the far corner via the
    // canvas-w offset math, read it back, then run a full-frame present.
    screen* s = test_screen();
    ASSERT_EQ(want.w, s->canvas_w());
    s->pointb(s->canvas_w() - 1, s->canvas_h() - 1, 47);
    int idx = 0;
    s->get_pixel(s->canvas_w() - 1, s->canvas_h() - 1, &idx);
    EXPECT_EQ(47, idx);
    s->buffer_to_screen(0, 0, s->canvas_w(), s->canvas_h());
}

TEST(CanvasScale, zoom_steps_keep_nearest_gameplay_ui_at_classic_pixel_density)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    // Gameplay chrome and zoom-1 World share the classic-density baseline.
    // Lower zoom values grow World alone while the fixed HUD stays put.
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 640, 400);
    const int hud_w = s->gameplay_ui_canvas_w();
    const int hud_h = s->gameplay_ui_canvas_h();
    ASSERT_EQ(320, hud_w);
    ASSERT_EQ(200, hud_h);

    E_Screen->set_active_canvas(CanvasTarget::World);
    SDL_Surface* const baseline_world = E_Screen->render;
    E_Screen->begin_gameplay_frame();
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active())
        << "nearest zoom 1.0 needs no separate identity-sized HUD overlay";
    E_Screen->set_active_canvas(CanvasTarget::GameplayUI);
    EXPECT_EQ(baseline_world, E_Screen->render);

    for (const int zoom_steps : {9, 8, 5})
    {
        E_Screen->set_world_zoom(zoom_steps,
                                 og::WorldScaleMode::Integer, 640, 400);
        const og::WorldCanvasDims expected_world =
            og::compute_zoom_canvas_dims(640, 400, zoom_steps);
        EXPECT_EQ(expected_world.w, E_Screen->world_w());
        EXPECT_EQ(expected_world.h, E_Screen->world_h());
        EXPECT_GT(E_Screen->world_w(), hud_w);
        EXPECT_GT(E_Screen->world_h(), hud_h);
        EXPECT_EQ(hud_w, s->gameplay_ui_canvas_w());
        EXPECT_EQ(hud_h, s->gameplay_ui_canvas_h());

        E_Screen->set_active_canvas(CanvasTarget::World);
        SDL_Surface* const world = E_Screen->render;
        E_Screen->begin_gameplay_frame();
        ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
        SDL_Surface* const overlay =
            E_Screen->gameplay_ui_overlay_surface();
        SDL_Texture* const overlay_texture =
            E_Screen->gameplay_ui_overlay_texture();
        ASSERT_NE(nullptr, overlay);
        ASSERT_NE(nullptr, overlay_texture);
        EXPECT_NE(world, overlay);
        EXPECT_EQ(hud_w, overlay->w);
        EXPECT_EQ(hud_h, overlay->h);
        EXPECT_EQ(hud_w, overlay_texture->w);
        EXPECT_EQ(hud_h, overlay_texture->h);

        SDL_ScaleMode overlay_scale = SDL_SCALEMODE_INVALID;
        ASSERT_TRUE(SDL_GetTextureScaleMode(overlay_texture, &overlay_scale));
        EXPECT_EQ(SDL_SCALEMODE_NEAREST, overlay_scale)
            << "fixed gameplay text must never be linearly filtered";

        {
            ScopedGameplayUiCanvas gameplay_ui(*s);
            EXPECT_EQ(CanvasTarget::GameplayUI, s->active_canvas());
            EXPECT_EQ(hud_w, s->canvas_w());
            EXPECT_EQ(hud_h, s->canvas_h());
            EXPECT_EQ(overlay, E_Screen->render);
        }
        EXPECT_EQ(CanvasTarget::World, s->active_canvas());
        EXPECT_EQ(expected_world.w, s->canvas_w());
        EXPECT_EQ(expected_world.h, s->canvas_h());
    }
}

TEST(CanvasScale, fractional_zoom_aspect_fits_hud_and_touch_independently)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* const s = test_screen();
    ASSERT_TRUE(s);
    const float old_overscan =
        og::runtime::current_session->overscan_percentage_;

    // At zoom 0.9 the scaler-safe width rounding makes World 352x222 rather
    // than an exact multiple of the fixed 320x200 HUD aspect. On a 640x400
    // viewport, World therefore fits at x=3..636 while HUD must still fill
    // the complete classic-aspect destination at x=0..639.
    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 400;
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    update_overscan_setting();
    E_Screen->set_world_zoom(9, og::WorldScaleMode::Integer, 640, 400);
    ASSERT_EQ(352, E_Screen->world_w());
    ASSERT_EQ(222, E_Screen->world_h());
    ASSERT_EQ(320, s->gameplay_ui_canvas_w());
    ASSERT_EQ(200, s->gameplay_ui_canvas_h());

    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    const og::CanvasViewport world_dest = active_canvas_viewport();
    const og::CanvasViewport hud_dest = gameplay_ui_canvas_viewport();
    EXPECT_EQ(3, world_dest.x);
    EXPECT_EQ(0, world_dest.y);
    EXPECT_EQ(634, world_dest.w);
    EXPECT_EQ(400, world_dest.h);
    EXPECT_EQ(0, hud_dest.x);
    EXPECT_EQ(0, hud_dest.y);
    EXPECT_EQ(640, hud_dest.w);
    EXPECT_EQ(400, hud_dest.h);
    EXPECT_NE(world_dest.x, hud_dest.x)
        << "HUD presentation must not inherit fractional World rounding";
    EXPECT_EQ(hud_dest.w * s->gameplay_ui_canvas_h(),
              hud_dest.h * s->gameplay_ui_canvas_w())
        << "the independently fitted HUD must retain its native aspect";

    // The left and right strips excluded by World's rounded destination are
    // live HUD pixels and touch targets. Mapping them through World would
    // reject x=1 and produce a negative logical coordinate.
    EXPECT_FALSE(window_point_in_active_canvas(1.0f, 200.0f));
    EXPECT_TRUE(window_point_in_gameplay_ui_canvas(1.0f, 200.0f));
    EXPECT_TRUE(window_point_in_gameplay_ui_canvas(639.0f, 200.0f));
    const auto [left_x, middle_y] =
        window_to_gameplay_ui_canvas(1.0f, 200.0f);
    const auto [right_x, right_y] =
        window_to_gameplay_ui_canvas(639.0f, 200.0f);
    EXPECT_NEAR(0.5f, left_x, 0.001f);
    EXPECT_NEAR(100.0f, middle_y, 0.001f);
    EXPECT_NEAR(319.5f, right_x, 0.001f);
    EXPECT_NEAR(100.0f, right_y, 0.001f);

    // Exercise Screen::swap's overlay branch with opaque edge pixels. Its
    // compositor consumes gameplay_ui_canvas_viewport(), the same geometry
    // asserted above and used by the touch conversion.
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0x00102030u);
    {
        ScopedGameplayUiCanvas gameplay_ui(*s);
        ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, nullptr, 0u));
        const Uint32 edge = SDL_MapSurfaceRGBA(
            E_Screen->render, 230, 30, 50, 255);
        const SDL_Rect left_edge{0, 0, 1, E_Screen->render->h};
        const SDL_Rect right_edge{
            E_Screen->render->w - 1, 0, 1, E_Screen->render->h};
        ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, &left_edge, edge));
        ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, &right_edge, edge));
    }
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
    EXPECT_EQ(CanvasTarget::World, s->last_presented_canvas());

    og::runtime::current_session->overscan_percentage_ = old_overscan;
}

TEST(CanvasScale, buffered_hud_text_draws_in_widescreen_ui_extension)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* const s = test_screen();
    ASSERT_TRUE(s);

    E_Screen->set_world_zoom(9,
                             og::WorldScaleMode::Integer, 640, 360);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->set_active_canvas(CanvasTarget::GameplayUI);
    ASSERT_EQ(356, s->canvas_w());
    ASSERT_EQ(200, s->canvas_h());
    ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, nullptr, 0u));

    ASSERT_EQ(1, s->text_normal.write_char_xy(
                     340, 10, 'A', WHITE, static_cast<short>(1)));
    bool found_glyph_pixel = false;
    for (int y = 10; y < 10 + s->text_normal.letters->h; ++y)
    {
        const auto* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(E_Screen->render->pixels) +
            static_cast<std::size_t>(y) * E_Screen->render->pitch);
        for (int x = 340; x < 356; ++x)
            found_glyph_pixel = found_glyph_pixel || row[x] != 0u;
    }
    EXPECT_TRUE(found_glyph_pixel)
        << "right-anchored HUD text must not clip at the old x=319 edge";
}

TEST(CanvasScale, modal_backdrop_center_crops_widescreen_world_without_squeeze)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    // A 16:9 classic-density world is 356x200. It must crop the aspect-only
    // horizontal extension before being reduced to the fixed 320x200 UI. If
    // the full world were squeezed instead, the red edge bands would be
    // visible on the modal backdrop's first and last columns.
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 1280, 720);
    E_Screen->set_active_canvas(CanvasTarget::World);
    SDL_Surface* const world = E_Screen->render;
    ASSERT_EQ(356, world->w);
    ASSERT_EQ(200, world->h);
    const og::CanvasViewport crop = og::crop_canvas_to_aspect(
        world->w, world->h, kUiCanvasW, kUiCanvasH);
    ASSERT_EQ(18, crop.x);
    ASSERT_EQ(320, crop.w);
    const Uint32 center = SDL_MapSurfaceRGBA(world, 20, 180, 60, 255);
    const Uint32 edge = SDL_MapSurfaceRGBA(world, 220, 30, 40, 255);
    ASSERT_TRUE(SDL_FillSurfaceRect(world, nullptr, center));
    const SDL_Rect left_edge{0, 0, crop.x, world->h};
    const SDL_Rect right_edge{crop.x + crop.w, 0,
                              world->w - crop.x - crop.w, world->h};
    ASSERT_TRUE(SDL_FillSurfaceRect(world, &left_edge, edge));
    ASSERT_TRUE(SDL_FillSurfaceRect(world, &right_edge, edge));

    {
        ScopedUiCanvas ui(*s);
        ASSERT_EQ(320, E_Screen->render->w);
        ASSERT_EQ(200, E_Screen->render->h);
        const auto* first_row =
            reinterpret_cast<const Uint32*>(E_Screen->render->pixels);
        EXPECT_EQ(center & 0x00ffffffu, first_row[0] & 0x00ffffffu);
        EXPECT_EQ(center & 0x00ffffffu, first_row[160] & 0x00ffffffu);
        EXPECT_EQ(center & 0x00ffffffu, first_row[319] & 0x00ffffffu);
        EXPECT_NE(edge & 0x00ffffffu, first_row[0] & 0x00ffffffu);
        EXPECT_NE(edge & 0x00ffffffu, first_row[319] & 0x00ffffffu);
    }
}

TEST(CanvasScale, scoped_ui_canvas_seeds_from_world_and_restores_routing)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai, 320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    constexpr Uint32 kWorldPixel = 0x00123456u;
    SDL_FillSurfaceRect(E_Screen->render, nullptr, kWorldPixel);
	E_Screen->begin_gameplay_frame();
	ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
	Uint32 overlay_pixel = 0;
	{
		ScopedGameplayUiCanvas gameplay_ui(*s);
		overlay_pixel = SDL_MapSurfaceRGBA(E_Screen->render, 230, 20, 40, 255);
		const SDL_Rect hud_rect{20, 20, 4, 4};
		ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, &hud_rect, overlay_pixel));
	}
	E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());

    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0u);
    E_Screen->set_active_canvas(CanvasTarget::World);

    {
        ScopedUiCanvas ui(*s);
        EXPECT_EQ(CanvasTarget::UI, s->active_canvas());
        EXPECT_EQ(kUiCanvasW, s->canvas_w());
        EXPECT_EQ(kUiCanvasH, s->canvas_h());
        const auto* ui_pixels = reinterpret_cast<const Uint32*>(E_Screen->render->pixels);
        EXPECT_EQ(kWorldPixel & 0x00ffffffu, ui_pixels[0] & 0x00ffffffu)
            << "split modal UI should start with a nearest-scaled world frame";
		const auto* hud_row = reinterpret_cast<const Uint32*>(
			static_cast<const Uint8*>(E_Screen->render->pixels) +
			static_cast<std::size_t>(20) * E_Screen->render->pitch);
		EXPECT_EQ(overlay_pixel & 0x00ffffffu, hud_row[20] & 0x00ffffffu)
			<< "modal backdrop should preserve the last presented gameplay UI";
    }

    EXPECT_EQ(CanvasTarget::World, s->active_canvas());
    EXPECT_EQ(640, s->canvas_w());
    const auto* world_pixels = reinterpret_cast<const Uint32*>(E_Screen->render->pixels);
    EXPECT_EQ(kWorldPixel, world_pixels[0]);
}

TEST(CanvasScale, scoped_ui_canvas_preserves_classic_smart_gameplay_overlay)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai,
                             320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0x00123456u);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());

    Uint32 overlay_pixel = 0;
    {
        ScopedGameplayUiCanvas gameplay_ui(*s);
        overlay_pixel = SDL_MapSurfaceRGBA(E_Screen->render, 220, 30, 50, 255);
        const SDL_Rect hud_rect{12, 14, 2, 2};
        ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, &hud_rect, overlay_pixel));
    }
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());

    {
        ScopedUiCanvas ui(*s);
        EXPECT_TRUE(ui.entered_from_world());
        EXPECT_FALSE(ui.entered_from_split_world());
        const auto* hud_row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(E_Screen->render->pixels) +
            static_cast<std::size_t>(14) * E_Screen->render->pitch);
        EXPECT_EQ(overlay_pixel & 0x00ffffffu, hud_row[12] & 0x00ffffffu)
            << "classic-size modal backdrop should preserve gameplay UI";
    }

    EXPECT_EQ(CanvasTarget::World, s->active_canvas());
}

TEST(CanvasScale, scoped_ui_canvas_uses_the_last_filtered_world_scenery)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    // Keep World/UI aliased at classic dimensions, but make the scenery
    // non-uniform so raw World pixels and the last successful SAI result are
    // observably different after each is reduced to the fixed UI canvas.
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai,
                             320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());

    constexpr std::array<Uint32, 4> kPattern{
        0x00112233u, 0x00a0b0c0u, 0x00203040u, 0x00405060u};
    std::vector<Uint32> raw_rgb(
        static_cast<std::size_t>(E_Screen->world_w()) * E_Screen->world_h());
    for (int y = 0; y < E_Screen->world_h(); ++y)
    {
        auto* row = reinterpret_cast<Uint32*>(
            static_cast<Uint8*>(E_Screen->render->pixels) +
            static_cast<std::size_t>(y) * E_Screen->render->pitch);
        for (int x = 0; x < E_Screen->world_w(); ++x)
        {
            const Uint32 pixel = kPattern[static_cast<std::size_t>(
                (x & 1) | ((y & 1) << 1))];
            row[x] = pixel;
            raw_rgb[static_cast<std::size_t>(y) * E_Screen->world_w() + x] =
                pixel & 0x00ffffffu;
        }
    }

    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
    ASSERT_TRUE(E_Screen->last_world_present_used_smart_surface());
    ASSERT_NE(nullptr, E_Screen->render2);

    using SurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    SurfacePtr filtered_ui(
        SDL_CreateSurface(kUiCanvasW, kUiCanvasH, SDL_PIXELFORMAT_XRGB8888),
        SDL_DestroySurface);
    ASSERT_NE(nullptr, filtered_ui);
    ASSERT_TRUE(SDL_BlitSurfaceScaled(E_Screen->render2, nullptr,
                                      filtered_ui.get(), nullptr,
                                      SDL_SCALEMODE_NEAREST));

    int probe_x = -1;
    int probe_y = -1;
    Uint32 expected_filtered = 0;
    for (int y = 0; y < kUiCanvasH && probe_x < 0; ++y)
    {
        const auto* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(filtered_ui->pixels) +
            static_cast<std::size_t>(y) * filtered_ui->pitch);
        for (int x = 0; x < kUiCanvasW; ++x)
        {
            const Uint32 filtered = row[x] & 0x00ffffffu;
            const Uint32 raw = raw_rgb[
                static_cast<std::size_t>(y) * kUiCanvasW + x];
            if (filtered != raw)
            {
                probe_x = x;
                probe_y = y;
                expected_filtered = filtered;
                break;
            }
        }
    }
    ASSERT_GE(probe_x, 0)
        << "the non-uniform fixture must distinguish SAI from raw World";

    {
        ScopedUiCanvas ui(*s);
        const auto* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(E_Screen->render->pixels) +
            static_cast<std::size_t>(probe_y) * E_Screen->render->pitch);
        EXPECT_EQ(expected_filtered, row[probe_x] & 0x00ffffffu)
            << "modal backdrop must downsample the last filtered scenery";
        EXPECT_NE(raw_rgb[static_cast<std::size_t>(probe_y) * kUiCanvasW +
                          probe_x],
                  row[probe_x] & 0x00ffffffu)
            << "modal entry must not snap the scenery back to raw nearest";
    }

    EXPECT_EQ(CanvasTarget::World, s->active_canvas());
}

TEST(CanvasScale, zoom_steps_clamp_to_the_grid_and_reshare_at_classic)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // Steps above the classic 1.0 clamp back to it and re-share the UI
    // surface (the byte-identity dims).
    E_Screen->set_world_zoom(99, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(ui, E_Screen->render) << "classic-zoom canvas re-shares the UI pair";
    test_screen()->buffer_to_screen(0, 0, 320, 200); // present smoke

    // zoom 0.8: canvas = classic/0.8 = 400x250 (width already a multiple of
    // 4), a real split pair.
    E_Screen->set_world_zoom(8, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(400, E_Screen->world_w());
    EXPECT_EQ(250, E_Screen->world_h());
    test_screen()->buffer_to_screen(0, 0, 400, 250); // present smoke
}

TEST(CanvasScale, sai_smoothing_doubles_into_render2_at_canvas_dims)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // graphics/smoothing=sai at zoom 0.5: canvas 640x400, the world
    // presents through the software 2x scaler into a 1280x800 render2.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai, 320, 200);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());
    EXPECT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    E_Screen->set_active_canvas(CanvasTarget::World);
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0);
    auto* row0 = reinterpret_cast<Uint32*>(E_Screen->render->pixels);
    auto* row1 = reinterpret_cast<Uint32*>(
        static_cast<Uint8*>(E_Screen->render->pixels) + E_Screen->render->pitch);
    row0[0] = 0x00112233u;
    row0[1] = 0x00a0b0c0u;
    row1[0] = 0x00203040u;
    row1[1] = 0x00405060u;
    E_Screen->swap(0, 0, 4, 4);
    EXPECT_EQ(CanvasTarget::World, test_screen()->last_presented_canvas());
    ASSERT_TRUE(E_Screen->render2);
    EXPECT_EQ(1280, E_Screen->render2->w);
    EXPECT_EQ(800, E_Screen->render2->h);
    const auto* scaled_row0 = reinterpret_cast<const Uint32*>(E_Screen->render2->pixels);
    EXPECT_EQ(0x00586979u, scaled_row0[1])
        << "live smoothing must initialize the scaler's 32-bit color masks";

    // The UI canvas is untouched by the smoothing key: menus and text
    // always present through the unsmoothed engine.
    E_Screen->set_active_canvas(CanvasTarget::UI);
    EXPECT_EQ(320, E_Screen->render->w);
    E_Screen->swap(0, 0, 320, 200);
    EXPECT_EQ(CanvasTarget::UI, test_screen()->last_presented_canvas());

    // The eagle scaler picks the Eagle engine over the same canvas.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Eagle, 320, 200);
    EXPECT_EQ(RenderEngine::Eagle, E_Screen->world_engine());
    EXPECT_EQ(640, E_Screen->world_w());
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(CanvasTarget::UI, test_screen()->last_presented_canvas())
        << "routing restoration must not rewrite what was physically shown";
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
}

TEST(CanvasScale, smart_smoothing_composites_gameplay_ui_nearest_after_world_filter)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai,
                             320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    ASSERT_NE(nullptr, E_Screen->gameplay_ui_overlay_surface());

    SDL_Surface* const world = E_Screen->render;
    SDL_FillSurfaceRect(world, nullptr, 0x00112233u);
    SDL_Surface* overlay = nullptr;
    Uint32 hud_pixel = 0;
    {
        ScopedGameplayUiCanvas gameplay_ui(*s);
        EXPECT_EQ(CanvasTarget::GameplayUI, s->active_canvas());
        EXPECT_EQ(kUiCanvasW, s->canvas_w());
        EXPECT_EQ(kUiCanvasH, s->canvas_h());
        overlay = E_Screen->render;
        ASSERT_NE(world, overlay)
            << "smart-smoothed HUD must not be drawn into the scaler source";
        hud_pixel = SDL_MapSurfaceRGBA(overlay, 220, 20, 30, 255);
        const SDL_Rect hud_rect{10, 10, 1, 1};
        ASSERT_TRUE(SDL_FillSurfaceRect(overlay, &hud_rect, hud_pixel));
    }
    EXPECT_EQ(CanvasTarget::World, s->active_canvas());
    EXPECT_EQ(world, E_Screen->render);

    SDL_ScaleMode overlay_scale = SDL_SCALEMODE_INVALID;
    ASSERT_TRUE(SDL_GetTextureScaleMode(
        E_Screen->gameplay_ui_overlay_texture(), &overlay_scale));
    EXPECT_EQ(SDL_SCALEMODE_NEAREST, overlay_scale);

    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
    ASSERT_NE(nullptr, E_Screen->render2);
    EXPECT_TRUE(E_Screen->last_world_present_used_smart_surface());
    const auto* filtered_row = reinterpret_cast<const Uint32*>(
        static_cast<const Uint8*>(E_Screen->render2->pixels) +
        static_cast<std::size_t>(20) * E_Screen->render2->pitch);
    EXPECT_NE(hud_pixel, filtered_row[20])
        << "the HUD pixel must not enter the SAI source or scratch";

    SDL_Surface* captured =
        E_Screen->compose_gameplay_ui_for_capture(E_Screen->render2);
    ASSERT_NE(nullptr, captured);
    ASSERT_EQ(640, captured->w);
    ASSERT_EQ(400, captured->h);
    const auto read_rgb = [captured](int x, int y) {
        const auto* row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(captured->pixels) +
            static_cast<std::size_t>(y) * captured->pitch);
        Uint8 r = 0, g = 0, b = 0;
        SDL_GetRGB(row[x], SDL_GetPixelFormatDetails(captured->format),
                   SDL_GetSurfacePalette(captured), &r, &g, &b);
        return std::array<Uint8, 3>{r, g, b};
    };
    const std::array<Uint8, 3> hud_rgb{220, 20, 30};
    EXPECT_EQ(hud_rgb, read_rgb(20, 20));
    EXPECT_EQ(hud_rgb, read_rgb(21, 20));
    EXPECT_EQ(hud_rgb, read_rgb(20, 21));
    EXPECT_EQ(hud_rgb, read_rgb(21, 21));
    EXPECT_NE(hud_rgb, read_rgb(19, 20))
        << "capture composition must nearest-scale one HUD pixel to 2x2";
    SDL_DestroySurface(captured);

    // Disabling smoothing at zoom 1.0 makes GameplayUI alias the complete
    // World surface. The now-inactive overlay resource may remain cached, but
    // it must not affect routing or be composited into a later frame.
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 320, 200);
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    EXPECT_EQ(nullptr,
              E_Screen->compose_gameplay_ui_for_capture(E_Screen->render))
        << "an engine-only change must invalidate the preceding smart HUD";
    E_Screen->set_active_canvas(CanvasTarget::World);
    SDL_Surface* const unsmoothed_world = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::GameplayUI);
    EXPECT_EQ(unsmoothed_world, E_Screen->render);
}

TEST(CanvasScale, gameplay_refresh_filters_and_presents_the_complete_world_once)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);
    ASSERT_TRUE(s->viewob[0]);

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai, 320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);

    // Allocate the scratch, then poison it. A legacy per-view refresh below
    // would update only the artificial inset and leave this corner stale.
    E_Screen->swap(0, 0, 1, 1);
    ASSERT_TRUE(E_Screen->render2);
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0x00ffffffu);
    SDL_FillSurfaceRect(E_Screen->render2, nullptr, 0x00000000u);

    viewscreen* view = s->viewob[0].get();
    const short old_numviews = s->numviews;
    const Sint32 old_xloc = view->xloc;
    const Sint32 old_yloc = view->yloc;
    const Sint32 old_xview = view->xview;
    const Sint32 old_yview = view->yview;
    s->numviews = 1;
    view->xloc = 100;
    view->yloc = 100;
    view->xview = 80;
    view->yview = 60;

    s->refresh();

    const auto* scaled = reinterpret_cast<const Uint32*>(E_Screen->render2->pixels);
    EXPECT_NE(0u, scaled[0])
        << "refresh must filter the full scenery canvas outside view rectangles";

    s->numviews = old_numviews;
    view->xloc = old_xloc;
    view->yloc = old_yloc;
    view->xview = old_xview;
    view->yview = old_yview;
}

TEST(CanvasScale, paired_world_and_gameplay_ui_layouts_stay_aligned)
{
    ASSERT_TRUE(E_Screen);
    screen* s = test_screen();
    viewscreen* vs = s->viewob[0].get();
    ASSERT_TRUE(vs);
    ClassicCanvasRestore restore;
    const short old_numviews = s->numviews;
    const short old_mynum = vs->mynum;

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, s->world_canvas_w());
    ASSERT_EQ(320, s->gameplay_ui_canvas_w());
    ASSERT_EQ(200, s->gameplay_ui_canvas_h());

    // The World rectangle is the exact edge projection of the stable HUD
    // rectangle. A full one-player view therefore covers both canvases.
    s->numviews = 1;
    vs->mynum = 0;
    vs->resize(PREF_VIEW_FULL);
    EXPECT_EQ(0, vs->xloc);
    EXPECT_EQ(0, vs->yloc);
    EXPECT_EQ(640, vs->xview);
    EXPECT_EQ(400, vs->yview);
    EXPECT_EQ(640, vs->endx);
    EXPECT_EQ(400, vs->endy);

    // The 2p right pane begins at x=161 in the 320x200 HUD space. Projecting
    // that shared edge to the doubled World gives x=322 (not a freshly
    // computed one-pixel World seam at x=321).
    s->numviews = 2;
    vs->mynum = 1;
    vs->resize(PREF_VIEW_FULL);
    const og::view_layout::ViewLayout hud =
        og::view_layout::compute_view_layout(
            2, 1, PREF_VIEW_FULL,
            s->gameplay_ui_canvas_w(), s->gameplay_ui_canvas_h());
    const og::view_layout::ViewLayout world =
        og::view_layout::project_view_layout(
            hud, s->gameplay_ui_canvas_w(), s->gameplay_ui_canvas_h(),
            s->world_canvas_w(), s->world_canvas_h());
    ASSERT_TRUE(hud.applies);
    ASSERT_TRUE(world.applies);
    EXPECT_EQ(161, hud.x);
    EXPECT_EQ(159, hud.w);
    EXPECT_EQ(322, world.x);
    EXPECT_EQ(318, world.w);
    EXPECT_EQ(world.x, vs->xloc);
    EXPECT_EQ(world.y, vs->yloc);
    EXPECT_EQ(world.w, vs->xview);
    EXPECT_EQ(world.h, vs->yview);

    // During a gameplay-UI draw, the same viewscreen temporarily exposes the
    // baseline rectangle. Leaving the scopes restores its projected World
    // rectangle, so scenery clipping and chrome share the same boundaries.
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    {
        ScopedGameplayUiCanvas gameplay_ui(*s);
        ScopedGameplayUiViewLayout gameplay_ui_layout(*vs, *s);
        EXPECT_EQ(hud.x, vs->xloc);
        EXPECT_EQ(hud.y, vs->yloc);
        EXPECT_EQ(hud.w, vs->xview);
        EXPECT_EQ(hud.h, vs->yview);
        EXPECT_EQ(hud.x + hud.w, vs->endx);
        EXPECT_EQ(hud.y + hud.h, vs->endy);
    }
    EXPECT_EQ(world.x, vs->xloc);
    EXPECT_EQ(world.y, vs->yloc);
    EXPECT_EQ(world.w, vs->xview);
    EXPECT_EQ(world.h, vs->yview);

    // relayout_views re-derives from the player's own saved PREF_VIEW mode.
    s->numviews = old_numviews;
    vs->mynum = old_mynum;
    s->redrawme = 0;
    s->relayout_views();
    EXPECT_EQ(1, s->redrawme);
    const og::view_layout::ViewLayout expect_hud =
        og::view_layout::compute_view_layout(
            s->numviews, vs->mynum, vs->prefs[PREF_VIEW], 320, 200);
    const og::view_layout::ViewLayout expect =
        og::view_layout::project_view_layout(
            expect_hud, 320, 200, 640, 400);
    ASSERT_TRUE(expect.applies);
    EXPECT_EQ(expect.x, vs->xloc);
    EXPECT_EQ(expect.w, vs->xview);
    EXPECT_EQ(expect.h, vs->yview);
}

TEST(CanvasScale, window_resize_recomputes_the_zoom_canvas_and_layout)
{
    ASSERT_TRUE(E_Screen);
    screen* s = test_screen();
    ASSERT_TRUE(s->viewob[0]);
    ClassicCanvasRestore restore;
    const std::string old_zoom = cfg.get_setting("graphics", "zoom");
    const std::string old_smoothing = cfg.get_setting("graphics", "smoothing");

    // Start with a half-zoom canvas derived from a 320x200 window. Changing
    // to 16:9 must preserve zoom 0.5 while expanding the classic-density
    // baseline horizontally and re-projecting the live view layout.
    cfg.apply_setting("graphics", "zoom", "0.5");
    cfg.apply_setting("graphics", "smoothing", "off");
    ASSERT_TRUE(SDL_SetWindowSize(E_Screen->window, 320, 200));
    ASSERT_TRUE(SDL_SyncWindow(E_Screen->window));
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    update_overscan_setting();
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, E_Screen->world_w());

    SDL_Event ev{};
    ev.type = SDL_EVENT_WINDOW_RESIZED;
    ASSERT_TRUE(SDL_SetWindowSize(E_Screen->window, 1280, 720));
    ASSERT_TRUE(SDL_SyncWindow(E_Screen->window));
    ev.window.data1 = 1280;
    ev.window.data2 = 720;
    handle_window_event(ev);

    const og::WorldCanvasDims resized_world =
        og::compute_zoom_canvas_dims(1280, 720, 5);
    ASSERT_EQ(712, resized_world.w);
    ASSERT_EQ(400, resized_world.h);
    EXPECT_EQ(resized_world.w, E_Screen->world_w());
    EXPECT_EQ(resized_world.h, E_Screen->world_h());
    EXPECT_EQ(356, s->gameplay_ui_canvas_w());
    EXPECT_EQ(200, s->gameplay_ui_canvas_h());
    EXPECT_EQ(1280.0f, og::runtime::current_session->window_w_);
    EXPECT_EQ(720.0f, og::runtime::current_session->window_h_);
    const viewscreen* const resized_view = s->viewob[0].get();
    const og::view_layout::ViewLayout expect_hud =
        og::view_layout::compute_view_layout(
            s->numviews, resized_view->mynum,
            resized_view->prefs[PREF_VIEW], 356, 200);
    const og::view_layout::ViewLayout expect =
        og::view_layout::project_view_layout(
            expect_hud, 356, 200, resized_world.w, resized_world.h);
    ASSERT_TRUE(expect.applies);
    EXPECT_EQ(expect.w, resized_view->xview);
    EXPECT_EQ(expect.h, resized_view->yview);

    // At zoom 1.0 the same 16:9 window returns to its 356x200
    // classic-density baseline rather than the physical window dimensions.
    cfg.apply_setting("graphics", "zoom", "1.0");
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(320, E_Screen->world_w());
    handle_window_event(ev);
    const og::WorldCanvasDims resized_baseline =
        og::compute_zoom_canvas_dims(1280, 720, og::kZoomStepsMax);
    EXPECT_EQ(resized_baseline.w, E_Screen->world_w());
    EXPECT_EQ(resized_baseline.h, E_Screen->world_h());

    cfg.apply_setting("graphics", "zoom", old_zoom);
    cfg.apply_setting("graphics", "smoothing", old_smoothing);
}

TEST(CanvasScale, apply_world_scale_from_cfg_reads_the_zoom_keys)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const std::string old_zoom = cfg.get_setting("graphics", "zoom");
    const std::string old_smoothing = cfg.get_setting("graphics", "smoothing");
    const std::string old_render = cfg.get_setting("graphics", "render");
    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 400;
    update_overscan_setting();

    cfg.apply_setting("graphics", "zoom", "0.5");
    cfg.apply_setting("graphics", "smoothing", "off");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Integer, E_Screen->world_scale().mode);
    EXPECT_EQ(640, E_Screen->world_w());
    EXPECT_EQ(400, E_Screen->world_h());

    // Smoothing selects the world-only engine without changing the
    // classic-density 320x200 zoom-1 canvas.
    cfg.apply_setting("graphics", "zoom", "1.0");
    cfg.apply_setting("graphics", "smoothing", "sai");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    EXPECT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());

    // Absent keys restore the classic-density zoom 1.0 baseline.
    cfg.apply_setting("graphics", "zoom", "");
    cfg.apply_setting("graphics", "smoothing", "");
    cfg.apply_setting("graphics", "render", "normal");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Legacy, E_Screen->world_scale().mode);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());

    // Existing configs stored SAI/Eagle in graphics/render. With no new
    // smoothing key, preserve that preference through the world-only path;
    // an explicit smoothing value must override the legacy fallback.
    cfg.apply_setting("graphics", "render", "sai");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    EXPECT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    cfg.apply_setting("graphics", "smoothing", "off");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Legacy, E_Screen->world_scale().mode);

    cfg.apply_setting("graphics", "zoom", old_zoom);
    cfg.apply_setting("graphics", "smoothing", old_smoothing);
    cfg.apply_setting("graphics", "render", old_render);
    apply_world_scale_from_cfg();
}

TEST(CanvasScale, rejected_zoom_allocation_keeps_cfg_and_renderer_in_sync)
{
	ASSERT_TRUE(E_Screen);
	ClassicCanvasRestore restore;
	const std::string old_zoom = cfg.get_setting("graphics", "zoom");
	const std::string old_smoothing = cfg.get_setting("graphics", "smoothing");

	E_Screen->set_world_zoom(og::kZoomStepsMax,
	                         og::WorldScaleMode::Integer, 640, 400);
	cfg.apply_setting("graphics", "zoom", "0.5");
	cfg.apply_setting("graphics", "smoothing", "sai");
	E_Screen->fail_next_world_canvas_allocation_for_testing();
	apply_world_scale_from_cfg();

	EXPECT_EQ("1.0", cfg.get_setting("graphics", "zoom"));
	EXPECT_EQ(og::kZoomStepsMax, E_Screen->world_zoom_steps());
	EXPECT_EQ(320, E_Screen->world_w());
	EXPECT_EQ(200, E_Screen->world_h());
	// The independent smoothing request still applies to the retained canvas.
	EXPECT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
	EXPECT_EQ(RenderEngine::SAI, E_Screen->world_engine());

	cfg.apply_setting("graphics", "zoom", old_zoom);
	cfg.apply_setting("graphics", "smoothing", old_smoothing);
	apply_world_scale_from_cfg();
}

TEST(CanvasScale, mouse_mapping_uses_the_aspect_fitted_active_canvas)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const float old_overscan = og::runtime::current_session->overscan_percentage_;

    // A 16:9 window with no overscan. Zoom 1.0 uses a 356x200
    // classic-density World, while the fixed 320x200 menu UI aspect-fits to
    // x=32..608 (576x360).
    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 360;
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    update_overscan_setting();
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 640, 360);
    ASSERT_EQ(356, E_Screen->world_w());
    ASSERT_EQ(200, E_Screen->world_h());

    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = 176;
    ev.motion.y = 90;

    // Gameplay maps through the aspect-fitted World rectangle. Derive the
    // expected logical point from the same public geometry helper.
    E_Screen->set_active_canvas(CanvasTarget::World);
    clear_events();
    SDL_PushEvent(&ev);
    get_input_events(POLL);
    MouseState& mymouse = query_mouse_no_poll();
    const og::CanvasViewport world_viewport = og::fit_canvas_in_viewport(
        356, 200, 0, 0, 640, 360);
    const float expected_world_x =
        (176.0f - static_cast<float>(world_viewport.x)) * 356.0f /
        static_cast<float>(world_viewport.w);
    const float expected_world_y =
        (90.0f - static_cast<float>(world_viewport.y)) * 200.0f /
        static_cast<float>(world_viewport.h);
    EXPECT_NEAR(expected_world_x, mymouse.x, 1.0f);
    EXPECT_NEAR(expected_world_y, mymouse.y, 1.0f);

    // UI routing subtracts the 32px pillarbox and scales through the fitted
    // 576px width: (176-32)*320/576 = 80, while y=90 maps to 50.
    E_Screen->set_active_canvas(CanvasTarget::UI);
    clear_events();
    SDL_PushEvent(&ev);
    get_input_events(POLL);
    EXPECT_NEAR(80.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(50.0f, mymouse.y, 1.0f);

    og::runtime::current_session->overscan_percentage_ = old_overscan;
}

TEST(CanvasScale, cached_mouse_remaps_between_world_and_ui_without_motion)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const float old_overscan =
        og::runtime::current_session->overscan_percentage_;

    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 360;
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    update_overscan_setting();
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 640, 360);
    E_Screen->set_active_canvas(CanvasTarget::World);

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 176;
    motion.motion.y = 90;
    handle_mouse_event(motion);
    MouseState& mymouse = query_mouse_no_poll();
    const og::CanvasViewport world_viewport = og::fit_canvas_in_viewport(
        356, 200, 0, 0, 640, 360);
    const float expected_world_x =
        (176.0f - static_cast<float>(world_viewport.x)) * 356.0f /
        static_cast<float>(world_viewport.w);
    const float expected_world_y =
        (90.0f - static_cast<float>(world_viewport.y)) * 200.0f /
        static_cast<float>(world_viewport.h);
    ASSERT_NEAR(expected_world_x, mymouse.x, 1.0f);
    ASSERT_NEAR(expected_world_y, mymouse.y, 1.0f);
    const float cached_world_x = mymouse.x;
    const float cached_world_y = mymouse.y;

    // No new native event: switching targets keeps the same physical point
    // while changing its cached logical coordinates through the fitted UI.
    E_Screen->set_active_canvas(CanvasTarget::UI);
    EXPECT_NEAR(80.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(50.0f, mymouse.y, 1.0f);
    E_Screen->set_active_canvas(CanvasTarget::World);
    // Each target switch deliberately truncates the cached coordinates to
    // legacy integer pixels. A round trip may therefore lose at most one
    // pixel per conversion, but must remain at the same physical point.
    EXPECT_NEAR(cached_world_x, mymouse.x, 2.0f);
    EXPECT_NEAR(cached_world_y, mymouse.y, 2.0f);

    og::runtime::current_session->overscan_percentage_ = old_overscan;
}

TEST(CanvasScale, gameplay_ui_render_scopes_do_not_quantize_cached_mouse)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* const s = test_screen();
    ASSERT_TRUE(s);
    const float old_overscan =
        og::runtime::current_session->overscan_percentage_;

    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 360;
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    update_overscan_setting();
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 640, 360);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 177.0f;
    motion.motion.y = 91.0f;
    handle_mouse_event(motion);
    MouseState& mymouse = query_mouse_no_poll();
    const float world_x = mymouse.x;
    const float world_y = mymouse.y;

    for (int i = 0; i < 8; ++i)
    {
        ScopedGameplayUiCanvas gameplay_ui(*s);
        EXPECT_EQ(world_x, mymouse.x);
        EXPECT_EQ(world_y, mymouse.y);
    }
    EXPECT_EQ(world_x, mymouse.x);
    EXPECT_EQ(world_y, mymouse.y);

    og::runtime::current_session->overscan_percentage_ = old_overscan;
}

TEST(CanvasScale, fixed_ui_ignores_mouse_down_in_pillarbox_bars)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const float old_overscan =
        og::runtime::current_session->overscan_percentage_;

    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 360;
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    update_overscan_setting();
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 640, 360);
    E_Screen->set_active_canvas(CanvasTarget::UI);

    MouseState& mymouse = query_mouse_no_poll();
    mymouse.left = 0;
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 16; // left pillarbox; fitted UI begins at x=32
    down.button.y = 180;
    handle_mouse_event(down);
    EXPECT_EQ(0, mymouse.left);

    down.button.x = 176; // inside the fitted UI
    down.button.y = 90;
    handle_mouse_event(down);
    EXPECT_EQ(1, mymouse.left);
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    handle_mouse_event(up);
    EXPECT_EQ(0, mymouse.left);

    og::runtime::current_session->overscan_percentage_ = old_overscan;
}

TEST(CanvasScale, level_editor_pins_the_classic_canvas)
{
    ASSERT_TRUE(E_Screen);
    screen* s = test_screen();
    ASSERT_TRUE(s->viewob[0]);
    ClassicCanvasRestore restore;

    // Grow the world canvas, then run the editor (world().end pre-set so its
    // loop exits immediately, as in LevelEditorSmoke).
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, s->world_canvas_w());

    trace_clear();
    const char old_end = s->world().end;
    s->world().end = 1;
    (void)level_editor();
    s->world().end = old_end;

    ASSERT_TRUE(trace_contains("canvas", "editor_pin_classic 320x200"))
        << "editor must pin the classic canvas while its chrome assumes 320x200";
    ASSERT_TRUE(trace_contains("canvas", "editor_unpin 640x400"))
        << "editor exit must restore the zoom-derived canvas";
    EXPECT_EQ(640, s->world_canvas_w());
    EXPECT_EQ(CanvasTarget::UI, s->active_canvas());
    EXPECT_FALSE(E_Screen->world_canvas_pinned_classic());

    // While pinned, a zoom change must not fight the pin (it is remembered
    // and applied on release).
    E_Screen->set_world_canvas_pinned_classic(true);
    EXPECT_EQ(320, E_Screen->world_w());
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(320, E_Screen->world_w()) << "pin must win over a zoom change";
    E_Screen->set_world_canvas_pinned_classic(false);
}

TEST(CanvasScale, failed_pin_release_is_retryable_and_does_not_stay_pinned)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());
    E_Screen->set_world_canvas_pinned_classic(true);
    ASSERT_TRUE(E_Screen->world_canvas_pinned_classic());
    ASSERT_EQ(320, E_Screen->world_w());

    E_Screen->fail_next_world_canvas_allocation_for_testing();
    E_Screen->set_world_canvas_pinned_classic(false);
    EXPECT_FALSE(E_Screen->world_canvas_pinned_classic())
        << "a failed restore must not strand later config/resize retries";
    EXPECT_EQ(320, E_Screen->world_w())
        << "the working classic pair remains live after allocation failure";
    EXPECT_EQ(200, E_Screen->world_h());

    // Reapplying the same selected zoom must retry now that the pin is clear.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(640, E_Screen->world_w());
    EXPECT_EQ(400, E_Screen->world_h());
    EXPECT_EQ(5, E_Screen->world_zoom_steps());
}

TEST(CanvasScale, level_editor_filters_map_but_keeps_controls_on_crisp_overlay)
{
    ASSERT_TRUE(E_Screen);
    screen* s = test_screen();
    ASSERT_TRUE(s);
    ASSERT_TRUE(s->viewob[0]);
    ClassicCanvasRestore restore;

    // The editor is pinned to classic coordinates, but SAI remains the World
    // present engine. Its draw transaction must therefore produce both a 2x
    // scenery scratch and a nearest-composited authoring-UI overlay.
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai,
                             320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    s->world().end = 0;

    SDL_Thread* stopper = SDL_CreateThread(
        stop_editor_after_render, "editor_overlay_stopper", nullptr);
    ASSERT_NE(nullptr, stopper);
    (void)level_editor();
    SDL_WaitThread(stopper, nullptr);
    s->world().end = 0;

    ASSERT_NE(nullptr, E_Screen->render2)
        << "SAI must have filtered the editor's map/world layer";
    EXPECT_EQ(640, E_Screen->render2->w);
    EXPECT_EQ(400, E_Screen->render2->h);

    SDL_Surface* const overlay = E_Screen->gameplay_ui_overlay_surface();
    ASSERT_NE(nullptr, overlay)
        << "the last editor present must retain its crisp authoring UI";
    const auto* overlay_row = reinterpret_cast<const Uint32*>(
        static_cast<const Uint8*>(overlay->pixels) + 10 * overlay->pitch);
    Uint8 ui_r = 0, ui_g = 0, ui_b = 0, ui_a = 0;
    SDL_GetRGBA(overlay_row[10], SDL_GetPixelFormatDetails(overlay->format),
                SDL_GetSurfacePalette(overlay), &ui_r, &ui_g, &ui_b, &ui_a);
    EXPECT_EQ(SDL_ALPHA_OPAQUE, ui_a)
        << "the File button at (10,10) must live on the editor UI layer";

    SDL_Surface* const composed =
        E_Screen->compose_gameplay_ui_for_capture(E_Screen->render2);
    ASSERT_NE(nullptr, composed);
    const auto* composed_row = reinterpret_cast<const Uint32*>(
        static_cast<const Uint8*>(composed->pixels) + 20 * composed->pitch);
    Uint8 out_r = 0, out_g = 0, out_b = 0;
    SDL_GetRGB(composed_row[20], SDL_GetPixelFormatDetails(composed->format),
               SDL_GetSurfacePalette(composed), &out_r, &out_g, &out_b);
    const std::array<Uint8, 3> ui_rgb{ui_r, ui_g, ui_b};
    const std::array<Uint8, 3> out_rgb{out_r, out_g, out_b};
    EXPECT_EQ(ui_rgb, out_rgb)
        << "editor chrome must nearest-scale over the filtered map";
    SDL_DestroySurface(composed);
}

// --- Adversarial pokes -------------------------------------------------------

TEST(CanvasScale, hostile_zoom_steps_clamp_and_round)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();

    // zoom 0.3: 320*10/3 = 1066 rounds DOWN to a multiple of 4 (1064); height
    // floors (200*10/3 = 666). A REAL surface + texture pair at non-round
    // dims, plotted and present-smoked in the far corner (the offset math
    // must use the 1064px stride).
    E_Screen->set_world_zoom(3, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(1064, E_Screen->world_w());
    EXPECT_EQ(666, E_Screen->world_h());
    EXPECT_EQ(0, E_Screen->world_w() % 4);
    E_Screen->set_active_canvas(CanvasTarget::World);
    s->pointb(1063, 665, 47);
    int idx = 0;
    s->get_pixel(1063, 665, &idx);
    EXPECT_EQ(47, idx);
    s->buffer_to_screen(0, 0, 1064, 666);

    // Out-of-range steps clamp to the grid: 0 and negative pin to the
    // deepest zoom (0.1 -> 3200x2000 is a real allocation, so clamp is
    // asserted via the computed dims), oversized steps pin to classic.
    const og::WorldCanvasDims deepest = og::compute_zoom_canvas_dims(320, 200, 1);
    EXPECT_EQ(3200, deepest.w);
    EXPECT_EQ(2000, deepest.h);
    E_Screen->set_world_zoom(0, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(3200, E_Screen->world_w());
    EXPECT_EQ(2000, E_Screen->world_h());
    E_Screen->set_world_zoom(-7, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(3200, E_Screen->world_w());
    E_Screen->set_world_zoom(99, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());

    // The sai scaler over the non-round canvas: render2 doubles the CANVAS
    // (2128x1332) — no hardcoded scratch — recreating any differently-sized
    // scratch a previous mode left behind.
    E_Screen->set_world_zoom(3, og::WorldScaleMode::Sai, 320, 200);
    ASSERT_EQ(1064, E_Screen->world_w());
    ASSERT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->swap(0, 0, 1064, 666);
    ASSERT_TRUE(E_Screen->render2);
    EXPECT_EQ(2128, E_Screen->render2->w);
    EXPECT_EQ(1332, E_Screen->render2->h);
}

TEST(CanvasScale, garbage_zoom_value_falls_back_to_classic_density_zoom_one)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const std::string old_zoom = cfg.get_setting("graphics", "zoom");
    const std::string old_smoothing = cfg.get_setting("graphics", "smoothing");

    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 400;
    update_overscan_setting();

    // Present-but-garbage values must fall back to the classic-density 1.0
    // canvas, never crash or produce a degenerate canvas.
    cfg.apply_setting("graphics", "smoothing", "off");
    for (const char* bogus : {"2x-bogus", "-0.5", "11", "1e9", "sai"}) {
        cfg.apply_setting("graphics", "zoom", bogus);
        apply_world_scale_from_cfg();
        EXPECT_EQ(og::WorldScaleMode::Legacy, E_Screen->world_scale().mode)
            << "graphics/zoom=" << bogus;
        EXPECT_EQ(320, E_Screen->world_w()) << "graphics/zoom=" << bogus;
        EXPECT_EQ(200, E_Screen->world_h()) << "graphics/zoom=" << bogus;
    }

    // Garbage smoothing likewise reads as "off".
    cfg.apply_setting("graphics", "zoom", "0.5");
    cfg.apply_setting("graphics", "smoothing", "trilinear");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Integer, E_Screen->world_scale().mode);
    EXPECT_EQ(RenderEngine::NoZoom, E_Screen->world_engine());

    cfg.apply_setting("graphics", "zoom", old_zoom);
    cfg.apply_setting("graphics", "smoothing", old_smoothing);
    apply_world_scale_from_cfg();
}

TEST(CanvasScale, fadeblack_matches_the_grown_world_canvas)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, E_Screen->world_w());
    E_Screen->set_active_canvas(CanvasTarget::World);

    // Paint a pixel outside the classic 320x200 area, then fade to black:
    // FadeBetween requires exact dim/pitch matches against the ACTIVE render
    // target, so a hardcoded 320x200 black surface would abort with "width
    // mismatch" and return 0 — and never darken the grown corner.
    s->pointb(639, 399, 47);
    EXPECT_EQ(1, s->fadeblack(false)) << "fade-to-black must run at 640x400";
    Uint8 r = 255, g = 255, b = 255;
    s->get_pixel(639, 399, &r, &g, &b);
    EXPECT_EQ(0, r | g | b) << "the fade must have reached the grown corner";
    EXPECT_EQ(1, s->fadeblack(true)) << "fade-from-black must run at 640x400";
}

TEST(CanvasScale, smart_smoothed_fade_discards_prepared_gameplay_ui)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai,
                             320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0x00ffffffu);
    {
        ScopedGameplayUiCanvas gameplay_ui(*s);
        const SDL_Rect hud_rect{10, 10, 8, 8};
        ASSERT_TRUE(SDL_FillSurfaceRect(
            E_Screen->render, &hud_rect,
            SDL_MapSurfaceRGBA(E_Screen->render, 255, 0, 0, 255)));
    }

    EXPECT_EQ(1, s->fadeblack(false));
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    EXPECT_EQ(nullptr,
              E_Screen->compose_gameplay_ui_for_capture(E_Screen->render))
        << "fade swaps must neither replay nor capture stale full-bright HUD";
    E_Screen->set_active_canvas(CanvasTarget::GameplayUI);
    EXPECT_EQ(E_Screen->world_w(), E_Screen->canvas_w())
        << "an inactive cached overlay must route GameplayUI back to World";
    E_Screen->set_active_canvas(CanvasTarget::World);
    Uint8 r = 255, g = 255, b = 255;
    s->get_pixel(10, 10, &r, &g, &b);
    EXPECT_EQ(0, r | g | b);
}

TEST(CanvasScale, nearest_zoom_overlay_allocation_failure_safely_aliases_world)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());
    ASSERT_EQ(320, s->gameplay_ui_canvas_w());
    ASSERT_EQ(200, s->gameplay_ui_canvas_h());
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->fail_next_gameplay_ui_allocation_for_testing();
    E_Screen->begin_gameplay_frame();

    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    EXPECT_FALSE(s->gameplay_ui_canvas_available())
        << "touch hit-testing must follow the World-sized fallback controls";
    EXPECT_FALSE(E_Screen->smart_present_suppressed())
        << "nearest rendering needs no smart-scaler fallback state";
    EXPECT_EQ(nullptr, E_Screen->gameplay_ui_overlay_surface());
    SDL_Surface* const world = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::GameplayUI);
    EXPECT_EQ(world, E_Screen->render);
    EXPECT_EQ(640, s->canvas_w());
    EXPECT_EQ(400, s->canvas_h());
    const SDL_Rect fallback_hud{639, 399, 1, 1};
    const Uint32 fallback_pixel =
        SDL_MapSurfaceRGBA(world, 230, 40, 20, 255);
    ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, &fallback_hud,
                                   fallback_pixel));
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
    EXPECT_EQ(nullptr,
              E_Screen->compose_gameplay_ui_for_capture(E_Screen->render));
    const auto* last_row = reinterpret_cast<const Uint32*>(
        static_cast<const Uint8*>(world->pixels) +
        static_cast<std::size_t>(399) * world->pitch);
    EXPECT_EQ(fallback_pixel, last_row[639])
        << "fallback HUD pixels remain part of the complete nearest frame";

    // The failed-size latch prevents an allocation storm. A World canvas
    // change clears it and the next frame can restore the fixed overlay.
    E_Screen->begin_gameplay_frame();
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->set_world_zoom(8, og::WorldScaleMode::Integer, 320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    EXPECT_TRUE(s->gameplay_ui_canvas_available());
    ASSERT_NE(nullptr, E_Screen->gameplay_ui_overlay_surface());
    EXPECT_EQ(320, E_Screen->gameplay_ui_overlay_surface()->w);
    EXPECT_EQ(200, E_Screen->gameplay_ui_overlay_surface()->h);
}

TEST(CanvasScale, gameplay_overlay_allocation_failure_presents_complete_frame_nearest)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    // Start from no cached smart resources, then fail only the overlay after
    // the SAI scratch succeeds. HUD must alias World and the whole frame must
    // bypass SAI; otherwise the "world-only" filter would consume the HUD.
    E_Screen->set_world_zoom(5,
                             og::WorldScaleMode::Integer, 320, 200);
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai,
                             320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->fail_next_gameplay_ui_allocation_for_testing();
    E_Screen->begin_gameplay_frame();
    ASSERT_NE(nullptr, E_Screen->render2)
        << "the injected failure must occur after scaler allocation";
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    EXPECT_TRUE(E_Screen->smart_present_suppressed());

    SDL_Surface* const world = E_Screen->render;
    SDL_FillSurfaceRect(world, nullptr, 0x00ffffffu);
    E_Screen->set_active_canvas(CanvasTarget::GameplayUI);
    EXPECT_EQ(world, E_Screen->render)
        << "fallback HUD must draw into the complete raw World frame";
    const SDL_Rect hud_rect{10, 10, 1, 1};
    ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, &hud_rect, 0x00ff0000u));
    E_Screen->set_active_canvas(CanvasTarget::World);

    SDL_FillSurfaceRect(E_Screen->render2, nullptr, 0u);
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
    EXPECT_FALSE(E_Screen->last_world_present_used_smart_surface());
    const auto* scratch =
        reinterpret_cast<const Uint32*>(E_Screen->render2->pixels);
    EXPECT_EQ(0u, scratch[0])
        << "overlay failure must skip SAI rather than filter HUD with World";

    // The failed-size latch makes the next begin a stable raw fallback too;
    // any explicit scale-mode change clears the latch and permits a clean
    // fixed-HUD retry, including turning smoothing off.
    E_Screen->begin_gameplay_frame();
    EXPECT_TRUE(E_Screen->smart_present_suppressed());
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer,
                             320, 200);
    E_Screen->begin_gameplay_frame();
    EXPECT_FALSE(E_Screen->smart_present_suppressed());
    EXPECT_TRUE(E_Screen->gameplay_ui_overlay_active());
    EXPECT_EQ(nullptr, E_Screen->render2);
}

TEST(CanvasScale, allocation_failure_preserves_the_live_world_canvas_pair)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::World);
    SDL_Surface* const live_surface = E_Screen->render;
    SDL_Texture* const live_texture = E_Screen->render_tex;
    test_screen()->pointb(639, 399, 47);

    // A width whose 32bpp byte size blows the SDL2-era 2 GiB surface bound:
    // set_world_canvas_size must reject it before SDL ever allocates (SDL3's
    // own guard is size_t-wide, so the request would otherwise reach the
    // real allocator). The failed replacement must leave the complete live
    // 640x400 pair, its state, and its contents intact.
    EXPECT_FALSE(E_Screen->set_world_canvas_size(1 << 30, 64));
    EXPECT_EQ(640, E_Screen->world_w());
    EXPECT_EQ(400, E_Screen->world_h());
    EXPECT_EQ(5, E_Screen->world_zoom_steps());
    EXPECT_EQ(og::WorldScaleMode::Integer, E_Screen->world_scale().mode);
    EXPECT_EQ(RenderEngine::NoZoom, E_Screen->world_engine());
    EXPECT_EQ(CanvasTarget::World, E_Screen->active_canvas());
    EXPECT_EQ(live_surface, E_Screen->render);
    EXPECT_EQ(live_texture, E_Screen->render_tex);
    int pixel = 0;
    test_screen()->get_pixel(639, 399, &pixel);
    EXPECT_EQ(47, pixel);
    test_screen()->buffer_to_screen(0, 0, 640, 400); // present smoke
}

TEST(CanvasScale, mouse_clicks_land_on_the_active_canvas_at_two_zooms)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const float old_overscan = og::runtime::current_session->overscan_percentage_;

    // A 1280x800 window with no overscan.
    og::runtime::current_session->window_w_ = 1280;
    og::runtime::current_session->window_h_ = 800;
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    update_overscan_setting();

    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.x = 640; // window center
    down.button.y = 400;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.type = SDL_EVENT_MOUSE_BUTTON_UP;

    MouseState& mymouse = query_mouse_no_poll();

    // Zoom 0.5 doubles the 320x200 classic-density baseline. The window
    // center therefore lands at the 640x400 World's center (320,200).
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 1280, 800);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::World);
    clear_events();
    SDL_PushEvent(&down);
    get_input_events(POLL);
    EXPECT_EQ(1, mymouse.left);
    EXPECT_NEAR(320.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(200.0f, mymouse.y, 1.0f);
    clear_events();
    SDL_PushEvent(&up);
    get_input_events(POLL);
    EXPECT_EQ(0, mymouse.left);

    // Zoom 1.0 restores the 320x200 baseline; the same click lands at its
    // center (160,100).
    E_Screen->set_world_zoom(og::kZoomStepsMax,
                             og::WorldScaleMode::Integer, 1280, 800);
    ASSERT_EQ(320, E_Screen->world_w());
    ASSERT_EQ(200, E_Screen->world_h());
    clear_events();
    SDL_PushEvent(&down);
    get_input_events(POLL);
    EXPECT_EQ(1, mymouse.left);
    EXPECT_NEAR(160.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(100.0f, mymouse.y, 1.0f);
    clear_events();
    SDL_PushEvent(&up);
    get_input_events(POLL);
    EXPECT_EQ(0, mymouse.left);

    // The UI canvas is zoom-blind at BOTH zooms: the same click maps to the
    // classic (160,100) menu space.
    E_Screen->set_active_canvas(CanvasTarget::UI);
    clear_events();
    SDL_PushEvent(&down);
    get_input_events(POLL);
    EXPECT_NEAR(160.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(100.0f, mymouse.y, 1.0f);
    clear_events();
    SDL_PushEvent(&up);
    get_input_events(POLL);

    og::runtime::current_session->overscan_percentage_ = old_overscan;
}

TEST(CanvasScale, smart_scaler_scratch_is_released_on_disable_and_canvas_resize)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai, 320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->swap(0, 0, 1, 1);
    ASSERT_NE(nullptr, E_Screen->render2);
    ASSERT_NE(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(1280, E_Screen->render2->w);
    EXPECT_EQ(800, E_Screen->render2->h);
    EXPECT_NE(nullptr, E_Screen->gameplay_ui_overlay_surface());

    // An engine-only change to nearest releases the expensive smart-scaler
    // CPU/GPU pair. The small fixed HUD pair may remain cached, but is inert
    // until begin_gameplay_frame prepares it for the reduced zoom.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer, 320, 200);
    EXPECT_EQ(nullptr, E_Screen->render2);
    EXPECT_EQ(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(nullptr, E_Screen->gameplay_ui_overlay_surface())
        << "the cached overlay must be hidden after its capture is invalidated";
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());

    // Recreate it, then prove a logical canvas resize drops the stale size
    // immediately rather than waiting for the next filtered present.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Eagle, 320, 200);
    E_Screen->begin_gameplay_frame();
    E_Screen->swap(0, 0, 1, 1);
    ASSERT_NE(nullptr, E_Screen->render2);
    ASSERT_NE(nullptr, E_Screen->gameplay_ui_overlay_surface());
    E_Screen->set_world_zoom(3, og::WorldScaleMode::Eagle, 320, 200);
    EXPECT_EQ(nullptr, E_Screen->render2);
    EXPECT_EQ(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(nullptr, E_Screen->gameplay_ui_overlay_surface());

    E_Screen->begin_gameplay_frame();
    E_Screen->swap(0, 0, 1, 1);
    ASSERT_NE(nullptr, E_Screen->render2);
    EXPECT_EQ(2128, E_Screen->render2->w);
    EXPECT_EQ(1332, E_Screen->render2->h);
}

TEST(CanvasScale, deepest_zoom_preserves_smoothing_choice_but_skips_huge_scratch)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // 0.1 zoom would request a 6400x4000 result: 25.6 million scaler
    // operations/output pixels, a 102.4 MB CPU surface, another texture of
    // similar size, and a 102.4 MB upload per present. The bounded software
    // path falls back to the raw nearest-present path without rewriting cfg.
    constexpr Sint64 kDeepScratchPixels =
        static_cast<Sint64>(6400) * static_cast<Sint64>(4000);
    static_assert(kDeepScratchPixels > Screen::kSmartScaleScratchPixelBudget);

    E_Screen->set_world_zoom(1, og::WorldScaleMode::Sai, 320, 200);
    ASSERT_EQ(3200, E_Screen->world_w());
    ASSERT_EQ(2000, E_Screen->world_h());
    ASSERT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    ASSERT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    E_Screen->set_active_canvas(CanvasTarget::World);

    E_Screen->begin_gameplay_frame();
    EXPECT_TRUE(E_Screen->smart_present_suppressed());
    EXPECT_TRUE(E_Screen->gameplay_ui_overlay_active())
        << "smart-scaler fallback must not make fixed HUD geometry zoom out";
    ASSERT_NE(nullptr, E_Screen->gameplay_ui_overlay_surface());
    EXPECT_EQ(320, E_Screen->gameplay_ui_overlay_surface()->w);
    EXPECT_EQ(200, E_Screen->gameplay_ui_overlay_surface()->h);
    SDL_ScaleMode overlay_scale = SDL_SCALEMODE_INVALID;
    ASSERT_TRUE(SDL_GetTextureScaleMode(
        E_Screen->gameplay_ui_overlay_texture(), &overlay_scale));
    EXPECT_EQ(SDL_SCALEMODE_NEAREST, overlay_scale);
    E_Screen->swap(0, 0, 1, 1);

    EXPECT_EQ(nullptr, E_Screen->render2);
    EXPECT_EQ(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode)
        << "resource fallback must not rewrite the selected smoothing mode";
}
