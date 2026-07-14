// Integration coverage for the cfg graphics/scale world-canvas machinery
// (stages 3+4 of the resolution/tile-scale decoupling):
//  * Screen::set_world_scale / apply_world_scale_for_window sizing the world
//    surface+texture from window dims (clamped to the classic 320x200 min),
//  * the per-canvas present engine in Screen::swap (integer = GPU nearest of
//    the variable canvas; sai/eagle = software 2x into a canvas*2 render2),
//  * viewscreen relayout from the live canvas dims (screen::relayout_views
//    and viewscreen::resize(whatmode)),
//  * the SDL_EVENT_WINDOW_RESIZED bridge hook,
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
        E_Screen->set_world_scale(og::WorldScaleSetting{}, win_w, win_h);
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

TEST(CanvasScale, scale1_splits_a_window_sized_world_canvas_and_presents)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // scale=1: world canvas = window dims (width rounded down to a multiple
    // of 4, both axes clamped to the classic 320x200 minimum).
    const og::WorldCanvasDims want = og::compute_world_canvas_dims(
        restore.win_w, restore.win_h, {og::WorldScaleMode::Integer, 1});
    if (want.w == 320 && want.h == 200)
        GTEST_SKIP() << "test window is classic-sized; scale=1 cannot split";
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 1},
                              restore.win_w, restore.win_h);
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

TEST(CanvasScale, scale4_clamps_small_windows_to_the_classic_canvas)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // A 640x400 window at scale=4 would be 160x100 — clamped to 320x200,
    // which re-shares the UI surface (the byte-identity dims).
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 4}, 640, 400);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(ui, E_Screen->render) << "clamped-to-default canvas re-shares the UI pair";
    test_screen()->buffer_to_screen(0, 0, 320, 200); // present smoke

    // A big window at scale=4 does produce a big canvas: 1920x1200 -> 480x300.
    E_Screen->apply_world_scale_for_window(1920, 1200);
    EXPECT_EQ(480, E_Screen->world_w());
    EXPECT_EQ(300, E_Screen->world_h());
    test_screen()->buffer_to_screen(0, 0, 480, 300); // present smoke at scale=4
}

TEST(CanvasScale, sai_scale_doubles_into_render2_at_canvas_dims)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // graphics/scale=sai with a 1280x800 window: canvas 640x400, world
    // presents through the software 2x scaler into a 1280x800 render2.
    E_Screen->set_world_scale({og::WorldScaleMode::Sai, 2}, 1280, 800);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());
    EXPECT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
    ASSERT_TRUE(E_Screen->render2);
    EXPECT_EQ(1280, E_Screen->render2->w);
    EXPECT_EQ(800, E_Screen->render2->h);

    // The UI canvas is untouched by the scale key: it presents through the
    // legacy graphics/render engine (NoZoom in the test cfg).
    E_Screen->set_active_canvas(CanvasTarget::UI);
    EXPECT_EQ(320, E_Screen->render->w);
    E_Screen->swap(0, 0, 320, 200);

    // The eagle scaler picks the Eagle engine over the same halved canvas.
    E_Screen->set_world_scale({og::WorldScaleMode::Eagle, 2}, 1280, 800);
    EXPECT_EQ(RenderEngine::Eagle, E_Screen->world_engine());
    EXPECT_EQ(640, E_Screen->world_w());
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
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

    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 1}, 640, 400);
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

TEST(CanvasScale, window_resize_event_retracks_the_canvas)
{
    ASSERT_TRUE(E_Screen);
    screen* s = test_screen();
    ASSERT_TRUE(s->viewob[0]);
    ClassicCanvasRestore restore;

    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 2}, 640, 400);
    ASSERT_EQ(320, E_Screen->world_w()); // 640/2 == the classic dims

    SDL_Event ev{};
    ev.type = SDL_EVENT_WINDOW_RESIZED;
    ev.window.data1 = 1280;
    ev.window.data2 = 800;
    handle_window_event(ev);

    // canvas = 1280/2 x 800/2, viewscreens re-laid-out to the new dims.
    EXPECT_EQ(640, E_Screen->world_w());
    EXPECT_EQ(400, E_Screen->world_h());
    const og::view_layout::ViewLayout expect =
        og::view_layout::compute_view_layout(
            s->numviews, s->viewob[0]->mynum, s->viewob[0]->prefs[PREF_VIEW],
            640, 400);
    ASSERT_TRUE(expect.applies);
    EXPECT_EQ(expect.w, s->viewob[0]->xview);
    EXPECT_EQ(expect.h, s->viewob[0]->yview);

    // Under Legacy the same event leaves the classic canvas alone.
    E_Screen->set_world_scale(og::WorldScaleSetting{}, 640, 400);
    ASSERT_EQ(320, E_Screen->world_w());
    handle_window_event(ev);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
}

TEST(CanvasScale, apply_world_scale_from_cfg_reads_the_scale_key)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const std::string old_value = cfg.get_setting("graphics", "scale");

    cfg.apply_setting("graphics", "scale", "1");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Integer, E_Screen->world_scale().mode);
    const og::WorldCanvasDims want = og::compute_world_canvas_dims(
        restore.win_w, restore.win_h, {og::WorldScaleMode::Integer, 1});
    EXPECT_EQ(want.w, E_Screen->world_w());
    EXPECT_EQ(want.h, E_Screen->world_h());

    // "off" (and any pre-existing cfg without the key) restores Legacy.
    cfg.apply_setting("graphics", "scale", "off");
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Legacy, E_Screen->world_scale().mode);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());

    cfg.apply_setting("graphics", "scale", old_value);
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
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 1}, 640, 400);
    ASSERT_EQ(640, E_Screen->world_w());
    ASSERT_EQ(400, E_Screen->world_h());

    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = 320; // window center
    ev.motion.y = 200;

    // Gameplay routing (world canvas active): the window->canvas mapping
    // divides by the world dims, so the center lands at 320,200 of 640x400.
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
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 1}, 640, 400);
    ASSERT_EQ(640, s->world_canvas_w());

    trace_clear();
    const char old_end = s->world().end;
    s->world().end = 1;
    (void)level_editor();
    s->world().end = old_end;

    ASSERT_TRUE(trace_contains("canvas", "editor_pin_classic 320x200"))
        << "editor must pin the classic canvas while its chrome assumes 320x200";
    ASSERT_TRUE(trace_contains("canvas", "editor_unpin 640x400"))
        << "editor exit must restore the scale-derived canvas";
    EXPECT_EQ(640, s->world_canvas_w());
    EXPECT_EQ(CanvasTarget::UI, s->active_canvas());
    EXPECT_FALSE(E_Screen->world_canvas_pinned_classic());

    // While pinned, apply_world_scale_for_window must not fight the pin.
    E_Screen->set_world_canvas_pinned_classic(true);
    EXPECT_EQ(320, E_Screen->world_w());
    E_Screen->apply_world_scale_for_window(1280, 800);
    EXPECT_EQ(320, E_Screen->world_w()) << "pin must win over resize";
    E_Screen->set_world_canvas_pinned_classic(false);
}

// --- Adversarial pokes -------------------------------------------------------

TEST(CanvasScale, hostile_window_dims_clamp_and_round)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();

    // A huge 4000x2000 window at scale=8: canvas 500x250 — a REAL surface +
    // texture pair at non-round dims, plotted and present-smoked in the far
    // corner (the offset math must use the 500px stride).
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 8}, 4000, 2000);
    EXPECT_EQ(500, E_Screen->world_w());
    EXPECT_EQ(250, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::World);
    s->pointb(499, 249, 47);
    int idx = 0;
    s->get_pixel(499, 249, &idx);
    EXPECT_EQ(47, idx);
    s->buffer_to_screen(0, 0, 500, 250);

    // scale=8 on the default 640x400 window: 80x50 clamps UP to the classic
    // 320x200 minimum and re-shares the UI pair — a coarser-than-requested
    // stretch, never a sub-classic canvas.
    E_Screen->apply_world_scale_for_window(640, 400);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(ui, E_Screen->render) << "clamped canvas must re-share the UI pair";

    // Odd window dims at scale=2: width rounds down to a multiple of 4
    // (1279/2 = 639 -> 636), height floors (799/2 = 399).
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 2}, 1279, 799);
    EXPECT_EQ(636, E_Screen->world_w());
    EXPECT_EQ(399, E_Screen->world_h());
    EXPECT_EQ(0, E_Screen->world_w() % 4);
    s->buffer_to_screen(0, 0, 636, 399);

    // The sai scaler over the same odd window: render2 doubles the CANVAS
    // (1272x798) — no hardcoded 640x400 scratch — recreating any
    // differently-sized scratch a previous mode left behind.
    E_Screen->set_world_scale({og::WorldScaleMode::Sai, 2}, 1279, 799);
    ASSERT_EQ(636, E_Screen->world_w());
    ASSERT_EQ(og::WorldScaleMode::Sai, E_Screen->world_scale().mode);
    E_Screen->swap(0, 0, 636, 399);
    ASSERT_TRUE(E_Screen->render2);
    EXPECT_EQ(1272, E_Screen->render2->w);
    EXPECT_EQ(798, E_Screen->render2->h);
}

TEST(CanvasScale, garbage_scale_value_falls_back_to_legacy_and_warns)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    const std::string old_value = cfg.get_setting("graphics", "scale");

    // A present-but-garbage value must fall back to the byte-identical
    // Legacy canvas AND say so (the user asked for something).
    cfg.apply_setting("graphics", "scale", "2x-bogus");
    trace_clear();
    apply_world_scale_from_cfg();
    EXPECT_EQ(og::WorldScaleMode::Legacy, E_Screen->world_scale().mode);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
    EXPECT_TRUE(trace_contains("canvas", "world_scale unrecognized value=2x-bogus"))
        << "a garbage graphics/scale value must warn, not silently vanish";

    // The documented explicit "off" and the empty string an absent key reads
    // back as are NOT garbage: no warning.
    cfg.apply_setting("graphics", "scale", "off");
    trace_clear();
    apply_world_scale_from_cfg();
    EXPECT_FALSE(trace_contains("canvas", "world_scale unrecognized"));
    cfg.apply_setting("graphics", "scale", "");
    trace_clear();
    apply_world_scale_from_cfg();
    EXPECT_FALSE(trace_contains("canvas", "world_scale unrecognized"));

    cfg.apply_setting("graphics", "scale", old_value);
    apply_world_scale_from_cfg();
}

TEST(CanvasScale, fadeblack_matches_the_grown_world_canvas)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;
    screen* s = test_screen();

    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 1}, 640, 400);
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

TEST(CanvasScale, allocation_failure_falls_back_to_the_shared_classic_pair)
{
    ASSERT_TRUE(E_Screen);
    ClassicCanvasRestore restore;

    // A width whose 32bpp pitch overflows int: SDL rejects the surface and
    // set_world_canvas_size must fall back to the shared classic pair — a
    // usable (if coarse) renderer — never a null render target.
    E_Screen->set_world_canvas_size(1 << 30, 64);
    EXPECT_EQ(320, E_Screen->world_w());
    EXPECT_EQ(200, E_Screen->world_h());
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_Surface* ui = E_Screen->render;
    ASSERT_TRUE(ui);
    E_Screen->set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(ui, E_Screen->render)
        << "the failed split must re-share the UI pair";
    test_screen()->buffer_to_screen(0, 0, 320, 200); // present smoke
}

TEST(CanvasScale, mouse_clicks_land_on_the_active_canvas_at_two_scales)
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

    // scale=2: world canvas 640x400; a click at the window center lands at
    // world (320,200) — half the window coordinate.
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 2}, 1280, 800);
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

    // scale=1: world canvas 1280x800; the same click lands at world (640,400).
    E_Screen->set_world_scale({og::WorldScaleMode::Integer, 1}, 1280, 800);
    ASSERT_EQ(1280, E_Screen->world_w());
    clear_events();
    SDL_PushEvent(&down);
    get_input_events(POLL);
    EXPECT_EQ(1, mymouse.left);
    EXPECT_NEAR(640.0f, mymouse.x, 1.0f);
    EXPECT_NEAR(400.0f, mymouse.y, 1.0f);
    clear_events();
    SDL_PushEvent(&up);
    get_input_events(POLL);
    EXPECT_EQ(0, mymouse.left);

    // The UI canvas is scale-blind at BOTH scales: the same click maps to the
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
