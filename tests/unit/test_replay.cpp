#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/replay.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <vector>

namespace {

void set_flags(bool (&slots)[NUM_INPUT_KEYS], std::initializer_list<InputAction> actions)
{
    for (const InputAction action : actions)
        slots[static_cast<int>(action)] = true;
}

void expect_player_input_eq(const PlayerInput& expected, const PlayerInput& actual)
{
    for (int key = 0; key < NUM_INPUT_KEYS; ++key)
    {
        EXPECT_EQ(expected.held[key], actual.held[key]) << "held mismatch at key " << key;
        EXPECT_EQ(expected.pressed[key], actual.pressed[key]) << "pressed mismatch at key " << key;
    }
}

void expect_input_state_eq(const InputState& expected, const InputState& actual)
{
    EXPECT_EQ(expected.quit_requested, actual.quit_requested);
    for (int player = 0; player < MAX_PLAYERS; ++player)
        expect_player_input_eq(expected.players[player], actual.players[player]);
}

InputState make_sparse_input()
{
    InputState input{};
    set_flags(input.players[0].held, {InputAction::MoveUpLeft, InputAction::Fire});
    set_flags(input.players[0].pressed, {InputAction::Fire});
    set_flags(input.players[1].held, {InputAction::MoveDownRight, InputAction::Special});
    set_flags(input.players[1].pressed, {InputAction::SwitchSpecial});
    return input;
}

InputState make_dense_input()
{
    InputState input{};
    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        {
            input.players[player].held[key] = ((player + key) % 2) == 0;
            input.players[player].pressed[key] = ((player * 5 + key) % 3) == 0;
        }
    }
    input.quit_requested = true;
    return input;
}

} // namespace

TEST(Replay, file_roundtrip_preserves_header_and_frames)
{
    const og::sim::ReplayHeader header = {
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = 0x12345678u,
        .level_id = 42,
        .player_count = 2,
        .timer_wait = 7,
    };

    og::sim::ReplayRecorder recorder(header);
    recorder.record_input(10u, make_sparse_input());
    recorder.record_input(11u, make_dense_input());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "openglad_test_replay_unit.ogr";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    ASSERT_TRUE(recorder.write_file(path, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);

    og::sim::ReplayPlayer player;
    ASSERT_TRUE(player.load_file(path, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);

    EXPECT_EQ(header.version, player.header().version);
    EXPECT_EQ(header.initial_rng_state, player.header().initial_rng_state);
    EXPECT_EQ(header.level_id, player.header().level_id);
    EXPECT_EQ(header.player_count, player.header().player_count);
    EXPECT_EQ(header.timer_wait, player.header().timer_wait);
    ASSERT_EQ(recorder.frame_count(), player.frame_count());

    for (std::size_t i = 0; i < recorder.frames().size(); ++i)
    {
        EXPECT_EQ(recorder.frames()[i].tick, player.frames()[i].tick);
        expect_input_state_eq(recorder.frames()[i].input, player.frames()[i].input);
    }

    std::filesystem::remove(path, ec);
}

TEST(Replay, deserialize_rejects_bad_magic_version_and_truncated_payload)
{
    const og::sim::ReplayHeader header = {
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = 1u,
        .level_id = 2,
        .player_count = 1,
        .timer_wait = 6,
    };
    const std::array<og::sim::InputStateMessage, 1> frames = {{
        {7u, make_sparse_input()},
    }};

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_replay(header, frames);

    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;

    auto bad_magic = bytes;
    bad_magic[0] = static_cast<std::uint8_t>('X');
    EXPECT_FALSE(og::sim::deserialize_replay(bad_magic, &io_error).has_value());
    EXPECT_EQ(og::sim::ReplayIoError::InvalidHeader, io_error);

    auto bad_version = bytes;
    bad_version[4] = static_cast<std::uint8_t>(og::sim::kReplayFormatVersion + 1);
    EXPECT_FALSE(og::sim::deserialize_replay(bad_version, &io_error).has_value());
    EXPECT_EQ(og::sim::ReplayIoError::UnsupportedVersion, io_error);

    auto truncated = bytes;
    truncated.pop_back();
    EXPECT_FALSE(og::sim::deserialize_replay(truncated, &io_error).has_value());
    EXPECT_EQ(og::sim::ReplayIoError::MalformedData, io_error);
}

TEST(Replay, find_first_snapshot_difference_reports_nested_field)
{
    og::sim::WorldSnapshot expected;
    og::sim::WorldSnapshot actual;

    expected.tick_count = 25u;
    actual.tick_count = 25u;
    expected.rng_state = 123u;
    actual.rng_state = 123u;
    expected.oblist.resize(1);
    actual.oblist = expected.oblist;
    expected.oblist[0].entity_id = 9u;
    actual.oblist[0].entity_id = 9u;
    actual.oblist[0].xpos = 77;

    const std::optional<og::sim::ReplayVerificationFailure> diff =
        og::sim::find_first_snapshot_difference(25u, expected, actual);
    ASSERT_TRUE(diff.has_value());
    EXPECT_EQ(25u, diff->tick);
    EXPECT_EQ("oblist[0].xpos", diff->field);
    EXPECT_EQ("0", diff->expected_value);
    EXPECT_EQ("77", diff->actual_value);
}
