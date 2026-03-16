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
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/resources/io_common.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

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
        "org.openglad.gladiator";
    return mount_campaign_package_with_error("org.openglad.gladiator") ==
           CampaignPackageIoError::None;
}

void configure_replay_team(SaveData& save, int player_count)
{
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = kReplayLevel;
    save.scen_num = kReplayLevel;
    save.my_team = 1;
    save.numplayers = static_cast<unsigned char>(player_count);
    save.allied_mode = 1;
    save.team_size = 0;

    constexpr std::array<int, 4> families = {
        FAMILY_ARCHMAGE,
        FAMILY_ARCHER,
        FAMILY_THIEF,
        FAMILY_CLERIC,
    };
    constexpr std::array<short, 4> levels = {10, 10, 10, 10};
    constexpr std::array<short, 4> teams = {1, 2, 3, 4};
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

void poison_replay_input_debounce(screen& game_screen)
{
    InputState input{};
    press_action(input.players[0], InputAction::SwitchSpecial);
    game_screen.process_input(input);
}

void reset_network_shadow(screen& game_screen)
{
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(game_screen.save_data.save("save0"))
        << "local transport shadow bootstrap save should succeed";
    og::runtime::reset_local_transport_shadow(
        *og::runtime::current_game_session,
        game_screen);
    ASSERT_TRUE(og::runtime::local_transport_active(
        *og::runtime::current_game_session));
}

void advance_network_tick(screen& game_screen, const InputState& input)
{
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(og::runtime::local_transport_active(
        *og::runtime::current_game_session));
    og::runtime::local_transport_shadow_send_input(
        *og::runtime::current_game_session,
        input,
        game_screen.world().tick_count_ + 1);
    game_screen.process_input(input);
    game_screen.continuous_input();
    og::runtime::local_transport_shadow_finish_tick(
        *og::runtime::current_game_session);
    ASSERT_FALSE(game_screen.world().game_ended)
        << "replay test should not end the game early";
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

    GameWorld& live_world = game_screen.world();
    reset_loaded_world_for_replay(live_world);
    reset_network_shadow(game_screen);
    ASSERT_EQ(kReplayLevel, live_world.id);
    ASSERT_TRUE(live_world.grid.valid());
    ASSERT_EQ(player_count, game_screen.numviews);
    const std::vector<std::uint32_t> expected_control_ids =
        capture_view_control_ids(game_screen, player_count);

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
        advance_network_tick(game_screen, input);
        expected_rng_states.push_back(live_world.rng_.state_);

        if (((tick + 1) % kCheckpointInterval) == 0)
            recorder.record_world_keyframe(live_world.tick_count_, live_world);
    }

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

    GameWorld& replay_world = game_screen.world();
    reset_loaded_world_for_replay(replay_world);
    reset_network_shadow(game_screen);
    ASSERT_EQ(player.header().level_id, replay_world.id);
    ASSERT_EQ(player.header().player_count, static_cast<std::uint8_t>(game_screen.numviews));
    ASSERT_EQ(player.header().timer_wait, replay_world.timer_wait);
    ASSERT_EQ(player.header().my_team, game_screen.save_data.my_team);
    ASSERT_EQ(player.header().allied_mode, game_screen.save_data.allied_mode);
    ASSERT_EQ(player.header().difficulty, replay_world.difficulty);
    ASSERT_EQ(0, game_screen.save_data.team_size);
    EXPECT_EQ(expected_control_ids,
              capture_view_control_ids(game_screen, player_count));

    const og::sim::WorldSnapshot actual_initial_snapshot =
        og::sim::peek_keyframe_snapshot(replay_world);
    assert_snapshot_bytes_match("initial snapshot payload",
                                player.initial_snapshot(),
                                actual_initial_snapshot);
    assert_snapshot_bytes_match("initial checkpoint",
                                expected_initial_snapshot,
                                actual_initial_snapshot);

    if (const std::optional<og::sim::ReplayVerificationFailure> initial_checkpoint_failure =
            player.verify_world(replay_world, replay_world.tick_count_, false);
        initial_checkpoint_failure.has_value())
    {
        FAIL() << "initial checkpoint divergence: "
               << format_failure(*initial_checkpoint_failure);
    }

    std::size_t replay_tick_index = 0;
    while (true)
    {
        const std::optional<og::sim::InputStateMessage> frame = player.next_frame();
        if (!frame.has_value())
            break;

        ASSERT_EQ(replay_world.tick_count_ + 1, frame->tick);
        advance_network_tick(game_screen, frame->input);
        ASSERT_EQ(frame->tick, replay_world.tick_count_);
        ASSERT_LT(replay_tick_index, expected_rng_states.size());
        EXPECT_EQ(expected_rng_states[replay_tick_index], replay_world.rng_.state_)
            << "rng divergence at replay tick " << replay_tick_index;

        const std::optional<og::sim::ReplayVerificationFailure> checkpoint_failure =
            player.verify_world(replay_world, replay_world.tick_count_, false);
        if (checkpoint_failure.has_value())
            FAIL() << "checkpoint divergence: " << format_failure(*checkpoint_failure);

        ++replay_tick_index;
    }

    EXPECT_EQ(expected_rng_states.size(), replay_tick_index);
    EXPECT_FALSE(player.first_divergence().has_value());

    const og::sim::WorldSnapshot actual_final_snapshot =
        og::sim::peek_keyframe_snapshot(replay_world);
    assert_snapshot_bytes_match("final snapshot",
                                expected_final_snapshot,
                                actual_final_snapshot);

    replay_world.delete_objects();
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
        "org.openglad:gladiator",
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
