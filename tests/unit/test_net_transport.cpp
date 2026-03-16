#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

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
