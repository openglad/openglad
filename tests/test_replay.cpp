#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
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

void reset_screen_replay_state(screen& game_screen, int player_count)
{
    reset_viewscreen_input_debounce();
    for (int player = 0; player < player_count; ++player)
    {
        if (game_screen.viewob[player] == nullptr)
        {
            ADD_FAILURE() << "missing view for player " << player;
            continue;
        }
        game_screen.viewob[player]->control = nullptr;
    }
}

void advance_screen_tick(screen& game_screen, const InputState& input)
{
    game_screen.process_input(input);
    ASSERT_TRUE(game_screen.act()) << "replay test act() should continue";
    ASSERT_FALSE(game_screen.world().game_ended)
        << "replay test should not end the game early";
}

void reset_loaded_world_for_replay(GameWorld& world)
{
    world.tick_count_ = 0;
    world.reset_level_progress();
    if (current_game != nullptr && current_game->sim_events != nullptr)
        current_game->sim_events->clear();
}

void apply_snapshot_for_replay(GameWorld& world, const og::sim::WorldSnapshot& snapshot)
{
    GameplayContext* const saved_context = current_game;
    current_game = nullptr;
    og::sim::apply_snapshot(world, snapshot);
    current_game = saved_context;
}

void strip_bookkeeping_for_final_compare(og::sim::WorldSnapshot& snapshot)
{
    snapshot.grid_dirty = false;
    snapshot.grid_full_resend = false;
    snapshot.full_grid_data.clear();
    snapshot.grid_dirty_tiles.clear();
    snapshot.removed_entity_ids.clear();

    const auto strip_entity_list = [](std::vector<og::sim::EntitySnapshot>& entities) {
        for (auto& entity : entities)
        {
            entity.path_check_counter = 0;
            entity.regen_delay = 0;
        }
    };
    strip_entity_list(snapshot.oblist);
    strip_entity_list(snapshot.fxlist);
    strip_entity_list(snapshot.weaplist);
}

std::string format_failure(const og::sim::ReplayVerificationFailure& failure)
{
    return std::format("tick={} field={} expected={} actual={}",
                       failure.tick,
                       failure.field,
                       failure.expected_value,
                       failure.actual_value);
}

void run_replay_roundtrip(int player_count)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored before replay test";

    screen& game_screen = *og::runtime::current_session->myscreen_;
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
    ASSERT_EQ(kReplayLevel, live_world.id);
    ASSERT_TRUE(live_world.grid.valid());
    ASSERT_EQ(player_count, game_screen.numviews);

    og::sim::ReplayRecorder recorder({
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = replay_load_seed,
        .level_id = live_world.id,
        .player_count = static_cast<std::uint8_t>(player_count),
        .timer_wait = live_world.timer_wait,
    });
    recorder.record_world_keyframe(0u, live_world);
    const og::sim::WorldSnapshot expected_initial_snapshot =
        recorder.checkpoints().front().snapshot;

    reset_screen_replay_state(game_screen, player_count);
    std::vector<std::uint32_t> expected_rng_states;
    expected_rng_states.reserve(kReplayTicks);

    InputState input{};
    for (int tick = 0; tick < kReplayTicks; ++tick)
    {
        populate_chaotic_input(input, tick, player_count);
        recorder.record_input(live_world.tick_count_ + 1, input);
        advance_screen_tick(game_screen, input);
        expected_rng_states.push_back(live_world.rng_.state_);

        if (((tick + 1) % kCheckpointInterval) == 0)
            recorder.record_world_keyframe(live_world.tick_count_, live_world);
    }

    const og::sim::WorldSnapshot expected_final_snapshot =
        og::sim::capture_keyframe_snapshot(live_world);

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
    ASSERT_EQ(recorder.frame_count(), player.frame_count());

    game_screen.world().rng_.state_ = player.header().initial_rng_state;
    ASSERT_TRUE(load_saved_game(save_name.c_str(), &game_screen) != 0)
        << "playback replay load should succeed";

    GameWorld& replay_world = game_screen.world();
    reset_loaded_world_for_replay(replay_world);
    ASSERT_EQ(player.header().level_id, replay_world.id);
    apply_snapshot_for_replay(replay_world, expected_initial_snapshot);

    reset_screen_replay_state(game_screen, player_count);
    std::size_t replay_tick_index = 0;
    while (true)
    {
        const std::optional<og::sim::InputStateMessage> frame = player.next_frame();
        if (!frame.has_value())
            break;

        ASSERT_EQ(replay_world.tick_count_ + 1, frame->tick);
        advance_screen_tick(game_screen, frame->input);
        ASSERT_EQ(frame->tick, replay_world.tick_count_);
        ASSERT_LT(replay_tick_index, expected_rng_states.size());
        EXPECT_EQ(expected_rng_states[replay_tick_index], replay_world.rng_.state_)
            << "rng divergence at replay tick " << replay_tick_index;

        ++replay_tick_index;
    }

    EXPECT_EQ(expected_rng_states.size(), replay_tick_index);

    og::sim::WorldSnapshot actual_final_snapshot =
        og::sim::capture_keyframe_snapshot(replay_world);
    og::sim::WorldSnapshot expected_final_snapshot_for_compare = expected_final_snapshot;
    strip_bookkeeping_for_final_compare(expected_final_snapshot_for_compare);
    strip_bookkeeping_for_final_compare(actual_final_snapshot);
    const std::optional<og::sim::ReplayVerificationFailure> final_failure =
        og::sim::find_first_snapshot_difference(actual_final_snapshot.tick_count,
                                                expected_final_snapshot_for_compare,
                                                actual_final_snapshot,
                                                false);
    if (final_failure.has_value())
        FAIL() << "final snapshot divergence: " << format_failure(*final_failure);

    replay_world.delete_objects();
    std::filesystem::remove(replay_path, ec);
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
