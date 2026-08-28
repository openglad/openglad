#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state_net.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <optional>
#include <span>

namespace {

void set_flags(bool (&slots)[NUM_INPUT_KEYS], std::initializer_list<InputAction> actions)
{
    for (const InputAction action : actions)
    {
        slots[static_cast<int>(action)] = true;
    }
}

void expect_player_input_eq(const PlayerInput& expected, const PlayerInput& actual)
{
    for (int key = 0; key < NUM_INPUT_KEYS; ++key)
    {
        ASSERT_EQ(expected.held[key], actual.held[key]) << "held mismatch at key " << key;
        ASSERT_EQ(expected.pressed[key], actual.pressed[key]) << "pressed mismatch at key " << key;
    }
}

void expect_input_state_eq(const InputState& expected, const InputState& actual)
{
    ASSERT_EQ(expected.quit_requested, actual.quit_requested);
    ASSERT_EQ(expected.timer_wait_request, actual.timer_wait_request);
    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        expect_player_input_eq(expected.players[player], actual.players[player]);
    }
}

InputState make_directional_pattern()
{
    InputState input{};
    set_flags(input.players[0].held, {InputAction::MoveUpLeft, InputAction::Fire, InputAction::OpenPrefs});
    set_flags(input.players[0].pressed, {InputAction::Fire, InputAction::Yell});

    set_flags(input.players[1].held, {InputAction::MoveLeft, InputAction::MoveRight, InputAction::Special});
    set_flags(input.players[1].pressed, {InputAction::SwitchSpecial, InputAction::Shift});

    set_flags(input.players[2].held, {InputAction::MoveUp, InputAction::MoveDown, InputAction::MoveDownRight, InputAction::Cheat});
    set_flags(input.players[2].pressed, {InputAction::MoveDown, InputAction::Special, InputAction::SwitchChar});

    set_flags(input.players[3].held, {InputAction::MoveDownLeft, InputAction::MoveUpRight, InputAction::MoveRight, InputAction::Special});
    set_flags(input.players[3].pressed, {InputAction::MoveUpLeft, InputAction::Fire, InputAction::OpenPrefs, InputAction::Cheat});

    input.quit_requested = true;
    input.timer_wait_request = 7;
    return input;
}

InputState make_dense_pattern()
{
    InputState input{};
    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        {
            input.players[player].held[key] = ((player + key) % 2) == 0;
            input.players[player].pressed[key] = ((player * 3 + key) % 5) <= 1;
        }
    }
    input.timer_wait_request = 20;
    return input;
}

} // namespace

TEST(InputStateNet, roundtrip_preserves_state_and_movement_helpers)
{
    const InputState inputs[] = {
        make_directional_pattern(),
        make_dense_pattern(),
    };

    for (const InputState& input : inputs)
    {
        const auto bytes = og::sim::serialize_input(input);
        ASSERT_EQ(bytes.size(), og::sim::kSerializedInputStateSize);

        const std::optional<InputState> decoded = og::sim::deserialize_input(bytes);
        ASSERT_TRUE(decoded.has_value());
        expect_input_state_eq(input, *decoded);

        for (int player = 0; player < MAX_PLAYERS; ++player)
        {
            ASSERT_EQ(input.players[player].move_x(), decoded->players[player].move_x())
                << "move_x mismatch for player " << player;
            ASSERT_EQ(input.players[player].move_y(), decoded->players[player].move_y())
                << "move_y mismatch for player " << player;
        }
    }
}

TEST(InputStateNet, roundtrip_preserves_transport_tick)
{
    const InputState input = make_directional_pattern();
    const auto bytes = og::sim::serialize_input(0x11223344u, input);

    const std::optional<og::sim::InputStateMessage> decoded =
        og::sim::deserialize_input_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(0x11223344u, decoded->tick);
    expect_input_state_eq(input, decoded->input);
}

TEST(InputStateNet, serialize_emits_expected_wire_format)
{
    InputState input{};
    set_flags(input.players[0].held, {InputAction::MoveUp, InputAction::Cheat});
    set_flags(input.players[0].pressed, {InputAction::MoveUp, InputAction::Cheat});

    set_flags(input.players[1].held, {InputAction::MoveUpRight, InputAction::Fire, InputAction::SwitchChar});
    set_flags(input.players[1].pressed, {InputAction::MoveRight, InputAction::SwitchSpecial});

    set_flags(input.players[2].held, {InputAction::MoveUpLeft});
    set_flags(input.players[2].pressed, {InputAction::MoveDownRight, InputAction::Yell});

    set_flags(input.players[3].held, {InputAction::MoveDownLeft, InputAction::Shift});
    set_flags(input.players[3].pressed, {InputAction::MoveLeft, InputAction::OpenPrefs});

    input.quit_requested = true;

    constexpr std::array<std::uint8_t, og::sim::kSerializedInputStateSize> expected = {
        0x11, 0x02, 0x15, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01,
        0x01, 0x80, 0x01, 0x80,
        0x02, 0x05, 0x04, 0x08,
        0x80, 0x00, 0x08, 0x10,
        0x20, 0x20, 0x40, 0x40,
    };

    const auto bytes = og::sim::serialize_input(input);
    ASSERT_EQ(bytes, expected);
}

TEST(InputStateNet, deserialize_reads_expected_wire_format)
{
    constexpr std::array<std::uint8_t, og::sim::kSerializedInputStateSize> bytes = {
        0x11, 0x02, 0x15, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x24, 0x00, 0x40, 0x02,
        0x00, 0x41, 0x10, 0x00,
        0x08, 0x88, 0x00, 0x20,
        0x10, 0x10, 0x01, 0x01,
    };

    InputState expected{};
    set_flags(expected.players[0].held, {InputAction::MoveRight, InputAction::MoveDownLeft});
    set_flags(expected.players[0].pressed, {InputAction::MoveLeft, InputAction::Special});

    set_flags(expected.players[1].held, {InputAction::Fire, InputAction::OpenPrefs});
    set_flags(expected.players[1].pressed, {InputAction::MoveDown});

    set_flags(expected.players[2].held, {InputAction::MoveDownRight, InputAction::SwitchSpecial, InputAction::Cheat});
    set_flags(expected.players[2].pressed, {InputAction::Shift});

    set_flags(expected.players[3].held, {InputAction::MoveDown, InputAction::Yell});
    set_flags(expected.players[3].pressed, {InputAction::MoveUp, InputAction::Fire});

    const std::optional<InputState> decoded = og::sim::deserialize_input(bytes);
    ASSERT_TRUE(decoded.has_value());
    expect_input_state_eq(expected, *decoded);
}

TEST(InputStateNet, deserialize_rejects_wrong_size_version_type_and_length)
{
    InputState input{};
    const auto bytes = og::sim::serialize_input(input);

    ASSERT_FALSE(
        og::sim::deserialize_input(
            std::span<const std::uint8_t>(bytes.data(), bytes.size() - 1))
            .has_value());

    auto bad_version = bytes;
    bad_version[0] = static_cast<std::uint8_t>(og::sim::kInputStateProtocolVersion + 1);
    ASSERT_FALSE(og::sim::deserialize_input(bad_version).has_value());

    auto bad_type = bytes;
    bad_type[1] = og::sim::kSnapshotMessageType;
    ASSERT_FALSE(og::sim::deserialize_input(bad_type).has_value());

    auto bad_length = bytes;
    bad_length[2] = 0;
    bad_length[3] = 0;
    ASSERT_FALSE(og::sim::deserialize_input(bad_length).has_value());
}

TEST(InputStateNet, metadata_byte_packs_quit_flag_and_timer_wait_request)
{
    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 7;

    const auto bytes = og::sim::serialize_input(input);
    ASSERT_EQ(0x11u, bytes[8]);

    const std::optional<og::sim::InputStateMessage> decoded =
        og::sim::deserialize_input_message(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->input.quit_requested);
    EXPECT_EQ(7, decoded->input.timer_wait_request);
}
