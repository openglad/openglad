#include <openglad/interface/screen.h>
#include "test_framework.h"

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
} // namespace

void test_video_fadeblack_smoke_in_and_out()
{
    // In TESTING, FadeBetween skips animation but still exercises surface checks + blits.
    int r1 = og::runtime::current_session->myscreen_->fadeblack(true);
    int r2 = og::runtime::current_session->myscreen_->fadeblack(false);
    TEST_ASSERT(r1 >= 0 && r2 >= 0, "fadeblack should return non-negative status");
}
REGISTER_TEST(test_video_fadeblack_smoke_in_and_out);

void test_video_fadebetween_precondition_failures_are_handled()
{
    SurfacePtr dest = make_surface(320, 200);
    TEST_ASSERT(dest != nullptr, "dest surface created");
    if (!dest)
        return;

    SurfacePtr old_ok = make_surface(320, 200);
    SurfacePtr new_bad_w = make_surface(321, 200);
    TEST_ASSERT(old_ok != nullptr && new_bad_w != nullptr, "test surfaces created");
    if (!(old_ok && new_bad_w))
        return;

    // Width mismatch should fail gracefully (return 0).
    int r = og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_bad_w.get(), dest.get());
    TEST_ASSERT_EQ(0, r, "FadeBetween should fail on width mismatch");
}
REGISTER_TEST(test_video_fadebetween_precondition_failures_are_handled);

void test_video_fadebetween_success_path_smoke()
{
    SurfacePtr dest = make_surface(320, 200);
    SurfacePtr old_ok = make_surface(320, 200);
    SurfacePtr new_ok = make_surface(320, 200);
    TEST_ASSERT(dest != nullptr && old_ok != nullptr && new_ok != nullptr, "surfaces created");
    if (!(dest && old_ok && new_ok))
        return;

    SDL_FillRect(old_ok.get(), nullptr, SDL_MapRGB(old_ok->format, 10, 20, 30));
    SDL_FillRect(new_ok.get(), nullptr, SDL_MapRGB(new_ok->format, 200, 180, 160));

    int r = og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_ok.get(), dest.get());
    TEST_ASSERT(r != 0, "FadeBetween should succeed for matching 32bpp surfaces");
}
REGISTER_TEST(test_video_fadebetween_success_path_smoke);

void test_video_fadebetween24_smoke()
{
    SurfacePtr s = make_surface(320, 200);
    TEST_ASSERT(s != nullptr, "surface created");
    if (!s)
        return;

    const Uint32 size = static_cast<Uint32>(s->pitch) * static_cast<Uint32>(s->h);
    std::vector<Uint8> from(size, 0);
    std::vector<Uint8> to(size, 255);

    og::runtime::current_session->myscreen_->fade_between24(s.get(), from.data(), to.data(), 1);
}
REGISTER_TEST(test_video_fadebetween24_smoke);
