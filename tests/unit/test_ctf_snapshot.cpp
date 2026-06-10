// CTF snapshot replication: capture/apply round trips, wire serialization in
// keyframe and delta form, hostile-input count caps, and the snapshot-restore
// equivalence proof that the replicated CtfState is the complete
// sim-affecting state for a running match.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>

#include "test_game_world_fixture.h"
#include "zlib.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

loader& ctf_snapshot_test_loader()
{
    static loader instance{EntityFactory{}};
    static const bool registered = [] {
        register_ctf_loader_entries(instance);
        return true;
    }();
    (void)registered;
    return instance;
}

// TestGameWorld wired to a loader carrying the CTF treasure entries so flag
// and control-point spawns (and snapshot-applied entities) run the production
// entity factory path.
struct CtfWorld : TestGameWorld
{
    explicit CtfWorld(int level_id = 500)
        : TestGameWorld(level_id)
    {
        loader* game_loader = &ctf_snapshot_test_loader();
        world().entity_factory =
            [game_loader](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
        world().type = GameWorld::TYPE_CTF;
    }

    walker* spawn_flag(int team, int x, int y)
    {
        walker* flag = world().add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
        if (flag == nullptr)
            return nullptr;
        flag->setxy(x, y);
        flag->set_team_num(static_cast<unsigned char>(team));
        return flag;
    }

    walker* spawn_point(int x, int y)
    {
        walker* point = world().add_fx_ob(Order::Treasure, og::FAMILY_CTF_POINT);
        if (point == nullptr)
            return nullptr;
        point->setxy(x, y);
        return point;
    }

    walker* spawn_anchor(int team, int x, int y)
    {
        walker* marker = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr)
            return nullptr;
        marker->setxy(x, y);
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }

    walker* spawn_living(int family, int team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(x, y);
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL);
        return w;
    }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }
};

// Fills every CtfState field (full anchors, control points, and respawn
// queue) plus the requested config shorts with distinct nonzero values.
void populate_full_ctf_state(GameWorld& world)
{
    og::sim::CtfState& ctf = world.ctf;
    ctf.active = true;
    ctf.init_attempted = true;
    ctf.team_count = 4;
    ctf.capture_limit = 7;
    ctf.respawn_ticks = 90;
    ctf.flag_return_ticks = 240;
    ctf.time_limit_ticks = 7200;
    ctf.winner_team = 2;
    ctf.winner_is_player = true;
    ctf.respawn_serial = 13;
    for (int t = 0; t < 4; ++t)
    {
        ctf.captures[t] = static_cast<std::uint16_t>(10 + t);
        ctf.team_active[t] = true;

        og::sim::CtfFlag& f = ctf.flags[t];
        f.state = static_cast<og::sim::CtfFlagState>(t % 3);
        f.carrier_entity_id = static_cast<std::uint32_t>(100 + t);
        f.x = static_cast<std::int16_t>(10 + t);
        f.y = static_cast<std::int16_t>(20 + t);
        f.home_x = static_cast<std::int16_t>(30 + t);
        f.home_y = static_cast<std::int16_t>(40 + t);
        f.return_ticks = static_cast<std::uint16_t>(50 + t);
        f.flag_entity_id = static_cast<std::uint32_t>(200 + t);
        f.present = true;

        ctf.anchor_count[t] = og::sim::kCtfMaxAnchorsPerTeam;
        for (int i = 0; i < og::sim::kCtfMaxAnchorsPerTeam; ++i)
        {
            ctf.anchor_x[t][i] = static_cast<std::int16_t>(t * 100 + i + 1);
            ctf.anchor_y[t][i] = static_cast<std::int16_t>(t * 100 + i + 2);
        }
    }
    ctf.cp_count = og::sim::kCtfMaxControlPoints;
    for (int i = 0; i < og::sim::kCtfMaxControlPoints; ++i)
    {
        og::sim::CtfControlPoint& cp = ctf.cps[i];
        cp.owner = static_cast<std::int8_t>(i);
        cp.progress = static_cast<std::int16_t>(5 + i);
        cp.progress_team = static_cast<std::int8_t>(3 - i);
        cp.x = static_cast<std::int16_t>(300 + i);
        cp.y = static_cast<std::int16_t>(400 + i);
        cp.radius_tiles = static_cast<std::uint8_t>(2 + i);
        cp.entity_id = static_cast<std::uint32_t>(300 + i);
        cp.next_pulse_tick = static_cast<std::uint32_t>(1000 + i);
    }
    for (int i = 0; i < og::sim::kCtfMaxRespawnEntries; ++i)
    {
        og::sim::CtfRespawnEntry entry;
        entry.kind = static_cast<std::uint8_t>(i % 2);
        entry.team = static_cast<std::uint8_t>(i % 4);
        entry.family = static_cast<std::uint8_t>((i % 6) + 1);
        entry.level = static_cast<std::uint8_t>((i % 9) + 1);
        entry.ticks_left = static_cast<std::uint16_t>(100 + i);
        entry.walker_entity_id = static_cast<std::uint32_t>(1000 + i);
        ctf.respawn_queue.push_back(entry);
    }
    world.ctf_requested_team_count = 4;
    world.ctf_requested_capture_limit = 9;
    world.ctf_requested_respawn_ticks = 55;
}

// Asserts every snapshot CTF field equals the world's live state. The
// populated test state carries full counts, so capture normalization is the
// identity and a strict field-by-field comparison is exact.
void expect_snapshot_matches_world(const og::sim::WorldSnapshot& snapshot,
                                   const GameWorld& world)
{
    const og::sim::CtfState& ctf = world.ctf;
    EXPECT_EQ(ctf.active, snapshot.ctf_active);
    EXPECT_EQ(ctf.init_attempted, snapshot.ctf_init_attempted);
    EXPECT_EQ(ctf.team_count, snapshot.ctf_team_count);
    EXPECT_EQ(ctf.capture_limit, snapshot.ctf_capture_limit);
    EXPECT_EQ(ctf.respawn_ticks, snapshot.ctf_respawn_ticks);
    EXPECT_EQ(ctf.flag_return_ticks, snapshot.ctf_flag_return_ticks);
    EXPECT_EQ(ctf.time_limit_ticks, snapshot.ctf_time_limit_ticks);
    EXPECT_EQ(ctf.winner_team, snapshot.ctf_winner_team);
    EXPECT_EQ(ctf.winner_is_player, snapshot.ctf_winner_is_player);
    EXPECT_EQ(ctf.respawn_serial, snapshot.ctf_respawn_serial);
    for (int t = 0; t < 4; ++t)
    {
        EXPECT_EQ(ctf.captures[t], snapshot.ctf_captures[t]) << "team " << t;
        EXPECT_EQ(ctf.team_active[t], snapshot.ctf_team_active[t]) << "team " << t;

        const og::sim::CtfFlag& expected = ctf.flags[t];
        const og::sim::CtfFlag& actual = snapshot.ctf_flags[t];
        EXPECT_EQ(expected.state, actual.state) << "flag " << t;
        EXPECT_EQ(expected.carrier_entity_id, actual.carrier_entity_id) << "flag " << t;
        EXPECT_EQ(expected.x, actual.x) << "flag " << t;
        EXPECT_EQ(expected.y, actual.y) << "flag " << t;
        EXPECT_EQ(expected.home_x, actual.home_x) << "flag " << t;
        EXPECT_EQ(expected.home_y, actual.home_y) << "flag " << t;
        EXPECT_EQ(expected.return_ticks, actual.return_ticks) << "flag " << t;
        EXPECT_EQ(expected.flag_entity_id, actual.flag_entity_id) << "flag " << t;
        EXPECT_EQ(expected.present, actual.present) << "flag " << t;

        EXPECT_EQ(ctf.anchor_count[t], snapshot.ctf_anchor_count[t]) << "team " << t;
        for (int i = 0; i < ctf.anchor_count[t]; ++i)
        {
            EXPECT_EQ(ctf.anchor_x[t][i], snapshot.ctf_anchor_x[t][i])
                << "anchor " << t << "/" << i;
            EXPECT_EQ(ctf.anchor_y[t][i], snapshot.ctf_anchor_y[t][i])
                << "anchor " << t << "/" << i;
        }
    }
    EXPECT_EQ(ctf.cp_count, snapshot.ctf_cp_count);
    for (int i = 0; i < ctf.cp_count; ++i)
    {
        const og::sim::CtfControlPoint& expected = ctf.cps[i];
        const og::sim::CtfControlPoint& actual = snapshot.ctf_cps[i];
        EXPECT_EQ(expected.owner, actual.owner) << "cp " << i;
        EXPECT_EQ(expected.progress, actual.progress) << "cp " << i;
        EXPECT_EQ(expected.progress_team, actual.progress_team) << "cp " << i;
        EXPECT_EQ(expected.x, actual.x) << "cp " << i;
        EXPECT_EQ(expected.y, actual.y) << "cp " << i;
        EXPECT_EQ(expected.radius_tiles, actual.radius_tiles) << "cp " << i;
        EXPECT_EQ(expected.entity_id, actual.entity_id) << "cp " << i;
        EXPECT_EQ(expected.next_pulse_tick, actual.next_pulse_tick) << "cp " << i;
    }
    ASSERT_EQ(ctf.respawn_queue.size(), snapshot.ctf_respawn_queue.size());
    for (std::size_t i = 0; i < ctf.respawn_queue.size(); ++i)
    {
        const og::sim::CtfRespawnEntry& expected = ctf.respawn_queue[i];
        const og::sim::CtfRespawnEntry& actual = snapshot.ctf_respawn_queue[i];
        EXPECT_EQ(expected.kind, actual.kind) << "entry " << i;
        EXPECT_EQ(expected.team, actual.team) << "entry " << i;
        EXPECT_EQ(expected.family, actual.family) << "entry " << i;
        EXPECT_EQ(expected.level, actual.level) << "entry " << i;
        EXPECT_EQ(expected.ticks_left, actual.ticks_left) << "entry " << i;
        EXPECT_EQ(expected.walker_entity_id, actual.walker_entity_id)
            << "entry " << i;
    }
    EXPECT_EQ(world.ctf_requested_team_count, snapshot.ctf_requested_team_count);
    EXPECT_EQ(world.ctf_requested_capture_limit,
              snapshot.ctf_requested_capture_limit);
    EXPECT_EQ(world.ctf_requested_respawn_ticks,
              snapshot.ctf_requested_respawn_ticks);
}

void expect_snapshot_ctf_defaults(const og::sim::WorldSnapshot& snapshot)
{
    const og::sim::WorldSnapshot defaults;
    EXPECT_EQ(defaults.ctf_active, snapshot.ctf_active);
    EXPECT_EQ(defaults.ctf_init_attempted, snapshot.ctf_init_attempted);
    EXPECT_EQ(defaults.ctf_team_count, snapshot.ctf_team_count);
    EXPECT_EQ(defaults.ctf_capture_limit, snapshot.ctf_capture_limit);
    EXPECT_EQ(defaults.ctf_respawn_ticks, snapshot.ctf_respawn_ticks);
    EXPECT_EQ(defaults.ctf_flag_return_ticks, snapshot.ctf_flag_return_ticks);
    EXPECT_EQ(defaults.ctf_time_limit_ticks, snapshot.ctf_time_limit_ticks);
    EXPECT_EQ(defaults.ctf_winner_team, snapshot.ctf_winner_team);
    EXPECT_EQ(defaults.ctf_winner_is_player, snapshot.ctf_winner_is_player);
    EXPECT_EQ(defaults.ctf_respawn_serial, snapshot.ctf_respawn_serial);
    EXPECT_EQ(defaults.ctf_cp_count, snapshot.ctf_cp_count);
    EXPECT_TRUE(snapshot.ctf_respawn_queue.empty());
    for (int t = 0; t < 4; ++t)
    {
        EXPECT_EQ(defaults.ctf_captures[t], snapshot.ctf_captures[t]) << t;
        EXPECT_EQ(defaults.ctf_team_active[t], snapshot.ctf_team_active[t]) << t;
        EXPECT_EQ(defaults.ctf_anchor_count[t], snapshot.ctf_anchor_count[t]) << t;
        EXPECT_FALSE(snapshot.ctf_flags[t].present) << t;
        EXPECT_EQ(0u, snapshot.ctf_flags[t].flag_entity_id) << t;
        EXPECT_EQ(og::sim::CtfFlagState::AtHome, snapshot.ctf_flags[t].state) << t;
        for (int i = 0; i < og::sim::kCtfMaxAnchorsPerTeam; ++i)
        {
            EXPECT_EQ(0, snapshot.ctf_anchor_x[t][i]) << t << "/" << i;
            EXPECT_EQ(0, snapshot.ctf_anchor_y[t][i]) << t << "/" << i;
        }
        EXPECT_EQ(0u, snapshot.ctf_cps[t].entity_id) << t;
        EXPECT_EQ(-1, snapshot.ctf_cps[t].owner) << t;
    }
    EXPECT_EQ(defaults.ctf_requested_team_count,
              snapshot.ctf_requested_team_count);
    EXPECT_EQ(defaults.ctf_requested_capture_limit,
              snapshot.ctf_requested_capture_limit);
    EXPECT_EQ(defaults.ctf_requested_respawn_ticks,
              snapshot.ctf_requested_respawn_ticks);
}

std::size_t payload_length_from_header(const std::vector<std::uint8_t>& bytes)
{
    return static_cast<std::size_t>(bytes[2]) |
           (static_cast<std::size_t>(bytes[3]) << 8);
}

std::vector<std::uint8_t> zlib_decompress_for_test(const std::uint8_t* data,
                                                   std::size_t size)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    EXPECT_EQ(Z_OK, inflateInit(&stream));

    std::vector<std::uint8_t> output;
    std::uint8_t chunk[512] = {};
    int rc = Z_OK;
    do
    {
        stream.next_out = chunk;
        stream.avail_out = static_cast<uInt>(sizeof(chunk));
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK)
        {
            EXPECT_EQ(Z_STREAM_END, rc);
        }
        output.insert(output.end(), chunk,
                      chunk + (sizeof(chunk) - stream.avail_out));
    } while (rc != Z_STREAM_END);
    EXPECT_EQ(Z_OK, inflateEnd(&stream));
    return output;
}

std::vector<std::uint8_t> zlib_compress_for_test(
    const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> compressed(
        compressBound(static_cast<uLong>(payload.size())));
    uLongf compressed_size = static_cast<uLongf>(compressed.size());
    EXPECT_EQ(Z_OK, compress2(compressed.data(), &compressed_size,
                              payload.data(),
                              static_cast<uLong>(payload.size()),
                              Z_DEFAULT_COMPRESSION));
    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

// Rebuilds a snapshot wire message from a raw payload patched at one offset.
std::vector<std::uint8_t> rebuild_patched_snapshot_message(
    std::vector<std::uint8_t> raw_payload,
    std::size_t patch_offset,
    std::uint8_t patch_value)
{
    raw_payload.at(patch_offset) = patch_value;
    const std::vector<std::uint8_t> compressed =
        zlib_compress_for_test(raw_payload);

    std::vector<std::uint8_t> message;
    message.reserve(og::sim::kTransportHeaderSize + compressed.size());
    message.push_back(og::sim::kSnapshotProtocolVersion);
    message.push_back(og::sim::kSnapshotMessageType);
    message.push_back(static_cast<std::uint8_t>(compressed.size() & 0xffu));
    message.push_back(static_cast<std::uint8_t>((compressed.size() >> 8) & 0xffu));
    message.insert(message.end(), compressed.begin(), compressed.end());
    return message;
}

} // namespace

// --- Capture / serialize / apply round trip -------------------------------

TEST(CtfSnapshot, full_state_survives_capture_serialize_apply_round_trip)
{
    std::vector<std::uint8_t> bytes;
    {
        CtfWorld source;
        populate_full_ctf_state(source.world());
        source.spawn_living(FAMILY_SOLDIER, 0, 160, 160);

        const og::sim::WorldSnapshot snapshot =
            og::sim::capture_keyframe_snapshot(source.world());
        expect_snapshot_matches_world(snapshot, source.world());
        bytes = og::sim::serialize_snapshot(snapshot);

        const og::sim::WorldSnapshot decoded =
            og::sim::deserialize_snapshot(bytes.data(), bytes.size());
        const auto failure = og::sim::find_first_snapshot_difference(
            snapshot.tick_count, snapshot, decoded);
        ASSERT_FALSE(failure.has_value())
            << "field " << (failure ? failure->field : std::string{})
            << " expected " << (failure ? failure->expected_value : std::string{})
            << " actual " << (failure ? failure->actual_value : std::string{});
    }

    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes.data(), bytes.size());
    CtfWorld target;
    og::sim::apply_snapshot(target.world(), decoded);
    expect_snapshot_matches_world(decoded, target.world());
}

TEST(CtfSnapshot, apply_clears_stale_ctf_state_from_default_snapshot)
{
    CtfWorld fx;
    fx.world().type = 0;
    fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    fx.tick(5);

    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fx.world());
    expect_snapshot_ctf_defaults(snapshot);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes.data(), bytes.size());
    expect_snapshot_ctf_defaults(decoded);

    // A polluted target world must come back to defaults: the apply path
    // replaces the whole CtfState rather than merging into it.
    CtfWorld target;
    target.world().type = 0;
    populate_full_ctf_state(target.world());
    og::sim::apply_snapshot(target.world(), decoded);
    EXPECT_FALSE(target.world().ctf.active);
    EXPECT_FALSE(target.world().ctf.init_attempted);
    EXPECT_TRUE(target.world().ctf.respawn_queue.empty());
    EXPECT_EQ(0, target.world().ctf.cp_count);
    EXPECT_EQ(0, target.world().ctf.anchor_count[0]);
    EXPECT_EQ(0, target.world().ctf.captures[2]);
    EXPECT_EQ(-1, target.world().ctf.winner_team);
    EXPECT_EQ(2, target.world().ctf_requested_team_count);
    EXPECT_EQ(0, target.world().ctf_requested_capture_limit);
    EXPECT_EQ(0, target.world().ctf_requested_respawn_ticks);
}

// --- Delta path ------------------------------------------------------------

TEST(CtfSnapshot, delta_payload_carries_ctf_changes_onto_baseline)
{
    CtfWorld fx;
    GameWorld& world = fx.world();
    og::sim::WorldSnapshot baseline = og::sim::capture_keyframe_snapshot(world);
    ASSERT_FALSE(baseline.ctf_active);

    populate_full_ctf_state(world);
    world.ctf.captures[0] = 2;
    world.ctf.flags[1].state = og::sim::CtfFlagState::Carried;
    world.ctf.flags[1].carrier_entity_id = 77;

    const og::sim::WorldSnapshot delta_source = og::sim::capture_snapshot(world);
    const std::vector<std::uint8_t> delta_bytes =
        og::sim::serialize_delta(delta_source);
    const og::sim::WorldSnapshot decoded_delta =
        og::sim::deserialize_delta(delta_bytes.data(), delta_bytes.size());

    og::sim::apply_delta(baseline, decoded_delta);
    EXPECT_TRUE(baseline.ctf_active);
    EXPECT_EQ(2, baseline.ctf_captures[0]);
    EXPECT_EQ(og::sim::CtfFlagState::Carried, baseline.ctf_flags[1].state);
    EXPECT_EQ(77u, baseline.ctf_flags[1].carrier_entity_id);
    expect_snapshot_matches_world(baseline, world);
}

// --- Hostile input caps ------------------------------------------------------

TEST(CtfSnapshot, serializer_rejects_out_of_cap_counts)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.ctf_cp_count = og::sim::kCtfMaxControlPoints + 1;
    EXPECT_THROW((void)og::sim::serialize_snapshot(snapshot), std::runtime_error);

    og::sim::WorldSnapshot anchor_snapshot;
    anchor_snapshot.ctf_anchor_count[2] = og::sim::kCtfMaxAnchorsPerTeam + 1;
    EXPECT_THROW((void)og::sim::serialize_snapshot(anchor_snapshot),
                 std::runtime_error);

    og::sim::WorldSnapshot queue_snapshot;
    queue_snapshot.ctf_respawn_queue.resize(og::sim::kCtfMaxRespawnEntries + 1);
    EXPECT_THROW((void)og::sim::serialize_snapshot(queue_snapshot),
                 std::runtime_error);
}

TEST(CtfSnapshot, deserializer_rejects_oversized_counts_in_crafted_payloads)
{
    CtfWorld fx;
    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fx.world());
    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const std::size_t payload_length = payload_length_from_header(bytes);
    const std::vector<std::uint8_t> raw_payload = zlib_decompress_for_test(
        bytes.data() + og::sim::kTransportHeaderSize, payload_length);

    // CTF block layout in a default snapshot: format byte + 71 world-scalar
    // bytes, then 28 scalar CTF bytes and 80 flag bytes put cp_count at 180,
    // the four anchor counts at 181..184, and the queue size at 185.
    constexpr std::size_t kCpCountOffset = 180;
    constexpr std::size_t kAnchorCountOffset = 181;
    constexpr std::size_t kQueueSizeOffset = 185;
    ASSERT_GE(raw_payload.size(), kQueueSizeOffset + 1 + 6);
    ASSERT_EQ(0, raw_payload[kCpCountOffset]);
    ASSERT_EQ(0, raw_payload[kAnchorCountOffset]);
    ASSERT_EQ(0, raw_payload[kQueueSizeOffset]);

    const std::vector<std::uint8_t> bad_cp_count =
        rebuild_patched_snapshot_message(raw_payload, kCpCountOffset, 0xffu);
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(bad_cp_count.data(),
                                            bad_cp_count.size()),
        std::runtime_error);

    const std::vector<std::uint8_t> bad_anchor_count =
        rebuild_patched_snapshot_message(raw_payload, kAnchorCountOffset, 0xffu);
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(bad_anchor_count.data(),
                                            bad_anchor_count.size()),
        std::runtime_error);

    const std::vector<std::uint8_t> bad_queue_size =
        rebuild_patched_snapshot_message(raw_payload, kQueueSizeOffset, 0xffu);
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(bad_queue_size.data(),
                                            bad_queue_size.size()),
        std::runtime_error);
}

TEST(CtfSnapshot, apply_clamps_out_of_cap_counts_from_crafted_snapshots)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.ctf_active = true;
    snapshot.ctf_cp_count = 0xff;
    snapshot.ctf_anchor_count[1] = 0xff;
    snapshot.ctf_respawn_queue.resize(og::sim::kCtfMaxRespawnEntries + 40);

    CtfWorld target;
    og::sim::apply_snapshot(target.world(), snapshot);
    EXPECT_EQ(og::sim::kCtfMaxControlPoints, target.world().ctf.cp_count);
    EXPECT_EQ(og::sim::kCtfMaxAnchorsPerTeam, target.world().ctf.anchor_count[1]);
    EXPECT_EQ(static_cast<std::size_t>(og::sim::kCtfMaxRespawnEntries),
              target.world().ctf.respawn_queue.size());
}

// --- Snapshot-restore equivalence -------------------------------------------

namespace {

// Builds the scripted match used by the equivalence proof: two flags, team
// anchors, one control point, a player-bound runner, and an AI defender that
// captures the point before dying into the respawn queue.
struct EquivalenceActors {
    walker* runner = nullptr;
    walker* enemy = nullptr;
    walker* flag1 = nullptr;
};

EquivalenceActors build_equivalence_scenario(CtfWorld& fx)
{
    EquivalenceActors actors;
    fx.spawn_flag(0, 96, 96);
    actors.flag1 = fx.spawn_flag(1, 544, 800);
    fx.spawn_anchor(0, 128, 128);
    fx.spawn_anchor(0, 160, 128);
    fx.spawn_anchor(1, 512, 832);
    fx.spawn_point(320, 320);
    actors.runner = fx.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
    actors.runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    actors.runner->myguy->id = 7;
    actors.enemy = fx.spawn_living(FAMILY_SOLDIER, 1, 352, 320);
    fx.world().ctf_requested_respawn_ticks = 60;
    return actors;
}

// Runs the first half of the scripted match: init, flag pickup, control-point
// capture, an AI death whose respawn fires before the snapshot, and a player
// death whose respawn entry is still pending when the snapshot is taken.
void run_equivalence_window_one(CtfWorld& fx, EquivalenceActors& actors)
{
    fx.tick(1);
    ASSERT_TRUE(fx.world().ctf.active);
    ASSERT_TRUE(actors.flag1->eat_me(actors.runner));
    ASSERT_EQ(og::sim::CtfFlagState::Carried, fx.world().ctf.flags[1].state);

    fx.tick(44); // tick 45: the enemy alone at the point has captured it
    ASSERT_EQ(1, fx.world().ctf.cps[0].owner);

    actors.enemy->set_dead(1);
    fx.tick(65); // tick 110: AI respawn scheduled at 46 fired around 106

    actors.runner->set_dead(1);
    fx.tick(10); // tick 120: flag dropped, player respawn entry still pending
    ASSERT_EQ(og::sim::CtfFlagState::Dropped, fx.world().ctf.flags[1].state);
    ASSERT_EQ(1u, fx.world().ctf.respawn_queue.size());
    ASSERT_EQ(0, fx.world().ctf.respawn_queue.front().kind);
}

// Snapshots intentionally do not carry queued commands or computed paths;
// apply_snapshot clears them on the restored side. Mirror that reset on the
// uninterrupted world at the capture point so both runs continue from the
// same transient state and any later divergence is a real replication gap.
void normalize_transient_walker_state(GameWorld& world)
{
    auto normalize_list = [](const auto& entities) {
        for (const auto& uptr : entities)
        {
            walker* w = uptr.get();
            if (w == nullptr)
                continue;
            if (w->stats() != nullptr)
                w->stats()->commands.clear();
            w->path_to_foe.clear();
        }
    };
    normalize_list(world.oblist);
    normalize_list(world.fxlist);
    normalize_list(world.weaplist);
}

} // namespace

TEST(CtfSnapshot, snapshot_restore_continuation_matches_uninterrupted_run)
{
    std::vector<std::uint8_t> mid_bytes;
    std::vector<std::uint8_t> uninterrupted_end;
    {
        CtfWorld original;
        EquivalenceActors actors = build_equivalence_scenario(original);
        run_equivalence_window_one(original, actors);

        // Freeze AI act types before the capture point (act_type replicates;
        // a wandering bot could fire a weapon mid-window, and post-restore
        // entity id allocation is not part of the snapshot contract).
        for (const auto& uptr : original.world().oblist)
        {
            walker* w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Living)
            {
                w->set_act_type(ACT_CONTROL);
            }
        }
        normalize_transient_walker_state(original.world());
        mid_bytes = og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(original.world()));

        original.tick(120); // tick 240: player respawn fires inside this window
        ASSERT_TRUE(original.world().ctf.respawn_queue.empty());
        ASSERT_FALSE(actors.runner->dead());
        uninterrupted_end = og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(original.world()));
    }

    CtfWorld restored;
    const og::sim::WorldSnapshot mid_snapshot =
        og::sim::deserialize_snapshot(mid_bytes.data(), mid_bytes.size());
    og::sim::apply_snapshot(restored.world(), mid_snapshot);
    ASSERT_TRUE(restored.world().ctf.active);
    ASSERT_TRUE(restored.world().ctf.init_attempted);
    ASSERT_EQ(1u, restored.world().ctf.respawn_queue.size());

    restored.tick(120);
    ASSERT_TRUE(restored.world().ctf.respawn_queue.empty());
    const std::vector<std::uint8_t> restored_end = og::sim::serialize_snapshot(
        og::sim::capture_keyframe_snapshot(restored.world()));

    const og::sim::WorldSnapshot expected_end = og::sim::deserialize_snapshot(
        uninterrupted_end.data(), uninterrupted_end.size());
    const og::sim::WorldSnapshot actual_end = og::sim::deserialize_snapshot(
        restored_end.data(), restored_end.size());
    const auto failure = og::sim::find_first_snapshot_difference(
        expected_end.tick_count, expected_end, actual_end);
    ASSERT_FALSE(failure.has_value())
        << "diverged at field " << (failure ? failure->field : std::string{})
        << " expected " << (failure ? failure->expected_value : std::string{})
        << " actual " << (failure ? failure->actual_value : std::string{});

    ASSERT_EQ(uninterrupted_end.size(), restored_end.size());
    ASSERT_TRUE(uninterrupted_end == restored_end)
        << "snapshot restore must reproduce the uninterrupted simulation "
           "byte-for-byte; some sim-affecting CTF state is not replicated";
}
