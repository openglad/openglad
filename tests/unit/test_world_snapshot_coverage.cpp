#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include "test_game_world_fixture.h"
#include "zlib.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

// Snapshot v9: one format byte followed by 215 bytes of fixed/default world
// state before the grid block. Keeping this wire pin explicit makes malformed
// payload tests fail loudly when the format is deliberately revised.
constexpr std::size_t kSerializedWorldStateBytes = 215;
constexpr std::size_t kGridOffset = 1 + kSerializedWorldStateBytes;
constexpr std::size_t kGridDirtyOffset = kGridOffset + 2;
constexpr std::size_t kGridFullResendOffset = kGridOffset + 3;
constexpr std::size_t kFullGridSizeOffset = kGridOffset + 4;
constexpr std::size_t kDirtyTileCountOffset = kFullGridSizeOffset + 4;
constexpr std::size_t kFirstDirtyTileOffset = kDirtyTileCountOffset + 4;
constexpr std::size_t kFirstObEntityFlagsOffset = 244;

void write_u16(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint16_t value)
{
    ASSERT_LE(offset + 2, bytes.size());
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffU);
}

void write_u32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value)
{
    ASSERT_LE(offset + 4, bytes.size());
    for (int byte = 0; byte < 4; ++byte)
    {
        bytes[offset + static_cast<std::size_t>(byte)] =
            static_cast<std::uint8_t>((value >> (byte * 8)) & 0xffU);
    }
}

std::vector<std::uint8_t> inflate_for_test(const std::uint8_t* data,
                                           std::size_t size)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(
        reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    if (inflateInit(&stream) != Z_OK)
        throw std::runtime_error("test inflate setup failed");

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 512> chunk{};
    int rc = Z_OK;
    do
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END)
        {
            inflateEnd(&stream);
            throw std::runtime_error("test inflate failed");
        }
        output.insert(output.end(), chunk.begin(),
                      chunk.begin() +
                          static_cast<std::ptrdiff_t>(chunk.size() -
                                                      stream.avail_out));
    } while (rc != Z_STREAM_END);

    if (inflateEnd(&stream) != Z_OK)
        throw std::runtime_error("test inflate teardown failed");
    return output;
}

std::vector<std::uint8_t> raw_delta_payload(
    const og::sim::WorldSnapshot& snapshot)
{
    const std::vector<std::uint8_t> framed =
        og::sim::serialize_delta(snapshot);
    if (framed.size() <
        og::sim::kTransportHeaderSize + og::sim::kDeltaPayloadHeaderSize)
    {
        throw std::runtime_error("serialized delta is unexpectedly short");
    }

    const std::size_t payload_length =
        static_cast<std::size_t>(framed[2]) |
        (static_cast<std::size_t>(framed[3]) << 8);
    if (framed.size() != og::sim::kTransportHeaderSize + payload_length)
        throw std::runtime_error("serialized delta has an invalid frame length");

    const std::size_t wire_size =
        payload_length - og::sim::kDeltaPayloadHeaderSize;
    const std::uint8_t* const wire =
        framed.data() + og::sim::kTransportHeaderSize +
        og::sim::kDeltaPayloadHeaderSize;
    if ((framed[og::sim::kTransportHeaderSize] &
         og::sim::kDeltaPayloadUncompressedFlag) != 0)
    {
        return std::vector<std::uint8_t>(wire, wire + wire_size);
    }
    return inflate_for_test(wire, wire_size);
}

std::vector<std::uint8_t> frame_uncompressed_delta(
    const std::vector<std::uint8_t>& payload)
{
    const std::size_t framed_payload_size =
        og::sim::kDeltaPayloadHeaderSize + payload.size();
    if (framed_payload_size > std::numeric_limits<std::uint16_t>::max())
        throw std::runtime_error("test delta payload is too large");

    std::vector<std::uint8_t> bytes;
    bytes.reserve(og::sim::kTransportHeaderSize + framed_payload_size);
    og::sim::append_transport_header(
        bytes, og::sim::kDeltaSnapshotMessageType,
        static_cast<std::uint16_t>(framed_payload_size));
    bytes.push_back(og::sim::kDeltaPayloadUncompressedFlag);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

template <typename Callable>
void expect_runtime_error_contains(Callable&& callable,
                                   const std::string& expected)
{
    try
    {
        std::forward<Callable>(callable)();
        FAIL() << "expected std::runtime_error containing: " << expected;
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find(expected), std::string::npos)
            << error.what();
    }
}

void expect_bad_delta_payload(const std::vector<std::uint8_t>& payload,
                              const std::string& expected)
{
    const std::vector<std::uint8_t> framed =
        frame_uncompressed_delta(payload);
    expect_runtime_error_contains(
        [&framed]() { (void)og::sim::deserialize_delta(framed); }, expected);
}

og::sim::EntitySnapshot dirty_position_entity(std::uint32_t entity_id,
                                               std::int16_t xpos)
{
    og::sim::EntitySnapshot snapshot;
    snapshot.entity_id = entity_id;
    snapshot.xpos = xpos;
    snapshot.dirty_mask[0] = 1ULL << og::dirty::BIT_XPOS;
    return snapshot;
}

walker* find_entity(GameWorld& world, std::uint32_t entity_id)
{
    return world.find_by_id(entity_id);
}

} // namespace

TEST(WorldSnapshotCoverage, guy_snapshot_roundtrip_preserves_nondefault_state)
{
    og::sim::WorldSnapshot snapshot;
    og::sim::GuySnapshot guy_snapshot;
    guy_snapshot.guy_id = 41;
    guy_snapshot.name = "Roundtrip Hero";
    guy_snapshot.family = FAMILY_ELF;
    guy_snapshot.strength = 17;
    guy_snapshot.dexterity = 18;
    guy_snapshot.exp = 123456U;
    guy_snapshot.total_damage = 777;
    guy_snapshot.scen_damage = 12.5F;
    guy_snapshot.scen_min_hp = 3.25F;
    guy_snapshot.level = 9;
    guy_snapshot.owner_player_index = 2;
    guy_snapshot.owner_save_slot = 5;
    snapshot.guy_snapshots.push_back(guy_snapshot);

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_snapshot(snapshot);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes);

    ASSERT_EQ(1U, decoded.guy_snapshots.size());
    const og::sim::GuySnapshot& actual = decoded.guy_snapshots.front();
    EXPECT_EQ(guy_snapshot.guy_id, actual.guy_id);
    EXPECT_EQ(guy_snapshot.name, actual.name);
    EXPECT_EQ(guy_snapshot.family, actual.family);
    EXPECT_EQ(guy_snapshot.strength, actual.strength);
    EXPECT_EQ(guy_snapshot.dexterity, actual.dexterity);
    EXPECT_EQ(guy_snapshot.exp, actual.exp);
    EXPECT_EQ(guy_snapshot.total_damage, actual.total_damage);
    EXPECT_FLOAT_EQ(guy_snapshot.scen_damage, actual.scen_damage);
    EXPECT_FLOAT_EQ(guy_snapshot.scen_min_hp, actual.scen_min_hp);
    EXPECT_EQ(guy_snapshot.level, actual.level);
    EXPECT_EQ(guy_snapshot.owner_player_index, actual.owner_player_index);
    EXPECT_EQ(guy_snapshot.owner_save_slot, actual.owner_save_slot);
}

TEST(WorldSnapshotCoverage, deserialize_delta_rejects_inconsistent_grid_shapes)
{
    og::sim::WorldSnapshot empty;
    const std::vector<std::uint8_t> empty_payload = raw_delta_payload(empty);
    ASSERT_GT(empty_payload.size(), kDirtyTileCountOffset + 4);
    EXPECT_EQ(og::sim::kSnapshotFormatVersion, empty_payload.front());

    std::vector<std::uint8_t> zero_dimension_resend = empty_payload;
    zero_dimension_resend[kGridDirtyOffset] = 1;
    zero_dimension_resend[kGridFullResendOffset] = 1;
    expect_bad_delta_payload(zero_dimension_resend,
                             "full resend requires non-zero dimensions");

    og::sim::WorldSnapshot full;
    full.grid_width = 2;
    full.grid_height = 2;
    full.grid_dirty = true;
    full.grid_full_resend = true;
    full.full_grid_data = {0x11, 0x22, 0x33, 0x00};
    const std::vector<std::uint8_t> full_payload = raw_delta_payload(full);
    ASSERT_GT(full_payload.size(), kFullGridSizeOffset + 8);

    std::vector<std::uint8_t> wrong_full_size = full_payload;
    write_u32(wrong_full_size, kFullGridSizeOffset, 3);
    expect_bad_delta_payload(
        wrong_full_size,
        "grid full payload size does not match grid dimensions");

    std::vector<std::uint8_t> incremental_with_full_data = full_payload;
    incremental_with_full_data[kGridFullResendOffset] = 0;
    expect_bad_delta_payload(
        incremental_with_full_data,
        "incremental grid update cannot include full grid payload");

    std::vector<std::uint8_t> clean_full_resend = full_payload;
    clean_full_resend[kGridDirtyOffset] = 0;
    expect_bad_delta_payload(clean_full_resend,
                             "full resend requires grid_dirty flag");
}

TEST(WorldSnapshotCoverage, deserialize_delta_rejects_invalid_dirty_tiles)
{
    og::sim::WorldSnapshot incremental;
    incremental.grid_width = 2;
    incremental.grid_height = 2;
    incremental.grid_dirty = true;
    incremental.grid_dirty_tiles.push_back({1, 1, 0x7a});
    const std::vector<std::uint8_t> payload =
        raw_delta_payload(incremental);
    ASSERT_GT(payload.size(), kFirstDirtyTileOffset + 5);

    std::vector<std::uint8_t> clean_with_tile = payload;
    clean_with_tile[kGridDirtyOffset] = 0;
    expect_bad_delta_payload(clean_with_tile,
                             "clean grid snapshot cannot include dirty tiles");

    std::vector<std::uint8_t> outside_grid = payload;
    write_u16(outside_grid, kFirstDirtyTileOffset, 2);
    expect_bad_delta_payload(outside_grid,
                             "grid dirty tile is outside snapshot dimensions");
}

TEST(WorldSnapshotCoverage, deserialize_delta_rejects_unknown_entity_flags)
{
    og::sim::WorldSnapshot delta;
    og::sim::EntitySnapshot removal;
    removal.entity_id = 0x12345678U;
    delta.oblist.push_back(removal);

    std::vector<std::uint8_t> payload = raw_delta_payload(delta);
    ASSERT_GT(payload.size(), kFirstObEntityFlagsOffset);
    ASSERT_EQ(0, payload[kFirstObEntityFlagsOffset]);
    payload[kFirstObEntityFlagsOffset] = 0x80U;
    expect_bad_delta_payload(payload, "unknown entity flags");
}

TEST(WorldSnapshotCoverage, public_deserializers_reject_missing_delta_framing)
{
    expect_runtime_error_contains(
        []() {
            (void)og::sim::deserialize_snapshot(
                std::span<const std::uint8_t>{});
        },
        "truncated header");
    expect_runtime_error_contains(
        []() {
            (void)og::sim::deserialize_delta(
                std::span<const std::uint8_t>{});
        },
        "truncated header");

    const std::array<std::uint8_t, og::sim::kTransportHeaderSize>
        missing_flags = {
            og::sim::kSnapshotProtocolVersion,
            og::sim::kDeltaSnapshotMessageType,
            0,
            0,
        };
    expect_runtime_error_contains(
        [&missing_flags]() {
            (void)og::sim::deserialize_delta(missing_flags);
        },
        "truncated payload flags");

    const std::array<std::uint8_t,
                     og::sim::kTransportHeaderSize +
                         og::sim::kDeltaPayloadHeaderSize>
        unknown_flags = {
            og::sim::kSnapshotProtocolVersion,
            og::sim::kDeltaSnapshotMessageType,
            1,
            0,
            0x80,
        };
    expect_runtime_error_contains(
        [&unknown_flags]() {
            (void)og::sim::deserialize_delta(unknown_flags);
        },
        "unsupported payload flags");
}

TEST(WorldSnapshotCoverage, serializers_enforce_the_transport_payload_limit)
{
    og::sim::WorldSnapshot large;
    std::uint32_t state = 0x9e3779b9U;
    for (std::int32_t guy_id = 0; guy_id < 80; ++guy_id)
    {
        og::sim::GuySnapshot guy_snapshot;
        guy_snapshot.guy_id = guy_id;
        guy_snapshot.name.resize(1024);
        for (char& value : guy_snapshot.name)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            value = static_cast<char>(state >> 24);
        }
        large.guy_snapshots.push_back(std::move(guy_snapshot));
    }

    expect_runtime_error_contains(
        [&large]() { (void)og::sim::serialize_snapshot(large); },
        "payload exceeds 16-bit wire length");
    expect_runtime_error_contains(
        [&large]() { (void)og::sim::serialize_delta(large); },
        "payload exceeds 16-bit wire length");
}

TEST(WorldSnapshotCoverage, apply_delta_moves_entities_and_keeps_unmentioned_order)
{
    og::sim::WorldSnapshot moved_baseline;
    moved_baseline.fxlist.push_back(dirty_position_entity(7, 10));
    og::sim::WorldSnapshot moved_delta;
    moved_delta.oblist.push_back(dirty_position_entity(7, 44));

    og::sim::apply_delta(moved_baseline, moved_delta);
    ASSERT_TRUE(moved_baseline.fxlist.empty());
    ASSERT_EQ(1U, moved_baseline.oblist.size());
    EXPECT_EQ(7U, moved_baseline.oblist.front().entity_id);
    EXPECT_EQ(44, moved_baseline.oblist.front().xpos);

    og::sim::WorldSnapshot ordered_baseline;
    ordered_baseline.oblist.push_back(dirty_position_entity(1, 1));
    ordered_baseline.oblist.push_back(dirty_position_entity(2, 2));
    ordered_baseline.oblist.push_back(dirty_position_entity(3, 3));
    og::sim::WorldSnapshot ordered_delta;
    ordered_delta.oblist.push_back(dirty_position_entity(2, 22));

    og::sim::apply_delta(ordered_baseline, ordered_delta);
    ASSERT_EQ(3U, ordered_baseline.oblist.size());
    EXPECT_EQ(2U, ordered_baseline.oblist[0].entity_id);
    EXPECT_EQ(22, ordered_baseline.oblist[0].xpos);
    EXPECT_EQ(1U, ordered_baseline.oblist[1].entity_id);
    EXPECT_EQ(3U, ordered_baseline.oblist[2].entity_id);
}

TEST(WorldSnapshotCoverage, snapshot_capture_and_apply_remove_null_entries)
{
    TestGameWorld fixture;
    GameWorld& world = fixture.world();
    world.oblist.push_back(std::unique_ptr<walker>{});
    ASSERT_EQ(1U, world.oblist.size());

    const og::sim::WorldSnapshot snapshot =
        og::sim::peek_keyframe_snapshot(world);
    EXPECT_TRUE(snapshot.oblist.empty());
    EXPECT_TRUE(og::sim::apply_snapshot(world, snapshot));
    EXPECT_TRUE(world.oblist.empty());
}

TEST(WorldSnapshotCoverage, apply_snapshot_reports_unavailable_entity_creation)
{
    {
        TestGameWorld fixture;
        GameWorld& world = fixture.world();
        og::sim::WorldSnapshot snapshot =
            og::sim::peek_keyframe_snapshot(world);
        og::sim::EntitySnapshot entity;
        entity.entity_id = 301;
        entity.family = FAMILY_SOLDIER;
        snapshot.oblist.push_back(entity);
        world.entity_factory = {};

        EXPECT_TRUE(og::sim::apply_snapshot(world, snapshot));
        EXPECT_EQ(nullptr, find_entity(world, entity.entity_id));
        EXPECT_TRUE(world.oblist.empty());
    }

    {
        TestGameWorld fixture;
        GameWorld& world = fixture.world();
        og::sim::WorldSnapshot snapshot =
            og::sim::peek_keyframe_snapshot(world);
        og::sim::EntitySnapshot entity;
        entity.entity_id = 302;
        entity.family = FAMILY_ELF;
        snapshot.oblist.push_back(entity);
        world.entity_factory =
            [](Order, std::int32_t) -> std::unique_ptr<walker> {
            return nullptr;
        };

        EXPECT_TRUE(og::sim::apply_snapshot(world, snapshot));
        EXPECT_EQ(nullptr, find_entity(world, entity.entity_id));
        EXPECT_TRUE(world.oblist.empty());
    }
}

TEST(WorldSnapshotCoverage, apply_snapshot_continues_after_reconfigure_failure)
{
    TestGameWorld fixture;
    GameWorld& world = fixture.world();
    walker* const actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    const std::uint32_t actor_id = actor->entity_id();
    og::sim::WorldSnapshot snapshot =
        og::sim::peek_keyframe_snapshot(world);
    ASSERT_EQ(1U, snapshot.oblist.size());
    snapshot.oblist.front().family = FAMILY_ELF;

    bool configurator_called = false;
    world.entity_configurator =
        [&configurator_called](walker&, Order, std::int32_t)
            -> const PixieData* {
        configurator_called = true;
        return nullptr;
    };
    world.entity_derived_stats = [](walker*, Order, std::int32_t) {};

    EXPECT_TRUE(og::sim::apply_snapshot(world, snapshot));
    EXPECT_TRUE(configurator_called);
    walker* const applied = find_entity(world, actor_id);
    ASSERT_NE(nullptr, applied);
    EXPECT_EQ(FAMILY_ELF, applied->family());
}

TEST(WorldSnapshotCoverage, apply_snapshot_validates_guy_links_and_grid_tiles)
{
    {
        TestGameWorld fixture;
        GameWorld& world = fixture.world();
        walker* const actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, actor);
        const std::uint32_t actor_id = actor->entity_id();
        og::sim::WorldSnapshot snapshot =
            og::sim::peek_keyframe_snapshot(world);
        ASSERT_EQ(1U, snapshot.oblist.size());
        snapshot.oblist.front().guy_id = 404;
        snapshot.guy_snapshots.clear();

        EXPECT_TRUE(og::sim::apply_snapshot(world, snapshot));
        walker* const applied = find_entity(world, actor_id);
        ASSERT_NE(nullptr, applied);
        EXPECT_EQ(nullptr, applied->myguy);
    }

    {
        TestGameWorld fixture;
        GameWorld& world = fixture.world();
        walker* const first = world.add_ob(Order::Living, FAMILY_SOLDIER);
        walker* const second = world.add_ob(Order::Living, FAMILY_ELF);
        ASSERT_NE(nullptr, first);
        ASSERT_NE(nullptr, second);
        const std::uint32_t first_id = first->entity_id();
        const std::uint32_t second_id = second->entity_id();
        og::sim::WorldSnapshot snapshot =
            og::sim::peek_keyframe_snapshot(world);
        ASSERT_EQ(2U, snapshot.oblist.size());

        og::sim::GuySnapshot shared_guy;
        shared_guy.guy_id = 405;
        shared_guy.name = "Shared Hero";
        shared_guy.family = FAMILY_SOLDIER;
        shared_guy.level = 8;
        snapshot.guy_snapshots = {shared_guy};
        snapshot.oblist[0].guy_id = shared_guy.guy_id;
        snapshot.oblist[1].guy_id = shared_guy.guy_id;

        world.myobmap.reset();
        EXPECT_TRUE(og::sim::apply_snapshot(world, snapshot));
        walker* const applied_first = find_entity(world, first_id);
        walker* const applied_second = find_entity(world, second_id);
        ASSERT_NE(nullptr, applied_first);
        ASSERT_NE(nullptr, applied_second);
        ASSERT_NE(nullptr, applied_first->myguy);
        ASSERT_NE(nullptr, applied_second->myguy);
        EXPECT_EQ(applied_first->myguy, applied_second->myguy);
        EXPECT_EQ(shared_guy.name, applied_first->myguy->name);
        EXPECT_EQ(shared_guy.level, applied_second->myguy->level);
    }

    {
        TestGameWorld fixture;
        GameWorld& world = fixture.world();
        ASSERT_TRUE(world.grid.valid());
        const unsigned char original = world.grid.data[0];
        og::sim::WorldSnapshot snapshot = og::sim::peek_snapshot(world);
        snapshot.grid_dirty = true;
        snapshot.grid_full_resend = false;
        snapshot.full_grid_data.clear();
        snapshot.grid_dirty_tiles = {{-1, 0, static_cast<std::uint8_t>(
                                                original ^ 0xffU)}};

        EXPECT_TRUE(og::sim::apply_snapshot(world, snapshot));
        EXPECT_EQ(original, world.grid.data[0]);
    }
}

TEST(WorldSnapshotCoverage, drain_sim_events_preserves_tick_and_drains_once)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 808;
    log.push_with_text(og::sim::EventKind::Notification, "snapshot event",
                       12, 34);

    const og::sim::SimEventBatch batch = og::sim::drain_sim_events(log);
    EXPECT_EQ(808U, batch.sequence);
    ASSERT_EQ(1U, batch.events.size());
    EXPECT_EQ(og::sim::EventKind::Notification, batch.events.front().kind);
    EXPECT_EQ(12U, batch.events.front().a);
    EXPECT_EQ(34U, batch.events.front().b);
    EXPECT_EQ("snapshot event", batch.events.front().text);
    EXPECT_TRUE(log.empty());

    const og::sim::SimEventBatch empty = og::sim::drain_sim_events(log);
    EXPECT_EQ(808U, empty.sequence);
    EXPECT_TRUE(empty.events.empty());
}
