#include <openglad/interface/screen.h>
#include <openglad/platform/video_sdl.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct SurfaceDeleter {
    void operator()(SDL_Surface* s) const { if (s) SDL_DestroySurface(s); }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

static SurfacePtr make_surface(int w, int h)
{
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    return SurfacePtr(s);
}

static SurfacePtr make_surface_masks(int w, int h, int depth,
                                     Uint32 rmask, Uint32 gmask,
                                     Uint32 bmask, Uint32 amask)
{
    SDL_Surface* s = SDL_CreateSurface(
        w, h, SDL_GetPixelFormatForMasks(depth, rmask, gmask, bmask, amask));
    return SurfacePtr(s);
}
} // namespace

TEST(VideoFade, video_fadeblack_out_then_in_tracks_the_window)
{
    // In TESTING, FadeBetween skips animation but still exercises surface
    // checks + blits. The order is the ownership rule's: a presented frame
    // fades OUT, then the buffer fades IN over the black that left; a fade-in
    // first would be the "fade-in without a fade-out" violation the listener
    // fails tests on.
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(scr->window_is_black())
        << "the test boundary leaves the window black, as an exit fade does";
    scr->buffer_to_screen(0, 0, 320, 200);
    ASSERT_FALSE(scr->window_is_black()) << "a present clears the flag";
    ASSERT_EQ(1, scr->fadeblack(false)) << "fade-to-black completes";
    ASSERT_TRUE(scr->window_is_black()) << "a completed fade-out blackens the window";
    ASSERT_EQ(0, scr->fadeblack(false))
        << "a second fade-out is a no-op on a black window (returns 0, no fade)";
    ASSERT_TRUE(scr->window_is_black());
    ASSERT_EQ(1, scr->fadeblack(true)) << "fade-from-black completes";
    ASSERT_FALSE(scr->window_is_black()) << "the fade-in's present clears the flag";
}

// The "never presented" invariant holds a present to the rect it DECLARED: a
// partial-rect present (a pressed button face, the help scroller's dialog
// rect) vouches for nothing outside it, so a draw outside that rect is still
// unpresented at the next fade-out. (The nearest present path happens to
// upload the whole surface; the SAI/Eagle path refreshes only the rect of
// its scaled scratch. The declared rect is the honest lower bound.)
TEST(VideoFade, video_fadeblack_out_after_a_draw_outside_the_presented_rect_is_a_violation)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_EQ(0, og::video_testing::g_fade_violations.load());

    // A full-frame present: everything on the window matches the buffer.
    scr->clearbuffer();
    scr->fastbox(0, 0, 320, 200, 40);
    scr->buffer_to_screen(0, 0, 320, 200);
    ASSERT_FALSE(scr->window_is_black());

    // Control: a draw inside a rect, presented by that rect, then a fade-out
    // — the fade reads exactly what the window shows. No violation.
    scr->fastbox(100, 50, 40, 20, 90);
    scr->buffer_to_screen(100, 50, 40, 20);
    ASSERT_EQ(1, scr->fadeblack(false)) << "fade-out completes";
    EXPECT_EQ(0, og::video_testing::g_fade_violations.load())
        << "a draw covered by its own partial present is presented";
    ASSERT_TRUE(scr->window_is_black());

    // Bring a frame back the honest way (fade-in from the buffer), then the
    // shape under test: a partial present of one rect, a draw OUTSIDE it,
    // and a fade-out. The window never showed that draw.
    ASSERT_EQ(1, scr->fadeblack(true));
    scr->fastbox(100, 50, 40, 20, 120);
    scr->buffer_to_screen(100, 50, 40, 20);
    scr->fastbox(200, 150, 30, 30, 200);  // outside the presented rect
    ASSERT_EQ(1, scr->fadeblack(false))
        << "production still fades — the violation is a TESTING report";
    EXPECT_EQ(1, og::video_testing::g_fade_violations.load())
        << "a draw outside every present's rect is a fade from a frame the "
           "window never showed";
    const std::vector<std::string> violations =
        og::video_testing::fade_violation_messages();
    ASSERT_EQ(1u, violations.size());
    EXPECT_NE(std::string::npos,
              violations.front().find("fade-out from a frame that was never presented"))
        << violations.front();
    EXPECT_NE(std::string::npos,
              violations.front().find("x 200..229, y 150..179 of 320x200"))
        << "the report names the culprit draw's bounding box: "
        << violations.front();
    // Triggered on purpose; the listener would otherwise fail this test.
    og::video_testing::reset_fade_violations();
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

    EXPECT_EQ(
        0, og::runtime::current_session->myscreen_->fade_between(
               old_ok.get(), new_ok.get(), nullptr));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(nullptr, nullptr, dest.get()));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(nullptr, new_ok.get(), dest.get()));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_ok.get(), nullptr, dest.get()));

    SurfacePtr new_bad_h = make_surface(320, 201);
    ASSERT_TRUE(new_bad_h != nullptr) << "height mismatch surface created";
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_bad_h.get(), dest.get()));

    std::vector<Uint32> narrow_pixels(320u * 200u);
    SurfacePtr narrow_same_pitch(SDL_CreateSurfaceFrom(
        319, 200, SDL_PIXELFORMAT_XRGB8888,
        narrow_pixels.data(), 320 * 4));
    ASSERT_NE(nullptr, narrow_same_pitch)
        << "same-pitch width mismatch surface created";
    ASSERT_EQ(old_ok->pitch, narrow_same_pitch->pitch);
    EXPECT_EQ(0,
              og::runtime::current_session->myscreen_->fade_between(
                  old_ok.get(), narrow_same_pitch.get(), dest.get()));

    SurfacePtr new_rgba = make_surface_masks(
        320, 200, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    ASSERT_TRUE(new_rgba != nullptr) << "R mask mismatch surface created";
    ASSERT_NE(old_ok->format, new_rgba->format);
    ASSERT_EQ(old_ok->pitch, new_rgba->pitch);
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_rgba.get(), dest.get()));
    EXPECT_EQ(0, og::runtime::current_session->myscreen_->fade_between(old_ok.get(), new_ok.get(), new_rgba.get()));

    SurfacePtr old_24 = make_surface_masks(320, 200, 24, 0x0000ff, 0x00ff00, 0xff0000, 0);
    SurfacePtr new_24 = make_surface_masks(320, 200, 24, 0x0000ff, 0x00ff00, 0xff0000, 0);
    SurfacePtr dest_24 = make_surface_masks(320, 200, 24, 0x0000ff, 0x00ff00, 0xff0000, 0);
    ASSERT_TRUE(old_24 != nullptr && new_24 != nullptr && dest_24 != nullptr)
        << "24-bit surfaces created";
    EXPECT_EQ(0,
              og::runtime::current_session->myscreen_->fade_between(
                  old_24.get(), new_24.get(), dest_24.get()));
}

TEST(VideoFade, video_fadebetween_honors_surface_lock_requirements)
{
    SurfacePtr dest = make_surface(320, 200);
    SurfacePtr old_rle = make_surface(320, 200);
    SurfacePtr new_ok = make_surface(320, 200);
    ASSERT_NE(nullptr, dest);
    ASSERT_NE(nullptr, old_rle);
    ASSERT_NE(nullptr, new_ok);
    SurfacePtr rle_blit_target = make_surface(320, 200);
    ASSERT_NE(nullptr, rle_blit_target);
    ASSERT_TRUE(SDL_SetSurfaceColorKey(
        old_rle.get(), true, SDL_MapSurfaceRGB(old_rle.get(), 255, 0, 255)));
    ASSERT_TRUE(SDL_SetSurfaceRLE(old_rle.get(), true));
    ASSERT_TRUE(SDL_BlitSurface(
        old_rle.get(), nullptr, rle_blit_target.get(), nullptr));
    ASSERT_TRUE(SDL_MUSTLOCK(old_rle.get()));

    SDL_FillSurfaceRect(
        new_ok.get(), nullptr,
        SDL_MapSurfaceRGB(new_ok.get(), 70, 80, 90));

    SurfacePtr wrong_dest = make_surface(319, 200);
    ASSERT_NE(nullptr, wrong_dest);
    ASSERT_TRUE(SDL_FillSurfaceRect(
        wrong_dest.get(), nullptr,
        SDL_MapSurfaceRGB(wrong_dest.get(), 4, 5, 6)));
    const std::vector<Uint8> wrong_dest_before(
        static_cast<const Uint8*>(wrong_dest->pixels),
        static_cast<const Uint8*>(wrong_dest->pixels) +
            wrong_dest->pitch * wrong_dest->h);
    EXPECT_EQ(0,
              og::runtime::current_session->myscreen_->fade_between(
                  old_rle.get(), new_ok.get(), wrong_dest.get()))
        << "destination dimensions must match even when both inputs match";
    EXPECT_EQ(0u, old_rle->flags & SDL_SURFACE_LOCKED)
        << "a later precondition failure must release the acquired old lock";
    EXPECT_EQ(
        0, std::memcmp(wrong_dest_before.data(), wrong_dest->pixels,
                       wrong_dest_before.size()))
        << "a rejected destination must remain byte-identical";

    EXPECT_EQ(1,
              og::runtime::current_session->myscreen_->fade_between(
                  old_rle.get(), new_ok.get(), dest.get()));
    EXPECT_EQ(0u, old_rle->flags & SDL_SURFACE_LOCKED)
        << "FadeBetween must release a lock it acquired";

    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(old_rle.get(), 10, 10, &red, &green, &blue, nullptr));
    EXPECT_EQ(70, red);
    EXPECT_EQ(80, green);
    EXPECT_EQ(90, blue);

    SurfacePtr old_ok = make_surface(320, 200);
    SurfacePtr new_rle = make_surface(320, 200);
    ASSERT_NE(nullptr, old_ok);
    ASSERT_NE(nullptr, new_rle);
    ASSERT_TRUE(SDL_SetSurfaceColorKey(
        new_rle.get(), true, SDL_MapSurfaceRGB(new_rle.get(), 255, 0, 255)));
    ASSERT_TRUE(SDL_SetSurfaceRLE(new_rle.get(), true));
    ASSERT_TRUE(SDL_BlitSurface(
        new_rle.get(), nullptr, rle_blit_target.get(), nullptr));
    ASSERT_TRUE(SDL_MUSTLOCK(new_rle.get()));
    EXPECT_EQ(0,
              og::runtime::current_session->myscreen_->fade_between(
                  old_ok.get(), new_rle.get(), dest.get()));

    SurfacePtr dest_rle = make_surface(320, 200);
    ASSERT_NE(nullptr, dest_rle);
    ASSERT_TRUE(SDL_SetSurfaceColorKey(
        dest_rle.get(), true,
        SDL_MapSurfaceRGB(dest_rle.get(), 255, 0, 255)));
    ASSERT_TRUE(SDL_SetSurfaceRLE(dest_rle.get(), true));
    ASSERT_TRUE(SDL_BlitSurface(
        dest_rle.get(), nullptr, rle_blit_target.get(), nullptr));
    ASSERT_TRUE(SDL_MUSTLOCK(dest_rle.get()));
    EXPECT_EQ(0,
              og::runtime::current_session->myscreen_->fade_between(
                  old_ok.get(), new_ok.get(), dest_rle.get()));
    EXPECT_EQ(0u, dest_rle->flags & SDL_SURFACE_LOCKED)
        << "rejected destination remains unlocked";
}


TEST(VideoFade, video_fadebetween_success_path_smoke)
{
    SurfacePtr dest = make_surface(320, 200);
    SurfacePtr old_ok = make_surface(320, 200);
    SurfacePtr new_ok = make_surface(320, 200);
    ASSERT_TRUE(dest != nullptr && old_ok != nullptr && new_ok != nullptr) << "surfaces created";
    if (!(dest && old_ok && new_ok))
        return;

    SDL_FillSurfaceRect(old_ok.get(), nullptr, SDL_MapSurfaceRGB(old_ok.get(), 10, 20, 30));
    SDL_FillSurfaceRect(new_ok.get(), nullptr, SDL_MapSurfaceRGB(new_ok.get(), 200, 180, 160));

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
