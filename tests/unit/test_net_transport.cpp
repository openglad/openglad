#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include <algorithm>
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
    using og::sim::ITransport::broadcast;

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

    void broadcast(const std::uint8_t* data, std::size_t len) override
    {
        broadcast_messages_.emplace_back(data, data + len);
        og::sim::ITransport::broadcast(data, len);
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

    void clear_sent_messages()
    {
        sent_messages_.clear();
    }

    const std::vector<std::vector<std::uint8_t>>&
    broadcast_messages() const noexcept
    {
        return broadcast_messages_;
    }

    void clear_broadcast_messages()
    {
        broadcast_messages_.clear();
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
    std::vector<std::vector<std::uint8_t>> broadcast_messages_;
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

og::sim::LobbyPlayer make_lobby_player_for_test()
{
    og::sim::LobbyCharacterData character;
    character.guy_id = 42;
    character.name = "Ari";
    character.family = 2;
    character.strength = 12;
    character.dexterity = 13;
    character.constitution = 14;
    character.intelligence = 15;
    character.armor = 16;
    character.exp = 1234u;
    character.kills = 8;
    character.level_kills = 9;
    character.total_damage = 10;
    character.total_hits = 11;
    character.total_shots = 12;
    character.teamnum = 2;
    character.scen_damage = 3.5f;
    character.scen_kills = 4;
    character.scen_damage_taken = 5.5f;
    character.scen_min_hp = 6.5f;
    character.scen_shots = 7;
    character.scen_hits = 8;
    character.level = 9;

    og::sim::LobbyPlayer player;
    player.player_index = 1u;
    player.name = "Player One";
    player.team = 2;
    player.ready = true;
    player.is_host = true;
    player.character_slots.push_back({
        .slot_index = 3u,
        .character = character,
    });
    return player;
}

og::sim::LobbyState make_lobby_state_for_test()
{
    og::sim::LobbyState state;
    state.settings.campaign_id = "org.openglad.gladiator";
    state.settings.scenario_id = 7;
    state.settings.difficulty = 2;
    state.settings.allied_mode = 1;
    state.host_player_id = 1u;
    state.players.push_back(make_lobby_player_for_test());
    return state;
}

const og::sim::EntitySnapshot* find_entity_snapshot(
    const std::vector<og::sim::EntitySnapshot>& entities,
    std::uint32_t entity_id)
{
    const auto it = std::find_if(
        entities.begin(), entities.end(),
        [entity_id](const og::sim::EntitySnapshot& snapshot) {
            return snapshot.entity_id == entity_id;
        });
    return it == entities.end() ? nullptr : &*it;
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

TEST(NetTransport, default_broadcast_sends_payload_to_all_connected_peers)
{
    MockTransport transport;
    transport.set_connected_peers({3u, 7u, 11u});

    const std::array<std::uint8_t, 3> payload = {0x10, 0x20, 0x30};
    transport.broadcast(payload.data(), payload.size());

    ASSERT_EQ(3u, transport.sent_messages().size());
    EXPECT_EQ(3u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(7u, transport.sent_messages()[1].peer_id);
    EXPECT_EQ(11u, transport.sent_messages()[2].peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0x10, 0x20, 0x30}),
              transport.sent_messages()[0].data);
}

TEST(NetTransport, default_poll_typed_decodes_raw_messages)
{
    MockTransport transport;

    transport.queue_received(
        9u,
        og::sim::serialize_client_ready_message(
            og::sim::ClientReadyMessage{.last_applied_tick = 42u}));

    const std::vector<og::sim::TypedReceivedMessage> messages =
        transport.poll_typed();
    ASSERT_EQ(1u, messages.size());
    EXPECT_EQ(9u, messages.front().peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::ClientReady,
              messages.front().kind);
    ASSERT_TRUE(messages.front().client_ready != nullptr);
    EXPECT_EQ(42u, messages.front().client_ready->last_applied_tick);
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

TEST(NetTransport,
     non_hello_deserializers_reject_wrong_transport_header_version)
{
    const auto bad_version = [](std::vector<std::uint8_t> bytes) {
        bytes[0] = static_cast<std::uint8_t>(
            og::sim::kNetworkProtocolVersion + 1);
        return bytes;
    };

    const auto initial_setup = bad_version(
        og::sim::serialize_initial_setup_message(og::sim::InitialSetupMessage{}));
    EXPECT_FALSE(
        og::sim::deserialize_initial_setup_message(initial_setup).has_value());

    const auto client_ready = bad_version(
        og::sim::serialize_client_ready_message({.last_applied_tick = 7u}));
    EXPECT_FALSE(
        og::sim::deserialize_client_ready_message(client_ready).has_value());

    const auto heartbeat = bad_version(
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    EXPECT_FALSE(
        og::sim::deserialize_heartbeat_message(heartbeat).has_value());

    const auto control_change = bad_version(
        og::sim::serialize_control_change_message({
            .player_index = 1u,
            .entity_id = 42u,
        }));
    EXPECT_FALSE(
        og::sim::deserialize_control_change_message(control_change).has_value());
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

TEST(NetTransport,
     game_server_polls_raw_lobby_messages_when_typed_path_is_unavailable)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    og::sim::LobbyMessage lobby_message;
    lobby_message.payload =
        og::sim::LobbyJoinMessage{make_lobby_player_for_test()};
    const og::sim::LobbyState lobby_state = make_lobby_state_for_test();

    transport.queue_received(
        5u, og::sim::serialize_lobby_message(lobby_message));
    transport.queue_received(
        5u, og::sim::serialize_lobby_state_message(lobby_state));

    server.poll_incoming_messages();

    ASSERT_EQ(2u, server.last_polled_messages().size());

    const og::sim::TypedReceivedMessage& decoded_message =
        server.last_polled_messages()[0];
    EXPECT_EQ(5u, decoded_message.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage,
              decoded_message.kind);
    ASSERT_NE(nullptr, decoded_message.lobby_message);
    EXPECT_EQ(lobby_message, *decoded_message.lobby_message);

    const og::sim::TypedReceivedMessage& decoded_state =
        server.last_polled_messages()[1];
    EXPECT_EQ(5u, decoded_state.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyState,
              decoded_state.kind);
    ASSERT_NE(nullptr, decoded_state.lobby_state);
    EXPECT_EQ(lobby_state, *decoded_state.lobby_state);
}

TEST(NetTransport, game_server_registers_connected_transport_peers_on_poll)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});

    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    ASSERT_EQ(2u, transport.sent_messages().size());
    EXPECT_EQ(7u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(7u, transport.sent_messages()[1].peer_id);
}

TEST(NetTransport, game_server_drops_removed_transport_peers_on_poll)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();
    transport.clear_sent_messages();

    transport.set_connected_peers({});
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    EXPECT_TRUE(transport.disconnected_peers().empty());
    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(NetTransport, game_server_keeps_remaining_clients_when_host_peer_is_removed)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u, 13u});
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();

    transport.set_connected_peers({11u, 13u});
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    EXPECT_TRUE(transport.disconnected_peers().empty());

    ASSERT_EQ(4u, transport.sent_messages().size());
    std::vector<og::sim::PeerId> recipients;
    recipients.reserve(transport.sent_messages().size());
    for (const auto& sent : transport.sent_messages())
        recipients.push_back(sent.peer_id);
    std::sort(recipients.begin(), recipients.end());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u, 11u, 13u, 13u}),
              recipients);
}

TEST(NetTransport,
     game_server_malformed_host_peer_does_not_disconnect_other_clients)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u, 13u});
    server.poll_incoming_messages();
    transport.clear_sent_messages();

    transport.queue_received(7u, {0x01, 0x06, 0x01});
    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());

    transport.set_connected_peers({11u, 13u});
    transport.clear_sent_messages();
    server.poll_incoming_messages();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    std::vector<og::sim::PeerId> recipients;
    recipients.reserve(transport.sent_messages().size());
    for (const auto& sent : transport.sent_messages())
        recipients.push_back(sent.peer_id);
    std::sort(recipients.begin(), recipients.end());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u, 11u, 13u, 13u}),
              recipients);
}

TEST(NetTransport,
     game_server_invalid_host_hello_does_not_disconnect_other_clients)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u, 13u});
    server.poll_incoming_messages();
    transport.clear_sent_messages();

    const auto invalid_hello = og::sim::serialize_hello(og::sim::HelloMessage{});
    transport.queue_received(
        7u,
        std::vector<std::uint8_t>(invalid_hello.begin(), invalid_hello.end()));
    server.step();
    server.send_initial_snapshots(og::sim::SnapshotCaptureMode::Peek);

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());

    std::vector<og::sim::PeerId> recipients;
    recipients.reserve(transport.sent_messages().size());
    for (const auto& sent : transport.sent_messages())
        recipients.push_back(sent.peer_id);
    std::sort(recipients.begin(), recipients.end());
    EXPECT_EQ((std::vector<og::sim::PeerId>{11u, 11u, 13u, 13u}),
              recipients);
}

TEST(NetTransport, heartbeat_resets_server_input_timeout)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    std::uint64_t now_ms = 1000;
    server.set_wall_clock_ms_source([&] { return now_ms; });

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) - 1u;
    transport.queue_received(
        7u,
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{}));
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty());

    now_ms += static_cast<std::uint64_t>(og::sim::DISCONNECT_TIMEOUT_MS) - 1u;
    server.step();
    EXPECT_TRUE(transport.disconnected_peers().empty());

    now_ms += 2u;
    server.step();
    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
}

TEST(NetTransport, game_client_sends_automatic_heartbeats_when_idle)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    transport.set_connected_peers({7u});
    client.poll_messages();

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_TRUE(
        og::sim::deserialize_hello_message(transport.sent_messages()[0].data)
            .has_value());

    transport.clear_sent_messages();
    client.testing_set_last_outbound_activity_elapsed_ms(2100.0f);
    client.poll_messages();

    ASSERT_EQ(1u, transport.sent_messages().size());
    EXPECT_TRUE(
        og::sim::deserialize_heartbeat_message(transport.sent_messages()[0].data)
            .has_value());

    transport.clear_sent_messages();
    client.poll_messages();
    EXPECT_TRUE(transport.sent_messages().empty());
}

TEST(NetTransport, game_client_notifies_when_server_is_gone_for_too_long)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    int connection_lost_count = 0;
    client.set_connection_lost_callback([&connection_lost_count] {
        ++connection_lost_count;
    });

    transport.set_connected_peers({7u});
    client.poll_messages();

    transport.set_connected_peers({});
    client.poll_messages();
    EXPECT_EQ(0, connection_lost_count);

    client.testing_set_transport_disconnect_elapsed_ms(
        static_cast<float>(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS + 1u));
    client.poll_messages();
    EXPECT_EQ(1, connection_lost_count);

    client.poll_messages();
    EXPECT_EQ(1, connection_lost_count);
}

TEST(NetTransport,
     disconnect_grace_uses_last_pending_held_input_from_removed_peer)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    walker* const control =
        fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(32, 48);
    control->set_user(0);
    control->set_act_type(ACT_CONTROL);
    server.bind_player(7u, 0u, fixture.world().my_team, control);

    InputState move_right;
    move_right.players[0].held[static_cast<int>(InputAction::MoveRight)] = true;
    const auto input_bytes = og::sim::serialize_input(1u, move_right);
    transport.queue_received(
        7u,
        std::vector<std::uint8_t>(input_bytes.begin(), input_bytes.end()));
    transport.set_connected_peers({});

    server.step();

    EXPECT_EQ(0, static_cast<int>(control->user()));
    ASSERT_EQ(1u, server.disconnected_players().size());

    const PlayerInput& repeated_input =
        server.disconnected_players().front().repeated_input;
    EXPECT_TRUE(repeated_input.held[static_cast<int>(InputAction::MoveRight)]);
    EXPECT_FALSE(
        repeated_input.pressed[static_cast<int>(InputAction::MoveRight)]);
}

TEST(NetTransport, game_server_snapshot_hash_check_is_strict_per_peer_per_tick)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u});
    server.poll_incoming_messages();

    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot first_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data.data(),
                                      transport.sent_messages()[1].data.size());
    transport.clear_sent_messages();

    fixture.world().current_palette_id = 1;
    server.send_initial_snapshot(11u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot second_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data.data(),
                                      transport.sent_messages()[1].data.size());
    ASSERT_EQ(first_snapshot.tick_count, second_snapshot.tick_count);
    ASSERT_NE(first_snapshot.snapshot_hash, second_snapshot.snapshot_hash);

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = first_snapshot.tick_count,
            .snapshot_hash = first_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count());

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = first_snapshot.tick_count,
            .snapshot_hash = second_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(1u, server.snapshot_hash_mismatch_count());
}

TEST(NetTransport, game_server_snapshot_hash_check_preserves_same_peer_same_tick_order)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    server.connect_client(7u);

    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot first_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data.data(),
                                      transport.sent_messages()[1].data.size());
    transport.clear_sent_messages();

    fixture.world().current_palette_id = 1;
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    ASSERT_GE(transport.sent_messages().size(), 2u);
    const og::sim::WorldSnapshot second_snapshot =
        og::sim::deserialize_snapshot(transport.sent_messages()[1].data.data(),
                                      transport.sent_messages()[1].data.size());
    ASSERT_EQ(first_snapshot.tick_count, second_snapshot.tick_count);
    ASSERT_NE(first_snapshot.snapshot_hash, second_snapshot.snapshot_hash);

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = first_snapshot.tick_count,
            .snapshot_hash = first_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count());

    transport.queue_received(
        7u,
        og::sim::serialize_snapshot_hash_check_message({
            .tick = second_snapshot.tick_count,
            .snapshot_hash = second_snapshot.snapshot_hash,
        }));
    server.step();
    EXPECT_EQ(0u, server.snapshot_hash_mismatch_count());
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

TEST(NetTransport, game_server_forward_event_batch_uses_ready_raw_fallback)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);
    server.connect_client(7u);
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);

    og::sim::ClientReadyMessage ready;
    ready.last_applied_tick = fixture.world().tick_count_;
    transport.queue_received(
        7u, og::sim::serialize_client_ready_message(ready));
    server.step();

    const std::size_t sent_before = transport.sent_messages().size();

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

    ASSERT_EQ(sent_before + 2u, transport.sent_messages().size());

    og::sim::TransportEnvelope envelope;
    const auto& sim_message = transport.sent_messages()[sent_before];
    EXPECT_EQ(7u, sim_message.peer_id);
    ASSERT_TRUE(og::sim::decode_transport_envelope(sim_message.data, envelope));
    EXPECT_EQ(og::sim::kSimEventBatchMessageType, envelope.message_type);
    const og::sim::SimEventBatch sim_batch =
        og::sim::deserialize_sim_event_batch(sim_message.data.data(),
                                             sim_message.data.size());
    EXPECT_NE(0u, sim_batch.sequence);
    ASSERT_EQ(1u, sim_batch.events.size());
    EXPECT_EQ(og::sim::EventKind::Notification, sim_batch.events[0].kind);
    EXPECT_EQ("sim", sim_batch.events[0].text);

    const auto& game_flow_message = transport.sent_messages()[sent_before + 1u];
    EXPECT_EQ(7u, game_flow_message.peer_id);
    ASSERT_TRUE(
        og::sim::decode_transport_envelope(game_flow_message.data, envelope));
    EXPECT_EQ(og::sim::kGameFlowEventBatchMessageType, envelope.message_type);
    const og::sim::SimEventBatch game_flow_batch =
        og::sim::deserialize_game_flow_event_batch(game_flow_message.data.data(),
                                                   game_flow_message.data.size());
    EXPECT_NE(0u, game_flow_batch.sequence);
    ASSERT_EQ(1u, game_flow_batch.events.size());
    EXPECT_EQ(og::sim::EventKind::EndGame, game_flow_batch.events[0].kind);
    EXPECT_EQ(1u, game_flow_batch.events[0].a);
    EXPECT_EQ(2u, game_flow_batch.events[0].b);
}

TEST(NetTransport,
     game_server_broadcast_current_state_uses_transport_broadcast_for_shared_keyframes)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u, 11u});
    server.connect_client(7u);
    server.connect_client(11u);
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.bind_player(11u, 1u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    server.send_initial_snapshot(11u, og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    const og::sim::ClientReadyMessage ready{
        .last_applied_tick = fixture.world().tick_count_,
    };
    transport.queue_received(
        7u, og::sim::serialize_client_ready_message(ready));
    transport.queue_received(
        11u, og::sim::serialize_client_ready_message(ready));
    server.step();
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS;
    fixture.world().current_palette_id = 3;

    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Peek,
                                   og::sim::EventDeliveryMode::Skip);

    ASSERT_EQ(1u, transport.broadcast_messages().size());
    ASSERT_EQ(2u, transport.sent_messages().size());
    EXPECT_EQ(7u, transport.sent_messages()[0].peer_id);
    EXPECT_EQ(11u, transport.sent_messages()[1].peer_id);
    EXPECT_EQ(transport.broadcast_messages().front(),
              transport.sent_messages()[0].data);
    EXPECT_EQ(transport.broadcast_messages().front(),
              transport.sent_messages()[1].data);

    const og::sim::WorldSnapshot snapshot =
        og::sim::deserialize_snapshot(
            transport.broadcast_messages().front().data(),
            transport.broadcast_messages().front().size());
    EXPECT_EQ(og::sim::KEYFRAME_INTERVAL_TICKS, snapshot.tick_count);
    EXPECT_EQ(3, snapshot.current_palette_id);
}

TEST(NetTransport,
     game_server_keyframe_preserves_hurt_flash_before_authoritative_consumption)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();
    server.bind_player(7u, 0u, fixture.world().my_team);
    server.send_initial_snapshot(7u, og::sim::SnapshotCaptureMode::Peek);
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    const og::sim::ClientReadyMessage ready{
        .last_applied_tick = fixture.world().tick_count_,
    };
    transport.queue_received(
        7u, og::sim::serialize_client_ready_message(ready));
    server.step();
    transport.clear_sent_messages();
    transport.clear_broadcast_messages();

    fixture.world().tick_count_ = og::sim::KEYFRAME_INTERVAL_TICKS;
    actor->set_hurt_flash(true);

    server.broadcast_current_state(og::sim::SnapshotCaptureMode::Consume,
                                   og::sim::EventDeliveryMode::Skip);

    ASSERT_EQ(1u, transport.broadcast_messages().size());
    ASSERT_EQ(1u, transport.sent_messages().size());
    const og::sim::WorldSnapshot snapshot =
        og::sim::deserialize_snapshot(
            transport.broadcast_messages().front().data(),
            transport.broadcast_messages().front().size());
    const og::sim::EntitySnapshot* actor_snapshot =
        find_entity_snapshot(snapshot.oblist, actor->entity_id());
    ASSERT_NE(nullptr, actor_snapshot);
    EXPECT_EQ(1u, actor_snapshot->hurt_flash);
    EXPECT_FALSE(actor->hurt_flash());
    EXPECT_NE(
        0ULL,
        actor->dirty_mask_word(og::dirty::BIT_HURT_FLASH / 64) &
            (1ULL << (og::dirty::BIT_HURT_FLASH % 64)));
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

TEST(NetTransport, lobby_state_and_messages_roundtrip)
{
    const og::sim::LobbyPlayer player = make_lobby_player_for_test();
    const og::sim::LobbyState state = make_lobby_state_for_test();

    const std::vector<std::uint8_t> state_bytes =
        og::sim::serialize_lobby_state_message(state);
    const auto decoded_state =
        og::sim::deserialize_lobby_state_message(state_bytes);
    ASSERT_TRUE(decoded_state.has_value());
    EXPECT_EQ(state, *decoded_state);

    std::vector<og::sim::LobbyMessage> messages;

    og::sim::LobbyMessage join;
    join.payload = og::sim::LobbyJoinMessage{player};
    messages.push_back(join);

    og::sim::LobbyMessage leave;
    leave.payload = og::sim::LobbyLeaveMessage{.player_index = 1u};
    messages.push_back(leave);

    og::sim::LobbyMessage ready;
    ready.payload =
        og::sim::LobbyReadyMessage{.player_index = 1u, .ready = false};
    messages.push_back(ready);

    og::sim::LobbyMessage team_change;
    team_change.payload =
        og::sim::LobbyTeamChangeMessage{.player_index = 1u, .team = 3};
    messages.push_back(team_change);

    og::sim::LobbyMessage start_game;
    start_game.payload = og::sim::LobbyStartGameMessage{.player_index = 1u};
    messages.push_back(start_game);

    og::sim::LobbyMessage settings_change;
    settings_change.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 1u,
        .settings =
            {
                .campaign_id = "org.openglad.gladiator",
                .scenario_id = 8,
                .difficulty = 1,
                .allied_mode = 0,
            },
    };
    messages.push_back(settings_change);

    for (const auto& message : messages)
    {
        const std::vector<std::uint8_t> bytes =
            og::sim::serialize_lobby_message(message);
        const auto decoded = og::sim::deserialize_lobby_message(bytes);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(message.kind(), decoded->kind());
        EXPECT_EQ(message, *decoded);
    }
}

TEST(NetTransport,
     deserialize_lobby_messages_rejects_unknown_kinds_and_oversized_counts)
{
    const auto empty_state_bytes =
        og::sim::serialize_lobby_state_message(og::sim::LobbyState{});
    auto oversized_player_count =
        std::vector<std::uint8_t>(empty_state_bytes.begin(),
                                  empty_state_bytes.end());
    write_u32_le(oversized_player_count, 15, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_lobby_state_message(oversized_player_count)
            .has_value());

    og::sim::LobbyPlayer player;
    player.player_index = 0u;
    og::sim::LobbyState state_with_player;
    state_with_player.players.push_back(player);
    const auto player_state_bytes =
        og::sim::serialize_lobby_state_message(state_with_player);
    auto oversized_slot_count =
        std::vector<std::uint8_t>(player_state_bytes.begin(),
                                  player_state_bytes.end());
    write_u32_le(oversized_slot_count, 28, 0xffffffffu);
    EXPECT_FALSE(
        og::sim::deserialize_lobby_state_message(oversized_slot_count)
            .has_value());

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyStartGameMessage{.player_index = 0u};
    auto bad_kind = og::sim::serialize_lobby_message(message);
    bad_kind[og::sim::kTransportHeaderSize] = 0xffu;
    EXPECT_FALSE(og::sim::deserialize_lobby_message(bad_kind).has_value());
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

TEST(NetTransport, game_client_tracks_interpolated_positions_across_snapshots)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(32, 48);
    fixture.world().tick_count_ = 1u;

    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    const auto initial_pos = client.render_position(actor->entity_id(), 0.5f);
    ASSERT_TRUE(initial_pos.has_value());
    EXPECT_FLOAT_EQ(32.0f, initial_pos->worldx);
    EXPECT_FLOAT_EQ(48.0f, initial_pos->worldy);
    EXPECT_FLOAT_EQ(32.0f, initial_pos->xpos);
    EXPECT_FLOAT_EQ(48.0f, initial_pos->ypos);

    actor->setxy(80, 96);
    fixture.world().tick_count_ = 2u;
    const og::sim::WorldSnapshot delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(delta));

    client.poll_messages();

    const auto start = client.render_position(actor->entity_id(), 0.0f);
    const auto middle = client.render_position(actor->entity_id(), 0.5f);
    const auto end = client.render_position(actor->entity_id(), 1.5f);
    ASSERT_TRUE(start.has_value());
    ASSERT_TRUE(middle.has_value());
    ASSERT_TRUE(end.has_value());

    EXPECT_FLOAT_EQ(32.0f, start->worldx);
    EXPECT_FLOAT_EQ(48.0f, start->worldy);
    EXPECT_FLOAT_EQ(56.0f, middle->worldx);
    EXPECT_FLOAT_EQ(72.0f, middle->worldy);
    EXPECT_FLOAT_EQ(80.0f, end->worldx);
    EXPECT_FLOAT_EQ(96.0f, end->worldy);
    EXPECT_FLOAT_EQ(56.0f, middle->xpos);
    EXPECT_FLOAT_EQ(72.0f, middle->ypos);
}

TEST(NetTransport,
     game_client_snaps_spawn_positions_and_suppresses_dead_entity_interpolation)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().tick_count_ = 1u;
    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    walker* const spawned =
        fixture.world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_NE(nullptr, spawned);
    spawned->setxy(90, 110);
    fixture.world().tick_count_ = 2u;
    const og::sim::WorldSnapshot spawn_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(spawn_delta));

    client.poll_messages();

    const auto spawn_pos = client.render_position(spawned->entity_id(), 0.0f);
    ASSERT_TRUE(spawn_pos.has_value());
    EXPECT_FLOAT_EQ(90.0f, spawn_pos->worldx);
    EXPECT_FLOAT_EQ(110.0f, spawn_pos->worldy);
    EXPECT_FLOAT_EQ(90.0f, spawn_pos->xpos);
    EXPECT_FLOAT_EQ(110.0f, spawn_pos->ypos);

    spawned->set_dead(1);
    fixture.world().tick_count_ = 3u;
    const og::sim::WorldSnapshot death_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(death_delta));

    client.poll_messages();

    EXPECT_FALSE(client.render_position(spawned->entity_id(), 0.5f).has_value());
}

TEST(NetTransport, game_client_render_interpolation_alpha_respects_game_speed)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().timer_wait = 6;
    fixture.world().tick_count_ = 1u;
    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    client.testing_set_render_interpolation_elapsed_ms(20.5f);
    EXPECT_NEAR(0.25f, client.render_interpolation_alpha(1.0f), 0.02f);

    client.testing_set_render_interpolation_elapsed_ms(20.5f);
    EXPECT_NEAR(0.5f, client.render_interpolation_alpha(2.0f), 0.02f);

    client.testing_set_render_interpolation_elapsed_ms(20.5f);
    EXPECT_FLOAT_EQ(1.0f, client.render_interpolation_alpha(0.0f));
}

TEST(NetTransport,
     game_client_render_interpolation_alpha_treats_zero_timer_wait_as_immediate)
{
    MockTransport transport;
    TestGameWorld fixture;

    fixture.world().timer_wait = 0;
    fixture.world().tick_count_ = 1u;
    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    client.testing_set_render_interpolation_elapsed_ms(1.0f);
    EXPECT_FLOAT_EQ(1.0f, client.render_interpolation_alpha(1.0f));
}

TEST(NetTransport,
     game_client_continues_interpolation_from_current_rendered_position)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(32, 48);
    fixture.world().tick_count_ = 1u;

    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    actor->setxy(80, 96);
    fixture.world().tick_count_ = 2u;
    const og::sim::WorldSnapshot first_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(first_delta));

    client.poll_messages();

    actor->setxy(128, 144);
    fixture.world().tick_count_ = 3u;
    const og::sim::WorldSnapshot second_delta =
        og::sim::capture_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_delta(second_delta));

    client.poll_messages(0.5f);

    const auto start = client.render_position(actor->entity_id(), 0.0f);
    const auto middle = client.render_position(actor->entity_id(), 0.5f);
    ASSERT_TRUE(start.has_value());
    ASSERT_TRUE(middle.has_value());

    EXPECT_NEAR(56.0f, start->worldx, 0.1f);
    EXPECT_NEAR(72.0f, start->worldy, 0.1f);
    EXPECT_NEAR(56.0f, start->xpos, 0.1f);
    EXPECT_NEAR(72.0f, start->ypos, 0.1f);
    EXPECT_NEAR(92.0f, middle->worldx, 0.1f);
    EXPECT_NEAR(108.0f, middle->worldy, 0.1f);
    EXPECT_NEAR(92.0f, middle->xpos, 0.1f);
    EXPECT_NEAR(108.0f, middle->ypos, 0.1f);
}

TEST(NetTransport, game_client_consumes_explicit_render_alpha_once_per_poll)
{
    MockTransport transport;
    TestGameWorld fixture;

    walker* const actor = fixture.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(32, 48);
    fixture.world().tick_count_ = 1u;

    const og::sim::WorldSnapshot initial =
        og::sim::capture_keyframe_snapshot(fixture.world());
    transport.queue_received(7u, og::sim::serialize_snapshot(initial));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    actor->setxy(80, 96);
    fixture.world().tick_count_ = 2u;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));
    client.poll_messages();

    actor->setxy(128, 144);
    fixture.world().tick_count_ = 3u;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));

    actor->setxy(176, 192);
    fixture.world().tick_count_ = 4u;
    transport.queue_received(
        7u,
        og::sim::serialize_delta(og::sim::capture_snapshot(fixture.world())));

    client.poll_messages(0.5f);

    const auto start = client.render_position(actor->entity_id(), 0.0f);
    const auto middle = client.render_position(actor->entity_id(), 0.5f);
    ASSERT_TRUE(start.has_value());
    ASSERT_TRUE(middle.has_value());

    EXPECT_NEAR(56.0f, start->worldx, 0.1f);
    EXPECT_NEAR(72.0f, start->worldy, 0.1f);
    EXPECT_NEAR(56.0f, start->xpos, 0.1f);
    EXPECT_NEAR(72.0f, start->ypos, 0.1f);
    EXPECT_NEAR(116.0f, middle->worldx, 0.1f);
    EXPECT_NEAR(132.0f, middle->worldy, 0.1f);
    EXPECT_NEAR(116.0f, middle->xpos, 0.1f);
    EXPECT_NEAR(132.0f, middle->ypos, 0.1f);
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
     game_client_processes_queued_transition_messages_after_endgame)
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

    ASSERT_EQ((std::vector<bool>{false, true}), transition_flags);
    ASSERT_TRUE(client.initial_setup().has_value());
    EXPECT_EQ(transition_setup.level_id, client.initial_setup()->level_id);
    EXPECT_EQ(transition_setup.current_scenario,
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

TEST(NetTransport,
     game_client_polls_raw_lobby_messages_when_typed_path_is_unavailable)
{
    MockTransport transport;

    og::sim::LobbyMessage lobby_message;
    lobby_message.payload =
        og::sim::LobbyJoinMessage{make_lobby_player_for_test()};
    const og::sim::LobbyState lobby_state = make_lobby_state_for_test();

    transport.queue_received(
        7u, og::sim::serialize_lobby_message(lobby_message));
    transport.queue_received(
        7u, og::sim::serialize_lobby_state_message(lobby_state));

    og::sim::GameClient client(transport, 7u);
    client.poll_messages();

    ASSERT_EQ(2u, client.last_polled_messages().size());

    const og::sim::TypedReceivedMessage& decoded_message =
        client.last_polled_messages()[0];
    EXPECT_EQ(7u, decoded_message.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage,
              decoded_message.kind);
    ASSERT_NE(nullptr, decoded_message.lobby_message);
    EXPECT_EQ(lobby_message, *decoded_message.lobby_message);

    const og::sim::TypedReceivedMessage& decoded_state =
        client.last_polled_messages()[1];
    EXPECT_EQ(7u, decoded_state.peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyState,
              decoded_state.kind);
    ASSERT_NE(nullptr, decoded_state.lobby_state);
    EXPECT_EQ(lobby_state, *decoded_state.lobby_state);
}

TEST(NetTransport, game_server_disconnects_peers_that_send_malformed_messages)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    transport.queue_received(7u, {0x01, 0x06, 0x01});
    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(server.last_polled_messages().empty());
}

TEST(NetTransport, game_server_disconnects_peers_that_send_unknown_raw_message_types)
{
    TestGameWorld fixture;
    MockTransport transport;
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    transport.set_connected_peers({7u});
    server.poll_incoming_messages();

    std::vector<std::uint8_t> unknown_type =
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{});
    unknown_type[1] = 0xffu;
    transport.queue_received(7u, std::move(unknown_type));

    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(server.last_polled_messages().empty());
}

TEST(NetTransport, game_client_disconnects_when_server_message_is_malformed)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    transport.set_connected_peers({7u});
    transport.queue_received(7u, {0x01, 0x02, 0x01, 0x00, 0xff});

    client.poll_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(client.last_polled_messages().empty());
}

TEST(NetTransport, game_client_disconnects_when_server_message_type_is_unknown)
{
    MockTransport transport;
    og::sim::GameClient client(transport, 7u);

    transport.set_connected_peers({7u});
    std::vector<std::uint8_t> unknown_type =
        og::sim::serialize_heartbeat_message(og::sim::HeartbeatMessage{});
    unknown_type[1] = 0xffu;
    transport.queue_received(7u, std::move(unknown_type));

    client.poll_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{7u}), transport.disconnected_peers());
    EXPECT_TRUE(client.last_polled_messages().empty());
}

} // namespace
