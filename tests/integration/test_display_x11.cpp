// The real-display x11 lane (#329): DISPLAY-screen behaviour that only a real
// video driver can show. The offscreen driver every other Linux group runs on
// enumerates no fullscreen mode, never switches one and is not "x11", so the
// mode selector, the exclusive mode switch and the multi-display X11 guard in
// src/platform/sdl/video_sdl.cpp are dark there.
//
// These tests run ONLY through scripts/ci/run_x11_display_lane.sh, which
// starts Xvfb with a known topology:
//   X11OneScreen   one 1920x1080 screen, XRandR modes 1280x720, 1024x768,
//                  800x600, 640x480 and 320x240 added, openbox running
//                  (ctest entry og_test_display_x11_one, label x11-one);
//   X11TwoScreens  two X screens (1920x1080, 1280x1024), no window manager
//                  (og_test_display_x11_two, label x11-two).
// The ctest entries set SDL_VIDEODRIVER=x11 and OG_X11_EXPECT_DISPLAYS. Every
// test first asserts the driver and the display count, so a lane started on
// the wrong server FAILS; nothing here skips.

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gparser.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{

using Size = std::pair<int, int>;

constexpr std::array<const char*, 5> kGraphicsKeys = {
    "fullscreen", "width", "height", "windowed_width", "windowed_height"};

// The screen run A starts Xvfb with, and the modes it adds. Kept as constants
// here, not read back from SDL, so the oracle cannot drift with the product.
constexpr Size kDesktop{1920, 1080};

int expected_display_count()
{
    const char* const value = std::getenv("OG_X11_EXPECT_DISPLAYS");
    if (value == nullptr)
        return 0;
    return std::atoi(value);
}

int sdl_display_count()
{
    int count = 0;
    SDL_DisplayID* const displays = SDL_GetDisplays(&count);
    if (displays == nullptr)
        return -1;
    SDL_free(displays);
    return count;
}

SDL_DisplayID window_display()
{
    const SDL_DisplayID display = SDL_GetDisplayForWindow(E_Screen->window);
    return display != 0 ? display : SDL_GetPrimaryDisplay();
}

Size current_mode_pixels(SDL_DisplayID display)
{
    const SDL_DisplayMode* const mode = SDL_GetCurrentDisplayMode(display);
    return mode != nullptr ? og::platform::display_mode_pixel_size(*mode)
                           : Size{0, 0};
}

Size window_size()
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(E_Screen->window, &w, &h);
    return {w, h};
}

bool window_is_fullscreen()
{
    return (SDL_GetWindowFlags(E_Screen->window) & SDL_WINDOW_FULLSCREEN) != 0;
}

std::string graphics(const char* key)
{
    return cfg.get_setting("graphics", key);
}

void request(const std::string& fullscreen, Size size)
{
    cfg.apply_setting("graphics", "fullscreen", fullscreen);
    cfg.apply_setting("graphics", "width", std::to_string(size.first));
    cfg.apply_setting("graphics", "height", std::to_string(size.second));
}

class X11DisplayLane : public ::testing::Test
{
protected:
    void SetUp() override
    {
        screen_ = og::runtime::current_session != nullptr
            ? og::runtime::current_session->myscreen_
            : nullptr;
        ASSERT_NE(nullptr, screen_);
        ASSERT_NE(nullptr, E_Screen);
        ASSERT_NE(nullptr, E_Screen->window);
        const char* const driver = SDL_GetCurrentVideoDriver();
        ASSERT_STREQ("x11", driver)
            << "the x11 lane must run on SDL's x11 driver; run it through "
               "scripts/ci/run_x11_display_lane.sh";
        expected_displays_ = expected_display_count();
        ASSERT_EQ(expected_displays_, sdl_display_count())
            << "OG_X11_EXPECT_DISPLAYS names the X topology the lane script "
               "started; SDL must see exactly that many displays";

        for (std::size_t i = 0; i < kGraphicsKeys.size(); ++i)
            saved_cfg_[i] = graphics(kGraphicsKeys[i]);
        initial_window_ = window_size();
        ASSERT_GT(initial_window_.first, 0);
        ASSERT_GT(initial_window_.second, 0);
        ASSERT_FALSE(window_is_fullscreen())
            << "every test starts from a Windowed window";
    }

    // Applies "off" until the product reports Windowed in cfg and SDL agrees
    // (a request the previous test left pending is settled by the first apply
    // and may need a second), then hands back the cfg keys.
    void TearDown() override
    {
        if (screen_ == nullptr || E_Screen == nullptr || E_Screen->window == nullptr)
            return;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            request("off", initial_window_);
            cfg.apply_setting("graphics", "windowed_width",
                              std::to_string(initial_window_.first));
            cfg.apply_setting("graphics", "windowed_height",
                              std::to_string(initial_window_.second));
            screen_->apply_display_settings_from_cfg();
            if (graphics("fullscreen") == "off" && !window_is_fullscreen() &&
                window_size() == initial_window_)
                break;
        }
        for (std::size_t i = 0; i < kGraphicsKeys.size(); ++i)
            cfg.apply_setting("graphics", kGraphicsKeys[i], saved_cfg_[i]);
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_EVENT_DISPLAY_FIRST, SDL_EVENT_WINDOW_LAST);
    }

    screen* screen_ = nullptr;
    int expected_displays_ = 0;
    std::array<std::string, kGraphicsKeys.size()> saved_cfg_{};
    Size initial_window_{0, 0};
};

class X11OneScreen : public X11DisplayLane
{
protected:
    void SetUp() override
    {
        X11DisplayLane::SetUp();
        if (HasFatalFailure())
            return;
        ASSERT_EQ(1, expected_displays_)
            << "X11OneScreen runs only in the one-screen lane (x11-one)";
        ASSERT_EQ(kDesktop, current_mode_pixels(window_display()))
            << "each test starts on the 1920x1080 desktop mode";
        // With a window manager, a fullscreen request for a window that is
        // not mapped yet times out and SDL reverts the mode switch. Map and
        // settle the window first.
        ASSERT_TRUE(SDL_ShowWindow(E_Screen->window));
        ASSERT_TRUE(SDL_SyncWindow(E_Screen->window))
            << "openbox must acknowledge the window before any request";
    }

    // Enter Exclusive through the product's apply path with a request the
    // display has no exact mode for: the product must choose the closest
    // real mode at least that large (1280x720) and persist THAT.
    void enter_exclusive_1280x720()
    {
        request("exclusive", {1200, 700});
        screen_->apply_display_settings_from_cfg();
    }
};

class X11TwoScreens : public X11DisplayLane
{
protected:
    void SetUp() override
    {
        X11DisplayLane::SetUp();
        if (HasFatalFailure())
            return;
        ASSERT_EQ(2, expected_displays_)
            << "X11TwoScreens runs only in the two-screen lane (x11-two)";
    }
};

// ---------------------------------------------------------------- run A

// display_resolutions() offers exactly the display's real modes at or above
// the classic 640x400 window, largest first. 320x240 is a real XRandR mode on
// this screen and must be filtered out.
TEST_F(X11OneScreen, selector_lists_exactly_the_xrandr_modes_at_or_above_640x400)
{
    const std::vector<Size> expected{
        {1920, 1080}, {1280, 720}, {1024, 768}, {800, 600}, {640, 480}};
    EXPECT_EQ(expected, screen_->display_resolutions())
        << "the selector must list every XRandR mode at or above 640x400, "
           "largest first, and nothing else";

    // Positive control for the filtered negative: SDL itself does enumerate
    // the 320x240 mode, so its absence above is the product's filter.
    int count = 0;
    SDL_DisplayMode** const modes =
        SDL_GetFullscreenDisplayModes(window_display(), &count);
    ASSERT_NE(nullptr, modes);
    bool sdl_has_320x240 = false;
    for (int i = 0; i < count; ++i)
    {
        if (og::platform::display_mode_pixel_size(*modes[i]) == Size{320, 240})
            sdl_has_320x240 = true;
    }
    SDL_free(modes);
    EXPECT_TRUE(sdl_has_320x240)
        << "SDL must enumerate the 320x240 mode the lane added";
}

// An Exclusive request really switches the display to the closest real mode
// at least as large as the request, and cfg persists the CONFIRMED physical
// mode (1280x720), not the request (1200x700).
TEST_F(X11OneScreen, exclusive_apply_switches_the_real_mode_and_persists_it)
{
    enter_exclusive_1280x720();

    EXPECT_EQ((Size{1280, 720}), current_mode_pixels(window_display()))
        << "the display must really be switched to 1280x720";
    EXPECT_TRUE(window_is_fullscreen())
        << "the window must be fullscreen after the acknowledged request";
    const SDL_DisplayMode* const attached =
        SDL_GetWindowFullscreenMode(E_Screen->window);
    ASSERT_NE(nullptr, attached)
        << "Exclusive attaches a real fullscreen mode to the window";
    EXPECT_EQ((Size{1280, 720}), og::platform::display_mode_pixel_size(*attached));
    EXPECT_EQ("exclusive", graphics("fullscreen"));
    EXPECT_EQ("1280", graphics("width"))
        << "cfg persists the confirmed physical mode, not the 1200x700 request";
    EXPECT_EQ("720", graphics("height"))
        << "cfg persists the confirmed physical mode, not the 1200x700 request";
}

// Leaving Exclusive with cfg still holding the exclusive pixels (the normal
// selector step) restores the desktop mode and the logical window size
// remembered from before fullscreen, not a 1280x720 window.
TEST_F(X11OneScreen, leaving_exclusive_restores_desktop_and_remembered_window)
{
    const Size before = window_size();
    ASSERT_NE((Size{1280, 720}), before)
        << "the pre-fullscreen window must differ from the exclusive mode, or "
           "the restore rule is unobservable";

    enter_exclusive_1280x720();
    ASSERT_EQ((Size{1280, 720}), current_mode_pixels(window_display()))
        << "precondition: Exclusive 1280x720 is active";
    ASSERT_EQ("1280", graphics("width"));
    ASSERT_EQ("720", graphics("height"));

    cfg.apply_setting("graphics", "fullscreen", "off");
    screen_->apply_display_settings_from_cfg();

    EXPECT_EQ(kDesktop, current_mode_pixels(window_display()))
        << "leaving Exclusive must restore the desktop mode";
    EXPECT_FALSE(window_is_fullscreen());
    EXPECT_EQ(before, window_size())
        << "the window must return to its remembered pre-fullscreen size";
    EXPECT_EQ("off", graphics("fullscreen"));
    EXPECT_EQ(std::to_string(before.first), graphics("width"));
    EXPECT_EQ(std::to_string(before.second), graphics("height"));
}

// ---------------------------------------------------------------- run B

// On multi-display x11 the selector offers no Exclusive mode at all, although
// SDL enumerates usable modes for the same display.
TEST_F(X11TwoScreens, selector_hides_exclusive_on_multi_display_x11)
{
    EXPECT_EQ(std::vector<Size>{}, screen_->display_resolutions())
        << "Exclusive must not be offered on multi-display X11";

    // Positive control: the display does have a mode the selector would
    // otherwise list (its own desktop mode, at least 640x400).
    const SDL_DisplayID display = window_display();
    const Size desktop = current_mode_pixels(display);
    ASSERT_GE(desktop.first, 640);
    ASSERT_GE(desktop.second, 400);
    int count = 0;
    SDL_DisplayMode** const modes = SDL_GetFullscreenDisplayModes(display, &count);
    ASSERT_NE(nullptr, modes);
    bool sdl_lists_desktop = false;
    for (int i = 0; i < count; ++i)
    {
        if (og::platform::display_mode_pixel_size(*modes[i]) == desktop)
            sdl_lists_desktop = true;
    }
    SDL_free(modes);
    EXPECT_TRUE(sdl_lists_desktop)
        << "SDL must enumerate the display's desktop mode, so the empty "
           "selector above is the product's guard";
}

// A saved Exclusive cfg is applied as Borderless on multi-display x11: no
// real fullscreen mode is attached to the window and cfg is normalised.
TEST_F(X11TwoScreens, saved_exclusive_is_applied_as_borderless)
{
    request("exclusive", current_mode_pixels(window_display()));
    screen_->apply_display_settings_from_cfg();

    EXPECT_EQ(nullptr, SDL_GetWindowFullscreenMode(E_Screen->window))
        << "no exclusive mode may be attached on multi-display X11";
    // The Borderless request it became is not acknowledged without a window
    // manager either, so cfg holds the confirmed Windowed state.
    EXPECT_EQ("off", graphics("fullscreen"))
        << "cfg must never describe an Exclusive state on multi-display X11";
}

// With no window manager the borderless request is never acknowledged:
// SDL_SyncWindow times out. cfg must then describe only the last CONFIRMED
// state (Windowed, "off"), never the unconfirmed request.
TEST_F(X11TwoScreens, a_timed_out_request_persists_only_the_confirmed_snapshot)
{
    request("borderless", initial_window_);
    screen_->apply_display_settings_from_cfg();

    EXPECT_EQ("off", graphics("fullscreen"))
        << "an unacknowledged request must not be persisted";
    EXPECT_EQ(std::to_string(initial_window_.first), graphics("width"));
    EXPECT_EQ(std::to_string(initial_window_.second), graphics("height"));

    // The next apply finds that request still pending and settles it with a
    // second barrier before issuing its own, which no window manager
    // acknowledges either: cfg still describes only the confirmed Windowed
    // state.
    request("borderless", initial_window_);
    screen_->apply_display_settings_from_cfg();
    EXPECT_EQ("off", graphics("fullscreen"))
        << "a second unacknowledged request must not be persisted either";
    EXPECT_EQ(std::to_string(initial_window_.first), graphics("width"));
    EXPECT_EQ(std::to_string(initial_window_.second), graphics("height"));
}

} // namespace
