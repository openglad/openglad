#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/replay.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

#include "test_game_world_fixture.h"

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
    EXPECT_EQ(expected.timer_wait_request, actual.timer_wait_request);
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

og::sim::WorldSnapshot make_initial_snapshot()
{
    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = 3u;
    snapshot.rng_state = 0xA1B2C3D4u;
    snapshot.level_tick_count = 9u;
    snapshot.timer_wait = 7;
    snapshot.living_count = 2;
    snapshot.control_hp = 42.5f;
    snapshot.my_team = 3;
    snapshot.allied_mode = 1;
    snapshot.difficulty = 125;
    snapshot.guy_id_counter = 2;
    snapshot.m_score[0] = 11u;
    snapshot.current_palette_id = 4u;
    snapshot.pending_exit_prompt = true;
    snapshot.grid_width = 2u;
    snapshot.grid_height = 2u;
    snapshot.grid_dirty = true;
    snapshot.grid_full_resend = true;
    snapshot.full_grid_data = {1u, 2u, 3u, 4u};
    snapshot.removed_entity_ids = {77u, 88u};

    snapshot.guy_snapshots.push_back({
        .guy_id = 1,
        .name = "Replay Hero",
        .family = 5,
        .strength = 12,
        .dexterity = 10,
        .constitution = 11,
        .intelligence = 9,
        .armor = 3,
        .exp = 456u,
        .kills = 7,
        .level_kills = 2,
        .total_damage = 90,
        .total_hits = 14,
        .total_shots = 20,
        .teamnum = 3,
        .scen_damage = 12.0f,
        .scen_kills = 1,
        .scen_damage_taken = 5.0f,
        .scen_min_hp = 30.0f,
        .scen_shots = 8,
        .scen_hits = 6,
        .level = 8,
    });

    og::sim::EntitySnapshot entity;
    entity.guy_id = 1;
    entity.entity_id = 99u;
    entity.xpos = 17;
    entity.ypos = 23;
    entity.team_num = 3u;
    entity.real_team_num = 3u;
    entity.user = 0;
    entity.order = Order::Living;
    entity.family = 5;
    entity.frame = 2;
    entity.worldx = 17.0f;
    entity.worldy = 23.0f;
    entity.hitpoints = 30.0f;
    entity.max_hitpoints = 40.0f;
    entity.magicpoints = 9.0f;
    entity.max_magicpoints = 12.0f;
    entity.level = 8;
    entity.path_check_counter = 4;
    entity.regen_delay = 6;
    snapshot.oblist.push_back(entity);

    return snapshot;
}

void expect_snapshot_eq(const og::sim::WorldSnapshot& expected,
                        const og::sim::WorldSnapshot& actual)
{
    EXPECT_EQ(og::sim::serialize_snapshot(expected),
              og::sim::serialize_snapshot(actual));
}

void write_u32_le(std::vector<std::uint8_t>& bytes,
                  std::size_t offset,
                  std::uint32_t value)
{
    ASSERT_GE(bytes.size(), offset + sizeof(value));
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
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
        .my_team = 3,
        .allied_mode = 1,
        .difficulty = 125,
        .campaign_id = "org.openglad.gladiator",
    };
    const og::sim::WorldSnapshot initial_snapshot = make_initial_snapshot();

    og::sim::ReplayRecorder recorder(header);
    recorder.set_initial_snapshot(initial_snapshot);
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
    EXPECT_EQ(header.my_team, player.header().my_team);
    EXPECT_EQ(header.allied_mode, player.header().allied_mode);
    EXPECT_EQ(header.difficulty, player.header().difficulty);
    EXPECT_EQ(header.campaign_id, player.header().campaign_id);
    expect_snapshot_eq(initial_snapshot, player.initial_snapshot());
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
        .my_team = 1,
        .allied_mode = 0,
        .difficulty = 100,
        .campaign_id = "org.openglad.gladiator",
    };
    const og::sim::WorldSnapshot initial_snapshot = make_initial_snapshot();
    const std::array<og::sim::InputStateMessage, 1> frames = {{
        {7u, make_sparse_input()},
    }};

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_replay(header, initial_snapshot, frames);

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

    og::sim::ReplayPlayer player;
    ASSERT_TRUE(player.load_bytes(bytes, &io_error));
    ASSERT_EQ(1u, player.frame_count());
    EXPECT_FALSE(player.load_bytes(bad_magic, &io_error));
    EXPECT_EQ(og::sim::ReplayIoError::InvalidHeader, io_error);
    EXPECT_EQ(0u, player.frame_count())
        << "a failed replacement must clear the previously loaded replay";
    EXPECT_TRUE(player.header().campaign_id.empty());
}

TEST(Replay, deserialize_rejects_each_malformed_replay_section)
{
    const og::sim::ReplayHeader header = {
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = 1u,
        .level_id = 2,
        .player_count = 1,
        .timer_wait = 6,
        .my_team = 1,
        .allied_mode = 0,
        .difficulty = 100,
        .campaign_id = "org.openglad.gladiator",
    };
    const std::array<og::sim::InputStateMessage, 1> frames = {{
        {7u, make_sparse_input()},
    }};
    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_replay(header, make_initial_snapshot(), frames);
    ASSERT_TRUE(og::sim::deserialize_replay(bytes).has_value());

    const auto expect_rejection = [](const std::vector<std::uint8_t>& malformed,
                                     og::sim::ReplayIoError expected_error) {
        og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
        EXPECT_FALSE(
            og::sim::deserialize_replay(malformed, &io_error).has_value());
        EXPECT_EQ(expected_error, io_error);
    };

    const std::vector<std::uint8_t> short_header(
        og::sim::kReplayHeaderSize - 1u,
        0u);
    expect_rejection(short_header, og::sim::ReplayIoError::InvalidHeader);

    auto too_many_players = bytes;
    too_many_players[5] = static_cast<std::uint8_t>(MAX_PLAYERS + 1);
    expect_rejection(too_many_players, og::sim::ReplayIoError::MalformedData);

    auto oversized_campaign = bytes;
    write_u32_le(oversized_campaign, 24u, 0xffffffffu);
    expect_rejection(oversized_campaign, og::sim::ReplayIoError::MalformedData);

    auto empty_campaign = bytes;
    write_u32_le(empty_campaign, 24u, 0u);
    expect_rejection(empty_campaign, og::sim::ReplayIoError::MalformedData);

    auto empty_snapshot = bytes;
    write_u32_le(empty_snapshot, 28u, 0u);
    expect_rejection(empty_snapshot, og::sim::ReplayIoError::MalformedData);

    auto corrupt_snapshot = bytes;
    const std::size_t snapshot_offset =
        og::sim::kReplayHeaderSize + header.campaign_id.size();
    ASSERT_LT(snapshot_offset, corrupt_snapshot.size());
    corrupt_snapshot[snapshot_offset] = 0xffu;
    expect_rejection(corrupt_snapshot, og::sim::ReplayIoError::MalformedData);

    auto invalid_input_frame = bytes;
    ASSERT_GE(invalid_input_frame.size(), og::sim::kSerializedInputMessageSize);
    const std::size_t frame_offset =
        invalid_input_frame.size() - og::sim::kSerializedInputMessageSize;
    ASSERT_LT(frame_offset + 1u, invalid_input_frame.size());
    invalid_input_frame[frame_offset + 1u] = og::sim::kSnapshotMessageType;
    expect_rejection(invalid_input_frame, og::sim::ReplayIoError::MalformedData);
}

TEST(Replay, recorder_write_file_requires_self_contained_bootstrap_data)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "openglad_test_replay_missing_bootstrap.ogr";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    og::sim::ReplayRecorder missing_campaign({
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = 7u,
        .level_id = 3,
        .player_count = 1,
        .timer_wait = 6,
        .my_team = 1,
        .allied_mode = 0,
        .difficulty = 100,
        .campaign_id = "",
    });
    missing_campaign.set_initial_snapshot(make_initial_snapshot());

    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    EXPECT_FALSE(missing_campaign.write_file(path, &io_error));
    EXPECT_EQ(og::sim::ReplayIoError::MalformedData, io_error);

    og::sim::ReplayRecorder missing_snapshot({
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = 7u,
        .level_id = 3,
        .player_count = 1,
        .timer_wait = 6,
        .my_team = 1,
        .allied_mode = 0,
        .difficulty = 100,
        .campaign_id = "org.openglad.gladiator",
    });

    EXPECT_FALSE(missing_snapshot.write_file(path, &io_error));
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

TEST(Replay, snapshot_difference_formats_all_field_value_kinds)
{
    const auto expect_diff =
        [](const og::sim::WorldSnapshot& expected,
           const og::sim::WorldSnapshot& actual,
           std::string_view field,
           std::string_view expected_value,
           std::string_view actual_value,
           bool compare_dirty_masks = true) {
            const std::optional<og::sim::ReplayVerificationFailure> diff =
                og::sim::find_first_snapshot_difference(
                    99u,
                    expected,
                    actual,
                    compare_dirty_masks);
            ASSERT_TRUE(diff.has_value()) << "expected difference for " << field;
            EXPECT_EQ(99u, diff->tick);
            EXPECT_EQ(field, diff->field);
            EXPECT_EQ(expected_value, diff->expected_value);
            EXPECT_EQ(actual_value, diff->actual_value);
        };

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        actual.game_ended = true;
        expect_diff(expected, actual, "game_ended", "false", "true");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.control_hp = 12.5f;
        actual.control_hp = 3.25f;
        expect_diff(expected, actual, "control_hp", "12.5", "3.25");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.end = -2;
        actual.end = 4;
        expect_diff(expected, actual, "end", "-2", "4");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.current_palette_id = 2u;
        actual.current_palette_id = 9u;
        expect_diff(expected, actual, "current_palette_id", "2", "9");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.ctf_flags[0].state = og::sim::CtfFlagState::AtHome;
        actual.ctf_flags[0].state = og::sim::CtfFlagState::Carried;
        expect_diff(expected,
                    actual,
                    "ctf_flags[0].state",
                    "0",
                    "1");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.ctf_captures[0] = 2u;
        actual.ctf_captures[0] = 4u;
        expect_diff(expected,
                    actual,
                    "ctf_captures[0]",
                    "2",
                    "4");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.ctf_anchor_x[0][0] = 5;
        actual.ctf_anchor_x[0][0] = 8;
        expect_diff(expected,
                    actual,
                    "ctf_anchor_x[0][0]",
                    "5",
                    "8");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.full_grid_data = {1u, 2u};
        actual.full_grid_data = {1u, 2u, 3u};
        expect_diff(expected,
                    actual,
                    "full_grid_data.size",
                    "2",
                    "3");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.full_grid_data = {1u, 2u};
        actual.full_grid_data = {1u, 9u};
        expect_diff(expected,
                    actual,
                    "full_grid_data[1]",
                    "2",
                    "9");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.grid_dirty_tiles.push_back({.x = 1, .y = 2, .value = 3u});
        actual.grid_dirty_tiles.push_back({.x = 1, .y = 2, .value = 7u});
        expect_diff(expected,
                    actual,
                    "grid_dirty_tiles[0].value",
                    "3",
                    "7");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.guy_snapshots.push_back({.name = "Expected"});
        actual.guy_snapshots.push_back({.name = "Actual"});
        expect_diff(expected,
                    actual,
                    "guy_snapshots[0].name",
                    "Expected",
                    "Actual");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        og::sim::EntitySnapshot expected_entity;
        og::sim::EntitySnapshot actual_entity = expected_entity;
        expected_entity.order = Order::Living;
        actual_entity.order = Order::Weapon;
        expected.oblist.push_back(expected_entity);
        actual.oblist.push_back(actual_entity);
        expect_diff(expected,
                    actual,
                    "oblist[0].order",
                    "0",
                    "1");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        og::sim::EntitySnapshot expected_entity;
        og::sim::EntitySnapshot actual_entity = expected_entity;
        expected_entity.special_cost[0] = 10u;
        actual_entity.special_cost[0] = 20u;
        expected.oblist.push_back(expected_entity);
        actual.oblist.push_back(actual_entity);
        expect_diff(expected,
                    actual,
                    "oblist[0].special_cost[0]",
                    "10",
                    "20");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        og::sim::EntitySnapshot expected_entity;
        og::sim::EntitySnapshot actual_entity = expected_entity;
        expected_entity.dirty_mask[0] = 1u;
        actual_entity.dirty_mask[0] = 2u;
        expected.oblist.push_back(expected_entity);
        actual.oblist.push_back(actual_entity);
        expect_diff(expected,
                    actual,
                    "oblist[0].dirty_mask[0]",
                    "1",
                    "2",
                    true);
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.fxlist.push_back({.entity_id = 41u});
        actual.fxlist.push_back({.entity_id = 42u});
        expect_diff(expected,
                    actual,
                    "fxlist[0].entity_id",
                    "41",
                    "42");
    }

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.weaplist.push_back({.xpos = 12});
        actual.weaplist.push_back({.xpos = 13});
        expect_diff(expected,
                    actual,
                    "weaplist[0].xpos",
                    "12",
                    "13");
    }

    {
        og::sim::WorldSnapshot expected;
        expected.grid_dirty_tiles.push_back({.x = 1, .y = 1, .value = 2u});
        expected.removed_entity_ids = {70u};
        og::sim::WorldSnapshot actual = expected;
        actual.removed_entity_ids[0] = 71u;
        expect_diff(expected,
                    actual,
                    "removed_entity_ids[0]",
                    "70",
                    "71");
    }

    const auto expect_size_difference =
        [&](auto add_expected, auto add_actual,
            std::string_view field) {
            og::sim::WorldSnapshot expected;
            og::sim::WorldSnapshot actual;
            add_expected(expected);
            add_actual(actual);
            expect_diff(expected, actual, field, "1", "0");
        };

    expect_size_difference(
        [](og::sim::WorldSnapshot& snapshot) {
            snapshot.ctf_respawn_queue.push_back({});
        },
        [](og::sim::WorldSnapshot&) {},
        "ctf_respawn_queue.size");
    expect_size_difference(
        [](og::sim::WorldSnapshot& snapshot) {
            snapshot.grid_dirty_tiles.push_back({});
        },
        [](og::sim::WorldSnapshot&) {},
        "grid_dirty_tiles.size");
    expect_size_difference(
        [](og::sim::WorldSnapshot& snapshot) {
            snapshot.guy_snapshots.push_back({});
        },
        [](og::sim::WorldSnapshot&) {},
        "guy_snapshots.size");
    expect_size_difference(
        [](og::sim::WorldSnapshot& snapshot) {
            snapshot.oblist.push_back({});
        },
        [](og::sim::WorldSnapshot&) {},
        "oblist.size");
    expect_size_difference(
        [](og::sim::WorldSnapshot& snapshot) {
            snapshot.fxlist.push_back({});
        },
        [](og::sim::WorldSnapshot&) {},
        "fxlist.size");
    expect_size_difference(
        [](og::sim::WorldSnapshot& snapshot) {
            snapshot.weaplist.push_back({});
        },
        [](og::sim::WorldSnapshot&) {},
        "weaplist.size");
    expect_size_difference(
        [](og::sim::WorldSnapshot& snapshot) {
            snapshot.removed_entity_ids.push_back(17u);
        },
        [](og::sim::WorldSnapshot&) {},
        "removed_entity_ids.size");

    {
        og::sim::WorldSnapshot expected;
        og::sim::WorldSnapshot actual = expected;
        expected.snapshot_hash = 0x12345678u;
        expect_diff(expected, actual, "snapshot_hash", "305419896", "0");
    }
}

TEST(Replay, recorder_zero_tick_keyframe_becomes_initial_snapshot)
{
    og::sim::ReplayRecorder recorder;
    const og::sim::WorldSnapshot initial = make_initial_snapshot();

    recorder.record_snapshot(
        0u,
        initial,
        og::sim::ReplayCheckpointKind::Keyframe);

    ASSERT_TRUE(recorder.has_initial_snapshot());
    expect_snapshot_eq(initial, recorder.initial_snapshot());
    ASSERT_EQ(1u, recorder.checkpoints().size());
    EXPECT_EQ(0u, recorder.checkpoints().front().tick);
    EXPECT_EQ(og::sim::ReplayCheckpointKind::Keyframe,
              recorder.checkpoints().front().kind);
    expect_snapshot_eq(initial, recorder.checkpoints().front().snapshot);
}

TEST(Replay, file_errors_report_the_failed_operation)
{
    const og::sim::ReplayHeader header = {
        .version = og::sim::kReplayFormatVersion,
        .player_count = 1,
        .campaign_id = "org.openglad.gladiator",
    };
    og::sim::ReplayRecorder recorder(header);
    recorder.set_initial_snapshot(make_initial_snapshot());

    const std::filesystem::path missing_parent =
        std::filesystem::temp_directory_path() /
        "openglad_test_replay_parent_does_not_exist";
    const std::filesystem::path write_path = missing_parent / "replay.ogr";
    std::error_code ec;
    std::filesystem::remove_all(missing_parent, ec);
    ASSERT_FALSE(std::filesystem::exists(missing_parent));

    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    EXPECT_FALSE(recorder.write_file(write_path, &io_error));
    EXPECT_EQ(og::sim::ReplayIoError::OpenWriteFailed, io_error);

    og::sim::ReplayPlayer player;
    EXPECT_FALSE(player.load_file(write_path, &io_error));
    EXPECT_EQ(og::sim::ReplayIoError::OpenReadFailed, io_error);

    EXPECT_FALSE(player.load_file(std::filesystem::temp_directory_path(),
                                  &io_error));
    EXPECT_EQ(og::sim::ReplayIoError::OpenReadFailed, io_error);

#if defined(__linux__)
    const std::filesystem::path dev_full = "/dev/full";
    ASSERT_TRUE(std::filesystem::is_character_file(dev_full))
        << "Linux write-error coverage requires the standard /dev/full device";
    // Exceed the stream buffer so the production write observes /dev/full
    // synchronously instead of deferring the device error to destruction.
    for (std::uint32_t tick = 0; tick < 4096u; ++tick)
        recorder.record_input(tick, make_dense_input());
    ASSERT_GT(recorder.serialize().size(), 65536u)
        << "fixture must exceed the standard stream buffer";
    io_error = og::sim::ReplayIoError::None;
    EXPECT_FALSE(recorder.write_file(dev_full, &io_error));
    EXPECT_EQ(og::sim::ReplayIoError::OpenWriteFailed, io_error)
        << "a stream that opens but rejects bytes is a write failure";
#endif
}

TEST(Replay, recorder_clear_and_world_snapshot_recorders_reset_state)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.tick_count_ = 12u;

    og::sim::ReplayRecorder recorder({
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = 5u,
        .level_id = 1,
        .player_count = 1,
        .timer_wait = 6,
        .my_team = 0,
        .allied_mode = 0,
        .difficulty = 100,
        .campaign_id = "org.openglad.gladiator",
    });

    recorder.record_initial_world(world);
    EXPECT_TRUE(recorder.has_initial_snapshot());
    recorder.record_world_snapshot(world.tick_count_, world);
    recorder.record_world_keyframe(world.tick_count_ + 1u, world);
    ASSERT_EQ(recorder.checkpoints().size(), 2u);
    EXPECT_EQ(recorder.checkpoints()[0].kind,
              og::sim::ReplayCheckpointKind::Snapshot);
    EXPECT_EQ(recorder.checkpoints()[1].kind,
              og::sim::ReplayCheckpointKind::Keyframe);

    recorder.record_input(12u, make_sparse_input());
    EXPECT_EQ(recorder.frame_count(), 1u);
    recorder.clear();
    EXPECT_FALSE(recorder.has_initial_snapshot());
    EXPECT_TRUE(recorder.frames().empty());
    EXPECT_TRUE(recorder.checkpoints().empty());
    EXPECT_EQ(recorder.header().campaign_id, "");
}

TEST(Replay, replay_player_verify_world_tracks_first_divergence)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.tick_count_ = 7u;
    world.rng_.state_ = 0xCAFEBABEu;
    world.timer_wait = 5;
    world.m_score[0] = 11u;
    world.pending_exit_prompt = true;

    const og::sim::ReplayCheckpoint checkpoint = {
        .tick = world.tick_count_,
        .kind = og::sim::ReplayCheckpointKind::Keyframe,
        .snapshot = og::sim::peek_keyframe_snapshot(world),
    };
    const og::sim::ReplayCheckpoint stale_checkpoint = {
        .tick = checkpoint.tick - 2u,
        .kind = og::sim::ReplayCheckpointKind::Keyframe,
        .snapshot = checkpoint.snapshot,
    };

    og::sim::ReplayPlayer player;
    player.set_checkpoints({checkpoint, stale_checkpoint});

    EXPECT_FALSE(player.verify_world(world, checkpoint.tick - 1, false).has_value());
    EXPECT_FALSE(player.first_divergence().has_value());

    EXPECT_FALSE(player.verify_world(world, checkpoint.tick, false).has_value());
    EXPECT_FALSE(player.first_divergence().has_value());

    player.reset();
    world.m_score[0] = 99u;

    const std::optional<og::sim::ReplayVerificationFailure> failure =
        player.verify_world(world, checkpoint.tick, false);
    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(checkpoint.tick, failure->tick);
    EXPECT_EQ("m_score[0]", failure->field);
    EXPECT_EQ("11", failure->expected_value);
    EXPECT_EQ("99", failure->actual_value);

    ASSERT_TRUE(player.first_divergence().has_value());
    EXPECT_EQ(failure->tick, player.first_divergence()->tick);
    EXPECT_EQ(failure->field, player.first_divergence()->field);
    EXPECT_EQ(failure->expected_value, player.first_divergence()->expected_value);
    EXPECT_EQ(failure->actual_value, player.first_divergence()->actual_value);

    // Exercise signed 32-bit diagnostics independently of the unsigned score
    // mismatch above. The formatter is part of the user-facing divergence
    // report and must preserve negative values.
    world.m_score[0] = 11u;
    og::sim::ReplayPlayer signed_value_player;
    signed_value_player.set_checkpoints({checkpoint});
    world.enemy_freeze = -17;
    const auto signed_failure =
        signed_value_player.verify_world(world, checkpoint.tick, false);
    ASSERT_TRUE(signed_failure.has_value());
    EXPECT_EQ("enemy_freeze", signed_failure->field);
    EXPECT_EQ("0", signed_failure->expected_value);
    EXPECT_EQ("-17", signed_failure->actual_value);
}
