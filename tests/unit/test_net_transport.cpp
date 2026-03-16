#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "../test_game_world_fixture.h"

namespace {

void write_u32_le(std::vector<std::uint8_t>& bytes,
                  std::size_t offset,
                  std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

class MockTransport final : public og::sim::ITransport
{
public:
    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        sent_messages_.push_back(
            {peer_id, std::vector<std::uint8_t>(data, data + len)});
    }

    std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> drained =
            std::move(received_messages_);
        received_messages_.clear();
        return drained;
    }

    void accept_connections() override
    {
        accepting_connections_ = true;
    }

    void disconnect(og::sim::PeerId peer_id) override
    {
        disconnected_peers_.push_back(peer_id);
    }

    std::vector<og::sim::PeerId> connected_peers() const override
    {
        return connected_peers_;
    }

    void set_connected_peers(std::vector<og::sim::PeerId> peers)
    {
        connected_peers_ = std::move(peers);
    }

    void queue_received(og::sim::PeerId peer_id, std::vector<std::uint8_t> data)
    {
        received_messages_.push_back({peer_id, std::move(data)});
    }

    bool accepting_connections() const noexcept
    {
        return accepting_connections_;
    }

    const std::vector<og::sim::ReceivedMessage>& sent_messages() const noexcept
    {
        return sent_messages_;
    }

    const std::vector<og::sim::PeerId>& disconnected_peers() const noexcept
    {
        return disconnected_peers_;
    }

private:
    bool accepting_connections_ = false;
    std::vector<og::sim::PeerId> connected_peers_;
    std::vector<og::sim::ReceivedMessage> received_messages_;
    std::vector<og::sim::ReceivedMessage> sent_messages_;
    std::vector<og::sim::PeerId> disconnected_peers_;
};

void expect_input_state_eq(const InputState& expected, const InputState& actual)
{
    EXPECT_EQ(expected.quit_requested, actual.quit_requested);
    EXPECT_EQ(expected.timer_wait_request, actual.timer_wait_request);
    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        {
            EXPECT_EQ(expected.players[player].held[key],
                      actual.players[player].held[key]);
            EXPECT_EQ(expected.players[player].pressed[key],
                      actual.players[player].pressed[key]);
        }
    }
}

TEST(NetTransport, header_helpers_roundtrip_envelope)
{
    std::vector<std::uint8_t> bytes;
    og::sim::append_transport_header(bytes, og::sim::kHelloMessageType, 0x2211u);

    const std::vector<std::uint8_t> expected = {0x01, 0x01, 0x11, 0x22};
    EXPECT_EQ(expected, bytes);

    og::sim::TransportEnvelope envelope;
    ASSERT_TRUE(og::sim::decode_transport_envelope(bytes, envelope));
    EXPECT_EQ(og::sim::kNetworkProtocolVersion, envelope.protocol_version);
    EXPECT_EQ(og::sim::kHelloMessageType, envelope.message_type);
    EXPECT_EQ(0x2211u, envelope.payload_length);
}

TEST(NetTransport, serialize_hello_emits_expected_wire_format)
{
    og::sim::HelloMessage message;
    message.snapshot_format_version = 3;
    message.session_token = {
        0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f,
    };
    message.campaign_content_hash = 0x11223344u;

    constexpr std::array<std::uint8_t, og::sim::kSerializedHelloMessageSize>
        expected = {
            0x01, 0x01, 0x17, 0x00,
            0x01, 0x01, 0x03,
            0x00, 0x01, 0x02, 0x03,
            0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0a, 0x0b,
            0x0c, 0x0d, 0x0e, 0x0f,
            0x44, 0x33, 0x22, 0x11,
        };

    EXPECT_EQ(expected, og::sim::serialize_hello(message));
}

TEST(NetTransport, hello_roundtrip_preserves_versions_token_and_campaign_hash)
{
    og::sim::HelloMessage expected;
    expected.snapshot_format_version = 7;
    expected.session_token = {
        0xf0, 0xe1, 0xd2, 0xc3,
        0xb4, 0xa5, 0x96, 0x87,
        0x78, 0x69, 0x5a, 0x4b,
        0x3c, 0x2d, 0x1e, 0x0f,
    };
    expected.campaign_content_hash = 0xa1b2c3d4u;

    const auto bytes = og::sim::serialize_hello(expected);
    const std::optional<og::sim::HelloMessage> decoded =
        og::sim::deserialize_hello_message(bytes);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(expected.protocol_version, decoded->protocol_version);
    EXPECT_EQ(expected.min_protocol_version, decoded->min_protocol_version);
    EXPECT_EQ(expected.snapshot_format_version,
              decoded->snapshot_format_version);
    EXPECT_EQ(expected.session_token, decoded->session_token);
    EXPECT_EQ(expected.campaign_content_hash, decoded->campaign_content_hash);
}

TEST(NetTransport, decode_rejects_truncated_and_wrong_version_headers)
{
    og::sim::TransportEnvelope envelope;

    const std::array<std::uint8_t, 3> truncated = {0x01, 0x01, 0x00};
    EXPECT_FALSE(og::sim::decode_transport_envelope(truncated, envelope));

    const std::array<std::uint8_t, 4> wrong_version = {0x02, 0x01, 0x00, 0x00};
    EXPECT_FALSE(og::sim::decode_transport_envelope(wrong_version, envelope));
}

TEST(NetTransport, deserialize_initial_setup_rejects_oversized_counts)
{
    const auto bytes = og::sim::serialize_initial_setup_message(
        og::sim::InitialSetupMessage{});

    auto oversized_guy_count = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    write_u32_le(oversized_guy_count, 33, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(oversized_guy_count)
            .has_value());

    auto oversized_level_count = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    write_u32_le(oversized_level_count, 37, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(oversized_level_count)
            .has_value());
}

TEST(NetTransport, deserialize_hello_rejects_wrong_size_version_type_and_range)
{
    og::sim::HelloMessage message;
    message.snapshot_format_version = 5;
    message.campaign_content_hash = 0x55667788u;

    const auto bytes = og::sim::serialize_hello(message);

    ASSERT_FALSE(
        og::sim::deserialize_hello_message(
            std::span<const std::uint8_t>(bytes.data(), bytes.size() - 1))
            .has_value());

    auto bad_version = bytes;
    bad_version[0] = static_cast<std::uint8_t>(
        og::sim::kNetworkProtocolVersion + 1);
    ASSERT_FALSE(og::sim::deserialize_hello_message(bad_version).has_value());

    auto bad_type = bytes;
    bad_type[1] = og::sim::kSnapshotMessageType;
    ASSERT_FALSE(og::sim::deserialize_hello_message(bad_type).has_value());

    auto bad_length = bytes;
    bad_length[2] = 0;
    bad_length[3] = 0;
    ASSERT_FALSE(og::sim::deserialize_hello_message(bad_length).has_value());

    auto bad_payload_version = bytes;
    bad_payload_version[og::sim::kTransportHeaderSize] = static_cast<std::uint8_t>(
        og::sim::kNetworkProtocolVersion + 1);
    ASSERT_FALSE(
        og::sim::deserialize_hello_message(bad_payload_version).has_value());

    auto bad_version_range = bytes;
    bad_version_range[og::sim::kTransportHeaderSize + 1] =
        static_cast<std::uint8_t>(bytes[og::sim::kTransportHeaderSize] + 1);
    ASSERT_FALSE(
        og::sim::deserialize_hello_message(bad_version_range).has_value());
}

TEST(NetTransport, interface_is_mockable_and_preserves_message_buffers)
{
    MockTransport transport;
    transport.accept_connections();
    EXPECT_TRUE(transport.accepting_connections());

    transport.set_connected_peers({7u, 11u});
    const std::vector<og::sim::PeerId> peers = transport.connected_peers();
    EXPECT_EQ((std::vector<og::sim::PeerId>{7u, 11u}), peers);

    const std::array<std::uint8_t, 3> outbound = {0xaa, 0xbb, 0xcc};
    transport.send(7u, outbound.data(), outbound.size());
    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(7u, transport.sent_messages().front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0xaa, 0xbb, 0xcc}),
              transport.sent_messages().front().data);

    transport.queue_received(11u, {0x10, 0x20});
    const std::vector<og::sim::ReceivedMessage> received = transport.poll();
    ASSERT_EQ(1u, received.size());
    EXPECT_EQ(11u, received.front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0x10, 0x20}), received.front().data);
    EXPECT_TRUE(transport.poll().empty());

    transport.disconnect(11u);
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u}),
              transport.disconnected_peers());
}

TEST(NetTransport, game_client_send_input_uses_raw_fallback)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 9u);

    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 9;
    input.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    input.players[1].pressed[static_cast<int>(InputAction::Fire)] = true;
    client.send_input(input, 12u);

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_EQ(9u, transport.sent_messages().front().peer_id);

    const auto expected =
        og::sim::serialize_input(12u, input);
    EXPECT_EQ((std::vector<std::uint8_t>(expected.begin(), expected.end())),
              transport.sent_messages().front().data);
}

TEST(NetTransport,
     game_server_polls_raw_input_messages_when_typed_path_is_unavailable)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    InputState input{};
    input.quit_requested = true;
    input.timer_wait_request = 4;
    input.players[0].held[static_cast<int>(InputAction::MoveLeft)] = true;
    input.players[1].pressed[static_cast<int>(InputAction::Fire)] = true;

    const auto bytes = og::sim::serialize_input(14u, input);
    transport.queue_received(
        5u,
        std::vector<std::uint8_t>(bytes.begin(), bytes.end()));

    server.poll_incoming_messages();

    ASSERT_EQ(1u, server.last_polled_messages().size());
    const og::sim::TypedReceivedMessage& message =
        server.last_polled_messages().front();
    EXPECT_EQ(5u, message.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Input, message.kind);
    EXPECT_EQ(14u, message.tick);
    ASSERT_NE(nullptr, message.input);
    expect_input_state_eq(input, *message.input);
}

TEST(NetTransport, game_server_broadcast_current_state_uses_raw_fallback)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    server.connect_client(7u);
    server.bind_player(7u, 0u, 2);

    fixture.world().tick_count_ = 3u;
    fixture.world().my_team = 2;
    fixture.world().current_palette_id = 1;

    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);

    fixture.world().tick_count_ = 4u;
    fixture.world().current_palette_id = 2;
    fixture.world().pending_exit_prompt = true;

    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);

    ASSERT_EQ(2u, transport.sent_messages().size());

    og::sim::TransportEnvelope envelope;
    ASSERT_TRUE(og::sim::decode_transport_envelope(
        transport.sent_messages()[0].data,
        envelope));
    EXPECT_EQ(og::sim::kInitialSetupMessageType, envelope.message_type);
    const auto initial_setup =
        og::sim::deserialize_initial_setup_message(
            transport.sent_messages()[0].data);
    ASSERT_TRUE(initial_setup.has_value());
    EXPECT_EQ(2, initial_setup->my_team);
    EXPECT_EQ(fixture.world().id, initial_setup->level_id);

    ASSERT_TRUE(og::sim::decode_transport_envelope(
        transport.sent_messages()[1].data,
        envelope));
    EXPECT_EQ(og::sim::kSnapshotMessageType, envelope.message_type);
    const og::sim::WorldSnapshot initial =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data.data(),
                                      transport.sent_messages()[1].data.size());
    EXPECT_EQ(3u, initial.tick_count);
    EXPECT_EQ(2, initial.my_team);
    EXPECT_EQ(1, initial.current_palette_id);
}

TEST(NetTransport, game_server_forward_event_batch_skips_unready_raw_clients)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    server.connect_client(7u);

    og::sim::SimEventBatch batch;
    batch.sequence = 9u;
    batch.events.push_back({
        .tick = 9u,
        .kind = og::sim::EventKind::Notification,
        .a = 30u,
        .b = 0u,
        .text = "sim",
    });
    batch.events.push_back({
        .tick = 9u,
        .kind = og::sim::EventKind::EndGame,
        .a = 1u,
        .b = 2u,
        .text = {},
    });

    server.forward_event_batch(batch);

    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(NetTransport,
     initial_setup_control_change_and_snapshot_hash_messages_roundtrip)
{
    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = 7;
    initial_setup.level_title = "Test Level";
    initial_setup.level_type = 3;
    initial_setup.par_value = 11;
    initial_setup.time_bonus_limit = 222;
    initial_setup.difficulty = 140;
    initial_setup.pixmaxx = 1024;
    initial_setup.pixmaxy = 768;
    initial_setup.my_team = 2;
    initial_setup.allied_mode = 1;
    initial_setup.current_scenario = 7;
    initial_setup.completed_levels = {1, 4, 7};
    initial_setup.controlled_entity_ids = {10u, 20u, 30u, 40u};
    initial_setup.guys.push_back({
        .guy_id = 99,
        .name = "Ari",
        .family = 2,
        .strength = 12,
        .dexterity = 13,
        .constitution = 14,
        .intelligence = 15,
        .armor = 16,
        .exp = 1234u,
        .kills = 8,
        .level_kills = 9,
        .total_damage = 10,
        .total_hits = 11,
        .total_shots = 12,
        .teamnum = 2,
        .scen_damage = 3.5f,
        .scen_kills = 4,
        .scen_damage_taken = 5.5f,
        .scen_min_hp = 6.5f,
        .scen_shots = 7,
        .scen_hits = 8,
        .level = 9,
    });

    const std::vector<std::uint8_t> initial_setup_bytes =
        og::sim::serialize_initial_setup_message(initial_setup);
    const auto decoded_initial_setup =
        og::sim::deserialize_initial_setup_message(initial_setup_bytes);
    ASSERT_TRUE(decoded_initial_setup.has_value());
    EXPECT_EQ(initial_setup, *decoded_initial_setup);

    og::sim::ControlChangeMessage control_change;
    control_change.player_index = 2;
    control_change.entity_id = 444u;
    const std::vector<std::uint8_t> control_change_bytes =
        og::sim::serialize_control_change_message(control_change);
    const auto decoded_control_change =
        og::sim::deserialize_control_change_message(control_change_bytes);
    ASSERT_TRUE(decoded_control_change.has_value());
    EXPECT_EQ(control_change, *decoded_control_change);

    og::sim::SnapshotHashCheckMessage hash_check;
    hash_check.tick = 55u;
    hash_check.snapshot_hash = 0xaabbccddU;
    const std::vector<std::uint8_t> hash_check_bytes =
        og::sim::serialize_snapshot_hash_check_message(hash_check);
    const auto decoded_hash_check =
        og::sim::deserialize_snapshot_hash_check_message(hash_check_bytes);
    ASSERT_TRUE(decoded_hash_check.has_value());
    EXPECT_EQ(hash_check, *decoded_hash_check);
}

TEST(NetTransport, game_client_dispatches_callbacks_for_runtime_state)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const first = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* const second = fixture.world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    first->setxy(32, 32);
    second->setxy(48, 48);

    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = fixture.world().id;
    initial_setup.level_title = fixture.world().title;
    initial_setup.level_type = fixture.world().type;
    initial_setup.par_value = fixture.world().par_value;
    initial_setup.time_bonus_limit = fixture.world().time_bonus_limit;
    initial_setup.difficulty = fixture.world().difficulty;
    initial_setup.pixmaxx = fixture.world().pixmaxx;
    initial_setup.pixmaxy = fixture.world().pixmaxy;
    initial_setup.my_team = fixture.world().my_team;
    initial_setup.allied_mode = fixture.world().allied_mode;
    initial_setup.current_scenario = fixture.world().current_scenario;
    initial_setup.controlled_entity_ids = {
        first->entity_id(), 0u, 0u, 0u};

    og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fixture.world());
    snapshot.tick_count = 1u;
    snapshot.current_palette_id = 1;

    og::sim::SimEventBatch sim_batch;
    sim_batch.sequence = 1u;
    sim_batch.events.push_back({
        .tick = 1u,
        .kind = og::sim::EventKind::Notification,
        .a = 25u,
        .text = "sim",
    });

    og::sim::SimEventBatch game_flow_batch;
    game_flow_batch.sequence = 1u;
    game_flow_batch.events.push_back({
        .tick = 1u,
        .kind = og::sim::EventKind::EndGame,
        .a = 0u,
        .b = 2u,
        .text = {},
    });

    og::sim::ExitPromptBroadcastMessage exit_prompt;
    exit_prompt.destination_level = 3;
    exit_prompt.prompt_text = "Exit now?";

    og::sim::PauseBroadcastMessage pause_broadcast;
    pause_broadcast.player_index = 0u;
    pause_broadcast.player_name = "Ari";

    og::sim::ControlChangeMessage control_change;
    control_change.player_index = 0u;
    control_change.entity_id = second->entity_id();

    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(initial_setup));
    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(7u, og::sim::serialize_sim_event_batch(sim_batch));
    transport.queue_received(
        7u,
        og::sim::serialize_game_flow_event_batch(game_flow_batch));
    transport.queue_received(
        7u,
        og::sim::serialize_exit_prompt_broadcast_message(exit_prompt));
    transport.queue_received(
        7u,
        og::sim::serialize_pause_broadcast_message(pause_broadcast));
    transport.queue_received(
        7u,
        og::sim::serialize_control_change_message(control_change));

    og::sim::GameClient client(transport, 7u, &fixture.world());
    std::vector<bool> initial_setup_transition_flags;
    std::vector<std::uint32_t> mapped_entity_ids;
    std::vector<std::uint32_t> resolved_entity_ids;
    std::vector<std::uint8_t> synced_palette_ids;
    std::vector<og::sim::SimEventBatch> dispatched_sim_batches;
    std::vector<og::sim::SimEventBatch> dispatched_game_flow_batches;
    std::optional<og::sim::ExitPromptBroadcastMessage> received_exit_prompt;
    std::optional<og::sim::PauseBroadcastMessage> received_pause_broadcast;

    client.set_initial_setup_callback(
        [&](const og::sim::InitialSetupMessage&, bool is_level_transition) {
            initial_setup_transition_flags.push_back(is_level_transition);
        });
    client.set_control_mapping_callback(
        [&](const std::array<std::uint32_t, MAX_PLAYERS>& controlled_entity_ids,
            GameWorld* world) {
            mapped_entity_ids.push_back(controlled_entity_ids[0]);
            walker* const mapped =
                (world != nullptr && controlled_entity_ids[0] != 0u)
                    ? world->find_by_id(controlled_entity_ids[0])
                    : nullptr;
            resolved_entity_ids.push_back(
                mapped != nullptr ? mapped->entity_id() : 0u);
        });
    client.set_sim_event_batch_callback(
        [&](const og::sim::SimEventBatch& batch) {
            dispatched_sim_batches.push_back(batch);
        });
    client.set_game_flow_event_batch_callback(
        [&](const og::sim::SimEventBatch& batch) {
            dispatched_game_flow_batches.push_back(batch);
        });
    client.set_exit_prompt_callback(
        [&](const og::sim::ExitPromptBroadcastMessage& message) {
            received_exit_prompt = message;
        });
    client.set_pause_broadcast_callback(
        [&](const og::sim::PauseBroadcastMessage& message) {
            received_pause_broadcast = message;
        });
    client.set_palette_sync_callback(
        [&](std::uint8_t palette_id) {
            synced_palette_ids.push_back(palette_id);
        });

    client.poll_messages();

    ASSERT_TRUE(client.baseline().has_value());
    EXPECT_EQ(1u, client.baseline()->tick_count);
    ASSERT_EQ((std::vector<bool>{false}), initial_setup_transition_flags);
    ASSERT_GE(mapped_entity_ids.size(), 2u);
    EXPECT_EQ(second->entity_id(), mapped_entity_ids.back());
    EXPECT_EQ(second->entity_id(), resolved_entity_ids.back());
    EXPECT_NE(mapped_entity_ids.end(),
              std::find(mapped_entity_ids.begin(),
                        mapped_entity_ids.end(),
                        first->entity_id()));
    ASSERT_EQ((std::vector<std::uint8_t>{1u}), synced_palette_ids);

    ASSERT_EQ(1u, dispatched_sim_batches.size());
    EXPECT_EQ(sim_batch.sequence, dispatched_sim_batches.front().sequence);
    EXPECT_EQ(sim_batch.events, dispatched_sim_batches.front().events);
    ASSERT_EQ(1u, dispatched_game_flow_batches.size());
    EXPECT_EQ(game_flow_batch.sequence,
              dispatched_game_flow_batches.front().sequence);
    EXPECT_EQ(game_flow_batch.events,
              dispatched_game_flow_batches.front().events);
    ASSERT_TRUE(received_exit_prompt.has_value());
    EXPECT_EQ(exit_prompt, *received_exit_prompt);
    ASSERT_TRUE(received_pause_broadcast.has_value());
    EXPECT_EQ(pause_broadcast, *received_pause_broadcast);
}

TEST(NetTransport, game_client_notifies_level_transition_before_next_keyframe)
{
    MockTransport transport;
    TestGameWorld fixture;

    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = fixture.world().id;
    initial_setup.level_title = fixture.world().title;
    initial_setup.level_type = fixture.world().type;
    initial_setup.par_value = fixture.world().par_value;
    initial_setup.time_bonus_limit = fixture.world().time_bonus_limit;
    initial_setup.difficulty = fixture.world().difficulty;
    initial_setup.pixmaxx = fixture.world().pixmaxx;
    initial_setup.pixmaxy = fixture.world().pixmaxy;
    initial_setup.my_team = fixture.world().my_team;
    initial_setup.allied_mode = fixture.world().allied_mode;
    initial_setup.current_scenario = fixture.world().current_scenario;

    og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fixture.world());
    snapshot.tick_count = 1u;

    og::sim::InitialSetupMessage transition_setup = initial_setup;
    transition_setup.level_id = fixture.world().id + 1;
    transition_setup.current_scenario = fixture.world().current_scenario + 1;

    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(initial_setup));
    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(transition_setup));

    og::sim::GameClient client(transport, 7u, &fixture.world());
    std::vector<bool> transition_flags;
    client.set_initial_setup_callback(
        [&](const og::sim::InitialSetupMessage&, bool is_level_transition) {
            transition_flags.push_back(is_level_transition);
            if (is_level_transition)
                client.send_client_ready();
        });

    client.poll_messages();

    ASSERT_EQ((std::vector<bool>{false, true}), transition_flags);
    std::vector<og::sim::ClientReadyMessage> ready_messages;
    for (const auto& sent : transport.sent_messages())
    {
        const auto ready =
            og::sim::deserialize_client_ready_message(sent.data);
        if (ready.has_value())
            ready_messages.push_back(*ready);
    }

    ASSERT_EQ(2u, ready_messages.size());
    EXPECT_EQ(1u, ready_messages[0].last_applied_tick);
    EXPECT_EQ(0u, ready_messages[1].last_applied_tick);
}

TEST(NetTransport,
     game_client_stops_processing_transition_messages_after_endgame)
{
    MockTransport transport;
    TestGameWorld fixture;

    og::sim::InitialSetupMessage initial_setup;
    initial_setup.level_id = fixture.world().id;
    initial_setup.level_title = fixture.world().title;
    initial_setup.level_type = fixture.world().type;
    initial_setup.par_value = fixture.world().par_value;
    initial_setup.time_bonus_limit = fixture.world().time_bonus_limit;
    initial_setup.difficulty = fixture.world().difficulty;
    initial_setup.pixmaxx = fixture.world().pixmaxx;
    initial_setup.pixmaxy = fixture.world().pixmaxy;
    initial_setup.my_team = fixture.world().my_team;
    initial_setup.allied_mode = fixture.world().allied_mode;
    initial_setup.current_scenario = fixture.world().current_scenario;

    og::sim::WorldSnapshot snapshot =
        og::sim::capture_keyframe_snapshot(fixture.world());
    snapshot.tick_count = 1u;

    og::sim::SimEventBatch endgame_batch;
    endgame_batch.sequence = 1u;
    endgame_batch.events.push_back({
        .tick = 1u,
        .kind = og::sim::EventKind::EndGame,
        .a = 0u,
        .b = 2u,
        .text = {},
    });

    og::sim::InitialSetupMessage transition_setup = initial_setup;
    transition_setup.level_id = fixture.world().id + 1;
    transition_setup.current_scenario = fixture.world().current_scenario + 1;

    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(initial_setup));
    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(
        7u, og::sim::serialize_game_flow_event_batch(endgame_batch));
    transport.queue_received(
        7u, og::sim::serialize_initial_setup_message(transition_setup));

    og::sim::GameClient client(transport, 7u, &fixture.world());
    std::vector<bool> transition_flags;
    client.set_initial_setup_callback(
        [&](const og::sim::InitialSetupMessage&, bool is_level_transition) {
            transition_flags.push_back(is_level_transition);
        });
    client.set_game_flow_event_batch_callback(
        [&](const og::sim::SimEventBatch& batch) {
            if (!batch.events.empty() &&
                batch.events.back().kind == og::sim::EventKind::EndGame)
            {
                fixture.world().end = 1;
            }
        });
    client.set_message_processing_break_callback([&fixture]() {
        return fixture.world().end != 0;
    });

    client.poll_messages();

    ASSERT_EQ((std::vector<bool>{false}), transition_flags);
    ASSERT_TRUE(client.initial_setup().has_value());
    EXPECT_EQ(initial_setup.level_id, client.initial_setup()->level_id);
    EXPECT_EQ(initial_setup.current_scenario,
              client.initial_setup()->current_scenario);
}

TEST(NetTransport, game_client_polls_raw_messages_when_typed_path_is_unavailable)
{
    MockTransport transport;

    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = 1u;
    snapshot.my_team = 2;
    snapshot.current_palette_id = 1;

    og::sim::WorldSnapshot delta;
    delta.tick_count = 2u;
    delta.my_team = 3;

    og::sim::SimEventBatch sim_batch;
    sim_batch.sequence = 2u;
    sim_batch.events.push_back({
        .tick = 2u,
        .kind = og::sim::EventKind::Notification,
        .a = 60u,
        .text = "sim",
    });

    og::sim::SimEventBatch game_flow_batch;
    game_flow_batch.sequence = 2u;
    game_flow_batch.events.push_back({
        .tick = 2u,
        .kind = og::sim::EventKind::EndGame,
        .a = 1u,
        .b = 7u,
        .text = {},
    });

    transport.queue_received(7u, og::sim::serialize_snapshot(snapshot));
    transport.queue_received(7u, og::sim::serialize_delta(delta));
    transport.queue_received(7u, og::sim::serialize_sim_event_batch(sim_batch));
    transport.queue_received(7u,
                             og::sim::serialize_game_flow_event_batch(
                                 game_flow_batch));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    ASSERT_EQ(4u, client.last_polled_messages().size());
    ASSERT_TRUE(client.baseline().has_value());
    EXPECT_EQ(2u, client.baseline()->tick_count);
    EXPECT_EQ(3, client.baseline()->my_team);
    ASSERT_EQ(1u, client.sim_event_batches().size());
    EXPECT_EQ(sim_batch.sequence, client.sim_event_batches().front().sequence);
    EXPECT_EQ(sim_batch.events, client.sim_event_batches().front().events);
    ASSERT_EQ(1u, client.game_flow_event_batches().size());
    EXPECT_EQ(game_flow_batch.sequence,
              client.game_flow_event_batches().front().sequence);
    EXPECT_EQ(game_flow_batch.events,
              client.game_flow_event_batches().front().events);
}

} // namespace
