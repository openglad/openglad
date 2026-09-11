#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <openglad/interface/ui/menu_screen_spec.h>

#include "test_input_helpers.h"
#include "test_interact.h"

// Teeth for tests/test_interact.h's wait_for_menu_frames().
//
// The helper is the counts-not-clocks replacement for the cargo-culted
// `SDL_Delay(750)  // fadeblack` settle: it returns as soon as
// run_menu_screen has completed N more frames. The property that makes it a
// WAIT rather than a sleep is that it can fail — and the only way to prove
// that here is to ask for a frame that can never arrive, because nothing
// outside run_menu_screen bumps the counter.
//
// This case is deliberately run with no picker loop open: the counter is
// frozen, so the helper must spend its (short) ceiling and report false.
// Plant `return true;` at the top of wait_for_menu_frames and this goes red.
TEST(MenuFrameWait, wait_for_menu_frames_times_out_when_no_menu_loop_is_running)
{
    const std::uint64_t before = og::ui::menu_screen_testing_completed_frames();

    const Uint64 started = SDL_GetTicks();
    const bool settled = wait_for_menu_frames(1, 300);
    const Uint64 spent = SDL_GetTicks() - started;

    ASSERT_FALSE(settled)
        << "no menu loop is running, so no frame can ever complete: the "
           "settle must report the miss instead of pretending it landed";
    ASSERT_EQ(before, og::ui::menu_screen_testing_completed_frames())
        << "nothing outside run_menu_screen may advance the frame counter";
    EXPECT_GE(static_cast<int>(spent), 300)
        << "the helper must actually spend its ceiling before giving up";
}

// A settle of zero frames is already satisfied: the helper never blocks for a
// frame it did not ask for. (This is the boundary the converted flows lean on
// when a screen is known to be live already.)
TEST(MenuFrameWait, wait_for_menu_frames_of_zero_returns_immediately)
{
    ASSERT_TRUE(wait_for_menu_frames(0, 300))
        << "zero frames is a no-op wait, not a 300 ms sleep";
}

// interact() reports whether the id was there to click. The blind-click
// failure class (a flow that fires at a screen that has not settled, gets a
// stderr warning nobody reads, then times out twenty lines later) is exactly
// what this return value converts into a named failure at the click.
TEST(MenuFrameWait, interact_reports_a_missing_button_instead_of_only_warning)
{
    clear_allbuttons();
    ASSERT_FALSE(interact("a_button_that_does_not_exist"))
        << "a click at an id that is not in allbuttons[] must report false";
}
