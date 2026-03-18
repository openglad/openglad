#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/net_transport_multiplex.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

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
