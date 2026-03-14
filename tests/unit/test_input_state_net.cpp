#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state_net.h>

#include <gtest/gtest.h>

#include <initializer_list>

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

TEST(InputStateNet, deserialize_rejects_wrong_size_and_version)
{
    InputState input{};
    const auto bytes = og::sim::serialize_input(input);

    ASSERT_FALSE(
        og::sim::deserialize_input(
            std::span<const std::uint8_t>(bytes.data(), bytes.size() - 1))
            .has_value());

    auto bad_version = bytes;
    bad_version[0] = static_cast<std::uint8_t>(((og::sim::kInputStateProtocolVersion + 1) << 1)
        | (bad_version[0] & 0x01u));
    ASSERT_FALSE(og::sim::deserialize_input(bad_version).has_value());
}
