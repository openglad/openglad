#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/net_transport_multiplex.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../test_game_world_fixture.h"

namespace {

og::sim::LobbyMessage make_join_message(const char* name)
{
    og::sim::LobbyPlayer player;
    player.name = name;
    player.team = 0;

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyJoinMessage{.player = std::move(player)};
    return message;
}

og::sim::LobbyState make_lobby_state(const char* campaign_id)
{
    og::sim::LobbyState state;
    state.settings.campaign_id = campaign_id;
    state.settings.scenario_id = 1;
    state.settings.difficulty = 1;
    state.settings.allied_mode = 1;
    return state;
}

class FakeRawTransport final : public og::sim::ITransport
{
public:
    void connect_peer(og::sim::PeerId peer_id)
    {
        if (std::find(peers_.begin(), peers_.end(), peer_id) != peers_.end())
            return;
        peers_.push_back(peer_id);
        std::sort(peers_.begin(), peers_.end());
    }

    void enqueue_raw(og::sim::PeerId peer_id, std::span<const std::uint8_t> bytes)
    {
        connect_peer(peer_id);
        received_messages_.push_back(og::sim::ReceivedMessage{
            .peer_id = peer_id,
            .data = std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
        });
    }

    [[nodiscard]] const std::vector<og::sim::ReceivedMessage>& sent_messages() const
    {
        return sent_messages_;
    }

    [[nodiscard]] std::size_t broadcast_call_count() const noexcept
    {
        return broadcast_call_count_;
    }

    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        og::sim::ReceivedMessage message;
        message.peer_id = peer_id;
        if (data != nullptr && len != 0)
            message.data.assign(data, data + len);
        sent_messages_.push_back(std::move(message));
    }

    void broadcast(const std::uint8_t* data, std::size_t len) override
    {
        ++broadcast_call_count_;
        for (const og::sim::PeerId peer_id : peers_)
            send(peer_id, data, len);
    }

    [[nodiscard]] std::vector<og::sim::ReceivedMessage> poll() override
    {
        std::vector<og::sim::ReceivedMessage> drained =
            std::move(received_messages_);
        received_messages_.clear();
        return drained;
    }

    void accept_connections() override {}

    void disconnect(og::sim::PeerId peer_id) override
    {
        disconnected_peers_.push_back(peer_id);
        peers_.erase(std::remove(peers_.begin(), peers_.end(), peer_id),
                     peers_.end());
    }

    [[nodiscard]] std::vector<og::sim::PeerId> connected_peers() const override
    {
        return peers_;
    }

    [[nodiscard]] const std::vector<og::sim::PeerId>&
    disconnected_peers() const noexcept
    {
        return disconnected_peers_;
    }

private:
    std::vector<og::sim::PeerId> peers_;
    std::vector<og::sim::ReceivedMessage> received_messages_;
    std::vector<og::sim::ReceivedMessage> sent_messages_;
    std::vector<og::sim::PeerId> disconnected_peers_;
    std::size_t broadcast_call_count_ = 0;
};

} // namespace

TEST(NetTransportMultiplex, poll_typed_assigns_distinct_public_peer_ids)
{
    auto server_a = og::sim::InProcessTransport::create_server();
    auto server_b = og::sim::InProcessTransport::create_server();
    server_a->accept_connections();
    server_b->accept_connections();

    const auto client_a = server_a->create_client_transport();
    const auto client_b = server_b->create_client_transport();

    og::sim::MultiplexTransport transport({
        std::shared_ptr<og::sim::ITransport>(server_a),
        std::shared_ptr<og::sim::ITransport>(server_b),
    });

    client_a->send_lobby_message(
        client_a->local_peer_id(),
        std::make_shared<og::sim::LobbyMessage>(make_join_message("A")));
    client_b->send_lobby_message(
        client_b->local_peer_id(),
        std::make_shared<og::sim::LobbyMessage>(make_join_message("B")));

    const std::vector<og::sim::TypedReceivedMessage> messages =
        transport.poll_typed();
    ASSERT_EQ(2u, messages.size());
    ASSERT_TRUE(messages[0].lobby_message);
    ASSERT_TRUE(messages[1].lobby_message);
    EXPECT_NE(messages[0].peer_id, messages[1].peer_id);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage,
              messages[0].kind);
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage,
              messages[1].kind);
}

TEST(NetTransportMultiplex, typed_send_routes_to_matching_underlying_transport)
{
    auto server_a = og::sim::InProcessTransport::create_server();
    auto server_b = og::sim::InProcessTransport::create_server();
    server_a->accept_connections();
    server_b->accept_connections();

    const auto client_a = server_a->create_client_transport();
    const auto client_b = server_b->create_client_transport();

    og::sim::MultiplexTransport transport({
        std::shared_ptr<og::sim::ITransport>(server_a),
        std::shared_ptr<og::sim::ITransport>(server_b),
    });

    client_a->send_lobby_message(
        client_a->local_peer_id(),
        std::make_shared<og::sim::LobbyMessage>(make_join_message("A")));
    client_b->send_lobby_message(
        client_b->local_peer_id(),
        std::make_shared<og::sim::LobbyMessage>(make_join_message("B")));

    const std::vector<og::sim::TypedReceivedMessage> messages =
        transport.poll_typed();
    ASSERT_EQ(2u, messages.size());

    og::sim::PeerId public_peer_a = 0;
    og::sim::PeerId public_peer_b = 0;
    for (const og::sim::TypedReceivedMessage& message : messages)
    {
        ASSERT_TRUE(message.lobby_message);
        const auto& join =
            std::get<og::sim::LobbyJoinMessage>(message.lobby_message->payload);
        if (join.player.name == "A")
            public_peer_a = message.peer_id;
        if (join.player.name == "B")
            public_peer_b = message.peer_id;
    }
    ASSERT_NE(0u, public_peer_a);
    ASSERT_NE(0u, public_peer_b);

    transport.send_lobby_state(
        public_peer_a,
        std::make_shared<og::sim::LobbyState>(make_lobby_state("campaign-a")));
    transport.send_lobby_state(
        public_peer_b,
        std::make_shared<og::sim::LobbyState>(make_lobby_state("campaign-b")));

    const std::vector<og::sim::TypedReceivedMessage> recv_a =
        client_a->poll_typed();
    const std::vector<og::sim::TypedReceivedMessage> recv_b =
        client_b->poll_typed();
    ASSERT_EQ(1u, recv_a.size());
    ASSERT_EQ(1u, recv_b.size());
    ASSERT_TRUE(recv_a.front().lobby_state);
    ASSERT_TRUE(recv_b.front().lobby_state);
    EXPECT_EQ("campaign-a", recv_a.front().lobby_state->settings.campaign_id);
    EXPECT_EQ("campaign-b", recv_b.front().lobby_state->settings.campaign_id);
}

TEST(NetTransportMultiplex, disconnect_removes_public_peer_mapping)
{
    auto server_a = og::sim::InProcessTransport::create_server();
    auto server_b = og::sim::InProcessTransport::create_server();
    server_a->accept_connections();
    server_b->accept_connections();

    const auto client_a = server_a->create_client_transport();
    const auto client_b = server_b->create_client_transport();

    og::sim::MultiplexTransport transport({
        std::shared_ptr<og::sim::ITransport>(server_a),
        std::shared_ptr<og::sim::ITransport>(server_b),
    });

    const std::vector<og::sim::PeerId> before = transport.connected_peers();
    ASSERT_EQ(2u, before.size());

    transport.disconnect(before.front());

    const std::vector<og::sim::PeerId> after = transport.connected_peers();
    ASSERT_EQ(1u, after.size());
    EXPECT_EQ(before.back(), after.front());
    EXPECT_TRUE(client_a->connected_peers().empty() || client_b->connected_peers().empty());
}

TEST(NetTransportMultiplex, poll_typed_decodes_raw_messages_from_untyped_child)
{
    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    const auto client = server->create_client_transport();
    auto raw_transport = std::make_shared<FakeRawTransport>();
    raw_transport->connect_peer(77u);

    og::sim::MultiplexTransport transport({
        std::shared_ptr<og::sim::ITransport>(server),
        raw_transport,
    });

    client->send_lobby_message(
        client->local_peer_id(),
        std::make_shared<og::sim::LobbyMessage>(make_join_message("local")));

    InputState input;
    input.quit_requested = true;
    input.players[0].pressed[0] = true;
    const auto input_bytes = og::sim::serialize_input(42u, input);
    raw_transport->enqueue_raw(77u, input_bytes);

    ASSERT_TRUE(transport.supports_typed_messages());
    const std::vector<og::sim::TypedReceivedMessage> messages =
        transport.poll_typed();
    ASSERT_EQ(2u, messages.size());

    bool saw_local_join = false;
    bool saw_remote_input = false;
    for (const og::sim::TypedReceivedMessage& message : messages)
    {
        if (message.kind == og::sim::TypedReceivedMessageKind::LobbyMessage)
        {
            ASSERT_TRUE(message.lobby_message);
            const auto& join = std::get<og::sim::LobbyJoinMessage>(
                message.lobby_message->payload);
            EXPECT_EQ("local", join.player.name);
            saw_local_join = true;
            continue;
        }

        if (message.kind == og::sim::TypedReceivedMessageKind::Input)
        {
            ASSERT_TRUE(message.input);
            EXPECT_TRUE(message.input->quit_requested);
            EXPECT_TRUE(message.input->players[0].pressed[0]);
            EXPECT_EQ(42u, message.tick);
            saw_remote_input = true;
        }
    }

    EXPECT_TRUE(saw_local_join);
    EXPECT_TRUE(saw_remote_input);
}

TEST(NetTransportMultiplex,
     poll_typed_surfaces_malformed_raw_messages_from_untyped_child)
{
    auto raw_transport = std::make_shared<FakeRawTransport>();
    raw_transport->connect_peer(77u);
    const std::array<std::uint8_t, 3> malformed = {0x01, 0x02, 0x03};

    og::sim::MultiplexTransport transport({raw_transport});

    raw_transport->enqueue_raw(77u, malformed);

    const std::vector<og::sim::TypedReceivedMessage> messages =
        transport.poll_typed();
    ASSERT_EQ(1u, messages.size());
    EXPECT_EQ(og::sim::TypedReceivedMessageKind::Malformed,
              messages.front().kind);
    EXPECT_NE(0u, messages.front().peer_id);
}

TEST(NetTransportMultiplex,
     game_server_disconnects_peer_that_sends_malformed_raw_message_via_multiplex)
{
    TestGameWorld fixture;
    auto raw_transport = std::make_shared<FakeRawTransport>();
    raw_transport->connect_peer(77u);
    const std::array<std::uint8_t, 3> malformed = {0x01, 0x02, 0x03};

    og::sim::MultiplexTransport transport({raw_transport});
    og::sim::GameServer server(fixture.world(), fixture.events, transport);

    raw_transport->enqueue_raw(77u, malformed);

    server.poll_incoming_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{77u}),
              raw_transport->disconnected_peers());
    EXPECT_TRUE(server.last_polled_messages().empty());
}

TEST(NetTransportMultiplex,
     game_client_disconnects_when_server_sends_malformed_raw_message_via_multiplex)
{
    auto raw_transport = std::make_shared<FakeRawTransport>();
    raw_transport->connect_peer(77u);
    const std::array<std::uint8_t, 3> malformed = {0x01, 0x02, 0x03};

    og::sim::MultiplexTransport transport({raw_transport});
    const std::vector<og::sim::PeerId> peers = transport.connected_peers();
    ASSERT_EQ(1u, peers.size());

    og::sim::GameClient client(transport, peers.front());
    raw_transport->enqueue_raw(77u, malformed);

    client.poll_messages();

    EXPECT_EQ((std::vector<og::sim::PeerId>{77u}),
              raw_transport->disconnected_peers());
    EXPECT_TRUE(client.last_polled_messages().empty());
}

TEST(NetTransportMultiplex, typed_send_routes_to_untyped_underlying_transport)
{
    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    const auto client = server->create_client_transport();
    auto raw_transport = std::make_shared<FakeRawTransport>();
    raw_transport->connect_peer(77u);

    og::sim::MultiplexTransport transport({
        std::shared_ptr<og::sim::ITransport>(server),
        raw_transport,
    });

    client->send_lobby_message(
        client->local_peer_id(),
        std::make_shared<og::sim::LobbyMessage>(make_join_message("local")));
    const auto raw_join_bytes =
        og::sim::serialize_lobby_message(make_join_message("remote"));
    raw_transport->enqueue_raw(77u, raw_join_bytes);

    const std::vector<og::sim::TypedReceivedMessage> messages =
        transport.poll_typed();
    ASSERT_EQ(2u, messages.size());

    og::sim::PeerId local_public_peer = 0;
    og::sim::PeerId remote_public_peer = 0;
    for (const og::sim::TypedReceivedMessage& message : messages)
    {
        ASSERT_EQ(og::sim::TypedReceivedMessageKind::LobbyMessage, message.kind);
        ASSERT_TRUE(message.lobby_message);
        const auto& join =
            std::get<og::sim::LobbyJoinMessage>(message.lobby_message->payload);
        if (join.player.name == "local")
            local_public_peer = message.peer_id;
        if (join.player.name == "remote")
            remote_public_peer = message.peer_id;
    }

    ASSERT_NE(0u, local_public_peer);
    ASSERT_NE(0u, remote_public_peer);
    EXPECT_NE(local_public_peer, remote_public_peer);

    transport.send_lobby_state(
        local_public_peer,
        std::make_shared<og::sim::LobbyState>(make_lobby_state("campaign-local")));
    transport.send_lobby_state(
        remote_public_peer,
        std::make_shared<og::sim::LobbyState>(make_lobby_state("campaign-remote")));

    const std::vector<og::sim::TypedReceivedMessage> local_messages =
        client->poll_typed();
    ASSERT_EQ(1u, local_messages.size());
    ASSERT_TRUE(local_messages.front().lobby_state);
    EXPECT_EQ("campaign-local",
              local_messages.front().lobby_state->settings.campaign_id);

    ASSERT_EQ(1u, raw_transport->sent_messages().size());
    EXPECT_EQ(77u, raw_transport->sent_messages().front().peer_id);
    const auto decoded =
        og::sim::deserialize_lobby_state_message(raw_transport->sent_messages().front().data);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ("campaign-remote", decoded->settings.campaign_id);
}

TEST(NetTransportMultiplex, broadcast_calls_each_underlying_transport_once)
{
    auto raw_a = std::make_shared<FakeRawTransport>();
    auto raw_b = std::make_shared<FakeRawTransport>();
    raw_a->connect_peer(11u);
    raw_a->connect_peer(12u);
    raw_b->connect_peer(21u);

    og::sim::MultiplexTransport transport({raw_a, raw_b});
    const std::array<std::uint8_t, 2> payload = {0xaa, 0x55};

    transport.broadcast(payload);

    EXPECT_EQ(1u, raw_a->broadcast_call_count());
    EXPECT_EQ(1u, raw_b->broadcast_call_count());
    ASSERT_EQ(2u, raw_a->sent_messages().size());
    ASSERT_EQ(1u, raw_b->sent_messages().size());
    EXPECT_EQ((std::vector<std::uint8_t>{0xaa, 0x55}),
              raw_a->sent_messages().front().data);
    EXPECT_EQ((std::vector<std::uint8_t>{0xaa, 0x55}),
              raw_b->sent_messages().front().data);
}
