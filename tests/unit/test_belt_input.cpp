// New Ability Select: the key fold (og::sim::belt::fold_keys and friends,
// include/openglad/gameplay/belt_input.h). Pure: a hand-built belt, no
// family, no session.

#include <openglad/gameplay/belt_input.h>

#include <gtest/gtest.h>

using og::sim::belt::Entry;
using og::sim::belt::Fold;
using og::sim::belt::fold_belt_keys;
using og::sim::belt::fold_classic_keys;
using og::sim::belt::fold_keys;
using og::sim::belt::kCastNothing;
using og::sim::belt::Keys;
using og::sim::belt::Latch;
using og::sim::belt::Layout;
using og::sim::belt::Resolved;
using og::sim::belt::SlotState;

namespace
{

Resolved castable(std::uint8_t slot, bool alt, const char* name)
{
    Resolved r;
    r.entry = Entry{slot, alt};
    r.state = SlotState::Castable;
    r.name = name;
    r.opens_at = 1;
    return r;
}

// A mage-like belt: 1 TELEPORT, 2 WARP SPACE, 3 FREEZE TIME (locked, L7),
// S1 TELEPORT MARKER, S2 empty, S3 HEARTBURST.
Layout test_belt()
{
    Layout belt{};
    belt[0] = castable(1, false, "TELEPORT");
    belt[1] = castable(2, false, "WARP SPACE");
    belt[2] = castable(3, false, "FREEZE TIME");
    belt[2].state = SlotState::Locked;
    belt[2].opens_at = 7;
    belt[3] = castable(1, true, "TELEPORT MARKER");
    // belt[4] stays Empty.
    belt[5] = castable(5, false, "HEARTBURST");
    return belt;
}

// The byte each slot of test_belt() sends.
constexpr std::uint8_t kTeleport = 0x01;
constexpr std::uint8_t kWarp = 0x02;
constexpr std::uint8_t kFreeze = 0x03;
constexpr std::uint8_t kMarker = 0x09;
constexpr std::uint8_t kHeartburst = 0x05;

// One sample: `held` is the SPEC keys down now (1..3 bits), `before` the
// keys down the sample before (for the press edges).
Keys sample(unsigned held, unsigned before, bool shift = false)
{
    Keys keys;
    for (int k = 0; k < 3; ++k)
    {
        keys.held[k] = (held & (1u << k)) != 0;
        keys.pressed[k] = keys.held[k] && (before & (1u << k)) == 0;
    }
    keys.shift_held = shift;
    return keys;
}

constexpr unsigned kSpec1 = 1u;
constexpr unsigned kSpec2 = 2u;
constexpr unsigned kSpec3 = 4u;

} // namespace

TEST(BeltInput, press_edge_sets_special_pressed_and_latches_the_byte)
{
    const Layout belt = test_belt();
    Latch latch;

    Fold f = fold_belt_keys(sample(kSpec2, 0), belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_TRUE(f.special_held);
    EXPECT_EQ(kWarp, f.belt_cast);
    EXPECT_EQ(2, latch.active_key);

    // Held: no new press, the same byte.
    f = fold_belt_keys(sample(kSpec2, kSpec2), belt, latch);
    EXPECT_FALSE(f.special_pressed);
    EXPECT_TRUE(f.special_held);
    EXPECT_EQ(kWarp, f.belt_cast);

    // Released: nothing goes out, the latch is clear.
    f = fold_belt_keys(sample(0, kSpec2), belt, latch);
    EXPECT_EQ(Fold{}, f);
    EXPECT_EQ(Latch{}, latch);
}

TEST(BeltInput, shift_at_press_selects_the_layer_and_a_shift_change_mid_hold_does_not)
{
    const Layout belt = test_belt();
    Latch latch;

    // SHIFTER held at the press: the SHIFTER row.
    Fold f = fold_belt_keys(sample(kSpec1, 0, /*shift=*/true), belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_EQ(kMarker, f.belt_cast);
    // SHIFTER let go mid-hold: the running entry stays TELEPORT MARKER.
    f = fold_belt_keys(sample(kSpec1, kSpec1, /*shift=*/false), belt, latch);
    EXPECT_FALSE(f.special_pressed);
    EXPECT_TRUE(f.special_held);
    EXPECT_EQ(kMarker, f.belt_cast);
    f = fold_belt_keys(sample(0, kSpec1), belt, latch);

    // And the other way: plain at the press, SHIFTER pressed mid-hold.
    f = fold_belt_keys(sample(kSpec1, 0, false), belt, latch);
    EXPECT_EQ(kTeleport, f.belt_cast);
    f = fold_belt_keys(sample(kSpec1, kSpec1, true), belt, latch);
    EXPECT_FALSE(f.special_pressed);
    EXPECT_EQ(kTeleport, f.belt_cast);
}

TEST(BeltInput, second_key_during_a_hold_is_a_press_not_a_hold)
{
    const Layout belt = test_belt();
    Latch latch;

    (void)fold_belt_keys(sample(kSpec1, 0), belt, latch);
    (void)fold_belt_keys(sample(kSpec1, kSpec1), belt, latch);
    // SPEC 2 goes down while SPEC 1 is still held (#228: one tap, one cast).
    Fold f = fold_belt_keys(sample(kSpec1 | kSpec2, kSpec1), belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_TRUE(f.special_held);
    EXPECT_EQ(kWarp, f.belt_cast);
    EXPECT_EQ(2, latch.active_key);
}

TEST(BeltInput, two_keys_down_in_one_sample_take_the_highest)
{
    const Layout belt = test_belt();
    Latch latch;
    const Fold f = fold_belt_keys(sample(kSpec1 | kSpec3, 0), belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_EQ(kFreeze, f.belt_cast);
    EXPECT_EQ(3, latch.active_key);
}

TEST(BeltInput, release_of_the_active_key_promotes_the_other_held_key_as_a_press)
{
    const Layout belt = test_belt();
    Latch latch;

    (void)fold_belt_keys(sample(kSpec1, 0), belt, latch);
    (void)fold_belt_keys(sample(kSpec1 | kSpec2, kSpec1), belt, latch);
    ASSERT_EQ(2, latch.active_key);

    // SPEC 2 (running) let go, SPEC 1 still held with SHIFTER now down: SPEC 1
    // takes over as a fresh press, its entry read now (the SHIFTER row).
    Fold f = fold_belt_keys(sample(kSpec1, kSpec1 | kSpec2, true), belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_TRUE(f.special_held);
    EXPECT_EQ(kMarker, f.belt_cast);
    EXPECT_EQ(1, latch.active_key);

    f = fold_belt_keys(sample(kSpec1, kSpec1, true), belt, latch);
    EXPECT_FALSE(f.special_pressed);
    EXPECT_EQ(kMarker, f.belt_cast);
}

TEST(BeltInput, a_key_held_before_the_belt_saw_it_is_not_a_press)
{
    // A SPEC key already down when the seat starts driving a walker (no
    // edge) never casts on its own.
    const Layout belt = test_belt();
    Latch latch;
    const Fold f = fold_belt_keys(sample(kSpec1, kSpec1), belt, latch);
    EXPECT_EQ(Fold{}, f);
    EXPECT_EQ(Latch{}, latch);
}

TEST(BeltInput, a_tap_inside_one_sample_still_sends_its_press_and_byte)
{
    // An edge with the key already up again (a client that reports presses
    // separately from holds): the press and byte go out once, no hold.
    const Layout belt = test_belt();
    Latch latch;
    Keys keys;
    keys.pressed[2] = true;
    Fold f = fold_belt_keys(keys, belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_FALSE(f.special_held);
    EXPECT_EQ(kFreeze, f.belt_cast);
    f = fold_belt_keys(Keys{}, belt, latch);
    EXPECT_EQ(Fold{}, f);
}

TEST(BeltInput, empty_slot_latches_cast_nothing)
{
    const Layout belt = test_belt();
    Latch latch;
    // SHIFTER + SPEC 2 is the empty slot.
    const Fold f = fold_belt_keys(sample(kSpec2, 0, true), belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_EQ(kCastNothing, f.belt_cast);
}

TEST(BeltInput, locked_slot_latches_its_entry)
{
    // The server refuses it and says which level opens it; the client never
    // swallows the press.
    const Layout belt = test_belt();
    Latch latch;
    const Fold f = fold_belt_keys(sample(kSpec3, 0), belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_EQ(kFreeze, f.belt_cast);
    // And SHIFTER + SPEC 3 is HEARTBURST.
    Latch other;
    EXPECT_EQ(kHeartburst, fold_belt_keys(sample(kSpec3, 0, true), belt, other).belt_cast);
}

TEST(BeltInput, on_fold_clears_switch_special)
{
    const Layout belt = test_belt();
    Latch latch;
    for (const Keys& keys : {sample(kSpec2, 0), sample(kSpec2, kSpec2), sample(kSpec1 | kSpec2 | kSpec3, 0)})
    {
        const Fold f = fold_keys(keys, /*ability_select=*/true, &belt, latch);
        EXPECT_FALSE(f.switch_held);
        EXPECT_FALSE(f.switch_pressed);
    }
}

TEST(BeltInput, off_fold_maps_slot_17_to_switch_special_and_ignores_18)
{
    Latch latch{2, kWarp};
    // SPEC 1 is SPECIAL, SPEC 2 is change special, SPEC 3 sends nothing.
    Fold f = fold_keys(sample(kSpec1 | kSpec2 | kSpec3, 0), /*ability_select=*/false, nullptr, latch);
    EXPECT_TRUE(f.special_held);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_TRUE(f.switch_held);
    EXPECT_TRUE(f.switch_pressed);
    EXPECT_EQ(0, f.belt_cast);
    EXPECT_EQ(Latch{}, latch) << "the classic fold clears a belt latch";

    f = fold_classic_keys(sample(kSpec3, 0), latch);
    EXPECT_EQ(Fold{}, f) << "SPEC 3 has no classic role";

    f = fold_classic_keys(sample(kSpec2, kSpec2), latch);
    EXPECT_TRUE(f.switch_held);
    EXPECT_FALSE(f.switch_pressed);
    EXPECT_FALSE(f.special_held);

    // Off ignores a belt it is handed: today's wire, exactly.
    const Layout belt = test_belt();
    f = fold_keys(sample(kSpec1, 0), false, &belt, latch);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_EQ(0, f.belt_cast);
}

TEST(BeltInput, on_without_a_walker_is_the_classic_special_and_nothing_else)
{
    // Menus and dead seats: SPEC 1 still reads as Special (menu select), no
    // byte, no change special, and a stale latch is dropped.
    Latch latch{3, kFreeze};
    Fold f = fold_keys(sample(kSpec1 | kSpec2 | kSpec3, 0), /*ability_select=*/true, nullptr, latch);
    EXPECT_TRUE(f.special_held);
    EXPECT_TRUE(f.special_pressed);
    EXPECT_FALSE(f.switch_held);
    EXPECT_FALSE(f.switch_pressed);
    EXPECT_EQ(0, f.belt_cast);
    EXPECT_EQ(Latch{}, latch);

    f = fold_keys(sample(kSpec2, 0), true, nullptr, latch);
    EXPECT_EQ(Fold{}, f);
}

TEST(BeltInput, on_with_a_walker_is_the_belt_fold)
{
    const Layout belt = test_belt();
    Latch a;
    Latch b;
    const Keys keys = sample(kSpec2, 0, true);
    EXPECT_EQ(fold_belt_keys(keys, belt, a), fold_keys(keys, true, &belt, b));
    EXPECT_EQ(a, b);
}
