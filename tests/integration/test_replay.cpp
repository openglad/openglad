#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/replay_runtime.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/io_common.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "test_network_fixture.h"

short load_saved_game(const char* filename, screen* scr);

namespace {

constexpr short kReplayLevel = 1;
constexpr int kReplayTicks = 100;
constexpr int kCheckpointInterval = 25;
constexpr std::uint32_t kReplayLoadSeedBase = 0x13579BDFu;

void hold_action(PlayerInput& input, InputAction action)
{
    input.held[static_cast<int>(action)] = true;
}

void press_action(PlayerInput& input, InputAction action)
{
    input.pressed[static_cast<int>(action)] = true;
}

void apply_direction(PlayerInput& input, int direction_index)
{
    switch (direction_index % 8)
    {
        case 0:
            hold_action(input, InputAction::MoveUp);
            break;
        case 1:
            hold_action(input, InputAction::MoveUpRight);
            break;
        case 2:
            hold_action(input, InputAction::MoveRight);
            break;
        case 3:
            hold_action(input, InputAction::MoveDownRight);
            break;
        case 4:
            hold_action(input, InputAction::MoveDown);
            break;
        case 5:
            hold_action(input, InputAction::MoveDownLeft);
            break;
        case 6:
            hold_action(input, InputAction::MoveLeft);
            break;
        case 7:
            hold_action(input, InputAction::MoveUpLeft);
            break;
        default:
            break;
    }
}

bool prepare_default_level_load()
{
    restore_default_campaigns();
    restore_default_settings();
#ifdef TESTING
    set_mounted_campaign_for_testing("");
#endif
    og::runtime::current_session->myscreen_->save_data.current_campaign =
        "gladiator";
    return mount_campaign_package_with_error("gladiator") ==
           CampaignPackageIoError::None;
}

void configure_replay_team(SaveData& save, int player_count)
{
    save.reset();
    save.current_campaign = "gladiator";
    save.current_levels[save.current_campaign] = kReplayLevel;
    save.scen_num = kReplayLevel;
    save.my_team = 0;
    save.numplayers = static_cast<unsigned char>(player_count);
    // Exercise four distinct combat teams. Together mode intentionally shares
    // one seat team and would reject this one-fighter-per-color roster at the
    // picker because it cannot supply every local seat with a controller.
    save.allied_mode = 0;
    save.team_size = 0;

    constexpr std::array<int, 4> families = {
        FAMILY_ARCHMAGE,
        FAMILY_ARCHER,
        FAMILY_THIEF,
        FAMILY_CLERIC,
    };
    constexpr std::array<short, 4> levels = {10, 10, 10, 10};
    // Include red explicitly: the old replay view reconstruction skipped team
    // 0 and shifted every mixed-team controller onto the next roster color.
    constexpr std::array<short, 4> teams = {0, 1, 2, 3};
    constexpr std::array<std::string_view, 4> names = {
        "REPLAY_ARCHMAGE",
        "REPLAY_ARCHER",
        "REPLAY_THIEF",
        "REPLAY_CLERIC",
    };

    for (int i = 0; i < player_count; ++i)
    {
        auto recruit = std::make_unique<guy>(families[static_cast<std::size_t>(i)]);
        recruit->name = std::string(names[static_cast<std::size_t>(i)]);
        recruit->teamnum = teams[static_cast<std::size_t>(i)];
        recruit->upgrade_to_level(levels[static_cast<std::size_t>(i)]);
        save.team_list[static_cast<std::size_t>(i)] = std::move(recruit);
    }

    save.team_size = static_cast<unsigned char>(player_count);
}

void populate_chaotic_input(InputState& input, int tick, int player_count)
{
    input.clear();

    for (int player = 0; player < player_count; ++player)
    {
        PlayerInput& pi = input.players[player];
        const int direction = ((tick / (3 + (player % 2))) + (player * 2)) % 8;
        apply_direction(pi, direction);

        if (((tick + player) % 3) != 0)
            hold_action(pi, InputAction::Fire);
        if (((tick + player) % 6) == 0)
            press_action(pi, InputAction::Fire);

        const int special_phase = (tick + player * 3) % 11;
        if (special_phase < 2)
            hold_action(pi, InputAction::Special);
        if (special_phase == 0)
            press_action(pi, InputAction::Special);

        if (special_phase < 2 && (((tick / 11) + player) % 2) == 1)
            hold_action(pi, InputAction::Shift);

        if (((tick + player * 5) % 19) == 0)
            press_action(pi, InputAction::SwitchSpecial);

        if (((tick + player * 7) % 31) == 0)
            press_action(pi, InputAction::Yell);
    }
}

std::vector<std::uint32_t> capture_view_control_ids(screen& game_screen,
                                                    int player_count)
{
    std::vector<std::uint32_t> control_ids;
    control_ids.reserve(static_cast<std::size_t>(player_count));

    for (int player = 0; player < player_count; ++player)
    {
        if (game_screen.viewob[player] == nullptr)
        {
            ADD_FAILURE() << "missing view for player " << player;
            control_ids.push_back(0u);
            continue;
        }
        if (game_screen.viewob[player]->control == nullptr)
        {
            ADD_FAILURE() << "missing control for player " << player;
            control_ids.push_back(0u);
            continue;
        }
        control_ids.push_back(game_screen.viewob[player]->control->entity_id());
    }

    return control_ids;
}

std::vector<short> capture_view_teams(screen& game_screen, int player_count)
{
    std::vector<short> teams;
    teams.reserve(static_cast<std::size_t>(player_count));

    for (int player = 0; player < player_count; ++player)
    {
        if (game_screen.viewob[player] == nullptr)
        {
            ADD_FAILURE() << "missing view for player " << player;
            teams.push_back(0);
            continue;
        }

        teams.push_back(game_screen.viewob[player]->my_team);
    }

    return teams;
}

void poison_replay_input_debounce(screen& game_screen)
{
    InputState input{};
    press_action(input.players[0], InputAction::SwitchSpecial);
    game_screen.process_input(input);
}

og::sim::test::NetworkTestConfig make_replay_fixture_config(
    int player_count,
    const std::vector<short>& player_teams)
{
    og::sim::test::NetworkTestConfig config;
    config.player_count = static_cast<std::size_t>(player_count);
    config.level_id = kReplayLevel;
    config.player_teams = player_teams;
    return config;
}

void initialize_fixture_world(
    og::sim::test::NetworkTestFixture& fixture,
    const og::sim::WorldSnapshot& initial_snapshot)
{
    fixture.load_level();
    fixture.apply_server_snapshot(initial_snapshot);
    fixture.initial_sync();
    fixture.expect_clients_match_server();
}

void expect_fixture_controls_match(
    og::sim::test::NetworkTestFixture& fixture,
    const std::vector<std::uint32_t>& expected_control_ids)
{
    for (std::size_t index = 0; index < expected_control_ids.size(); ++index)
    {
        ASSERT_NE(nullptr, fixture.server_control(index))
            << "fixture should bind a control for player " << index;
        EXPECT_EQ(expected_control_ids[index],
                  fixture.server_control(index)->entity_id())
            << "fixture control binding diverged for player " << index;
    }
}

void reset_loaded_world_for_replay(GameWorld& world)
{
    world.tick_count_ = 0;
    world.reset_level_progress();
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
    if (current_game != nullptr && current_game->sim_events != nullptr)
        current_game->sim_events->clear();
}

std::string format_failure(const og::sim::ReplayVerificationFailure& failure)
{
    return std::format("tick={} field={} expected={} actual={}",
                       failure.tick,
                       failure.field,
                       failure.expected_value,
                       failure.actual_value);
}

void assert_snapshot_bytes_match(std::string_view label,
                                 const og::sim::WorldSnapshot& expected,
                                 const og::sim::WorldSnapshot& actual)
{
    const std::vector<std::uint8_t> expected_bytes =
        og::sim::serialize_snapshot(expected);
    const std::vector<std::uint8_t> actual_bytes =
        og::sim::serialize_snapshot(actual);
    ASSERT_EQ(expected_bytes.size(), actual_bytes.size())
        << label << " byte length diverged";
    ASSERT_TRUE(expected_bytes == actual_bytes) << label << " bytes diverged";
}

void run_replay_roundtrip(int player_count)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    const std::int32_t saved_difficulty =
        og::runtime::current_session->current_difficulty_;
    og::runtime::current_session->current_difficulty_ = 2;
    configure_replay_team(game_screen.save_data, player_count);

    const std::string save_name =
        std::format("test_phase11_replay_{}", player_count);
    ASSERT_TRUE(game_screen.save_data.save(save_name))
        << "replay test save should succeed";

    const std::uint32_t replay_load_seed =
        kReplayLoadSeedBase + static_cast<std::uint32_t>(player_count);
    game_screen.world().rng_.state_ = replay_load_seed;
    ASSERT_TRUE(load_saved_game(save_name.c_str(), &game_screen) != 0)
        << "initial replay load should succeed";

    GameWorld& live_screen_world = game_screen.world();
    reset_loaded_world_for_replay(live_screen_world);
    ASSERT_EQ(kReplayLevel, live_screen_world.id);
    ASSERT_TRUE(live_screen_world.grid.valid());
    ASSERT_EQ(player_count, game_screen.numviews);
    const std::vector<std::uint32_t> expected_control_ids =
        capture_view_control_ids(game_screen, player_count);
    const std::vector<short> player_teams =
        capture_view_teams(game_screen, player_count);
    const og::sim::WorldSnapshot seeded_snapshot =
        og::sim::peek_keyframe_snapshot(live_screen_world);

    og::sim::test::NetworkTestFixture live_fixture(
        make_replay_fixture_config(player_count, player_teams));
    initialize_fixture_world(live_fixture, seeded_snapshot);
    expect_fixture_controls_match(live_fixture, expected_control_ids);

    GameWorld& live_world = live_fixture.server_world();
    ASSERT_EQ(kReplayLevel, live_world.id);
    ASSERT_TRUE(live_world.grid.valid());

    og::sim::ReplayRecorder recorder({
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = replay_load_seed,
        .level_id = live_world.id,
        .player_count = static_cast<std::uint8_t>(player_count),
        .timer_wait = live_world.timer_wait,
        .my_team = game_screen.save_data.my_team,
        .allied_mode = game_screen.save_data.allied_mode,
        .difficulty = live_world.difficulty,
        .campaign_id = game_screen.save_data.current_campaign,
        .campaign_vars = {},
    });
    recorder.record_initial_world(live_world);
    recorder.record_world_keyframe(0u, live_world);
    const og::sim::WorldSnapshot expected_initial_snapshot =
        recorder.checkpoints().front().snapshot;
    std::vector<std::uint32_t> expected_rng_states;
    expected_rng_states.reserve(kReplayTicks);

    InputState input{};
    for (int tick = 0; tick < kReplayTicks; ++tick)
    {
        populate_chaotic_input(input, tick, player_count);
        recorder.record_input(live_world.tick_count_ + 1, input);
        live_fixture.step_tick(input);
        ASSERT_FALSE(live_world.game_ended)
            << "replay test should not end the game early";
        expected_rng_states.push_back(live_world.rng_.state_);

        if (((tick + 1) % kCheckpointInterval) == 0)
            live_fixture.expect_clients_match_server_ignoring_visual_transients();

        if (((tick + 1) % kCheckpointInterval) == 0)
            recorder.record_world_keyframe(live_world.tick_count_, live_world);
    }
    live_fixture.expect_clients_match_server_ignoring_visual_transients();

    const og::sim::WorldSnapshot expected_final_snapshot =
        og::sim::peek_keyframe_snapshot(live_world);

    const std::filesystem::path replay_path =
        std::filesystem::temp_directory_path() /
        std::format("openglad_phase11_replay_{}.ogr", player_count);
    std::error_code ec;
    std::filesystem::remove(replay_path, ec);

    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    ASSERT_TRUE(recorder.write_file(replay_path, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);

    og::sim::ReplayPlayer player;
    ASSERT_TRUE(player.load_file(replay_path, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);
    player.set_checkpoints(recorder.checkpoints());

    EXPECT_EQ(recorder.header().initial_rng_state, player.header().initial_rng_state);
    EXPECT_EQ(recorder.header().level_id, player.header().level_id);
    EXPECT_EQ(recorder.header().player_count, player.header().player_count);
    EXPECT_EQ(recorder.header().timer_wait, player.header().timer_wait);
    EXPECT_EQ(recorder.header().my_team, player.header().my_team);
    EXPECT_EQ(recorder.header().allied_mode, player.header().allied_mode);
    EXPECT_EQ(recorder.header().difficulty, player.header().difficulty);
    EXPECT_EQ(recorder.header().campaign_id, player.header().campaign_id);
    ASSERT_EQ(recorder.frame_count(), player.frame_count());
    assert_snapshot_bytes_match("initial snapshot payload",
                                recorder.initial_snapshot(),
                                player.initial_snapshot());

    ASSERT_EQ(CampaignPackageIoError::None,
              unmount_campaign_package_with_error(get_mounted_campaign()));
    poison_replay_input_debounce(game_screen);
    game_screen.save_data.reset();
    game_screen.save_data.current_campaign = "wrong.campaign";
    game_screen.save_data.scen_num = static_cast<short>(player.header().level_id + 1);
    game_screen.save_data.numplayers = 0;
    game_screen.save_data.my_team = static_cast<short>(player.header().my_team + 1);
    game_screen.save_data.allied_mode = static_cast<short>(1 - player.header().allied_mode);
    ASSERT_EQ(0, game_screen.save_data.team_size);
    game_screen.world().rng_.state_ = 0u;
    game_screen.world().difficulty = 1;
    og::runtime::current_session->current_difficulty_ = 0;
    ASSERT_TRUE(og::runtime::initialize_replay_screen(game_screen, player))
        << "replay runtime should seed RNG and load the replay world";

    GameWorld& replay_screen_world = game_screen.world();
    reset_loaded_world_for_replay(replay_screen_world);
    ASSERT_EQ(player.header().level_id, replay_screen_world.id);
    ASSERT_EQ(player.header().player_count,
              static_cast<std::uint8_t>(game_screen.numviews));
    ASSERT_EQ(player.header().timer_wait, replay_screen_world.timer_wait);
    ASSERT_EQ(player.header().my_team, game_screen.save_data.my_team);
    ASSERT_EQ(player.header().allied_mode, game_screen.save_data.allied_mode);
    ASSERT_EQ(player.header().difficulty, replay_screen_world.difficulty);
    ASSERT_EQ(0, game_screen.save_data.team_size);
    EXPECT_EQ(expected_control_ids,
              capture_view_control_ids(game_screen, player_count));
    EXPECT_EQ(player_teams, capture_view_teams(game_screen, player_count))
        << "replay views must use the recorded controllers' actual teams";

    const og::sim::WorldSnapshot actual_initial_snapshot =
        og::sim::peek_keyframe_snapshot(replay_screen_world);
    assert_snapshot_bytes_match("initial snapshot payload",
                                player.initial_snapshot(),
                                actual_initial_snapshot);
    assert_snapshot_bytes_match("initial checkpoint",
                                expected_initial_snapshot,
                                actual_initial_snapshot);

    if (const std::optional<og::sim::ReplayVerificationFailure> initial_checkpoint_failure =
            player.verify_world(replay_screen_world,
                                replay_screen_world.tick_count_,
                                false);
        initial_checkpoint_failure.has_value())
    {
        FAIL() << "initial checkpoint divergence: "
               << format_failure(*initial_checkpoint_failure);
    }

    og::sim::test::NetworkTestFixture replay_fixture(
        make_replay_fixture_config(player_count, player_teams));
    initialize_fixture_world(replay_fixture, player.initial_snapshot());
    expect_fixture_controls_match(replay_fixture, expected_control_ids);
    GameWorld& replay_world = replay_fixture.server_world();

    std::size_t replay_tick_index = 0;
    while (true)
    {
        const std::optional<og::sim::InputStateMessage> frame = player.next_frame();
        if (!frame.has_value())
            break;

        ASSERT_EQ(replay_world.tick_count_ + 1, frame->tick);
        replay_fixture.step_tick(frame->input);
        ASSERT_FALSE(replay_world.game_ended)
            << "replay playback should not end the game early";
        ASSERT_EQ(frame->tick, replay_world.tick_count_);
        ASSERT_LT(replay_tick_index, expected_rng_states.size());
        EXPECT_EQ(expected_rng_states[replay_tick_index], replay_world.rng_.state_)
            << "rng divergence at replay tick " << replay_tick_index;

        if (((replay_tick_index + 1) % kCheckpointInterval) == 0)
            replay_fixture.expect_clients_match_server_ignoring_visual_transients();

        const std::optional<og::sim::ReplayVerificationFailure> checkpoint_failure =
            player.verify_world(replay_world, replay_world.tick_count_, false);
        if (checkpoint_failure.has_value())
            FAIL() << "checkpoint divergence: " << format_failure(*checkpoint_failure);

        ++replay_tick_index;
    }

    EXPECT_EQ(expected_rng_states.size(), replay_tick_index);
    EXPECT_FALSE(player.first_divergence().has_value());
    replay_fixture.expect_clients_match_server_ignoring_visual_transients();
    const og::sim::WorldSnapshot actual_final_snapshot =
        og::sim::peek_keyframe_snapshot(replay_world);
    assert_snapshot_bytes_match("final snapshot",
                                expected_final_snapshot,
                                actual_final_snapshot);

    replay_screen_world.delete_objects();
    std::filesystem::remove(replay_path, ec);
    og::runtime::current_session->current_difficulty_ = saved_difficulty;
}

} // namespace

TEST(Replay, phase11_roundtrip_matches_final_state_for_single_player)
{
    run_replay_roundtrip(1);
}

TEST(Replay, phase11_roundtrip_matches_final_state_for_two_players)
{
    run_replay_roundtrip(2);
}

TEST(Replay, phase11_roundtrip_matches_final_state_for_four_players)
{
    run_replay_roundtrip(4);
}

TEST(Replay, format_version_19_rejects_v18)
{
    static_assert(og::sim::kReplayFormatVersion == 19);
    std::array<std::uint8_t, og::sim::kReplayHeaderSize> old_header{};
    old_header[0] = static_cast<std::uint8_t>('O');
    old_header[1] = static_cast<std::uint8_t>('G');
    old_header[2] = static_cast<std::uint8_t>('R');
    old_header[3] = static_cast<std::uint8_t>('P');
    old_header[4] = 18;

    og::sim::ReplayIoError error = og::sim::ReplayIoError::None;
    EXPECT_FALSE(og::sim::deserialize_replay(old_header, &error).has_value());
    EXPECT_EQ(og::sim::ReplayIoError::UnsupportedVersion, error);
}

TEST(Replay, initialize_replay_screen_rejects_unsafe_campaign_ids)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    configure_replay_team(game_screen.save_data, 1);

    const std::string save_name = "test_phase11_replay_invalid_campaign";
    ASSERT_TRUE(game_screen.save_data.save(save_name))
        << "replay test save should succeed";

    game_screen.world().rng_.state_ = kReplayLoadSeedBase;
    ASSERT_TRUE(load_saved_game(save_name.c_str(), &game_screen) != 0)
        << "initial replay load should succeed";

    GameWorld& live_world = game_screen.world();
    reset_loaded_world_for_replay(live_world);

    const og::sim::WorldSnapshot initial_snapshot =
        og::sim::peek_keyframe_snapshot(live_world);
    const std::string mounted_before = get_mounted_campaign();
    const std::string save_campaign_before = game_screen.save_data.current_campaign;
    const short level_before = game_screen.save_data.scen_num;

    const std::array<std::string_view, 6> unsafe_ids = {
        "../escape",
        "..\\escape",
        "/tmp/escape",
        "C:\\temp\\escape",
        "org/openglad.gladiator",
        "bad:colon-id",
    };

    for (const std::string_view unsafe_id : unsafe_ids)
    {
        const std::vector<std::uint8_t> bytes = og::sim::serialize_replay(
            {
                .version = og::sim::kReplayFormatVersion,
                .initial_rng_state = live_world.rng_.state_,
                .level_id = live_world.id,
                .player_count = 1,
                .timer_wait = live_world.timer_wait,
                .my_team = game_screen.save_data.my_team,
                .allied_mode = game_screen.save_data.allied_mode,
                .difficulty = live_world.difficulty,
                .campaign_id = std::string(unsafe_id),
                .campaign_vars = {},
            },
            initial_snapshot,
            {});

        og::sim::ReplayPlayer player;
        og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
        ASSERT_TRUE(player.load_bytes(bytes, &io_error))
            << "crafted replay should deserialize before runtime validation";
        ASSERT_EQ(og::sim::ReplayIoError::None, io_error);
        ASSERT_EQ(unsafe_id, player.header().campaign_id);

        EXPECT_FALSE(og::runtime::initialize_replay_screen(game_screen, player))
            << "unsafe campaign id should be rejected: " << unsafe_id;
        EXPECT_EQ(mounted_before, get_mounted_campaign())
            << "unsafe campaign id should not change mounted campaign: "
            << unsafe_id;
        EXPECT_EQ(save_campaign_before, game_screen.save_data.current_campaign)
            << "unsafe campaign id should not mutate save selection: "
            << unsafe_id;
        EXPECT_EQ(level_before, game_screen.save_data.scen_num)
            << "unsafe campaign id should not mutate level selection: "
            << unsafe_id;
    }
}

// v15 campaign vars: begin_replay_recording stamps the world's campaign
// vars into the header, and initialize_replay_screen applies the recorded
// values to the world before the first tick — so a replay of a
// decision-taken run replays the recorded branch on any machine.
TEST(Replay, campaign_vars_stamp_at_record_and_apply_at_playback)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    configure_replay_team(game_screen.save_data, 1);

    const std::string save_name = "test_replay_campaign_vars";
    ASSERT_TRUE(game_screen.save_data.save(save_name));
    game_screen.world().rng_.state_ = kReplayLoadSeedBase;
    ASSERT_TRUE(load_saved_game(save_name.c_str(), &game_screen) != 0);
    reset_loaded_world_for_replay(game_screen.world());

    const std::vector<std::pair<std::string, std::int32_t>> vars = {
        {"delve_counted", 3},
        {"watch_paid", -2},
    };
    game_screen.world().campaign_vars = vars;

    og::runtime::begin_replay_recording(game_screen);
    ASSERT_TRUE(og::runtime::current_session->replay_recorder_.has_value());
    EXPECT_EQ(vars,
              og::runtime::current_session->replay_recorder_->header()
                  .campaign_vars)
        << "record start stamps the world's campaign vars into the header";

    const std::vector<std::uint8_t> bytes =
        og::runtime::current_session->replay_recorder_->serialize();
    og::runtime::current_session->replay_recorder_.reset();

    og::sim::ReplayPlayer player;
    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    ASSERT_TRUE(player.load_bytes(bytes, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);
    EXPECT_EQ(vars, player.header().campaign_vars)
        << "the vars survive the byte round-trip";

    game_screen.world().campaign_vars.clear();
    ASSERT_TRUE(og::runtime::initialize_replay_screen(game_screen, player))
        << "replay runtime should load the recorded world";
    EXPECT_EQ(vars, game_screen.world().campaign_vars)
        << "playback start applies the recorded vars to the world";

    game_screen.world().campaign_vars.clear();
    game_screen.world().delete_objects();
    og::runtime::current_session->replay_playback_active_ = false;
}

// A replay whose recording bound nobody to view 0 (a headless host's
// recording, a controller that was never seated) must still hand the view a
// walker to follow, on the recorded team, acting under player control — a
// replay that opens on an unbound camera shows the viewer nothing at all.
TEST(Replay, unbound_recording_still_gives_view_zero_a_controlled_walker)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    configure_replay_team(game_screen.save_data, 1);

    const std::string save_name = "test_replay_unbound_control";
    ASSERT_TRUE(game_screen.save_data.save(save_name));
    game_screen.world().rng_.state_ = kReplayLoadSeedBase;
    ASSERT_TRUE(load_saved_game(save_name.c_str(), &game_screen) != 0);
    reset_loaded_world_for_replay(game_screen.world());

    og::sim::WorldSnapshot unbound =
        og::sim::peek_keyframe_snapshot(game_screen.world());
    // A recording made with nobody bound to view 0 carries no HUD readout
    // either, so the seeding the view assignment does is the only thing that
    // can put the walker's hit points on screen.
    unbound.control_hp = 0.0f;
    int rebound = 0;
    for (og::sim::EntitySnapshot& entity : unbound.oblist)
    {
        if (entity.user >= 0)
        {
            entity.user = -1;
            ++rebound;
        }
    }
    ASSERT_EQ(1, rebound)
        << "the one-player recording must have had exactly one bound walker "
           "to unbind";

    const std::vector<std::uint8_t> bytes = og::sim::serialize_replay(
        {
            .version = og::sim::kReplayFormatVersion,
            .initial_rng_state = game_screen.world().rng_.state_,
            .level_id = game_screen.world().id,
            .player_count = 1,
            .timer_wait = game_screen.world().timer_wait,
            .my_team = game_screen.save_data.my_team,
            .allied_mode = game_screen.save_data.allied_mode,
            .difficulty = game_screen.world().difficulty,
            .campaign_id = "gladiator",
            .campaign_vars = {},
        },
        unbound,
        {});

    og::sim::ReplayPlayer player;
    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    ASSERT_TRUE(player.load_bytes(bytes, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);

    ASSERT_TRUE(og::runtime::initialize_replay_screen(game_screen, player));

    ASSERT_TRUE(game_screen.viewob[0] != nullptr);
    walker* const control = game_screen.viewob[0]->control;
    ASSERT_NE(nullptr, control)
        << "view 0 must be given a walker even when the recording bound none";
    EXPECT_EQ(0, static_cast<int>(control->user()))
        << "the adopted walker is bound to view 0";
    EXPECT_EQ(ACT_CONTROL, static_cast<int>(control->act_type()))
        << "the adopted walker acts under the player, not the AI";
    EXPECT_EQ(game_screen.world().my_team, game_screen.viewob[0]->my_team)
        << "with no recorded team for the view, the world's own team stands";
    EXPECT_EQ(control->stats()->hitpoints(), game_screen.world().control_hp)
        << "the HUD's hit-point readout seeds from the adopted walker";

    game_screen.world().delete_objects();
    og::runtime::current_session->replay_playback_active_ = false;
    ASSERT_TRUE(prepare_default_level_load());
}

// A spectator recording (nobody seated) follows a walker without ever
// taking it over: the AI keeps acting it, and the HUD still gets its
// hit-point readout. Taking control here would make a spectator's replay
// diverge from the run it recorded.
TEST(Replay, spectator_playback_seeds_the_hud_without_binding_the_walker)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    configure_replay_team(game_screen.save_data, 1);

    const std::string save_name = "test_replay_spectator_seed";
    ASSERT_TRUE(game_screen.save_data.save(save_name));
    game_screen.world().rng_.state_ = kReplayLoadSeedBase;
    ASSERT_TRUE(load_saved_game(save_name.c_str(), &game_screen) != 0);
    reset_loaded_world_for_replay(game_screen.world());

    og::sim::WorldSnapshot unbound =
        og::sim::peek_keyframe_snapshot(game_screen.world());
    for (og::sim::EntitySnapshot& entity : unbound.oblist)
        entity.user = -1;
    // A seatless recording carries no HUD readout; the restored world starts
    // at zero, so a non-zero readout afterwards can only come from the
    // spectator seeding branch.
    unbound.control_hp = 0.0f;

    const std::vector<std::uint8_t> bytes = og::sim::serialize_replay(
        {
            .version = og::sim::kReplayFormatVersion,
            .initial_rng_state = game_screen.world().rng_.state_,
            .level_id = game_screen.world().id,
            .player_count = 0,  // spectator: no seats
            .timer_wait = game_screen.world().timer_wait,
            .my_team = game_screen.save_data.my_team,
            .allied_mode = game_screen.save_data.allied_mode,
            .difficulty = game_screen.world().difficulty,
            .campaign_id = "gladiator",
            .campaign_vars = {},
        },
        unbound,
        {});

    og::sim::ReplayPlayer player;
    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    ASSERT_TRUE(player.load_bytes(bytes, &io_error));
    ASSERT_EQ(og::sim::ReplayIoError::None, io_error);
    ASSERT_EQ(0, static_cast<int>(player.header().player_count));

    ASSERT_TRUE(og::runtime::initialize_replay_screen(game_screen, player));
    EXPECT_EQ(0, static_cast<int>(game_screen.save_data.numplayers))
        << "the recorded seat count is what makes this a spectator replay";

    ASSERT_TRUE(game_screen.viewob[0] != nullptr);
    walker* const control = game_screen.viewob[0]->control;
    ASSERT_NE(nullptr, control) << "a spectator still follows somebody";
    EXPECT_EQ(-1, static_cast<int>(control->user()))
        << "a spectator never takes the walker over";
    EXPECT_EQ(control->stats()->hitpoints(), game_screen.world().control_hp)
        << "the HUD readout is still seeded from the followed walker";

    game_screen.world().delete_objects();
    og::runtime::current_session->replay_playback_active_ = false;
    ASSERT_TRUE(prepare_default_level_load());
}

// Two refusals that must leave the machine exactly where they found it: a
// well-formed campaign id for a campaign this machine does not have
// installed, and a level the mounted campaign does not carry. Neither may
// swap the mount or move the player's level cursor — a failed replay that
// left a half-applied campaign behind would send the next real game to the
// wrong scenario.
TEST(Replay, uninstalled_campaign_and_missing_level_refuse_without_side_effects)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    configure_replay_team(game_screen.save_data, 1);

    const std::string save_name = "test_replay_refusals";
    ASSERT_TRUE(game_screen.save_data.save(save_name));
    game_screen.world().rng_.state_ = kReplayLoadSeedBase;
    ASSERT_TRUE(load_saved_game(save_name.c_str(), &game_screen) != 0);
    reset_loaded_world_for_replay(game_screen.world());

    const og::sim::WorldSnapshot initial_snapshot =
        og::sim::peek_keyframe_snapshot(game_screen.world());
    const short level_before = game_screen.save_data.scen_num;

    auto crafted = [&](std::string_view campaign_id, short level_id) {
        return og::sim::serialize_replay(
            {
                .version = og::sim::kReplayFormatVersion,
                .initial_rng_state = game_screen.world().rng_.state_,
                .level_id = level_id,
                .player_count = 1,
                .timer_wait = game_screen.world().timer_wait,
                .my_team = game_screen.save_data.my_team,
                .allied_mode = game_screen.save_data.allied_mode,
                .difficulty = game_screen.world().difficulty,
                .campaign_id = std::string(campaign_id),
                .campaign_vars = {},
            },
            initial_snapshot,
            {});
    };

    // 1. A safe id (it passes the traversal check) for a campaign that is
    // not installed: the mount refuses and so does the runtime.
    {
        og::sim::ReplayPlayer player;
        og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
        const std::vector<std::uint8_t> bytes =
            crafted("nosuchcampaign", kReplayLevel);
        ASSERT_TRUE(player.load_bytes(bytes, &io_error));
        ASSERT_EQ(og::sim::ReplayIoError::None, io_error);
        ASSERT_EQ("nosuchcampaign", player.header().campaign_id);

        EXPECT_FALSE(og::runtime::initialize_replay_screen(game_screen, player))
            << "a campaign this machine does not have cannot be replayed";
        EXPECT_EQ("", get_mounted_campaign())
            << "a mount that failed leaves nothing mounted";
        EXPECT_EQ(level_before, game_screen.save_data.scen_num)
            << "the refusal returns before the save is rewritten, so the "
               "player's level cursor is untouched";
        EXPECT_EQ("gladiator", game_screen.save_data.current_campaign)
            << "and so is the player's campaign selection";
        ASSERT_TRUE(prepare_default_level_load())
            << "remount for the control arm below";
    }

    // 2. The paired control, then the second refusal: the SAME crafted
    // replay on the installed campaign loads, and the same replay pointed at
    // a level the campaign does not carry is refused by the loader.
    {
        og::sim::ReplayPlayer player;
        og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
        const std::vector<std::uint8_t> bytes =
            crafted("gladiator", kReplayLevel);
        ASSERT_TRUE(player.load_bytes(bytes, &io_error));
        EXPECT_TRUE(og::runtime::initialize_replay_screen(game_screen, player))
            << "the same crafted replay on an installed campaign DOES load";
        EXPECT_EQ(kReplayLevel, game_screen.save_data.scen_num);
        game_screen.world().delete_objects();
        og::runtime::current_session->replay_playback_active_ = false;
    }

    ASSERT_TRUE(prepare_default_level_load());
    {
        og::sim::ReplayPlayer player;
        og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
        const std::vector<std::uint8_t> bytes = crafted("gladiator", 9999);
        ASSERT_TRUE(player.load_bytes(bytes, &io_error));
        ASSERT_EQ(9999, static_cast<int>(player.header().level_id));

        EXPECT_FALSE(og::runtime::initialize_replay_screen(game_screen, player))
            << "a level the campaign does not carry cannot be replayed";
        EXPECT_FALSE(og::runtime::current_session->replay_playback_active_)
            << "a refused load never arms playback";
    }

    game_screen.world().delete_objects();
    og::runtime::current_session->replay_playback_active_ = false;
    ASSERT_TRUE(prepare_default_level_load());
}

// The recorder is armed from the LIVE save's campaign id, so it has to run
// the same traversal check the playback side does: an unsafe id must leave
// the recorder disarmed and no output path claimed, or a recording would be
// written for a campaign that can never be mounted back.
TEST(Replay, unsafe_current_campaign_never_arms_the_recorder)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    configure_replay_team(game_screen.save_data, 1);
    game_screen.save_data.current_campaign = "../escape";

    og::runtime::begin_replay_recording(game_screen);
    EXPECT_FALSE(og::runtime::current_session->replay_recorder_.has_value())
        << "an unsafe campaign id must not arm the recorder";
    EXPECT_TRUE(og::runtime::current_session->replay_output_path_.empty())
        << "and must not claim an output path either";

    // Paired control: the same call on the installed campaign DOES arm.
    game_screen.save_data.current_campaign = "gladiator";
    og::runtime::begin_replay_recording(game_screen);
    ASSERT_TRUE(og::runtime::current_session->replay_recorder_.has_value())
        << "a safe campaign id arms the recorder";
    EXPECT_EQ("gladiator",
              og::runtime::current_session->replay_recorder_->header()
                  .campaign_id);
    EXPECT_FALSE(og::runtime::current_session->replay_output_path_.empty());

    og::runtime::current_session->replay_recorder_.reset();
    og::runtime::current_session->replay_output_path_.clear();
}
