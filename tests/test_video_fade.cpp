#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL.h>

#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct SurfaceDeleter {
    void operator()(SDL_Surface* s) const { if (s) SDL_FreeSurface(s); }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

static SurfacePtr make_surface(int w, int h)
{
    SDL_Surface* s = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, 0, 0, 0, 0);
    return SurfacePtr(s);
}

static SurfacePtr make_surface_masks(int w, int h, int depth,
                                     Uint32 rmask, Uint32 gmask,
                                     Uint32 bmask, Uint32 amask)
{
    SDL_Surface* s = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, depth,
                                          rmask, gmask, bmask, amask);
    return SurfacePtr(s);
}
} // namespace

TEST(VideoFade, video_fadeblack_smoke_in_and_out)
{
    // In TESTING, FadeBetween skips animation but still exercises surface checks + blits.
    int r1 = og::runtime::current_session->myscreen_->fadeblack(true);
    int r2 = og::runtime::current_session->myscreen_->fadeblack(false);
    ASSERT_TRUE(r1 >= 0 && r2 >= 0) << "fadeblack should return non-negative status";
}


TEST(VideoFade, video_fadebetween_precondition_failures_are_handled)
{
    SurfacePtr dest = make_surface(320, 200);
    ASSERT_TRUE(dest != nullptr) << "dest surface created";
    if (!dest)
        return;

    SurfacePtr old_ok = make_surface(320, 200);
    SurfacePtr new_bad_w = make_surface(321, 200);
    ASSERT_TRUE(old_ok != nullptr && new_bad_w != nullptr) << "test surfaces created";
    if (!(old_ok && new_bad_w))
        return;

    // Width mismatch should fail gracefully (return 0).
    int r = og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_bad_w.get(), dest.get());
    ASSERT_EQ(0, r) << "FadeBetween should fail on width mismatch";
}

TEST(VideoFade, video_fadebetween_null_and_format_preconditions_are_handled)
{
    SurfacePtr dest = make_surface(320, 200);
    SurfacePtr old_ok = make_surface(320, 200);
    SurfacePtr new_ok = make_surface(320, 200);
    ASSERT_TRUE(dest != nullptr && old_ok != nullptr && new_ok != nullptr) << "base surfaces created";
    if (!(dest && old_ok && new_ok))
        return;

    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(nullptr, nullptr, dest.get()));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(nullptr, new_ok.get(), dest.get()));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_ok.get(), nullptr, dest.get()));

    SurfacePtr new_bad_h = make_surface(320, 201);
    ASSERT_TRUE(new_bad_h != nullptr) << "height mismatch surface created";
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_bad_h.get(), dest.get()));

    SurfacePtr old_bad_w = make_surface(321, 200);
    ASSERT_TRUE(old_bad_w != nullptr) << "old width mismatch surface created";
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_bad_w.get(), nullptr, dest.get()));

    SurfacePtr new_rgba = make_surface_masks(
        320, 200, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    ASSERT_TRUE(new_rgba != nullptr) << "R mask mismatch surface created";
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_rgba.get(), dest.get()));

    SurfacePtr old_24 = make_surface_masks(320, 200, 24, 0x0000ff, 0x00ff00, 0xff0000, 0);
    SurfacePtr new_24 = make_surface_masks(320, 200, 24, 0x0000ff, 0x00ff00, 0xff0000, 0);
    ASSERT_TRUE(old_24 != nullptr && new_24 != nullptr) << "24-bit surfaces created";
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_24.get(), new_24.get(), dest.get()));
}


TEST(VideoFade, video_fadebetween_success_path_smoke)
{
    SurfacePtr dest = make_surface(320, 200);
    SurfacePtr old_ok = make_surface(320, 200);
    SurfacePtr new_ok = make_surface(320, 200);
    ASSERT_TRUE(dest != nullptr && old_ok != nullptr && new_ok != nullptr) << "surfaces created";
    if (!(dest && old_ok && new_ok))
        return;

    SDL_FillRect(old_ok.get(), nullptr, SDL_MapRGB(old_ok->format, 10, 20, 30));
    SDL_FillRect(new_ok.get(), nullptr, SDL_MapRGB(new_ok->format, 200, 180, 160));

    int r = og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_ok.get(), dest.get());
    ASSERT_TRUE(r != 0) << "FadeBetween should succeed for matching 32bpp surfaces";
}


TEST(VideoFade, video_fadebetween24_smoke)
{
    SurfacePtr s = make_surface(320, 200);
    ASSERT_TRUE(s != nullptr) << "surface created";
    if (!s)
        return;

    const Uint32 size = static_cast<Uint32>(s->pitch) * static_cast<Uint32>(s->h);
    std::vector<Uint8> from(size, 0);
    std::vector<Uint8> to(size, 255);

    og::runtime::current_session->myscreen_->fade_between24(s.get(), from.data(), to.data(), 1);
}
