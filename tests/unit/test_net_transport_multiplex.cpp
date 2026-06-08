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

class FakeTypedTransport final : public og::sim::ITransport
{
public:
    void connect_peer(og::sim::PeerId peer_id)
    {
        peers_.push_back(peer_id);
    }

    void send(og::sim::PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        raw_send_peer = peer_id;
        raw_send_size = len;
        if (data != nullptr && len > 0)
            raw_send_first = data[0];
    }

    bool supports_typed_messages() const noexcept override { return true; }
    void send_snapshot(og::sim::PeerId peer_id,
                       std::shared_ptr<og::sim::WorldSnapshot>) override
    {
        snapshot_peer = peer_id;
    }
    void send_delta_snapshot(og::sim::PeerId peer_id,
                             std::shared_ptr<og::sim::WorldSnapshot>) override
    {
        delta_peer = peer_id;
    }
    void send_input(og::sim::PeerId peer_id,
                    std::shared_ptr<InputState>,
                    std::uint32_t tick) override
    {
        input_peer = peer_id;
        input_tick = tick;
    }
    void send_sim_event_batch(og::sim::PeerId peer_id,
                              std::shared_ptr<og::sim::SimEventBatch>) override
    {
        sim_events_peer = peer_id;
    }
    void send_game_flow_event_batch(og::sim::PeerId peer_id,
                                    std::shared_ptr<og::sim::SimEventBatch>) override
    {
        game_flow_peer = peer_id;
    }
    void send_lobby_message(og::sim::PeerId peer_id,
                            std::shared_ptr<og::sim::LobbyMessage>) override
    {
        lobby_message_peer = peer_id;
    }
    void send_lobby_state(og::sim::PeerId peer_id,
                          std::shared_ptr<og::sim::LobbyState>) override
    {
        lobby_state_peer = peer_id;
    }
    void send_initial_setup(og::sim::PeerId peer_id,
                            std::shared_ptr<og::sim::InitialSetupMessage>) override
    {
        initial_setup_peer = peer_id;
    }
    void send_hello(og::sim::PeerId peer_id,
                    std::shared_ptr<og::sim::HelloMessage>) override
    {
        hello_peer = peer_id;
    }
    void send_client_ready(og::sim::PeerId peer_id,
                           std::shared_ptr<og::sim::ClientReadyMessage>) override
    {
        client_ready_peer = peer_id;
    }
    void send_keyframe_request(og::sim::PeerId peer_id,
                               std::shared_ptr<og::sim::KeyframeRequestMessage>) override
    {
        keyframe_peer = peer_id;
    }
    void send_heartbeat(og::sim::PeerId peer_id,
                        std::shared_ptr<og::sim::HeartbeatMessage>) override
    {
        heartbeat_peer = peer_id;
    }
    void send_exit_prompt_broadcast(
        og::sim::PeerId peer_id,
        std::shared_ptr<og::sim::ExitPromptBroadcastMessage>) override
    {
        exit_broadcast_peer = peer_id;
    }
    void send_exit_prompt_response(
        og::sim::PeerId peer_id,
        std::shared_ptr<og::sim::ExitPromptResponseMessage>) override
    {
        exit_response_peer = peer_id;
    }
    void send_pause_broadcast(og::sim::PeerId peer_id,
                              std::shared_ptr<og::sim::PauseBroadcastMessage>) override
    {
        pause_broadcast_peer = peer_id;
    }
    void send_pause_response(og::sim::PeerId peer_id,
                             std::shared_ptr<og::sim::PauseResponseMessage>) override
    {
        pause_response_peer = peer_id;
    }
    void send_control_change(og::sim::PeerId peer_id,
                             std::shared_ptr<og::sim::ControlChangeMessage>) override
    {
        control_change_peer = peer_id;
    }
    void send_snapshot_hash_check(
        og::sim::PeerId peer_id,
        std::shared_ptr<og::sim::SnapshotHashCheckMessage>) override
    {
        hash_check_peer = peer_id;
    }

    std::vector<og::sim::ReceivedMessage> poll() override { return {}; }
    void accept_connections() override {}
    void disconnect(og::sim::PeerId peer_id) override { disconnected_peer = peer_id; }
    std::vector<og::sim::PeerId> connected_peers() const override { return peers_; }

    og::sim::PeerId raw_send_peer = 0;
    std::size_t raw_send_size = 0;
    std::uint8_t raw_send_first = 0;
    og::sim::PeerId snapshot_peer = 0;
    og::sim::PeerId delta_peer = 0;
    og::sim::PeerId input_peer = 0;
    std::uint32_t input_tick = 0;
    og::sim::PeerId sim_events_peer = 0;
    og::sim::PeerId game_flow_peer = 0;
    og::sim::PeerId lobby_message_peer = 0;
    og::sim::PeerId lobby_state_peer = 0;
    og::sim::PeerId initial_setup_peer = 0;
    og::sim::PeerId hello_peer = 0;
    og::sim::PeerId client_ready_peer = 0;
    og::sim::PeerId keyframe_peer = 0;
    og::sim::PeerId heartbeat_peer = 0;
    og::sim::PeerId exit_broadcast_peer = 0;
    og::sim::PeerId exit_response_peer = 0;
    og::sim::PeerId pause_broadcast_peer = 0;
    og::sim::PeerId pause_response_peer = 0;
    og::sim::PeerId control_change_peer = 0;
    og::sim::PeerId hash_check_peer = 0;
    og::sim::PeerId disconnected_peer = 0;

private:
    std::vector<og::sim::PeerId> peers_;
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

TEST(NetTransportMultiplex, typed_forwarding_methods_route_to_native_peer)
{
    auto typed = std::make_shared<FakeTypedTransport>();
    typed->connect_peer(44u);
    og::sim::MultiplexTransport transport({typed});

    const std::vector<og::sim::PeerId> public_peers = transport.connected_peers();
    ASSERT_EQ(1u, public_peers.size());
    const og::sim::PeerId public_peer = public_peers.front();

    const std::array<std::uint8_t, 2> raw = {0x12, 0x34};
    transport.send(public_peer, raw.data(), raw.size());
    transport.send_snapshot(public_peer, {});
    transport.send_delta_snapshot(public_peer, {});
    transport.send_input(public_peer, std::make_shared<InputState>(), 1234u);
    transport.send_sim_event_batch(public_peer, {});
    transport.send_game_flow_event_batch(public_peer, {});
    transport.send_lobby_message(public_peer, std::make_shared<og::sim::LobbyMessage>());
    transport.send_lobby_state(public_peer, std::make_shared<og::sim::LobbyState>());
    transport.send_initial_setup(public_peer,
                                 std::make_shared<og::sim::InitialSetupMessage>());
    transport.send_hello(public_peer, std::make_shared<og::sim::HelloMessage>());
    transport.send_client_ready(public_peer,
                                std::make_shared<og::sim::ClientReadyMessage>());
    transport.send_keyframe_request(public_peer,
                                    std::make_shared<og::sim::KeyframeRequestMessage>());
    transport.send_heartbeat(public_peer, std::make_shared<og::sim::HeartbeatMessage>());
    transport.send_exit_prompt_broadcast(
        public_peer, std::make_shared<og::sim::ExitPromptBroadcastMessage>());
    transport.send_exit_prompt_response(
        public_peer, std::make_shared<og::sim::ExitPromptResponseMessage>());
    transport.send_pause_broadcast(public_peer,
                                   std::make_shared<og::sim::PauseBroadcastMessage>());
    transport.send_pause_response(public_peer,
                                  std::make_shared<og::sim::PauseResponseMessage>());
    transport.send_control_change(public_peer,
                                  std::make_shared<og::sim::ControlChangeMessage>());
    transport.send_snapshot_hash_check(
        public_peer, std::make_shared<og::sim::SnapshotHashCheckMessage>());
    transport.disconnect(public_peer);

    EXPECT_EQ(44u, typed->raw_send_peer);
    EXPECT_EQ(2u, typed->raw_send_size);
    EXPECT_EQ(0x12u, typed->raw_send_first);
    EXPECT_EQ(44u, typed->snapshot_peer);
    EXPECT_EQ(44u, typed->delta_peer);
    EXPECT_EQ(44u, typed->input_peer);
    EXPECT_EQ(1234u, typed->input_tick);
    EXPECT_EQ(44u, typed->sim_events_peer);
    EXPECT_EQ(44u, typed->game_flow_peer);
    EXPECT_EQ(44u, typed->lobby_message_peer);
    EXPECT_EQ(44u, typed->lobby_state_peer);
    EXPECT_EQ(44u, typed->initial_setup_peer);
    EXPECT_EQ(44u, typed->hello_peer);
    EXPECT_EQ(44u, typed->client_ready_peer);
    EXPECT_EQ(44u, typed->keyframe_peer);
    EXPECT_EQ(44u, typed->heartbeat_peer);
    EXPECT_EQ(44u, typed->exit_broadcast_peer);
    EXPECT_EQ(44u, typed->exit_response_peer);
    EXPECT_EQ(44u, typed->pause_broadcast_peer);
    EXPECT_EQ(44u, typed->pause_response_peer);
    EXPECT_EQ(44u, typed->control_change_peer);
    EXPECT_EQ(44u, typed->hash_check_peer);
    EXPECT_EQ(44u, typed->disconnected_peer);

    transport.send(999u, raw.data(), raw.size());
    transport.send_lobby_state(999u, std::make_shared<og::sim::LobbyState>());
    transport.disconnect(999u);
}
