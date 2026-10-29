#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state_net.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
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
    ASSERT_EQ(expected.belt_cast, actual.belt_cast) << "belt_cast mismatch";
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

    input.players[1].belt_cast = 0x0A;
    input.players[2].belt_cast = 0x07;
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

    // New Ability Select (protocol v22): slot 2 with its alternate for
    // player 1, slot 5 for player 3; players 0 and 2 send the classic 0.
    input.players[1].belt_cast = 0x0A;
    input.players[3].belt_cast = 0x05;

    input.quit_requested = true;

    constexpr std::array<std::uint8_t, og::sim::kSerializedInputStateSize> expected = {
        0x17, 0x02, 0x19, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01,
        0x01, 0x80, 0x01, 0x80,
        0x02, 0x05, 0x04, 0x08,
        0x80, 0x00, 0x08, 0x10,
        0x20, 0x20, 0x40, 0x40,
        0x00, 0x0A, 0x00, 0x05,
    };

    const auto bytes = og::sim::serialize_input(input);
    ASSERT_EQ(bytes, expected);
}

// The OFF client: a seat with New Ability Select off never sets belt_cast,
// so its frame is today's (protocol v21) frame byte for byte under the new
// header, plus four zero belt bytes. The 21 payload bytes below are d758's
// literal pin, unchanged. Proof it can fail: writing the metadata byte into
// the belt slots, or packing the belt bytes into the player bitfields, moves
// a payload byte here.
TEST(InputStateNet, off_client_input_is_bitwise_todays_input)
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

    constexpr std::array<std::uint8_t, 21> todays_payload = {
        0x00, 0x00, 0x00, 0x00,
        0x01,
        0x01, 0x80, 0x01, 0x80,
        0x02, 0x05, 0x04, 0x08,
        0x80, 0x00, 0x08, 0x10,
        0x20, 0x20, 0x40, 0x40,
    };

    const auto bytes = og::sim::serialize_input(input);
    ASSERT_EQ(og::sim::kTransportHeaderSize + 21 + 4, bytes.size());
    for (std::size_t i = 0; i < todays_payload.size(); ++i)
        EXPECT_EQ(todays_payload[i], bytes[og::sim::kTransportHeaderSize + i])
            << "payload byte " << i;
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_EQ(0u, bytes[og::sim::kTransportHeaderSize + 21 + i])
            << "belt byte " << i;
}

// Every belt byte value survives the codec unchanged for every seat: the
// codec never clamps (the in-process validator needs a lossless round trip;
// the server, not the codec, refuses a malformed byte). Proof it can fail:
// masking the byte with 0x7F on read, or dropping seat 3's byte, goes red.
TEST(InputStateNet, belt_byte_round_trips_all_256_values)
{
    for (int value = 0; value < 256; ++value)
    {
        for (int seat = 0; seat < MAX_PLAYERS; ++seat)
        {
            InputState input{};
            input.players[seat].belt_cast = static_cast<std::uint8_t>(value);
            input.players[seat].held[static_cast<int>(InputAction::Special)] = true;
            const auto bytes = og::sim::serialize_input(input);
            const std::optional<InputState> decoded = og::sim::deserialize_input(bytes);
            ASSERT_TRUE(decoded.has_value());
            for (int other = 0; other < MAX_PLAYERS; ++other)
                ASSERT_EQ(other == seat ? value : 0,
                          decoded->players[other].belt_cast)
                    << "value " << value << " seat " << seat << " read at " << other;
        }
    }
}

// InputState::clear() resets the belt byte beside held and pressed, or a
// cleared state carries a stale entry into the next sample.
TEST(InputStateNet, clear_resets_the_belt_byte)
{
    InputState input{};
    for (int seat = 0; seat < MAX_PLAYERS; ++seat)
    {
        input.players[seat].belt_cast = static_cast<std::uint8_t>(0x09 + seat);
        input.players[seat].held[static_cast<int>(InputAction::Special)] = true;
    }
    input.clear();
    for (int seat = 0; seat < MAX_PLAYERS; ++seat)
    {
        EXPECT_EQ(0, input.players[seat].belt_cast) << "seat " << seat;
        EXPECT_FALSE(input.players[seat].is_held(InputAction::Special));
    }
}

TEST(InputStateNet, deserialize_reads_expected_wire_format)
{
    constexpr std::array<std::uint8_t, og::sim::kSerializedInputStateSize> bytes = {
        0x17, 0x02, 0x19, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
        0x24, 0x00, 0x40, 0x02,
        0x00, 0x41, 0x10, 0x00,
        0x08, 0x88, 0x00, 0x20,
        0x10, 0x10, 0x01, 0x01,
        0x09, 0x00, 0x07, 0x00,
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

    expected.players[0].belt_cast = 0x09;
    expected.players[2].belt_cast = 0x07;

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
    // A protocol-v21 frame (21-byte payload, no belt bytes) is too short.
    ASSERT_EQ(28u, bytes.size() - 1);
    ASSERT_FALSE(
        og::sim::deserialize_input(
            std::span<const std::uint8_t>(bytes.data(), bytes.size() - 4))
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
