// Integration coverage for the cfg graphics/zoom + graphics/smoothing
// world-canvas machinery:
//  * Screen::set_world_zoom sizing the world surface+texture from the zoom
//    steps (canvas = classic 320x200 / zoom, window-independent),
//  * the per-canvas present engine in Screen::swap (off = GPU nearest of
//    the variable canvas; sai/eagle = software 2x into a canvas*2 render2,
//    world canvas ONLY — menus/UI always present unsmoothed),
//  * viewscreen relayout from the live canvas dims (screen::relayout_views
//    and viewscreen::resize(whatmode)),
//  * window-resize independence (the SDL_EVENT_WINDOW_RESIZED bridge only
//    retracks overscan, never the canvas),
//  * the level editor's classic-canvas pin.
//
// Every test restores the Legacy default (shared 320x200 canvas, UI target)
// so the rest of the binary keeps running on the classic setup.

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

// RAII restore of the classic default renderer state around a test body.
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
        E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer);
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

TEST(CanvasScale, legacy_default_shares_one_320x200_surface)
{
    ASSERT_TRUE(E_Screen);
    ASSERT_EQ(og::WorldScaleMode::Legacy, E_Screen->world_scale().mode);
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(ui, E_Screen->render) << "default world canvas must alias the UI surface";
    EXPECT_EQ(320, E_Screen->render->w);
    EXPECT_EQ(200, E_Screen->render->h);
    EXPECT_EQ(320, test_screen()->world_canvas_w());
    EXPECT_EQ(200, test_screen()->world_canvas_h());
    E_Screen->set_active_canvas(CanvasTarget::UI);
}

TEST(CanvasScale, zoom_half_splits_a_640x400_world_canvas_and_presents)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // zoom=0.5: world canvas = classic/0.5 = 640x400, regardless of the
    // window (the zoom model is window-independent).
    const og::WorldCanvasDims want = og::compute_zoom_canvas_dims(5);
    ASSERT_EQ(640, want.w);
    ASSERT_EQ(400, want.h);
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
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

TEST(CanvasScale, scoped_ui_canvas_seeds_from_world_and_restores_routing)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();
    ASSERT_TRUE(s);

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai);
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
			static_cast<std::size_t>(10) * E_Screen->render->pitch);
		EXPECT_EQ(overlay_pixel & 0x00ffffffu, hud_row[10] & 0x00ffffffu)
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

    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai);
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
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai);
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
    E_Screen->set_world_zoom(99, og::WorldScaleMode::Integer);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(ui, E_Screen->render) << "classic-zoom canvas re-shares the UI pair";
    test_screen()->buffer_to_screen(0, 0, 320, 200); // present smoke

    // zoom 0.8: canvas = classic/0.8 = 400x250 (width already a multiple of
    // 4), a real split pair.
    E_Screen->set_world_zoom(8, og::WorldScaleMode::Integer);
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
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai);
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
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Eagle);
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

    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai);
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

    // Disabling smoothing releases the layer and makes GameplayUI alias the
    // complete World surface, which is also the allocation-fallback behavior.
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer);
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    EXPECT_EQ(nullptr, E_Screen->gameplay_ui_overlay_surface());
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

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai);
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

TEST(CanvasScale, relayout_views_follows_the_canvas)
{
    ASSERT_TRUE(E_Screen);
    screen* s = test_screen();
    viewscreen* vs = s->viewob[0].get();
    ASSERT_TRUE(vs);
    ClassicCanvasRestore restore;
    const short old_numviews = s->numviews;
    const short old_mynum = vs->mynum;

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
    ASSERT_EQ(640, s->world_canvas_w());

    // 1p FULL covers the whole doubled canvas.
    s->numviews = 1;
    vs->mynum = 0;
    vs->resize(PREF_VIEW_FULL);
    EXPECT_EQ(0, vs->xloc);
    EXPECT_EQ(0, vs->yloc);
    EXPECT_EQ(640, vs->xview);
    EXPECT_EQ(400, vs->yview);
    EXPECT_EQ(640, vs->endx);
    EXPECT_EQ(400, vs->endy);

    // 2p right pane on the doubled canvas.
    s->numviews = 2;
    vs->mynum = 1;
    vs->resize(PREF_VIEW_FULL);
    EXPECT_EQ(321, vs->xloc);
    EXPECT_EQ(319, vs->xview);
    EXPECT_EQ(400, vs->yview);

    // relayout_views re-derives from the player's own saved PREF_VIEW mode.
    s->numviews = old_numviews;
    vs->mynum = old_mynum;
    s->redrawme = 0;
    s->relayout_views();
    EXPECT_EQ(1, s->redrawme);
    const og::view_layout::ViewLayout expect =
        og::view_layout::compute_view_layout(
            s->numviews, vs->mynum, vs->prefs[PREF_VIEW], 640, 400);
    ASSERT_TRUE(expect.applies);
    EXPECT_EQ(expect.x, vs->xloc);
    EXPECT_EQ(expect.w, vs->xview);
    EXPECT_EQ(expect.h, vs->yview);
}

TEST(CanvasScale, window_resize_leaves_the_zoom_canvas_alone)
{
    ASSERT_TRUE(E_Screen);
    screen* s = test_screen();
    ASSERT_TRUE(s->viewob[0]);
    ClassicCanvasRestore restore;

    // The zoom-model canvas is window-independent: a window resize only
    // re-derives the overscan viewport; canvas and view layout stay put.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
    ASSERT_EQ(640, E_Screen->world_w());

    SDL_Event ev{};
    ev.type = SDL_EVENT_WINDOW_RESIZED;
    ev.window.data1 = 1280;
    ev.window.data2 = 800;
    handle_window_event(ev);

    EXPECT_EQ(640, E_Screen->world_w());
    EXPECT_EQ(400, E_Screen->world_h());
    EXPECT_EQ(1280.0f, og::runtime::current_session->window_w_);
    EXPECT_EQ(800.0f, og::runtime::current_session->window_h_);

    // Classic zoom: same event, same stability.
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer);
    ASSERT_EQ(320, E_Screen->world_w());
    handle_window_event(ev);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
}

TEST(CanvasScale, apply_world_scale_from_cfg_reads_the_zoom_keys)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const std::string old_zoom = cfg.get_setting("graphics", "zoom");
    const std::string old_smoothing = cfg.get_setting("graphics", "smoothing");
    const std::string old_render = cfg.get_setting("graphics", "render");

    cfg.apply_setting("graphics", "zoom", "0.5");
    cfg.apply_setting("graphics", "smoothing", "off");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Integer, E_Screen->world_scale().mode);
    EXPECT_EQ(640, E_Screen->world_w());
    EXPECT_EQ(400, E_Screen->world_h());

    // Smoothing selects the world-only engine; at classic zoom the canvas
    // stays the byte-identity 320x200 (shared surface, smoothed world
    // present).
    cfg.apply_setting("graphics", "zoom", "1.0");
    cfg.apply_setting("graphics", "smoothing", "sai");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    EXPECT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());

    // Absent keys restore the Legacy classic canvas.
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

	E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer);
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

TEST(CanvasScale, mouse_mapping_divides_by_the_active_canvas_dims)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const float old_overscan = og::runtime::current_session->overscan_percentage_;

    // A 640x400 window with no overscan; scale=1 grows the world canvas to
    // the full window while the UI canvas stays 320x200.
    og::runtime::current_session->window_w_ = 640;
    og::runtime::current_session->window_h_ = 400;
    og::runtime::current_session->overscan_percentage_ = 0.0f;
    update_overscan_setting();
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());

    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = 320; // window center
    ev.motion.y = 200;

    // Gameplay routing (world canvas active): the window->canvas mapping
    // divides by the world dims, so the center lands at 320,200 of the
    // zoom-0.5 640x400 canvas.
    E_Screen->set_active_canvas(CanvasTarget::World);
    clear_events();
    SDL_PushEvent(&ev);
    get_input_events(POLL);
    MouseState& mymouse = query_mouse_no_poll();
    EXPECT_NEAR(320.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(200.0f, mymouse.y, 1.0f);

    // Menu routing (UI canvas active): the SAME window point maps to the
    // classic 160,100 — every menu layout and pixel pin keeps its logical
    // 320x200 mouse space regardless of the world scale.
    E_Screen->set_active_canvas(CanvasTarget::UI);
    clear_events();
    SDL_PushEvent(&ev);
    get_input_events(POLL);
    EXPECT_NEAR(160.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(100.0f, mymouse.y, 1.0f);

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
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
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
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
    EXPECT_EQ(320, E_Screen->world_w()) << "pin must win over a zoom change";
    E_Screen->set_world_canvas_pinned_classic(false);
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
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai);
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

    // zoom 0.3: 320*10/3 = 1066 rounds UP to a multiple of 4 (1068); height
    // floors (200*10/3 = 666). A REAL surface + texture pair at non-round
    // dims, plotted and present-smoked in the far corner (the offset math
    // must use the 1068px stride).
    E_Screen->set_world_zoom(3, og::WorldScaleMode::Integer);
    EXPECT_EQ(1068, E_Screen->world_w());
    EXPECT_EQ(666, E_Screen->world_h());
    EXPECT_EQ(0, E_Screen->world_w() % 4);
    E_Screen->set_active_canvas(CanvasTarget::World);
    s->pointb(1067, 665, 47);
    int idx = 0;
    s->get_pixel(1067, 665, &idx);
    EXPECT_EQ(47, idx);
    s->buffer_to_screen(0, 0, 1068, 666);

    // Out-of-range steps clamp to the grid: 0 and negative pin to the
    // deepest zoom (0.1 -> 3200x2000 is a real allocation, so clamp is
    // asserted via the computed dims), oversized steps pin to classic.
    const og::WorldCanvasDims deepest = og::compute_zoom_canvas_dims(1);
    EXPECT_EQ(3200, deepest.w);
    EXPECT_EQ(2000, deepest.h);
    E_Screen->set_world_zoom(0, og::WorldScaleMode::Integer);
    EXPECT_EQ(3200, E_Screen->world_w());
    EXPECT_EQ(2000, E_Screen->world_h());
    E_Screen->set_world_zoom(-7, og::WorldScaleMode::Integer);
    EXPECT_EQ(3200, E_Screen->world_w());
    E_Screen->set_world_zoom(99, og::WorldScaleMode::Integer);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());

    // The sai scaler over the non-round canvas: render2 doubles the CANVAS
    // (2136x1332) — no hardcoded scratch — recreating any differently-sized
    // scratch a previous mode left behind.
    E_Screen->set_world_zoom(3, og::WorldScaleMode::Sai);
    ASSERT_EQ(1068, E_Screen->world_w());
    ASSERT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->swap(0, 0, 1068, 666);
    ASSERT_TRUE(E_Screen->render2);
    EXPECT_EQ(2136, E_Screen->render2->w);
    EXPECT_EQ(1332, E_Screen->render2->h);
}

TEST(CanvasScale, garbage_zoom_value_falls_back_to_the_classic_canvas)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const std::string old_zoom = cfg.get_setting("graphics", "zoom");
    const std::string old_smoothing = cfg.get_setting("graphics", "smoothing");

    // Present-but-garbage values must fall back to the byte-identical
    // classic canvas, never crash or produce a degenerate canvas.
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

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
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

    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai);
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
    EXPECT_EQ(nullptr, E_Screen->gameplay_ui_overlay_surface())
        << "fade swaps must neither replay nor capture stale full-bright HUD";
    Uint8 r = 255, g = 255, b = 255;
    s->get_pixel(10, 10, &r, &g, &b);
    EXPECT_EQ(0, r | g | b);
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
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer);
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai);
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
    // changing the smart mode clears the latch and permits a clean retry.
    E_Screen->begin_gameplay_frame();
    EXPECT_TRUE(E_Screen->smart_present_suppressed());
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Eagle);
    E_Screen->begin_gameplay_frame();
    EXPECT_FALSE(E_Screen->smart_present_suppressed());
    EXPECT_TRUE(E_Screen->gameplay_ui_overlay_active());
}

TEST(CanvasScale, allocation_failure_preserves_the_live_world_canvas_pair)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
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

    // zoom 0.5: world canvas 640x400; a click at the 1280x800 window's
    // center lands at world (320,200) — half the window coordinate.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
    ASSERT_EQ(640, E_Screen->world_w());
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

    // zoom 0.2: world canvas 1600x1000; the same click lands at world
    // (800,500).
    E_Screen->set_world_zoom(2, og::WorldScaleMode::Integer);
    ASSERT_EQ(1600, E_Screen->world_w());
    clear_events();
    SDL_PushEvent(&down);
    get_input_events(POLL);
    EXPECT_EQ(1, mymouse.left);
    EXPECT_NEAR(800.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(500.0f, mymouse.y, 1.0f);
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

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->swap(0, 0, 1, 1);
    ASSERT_NE(nullptr, E_Screen->render2);
    ASSERT_NE(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(1280, E_Screen->render2->w);
    EXPECT_EQ(800, E_Screen->render2->h);
    EXPECT_NE(nullptr, E_Screen->gameplay_ui_overlay_surface());

    // An engine-only change to nearest must not retain the CPU/GPU pair.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Integer);
    EXPECT_EQ(nullptr, E_Screen->render2);
    EXPECT_EQ(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(nullptr, E_Screen->gameplay_ui_overlay_surface());

    // Recreate it, then prove a logical canvas resize drops the stale size
    // immediately rather than waiting for the next filtered present.
    E_Screen->set_world_zoom(5, og::WorldScaleMode::Eagle);
    E_Screen->begin_gameplay_frame();
    E_Screen->swap(0, 0, 1, 1);
    ASSERT_NE(nullptr, E_Screen->render2);
    ASSERT_NE(nullptr, E_Screen->gameplay_ui_overlay_surface());
    E_Screen->set_world_zoom(3, og::WorldScaleMode::Eagle);
    EXPECT_EQ(nullptr, E_Screen->render2);
    EXPECT_EQ(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(nullptr, E_Screen->gameplay_ui_overlay_surface());

    E_Screen->begin_gameplay_frame();
    E_Screen->swap(0, 0, 1, 1);
    ASSERT_NE(nullptr, E_Screen->render2);
    EXPECT_EQ(2136, E_Screen->render2->w);
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

    E_Screen->set_world_zoom(1, og::WorldScaleMode::Sai);
    ASSERT_EQ(3200, E_Screen->world_w());
    ASSERT_EQ(2000, E_Screen->world_h());
    ASSERT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    ASSERT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    E_Screen->set_active_canvas(CanvasTarget::World);

    E_Screen->begin_gameplay_frame();
    EXPECT_TRUE(E_Screen->smart_present_suppressed());
    EXPECT_FALSE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->swap(0, 0, 1, 1);

    EXPECT_EQ(nullptr, E_Screen->render2);
    EXPECT_EQ(nullptr, E_Screen->render2_tex);
    EXPECT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode)
        << "resource fallback must not rewrite the selected smoothing mode";
}
