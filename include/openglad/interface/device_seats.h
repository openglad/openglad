#pragma once

// A single-seat device (a phone) offers exactly one built-in seat: the
// touchscreen. There is no second set of on-screen controls and no keyboard
// behind them, so every additional local seat has to arrive as a real
// attached device. Tablets and desktops are never single-seat — the caller's
// build limit (MAX_PLAYERS) governs them alone.
//
// This header is pure logic (no SDL includes) so headless unit tests can
// cover the whole cap matrix. The live device class is a runtime flag
// (InputHardwareState::single_seat_device, set from the web shell through
// openglad_web_set_single_seat_device); og::input::local_seat_cap() feeds it
// here, and is the ONE rule every seat door consults.

namespace og::input
{

// Seats this machine may fill locally. hard_cap is the caller's build limit.
constexpr int max_local_seats(bool single_seat_device, int joystick_count,
                              int hard_cap)
{
    if (!single_seat_device)
        return hard_cap;
    const int seats = 1 + (joystick_count > 0 ? joystick_count : 0);
    return seats < hard_cap ? seats : hard_cap;
}

// Owner token a seat card shows when the on-screen controls drive the seat:
// there the touchscreen IS the controller, so naming a keyboard mapping
// would name keys the device does not have.
inline constexpr const char* kScreenSeatOwnerLabel = "SCRN";

// A local seat on a single-seat device runs on the on-screen controls unless
// the player has cycled it onto a real joystick.
constexpr bool seat_owner_is_screen(bool single_seat_device,
                                    bool selection_is_joystick)
{
    return single_seat_device && !selection_is_joystick;
}

} // namespace og::input
