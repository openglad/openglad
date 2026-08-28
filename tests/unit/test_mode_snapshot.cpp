// Mode snapshot replication (snapshot v12): capture/apply round trips, wire
// serialization in keyframe and delta form, hostile-input count caps and
// text-termination hardening, and the snapshot-restore equivalence proof
// that the replicated RespawnState + ModeState blocks are the complete
// sim-affecting state for a running scripted match.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>

#include "test_game_world_fixture.h"
#include "zlib.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// TestGameWorld authored as a scripted-mode level. og_unit binaries mount no
// campaign pack, so no level hooks are registered: mode_run_tick's Lua
// dispatches early-out and the engine frame (win latch, respawn timers) is
// what runs — exactly the replicated surface this suite pins.
struct ModeWorld : TestGameWorld
{
    explicit ModeWorld(int level_id = 500)
        : TestGameWorld(level_id)
    {
        world().type = GameWorld::TYPE_SCRIPTED;
    }

    // Hand-arm the activation latch (a mounted pack's on_mode_init would set
    // this on the first scripted tick).
    void activate_mode()
    {
        world().mode.active = true;
        world().mode.init_attempted = true;
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

void set_mode_text(std::array<char, og::sim::kModeNameBytes>& dst,
                   const char* text)
{
    dst.fill('\0');
    std::strncpy(dst.data(), text, dst.size() - 1);
}

void set_hud_text(std::array<char, og::sim::kModeHudTextBytes>& dst,
                  const char* text)
{
    dst.fill('\0');
    std::strncpy(dst.data(), text, dst.size() - 1);
}

// Fills every RespawnState field (full anchors and respawn queue), every
// ModeState field (name, vars, HUD lines, beacons, win latch), and the
// requested match-knob shorts with distinct nonzero values.
void populate_full_mode_state(GameWorld& world)
{
    og::sim::RespawnState& respawn = world.respawn;
    respawn.respawn_ticks = 90;
    respawn.respawn_serial = 13;
    for (int t = 0; t < 4; ++t)
    {
        respawn.anchor_count[t] = og::sim::kRespawnMaxAnchorsPerTeam;
        for (int i = 0; i < og::sim::kRespawnMaxAnchorsPerTeam; ++i)
        {
            respawn.anchor_x[t][i] = static_cast<std::int16_t>(t * 100 + i + 1);
            respawn.anchor_y[t][i] = static_cast<std::int16_t>(t * 100 + i + 2);
        }
    }
    for (int i = 0; i < og::sim::kRespawnMaxQueueEntries; ++i)
    {
        og::sim::RespawnEntry entry;
        entry.kind = static_cast<std::uint8_t>(i % 2);
        entry.team = static_cast<std::uint8_t>(i % 4);
        entry.family = static_cast<std::uint8_t>((i % 6) + 1);
        entry.level = static_cast<std::uint8_t>((i % 9) + 1);
        entry.ticks_left = static_cast<std::uint16_t>(100 + i);
        entry.walker_entity_id = static_cast<std::uint32_t>(1000 + i);
        entry.x = static_cast<std::int16_t>(500 + i);
        entry.y = static_cast<std::int16_t>(600 + i);
        entry.floor = static_cast<std::uint8_t>(i % 3);
        respawn.respawn_queue.push_back(entry);
    }

    og::sim::ModeState& mode = world.mode;
    mode.active = true;
    mode.init_attempted = true;
    mode.win_latched = true;
    mode.winner_team = 2;
    mode.winner_is_player = true;
    mode.win_ending = 1;
    mode.win_next_level = 503;
    set_mode_text(mode.name, "CTF");
    for (int i = 0; i < og::sim::kModeVarCount; ++i)
        mode.vars[static_cast<std::size_t>(i)] = 1000 + i * 7;
    for (int i = 0; i < og::sim::kModeHudLines; ++i)
    {
        mode.hud[static_cast<std::size_t>(i)].team = static_cast<std::uint8_t>(i);
        set_hud_text(mode.hud[static_cast<std::size_t>(i)].text, i % 2 == 0 ? "2H" : "FLAG!");
    }
    for (int i = 0; i < og::sim::kModeBeacons; ++i)
    {
        mode.beacons[static_cast<std::size_t>(i)].entity_id = 100 + i;
        mode.beacons[static_cast<std::size_t>(i)].team = static_cast<std::uint8_t>(3 - i);
    }
    for (int i = 0; i < og::sim::kModeCameraViews; ++i)
    {
        mode.cameras[static_cast<std::size_t>(i)].entity_id = 200 + i;
        mode.cameras[static_cast<std::size_t>(i)].style = og::sim::kCameraStyleInset;
    }

    // The retired TEAMS knob (A3) holds one value everywhere, and
    // apply_snapshot writes it: populating a 4 here would only assert that
    // the mirror disagrees with its host.
    world.ctf_requested_team_count = 0;
    world.ctf_requested_capture_limit = 9;
    world.ctf_requested_respawn_ticks = 55;
    world.ctf_requested_strip_scenario_troops = 1;
    world.ctf_requested_time_limit = 7200;
}

// Asserts every snapshot respawn/mode field equals the world's live state.
// The populated test state carries full counts and NUL-clean text, so
// capture normalization is the identity and strict comparison is exact.
void expect_snapshot_matches_world(const og::sim::WorldSnapshot& snapshot,
                                   const GameWorld& world)
{
    const og::sim::RespawnState& respawn = world.respawn;
    EXPECT_EQ(respawn.respawn_ticks, snapshot.respawn.respawn_ticks);
    EXPECT_EQ(respawn.respawn_serial, snapshot.respawn.respawn_serial);
    for (int t = 0; t < 4; ++t)
    {
        EXPECT_EQ(respawn.anchor_count[t], snapshot.respawn.anchor_count[t])
            << "team " << t;
        for (int i = 0; i < respawn.anchor_count[t]; ++i)
        {
            EXPECT_EQ(respawn.anchor_x[t][i], snapshot.respawn.anchor_x[t][i])
                << "anchor " << t << "/" << i;
            EXPECT_EQ(respawn.anchor_y[t][i], snapshot.respawn.anchor_y[t][i])
                << "anchor " << t << "/" << i;
        }
    }
    ASSERT_EQ(respawn.respawn_queue.size(),
              snapshot.respawn.respawn_queue.size());
    for (std::size_t i = 0; i < respawn.respawn_queue.size(); ++i)
    {
        const og::sim::RespawnEntry& expected = respawn.respawn_queue[i];
        const og::sim::RespawnEntry& actual =
            snapshot.respawn.respawn_queue[i];
        EXPECT_EQ(expected.kind, actual.kind) << "entry " << i;
        EXPECT_EQ(expected.team, actual.team) << "entry " << i;
        EXPECT_EQ(expected.family, actual.family) << "entry " << i;
        EXPECT_EQ(expected.level, actual.level) << "entry " << i;
        EXPECT_EQ(expected.ticks_left, actual.ticks_left) << "entry " << i;
        EXPECT_EQ(expected.walker_entity_id, actual.walker_entity_id)
            << "entry " << i;
        EXPECT_EQ(expected.x, actual.x) << "entry " << i;
        EXPECT_EQ(expected.y, actual.y) << "entry " << i;
        EXPECT_EQ(expected.floor, actual.floor) << "entry " << i;
    }

    const og::sim::ModeState& mode = world.mode;
    EXPECT_EQ(mode.active, snapshot.mode.active);
    EXPECT_EQ(mode.init_attempted, snapshot.mode.init_attempted);
    EXPECT_EQ(mode.win_latched, snapshot.mode.win_latched);
    EXPECT_EQ(mode.winner_team, snapshot.mode.winner_team);
    EXPECT_EQ(mode.winner_is_player, snapshot.mode.winner_is_player);
    EXPECT_EQ(mode.win_ending, snapshot.mode.win_ending);
    EXPECT_EQ(mode.win_next_level, snapshot.mode.win_next_level);
    EXPECT_EQ(mode.name, snapshot.mode.name);
    EXPECT_EQ(mode.vars, snapshot.mode.vars);
    for (int i = 0; i < og::sim::kModeHudLines; ++i)
    {
        EXPECT_EQ(mode.hud[static_cast<std::size_t>(i)].team, snapshot.mode.hud[static_cast<std::size_t>(i)].team) << "hud " << i;
        EXPECT_EQ(mode.hud[static_cast<std::size_t>(i)].text, snapshot.mode.hud[static_cast<std::size_t>(i)].text) << "hud " << i;
    }
    for (int i = 0; i < og::sim::kModeBeacons; ++i)
    {
        EXPECT_EQ(mode.beacons[static_cast<std::size_t>(i)].entity_id, snapshot.mode.beacons[static_cast<std::size_t>(i)].entity_id)
            << "beacon " << i;
        EXPECT_EQ(mode.beacons[static_cast<std::size_t>(i)].team, snapshot.mode.beacons[static_cast<std::size_t>(i)].team)
            << "beacon " << i;
    }
    for (int i = 0; i < og::sim::kModeCameraViews; ++i)
    {
        EXPECT_EQ(mode.cameras[static_cast<std::size_t>(i)].entity_id,
                  snapshot.mode.cameras[static_cast<std::size_t>(i)].entity_id)
            << "camera " << i;
        EXPECT_EQ(mode.cameras[static_cast<std::size_t>(i)].style,
                  snapshot.mode.cameras[static_cast<std::size_t>(i)].style)
            << "camera " << i;
    }

    EXPECT_EQ(world.ctf_requested_team_count, snapshot.ctf_requested_team_count);
    EXPECT_EQ(world.ctf_requested_capture_limit,
              snapshot.ctf_requested_capture_limit);
    EXPECT_EQ(world.ctf_requested_respawn_ticks,
              snapshot.ctf_requested_respawn_ticks);
    // ctf_requested_strip_scenario_troops is RETIRED (amendment B5): the
    // capture carries whatever the world holds, but apply_mode_state snaps
    // the mirror's copy to 0, so the two are compared only in
    // strip_scenario_troops_still_rides_the_wire_but_never_the_mirror below.
    EXPECT_EQ(world.ctf_requested_time_limit,
              snapshot.ctf_requested_time_limit)
        << "snapshot v11 replicates the match time limit (#241)";
}

void expect_snapshot_mode_defaults(const og::sim::WorldSnapshot& snapshot)
{
    const og::sim::WorldSnapshot defaults;
    EXPECT_EQ(defaults.respawn.respawn_ticks, snapshot.respawn.respawn_ticks);
    EXPECT_EQ(defaults.respawn.respawn_serial, snapshot.respawn.respawn_serial);
    EXPECT_TRUE(snapshot.respawn.respawn_queue.empty());
    for (int t = 0; t < 4; ++t)
    {
        EXPECT_EQ(0, snapshot.respawn.anchor_count[t]) << t;
        for (int i = 0; i < og::sim::kRespawnMaxAnchorsPerTeam; ++i)
        {
            EXPECT_EQ(0, snapshot.respawn.anchor_x[t][i]) << t << "/" << i;
            EXPECT_EQ(0, snapshot.respawn.anchor_y[t][i]) << t << "/" << i;
        }
    }
    EXPECT_FALSE(snapshot.mode.active);
    EXPECT_FALSE(snapshot.mode.init_attempted);
    EXPECT_FALSE(snapshot.mode.win_latched);
    EXPECT_EQ(-1, snapshot.mode.winner_team);
    EXPECT_FALSE(snapshot.mode.winner_is_player);
    EXPECT_EQ(0, snapshot.mode.win_ending);
    EXPECT_EQ(-1, snapshot.mode.win_next_level);
    EXPECT_EQ('\0', snapshot.mode.name[0]);
    for (int i = 0; i < og::sim::kModeVarCount; ++i)
        EXPECT_EQ(0, snapshot.mode.vars[static_cast<std::size_t>(i)]) << "var " << i;
    for (int i = 0; i < og::sim::kModeHudLines; ++i)
    {
        EXPECT_EQ(255, snapshot.mode.hud[static_cast<std::size_t>(i)].team) << "hud " << i;
        EXPECT_EQ('\0', snapshot.mode.hud[static_cast<std::size_t>(i)].text[0]) << "hud " << i;
    }
    for (int i = 0; i < og::sim::kModeBeacons; ++i)
        EXPECT_EQ(0, snapshot.mode.beacons[static_cast<std::size_t>(i)].entity_id) << "beacon " << i;
    for (int i = 0; i < og::sim::kModeCameraViews; ++i)
    {
        EXPECT_EQ(0, snapshot.mode.cameras[static_cast<std::size_t>(i)].entity_id) << "camera " << i;
        EXPECT_EQ(og::sim::kCameraStyleAuto,
                  snapshot.mode.cameras[static_cast<std::size_t>(i)].style)
            << "camera " << i;
    }
    EXPECT_EQ(defaults.ctf_requested_team_count,
              snapshot.ctf_requested_team_count);
    EXPECT_EQ(defaults.ctf_requested_capture_limit,
              snapshot.ctf_requested_capture_limit);
    EXPECT_EQ(defaults.ctf_requested_respawn_ticks,
              snapshot.ctf_requested_respawn_ticks);
    EXPECT_EQ(defaults.ctf_requested_strip_scenario_troops,
              snapshot.ctf_requested_strip_scenario_troops);
    EXPECT_EQ(defaults.ctf_requested_time_limit,
              snapshot.ctf_requested_time_limit);
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

// Rebuilds a snapshot wire message from a raw payload patched at offsets.
std::vector<std::uint8_t> rebuild_patched_snapshot_message(
    std::vector<std::uint8_t> raw_payload,
    std::size_t patch_offset,
    std::uint8_t patch_value,
    std::size_t patch_count = 1)
{
    for (std::size_t i = 0; i < patch_count; ++i)
        raw_payload.at(patch_offset + i) = patch_value;
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

// Self-describing raw-payload offsets for a DEFAULT (empty-state) v13
// snapshot: format byte, then 72 world-scalar bytes, then the respawn block
// (respawn_ticks u16 at 73, respawn_serial u16 at 75, the four anchor counts
// at 77..80 — no anchor pairs follow when all counts are zero — and the
// queue size at 81), then the mode block (8 scalar bytes at 82..89, the
// 12-byte name at 90, 64 i32 vars at 102, the HUD lines at 358, the beacons
// at 466, the camera-view slots at 486), then the thirteen match-knob i16s
// at 491 (v12 appended the eight per-team bot knobs LAST and v13 appended
// the camera slots LAST inside the mode block, which is why every offset
// above the camera slots is unchanged).
constexpr std::size_t kFirstAnchorCountOffset = 77;
constexpr std::size_t kQueueSizeOffset = 81;
constexpr std::size_t kModeNameOffset = 90;
constexpr std::size_t kFirstHudTextOffset = 359; // hud[0].team is at 358
constexpr std::size_t kFirstCameraEntityIdOffset = 486;
constexpr std::size_t kFirstCameraStyleOffset = 490;

} // namespace

// --- Capture / serialize / apply round trip -------------------------------

TEST(ModeSnapshot, full_state_survives_capture_serialize_apply_round_trip)
{
    std::vector<std::uint8_t> bytes;
    {
        ModeWorld source;
        populate_full_mode_state(source.world());
        source.spawn_living(FAMILY_SOLDIER, 0, 160, 160);

        const og::sim::WorldSnapshot snapshot =
            og::sim::capture_keyframe_snapshot(source.world());
        expect_snapshot_matches_world(snapshot, source.world());
        bytes = og::sim::serialize_snapshot(snapshot);

        const og::sim::WorldSnapshot decoded =
            og::sim::deserialize_snapshot(bytes);
        const auto failure = og::sim::find_first_snapshot_difference(
            snapshot.tick_count, snapshot, decoded);
        ASSERT_FALSE(failure.has_value())
            << "field " << (failure ? failure->field : std::string{})
            << " expected " << (failure ? failure->expected_value : std::string{})
            << " actual " << (failure ? failure->actual_value : std::string{});
    }

    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes);
    ModeWorld target;
    og::sim::apply_snapshot(target.world(), decoded);
    expect_snapshot_matches_world(decoded, target.world());
}

TEST(ModeSnapshot, apply_clears_stale_mode_state_from_default_snapshot)
{
    ModeWorld fx;
    fx.world().type = 0;
    fx.spawn_living(FAMILY_SOLDIER, 0, 160, 160);
    fx.tick(5);

    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fx.world());
    expect_snapshot_mode_defaults(snapshot);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes);
    expect_snapshot_mode_defaults(decoded);

    // A polluted target world must come back to defaults: the apply path
    // replaces the whole RespawnState/ModeState rather than merging into it.
    ModeWorld target;
    target.world().type = 0;
    populate_full_mode_state(target.world());
    og::sim::apply_snapshot(target.world(), decoded);
    EXPECT_FALSE(target.world().mode.active);
    EXPECT_FALSE(target.world().mode.init_attempted);
    EXPECT_FALSE(target.world().mode.win_latched);
    EXPECT_EQ(-1, target.world().mode.winner_team);
    EXPECT_EQ(0, target.world().mode.vars[0]);
    EXPECT_EQ('\0', target.world().mode.name[0]);
    EXPECT_TRUE(target.world().respawn.respawn_queue.empty());
    EXPECT_EQ(0, target.world().respawn.anchor_count[0]);
    EXPECT_EQ(0, target.world().ctf_requested_team_count);
    EXPECT_EQ(0, target.world().ctf_requested_capture_limit);
    EXPECT_EQ(0, target.world().ctf_requested_respawn_ticks);
    EXPECT_EQ(0, target.world().ctf_requested_strip_scenario_troops);
    EXPECT_EQ(0, target.world().ctf_requested_time_limit);
}

// --- Delta path ------------------------------------------------------------

TEST(ModeSnapshot, delta_payload_carries_mode_changes_onto_baseline)
{
    ModeWorld fx;
    GameWorld& world = fx.world();
    og::sim::WorldSnapshot baseline = og::sim::capture_keyframe_snapshot(world);
    ASSERT_FALSE(baseline.mode.active);

    populate_full_mode_state(world);
    world.mode.vars[5] = 424242;
    set_hud_text(world.mode.hud[0].text, "RED 2 CAPS");
    world.mode.beacons[0].entity_id = 777;
    world.mode.cameras[0].entity_id = 909;

    const og::sim::WorldSnapshot delta_source = og::sim::capture_snapshot(world);
    const std::vector<std::uint8_t> delta_bytes =
        og::sim::serialize_delta(delta_source);
    const og::sim::WorldSnapshot decoded_delta =
        og::sim::deserialize_delta(delta_bytes);

    og::sim::apply_delta(baseline, decoded_delta);
    EXPECT_TRUE(baseline.mode.active);
    EXPECT_EQ(424242, baseline.mode.vars[5]);
    EXPECT_STREQ("RED 2 CAPS", baseline.mode.hud[0].text.data());
    EXPECT_EQ(777, baseline.mode.beacons[0].entity_id);
    EXPECT_EQ(909, baseline.mode.cameras[0].entity_id);
    EXPECT_EQ(og::sim::kCameraStyleInset, baseline.mode.cameras[0].style);
    expect_snapshot_matches_world(baseline, world);
}

// A queued respawn names the family to spawn, and a class pack's
// `wire_id: auto` families are numbered from NUM_FAMILIES upward
// (packs.cpp AutoWireIds). The deserializer's family clamp took NUM_FAMILIES
// — the CORE span — as its bound, so a queued pack-family respawn came back
// as family 0 on every peer: the wrong class would spawn, and the client's
// re-capture disagreed with the authority on that byte for as long as the
// entry sat in the queue, striking the per-tick hash check. The registries'
// real capacity is NUM_FAMILY_SLOTS, which the whole byte range fits inside.
TEST(ModeSnapshot, respawn_queue_keeps_pack_family_ids_across_the_wire)
{
    constexpr std::uint8_t kPackFamily =
        static_cast<std::uint8_t>(NUM_FAMILIES + 5);
    static_assert(kPackFamily < NUM_FAMILY_SLOTS,
                  "the probe family must sit inside the registry capacity");

    ModeWorld source;
    source.activate_mode();
    og::sim::RespawnEntry entry;
    entry.kind = 1;  // spawn from family/level
    entry.team = 1;
    entry.family = kPackFamily;
    entry.level = 3;
    entry.ticks_left = 42;
    entry.x = 128;
    entry.y = 160;
    source.world().respawn.respawn_queue.push_back(entry);

    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(source.world());
    ASSERT_EQ(1u, snapshot.respawn.respawn_queue.size());
    EXPECT_EQ(kPackFamily, snapshot.respawn.respawn_queue[0].family);

    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(og::sim::serialize_snapshot(snapshot));
    ASSERT_EQ(1u, decoded.respawn.respawn_queue.size());
    EXPECT_EQ(kPackFamily, decoded.respawn.respawn_queue[0].family)
        << "the wire must not collapse a pack family to 0";

    ModeWorld target;
    og::sim::apply_snapshot(target.world(), decoded);
    ASSERT_EQ(1u, target.world().respawn.respawn_queue.size());
    EXPECT_EQ(kPackFamily, target.world().respawn.respawn_queue[0].family);
}

// --- Hostile input caps ------------------------------------------------------

TEST(ModeSnapshot, serializer_rejects_out_of_cap_counts)
{
    og::sim::WorldSnapshot anchor_snapshot;
    anchor_snapshot.respawn.anchor_count[2] = og::sim::kRespawnMaxAnchorsPerTeam + 1;
    EXPECT_THROW((void)og::sim::serialize_snapshot(anchor_snapshot),
                 std::runtime_error);

    og::sim::WorldSnapshot queue_snapshot;
    queue_snapshot.respawn.respawn_queue.resize(
        og::sim::kRespawnMaxQueueEntries + 1);
    EXPECT_THROW((void)og::sim::serialize_snapshot(queue_snapshot),
                 std::runtime_error);
}

TEST(ModeSnapshot, deserializer_rejects_oversized_counts_in_crafted_payloads)
{
    ModeWorld fx;
    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fx.world());
    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const std::size_t payload_length = payload_length_from_header(bytes);
    const std::vector<std::uint8_t> raw_payload = zlib_decompress_for_test(
        bytes.data() + og::sim::kTransportHeaderSize, payload_length);

    ASSERT_GE(raw_payload.size(), kQueueSizeOffset + 1 + 6);
    ASSERT_EQ(0, raw_payload[kFirstAnchorCountOffset]);
    ASSERT_EQ(0, raw_payload[kQueueSizeOffset]);

    const std::vector<std::uint8_t> bad_anchor_count =
        rebuild_patched_snapshot_message(raw_payload, kFirstAnchorCountOffset,
                                         0xffu);
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(bad_anchor_count),
        std::runtime_error);

    const std::vector<std::uint8_t> bad_queue_size =
        rebuild_patched_snapshot_message(raw_payload, kQueueSizeOffset, 0xffu);
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(bad_queue_size),
        std::runtime_error);
}

TEST(ModeSnapshot, deserializer_nul_terminates_crafted_mode_text)
{
    ModeWorld fx;
    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fx.world());
    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const std::size_t payload_length = payload_length_from_header(bytes);
    const std::vector<std::uint8_t> raw_payload = zlib_decompress_for_test(
        bytes.data() + og::sim::kTransportHeaderSize, payload_length);

    // Fill the whole 12-byte name region and the whole 26-byte hud[0] text
    // region with 'A': the decoder must force the terminator byte so no
    // renderer ever sees unterminated text.
    const std::vector<std::uint8_t> unterminated_name =
        rebuild_patched_snapshot_message(raw_payload, kModeNameOffset, 'A',
                                         og::sim::kModeNameBytes);
    const og::sim::WorldSnapshot decoded_name =
        og::sim::deserialize_snapshot(unterminated_name);
    EXPECT_EQ('\0', decoded_name.mode.name.back());
    EXPECT_EQ('A', decoded_name.mode.name[0]);

    const std::vector<std::uint8_t> unterminated_hud =
        rebuild_patched_snapshot_message(raw_payload, kFirstHudTextOffset, 'A',
                                         og::sim::kModeHudTextBytes);
    const og::sim::WorldSnapshot decoded_hud =
        og::sim::deserialize_snapshot(unterminated_hud);
    EXPECT_EQ('\0', decoded_hud.mode.hud[0].text.back());
    EXPECT_EQ('A', decoded_hud.mode.hud[0].text[0]);
}

// Snapshot v13's camera-view slot: the style byte selects a render geometry
// path, so a crafted byte outside the declared range must collapse onto
// kCameraStyleAuto rather than reach the interface. The clamp is deliberately
// the IDENTITY for every value the binding can write (0 and 1), which is what
// keeps round-trip equality true under VALIDATE_SERIALIZATION=ON.
TEST(ModeSnapshot, deserializer_clamps_crafted_camera_style_byte)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.mode.cameras[0].entity_id = 0x11223344;
    snapshot.mode.cameras[0].style = og::sim::kCameraStyleInset;

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const std::size_t payload_length = payload_length_from_header(bytes);
    const std::vector<std::uint8_t> raw_payload = zlib_decompress_for_test(
        bytes.data() + og::sim::kTransportHeaderSize, payload_length);

    // The offsets are self-describing: the four little-endian entity-id bytes
    // are followed by the style byte the writer emitted.
    ASSERT_GE(raw_payload.size(), kFirstCameraStyleOffset + 1);
    EXPECT_EQ(0x44u, raw_payload[kFirstCameraEntityIdOffset + 0]);
    EXPECT_EQ(0x33u, raw_payload[kFirstCameraEntityIdOffset + 1]);
    EXPECT_EQ(0x22u, raw_payload[kFirstCameraEntityIdOffset + 2]);
    EXPECT_EQ(0x11u, raw_payload[kFirstCameraEntityIdOffset + 3]);
    EXPECT_EQ(og::sim::kCameraStyleInset, raw_payload[kFirstCameraStyleOffset]);

    // Every legitimately-written byte round-trips byte-identical.
    for (std::uint8_t legit = 0; legit <= og::sim::kCameraStyleMax; ++legit)
    {
        const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(
            rebuild_patched_snapshot_message(raw_payload,
                                             kFirstCameraStyleOffset, legit));
        EXPECT_EQ(legit, decoded.mode.cameras[0].style)
            << "legit style byte " << static_cast<int>(legit);
        EXPECT_EQ(0x11223344, decoded.mode.cameras[0].entity_id);
    }

    // A crafted byte above the range collapses onto auto, entity id intact.
    for (const std::uint8_t crafted : {std::uint8_t{2}, std::uint8_t{0x7f},
                                       std::uint8_t{0xff}})
    {
        const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(
            rebuild_patched_snapshot_message(raw_payload,
                                             kFirstCameraStyleOffset, crafted));
        EXPECT_EQ(og::sim::kCameraStyleAuto, decoded.mode.cameras[0].style)
            << "crafted style byte " << static_cast<int>(crafted);
        EXPECT_EQ(0x11223344, decoded.mode.cameras[0].entity_id);
    }
}

// Per-entry hardening for the respawn queue: the queue drives
// classic_fire_respawn, which spawns from family/level and revives at floor,
// so a crafted entry must not carry a value no reader can name.
TEST(ModeSnapshot, deserializer_clamps_crafted_respawn_queue_entries)
{
    og::sim::WorldSnapshot snapshot;
    og::sim::RespawnEntry entry;
    entry.kind = 1;
    entry.team = 2;
    entry.family = 3;
    entry.level = 4;
    entry.ticks_left = 40;
    entry.walker_entity_id = 77;
    entry.x = 120;
    entry.y = 130;
    entry.floor = 0;
    snapshot.respawn.respawn_queue.push_back(entry);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const std::size_t payload_length = payload_length_from_header(bytes);
    const std::vector<std::uint8_t> raw_payload = zlib_decompress_for_test(
        bytes.data() + og::sim::kTransportHeaderSize, payload_length);

    // The one queue entry follows the queue-size byte: kind, team, family,
    // level, ticks_left u16, walker_entity_id u32, x i16, y i16, floor.
    constexpr std::size_t kEntry = kQueueSizeOffset + 1;
    ASSERT_GE(raw_payload.size(), kEntry + 15);
    ASSERT_EQ(1, raw_payload[kQueueSizeOffset]);
    ASSERT_EQ(1, raw_payload[kEntry + 0]) << "kind";
    ASSERT_EQ(2, raw_payload[kEntry + 1]) << "team";
    ASSERT_EQ(3, raw_payload[kEntry + 2]) << "family";
    ASSERT_EQ(4, raw_payload[kEntry + 3]) << "level";

    const auto decode_patched = [&](std::size_t offset, std::uint8_t value) {
        return og::sim::deserialize_snapshot(
            rebuild_patched_snapshot_message(raw_payload, offset, value));
    };

    // kind: only 0 and 1 exist; unknown values collapse onto the AI arm.
    const og::sim::WorldSnapshot bad_kind = decode_patched(kEntry + 0, 0xffu);
    ASSERT_EQ(1u, bad_kind.respawn.respawn_queue.size());
    EXPECT_EQ(1, bad_kind.respawn.respawn_queue[0].kind);

    // family indexes the per-family tables and takes the entity block's clamp,
    // whose bound is the registry capacity (NUM_FAMILY_SLOTS), not the core span
    // (NUM_FAMILIES). Ids from NUM_FAMILIES up are the free slots class packs
    // claim with `wire_id: auto`, so they are legal wire values that must
    // survive; an unpopulated one simply answers nullptr at every lookup and
    // takes the loader's soldier/0 fallback when it is spawned.
    const og::sim::WorldSnapshot last_core_family =
        decode_patched(kEntry + 2, static_cast<std::uint8_t>(NUM_FAMILIES - 1));
    EXPECT_EQ(NUM_FAMILIES - 1, last_core_family.respawn.respawn_queue[0].family)
        << "the last core family must survive";
    const og::sim::WorldSnapshot first_pack_family =
        decode_patched(kEntry + 2, static_cast<std::uint8_t>(NUM_FAMILIES));
    EXPECT_EQ(NUM_FAMILIES, first_pack_family.respawn.respawn_queue[0].family)
        << "NUM_FAMILIES is the first pack slot, not an out-of-range value";
    const og::sim::WorldSnapshot top_family = decode_patched(kEntry + 2, 0xffu);
    EXPECT_EQ(255, top_family.respawn.respawn_queue[0].family)
        << "the whole byte fits inside the registry capacity";

    // level 0 is not a legal walker level.
    const og::sim::WorldSnapshot zero_level = decode_patched(kEntry + 3, 0);
    EXPECT_EQ(1, zero_level.respawn.respawn_queue[0].level);

    // team is a full byte on purpose: classic levels field arbitrary teams.
    const og::sim::WorldSnapshot wild_team = decode_patched(kEntry + 1, 0xffu);
    EXPECT_EQ(255, wild_team.respawn.respawn_queue[0].team);

    // A half-negative x/y pair collapses to the full -1/-1 sentinel (the high
    // byte of x makes it negative while y stays positive).
    const og::sim::WorldSnapshot half_negative =
        decode_patched(kEntry + 11, 0xffu);
    EXPECT_EQ(-1, half_negative.respawn.respawn_queue[0].x);
    EXPECT_EQ(-1, half_negative.respawn.respawn_queue[0].y);

    // An untouched payload round-trips unchanged.
    const og::sim::WorldSnapshot clean = og::sim::deserialize_snapshot(bytes);
    ASSERT_EQ(1u, clean.respawn.respawn_queue.size());
    EXPECT_EQ(1, clean.respawn.respawn_queue[0].kind);
    EXPECT_EQ(2, clean.respawn.respawn_queue[0].team);
    EXPECT_EQ(3, clean.respawn.respawn_queue[0].family);
    EXPECT_EQ(4, clean.respawn.respawn_queue[0].level);
    EXPECT_EQ(120, clean.respawn.respawn_queue[0].x);
    EXPECT_EQ(130, clean.respawn.respawn_queue[0].y);
}

TEST(ModeSnapshot, apply_clamps_out_of_cap_counts_from_crafted_snapshots)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.mode.active = true;
    snapshot.respawn.anchor_count[1] = 0xff;
    snapshot.respawn.respawn_queue.resize(og::sim::kRespawnMaxQueueEntries + 40);
    // In-memory snapshots bypass the decoder's terminator enforcement; apply
    // must re-terminate before the state reaches any renderer.
    snapshot.mode.name.fill('A');
    snapshot.mode.hud[2].text.fill('B');

    ModeWorld target;
    og::sim::apply_snapshot(target.world(), snapshot);
    EXPECT_EQ(og::sim::kRespawnMaxAnchorsPerTeam,
              target.world().respawn.anchor_count[1]);
    EXPECT_EQ(static_cast<std::size_t>(og::sim::kRespawnMaxQueueEntries),
              target.world().respawn.respawn_queue.size());
    EXPECT_TRUE(target.world().mode.active);
    EXPECT_EQ('\0', target.world().mode.name.back());
    EXPECT_EQ('\0', target.world().mode.hud[2].text.back());
}

// --- Snapshot-restore equivalence -------------------------------------------

namespace {

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

// The keyframe-continuation proof for scripted matches: a mid-countdown
// respawn entry, live mode vars/HUD/beacons, and the engine frame must all
// carry across a restore — the continuation from the keyframe must match the
// uninterrupted run byte-for-byte (this is what a mid-join keyframe restore
// runs on the wire).
TEST(ModeSnapshot, snapshot_restore_continuation_matches_uninterrupted_run)
{
    std::vector<std::uint8_t> mid_bytes;
    std::vector<std::uint8_t> uninterrupted_end;
    {
        ModeWorld original;
        original.activate_mode();
        // Start markers exist only until the level bootstrap consumes them
        // (the anchor scan reads dead markers by design); a live marker acts
        // and would allocate transient weapon entities, which post-restore id
        // allocation cannot reproduce (entity id allocation is not part of
        // the snapshot contract — the CTF-era test froze act types for the
        // same reason).
        walker* anchor0 = original.spawn_anchor(0, 128, 128);
        walker* anchor1 = original.spawn_anchor(1, 512, 832);
        ASSERT_NE(nullptr, anchor0);
        ASSERT_NE(nullptr, anchor1);
        og::sim::respawn_scan_anchors(original.world());
        anchor0->set_dead(1);
        anchor1->set_dead(1);
        walker* runner =
            original.spawn_living(FAMILY_SOLDIER, 0, 200, 200);
        ASSERT_NE(nullptr, runner);
        runner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        runner->myguy->id = 7;
        original.spawn_living(FAMILY_SOLDIER, 1, 352, 320);
        original.world().respawn.respawn_ticks = 60;
        set_mode_text(original.world().mode.name, "TDM");
        original.world().mode.vars[0] = 3;
        original.world().mode.vars[1] = 5;
        set_hud_text(original.world().mode.hud[0].text, "3:5");
        original.world().mode.beacons[0].entity_id =
            static_cast<std::int32_t>(runner->entity_id());
        original.world().mode.beacons[0].team = 0;

        original.tick(10);

        // Kill the player hero and schedule its mode respawn (Lua owns
        // eligibility on scripted levels; og.respawn_schedule backs onto
        // this exact engine call).
        runner->set_dead(1);
        ASSERT_TRUE(og::sim::respawn_schedule_corpse(original.world(), runner,
                                                     /*ticks_override=*/0));
        original.tick(10);
        ASSERT_EQ(1u, original.world().respawn.respawn_queue.size());
        ASSERT_EQ(0, original.world().respawn.respawn_queue.front().kind);

        normalize_transient_walker_state(original.world());
        mid_bytes = og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(original.world()));

        original.tick(80); // the pending respawn fires inside this window
        ASSERT_TRUE(original.world().respawn.respawn_queue.empty());
        ASSERT_FALSE(runner->dead());
        uninterrupted_end = og::sim::serialize_snapshot(
            og::sim::capture_keyframe_snapshot(original.world()));
    }

    ModeWorld restored;
    const og::sim::WorldSnapshot mid_snapshot =
        og::sim::deserialize_snapshot(mid_bytes);
    og::sim::apply_snapshot(restored.world(), mid_snapshot);
    ASSERT_TRUE(restored.world().mode.active);
    ASSERT_TRUE(restored.world().mode.init_attempted);
    ASSERT_EQ(1u, restored.world().respawn.respawn_queue.size());
    ASSERT_EQ(3, restored.world().mode.vars[0]);
    ASSERT_STREQ("TDM", restored.world().mode.name.data());
    ASSERT_STREQ("3:5", restored.world().mode.hud[0].text.data());

    restored.tick(80);
    ASSERT_TRUE(restored.world().respawn.respawn_queue.empty());
    const std::vector<std::uint8_t> restored_end = og::sim::serialize_snapshot(
        og::sim::capture_keyframe_snapshot(restored.world()));

    const og::sim::WorldSnapshot expected_end =
        og::sim::deserialize_snapshot(uninterrupted_end);
    const og::sim::WorldSnapshot actual_end =
        og::sim::deserialize_snapshot(restored_end);
    const auto failure = og::sim::find_first_snapshot_difference(
        expected_end.tick_count, expected_end, actual_end);
    ASSERT_FALSE(failure.has_value())
        << "diverged at field " << (failure ? failure->field : std::string{})
        << " expected " << (failure ? failure->expected_value : std::string{})
        << " actual " << (failure ? failure->actual_value : std::string{});

    ASSERT_EQ(uninterrupted_end.size(), restored_end.size());
    ASSERT_TRUE(uninterrupted_end == restored_end)
        << "snapshot restore must reproduce the uninterrupted simulation "
           "byte-for-byte; some sim-affecting mode state is not replicated";
}

// --- Requested strip-scenario-troops replication -----------------------------

TEST(ModeSnapshot, strip_scenario_troops_still_rides_the_wire_but_never_the_mirror)
{
    ModeWorld fx;
    GameWorld& world = fx.world();
    world.ctf_requested_strip_scenario_troops = 1;

    // Keyframe capture + serialize + deserialize preserves the field.
    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(world);
    EXPECT_EQ(1, snapshot.ctf_requested_strip_scenario_troops);
    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes);
    EXPECT_EQ(1, decoded.ctf_requested_strip_scenario_troops);

    // ...but apply REFUSES it into the mirror (amendment B5): the field is
    // retired, the host's own world holds 0, and a mirror that kept a 1
    // here would strip a cast the host fielded.
    ModeWorld target;
    ASSERT_EQ(0, target.world().ctf_requested_strip_scenario_troops);
    og::sim::apply_snapshot(target.world(), decoded);
    EXPECT_EQ(0, target.world().ctf_requested_strip_scenario_troops);

    // Delta-merge carries it onto a baseline.
    og::sim::WorldSnapshot baseline =
        og::sim::capture_keyframe_snapshot(target.world());
    baseline.ctf_requested_strip_scenario_troops = 0;
    const og::sim::WorldSnapshot delta_source = og::sim::capture_snapshot(world);
    const std::vector<std::uint8_t> delta_bytes =
        og::sim::serialize_delta(delta_source);
    const og::sim::WorldSnapshot decoded_delta =
        og::sim::deserialize_delta(delta_bytes);
    og::sim::apply_delta(baseline, decoded_delta);
    EXPECT_EQ(1, baseline.ctf_requested_strip_scenario_troops);

    // The payload hash must see the field, and the mode vars too.
    og::sim::WorldSnapshot off = snapshot;
    off.ctf_requested_strip_scenario_troops = 0;
    EXPECT_NE(og::sim::compute_snapshot_hash(snapshot),
              og::sim::compute_snapshot_hash(off));
    og::sim::WorldSnapshot var_changed = snapshot;
    var_changed.mode.vars[17] = 99;
    EXPECT_NE(og::sim::compute_snapshot_hash(snapshot),
              og::sim::compute_snapshot_hash(var_changed));
}

// --- Requested match time limit replication (#241) ---------------------------

TEST(ModeSnapshot, time_limit_round_trips_and_changes_hash)
{
    ModeWorld fx;
    GameWorld& world = fx.world();
    world.ctf_requested_time_limit = 7200;

    // Keyframe capture + serialize + deserialize preserves the field.
    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(world);
    EXPECT_EQ(7200, snapshot.ctf_requested_time_limit);
    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(snapshot);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes);
    EXPECT_EQ(7200, decoded.ctf_requested_time_limit);

    // Apply writes it back into a fresh world.
    ModeWorld target;
    ASSERT_EQ(0, target.world().ctf_requested_time_limit);
    og::sim::apply_snapshot(target.world(), decoded);
    EXPECT_EQ(7200, target.world().ctf_requested_time_limit);

    // Delta-merge carries it onto a baseline.
    og::sim::WorldSnapshot baseline =
        og::sim::capture_keyframe_snapshot(target.world());
    baseline.ctf_requested_time_limit = 0;
    const og::sim::WorldSnapshot delta_source = og::sim::capture_snapshot(world);
    const std::vector<std::uint8_t> delta_bytes =
        og::sim::serialize_delta(delta_source);
    const og::sim::WorldSnapshot decoded_delta =
        og::sim::deserialize_delta(delta_bytes);
    og::sim::apply_delta(baseline, decoded_delta);
    EXPECT_EQ(7200, baseline.ctf_requested_time_limit);

    // The desync detector must SEE the clock: two peers holding different
    // time limits have to hash differently, or the mismatch that would name
    // the divergence never fires.
    og::sim::WorldSnapshot other = snapshot;
    other.ctf_requested_time_limit = 3600;
    EXPECT_NE(og::sim::compute_snapshot_hash(snapshot),
              og::sim::compute_snapshot_hash(other));
}

// The sim-side clamp apply_mode_state carries for this one knob: a crafted
// snapshot must not hand the world a clock the lobby sanitizer, the
// provider and both sync_world_from_save_data twins would all have refused.
// 0 is the sentinel ("the map's own value"), never a clock, so it is never
// lifted to the floor.
TEST(ModeSnapshot, applied_time_limit_is_clamped_into_the_sanitized_band)
{
    ModeWorld fx;
    const og::sim::WorldSnapshot base =
        og::sim::capture_keyframe_snapshot(fx.world());
    ModeWorld target;

    struct Case
    {
        std::int16_t crafted;
        std::int16_t applied;
    };
    const Case cases[] = {
        {100, 720},     // under the floor
        {1, 720},
        {-1000, 720},   // a negative clock is still a request, still bounded
        {30000, 21600}, // over the ceiling, under the 36000-tick loss net
        {720, 720},     // the floor itself (exactly one minute)
        {21600, 21600}, // the ceiling itself
        {7200, 7200},   // in band: untouched
        {0, 0},         // the sentinel
    };
    for (const Case& one : cases)
    {
        og::sim::WorldSnapshot crafted = base;
        crafted.ctf_requested_time_limit = one.crafted;
        og::sim::apply_snapshot(target.world(), crafted);
        EXPECT_EQ(one.applied, target.world().ctf_requested_time_limit)
            << "crafted " << one.crafted;
    }
}

// --- Per-team bot knob replication (LINEUP §3.1) ---------------------------

TEST(ModeSnapshot, bot_knobs_round_trip_and_change_hash)
{
    ModeWorld fx;
    GameWorld& world = fx.world();
    // Distinct per team and distinct between squad and level, so a
    // transposed loop in serialize/deserialize/capture/apply fails loudly
    // instead of silently swapping two equal values.
    world.ctf_requested_fill = {0, 2, 1, 4};
    world.ctf_requested_map_units = {1, 0, 1, 0};

    const og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(world);
    const std::array<std::int16_t, 4> expected_squad = {0, 2, 1, 4};
    const std::array<std::int16_t, 4> expected_level = {1, 0, 1, 0};
    EXPECT_EQ(expected_squad, snapshot.ctf_requested_fill);
    EXPECT_EQ(expected_level, snapshot.ctf_requested_map_units);

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_snapshot(snapshot);
    const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(bytes);
    EXPECT_EQ(expected_squad, decoded.ctf_requested_fill);
    EXPECT_EQ(expected_level, decoded.ctf_requested_map_units);

    ModeWorld target;
    ASSERT_EQ(0, target.world().ctf_requested_fill[1]);
    og::sim::apply_snapshot(target.world(), decoded);
    for (std::size_t team = 0; team < 4; ++team)
    {
        EXPECT_EQ(expected_squad[team],
                  target.world().ctf_requested_fill[team])
            << "squad team " << team;
        EXPECT_EQ(expected_level[team],
                  target.world().ctf_requested_map_units[team])
            << "level team " << team;
    }

    // Delta-merge carries them onto a baseline.
    og::sim::WorldSnapshot baseline =
        og::sim::capture_keyframe_snapshot(target.world());
    baseline.ctf_requested_fill = {};
    baseline.ctf_requested_map_units = {};
    const og::sim::WorldSnapshot delta_source = og::sim::capture_snapshot(world);
    const std::vector<std::uint8_t> delta_bytes =
        og::sim::serialize_delta(delta_source);
    const og::sim::WorldSnapshot decoded_delta =
        og::sim::deserialize_delta(delta_bytes);
    og::sim::apply_delta(baseline, decoded_delta);
    EXPECT_EQ(expected_squad, baseline.ctf_requested_fill);
    EXPECT_EQ(expected_level, baseline.ctf_requested_map_units);

    // Every one of the eight must move the hash on its own, or a peer that
    // disagreed about exactly that team's squad would never be detected.
    for (std::size_t team = 0; team < 4; ++team)
    {
        og::sim::WorldSnapshot other_squad = snapshot;
        other_squad.ctf_requested_fill[team] =
            static_cast<std::int16_t>(other_squad
                                          .ctf_requested_fill[team] +
                                      1);
        EXPECT_NE(og::sim::compute_snapshot_hash(snapshot),
                  og::sim::compute_snapshot_hash(other_squad))
            << "fill team " << team;

        og::sim::WorldSnapshot other_level = snapshot;
        other_level.ctf_requested_map_units[team] =
            static_cast<std::int16_t>(other_level
                                          .ctf_requested_map_units[team] +
                                      1);
        EXPECT_NE(og::sim::compute_snapshot_hash(snapshot),
                  og::sim::compute_snapshot_hash(other_level))
            << "map_units team " << team;
    }
}

// The sim-side twin of the lobby sanitizer / provider clamp: a crafted
// snapshot must not hand a mirror a preset ordinal past the campaign's list
// or a level past the difficulty table. 0 is AUTO and is never lifted.
TEST(ModeSnapshot, applied_bot_knobs_are_clamped_into_the_sanitized_band)
{
    ModeWorld fx;
    const og::sim::WorldSnapshot base =
        og::sim::capture_keyframe_snapshot(fx.world());
    ModeWorld target;

    struct Case
    {
        std::int16_t crafted;
        std::int16_t applied_fill;
        std::int16_t applied_map_units;
    };
    const Case cases[] = {
        {0, 0, 0},  // FILL: NONE, MAP UNITS ON — the default state (E1)
        // Both floors are 0 (amendment B1-B4), so a negative clamps up.
        {-1, 0, 0},
        {-30000, 0, 0},
        {1, 1, og::sim::kMaxMapUnits},  // in band on both
        {og::sim::kMaxFill, og::sim::kMaxFill, og::sim::kMaxMapUnits},
        {32000, og::sim::kMaxFill, og::sim::kMaxMapUnits},  // over
    };
    for (const Case& one : cases)
    {
        og::sim::WorldSnapshot crafted = base;
        crafted.ctf_requested_fill.fill(one.crafted);
        crafted.ctf_requested_map_units.fill(one.crafted);
        // A crafted snapshot cannot reintroduce the retired TROOPS knob
        // either (amendment B5): the mirror snaps it to 0 or it would
        // strip a cast the host fielded.
        crafted.ctf_requested_strip_scenario_troops = og::sim::kTroopsMatched;
        og::sim::apply_snapshot(target.world(), crafted);
        EXPECT_EQ(0, target.world().ctf_requested_strip_scenario_troops);
        for (std::size_t team = 0; team < 4; ++team)
        {
            EXPECT_EQ(one.applied_fill,
                      target.world().ctf_requested_fill[team])
                << "crafted fill " << one.crafted << " team " << team;
            EXPECT_EQ(one.applied_map_units,
                      target.world().ctf_requested_map_units[team])
                << "crafted map_units " << one.crafted << " team " << team;
        }
    }
}
