#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <openglad/platform/game_context.h>
#include <openglad/interface/screen.h>
#include <array>
#include <filesystem>
#include <memory>
#include <span>


// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct SurfaceDeleter
{
    void operator()(SDL_Surface* s) const
    {
        if (s)
            SDL_DestroySurface(s);
    }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

static SurfacePtr make_surface(int w, int h)
{
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    return SurfacePtr(s);
}

static void cleanup_screenshots()
{
    // save_screenshot() uses a static counter and writes to CWD.
    // Keep tests idempotent and avoid accumulating files across runs.
    std::error_code ec;
    for (const auto& p : std::filesystem::directory_iterator(std::filesystem::current_path(), ec))
    {
        if (ec)
            break;
        const auto name = p.path().filename().string();
        if (name.rfind("screenshot", 0) == 0 && (p.path().extension() == ".png" || p.path().extension() == ".bmp"))
            std::filesystem::remove(p.path(), ec);
    }
}
} // namespace

TEST(VideoModesMore, video_putbuffer_surface_clipping_and_blit)
{
    SurfacePtr surf = make_surface(32, 32);
    ASSERT_TRUE(surf != nullptr) << "SDL surface created";
    if (!surf)
        return;

    // Fill with a non-zero pattern so the blit does something.
    SDL_FillSurfaceRect(surf.get(), nullptr, SDL_MapRGB(surf->format, 10, 20, 30));

    // Early-out: tile outside clipping region.
    og::runtime::current_session->myscreen_->putbuffer_surface(500, 500, 16, 16, 0, 0, 319, 199, surf.get());

    // Clip left/top.
    og::runtime::current_session->myscreen_->putbuffer_surface(-5, -5, 16, 16, 0, 0, 319, 199, surf.get());

    // Clip right/bottom.
    og::runtime::current_session->myscreen_->putbuffer_surface(310, 190, 32, 32, 0, 0, 319, 199, surf.get());

    // No clipping.
    og::runtime::current_session->myscreen_->putbuffer_surface(10, 10, 16, 16, 0, 0, 319, 199, surf.get());
}

TEST(VideoModesMore, video_clipping_and_accel_surface_edge_paths)
{
    std::array<unsigned char, 16 * 16> pixels{};
    pixels.fill(42);
    pixels[0] = 0;
    auto span = std::span<const unsigned char>(pixels.data(), pixels.size());

    og::runtime::current_session->myscreen_->fastbox(10, 10, 4, 4, 12, 0);

    og::runtime::current_session->myscreen_->putbuffer(-4, -4, 16, 16, 0, 0, 319, 199, span);
    og::runtime::current_session->myscreen_->putbuffer(400, 400, 16, 16, 0, 0, 319, 199, span);
    og::runtime::current_session->myscreen_->putbuffer_alpha(-4, -4, 16, 16, 0, 0, 319, 199, span, 128);
    og::runtime::current_session->myscreen_->putbuffer_alpha(400, 400, 16, 16, 0, 0, 319, 199, span, 128);
    og::runtime::current_session->myscreen_->putbuffer_alpha(10, 10, 0, 16, 0, 0, 319, 199, span, 128);

    EXPECT_EQ(nullptr, og::runtime::current_session->myscreen_->create_accel_surface(span, 0, 16));
    EXPECT_EQ(nullptr, og::runtime::current_session->myscreen_->create_accel_surface(span.first(3), 4, 4));
    void* surface = og::runtime::current_session->myscreen_->create_accel_surface(span, 16, 16);
    ASSERT_NE(nullptr, surface);
    og::runtime::current_session->myscreen_->destroy_accel_surface(surface);
    og::runtime::current_session->myscreen_->destroy_accel_surface(nullptr);

    og::runtime::current_session->myscreen_->walkputbuffer_flash(400, 400, 16, 16, 0, 0, 319, 199, span, 40);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(-4, -4, 16, 16, 0, 0, 319, 199, span, 40);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(310, 190, 16, 16, 0, 0, 319, 199, span, 40);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(10, 10, 0, 16, 0, 0, 319, 199, span, 40);

    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(400, 400, 16, 16, 0, 0, 319, 199, span, 40, 128);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(-4, -4, 16, 16, 0, 0, 319, 199, span, 40, 128);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(310, 190, 16, 16, 0, 0, 319, 199, span, 40, 128);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(10, 10, 0, 16, 0, 0, 319, 199, span, 40, 128);
}


TEST(VideoModesMore, video_walkputbuffer_modes_invisible_outline_phantom)
{
    // A small sprite with edges and a transparent interior to exercise outline
    // and transparency checks. Use some >247 indices to hit team color remap.
    std::array<unsigned char, 8 * 8> sprite{};
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            const bool edge = (x == 0 || y == 0 || x == 7 || y == 7);
            sprite[y * 8 + x] = edge ? static_cast<unsigned char>(250) : static_cast<unsigned char>(0);
        }
    }
    auto span = std::span<const unsigned char>(sprite.data(), sprite.size());

    // Ensure get_pixel() has something to read (phantom modes sample from the screen).
    og::runtime::current_session->myscreen_->clearbuffer();
    og::runtime::current_session->myscreen_->putdata(0, 0, 8, 8, span);
    og::runtime::current_session->myscreen_->swap();

    IRandom* old_rng = ctx().rng;

    // INVISIBLE_MODE: cover both the "skip draw" and "draw" paths deterministically.
    FixedRandom rng0(0);
    ctx().rng = &rng0; // rng(1) -> 0
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(INVISIBLE_MODE), /*invisibility*/ 1,
                            /*outline*/ 7, /*shifttype*/ 0);

    FixedRandom rng9(9);
    ctx().rng = &rng9; // rng(10) -> 9 (> 8)
    og::runtime::current_session->myscreen_->walkputbuffer(60, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(INVISIBLE_MODE), /*invisibility*/ 10,
                            /*outline*/ 7, /*shifttype*/ 0);

    // OUTLINE_MODE: border-only drawing.
    ctx().rng = &rng0;
    og::runtime::current_session->myscreen_->walkputbuffer(70, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(OUTLINE_MODE), /*invisibility*/ 0,
                            /*outline*/ 7, /*shifttype*/ 0);

    // PHANTOM_MODE: exercise shift type branches.
    og::runtime::current_session->myscreen_->walkputbuffer(80, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_LEFT));
    og::runtime::current_session->myscreen_->walkputbuffer(90, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_RIGHT));
    og::runtime::current_session->myscreen_->walkputbuffer(100, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_RIGHT_RANDOM));
    og::runtime::current_session->myscreen_->walkputbuffer(110, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_RANDOM));
    og::runtime::current_session->myscreen_->walkputbuffer(120, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_LIGHTER));
    og::runtime::current_session->myscreen_->walkputbuffer(130, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_DARKER));
    og::runtime::current_session->myscreen_->walkputbuffer(140, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_BLOCKY));

    ctx().rng = old_rng;
}


TEST(VideoModesMore, video_save_screenshot_smoke_and_cleanup)
{
    cleanup_screenshots();

    // Default engine path (NoZoom) should save using E_Screen->render.
    (void)og::runtime::current_session->myscreen_->save_screenshot();

    cleanup_screenshots();
}
