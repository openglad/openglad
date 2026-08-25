#include <openglad/interface/screen.h>
#include <openglad/platform/sai2x.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// picker_dialogs.cpp symbols
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
void picker_testing_set_force_real_dialogs(bool enabled);

namespace
{
struct ViewportGuard
{
    float ow = 0.0f;
    float oh = 0.0f;
    float ovw = 0.0f;
    float ovh = 0.0f;
    float ox = 0.0f;
    float oy = 0.0f;

    ViewportGuard()
    {
        ow = og::runtime::current_session->window_w_;
        oh = og::runtime::current_session->window_h_;
        ovw = og::runtime::current_session->viewport_w_;
        ovh = og::runtime::current_session->viewport_h_;
        ox = og::runtime::current_session->viewport_offset_x_;
        oy = og::runtime::current_session->viewport_offset_y_;

        og::runtime::current_session->window_w_ = 320;
        og::runtime::current_session->window_h_ = 200;
        og::runtime::current_session->viewport_offset_x_ = 0;
        og::runtime::current_session->viewport_offset_y_ = 0;
        og::runtime::current_session->viewport_w_ = 320;
        og::runtime::current_session->viewport_h_ = 200;
    }

    ~ViewportGuard()
    {
        og::runtime::current_session->window_w_ = ow;
        og::runtime::current_session->window_h_ = oh;
        og::runtime::current_session->viewport_w_ = ovw;
        og::runtime::current_session->viewport_h_ = ovh;
        og::runtime::current_session->viewport_offset_x_ = ox;
        og::runtime::current_session->viewport_offset_y_ = oy;
    }
};

struct RealDialogsGuard
{
    RealDialogsGuard() { picker_testing_set_force_real_dialogs(true); }
    ~RealDialogsGuard() { picker_testing_set_force_real_dialogs(false); }
};

struct CanvasRoutingGuard
{
    int zoom_steps = E_Screen->world_zoom_steps();
    og::WorldScaleMode smoothing = E_Screen->world_scale().mode;
    CanvasTarget target = E_Screen->active_canvas();

    ~CanvasRoutingGuard()
    {
        E_Screen->set_world_zoom(zoom_steps, smoothing);
        E_Screen->set_active_canvas(target);
    }
};

struct DialogThreadState
{
    bool started = false;
    bool finished = false;
    int x = 0;
    int y = 0;
};

static int dialog_click_injector(void* data)
{
    og::runtime::ensure_thread_session();
    DialogThreadState* st = static_cast<DialogThreadState*>(data);
    st->started = true;

    SDL_Delay(100);
    inject_click(st->x, st->y, 20);

    st->finished = true;
    return 0;
}
} // namespace

TEST(PickerDialogsReal, picker_dialogs_yes_or_no_queued_override_paths)
{
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    picker_testing_yes_or_no_queue_push(false);

    ASSERT_TRUE(yes_or_no_prompt("Delete", "Proceed with deletion?", false)) << "queued true override should force yes result";
    ASSERT_TRUE(!yes_or_no_prompt("Delete", "Proceed with deletion?", true)) << "queued false override should force no result";

    picker_testing_yes_or_no_queue_clear();
}


TEST(PickerDialogsReal, picker_dialogs_no_or_yes_default_paths)
{
    ASSERT_TRUE(no_or_yes_prompt("Reset", "Reset current progress?", true)) << "no_or_yes_prompt should return default=true in test mode";
    ASSERT_TRUE(!no_or_yes_prompt("Reset", "Reset current progress?", false)) << "no_or_yes_prompt should return default=false in test mode";
}


TEST(PickerDialogsReal, picker_dialogs_popup_dialog_testmode_noop)
{
    vbutton* buttons_before = og::runtime::current_session->localbuttons_;
    popup_dialog("Information", "This is a test popup\\nwith two lines.");
    ASSERT_EQ(buttons_before, og::runtime::current_session->localbuttons_)
        << "test-mode popup should return before installing dialog buttons";
}


TEST(PickerDialogsReal, picker_dialogs_yes_or_no_real_dialog_click_yes)
{
    ViewportGuard viewport_guard;
    RealDialogsGuard real_dialogs_guard;

    DialogThreadState st{false, false, 95, 140};
    SDL_Thread* thread = SDL_CreateThread(dialog_click_injector, "picker_yes_dialog", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create yes dialog injector";

    const bool accepted = yes_or_no_prompt("Delete", "Proceed with deletion?\nThis cannot be undone.", false);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started && st.finished) << "yes dialog injector should run";
    ASSERT_TRUE(accepted) << "YES click should accept the dialog";
}

TEST(PickerDialogsReal, in_game_dialog_uses_fixed_ui_and_preserves_zoomed_world)
{
    ViewportGuard viewport_guard;
    RealDialogsGuard real_dialogs_guard;
    CanvasRoutingGuard canvas_guard;

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai);
    E_Screen->set_active_canvas(CanvasTarget::World);
    const int zoomed_world_w = E_Screen->world_w();
    const int zoomed_world_h = E_Screen->world_h();
    ASSERT_EQ(zoomed_world_w, E_Screen->render->w);
    ASSERT_EQ(zoomed_world_h, E_Screen->render->h);
    ASSERT_GT(zoomed_world_w, kUiCanvasW)
        << "the test requires a split world canvas";
    constexpr Uint32 kWorldPixel = 0x00123456u;
    SDL_FillSurfaceRect(E_Screen->render, nullptr, kWorldPixel);

    DialogThreadState st{false, false, 95, 140};
    SDL_Thread* thread = SDL_CreateThread(dialog_click_injector, "picker_zoom_dialog", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create zoom dialog injector";

    const bool accepted = yes_or_no_prompt("Delete", "Keep the world canvas intact?", false);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started && st.finished) << "zoom dialog injector should run";
    EXPECT_TRUE(accepted);
    EXPECT_EQ(CanvasTarget::World, E_Screen->active_canvas());
    EXPECT_EQ(zoomed_world_w, E_Screen->render->w);
    EXPECT_EQ(zoomed_world_h, E_Screen->render->h);
    const auto* world_pixels = reinterpret_cast<const Uint32*>(E_Screen->render->pixels);
    EXPECT_EQ(kWorldPixel, world_pixels[60 + 90 * (E_Screen->render->pitch / 4)])
        << "modal darkening and chrome must not modify the split world surface";
}


TEST(PickerDialogsReal, picker_dialogs_no_or_yes_real_dialog_click_no)
{
    ViewportGuard viewport_guard;
    RealDialogsGuard real_dialogs_guard;

    DialogThreadState st{false, false, 95, 140};
    SDL_Thread* thread = SDL_CreateThread(dialog_click_injector, "picker_no_dialog", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create no dialog injector";

    const bool accepted = no_or_yes_prompt("Reset", "Reset current progress?\nThis cannot be undone.", true);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started && st.finished) << "no dialog injector should run";
    ASSERT_TRUE(!accepted) << "NO click should reject the dialog";
}


TEST(PickerDialogsReal, picker_dialogs_popup_dialog_real_click_ok)
{
    ViewportGuard viewport_guard;
    RealDialogsGuard real_dialogs_guard;

    DialogThreadState st{false, false, 160, 140};
    SDL_Thread* thread = SDL_CreateThread(dialog_click_injector, "picker_popup_dialog", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create popup dialog injector";

    popup_dialog("Information", "This is a real dialog\nwith two lines.");

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started && st.finished) << "popup dialog injector should run";
}

// --- #259: the dialog header band -----------------------------------------
// Geometry mirrors compute_dialog_bounds (src/interface/ui/picker_dialogs.cpp)
// and sdl_video::draw_dialog: never hard-code the y, so a bounds change moves
// the probe with the box instead of silently reading empty grey.
namespace
{
constexpr int kDialogPixPerChar = 6;   // compute_dialog_bounds' PIX_PER_CHAR
constexpr int kDialogTitlePitch = 9;   // its title pitch

struct DialogBandGeometry
{
    int x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    int header_top = 0, header_bottom = 0;   // draw_dialog's header field
    int body_top = 0, body_bottom = 0;       // draw_dialog's message field
};

DialogBandGeometry dialog_band_geometry(const char* title,
                                        const std::vector<std::string>& lines)
{
    int w = static_cast<int>(strlen(title)) * kDialogTitlePitch;
    for (const std::string& line : lines)
        w = std::max(w, static_cast<int>(line.size()) * kDialogPixPerChar);
    const int h = 30 + 10 * static_cast<int>(lines.size());

    DialogBandGeometry g;
    g.x1 = 160 - w / 2 - 12;
    g.x2 = 160 + w / 2 + 12;
    g.y1 = 80 - h / 2;
    g.y2 = 80 + h / 2;
    g.header_top = g.y1 + 4;
    g.header_bottom = g.y1 + 18;
    g.body_top = g.y1 + 20;
    g.body_bottom = g.y2 - 4;
    return g;
}

// The header font (pix/textbig.png) is a single-index pixie: every lit glyph
// pixel carries palette entry 40, the pure red that draw_dialog asks for, and
// the header bar under it is the neutral grey of palette entry 12. So "the
// title is on screen" is exactly "red pixels inside the header field".
std::size_t count_red(int x1, int x2, int y1, int y2)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    std::size_t red = 0;
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
        {
            Uint8 r = 0, g = 0, b = 0;
            scr->get_pixel(x, y, &r, &g, &b);
            if (r > 150 && g + 60 < r && b + 60 < r)
                ++red;
        }
    return red;
}

std::size_t count_blue(int x1, int x2, int y1, int y2)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    std::size_t blue = 0;
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
        {
            Uint8 r = 0, g = 0, b = 0;
            scr->get_pixel(x, y, &r, &g, &b);
            if (b > 100 && r + 30 < b && g + 30 < b)
                ++blue;
        }
    return blue;
}
} // namespace

TEST(PickerDialogsReal, dialog_header_paints_inside_its_own_field)
{
    ViewportGuard viewport_guard;
    CanvasRoutingGuard canvas_guard;
    E_Screen->set_active_canvas(CanvasTarget::UI);
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0);

    const char* title = "Victory!";
    const std::vector<std::string> lines{"You have won the battle!"};
    const DialogBandGeometry g = dialog_band_geometry(title, lines);

    screen* const scr = og::runtime::current_session->myscreen_;
    const Sint32 left = scr->draw_dialog(g.x1, g.y1, g.x2, g.y2, title);
    scr->text_normal.write_xy(left + 9, g.body_top + 4, lines[0].c_str(),
                              static_cast<unsigned char>(DARK_BLUE), 1);

    const std::size_t header_red =
        count_red(g.x1 + 4, g.x2 - 4, g.header_top, g.header_bottom);
    const std::size_t body_blue =
        count_blue(g.x1 + 4, g.x2 - 4, g.body_top, g.body_bottom);

    EXPECT_GT(header_red, 40u)
        << "#259: the dialog title must paint inside the header field";
    EXPECT_GT(body_blue, 40u)
        << "control: the message body must paint too (if this is 0 the probe "
           "is measuring the wrong band, not a header regression)";
}

TEST(PickerDialogsReal, dialog_header_survives_stale_cached_font_geometry)
{
    ViewportGuard viewport_guard;
    CanvasRoutingGuard canvas_guard;
    E_Screen->set_active_canvas(CanvasTarget::UI);

    // The #259 mechanism: a text built before its shared font pixie could be
    // read keeps a 0x0 glyph box, while a later text loads the pixie for real.
    // The blit then clips to nothing and the header comes out blank over an
    // intact body. Geometry is re-read at use, so a stale cache cannot do it.
    text& big = og::runtime::current_session->myscreen_->text_big;
    const short saved_x = big.sizex;
    const short saved_y = big.sizey;
    ASSERT_TRUE(big.letters != nullptr && big.letters->valid())
        << "the header font must be loaded for this test to mean anything";
    big.sizex = 0;
    big.sizey = 0;

    const char* title = "Victory!";
    const std::vector<std::string> lines{"You have won the battle!"};
    const DialogBandGeometry g = dialog_band_geometry(title, lines);

    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0);
    og::runtime::current_session->myscreen_->draw_dialog(g.x1, g.y1, g.x2, g.y2,
                                                        title);
    const std::size_t header_red =
        count_red(g.x1 + 4, g.x2 - 4, g.header_top, g.header_bottom);

    big.sizex = saved_x;
    big.sizey = saved_y;

    EXPECT_GT(header_red, 40u)
        << "#259: a stale cached glyph box must not blank the dialog header";
    EXPECT_EQ(saved_x, big.sizex) << "the live geometry must match the pixie";
}
