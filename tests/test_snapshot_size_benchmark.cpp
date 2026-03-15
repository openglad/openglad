#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/io_common.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

#include "zlib.h"

short load_saved_game(const char* filename, screen* scr);

namespace {

constexpr short kBenchmarkLevel = 30;
constexpr int kWarmupTicks = 100;
constexpr int kCatchupDeltaTicks = 10;
constexpr std::string_view kBenchmarkSaveName = "test_snapshot_size_benchmark";

struct PayloadSizeReport {
    std::size_t raw_payload_bytes = 0;
    std::size_t zlib_payload_bytes = 0;
    std::size_t wire_payload_bytes = 0;
    std::size_t wire_message_bytes = 0;
    bool wire_payload_uncompressed = false;
};

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

void configure_benchmark_team(SaveData& save)
{
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = kBenchmarkLevel;
    save.scen_num = kBenchmarkLevel;
    save.my_team = 1;
    save.numplayers = 4;
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
        "BENCH_ARCHMAGE",
        "BENCH_ARCHER",
        "BENCH_THIEF",
        "BENCH_CLERIC",
    };

    for (std::size_t i = 0; i < families.size(); ++i)
    {
        auto recruit = std::make_unique<guy>(families[i]);
        recruit->name = std::string(names[i]);
        recruit->teamnum = teams[i];
        recruit->upgrade_to_level(levels[i]);
        save.team_list[i] = std::move(recruit);
    }

    save.team_size = static_cast<unsigned char>(families.size());
}

void populate_chaotic_input(InputState& input, int tick)
{
    input.clear();

    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        PlayerInput& pi = input.players[player];
        const int direction = ((tick / (3 + (player % 2))) + (player * 2)) % 8;
        apply_direction(pi, direction);

        const bool fire_active = ((tick + player) % 3) != 0;
        if (fire_active)
            hold_action(pi, InputAction::Fire);
        if (((tick + player) % 6) == 0)
            press_action(pi, InputAction::Fire);

        const int special_phase = (tick + player * 3) % 11;
        const bool special_active = special_phase < 2;
        if (special_active)
            hold_action(pi, InputAction::Special);
        if (special_phase == 0)
            press_action(pi, InputAction::Special);

        if (special_active && (((tick / 11) + player) % 2) == 1)
            hold_action(pi, InputAction::Shift);

        if (((tick + player * 5) % 19) == 0)
            press_action(pi, InputAction::SwitchSpecial);

        if (((tick + player * 7) % 31) == 0)
            press_action(pi, InputAction::Yell);
    }
}

std::vector<std::uint8_t> zlib_compress_for_benchmark(
    const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> compressed(
        compressBound(static_cast<uLong>(payload.size())));
    uLongf compressed_size = static_cast<uLongf>(compressed.size());
    const int rc = compress2(compressed.data(),
                             &compressed_size,
                             payload.data(),
                             static_cast<uLong>(payload.size()),
                             Z_DEFAULT_COMPRESSION);
    EXPECT_EQ(Z_OK, rc);
    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

std::vector<std::uint8_t> zlib_decompress_for_benchmark(
    const std::uint8_t* data,
    std::size_t size)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    EXPECT_EQ(Z_OK, inflateInit(&stream));

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 256> chunk{};
    int rc = Z_OK;
    do
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&stream, Z_NO_FLUSH);
        EXPECT_TRUE(rc == Z_OK || rc == Z_STREAM_END);
        output.insert(output.end(),
                      chunk.begin(),
                      chunk.begin() + (chunk.size() - stream.avail_out));
    } while (rc != Z_STREAM_END);

    EXPECT_EQ(Z_OK, inflateEnd(&stream));
    return output;
}

std::size_t payload_length_from_header(const std::vector<std::uint8_t>& bytes)
{
    return static_cast<std::size_t>(bytes[2]) |
           (static_cast<std::size_t>(bytes[3]) << 8);
}

PayloadSizeReport measure_full_snapshot_payload(const og::sim::WorldSnapshot& snapshot)
{
    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    EXPECT_GE(bytes.size(), 8u);

    const std::size_t payload_length = payload_length_from_header(bytes);
    EXPECT_EQ(bytes.size(), payload_length + 8u);

    const std::vector<std::uint8_t> raw_payload =
        zlib_decompress_for_benchmark(bytes.data() + 8, payload_length);

    PayloadSizeReport report;
    report.raw_payload_bytes = raw_payload.size();
    report.zlib_payload_bytes = payload_length;
    report.wire_payload_bytes = payload_length;
    report.wire_message_bytes = bytes.size();
    report.wire_payload_uncompressed = false;
    return report;
}

PayloadSizeReport measure_delta_payload(const og::sim::WorldSnapshot& delta)
{
    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    EXPECT_GE(bytes.size(), 8u);

    const std::size_t payload_length = payload_length_from_header(bytes);
    EXPECT_EQ(bytes.size(), payload_length + 8u);

    const bool payload_is_uncompressed =
        (bytes[1] & og::sim::kDeltaPayloadUncompressedFlag) != 0;
    const std::vector<std::uint8_t> raw_payload =
        payload_is_uncompressed
            ? std::vector<std::uint8_t>(bytes.begin() + 8, bytes.end())
            : zlib_decompress_for_benchmark(bytes.data() + 8, payload_length);
    const std::vector<std::uint8_t> compressed_payload =
        zlib_compress_for_benchmark(raw_payload);

    PayloadSizeReport report;
    report.raw_payload_bytes = raw_payload.size();
    report.zlib_payload_bytes = compressed_payload.size();
    report.wire_payload_bytes = payload_length;
    report.wire_message_bytes = bytes.size();
    report.wire_payload_uncompressed = payload_is_uncompressed;
    return report;
}

double compression_percent(std::size_t raw_bytes, std::size_t compressed_bytes)
{
    if (raw_bytes == 0)
        return 0.0;

    const double ratio =
        static_cast<double>(compressed_bytes) / static_cast<double>(raw_bytes);
    return 100.0 * (1.0 - ratio);
}

} // namespace

TEST(SnapshotSizeBenchmark,
     phase10_reports_level30_snapshot_and_delta_sizes_under_chaotic_load)
{
    ASSERT_TRUE(prepare_default_level_load())
        << "default campaign should be restored and mounted before snapshot benchmark";

    screen& game_screen = *og::runtime::current_session->myscreen_;
    configure_benchmark_team(game_screen.save_data);

    ASSERT_TRUE(game_screen.save_data.save(std::string(kBenchmarkSaveName)))
        << "benchmark save should succeed";
    ASSERT_TRUE(load_saved_game(kBenchmarkSaveName.data(), &game_screen) != 0)
        << "load_saved_game should succeed for snapshot benchmark";

    GameWorld& world = game_screen.world();
    ASSERT_EQ(kBenchmarkLevel, world.id);
    ASSERT_TRUE(world.grid.valid()) << "benchmark level should have a valid grid";
    ASSERT_EQ(4, game_screen.numviews);
    for (int i = 0; i < 4; ++i)
        ASSERT_NE(nullptr, game_screen.viewob[i]);

    InputState input;
    for (int tick = 0; tick < kWarmupTicks; ++tick)
    {
        populate_chaotic_input(input, tick);
        game_screen.process_input(input);
        ASSERT_TRUE(game_screen.act()) << "screen.act should continue running";
    }

    const og::sim::WorldSnapshot live_snapshot = og::sim::capture_snapshot(world);
    // Measure the full snapshot from a same-tick keyframe because the full
    // transport message always serializes keyframe/full-grid state.
    const og::sim::WorldSnapshot keyframe_snapshot =
        og::sim::capture_keyframe_snapshot(world);
    const PayloadSizeReport full_sizes =
        measure_full_snapshot_payload(keyframe_snapshot);

    EXPECT_EQ(live_snapshot.oblist.size(), keyframe_snapshot.oblist.size());
    EXPECT_EQ(live_snapshot.fxlist.size(), keyframe_snapshot.fxlist.size());
    EXPECT_EQ(live_snapshot.weaplist.size(), keyframe_snapshot.weaplist.size());

    og::sim::PerClientState one_tick_client_state;
    og::sim::PerClientState catchup_client_state;
    og::sim::seed_client_snapshot_baseline(one_tick_client_state, keyframe_snapshot);
    og::sim::seed_client_snapshot_baseline(catchup_client_state, keyframe_snapshot);

    og::sim::WorldSnapshot one_tick_delta_snapshot;
    og::sim::WorldSnapshot final_tick_snapshot;
    for (int tick = 0; tick < kCatchupDeltaTicks; ++tick)
    {
        populate_chaotic_input(input, kWarmupTicks + tick);
        game_screen.process_input(input);
        ASSERT_TRUE(game_screen.act()) << "screen.act should continue during delta window";

        final_tick_snapshot = og::sim::capture_snapshot(world);
        if (tick == 0)
        {
            og::sim::accumulate_snapshot_for_client(one_tick_client_state,
                                                    final_tick_snapshot);
            one_tick_delta_snapshot =
                og::sim::consume_delta_snapshot_for_client(one_tick_client_state,
                                                           final_tick_snapshot);
        }

        og::sim::accumulate_snapshot_for_client(catchup_client_state,
                                                final_tick_snapshot);
    }

    const og::sim::WorldSnapshot catchup_delta_snapshot =
        og::sim::consume_delta_snapshot_for_client(catchup_client_state,
                                                   final_tick_snapshot);
    const PayloadSizeReport one_tick_delta_sizes =
        measure_delta_payload(one_tick_delta_snapshot);
    const PayloadSizeReport catchup_delta_sizes =
        measure_delta_payload(catchup_delta_snapshot);

    const std::size_t ob_count = live_snapshot.oblist.size();
    const std::size_t fx_count = live_snapshot.fxlist.size();
    const std::size_t weap_count = live_snapshot.weaplist.size();
    const std::size_t total_entities = ob_count + fx_count + weap_count;
    const std::size_t one_tick_delta_entity_count =
        one_tick_delta_snapshot.oblist.size() + one_tick_delta_snapshot.fxlist.size() +
        one_tick_delta_snapshot.weaplist.size();
    const std::size_t catchup_delta_entity_count =
        catchup_delta_snapshot.oblist.size() + catchup_delta_snapshot.fxlist.size() +
        catchup_delta_snapshot.weaplist.size();

    std::printf(
        "\nPhase 10 snapshot benchmark\n"
        "Scenario: level %d - %s\n"
        "Ticks: warmup=%d catchup_delta=%d\n"
        "Entities: ob=%zu fx=%zu weap=%zu total=%zu\n"
        "EntitySnapshot raw size: %zu bytes\n"
        "Full snapshot: raw=%zu bytes zlib=%zu bytes (%.1f%% saved) wire=%zu bytes\n"
        "1-tick delta: raw=%zu bytes zlib=%zu bytes (%.1f%% saved) wire=%zu bytes%s\n"
        "1-tick delta entities: ob=%zu fx=%zu weap=%zu removed=%zu\n"
        "%d-tick catch-up delta: raw=%zu bytes zlib=%zu bytes (%.1f%% saved) wire=%zu bytes%s\n"
        "%d-tick catch-up entities: ob=%zu fx=%zu weap=%zu removed=%zu\n",
        kBenchmarkLevel,
        world.title.c_str(),
        kWarmupTicks,
        kCatchupDeltaTicks,
        ob_count,
        fx_count,
        weap_count,
        total_entities,
        sizeof(og::sim::EntitySnapshot),
        full_sizes.raw_payload_bytes,
        full_sizes.zlib_payload_bytes,
        compression_percent(full_sizes.raw_payload_bytes,
                            full_sizes.zlib_payload_bytes),
        full_sizes.wire_message_bytes,
        one_tick_delta_sizes.raw_payload_bytes,
        one_tick_delta_sizes.zlib_payload_bytes,
        compression_percent(one_tick_delta_sizes.raw_payload_bytes,
                            one_tick_delta_sizes.zlib_payload_bytes),
        one_tick_delta_sizes.wire_message_bytes,
        one_tick_delta_sizes.wire_payload_uncompressed ? " (wire payload left uncompressed)" : "",
        one_tick_delta_snapshot.oblist.size(),
        one_tick_delta_snapshot.fxlist.size(),
        one_tick_delta_snapshot.weaplist.size(),
        one_tick_delta_snapshot.removed_entity_ids.size(),
        kCatchupDeltaTicks,
        catchup_delta_sizes.raw_payload_bytes,
        catchup_delta_sizes.zlib_payload_bytes,
        compression_percent(catchup_delta_sizes.raw_payload_bytes,
                            catchup_delta_sizes.zlib_payload_bytes),
        catchup_delta_sizes.wire_message_bytes,
        catchup_delta_sizes.wire_payload_uncompressed ? " (wire payload left uncompressed)" : "",
        kCatchupDeltaTicks,
        catchup_delta_snapshot.oblist.size(),
        catchup_delta_snapshot.fxlist.size(),
        catchup_delta_snapshot.weaplist.size(),
        catchup_delta_snapshot.removed_entity_ids.size());

    EXPECT_GE(total_entities, 250u);
    EXPECT_GT(full_sizes.raw_payload_bytes, 0u);
    EXPECT_GT(full_sizes.zlib_payload_bytes, 0u);
    EXPECT_GT(one_tick_delta_sizes.raw_payload_bytes, 0u);
    EXPECT_GT(catchup_delta_sizes.raw_payload_bytes, 0u);
    EXPECT_GT(one_tick_delta_entity_count + one_tick_delta_snapshot.removed_entity_ids.size(),
              0u);
    EXPECT_GT(catchup_delta_entity_count + catchup_delta_snapshot.removed_entity_ids.size(),
              0u);
    EXPECT_LT(one_tick_delta_sizes.raw_payload_bytes, full_sizes.raw_payload_bytes);
    EXPECT_LT(catchup_delta_sizes.raw_payload_bytes, full_sizes.raw_payload_bytes);
    EXPECT_GE(catchup_delta_sizes.raw_payload_bytes,
              one_tick_delta_sizes.raw_payload_bytes);

    world.delete_objects();
}
