#include <gtest/gtest.h>

#include <openglad/interface/device_seats.h>

#include <cstring>

// #249: the local-seat cap is pure logic so the whole matrix is pinned here
// without SDL, a session or a lobby. Every seat door in the game asks
// og::input::local_seat_cap(), which is this function fed the live device
// class and joystick count.

TEST(DeviceSeats, desktop_keeps_the_build_cap)
{
    EXPECT_EQ(4, og::input::max_local_seats(false, 0, 4));
    EXPECT_EQ(4, og::input::max_local_seats(false, 2, 4));
    // A desktop's cap does not move with its hardware: four seats share one
    // keyboard by design.
    EXPECT_EQ(4, og::input::max_local_seats(false, -1, 4));
    EXPECT_EQ(2, og::input::max_local_seats(false, 0, 2));
}

TEST(DeviceSeats, phone_offers_one_built_in_seat)
{
    EXPECT_EQ(1, og::input::max_local_seats(true, 0, 4));
}

TEST(DeviceSeats, phone_earns_a_seat_per_attached_pad)
{
    EXPECT_EQ(2, og::input::max_local_seats(true, 1, 4));
    EXPECT_EQ(3, og::input::max_local_seats(true, 2, 4));
    EXPECT_EQ(4, og::input::max_local_seats(true, 3, 4));
}

TEST(DeviceSeats, phone_cap_never_passes_the_build_limit)
{
    EXPECT_EQ(4, og::input::max_local_seats(true, 4, 4));
    EXPECT_EQ(4, og::input::max_local_seats(true, 99, 4));
    EXPECT_EQ(1, og::input::max_local_seats(true, 9, 1));
}

TEST(DeviceSeats, negative_device_count_still_leaves_the_screen_seat)
{
    // joystick_device_count() cannot go negative, but the rule must not hand
    // back a cap below the seat the touchscreen always provides.
    EXPECT_EQ(1, og::input::max_local_seats(true, -1, 4));
    EXPECT_EQ(1, og::input::max_local_seats(true, -99, 4));
}

TEST(DeviceSeats, cap_is_a_compile_time_rule)
{
    static_assert(og::input::max_local_seats(false, 0, 4) == 4);
    static_assert(og::input::max_local_seats(true, 0, 4) == 1);
    static_assert(og::input::max_local_seats(true, 2, 4) == 3);
    SUCCEED();
}

TEST(DeviceSeats, screen_owner_token_covers_the_keyboard_seat_only)
{
    // A phone seat the player cycled onto a real pad keeps naming that pad.
    EXPECT_TRUE(og::input::seat_owner_is_screen(true, false, 0));
    EXPECT_FALSE(og::input::seat_owner_is_screen(true, true, 0));
    EXPECT_FALSE(og::input::seat_owner_is_screen(false, false, 0));
    EXPECT_FALSE(og::input::seat_owner_is_screen(false, true, 0));
    // Only local slot 0 is the screen: the overlay drives player 0 alone, so
    // a phone's later seats (opened by pads) never claim it.
    EXPECT_FALSE(og::input::seat_owner_is_screen(true, false, 1));
    EXPECT_FALSE(og::input::seat_owner_is_screen(true, true, 1));
    EXPECT_FALSE(og::input::seat_owner_is_screen(true, false, 3));
}

TEST(DeviceSeats, screen_owner_label_fits_the_seat_card_face)
{
    // The seat card face is eleven characters including "P{n} " and the
    // load-bearing trailing pad (picker_sdl_defs.h), and a two-digit lobby-
    // wide P# eats one more, so the token stays inside four.
    EXPECT_EQ(4u, std::strlen(og::input::kScreenSeatOwnerLabel));
    EXPECT_STREQ("SCRN", og::input::kScreenSeatOwnerLabel);
}
